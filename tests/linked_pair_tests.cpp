#include "dmg/linked_pair.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include "dmg/snapshot.hpp"
#include "link_fixture.hpp"
#include <iostream>
#include <stdexcept>
#include <memory>
static void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
int main(){try{
    for(unsigned master=0;master<2;++master){
        dmg::Bus a(linked_gb_rom()),b(linked_gb_rom());dmg::Cpu ac(a),bc(b);
        a.poke(0xC001,static_cast<std::uint8_t>(master==0));b.poke(0xC001,static_cast<std::uint8_t>(master==1));
        a.capture_serial_output=b.capture_serial_output=false;a.set_buttons(0x10);b.set_buttons(0x20);
        {
            dmg::LinkedPair cable(a,ac,b,bc);
            for(unsigned i=0;i<120;++i)cable.run_frame();
            check(a.peek(0xC000)==0xDD && b.peek(0xC000)==0xDE,"duplex cable must deliver actual peer joypad byte");
            check((a.iflag&8)&&(b.iflag&8),"both serial IRQs must fire");
            check(cable.edges()>1000,"real program must complete repeated transfers");
            a.set_buttons(0x40);b.set_buttons(0x80);
            for(unsigned i=0;i<4;++i)cable.run_frame();
            check(a.peek(0xC000)==0xD7 && b.peek(0xC000)==0xDB,"live input must change received bytes");
        }
        check(!a.serial_endpoint&&!b.serial_endpoint,"cable destructor must detach both endpoints");
    }
    std::cout<<"PASS actual SM83 duplex, both clock roles, live joypad data, IRQ and cable lifecycle\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
