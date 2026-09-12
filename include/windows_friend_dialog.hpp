#pragma once
#include <windows.h>
#include "friend_transport.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

namespace matcha::windows {
inline std::wstring friend_wide(const std::string &value) {
    if(value.empty())return {};
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!count)throw std::runtime_error("Invalid UTF-8 connection text.");
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count);
    return result;
}
inline std::string friend_utf8(const std::wstring &value) {
    if(value.empty())return {};
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(!count)throw std::runtime_error("Invalid connection text.");
    std::string result(static_cast<std::size_t>(count),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr);
    return result;
}
inline void copy_friend_text(HWND owner,const std::string &text) {
    const auto value=friend_wide(text);
    HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,(value.size()+1)*sizeof(wchar_t));
    if(!data)throw std::runtime_error("Unable to allocate clipboard text.");
    auto *target=static_cast<wchar_t *>(GlobalLock(data));
    if(!target){GlobalFree(data);throw std::runtime_error("Unable to prepare clipboard text.");}
    std::copy(value.c_str(),value.c_str()+value.size()+1,target);GlobalUnlock(data);
    if(!OpenClipboard(owner)){GlobalFree(data);throw std::runtime_error("Clipboard is busy. Try again.");}
    const bool copied=EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,data);
    CloseClipboard();
    if(!copied){GlobalFree(data);throw std::runtime_error("Unable to copy clipboard text.");}
}
// This modal window dispatches the owner's WM_TIMER messages too. A live link
// must continue servicing UDP and confirmed frames while a dialog is open.
class FriendDialog {
    HWND owner_{},window_{},address_{},port_{},code_{},error_{};
    bool details_{},done_{},accepted_{};
    FriendTransportOptions options_;
    std::string description_;
    std::function<std::string()> diagnostics_;
    static std::wstring value(HWND field) {
        const int length=GetWindowTextLengthW(field);
        std::wstring result(static_cast<std::size_t>(length)+1,L'\0');
        GetWindowTextW(field,result.data(),length+1);result.resize(static_cast<std::size_t>(length));return result;
    }
    HWND control(const wchar_t *type,const std::wstring &label,DWORD style,int x,int y,int width,int height,unsigned id=0) {
        auto child=CreateWindowExW(std::wstring(type)==L"EDIT"?WS_EX_CLIENTEDGE:0,type,label.c_str(),WS_CHILD|WS_VISIBLE|style,
            x,y,width,height,window_,reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!child)throw std::runtime_error("Unable to create friend play dialog controls.");
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return child;
    }
    void initialize() {
        if(details_){
            control(L"EDIT",friend_wide(description_),ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,20,20,510,270);
            error_=control(L"STATIC",L"",0,20,300,510,36);
            control(L"BUTTON",L"Close",BS_DEFPUSHBUTTON|WS_TABSTOP,420,354,110,30,IDOK);
            control(L"BUTTON",L"Copy diagnostics",BS_PUSHBUTTON|WS_TABSTOP,20,354,175,30,3);
            control(L"BUTTON",L"Copy room code",BS_PUSHBUTTON|WS_TABSTOP,205,354,170,30,4);
        }else{
            control(L"STATIC",L"The game restarts from saved progress. Both players need the exact same ROM. On the same Wi-Fi, use the host's LAN IP. Internet play needs a VPN or UDP port forwarding; no relay is provided. Room codes and game data are unencrypted.",0,20,20,510,86);
            control(L"STATIC",options_.host?L"Listen address (all local interfaces)":L"Host IP or name",0,20,115,510,20);
            address_=control(L"EDIT",friend_wide(options_.address),WS_TABSTOP|ES_AUTOHSCROLL|(options_.host?ES_READONLY:0),20,138,510,26,10);
            control(L"STATIC",L"UDP port",0,20,177,510,20);
            port_=control(L"EDIT",std::to_wstring(options_.port),WS_TABSTOP|ES_NUMBER,20,200,510,26,11);
            control(L"STATIC",L"Room code (32 hexadecimal characters)",0,20,239,510,20);
            code_=control(L"EDIT",friend_wide(options_.code),WS_TABSTOP|ES_PASSWORD|ES_AUTOHSCROLL|(options_.host?ES_READONLY:0),20,262,510,26,12);
            SendMessageW(code_,EM_SETLIMITTEXT,32,0);
            error_=control(L"STATIC",L"",0,20,301,510,50);
            if(options_.host)control(L"BUTTON",L"Copy room code",BS_PUSHBUTTON|WS_TABSTOP,20,365,170,30,4);
            control(L"BUTTON",L"Cancel",BS_PUSHBUTTON|WS_TABSTOP,298,365,110,30,IDCANCEL);
            control(L"BUTTON",options_.host?L"Host game":L"Join game",BS_DEFPUSHBUTTON|WS_TABSTOP,420,365,110,30,IDOK);
        }
    }
    void command(unsigned id) {
        if(id==IDCANCEL){done_=true;return;}
        if(id==3){copy_friend_text(window_,diagnostics_());SetWindowTextW(error_,L"Diagnostics copied. No room code, address or save data is included.");return;}
        if(id==4){copy_friend_text(window_,options_.code);SetWindowTextW(error_,L"Room code copied.");return;}
        if(id!=IDOK)return;
        if(details_){done_=true;return;}
        const auto text=value(port_);
        if(text.empty()||text.size()>5||text.find_first_not_of(L"0123456789")!=std::wstring::npos)
            throw std::runtime_error("UDP port must be between 1 and 65535.");
        const auto number=std::stoul(text);
        if(number==0||number>65535)throw std::runtime_error("UDP port must be between 1 and 65535.");
        options_.port=static_cast<std::uint16_t>(number);options_.address=friend_utf8(value(address_));options_.code=friend_utf8(value(code_));
        if(options_.address.empty())throw std::runtime_error("Enter the host's IP address or name.");
        if(options_.code.size()!=32||!std::all_of(options_.code.begin(),options_.code.end(),[](unsigned char c){return std::isxdigit(c)!=0;}))
            throw std::runtime_error("Enter all 32 hexadecimal characters from your friend's room code.");
        accepted_=true;done_=true;
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
        auto *self=reinterpret_cast<FriendDialog *>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<FriendDialog *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);self->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,wp,lp);
        try{
            if(message==WM_COMMAND){self->command(LOWORD(wp));return 0;}
            if(message==WM_CLOSE){self->done_=true;return 0;}
            if(message==WM_NCDESTROY){self->done_=true;self->window_=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);}
        }catch(const std::exception &error){SetWindowTextW(self->error_,friend_wide(error.what()).c_str());return 0;}
        return DefWindowProcW(window,message,wp,lp);
    }
public:
    FriendDialog(HWND owner,FriendTransportOptions options):owner_(owner),options_(std::move(options)){}
    FriendDialog(HWND owner,std::string description,std::string code,std::function<std::string()> diagnostics):
        owner_(owner),details_(true),description_(std::move(description)),diagnostics_(std::move(diagnostics)){options_.code=std::move(code);}
    ~FriendDialog(){if(window_)DestroyWindow(window_);}
    std::optional<FriendTransportOptions> show() {
        WNDCLASSW type{};type.lpfnWndProc=procedure;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        type.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);type.lpszClassName=L"MatchaboyFriendDialog";
        if(!RegisterClassW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Unable to create friend play dialog.");
        RECT rect{0,0,550,details_?404:415};AdjustWindowRectEx(&rect,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT);
        RECT parent{};GetWindowRect(owner_,&parent);
        const auto title=details_?L"Friend play connection":options_.host?L"Host a game with a friend":L"Join a friend's game";
        window_=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,type.lpszClassName,title,WS_CAPTION|WS_SYSMENU,
            parent.left+40,parent.top+60,rect.right-rect.left,rect.bottom-rect.top,owner_,nullptr,type.hInstance,this);
        if(!window_)throw std::runtime_error("Unable to open friend play dialog.");
        initialize();const bool enabled=IsWindowEnabled(owner_)!=FALSE;EnableWindow(owner_,FALSE);ShowWindow(window_,SW_SHOW);
        SetFocus(details_?GetDlgItem(window_,IDOK):options_.host?port_:address_);
        struct OwnerGuard {
            HWND popup,owner;bool enabled;
            ~OwnerGuard(){if(IsWindow(popup))DestroyWindow(popup);EnableWindow(owner,enabled);SetForegroundWindow(owner);}
        } guard{window_,owner_,enabled};
        MSG message{};
        while(!done_){
            const auto result=GetMessageW(&message,nullptr,0,0);
            if(result<0)throw std::runtime_error("Unable to read friend play dialog messages.");
            if(result==0){PostQuitMessage(static_cast<int>(message.wParam));break;}
            const bool in_dialog=message.hwnd==window_||IsChild(window_,message.hwnd);
            if(!in_dialog||!IsDialogMessageW(window_,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        return accepted_?std::optional(options_):std::nullopt;
    }
};
} // namespace matcha::windows
