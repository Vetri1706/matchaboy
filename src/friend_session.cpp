#include "friend_session.hpp"
#include "gba_core.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include "dmg/linked_pair.hpp"
#include "dmg/snapshot.hpp"
#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace matcha {
namespace {
constexpr unsigned input_delay=6;
constexpr std::uint8_t version=1;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void put(std::vector<std::uint8_t> &out,std::uint64_t n,unsigned bytes){
    for(unsigned i=bytes;i>0;--i)out.push_back(static_cast<std::uint8_t>(n>>((i-1)*8)));
}
std::uint64_t get(std::span<const std::uint8_t> bytes,unsigned offset,unsigned n){
    require(offset+n<=bytes.size(),"Truncated friend session message.");
    std::uint64_t result=0;for(unsigned i=0;i<n;++i)result=(result<<8)|bytes[offset+i];return result;
}
std::uint16_t gba_buttons(std::uint16_t mask){
    constexpr std::array<unsigned,10> mapping{4,5,6,7,0,1,2,3,9,8};std::uint16_t result=0;
    for(unsigned i=0;i<mapping.size();++i)if(mask&(1U<<i))result|=static_cast<std::uint16_t>(1U<<mapping[i]);
    return result;
}
void snapshot_ok(dmg::SnapshotResult result){
    if(result!=dmg::SnapshotResult::Ok)throw std::runtime_error(dmg::snapshot_result_name(result));
}
}
struct FriendSession::Impl {
    FriendTransport transport;
    std::filesystem::path rom_path;
    std::vector<std::uint8_t> rom,hello;
    std::array<std::uint8_t,32> identity{};
    unsigned side;
    dmg::Bus *local_bus;
    dmg::Cpu *local_cpu;
    GbaCore *local_gba;
    std::unique_ptr<dmg::Bus> remote_bus;
    std::unique_ptr<dmg::Cpu> remote_cpu;
    std::unique_ptr<GbaCore> remote_gba;
    // Destruction detaches the cable before deleting either console.
    std::unique_ptr<dmg::LinkedPair> gb_pair;
    std::unique_ptr<GbaLinkedPair> gba_pair;
    std::unique_ptr<dmg::MachineSnapshot> snapshot=std::make_unique<dmg::MachineSnapshot>();
    std::map<std::uint64_t,std::uint16_t> local_inputs,remote_inputs;
    using Hashes=std::array<std::uint64_t,3>;
    std::map<std::uint64_t,Hashes> hashes,peer_hashes;
    std::deque<std::vector<std::uint8_t>> control;
    std::vector<std::int16_t> audio;
    std::uint64_t frame{},next_input{},received_input{},received_hash=60,verified{};
    bool sent_hello=false,ready=false,stopped=false,ever_connected=false;
    std::string error;

    Impl(const FriendTransportOptions &options,const std::filesystem::path &path,dmg::Bus *bus,dmg::Cpu *cpu,GbaCore *gba)
      :transport(options),rom_path(path),side(options.host?0U:1U),local_bus(bus),local_cpu(cpu),local_gba(gba){
        require((gba!=nullptr)!=(bus!=nullptr&&cpu!=nullptr),"Choose one local console for friend play.");
        std::ifstream in(path,std::ios::binary);require(bool(in),"Cannot read friend session ROM.");
        rom.assign(std::istreambuf_iterator<char>(in),{});
        require(rom.size()>=(gba?0xC0U:0x150U) && rom.size()<=32*1024*1024,"Invalid friend session ROM size.");
        identity=dmg::snapshot_rom_identity(rom);
        std::vector<std::uint8_t> state;
        if(gba){
            require(gba->fresh(),"Start friend play from a freshly loaded game.");
            state=gba->export_save();gba->enable_audio();
        }else{
            require(bus->cycles()==0,"Start friend play from a freshly loaded game.");
            bus->capture_serial_output=false;bus->apu.set_sample_rate(48000);bus->autopsy=nullptr;
            snapshot_ok(dmg::save_snapshot(*cpu,*bus,*snapshot));
            state.assign(snapshot->data.begin(),snapshot->data.begin()+snapshot->size);
        }
        hello={1,version,static_cast<std::uint8_t>(gba?1:0),static_cast<std::uint8_t>(side),input_delay};
        hello.insert(hello.end(),identity.begin(),identity.end());put(hello,state.size(),4);
        hello.insert(hello.end(),state.begin(),state.end());
        require(hello.size()<=256*1024,"Initial save exceeds the friend transport limit.");
    }
    ~Impl(){transport.close();}
    void stop(){transport.close();stopped=true;gba_pair.reset();gb_pair.reset();audio.clear();}
    void accept_hello(std::span<const std::uint8_t> bytes){
        require(!ready && bytes.size()>=41,"Unexpected friend handshake.");
        require(bytes[1]==version && bytes[2]==(local_gba?1:0) && bytes[3]==1-side && bytes[4]==input_delay,
                "Friend uses an incompatible app or console configuration.");
        require(std::equal(identity.begin(),identity.end(),bytes.begin()+5),"Both friends must open the exact same ROM version.");
        require(get(bytes,37,4)==bytes.size()-41,"Invalid friend save length.");
        const auto state=bytes.subspan(41);
        if(local_gba){
            remote_gba=std::make_unique<GbaCore>(rom_path);remote_gba->import_save(state);remote_gba->enable_audio();
            gba_pair=side==0?std::make_unique<GbaLinkedPair>(*local_gba,*remote_gba):std::make_unique<GbaLinkedPair>(*remote_gba,*local_gba);
        }else{
            remote_bus=std::make_unique<dmg::Bus>(rom);remote_cpu=std::make_unique<dmg::Cpu>(*remote_bus);
            require(state.size()<=snapshot->data.size(),"Invalid Game Boy friend snapshot size.");
            std::copy(state.begin(),state.end(),snapshot->data.begin());snapshot->size=static_cast<std::uint32_t>(state.size());
            // Derive the snapshot container checksum from the wire-validated
            // bytes. load_snapshot also validates every encoded field/bound.
            snapshot->checksum=dmg::snapshot_hash(*snapshot);
            snapshot_ok(dmg::load_snapshot(*remote_cpu,*remote_bus,*snapshot));
            remote_bus->capture_serial_output=false;remote_bus->autopsy=nullptr;
            gb_pair=side==0?std::make_unique<dmg::LinkedPair>(*local_bus,*local_cpu,*remote_bus,*remote_cpu)
                           :std::make_unique<dmg::LinkedPair>(*remote_bus,*remote_cpu,*local_bus,*local_cpu);
        }
        ready=true;
    }
    void compare(){
        for(auto it=peer_hashes.begin();it!=peer_hashes.end();){
            auto ours=hashes.find(it->first);if(ours==hashes.end()){++it;continue;}
            if(ours->second!=it->second)throw std::runtime_error("Link desync at frame "+std::to_string(it->first)+". Session stopped; reconnect before continuing.");
            verified=std::max(verified,it->first);
            hashes.erase(ours);it=peer_hashes.erase(it);
        }
        require(hashes.size()<64 && peer_hashes.size()<64,"Friend state verification timed out.");
    }
    void messages(){
        std::vector<std::uint8_t> bytes;
        while(transport.receive(bytes)){
            require(!bytes.empty(),"Empty friend session message.");
            if(bytes[0]==1)accept_hello(bytes);
            else if(bytes[0]==2){
                require(ready&&bytes.size()==11,"Invalid controller input message.");
                const auto n=get(bytes,1,8),buttons=get(bytes,9,2);
                require(n==received_input && n<=frame+120 && buttons<1024,"Friend controller input is out of order or out of bounds.");
                remote_inputs.emplace(n,static_cast<std::uint16_t>(buttons));++received_input;
            }else if(bytes[0]==3){
                require(ready&&bytes.size()==33,"Invalid friend state verification message.");
                const auto n=get(bytes,1,8);
                require(n==received_hash && n<=frame+120,"Friend state verification is out of bounds.");
                received_hash+=60;
                peer_hashes.emplace(n,Hashes{get(bytes,9,8),get(bytes,17,8),get(bytes,25,8)});
            }else throw std::runtime_error("Unknown friend session message.");
        }
        compare();
    }
    void sound(){
        std::array<std::int16_t,8192> samples{};
        while(const auto n=local_gba?local_gba->drain_audio(samples):local_bus->apu.drain_samples(samples)){
            if(audio.size()+n<=192000)audio.insert(audio.end(),samples.begin(),samples.begin()+static_cast<std::ptrdiff_t>(n));
        }
        while(local_gba?remote_gba->drain_audio(samples):remote_bus->apu.drain_samples(samples)){}
    }
    Hashes digest(){
        Hashes result{};
        if(local_gba){result[side]=local_gba->digest();result[1-side]=remote_gba->digest();result[2]=gba_pair->digest();}
        else{
            snapshot_ok(dmg::save_snapshot(*local_cpu,*local_bus,*snapshot));result[side]=dmg::snapshot_hash(*snapshot);
            snapshot_ok(dmg::save_snapshot(*remote_cpu,*remote_bus,*snapshot));result[1-side]=dmg::snapshot_hash(*snapshot);
            result[2]=gb_pair->edges();
        }
        return result;
    }
    void service(){
        if(stopped)return;
        transport.poll();
        if(!transport.connected()){
            if(ever_connected||transport.finished()){error=transport.status();stop();}
            return;
        }
        ever_connected=true;
        if(!sent_hello)sent_hello=transport.send(hello);
        messages();
        while(!control.empty()&&transport.send(control.front()))control.pop_front();
    }
    bool advance(std::uint16_t buttons){
        service();if(stopped||!ready||!sent_hello)return false;
        while(next_input<=frame+input_delay){
            const auto value=static_cast<std::uint16_t>(next_input<input_delay?0:buttons&1023);
            std::vector<std::uint8_t> packet{2};put(packet,next_input,8);put(packet,value,2);
            if(!transport.send(packet))break;
            local_inputs.emplace(next_input,value);++next_input;
        }
        if(!local_inputs.contains(frame)||!remote_inputs.contains(frame))return false;
        if(local_gba){
            local_gba->set_buttons(gba_buttons(local_inputs.at(frame)));remote_gba->set_buttons(gba_buttons(remote_inputs.at(frame)));
            gba_pair->run_frame();
        }else{
            local_bus->autopsy=nullptr;remote_bus->autopsy=nullptr;
            local_bus->set_buttons(static_cast<std::uint8_t>(local_inputs.at(frame)));
            remote_bus->set_buttons(static_cast<std::uint8_t>(remote_inputs.at(frame)));gb_pair->run_frame();
        }
        local_inputs.erase(frame);remote_inputs.erase(frame);++frame;sound();
        if(frame%60==0){
            const auto hash=digest();hashes.emplace(frame,hash);std::vector<std::uint8_t> packet{3};put(packet,frame,8);
            for(auto part:hash)put(packet,part,8);
            control.push_back(std::move(packet));compare();
        }
        return true;
    }
};
FriendSession::FriendSession(const FriendTransportOptions &options,const std::filesystem::path &rom,dmg::Bus *bus,dmg::Cpu *cpu,GbaCore *gba)
 :impl_(std::make_unique<Impl>(options,rom,bus,cpu,gba)){}
FriendSession::~FriendSession()=default;
bool FriendSession::advance(std::uint16_t buttons){
    try{return impl_->advance(buttons);}catch(const std::exception &e){impl_->error=e.what();impl_->stop();return false;}
}
void FriendSession::service(){
    try{impl_->service();}catch(const std::exception &e){impl_->error=e.what();impl_->stop();}
}
std::string FriendSession::status()const{
    if(!impl_->error.empty())return impl_->error;
    if(impl_->stopped)return "Disconnected";
    if(!impl_->ready)return impl_->transport.connected()?"Checking game and exchanging saved progress…":impl_->transport.status();
    return "Linked · Player "+std::to_string(impl_->side+1)+" · Frame "+std::to_string(impl_->frame)+
        (impl_->remote_inputs.contains(impl_->frame)?"":" · Waiting for friend");
}
bool FriendSession::connected()const{return impl_->ready&&!impl_->stopped&&impl_->transport.connected();}
std::string FriendSession::diagnostics()const{
    const auto network=impl_->transport.diagnostics();std::ostringstream out;
    out<<"Matchaboy friend diagnostics\nStatus: "<<status()
       <<"\nConsole: "<<(impl_->local_gba?"GBA":"GB")<<" | Player: "<<impl_->side+1
       <<"\nFrame: "<<impl_->frame<<" | Last verified frame: "<<impl_->verified
       <<"\nInputs queued: local "<<impl_->local_inputs.size()<<", peer "<<impl_->remote_inputs.size()
       <<"\nInput delay: "<<input_delay<<" frames"
       <<"\nPackets: sent "<<network.sent_packets<<", received "<<network.received_packets
       <<", retransmissions "<<network.retransmissions
       <<"\nTime without peer traffic: "<<network.silence_ms<<" ms";
    return out.str();
}
bool FriendSession::finished()const{return impl_->stopped;}
std::uint64_t FriendSession::frames()const{return impl_->frame;}
std::uint64_t FriendSession::verified_frames()const{return impl_->verified;}
std::uint16_t FriendSession::port()const{return impl_->transport.local_port();}
std::size_t FriendSession::drain_audio(std::span<std::int16_t> out){
    const auto count=std::min(out.size(),impl_->audio.size());std::copy_n(impl_->audio.begin(),count,out.begin());
    impl_->audio.erase(impl_->audio.begin(),impl_->audio.begin()+static_cast<std::ptrdiff_t>(count));return count;
}
void FriendSession::close(){impl_->stop();}
std::string FriendSession::make_room_code(){
    std::random_device random;std::string result;constexpr char digits[]="0123456789abcdef";
    for(unsigned i=0;i<4;++i){const auto n=random();for(unsigned j=0;j<8;++j)result+=digits[(n>>(j*4))&15U];}return result;
}
}
