#include "gba_core.hpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned checks=0;
void check(bool condition,const char *message){++checks;if(!condition)throw std::runtime_error(message);}
// Tiny independent ARM7 assembler. Generated cartridges execute real ARM
// loads/stores, serial IRQ polling and input-dependent payloads; no host memory
// writes, serial callbacks or precomputed peer replies are used by the tests.
struct ArmRom {
    std::vector<std::uint32_t> words;
    std::map<std::string,std::size_t> labels;
    struct Jump{std::size_t index;std::string label;unsigned condition;};
    struct Literal{std::size_t index;std::uint32_t value;unsigned reg;};
    std::vector<Jump> jumps;
    std::vector<Literal> literals;
    void op(std::uint32_t value){words.push_back(value);}
    void load(unsigned reg,std::uint32_t value){literals.push_back({words.size(),value,reg});op(0);}
    void label(const std::string &name){labels[name]=words.size();}
    void branch(const std::string &name,unsigned condition=14){jumps.push_back({words.size(),name,condition});op(0);}
    void half(bool read,unsigned reg,unsigned base,unsigned offset){op((read?0xE1D000B0U:0xE1C000B0U)|(base<<16)|(reg<<12)|((offset&0xF0)<<4)|(offset&15));}
    void word(bool read,unsigned reg,unsigned base,unsigned offset){op((read?0xE5900000U:0xE5800000U)|(base<<16)|(reg<<12)|offset);}
    void save(const std::filesystem::path &path,const std::string &hardware=""){
        for(const auto &literal:literals){
            const auto offset=(words.size()-literal.index)*4-8;
            check(offset<4096,"ARM literal range");words[literal.index]=0xE59F0000U|(literal.reg<<12)|static_cast<std::uint32_t>(offset);op(literal.value);
        }
        for(const auto &jump:jumps){const auto delta=static_cast<std::int32_t>(labels.at(jump.label))-static_cast<std::int32_t>(jump.index)-2;
            words[jump.index]=(jump.condition<<28)|0x0A000000U|(static_cast<std::uint32_t>(delta)&0xFFFFFF);}
        std::vector<std::uint8_t> bytes(512*1024,0);bytes[0]=0x2E;bytes[3]=0xEA;bytes[0xB2]=0x96;
        for(std::size_t i=0;i<words.size();++i)for(unsigned j=0;j<4;++j)bytes[0xC0+i*4+j]=static_cast<std::uint8_t>(words[i]>>(j*8));
        for(std::size_t i=0;i<hardware.size()&&i<4;++i)bytes[0xAC+i]=static_cast<std::uint8_t>(hardware[i]);
        std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char *>(bytes.data()),bytes.size());
        if(!out)throw std::runtime_error("write fixture ROM");
    }
};
void fixture(const std::filesystem::path &path,unsigned side,unsigned mode){
    ArmRom r;r.load(4,0x04000100);r.load(5,0x02000000);r.op(0xE3A06000); // counter
    r.op(0xE3A00000);r.half(false,0,4,0x34); // RCNT normal serial
    const bool wide=mode==1||mode==3;
    const unsigned control=mode==2?0x6003:(wide?0x5000:0x4000)|(side==0?1:0)|(mode>=3?2:0);
    r.load(0,control);r.half(false,0,4,0x28);
    r.label("send");r.half(true,0,4,0x30); // actual active-low KEYINPUT
    r.load(1,wide?(side?0x87654321:0x12345678):(side?0x22:0x11));r.op(0xE0211000); // EOR payload,keys
    if(wide)r.word(false,1,4,0x20);else r.half(false,1,4,0x2A);
    if(side==0){
        if(mode==2){r.label("ready");r.half(true,0,4,0x28);r.op(0xE3100008);r.branch("ready",0);}
        r.load(2,12000);r.label("delay");r.op(0xE2522001);r.branch("delay",1);
        r.load(0,control|0x80);r.half(false,0,4,0x28);
    }
    r.load(7,0x04000202);r.label("irq");r.half(true,0,7,0);r.op(0xE3100080);r.branch("irq",0);
    r.word(false,0,5,12); // prove hardware set serial IRQ
    if(mode==2){r.half(true,0,4,0x20);r.word(false,0,5,0);r.half(true,0,4,0x22);r.word(false,0,5,4);}
    else if(wide){r.word(true,0,4,0x20);r.word(false,0,5,0);}
    else{r.half(true,0,4,0x2A);r.word(false,0,5,0);}
    r.op(0xE2866001);r.word(false,6,5,8);r.op(0xE3A00080);r.half(false,0,7,0);r.branch("send");r.save(path);
}
std::uint32_t word(const GbaCore &core,unsigned offset){const auto s=core.inspect(0x02000000);std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=std::uint32_t(s.memory[offset+i])<<(i*8);return n;}
void run(const std::filesystem::path &directory,unsigned mode){
    const auto a=directory/"player0.gba",b=directory/"player1.gba";fixture(a,0,mode);fixture(b,1,mode);
    GbaCore first(a),second(b),again0(a),again1(b);
    first.import_save({});second.import_save({});again0.import_save({});again1.import_save({});
    GbaLinkedPair link(first,second),replay(again0,again1);
    for(unsigned frame=0;frame<8;++frame){link.run_frame();replay.run_frame();check(link.digest()==replay.digest(),"repeated linked pair state must match");}
    std::cerr<<"MODE "<<mode<<" counts="<<word(first,8)<<','<<word(second,8)<<" recv="<<std::hex<<word(first,0)<<','<<word(first,4)<<'/'<<word(second,0)<<','<<word(second,4)<<std::dec<<'\n';
    check(word(first,8)>3&&word(second,8)>3,"both actual CPUs complete serial transfers");
    check((word(first,12)&0x80)&&(word(second,12)&0x80),"serial IRQ raised for both consoles");
    const auto old=word(first,0);second.set_buttons(1);again1.set_buttons(1);
    for(unsigned frame=0;frame<3;++frame){link.run_frame();replay.run_frame();check(link.digest()==replay.digest(),"input-dependent replay must stay deterministic");}
    if(mode==2){check(word(first,0)==(0x11^0x3FF),"multiplayer master word");check(word(first,4)==(0x22^0x3FE),"multiplayer peer input changes transmitted word");check(word(first,0)==word(second,0)&&word(first,4)==word(second,4),"both multiplayer receivers agree");}
    else{const bool wide=mode==1||mode==3;const auto mask=wide?0xFFFFFFFFU:0xFFU;const auto expected=((wide?0x87654321:0x22)^0x3FE)&mask;check((word(first,0)&mask)==expected,"normal master receives actual slave word");check((word(first,0)&mask)!=(old&mask),"peer buttons affect normal reply");}
}
void save_and_lifetime(const std::filesystem::path &directory) {
    const auto a=directory/"save0.gba",b=directory/"save1.gba";fixture(a,0,2);fixture(b,1,2);
    const std::vector<std::uint8_t> imported(32768,0x5A);
    GbaCore first(a),second(b);
    check(first.fresh()&&second.fresh(),"newly reset cores are fresh");
    first.step();check(!first.fresh()&&first.frames()==0,"one instruction invalidates freshness before the first video frame");
    bool rejected=false;try{GbaLinkedPair invalid(first,second);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"link rejects a stepped core even when video frame counter is zero");
    first.import_save(imported);second.import_save(imported);
    check(first.fresh()&&second.fresh(),"save import restores freshness");
    check(first.export_save()==imported,"save bytes survive memory import/export");
    check(!std::filesystem::exists(directory/"save0.matchaboy.sav"),"save import never writes source disk");
    {
        GbaLinkedPair pair(first,second);pair.run_frame();
        check(!first.fresh()&&!second.fresh(),"linked emulation marks both consoles started");
        rejected=false;try{first.step();}catch(const std::logic_error&){rejected=true;}check(rejected,"one linked CPU cannot advance independently");
        rejected=false;try{first.import_save({});}catch(const std::logic_error&){rejected=true;}check(rejected,"linked save import is rejected before mutation");
        rejected=false;try{GbaLinkedPair duplicate(first,second);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"attached cores cannot join another pair");
    }
    first.step();second.step(); // drivers/events were safely removed on detach
    first.import_save(imported);second.import_save(imported);
    check(first.frames()==0&&second.frames()==0,"save import resets both CPU timelines");
    GbaLinkedPair reattached(first,second);reattached.run_frame();
    check(reattached.frames()==1,"reset cores can reattach and run a fresh cable");
    check(first.export_save()==imported,"serial emulation preserves cartridge save bytes");
}
void timezone(const char *zone) {
#ifdef _WIN32
    if(_putenv_s("TZ",zone)!=0)throw std::runtime_error("Cannot set test timezone");_tzset();
#else
    if(setenv("TZ",zone,1)!=0)throw std::runtime_error("Cannot set test timezone");tzset();
#endif
}
void rtc(const std::filesystem::path &directory) {
    const auto path=directory/"rtc.gba";ArmRom r;r.load(4,0x080000C4);r.load(5,0x02000000);
    const auto write=[&r](unsigned value,unsigned offset){r.op(0xE3A00000U|value);r.half(false,0,4,offset);};
    write(1,4);write(7,2);write(1,0);write(5,0);
    for(unsigned bit=0;bit<8;++bit){const auto value=4|(((0xA6U>>bit)&1)<<1);write(value,0);write(value|1,0);}
    write(5,2); // release DATA, retain clock/select outputs
    for(unsigned byte=0;byte<7;++byte){
        r.op(0xE3A02000);
        for(unsigned bit=0;bit<8;++bit){write(4,0);write(5,0);r.half(true,1,4,0);r.op(0xE2011002);r.op(0xE1A010A1);r.op(0xE1822001U|(bit<<7));}
        r.word(false,2,5,byte*4);
    }
    write(1,0);r.label("done");r.branch("done");r.save(path,"AXVE"); // selects the real RTC/flash cartridge hardware profile
    const char *prior=std::getenv("TZ");const std::string old=prior?prior:"";const bool had=prior!=nullptr;
    timezone("UTC0");GbaCore first(path),second(path);GbaLinkedPair one(first,second);one.run_frame();
    const auto baseline=one.digest();
    timezone("PST8PDT");GbaCore third(path),fourth(path);GbaLinkedPair two(third,fourth);two.run_frame();
    check(two.digest()==baseline,"linked RTC is independent of host timezone");
    const std::array<unsigned,7> expected{0,1,1,6,0,0,0};
    for(unsigned byte=0;byte<7;++byte){std::cerr<<"RTC "<<byte<<"="<<word(third,byte*4)<<" expected="<<expected[byte]<<"\n";check(word(third,byte*4)==expected[byte],"real ARM GPIO reads canonical linked UTC date");}
    timezone(old.c_str());
    if(!had){
#ifdef _WIN32
        _putenv_s("TZ","");_tzset();
#else
        unsetenv("TZ");tzset();
#endif
    }
}

}
int main(){
    const auto path=std::filesystem::temp_directory_path()/ ("matchaboy-real-gba-link-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{std::filesystem::create_directories(path);run(path,2);run(path,1);run(path,0);run(path,3);run(path,4);save_and_lifetime(path);rtc(path);std::filesystem::remove_all(path);std::cout<<"PASS "<<checks<<" real GBA link checks\n";return 0;}
    catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<" fixtures="<<path<<'\n';return 1;}
}
