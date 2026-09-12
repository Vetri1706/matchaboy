#include "keyboard_mapping.hpp"
#include <iostream>
#include <stdexcept>
using namespace matcha::keyboard;
int main(){
    unsigned checks=0;
    const auto check=[&](bool ok,const char *message){++checks;if(!ok)throw std::runtime_error(message);};
    try{
        check(validate_mapping(balanced()).empty(),"Balanced valid");
        check(validate_mapping(classic()).empty(),"Classic valid");
        // Independently specified physical keys -> logical console bits for
        // three OS event streams. The peer only ever receives the bitmask.
        constexpr std::array<unsigned,10> win_scans{0x20,0x1e,0x11,0x1f,0x26,0x25,0x39,0x1c,0x10,0x17};
        constexpr std::array<unsigned,10> mac_codes{2,0,13,1,37,40,49,36,12,34};
        for(unsigned bit=0;bit<10;++bit){
            check(key_bit(balanced(),from_windows_key(win_scans[bit],false,0))==bit,"Windows physical Balanced input");
            check(key_bit(balanced(),from_linux_keycode(win_scans[bit]+8))==bit,"Linux physical Balanced input");
            check(key_bit(balanced(),from_macos_key(mac_codes[bit]))==bit,"Mac physical Balanced input");
        }
        check(key_bit(classic(),from_windows_key(0x48,true,0))==2,"Windows Up arrow");
        check(key_bit(classic(),from_linux_keycode(111))==2,"Linux Up arrow");
        check(key_bit(classic(),from_windows_key(0x48,false,0))==10,"Keypad 8 distinct from arrow");
        check(key_bit(classic(),from_linux_keycode(80))==10,"Linux keypad 8 distinct from arrow");
        check(from_windows_key(0x1c,true,0)==from_macos_key(76),"Windows keypad Enter alias");
        check(from_linux_keycode(104)==from_macos_key(76),"Linux keypad Enter alias");
        check(from_windows_key(0x36,false,0)==56&&from_linux_keycode(62)==56,"Right Shift aliases");
        check(from_windows_key(0x1e,false,'Q')==0,"Windows input language does not move physical A");
        check(from_windows_key(0,false,'L')==37,"Accessibility messages have VK fallback");
        check(from_linux_keycode(0)==unknown_key&&from_linux_keycode(250)==unknown_key,"Unknown Linux key ignored");
        check(from_windows_key(0x5b,true,0)==unknown_key,"Windows key ignored");
        check(from_macos_key(200)==unknown_key,"Unknown Mac key ignored");
        for(auto key:{53U,48U,111U}){
            auto m=balanced();m[4]=key;check(!validate_mapping(m).empty(),"Reserved keys rejected");
        }
        auto m=balanced();m[4]=m[2];check(!validate_mapping(m).empty(),"Duplicate mappings rejected");
        m=classic();m[4]=60;check(!validate_mapping(m).empty(),"Shift alias duplicate rejected");
        m=balanced();m[4]=255;check(!validate_mapping(m).empty(),"Unknown stored keys rejected");
        check(arcade_help("Arrows; Z (A); X (B); Enter; Shift",balanced())=="W/A/S/D; L (A); K (B); Enter; Space","Help uses same bindings");
        m=balanced();m[4]=7;m[5]=6;
        check(arcade_help("Z (A) / X (B)",m)=="X (A) / Z (B)","Help replacement does not recurse");
        check(arcade_help("ZEBRA Xylophone",m)=="ZEBRA Xylophone","Help preserves non-key words");
        std::cout<<"PASS "<<checks<<" shared keyboard and OS adapter checks\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
