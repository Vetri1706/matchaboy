#pragma once
#include <array>
#include <string>
#include <string_view>
#include <cctype>
#include <utility>

namespace matcha::keyboard {

// Matchaboy controller-mask order, shared by local play and friend sessions.
enum class Button : unsigned { Right, Left, Up, Down, A, B, Select, Start, L, R };
inline constexpr unsigned button_count=10;
using Mapping=std::array<unsigned,button_count>;
inline constexpr std::array<std::string_view,button_count> button_names{
    "Right","Left","Up","Down","A","B","Select","Start","L shoulder","R shoulder"};

constexpr Mapping balanced() {return {2,0,13,1,37,40,49,36,12,34};}
constexpr Mapping classic() {return {124,123,126,125,6,7,56,36,12,13};}

// Stable key identifiers for physical ANSI positions, not OS event codes.
// Values preserve the original Mac preferences format. Windows scan codes and
// Linux X11/evdev codes must go through the adapters below before lookup/storage.
// Both Shift keys and both Enter keys are aliases.
constexpr unsigned canonical_key(unsigned key) {
    return key==60?56:key==76?36:key;
}
constexpr bool reserved_key(unsigned key) {return key==53||key==48||key==111;}

constexpr std::string_view known_key_name(unsigned key) {
    switch(canonical_key(key)) {
    case 0:return "A";case 1:return "S";case 2:return "D";case 3:return "F";
    case 4:return "H";case 5:return "G";case 6:return "Z";case 7:return "X";
    case 8:return "C";case 9:return "V";case 11:return "B";case 12:return "Q";
    case 13:return "W";case 14:return "E";case 15:return "R";case 16:return "Y";
    case 17:return "T";case 18:return "1";case 19:return "2";case 20:return "3";
    case 21:return "4";case 22:return "6";case 23:return "5";case 24:return "=";
    case 25:return "9";case 26:return "7";case 27:return "-";case 28:return "8";
    case 29:return "0";case 30:return "]";case 31:return "O";case 32:return "U";
    case 33:return "[";case 34:return "I";case 35:return "P";case 36:return "Enter";
    case 37:return "L";case 38:return "J";case 39:return "'";case 40:return "K";
    case 41:return ";";case 42:return "\\";case 43:return ",";case 44:return "/";
    case 45:return "N";case 46:return "M";case 47:return ".";case 48:return "Tab";
    case 49:return "Space";case 50:return "`";case 51:return "Backspace";
    case 53:return "Escape";case 54:return "Right Command";case 55:return "Command";
    case 56:return "Shift";case 57:return "Caps Lock";case 58:return "Option";
    case 59:return "Control";case 61:return "Right Option";case 62:return "Right Control";
    case 63:return "Fn";case 64:return "F17";case 65:return "Keypad .";
    case 67:return "Keypad *";case 69:return "Keypad +";case 71:return "Keypad Clear";
    case 72:return "Volume Up";case 73:return "Volume Down";case 74:return "Mute";
    case 75:return "Keypad /";case 78:return "Keypad -";case 79:return "F18";
    case 80:return "F19";case 81:return "Keypad =";case 82:return "Keypad 0";
    case 83:return "Keypad 1";case 84:return "Keypad 2";case 85:return "Keypad 3";
    case 86:return "Keypad 4";case 87:return "Keypad 5";case 88:return "Keypad 6";
    case 89:return "Keypad 7";case 90:return "F20";case 91:return "Keypad 8";
    case 92:return "Keypad 9";case 96:return "F5";case 97:return "F6";
    case 98:return "F7";case 99:return "F3";case 100:return "F8";case 101:return "F9";
    case 103:return "F11";case 105:return "F13";case 106:return "F16";
    case 107:return "F14";case 109:return "F10";case 110:return "Menu";
    case 111:return "F12";case 113:return "F15";case 114:return "Help";
    case 115:return "Home";case 116:return "Page Up";case 117:return "Delete";
    case 118:return "F4";case 119:return "End";case 120:return "F2";
    case 121:return "Page Down";case 122:return "F1";case 123:return "Left Arrow";
    case 124:return "Right Arrow";case 125:return "Down Arrow";case 126:return "Up Arrow";
    default:return {};
    }
}

inline std::string key_name(unsigned key) {
    const auto name=known_key_name(key);
    return name.empty()?"Unknown key ("+std::to_string(key)+")":std::string(name);
}

constexpr bool supported_key(unsigned key) {
    if(known_key_name(key).empty())return false;
    switch(canonical_key(key)) {
    // Shift has a dedicated flagsChanged path. Other modifiers, system media
    // keys and the contextual-menu key are not ordinary controller buttons.
    case 54:case 55:case 57:case 58:case 59:case 61:case 62:case 63:
    case 72:case 73:case 74:case 110:return false;
    default:return true;
    }
}

inline std::string validate_mapping(const Mapping &mapping) {
    for(unsigned button=0;button<button_count;++button) {
        const auto key=mapping[button];
        if(reserved_key(key))return key_name(key)+" is reserved for emulator controls.";
        if(!supported_key(key))return key_name(key)+" cannot be used as a game button.";
        for(unsigned earlier=0;earlier<button;++earlier) {
            if(canonical_key(mapping[earlier])==canonical_key(key))
                return key_name(key)+" is assigned to both "+std::string(button_names[earlier])+" and "+std::string(button_names[button])+". Choose different keys.";
        }
    }
    return {};
}

constexpr unsigned key_bit(const Mapping &mapping,unsigned key) {
    if(!supported_key(key)||reserved_key(key))return button_count;
    for(unsigned button=0;button<button_count;++button)
        if(canonical_key(mapping[button])==canonical_key(key))return button;
    return button_count;
}


// PC/AT set-1 scan codes, with 0x100 denoting the E0 extended prefix. This
// explicit adapter keeps the layout independent of a Windows input language.
struct ScanKey { unsigned scan, key; };
inline constexpr ScanKey pc_keys[]{
    {0x01,53},{0x02,18},{0x03,19},{0x04,20},{0x05,21},{0x06,23},{0x07,22},{0x08,26},{0x09,28},{0x0a,25},{0x0b,29},
    {0x0c,27},{0x0d,24},{0x0e,51},{0x0f,48},{0x10,12},{0x11,13},{0x12,14},{0x13,15},{0x14,17},{0x15,16},{0x16,32},
    {0x17,34},{0x18,31},{0x19,35},{0x1a,33},{0x1b,30},{0x1c,36},{0x1e,0},{0x1f,1},{0x20,2},{0x21,3},{0x22,5},
    {0x23,4},{0x24,38},{0x25,40},{0x26,37},{0x27,41},{0x28,39},{0x29,50},{0x2a,56},{0x2b,42},{0x2c,6},{0x2d,7},
    {0x2e,8},{0x2f,9},{0x30,11},{0x31,45},{0x32,46},{0x33,43},{0x34,47},{0x35,44},{0x36,56},{0x37,67},{0x39,49},
    {0x3b,122},{0x3c,120},{0x3d,99},{0x3e,118},{0x3f,96},{0x40,97},{0x41,98},{0x42,100},{0x43,101},{0x44,109},
    {0x45,71},{0x47,89},{0x48,91},{0x49,92},{0x4a,78},{0x4b,86},{0x4c,87},{0x4d,88},{0x4e,69},{0x4f,83},{0x50,84},
    {0x51,85},{0x52,82},{0x53,65},{0x57,103},{0x58,111},{0x64,105},{0x65,107},{0x66,113},{0x67,106},{0x68,64},
    {0x69,79},{0x6a,80},{0x6b,90},{0x11c,36},{0x135,75},{0x147,115},{0x148,126},{0x149,116},{0x14b,123},
    {0x14d,124},{0x14f,119},{0x150,125},{0x151,121},{0x152,114},{0x153,117}
};
inline constexpr unsigned unknown_key=255;
constexpr unsigned from_pc_scan(unsigned scan){
    for(const auto entry:pc_keys)if(entry.scan==scan)return entry.key;
    return unknown_key;
}
constexpr unsigned from_macos_key(unsigned key){
    return known_key_name(key).empty()?unknown_key:canonical_key(key);
}
constexpr unsigned from_windows_key(unsigned scan,bool extended,unsigned vk){
    if(scan)return from_pc_scan(scan|(extended?0x100U:0U));
    // Messages from accessibility/test clients may have no scan code. Real
    // keyboard events always use the physical path above.
    constexpr unsigned letters[]{0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6};
    constexpr unsigned digits[]{29,18,19,20,21,23,22,26,28,25};
    if(vk>='A'&&vk<='Z')return letters[vk-'A'];
    if(vk>='0'&&vk<='9')return digits[vk-'0'];
    if(vk>=0x70&&vk<=0x7b){constexpr unsigned fn[]{122,120,99,118,96,97,98,100,101,109,103,111};return fn[vk-0x70];}
    switch(vk){
    case 0x08:return 51;case 0x09:return 48;case 0x0d:return 36;case 0x10:case 0xa0:case 0xa1:return 56;
    case 0x1b:return 53;case 0x20:return 49;case 0x21:return 116;case 0x22:return 121;case 0x23:return 119;
    case 0x24:return 115;case 0x25:return 123;case 0x26:return 126;case 0x27:return 124;case 0x28:return 125;
    case 0x2d:return 114;case 0x2e:return 117;case 0xba:return 41;case 0xbb:return 24;case 0xbc:return 43;
    case 0xbd:return 27;case 0xbe:return 47;case 0xbf:return 44;case 0xc0:return 50;case 0xdb:return 33;
    case 0xdc:return 42;case 0xdd:return 30;case 0xde:return 39;default:return unknown_key;
    }
}
constexpr unsigned from_linux_keycode(unsigned x11_keycode){
    if(x11_keycode<8)return unknown_key;
    const auto evdev=x11_keycode-8;
    if(evdev<=88)return from_pc_scan(evdev);
    switch(evdev){
    case 96:return 36;case 98:return 75;case 102:return 115;case 103:return 126;case 104:return 116;
    case 105:return 123;case 106:return 124;case 107:return 119;case 108:return 125;case 109:return 121;
    case 110:return 114;case 111:return 117;case 117:return 81;
    default:if(evdev>=183&&evdev<=190){constexpr unsigned fn[]{105,107,113,106,64,79,80,90};return fn[evdev-183];}
        return unknown_key;
    }
}
inline std::string arcade_help(std::string_view original,const Mapping &mapping){
    const auto name=[&](unsigned bit){return key_name(mapping[bit]);};
    const std::array<std::pair<std::string_view,std::string>,7> replacements{{
        {"Left/Right",name(1)+"/"+name(0)},{"Up/Down",name(2)+"/"+name(3)},
        {"Arrows",name(2)+"/"+name(1)+"/"+name(3)+"/"+name(0)},
        {"Enter",name(7)},{"Shift",name(6)},{"Z",name(4)},{"X",name(5)}}};
    std::string result;
    for(std::size_t i=0;i<original.size();){
        bool replaced=false;
        for(const auto &[token,label]:replacements){
            const auto end=i+token.size();
            if(original.substr(i,token.size())==token &&
               (i==0||!std::isalnum(static_cast<unsigned char>(original[i-1]))) &&
               (end==original.size()||!std::isalnum(static_cast<unsigned char>(original[end])))){
                result+=label;i=end;replaced=true;break;
            }
        }
        if(!replaced)result+=original[i++];
    }
    return result;
}

} // namespace matcha::keyboard
