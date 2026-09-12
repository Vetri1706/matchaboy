#include "macos_keyboard.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace keyboard=matcha::keyboard;
namespace {
unsigned checks=0;
void check(bool value,const char *message) {++checks;if(!value)throw std::runtime_error(message);}
}
int main() {
    try {
        const auto balanced=keyboard::balanced(),classic=keyboard::classic();
        check(keyboard::validate_mapping(balanced).empty(),"balanced defaults must be valid");
        check(keyboard::validate_mapping(classic).empty(),"classic defaults must be valid");
        check(keyboard::key_bit(balanced,2)==0&&keyboard::key_bit(balanced,0)==1&&keyboard::key_bit(balanced,13)==2&&keyboard::key_bit(balanced,1)==3,"WASD maps to the shared controller mask order");
        check(keyboard::key_bit(balanced,37)==4&&keyboard::key_bit(balanced,40)==5,"L and K are A and B");
        check(keyboard::key_bit(balanced,49)==6&&keyboard::key_bit(balanced,36)==7,"Space is Select and Enter is Start");
        check(keyboard::key_bit(balanced,12)==8&&keyboard::key_bit(balanced,34)==9,"Q and I are shoulder buttons");
        check(keyboard::key_bit(balanced,76)==7,"keypad Enter aliases Enter");
        check(keyboard::key_bit(classic,60)==6,"right Shift aliases left Shift");
        auto edited=balanced;edited[9]=31;
        check(keyboard::validate_mapping(edited).empty()&&keyboard::key_bit(edited,31)==9&&keyboard::key_bit(edited,34)==10,"changing R shoulder to O removes I binding");
        // A persisted mapping may retain either keycode from the alias pair.
        edited=classic;edited[6]=60;edited[7]=76;
        check(keyboard::validate_mapping(edited).empty()&&keyboard::key_bit(edited,56)==6&&keyboard::key_bit(edited,36)==7,"aliases also resolve in saved mappings");
        edited=balanced;edited[4]=76;
        check(keyboard::validate_mapping(edited).find("both A and Start")!=std::string::npos,"Enter aliases cannot bind two actions");
        edited=classic;edited[4]=60;
        check(keyboard::validate_mapping(edited).find("both A and Select")!=std::string::npos,"Shift aliases cannot bind two actions");
        edited=balanced;edited[4]=edited[0];
        check(!keyboard::validate_mapping(edited).empty(),"duplicate physical keys are rejected");
        for(const auto key:{48U,53U,111U}) {
            edited=balanced;edited[4]=key;
            check(keyboard::validate_mapping(edited).find("reserved")!=std::string::npos,"permanent emulator shortcuts are reserved");
            check(keyboard::key_bit(edited,key)==10,"reserved keys never become game input even in an invalid saved mapping");
        }
        for(const auto key:{54U,55U,57U,58U,59U,61U,62U,63U,72U,73U,74U,110U,127U,65535U,std::numeric_limits<unsigned>::max()}) {
            edited=balanced;edited[4]=key;
            check(!keyboard::validate_mapping(edited).empty(),"unsupported system/modifier/out-of-range keys are rejected");
            check(keyboard::key_bit(edited,key)==10,"invalid input key returns the unknown-button sentinel");
        }
        for(const auto key:{8U,46U,35U,3U}) {
            edited=balanced;edited[4]=key;
            check(keyboard::validate_mapping(edited).empty()&&keyboard::key_bit(edited,key)==4,"custom bindings may use former bare-letter emulator shortcuts");
        }
        check(keyboard::key_name(49)=="Space"&&keyboard::key_name(76)=="Enter"&&keyboard::key_name(60)=="Shift","aliases display stable user-facing names");
        check(keyboard::key_name(43)==","&&keyboard::key_name(24)=="="&&keyboard::key_name(117)=="Delete","punctuation and navigation names reflect physical keys");
        check(keyboard::key_name(82)=="Keypad 0"&&keyboard::key_name(122)=="F1","keypad and function names are distinct");
        check(keyboard::key_name(65535).find("65535")!=std::string::npos,"unknown persisted keys have an actionable label");
        std::cout<<"PASS "<<checks<<" keyboard mapping checks\n";
        return 0;
    }catch(const std::exception &error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
