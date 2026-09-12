#include "windows_keyboard_settings.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

namespace {
using matcha::keyboard::Mapping;
constexpr wchar_t registry_path[]=L"Software\\Matchaboy";
constexpr wchar_t registry_value[]=L"KeyboardMappingV1";
constexpr DWORD mapping_version=1;
constexpr unsigned first_binding_id=100;
constexpr unsigned balanced_id=200,classic_id=201,set_all_id=202,status_id=203;
constexpr int logical_width=570,logical_height=602;

std::wstring wide(const std::string &text) {
    // Mapping labels contain only ASCII key names and controller names.
    return {text.begin(),text.end()};
}
std::runtime_error windows_error(const char *message,DWORD error=GetLastError()) {
    return std::runtime_error(std::string(message)+" (Windows error "+std::to_string(error)+")");
}
struct RegistryKey {
    HKEY value{};
    ~RegistryKey(){if(value)RegCloseKey(value);}
};

struct Settings {
    Mapping draft;
    HWND window{},status{},apply{};
    std::array<HWND,matcha::keyboard::button_count> key_buttons{};
    std::array<bool,256> suppressed_keys{};
    HFONT font{};
    UINT dpi=96;
    int capture=-1;
    bool sequential=false,done=false,accepted=false,warning=false;
    std::exception_ptr failure;

    explicit Settings(const Mapping &initial):draft(initial){}
    ~Settings(){if(window&&IsWindow(window))DestroyWindow(window);if(font)DeleteObject(font);}
    int px(int value)const{return MulDiv(value,static_cast<int>(dpi),96);}
    void set_status(const std::string &message,bool is_warning) {
        warning=is_warning;
        const auto text=wide(message);
        SetWindowTextW(status,text.c_str());InvalidateRect(status,nullptr,TRUE);
    }
    void refresh() {
        for(unsigned i=0;i<matcha::keyboard::button_count;++i) {
            const auto name=std::string(matcha::keyboard::button_names[i]);
            const auto value=capture==static_cast<int>(i)?"Press a key...":matcha::keyboard::key_name(draft[i]);
            const auto title=wide(name+": "+value);
            SetWindowTextW(key_buttons[i],title.c_str());
        }
        const auto error=matcha::keyboard::validate_mapping(draft);
        EnableWindow(apply,capture<0&&error.empty());
        if(capture>=0) {
            const auto prompt="Press a key for "+std::string(matcha::keyboard::button_names[static_cast<unsigned>(capture)])+". Esc cancels capture.";
            set_status(error.empty()?prompt:prompt+"\n"+error,!error.empty());
        } else set_status(error.empty()?"Bindings use physical key positions. Both Enter keys and both Shift keys are equivalent.":error,!error.empty());
    }
    void stop_capture(){capture=-1;sequential=false;}
    void begin_capture(unsigned button,bool all=false) {
        if(button>=matcha::keyboard::button_count)return;
        capture=static_cast<int>(button);sequential=all;
        SetFocus(key_buttons[button]);refresh();
    }
    void finish(bool apply_changes) {
        if(apply_changes&&(capture>=0||!matcha::keyboard::validate_mapping(draft).empty()))return;
        accepted=apply_changes;done=true;stop_capture();DestroyWindow(window);
    }
    void command(unsigned command_id) {
        if(command_id>=first_binding_id&&command_id<first_binding_id+matcha::keyboard::button_count)
            begin_capture(command_id-first_binding_id);
        else if(command_id==balanced_id){stop_capture();draft=matcha::keyboard::balanced();refresh();}
        else if(command_id==classic_id){stop_capture();draft=matcha::keyboard::classic();refresh();}
        else if(command_id==set_all_id)begin_capture(0,true);
        else if(command_id==IDOK)finish(true);
        else if(command_id==IDCANCEL){if(capture>=0){stop_capture();refresh();}else finish(false);}
    }
    bool capture_message(const MSG &message) {
        if(message.hwnd!=window&&!IsChild(window,message.hwnd))return false;
        const bool down=message.message==WM_KEYDOWN||message.message==WM_SYSKEYDOWN;
        const bool up=message.message==WM_KEYUP||message.message==WM_SYSKEYUP;
        if(!down&&!up)return false;
        const auto vk=static_cast<unsigned>(message.wParam);
        // Consume release/repeat messages for a captured key too. In particular,
        // assigning Space must never activate its formerly focused key button.
        if(vk<suppressed_keys.size()&&suppressed_keys[vk]) {
            if(up)suppressed_keys[vk]=false;
            return true;
        }
        if(capture<0||!down)return false;
        if((message.lParam&(LPARAM{1}<<30))!=0)return true;
        if(vk<suppressed_keys.size())suppressed_keys[vk]=true;
        if(vk==VK_ESCAPE){stop_capture();refresh();return true;}
        if((GetKeyState(VK_CONTROL)&0x8000)||(GetKeyState(VK_MENU)&0x8000)||
           (GetKeyState(VK_LWIN)&0x8000)||(GetKeyState(VK_RWIN)&0x8000)) {
            set_status("Choose a physical key without Ctrl, Alt, or Windows held.",true);return true;
        }
        const auto scan=static_cast<unsigned>((message.lParam>>16)&0xff);
        const bool extended=(message.lParam&(LPARAM{1}<<24))!=0;
        const auto key=matcha::keyboard::from_windows_key(scan,extended,vk);
        if(matcha::keyboard::reserved_key(key)||!matcha::keyboard::supported_key(key)) {
            set_status(matcha::keyboard::key_name(key)+(matcha::keyboard::reserved_key(key)?" is reserved for emulator controls.":" cannot be used as a game button."),true);
            return true;
        }
        draft[static_cast<unsigned>(capture)]=matcha::keyboard::canonical_key(key);
        if(sequential&&capture+1<static_cast<int>(matcha::keyboard::button_count))
            begin_capture(static_cast<unsigned>(capture+1),true);
        else {stop_capture();refresh();}
        return true;
    }
    HWND control(const wchar_t *kind,const wchar_t *title,DWORD style,int x,int y,int width,int height,unsigned id) {
        HWND child=CreateWindowExW(0,kind,title,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),window,
                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!child)throw windows_error("Could not create a keyboard settings control");
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return child;
    }
    void initialize(HWND parent) {
        dpi=parent?GetDpiForWindow(parent):GetDpiForSystem();if(!dpi)dpi=96;
        font=CreateFontW(-MulDiv(10,static_cast<int>(dpi),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        if(!font)throw windows_error("Could not create the keyboard settings font");
        SetWindowTextW(window,L"Keyboard Controls");
        SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        control(L"STATIC",L"Choose the keys that feel right to you.",SS_LEFT,20,16,530,22,0);
        control(L"STATIC",L"Click a key, then press its replacement. Esc cancels capture. Esc, Tab, and F12 are reserved.",SS_LEFT,20,44,530,38,0);
        control(L"STATIC",L"GAME BUTTON",SS_LEFT,20,94,150,20,0);
        control(L"STATIC",L"KEY ASSIGNMENT",SS_LEFT,178,94,372,20,0);
        for(unsigned i=0;i<matcha::keyboard::button_count;++i) {
            const auto name=wide(std::string(matcha::keyboard::button_names[i]));const int y=118+static_cast<int>(i)*30;
            control(L"STATIC",name.c_str(),SS_LEFT,20,y+4,150,22,0);
            key_buttons[i]=control(L"BUTTON",L"",WS_TABSTOP|BS_PUSHBUTTON,178,y,372,26,first_binding_id+i);
        }
        control(L"BUTTON",L"&Balanced",WS_TABSTOP|BS_PUSHBUTTON,20,432,130,30,balanced_id);
        control(L"BUTTON",L"&Classic",WS_TABSTOP|BS_PUSHBUTTON,160,432,130,30,classic_id);
        control(L"BUTTON",L"Set &all keys...",WS_TABSTOP|BS_PUSHBUTTON,340,432,210,30,set_all_id);
        status=control(L"STATIC",L"",SS_LEFT,20,478,530,62,status_id);
        apply=control(L"BUTTON",L"&Apply",WS_TABSTOP|BS_DEFPUSHBUTTON,340,552,100,30,IDOK);
        control(L"BUTTON",L"Cancel",WS_TABSTOP|BS_PUSHBUTTON,450,552,100,30,IDCANCEL);
        SendMessageW(window,DM_SETDEFID,IDOK,0);
        RECT bounds{0,0,px(logical_width),px(logical_height)};
        if(!AdjustWindowRectExForDpi(&bounds,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,
                                    static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi))
            throw windows_error("Could not size keyboard settings");
        MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        if(!GetMonitorInfoW(MonitorFromWindow(parent?parent:window,MONITOR_DEFAULTTONEAREST),&monitor))
            throw windows_error("Could not locate the keyboard settings display");
        RECT owner=monitor.rcWork;if(parent)GetWindowRect(parent,&owner);
        const auto width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        const auto x=std::max(monitor.rcWork.left,std::min(owner.left+(owner.right-owner.left-width)/2,monitor.rcWork.right-width));
        const auto y=std::max(monitor.rcWork.top,std::min(owner.top+(owner.bottom-owner.top-height)/2,monitor.rcWork.bottom-height));
        SetWindowPos(window,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
        refresh();
    }
};

INT_PTR CALLBACK settings_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) noexcept {
    auto *state=reinterpret_cast<Settings *>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG) {
        state=reinterpret_cast<Settings *>(lparam);state->window=window;
        SetWindowLongPtrW(window,DWLP_USER,reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }
    if(!state)return FALSE;
    try {
        switch(message) {
        case WM_COMMAND:
            if(HIWORD(wparam)==BN_CLICKED) {
                // Escape stops key capture first; clicking Cancel explicitly
                // closes the editor, even while a capture is armed.
                if(LOWORD(wparam)==IDCANCEL&&reinterpret_cast<HWND>(lparam)==GetDlgItem(window,IDCANCEL))state->finish(false);
                else state->command(LOWORD(wparam));
                return TRUE;
            }
            break;
        case WM_CLOSE:state->finish(false);return TRUE;
        case WM_CTLCOLORSTATIC:
            if(reinterpret_cast<HWND>(lparam)==state->status) {
                const auto dc=reinterpret_cast<HDC>(wparam);
                SetTextColor(dc,state->warning?RGB(160,30,15):GetSysColor(COLOR_WINDOWTEXT));
                SetBkColor(dc,GetSysColor(COLOR_3DFACE));
                return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_3DFACE));
            }
            break;
        case WM_NCDESTROY:state->window=nullptr;state->done=true;break;
        default:break;
        }
    } catch(...) {
        state->failure=std::current_exception();state->done=true;DestroyWindow(window);return TRUE;
    }
    return FALSE;
}
} // namespace

matcha::keyboard::Mapping matcha::keyboard::load_windows_mapping() {
    std::array<DWORD,button_count+1> record{};DWORD bytes=sizeof(record),type{};
    if(RegGetValueW(HKEY_CURRENT_USER,registry_path,registry_value,RRF_RT_REG_BINARY,&type,record.data(),&bytes)!=ERROR_SUCCESS||
       type!=REG_BINARY||bytes!=sizeof(record)||record[0]!=mapping_version)return balanced();
    Mapping mapping{};
    for(unsigned i=0;i<button_count;++i)mapping[i]=canonical_key(record[i+1]);
    return validate_mapping(mapping).empty()?mapping:balanced();
}

void matcha::keyboard::save_windows_mapping(const Mapping &mapping) {
    const auto error=validate_mapping(mapping);if(!error.empty())throw std::invalid_argument(error);
    std::array<DWORD,button_count+1> record{};record[0]=mapping_version;
    for(unsigned i=0;i<button_count;++i)record[i+1]=canonical_key(mapping[i]);
    RegistryKey key;
    auto result=RegCreateKeyExW(HKEY_CURRENT_USER,registry_path,0,nullptr,0,KEY_SET_VALUE,nullptr,&key.value,nullptr);
    if(result!=ERROR_SUCCESS)throw windows_error("Windows could not open Matchaboy keyboard preferences",static_cast<DWORD>(result));
    // One registry value holds the complete versioned record, so readers cannot
    // observe a mixture of old and new individual button assignments.
    result=RegSetValueExW(key.value,registry_value,0,REG_BINARY,reinterpret_cast<const BYTE *>(record.data()),static_cast<DWORD>(sizeof(record)));
    if(result!=ERROR_SUCCESS)throw windows_error("Windows could not save Matchaboy keyboard preferences",static_cast<DWORD>(result));
}

bool matcha::keyboard::show_windows_settings(HWND parent,Mapping &draft_out) {
    Settings state(draft_out);
    // A native dialog without resource-file dependencies. Controls are ordinary
    // Win32 buttons/static labels, with standard accessibility and tab traversal.
    alignas(DWORD) std::array<std::byte,sizeof(DLGTEMPLATE)+3*sizeof(WORD)> storage{};
    DLGTEMPLATE spec{};spec.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;
    spec.dwExtendedStyle=WS_EX_CONTROLPARENT;spec.cx=360;spec.cy=380;
    std::memcpy(storage.data(),&spec,sizeof(spec));
    HWND dialog=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE *>(storage.data()),
                                           parent,settings_proc,reinterpret_cast<LPARAM>(&state));
    if(!dialog)throw windows_error("Could not open keyboard settings");
    state.initialize(parent);
    const bool restore_parent=parent&&IsWindowEnabled(parent);
    HWND previous_focus=GetFocus();
    struct RestoreOwner {
        Settings &state;HWND parent,focus;bool enabled;
        ~RestoreOwner(){
            if(state.window&&IsWindow(state.window))DestroyWindow(state.window);
            if(enabled&&IsWindow(parent)){EnableWindow(parent,TRUE);SetActiveWindow(parent);if(IsWindow(focus))SetFocus(focus);}
        }
    } restore{state,parent,previous_focus,restore_parent};
    if(restore_parent)EnableWindow(parent,FALSE);
    ShowWindow(dialog,SW_SHOW);SetActiveWindow(dialog);SetFocus(state.key_buttons[0]);
    bool quit=false;int exit_code=0;
    while(!state.done) {
        MSG message{};const BOOL received=GetMessageW(&message,nullptr,0,0);
        if(received<0)throw windows_error("Windows could not process keyboard settings");
        if(!received){quit=true;exit_code=static_cast<int>(message.wParam);break;}
        if(state.capture_message(message))continue;
        const bool dialog_message=message.hwnd==dialog||IsChild(dialog,message.hwnd);
        if(dialog_message&&IsDialogMessageW(dialog,&message))continue;
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    if(quit)PostQuitMessage(exit_code);
    if(state.failure)std::rethrow_exception(state.failure);
    if(quit||!state.accepted||!validate_mapping(state.draft).empty())return false;
    draft_out=state.draft;return true;
}
