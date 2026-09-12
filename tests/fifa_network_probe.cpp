#include "friend_session.hpp"
#include "gba_core.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
struct Input {unsigned duration,p0,p1;};
// Recorded by driving FIFA 07's actual menus with a physical linked pair.
constexpr std::array<Input,32> transcript{{
    {300,0,0},{1,1,1},{180,0,0},{600,0,0},{1,8,8},{180,0,0},
    {1,8,8},{90,0,0},{6,8,8},{180,0,0},{6,128,128},{6,0,0},
    {6,128,128},{6,0,0},{6,128,128},{6,0,0},{6,1,1},{180,0,0},
    {6,1,1},{180,0,0},{6,32,16},{12,0,0},{6,1,1},{120,0,0},
    {6,1,1},{120,0,0},{6,1,1},{120,0,0},{6,1,1},{600,0,0},
    {1200,0,0},{1000,17,34}
}};
std::uint16_t buttons(std::uint64_t frame,bool host) {
    unsigned raw=0;
    for(const auto &row:transcript){if(frame<row.duration){raw=host?row.p0:row.p1;break;}frame-=row.duration;}
    // Raw GBA A B Select Start Right Left Up Down R L -> UI schema.
    constexpr std::array<unsigned,10> mapping{4,5,6,7,0,1,2,3,9,8};
    std::uint16_t result=0;
    for(unsigned bit=0;bit<mapping.size();++bit)if(raw&(1U<<bit))result|=static_cast<std::uint16_t>(1U<<mapping[bit]);
    return result;
}
void capture(const GbaCore &core,const matcha::FriendSession &session,const std::filesystem::path &output) {
    const auto stem="frame-"+std::to_string(session.frames());
    std::ofstream image(output/(stem+".ppm"),std::ios::binary);image<<"P6\n240 160\n255\n";
    for(const auto pixel:core.pixels()) {
        const std::array<char,3> rgb{static_cast<char>(pixel),static_cast<char>(pixel>>8),static_cast<char>(pixel>>16)};
        image.write(rgb.data(),rgb.size());
    }
    if(!image)throw std::runtime_error("Cannot write network framebuffer capture.");
    const auto state=core.inspect(0x02000000);
    std::ostringstream json;json<<"{\"event\":\"checkpoint\",\"frame\":"<<session.frames()
        <<",\"verified_frame\":"<<session.verified_frames()<<",\"core_frames\":"<<core.frames()
        <<",\"pc\":"<<state.registers[15]<<",\"siocnt\":"<<state.siocnt<<",\"rcnt\":"<<state.rcnt
        <<",\"if\":"<<state.interrupt_flags<<",\"multi\":[";
    for(unsigned i=0;i<4;++i){if(i)json<<',';json<<state.serial_multi[i];}
    json<<"],\"digest\":\""<<std::hex<<core.digest()<<std::dec<<"\"}";
    std::ofstream report(output/(stem+".json"));report<<json.str()<<'\n';if(!report)throw std::runtime_error("Cannot write checkpoint diagnostics.");
    std::cout<<json.str()<<'\n'<<std::flush;
}
}
int main(int argc,char **argv) {
    try {
        if(argc!=9)throw std::invalid_argument("Usage: fifa_network_probe ROM OUTPUT_DIRECTORY host|join ADDRESS PORT CODE STOP_FILE paced|unpaced");
        const std::filesystem::path output=argv[2],stop_file=argv[7];std::filesystem::create_directories(output);
        const std::string role=argv[3],pace=argv[8];
        if(role!="host"&&role!="join")throw std::invalid_argument("Invalid network role.");
        if(pace!="paced"&&pace!="unpaced")throw std::invalid_argument("Invalid pacing mode.");
        const auto port=std::stoul(argv[5]);if(port==0||port>65535)throw std::invalid_argument("Invalid port.");
        const bool host=role=="host";
        GbaCore local(argv[1]);local.import_save(local.export_save());
        matcha::FriendSession session({host,argv[4],static_cast<std::uint16_t>(port),argv[6]},argv[1],nullptr,nullptr,&local);
        std::array<std::int16_t,8192> discarded{};
        const auto start=std::chrono::steady_clock::now();const auto deadline=start+std::chrono::seconds(300);
        auto frame_time=start;bool ready_reported=false;
        std::cout<<"{\"event\":\"started\",\"role\":\""<<role<<"\",\"port\":"<<session.port()<<"}\n"<<std::flush;
        for(;;) {
            const auto now=std::chrono::steady_clock::now();
            if(now>deadline)throw std::runtime_error("Network FIFA probe timed out: "+session.status());
            const bool due=pace=="unpaced"||now>=frame_time;
            if(session.frames()<5149&&due) {
                // The session schedules this input six frames in the future.
                if(session.advance(buttons(session.frames()+6,host))) {
                    frame_time=std::max(frame_time,now)+std::chrono::nanoseconds(16666667);
                    while(session.drain_audio(discarded)){}
                    if(session.frames()==4149||session.frames()==5149)capture(local,session,output);
                    else if(session.frames()%600==0)std::cout<<"{\"event\":\"progress\",\"frame\":"<<session.frames()<<",\"verified_frame\":"<<session.verified_frames()<<"}\n"<<std::flush;
                }
            } else session.service();
            if(ready_reported&&std::filesystem::exists(stop_file))break;
            if(session.finished())throw std::runtime_error(session.status());
            if(session.frames()==5149&&session.verified_frames()>=5100&&!ready_reported) {
                ready_reported=true;
                std::cout<<"{\"event\":\"verified\",\"frame\":5149,\"verified_frame\":"<<session.verified_frames()<<"}\n"<<std::flush;
            }
            if(ready_reported&&std::filesystem::exists(stop_file))break;
            if(!due||session.frames()==5149||!session.connected())std::this_thread::sleep_for(std::chrono::milliseconds(1));
            else std::this_thread::yield();
        }
        session.close();return 0;
    } catch(const std::exception &error) {std::cerr<<"FIFA network probe: "<<error.what()<<'\n';return 1;}
}
