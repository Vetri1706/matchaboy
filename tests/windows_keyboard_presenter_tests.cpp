// These tests drive real Win32 controls and the presenter's actual message
// queue. No global keyboard hooks or changes to the user's preferences occur.
#include "windows_keyboard_settings.hpp"
#include <array>
#include <cstdio>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned checks=0;
void require(bool condition,const char *message) {
    ++checks;if(!condition)throw std::runtime_error(message);
}
std::wstring window_text(HWND window) {
    const int count=GetWindowTextLengthW(window);
    std::wstring value(static_cast<std::size_t>(count)+1,L'\0');
    const int written=GetWindowTextW(window,value.data(),count+1);value.resize(static_cast<std::size_t>(written));return value;
}
HWND settings_window{};
BOOL CALLBACK find_settings(HWND window,LPARAM) {
    if(window_text(window)==L"Keyboard Controls"){settings_window=window;return FALSE;}return TRUE;
}
HWND find_settings() {
    settings_window=nullptr;EnumThreadWindows(GetCurrentThreadId(),find_settings,0);return settings_window;
}
void click(HWND window,unsigned id) {
    const auto button=GetDlgItem(window,static_cast<int>(id));require(button!=nullptr,"missing native button");
    SendMessageW(button,BM_CLICK,0,0);
}
void key(HWND window,unsigned vk,unsigned scan,bool extended=false,bool repeat=false,bool release=true) {
    HWND target=GetFocus();if(!target||(!IsChild(window,target)&&target!=window))target=window;
    const LPARAM packed=1|(static_cast<LPARAM>(scan)<<16)|(extended?(LPARAM{1}<<24):0)|(repeat?(LPARAM{1}<<30):0);
    require(PostMessageW(target,WM_KEYDOWN,vk,packed)!=FALSE,"could not queue key press");
    if(release)require(PostMessageW(target,WM_KEYUP,vk,packed|(LPARAM{1}<<30)|(LPARAM{1}<<31))!=FALSE,"could not queue key release");
}
struct Driver {
    std::vector<std::function<void(HWND)>> steps;
    std::size_t next=0;
    unsigned timer_ticks=0;
    ULONGLONG start=0;
    std::exception_ptr error;
};
Driver *driver{};
LRESULT CALLBACK owner_proc(HWND owner,UINT message,WPARAM wparam,LPARAM lparam) noexcept {
    if(message==WM_TIMER&&driver) {
        ++driver->timer_ticks;
        try {
            HWND dialog=find_settings();
            if(GetTickCount64()-driver->start>5000)throw std::runtime_error("keyboard settings test timed out");
            if(!dialog)return 0;
            require(!IsWindowEnabled(owner),"owner stays disabled while settings is open");
            if(driver->next<driver->steps.size())driver->steps[driver->next++](dialog);
            else throw std::runtime_error("scenario finished without closing the dialog");
        } catch(...) {
            driver->error=std::current_exception();if(HWND dialog=find_settings())PostMessageW(dialog,WM_CLOSE,0,0);
        }
        return 0;
    }
    return DefWindowProcW(owner,message,wparam,lparam);
}
using Steps=std::vector<std::function<void(HWND)>>;
bool run(HWND owner,matcha::keyboard::Mapping &mapping,Steps steps) {
    Driver state{std::move(steps),0,0,GetTickCount64(),{}};driver=&state;
    require(SetTimer(owner,1,10,nullptr)!=0,"could not create parent network timer");
    bool applied=false;
    try{applied=matcha::keyboard::show_windows_settings(owner,mapping);}
    catch(...){KillTimer(owner,1);driver=nullptr;throw;}
    KillTimer(owner,1);driver=nullptr;
    if(state.error)std::rethrow_exception(state.error);
    require(state.next==state.steps.size(),"dialog closed before all expected actions");
    require(state.timer_ticks>=state.steps.size(),"parent timer was not serviced in the settings loop");
    require(IsWindowEnabled(owner)!=FALSE,"owner was not re-enabled after settings");
    require(find_settings()==nullptr,"settings window leaked after close");
    return applied;
}
struct IsolatedRegistry {
    std::wstring path=L"Software\\MatchaboyKeyboardTest_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
    HKEY key{};
    IsolatedRegistry() {
        // Production preferences create ordinary nonvolatile subkeys; Windows
        // disallows those below a volatile parent, even inside an override.
        require(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,nullptr,&key,nullptr)==ERROR_SUCCESS,"could not create isolated registry key");
        if(RegOverridePredefKey(HKEY_CURRENT_USER,key)!=ERROR_SUCCESS) {
            RegCloseKey(key);key=nullptr;RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());
            throw std::runtime_error("could not isolate HKCU for persistence tests");
        }
    }
    ~IsolatedRegistry(){RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);if(key)RegCloseKey(key);RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());}
};
void persistence_tests() {
    using namespace matcha::keyboard;
    IsolatedRegistry sandbox;
    require(load_windows_mapping()==balanced(),"missing mapping must load Balanced");
    auto custom=balanced();custom[9]=31;save_windows_mapping(custom);
    require(load_windows_mapping()==custom,"saved mapping did not round-trip");
    auto duplicate=custom;duplicate[9]=custom[4];bool rejected=false;
    try{save_windows_mapping(duplicate);}catch(const std::invalid_argument &){rejected=true;}
    require(rejected,"invalid mappings must be rejected before saving");
    require(load_windows_mapping()==custom,"rejected mapping changed saved preferences");
    HKEY app_key{};
    require(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Matchaboy",0,KEY_SET_VALUE,&app_key)==ERROR_SUCCESS,"saved test registry key missing");
    struct CloseKey{HKEY key;~CloseKey(){RegCloseKey(key);}} close{app_key};
    const auto write=[&](const void *data,DWORD bytes,DWORD type=REG_BINARY){require(RegSetValueExW(app_key,L"KeyboardMappingV1",0,type,static_cast<const BYTE *>(data),bytes)==ERROR_SUCCESS,"could not write malformed test preference");};
    DWORD version=1;write(&version,sizeof(version));
    require(load_windows_mapping()==balanced(),"short preference must fall back to Balanced");
    std::array<DWORD,button_count+1> record{};record[0]=2;
    for(unsigned i=0;i<button_count;++i)record[i+1]=custom[i];
    write(record.data(),sizeof(record));require(load_windows_mapping()==balanced(),"unknown version must fall back to Balanced");
    record[0]=1;record[1]=999;write(record.data(),sizeof(record));
    require(load_windows_mapping()==balanced(),"unknown key must fall back to Balanced");
    record[1]=custom[0];record[2]=record[1];write(record.data(),sizeof(record));
    require(load_windows_mapping()==balanced(),"duplicate saved keys must fall back to Balanced");
    constexpr wchar_t invalid[]=L"not a mapping";write(invalid,sizeof(invalid),REG_SZ);
    require(load_windows_mapping()==balanced(),"wrong registry type must fall back to Balanced");
    auto aliases=classic();aliases[6]=60;aliases[7]=76;save_windows_mapping(aliases);
    require(load_windows_mapping()==classic(),"Shift and Enter aliases must be canonicalized in preferences");
}
void presenter_tests(HWND owner) {
    using namespace matcha::keyboard;
    auto mapping=balanced();
    require(!run(owner,mapping,{
        [](HWND d){require(window_text(GetDlgItem(d,104))==L"A: L","Balanced A label missing");click(d,201);},
        [](HWND d){require(window_text(GetDlgItem(d,104))==L"A: Z","Classic preset was not shown");click(d,IDCANCEL);}}),"Cancel incorrectly applied changes");
    require(mapping==balanced(),"Cancel changed the original mapping");
    require(run(owner,mapping,{[](HWND d){click(d,201);},[](HWND d){click(d,IDOK);}}),"Classic Apply failed");
    require(mapping==classic(),"Classic preset mismatch");
    mapping=balanced();
    require(run(owner,mapping,{
        [](HWND d){click(d,109);key(d,'O',0x18);},
        [](HWND d){require(window_text(GetDlgItem(d,109))==L"R shoulder: O","physical O assignment failed");click(d,IDOK);}}),"custom Apply failed");
    require(mapping[9]==31,"custom assignment did not leave the dialog");
    mapping=balanced();
    require(run(owner,mapping,{
        [](HWND d){click(d,109);key(d,'L',0x26);},
        [](HWND d){require(!IsWindowEnabled(GetDlgItem(d,IDOK)),"duplicate keys left Apply enabled");require(window_text(GetDlgItem(d,203)).find(L"assigned to both")!=std::wstring::npos,"duplicate warning missing");key(d,VK_RETURN,0x1c);},
        [](HWND d){require(IsWindow(d)!=FALSE,"Enter applied an invalid mapping");click(d,200);},
        [](HWND d){require(IsWindowEnabled(GetDlgItem(d,IDOK))!=FALSE,"preset did not resolve invalid mapping");click(d,IDOK);}}),"fixed duplicate mapping could not apply");
    require(mapping==balanced(),"duplicate recovery changed the Balanced preset");
    require(!run(owner,mapping,{
        [](HWND d){click(d,109);key(d,VK_TAB,0x0f);},
        [](HWND d){require(window_text(GetDlgItem(d,203)).find(L"reserved")!=std::wstring::npos,"reserved key warning missing");require(!IsWindowEnabled(GetDlgItem(d,IDOK)),"Apply enabled during capture");key(d,VK_ESCAPE,0x01);},
        [](HWND d){require(IsWindowEnabled(GetDlgItem(d,IDOK))!=FALSE,"Escape did not stop capture");key(d,VK_ESCAPE,0x01);}}),"Escape incorrectly applied changes");
    require(mapping==balanced(),"reserved capture changed mapping");
    require(run(owner,mapping,{
        [](HWND d){click(d,107);key(d,VK_RETURN,0x1c);},
        [](HWND d){require(IsWindow(d)!=FALSE,"captured Enter applied prematurely");require(IsWindowEnabled(GetDlgItem(d,IDOK))!=FALSE,"Enter capture did not finish");click(d,106);key(d,VK_SPACE,0x39);},
        [](HWND d){require(IsWindowEnabled(GetDlgItem(d,IDOK))!=FALSE,"captured Space reactivated the key button");click(d,IDOK);}}),"Enter/Space capture failed");
    require(mapping==balanced(),"Enter/Space capture changed unrelated keys");
    require(run(owner,mapping,{
        [](HWND d){click(d,109);key(d,'O',0x18,false,true);},
        [](HWND d){require(!IsWindowEnabled(GetDlgItem(d,IDOK)),"repeated key was captured");key(d,'O',0x18);},
        [](HWND d){click(d,IDOK);}}),"non-repeat assignment after repeat failed");
    require(mapping[9]==31,"non-repeat O was not saved");
    mapping=balanced();
    struct Physical{unsigned vk,scan;bool extended;};
    constexpr std::array<Physical,button_count> classic_keys{{
        {VK_RIGHT,0x4d,true},{VK_LEFT,0x4b,true},{VK_UP,0x48,true},{VK_DOWN,0x50,true},
        {'Z',0x2c,false},{'X',0x2d,false},{VK_SHIFT,0x36,false},{VK_RETURN,0x1c,true},{'Q',0x10,false},{'W',0x11,false}}};
    Steps all{[](HWND d){click(d,202);}};
    for(const auto physical:classic_keys)all.push_back([physical](HWND d){require(!IsWindowEnabled(GetDlgItem(d,IDOK)),"Set All enabled Apply before completion");key(d,physical.vk,physical.scan,physical.extended);});
    all.push_back([](HWND d){require(IsWindowEnabled(GetDlgItem(d,IDOK))!=FALSE,"Set All remained invalid after completion");click(d,IDOK);});
    require(run(owner,mapping,std::move(all)),"Set All did not apply");require(mapping==classic(),"Set All key order or aliases incorrect");
    require(!run(owner,mapping,{[](HWND d){click(d,100);click(d,IDCANCEL);}}),"Cancel button during capture failed");
    require(mapping==classic(),"canceling armed capture changed mapping");
    require(!run(owner,mapping,{[](HWND d){click(d,200);PostMessageW(d,WM_CLOSE,0,0);}}),"window close incorrectly applied");
    require(mapping==classic(),"window close changed mapping");
    require(!run(owner,mapping,{[](HWND){PostQuitMessage(23);}}),"WM_QUIT incorrectly applied");
    MSG quit{};require(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)!=FALSE&&quit.wParam==23,"nested pump did not preserve WM_QUIT");
}
} // namespace

int main() {
    try {
        persistence_tests();
        WNDCLASSW kind{};kind.lpfnWndProc=owner_proc;kind.hInstance=GetModuleHandleW(nullptr);kind.lpszClassName=L"MatchaboyKeyboardTestOwner";
        require(RegisterClassW(&kind)!=0,"could not register native test owner");
        HWND owner=CreateWindowExW(0,kind.lpszClassName,L"Matchaboy keyboard test",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,kind.hInstance,nullptr);
        require(owner!=nullptr,"could not create native test owner");
        struct Cleanup{HWND window;~Cleanup(){DestroyWindow(window);}} cleanup{owner};
        presenter_tests(owner);
        std::printf("Windows keyboard presenter: %u checks passed\n",checks);return 0;
    }catch(const std::exception &error){std::fprintf(stderr,"Windows keyboard presenter failed: %s\n",error.what());return 1;}
}
