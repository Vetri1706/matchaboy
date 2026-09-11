// Headless hardware integration probe for the original GBA cartridges.
// Commands on stdin are "frames GBA-key-mask"; each reply is real EWRAM/PCM.
#include "gba_core.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
int main(int argc,char **argv) {
    try {
        if(argc!=2) throw std::runtime_error("usage: gba_arcade_probe ROM");
        GbaCore game(argv[1]); game.enable_audio();
        std::array<std::int16_t,8192> audio{};
        unsigned frames=0,keys=0; unsigned peak=0;
        while(std::cin>>frames>>keys) {
            if(frames>100000 || keys>1023) throw std::runtime_error("invalid probe command");
            game.set_buttons(static_cast<std::uint16_t>(keys));
            for(unsigned i=0;i<frames;++i) {
                game.run_frame();
                for(auto n=game.drain_audio(audio);n;n=game.drain_audio(audio))
                    for(std::size_t j=0;j<n;++j) peak=std::max(peak,static_cast<unsigned>(std::abs(static_cast<int>(audio[j]))));
            }
            const auto view=game.inspect(0x02000000);
            const auto word=[&](unsigned offset) {
                std::uint32_t value=0;
                for(unsigned j=0;j<4;++j)value|=static_cast<std::uint32_t>(view.memory[offset+j])<<(8*j);
                return static_cast<std::int32_t>(value);
            };
            std::cout<<"{\"frames\":"<<game.frames()<<",\"kind\":"<<word(0)
                <<",\"phase\":"<<word(4)<<",\"game_frame\":"<<word(8)<<",\"tick\":"<<word(12)<<",\"score\":"<<word(16)
                <<",\"time\":"<<word(20)<<",\"hp\":"<<word(24)<<",\"x\":"<<word(28)
                <<",\"y\":"<<word(32)<<",\"stage\":"<<word(60)<<",\"target\":"<<word(64)
                <<",\"carrying\":"<<word(68)<<",\"moves\":"<<word(72)<<",\"objects\":[";
            for(unsigned i=0;i<4;++i) {
                if(i) std::cout<<',';
                std::cout<<"{\"x\":"<<word(172+i*24)<<",\"y\":"<<word(176+i*24)<<"}";
            }
            std::cout<<"],\"peak\":"<<peak<<"}"<<std::endl;
        }
        return 0;
    } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
