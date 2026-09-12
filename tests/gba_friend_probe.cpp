#include "gba_core.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void capture(const GbaCore &core,const std::filesystem::path &path) {
    std::ofstream out(path,std::ios::binary);out<<"P6\n240 160\n255\n";
    for(const auto pixel:core.pixels()) {
        const std::array<char,3> rgb{static_cast<char>(pixel),static_cast<char>(pixel>>8),static_cast<char>(pixel>>16)};
        out.write(rgb.data(),rgb.size());
    }
    if(!out)throw std::runtime_error("Cannot write paired framebuffer.");
}
void state(std::ostream &out,const GbaCore &core) {
    const auto s=core.inspect(0x02000000);
    out<<"{\"frames\":"<<core.frames()<<",\"pc\":"<<s.registers[15]<<",\"cpsr\":"<<s.cpsr
       <<",\"siocnt\":"<<s.siocnt<<",\"rcnt\":"<<s.rcnt<<",\"send\":"<<s.serial_send
       <<",\"if\":"<<s.interrupt_flags<<",\"vcount\":"<<s.vcount<<",\"multi\":[";
    for(unsigned i=0;i<4;++i){if(i)out<<',';out<<s.serial_multi[i];}
    out<<"],\"digest\":\""<<std::hex<<core.digest()<<std::dec<<"\"}";
}
}
// A bounded, persistent two-console probe, suitable for driving real game menus
// without touching the user's running app. It never flushes cartridge saves.
// stdin: "frames buttons0 buttons1" (decimal masks: A1 B2 Select4 Start8,
// Right16 Left32 Up64 Down128 R256 L512); "quit" exits.
int main(int argc,char **argv) {
    try {
        if(argc!=3)throw std::invalid_argument("Usage: gba_friend_probe ROM OUTPUT_DIRECTORY");
        const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
        GbaCore first(argv[1]),second(argv[1]);
        first.enable_audio();second.enable_audio();
        GbaLinkedPair pair(first,second);std::array<std::int16_t,4096> discarded{};
        std::cout<<"READY frames buttons0 buttons1 (up to 3600 frames per command; 36000 total)\n"<<std::flush;
        std::string line;
        while(std::getline(std::cin,line)&&line!="quit") {
            if(line.empty())continue;
            std::istringstream command(line);unsigned frames=0,buttons0=0,buttons1=0;std::string extra;
            if(!(command>>frames>>buttons0>>buttons1)||(command>>extra)||frames>3600||buttons0>1023||buttons1>1023||pair.frames()+frames>36000)
                throw std::invalid_argument("Expected bounded frames buttons0 buttons1.");
            first.set_buttons(static_cast<std::uint16_t>(buttons0));second.set_buttons(static_cast<std::uint16_t>(buttons1));
            for(unsigned i=0;i<frames;++i){pair.run_frame();first.drain_audio(discarded);second.drain_audio(discarded);}
            const auto stem="frame-"+std::to_string(pair.frames());
            capture(first,output/(stem+"-p0.ppm"));capture(second,output/(stem+"-p1.ppm"));
            std::ostringstream json;json<<"{\"frame\":"<<pair.frames()<<",\"pair_digest\":\""<<std::hex<<pair.digest()<<std::dec<<"\",\"players\":[";
            state(json,first);json<<',';state(json,second);json<<"]}";
            std::ofstream report(output/(stem+".json"));report<<json.str()<<'\n';if(!report)throw std::runtime_error("Cannot write paired diagnostics.");
            std::cout<<json.str()<<'\n'<<std::flush;
        }
        return 0;
    } catch(const std::exception &error) {std::cerr<<"GBA pair probe: "<<error.what()<<'\n';return 1;}
}
