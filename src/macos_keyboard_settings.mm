#import <AppKit/AppKit.h>
#include "macos_keyboard_settings.hpp"
#include <algorithm>
#include <array>
#include <string>

namespace {
NSString *native_key_text(const std::string &text) {return [NSString stringWithUTF8String:text.c_str()];}
NSTextField *label(NSString *text,NSRect bounds) {
    NSTextField *field=[NSTextField labelWithString:text];[field setFrame:bounds];
    [field setFont:[NSFont systemFontOfSize:13]];return field;
}
}

@interface MatchaKeyboardSettingsController:NSObject {
@public
    matcha::keyboard::Mapping draft_;
    NSAlert *alert_;
    std::array<NSButton *,matcha::keyboard::button_count> key_buttons_;
    NSTextField *status_;
    id monitor_;
    NSInteger capture_index_;
    BOOL sequential_;
}
- (instancetype)initWithMapping:(const matcha::keyboard::Mapping &)mapping;
- (void)refresh;
- (void)stopCapture;
- (void)beginCapture:(NSInteger)index;
- (NSEvent *)captureEvent:(NSEvent *)event;
- (void)changeKey:(id)sender;
- (void)balancedPreset:(id)sender;
- (void)classicPreset:(id)sender;
- (void)setAllKeys:(id)sender;
@end

@implementation MatchaKeyboardSettingsController
- (instancetype)initWithMapping:(const matcha::keyboard::Mapping &)mapping {
    self=[super init];if(!self)return nil;
    draft_=mapping;capture_index_=-1;sequential_=NO;monitor_=nil;
    alert_=[[NSAlert alloc]init];
    [alert_ setMessageText:@"Keyboard Controls"];
    [alert_ setInformativeText:@"Click a key, then press its replacement. Esc cancels capture. Esc, Tab, and F12 are reserved."];
    [alert_ addButtonWithTitle:@"Apply"];[alert_ addButtonWithTitle:@"Cancel"];
    [[alert_ buttons][0]setKeyEquivalent:@"\r"];
    [[alert_ buttons][1]setKeyEquivalent:@"\033"];

    NSView *content=[[[NSView alloc]initWithFrame:NSMakeRect(0,0,540,450)]autorelease];
    NSTextField *button_heading=label(@"GAME BUTTON",NSMakeRect(0,426,190,18));
    NSTextField *key_heading=label(@"KEY",NSMakeRect(208,426,300,18));
    [button_heading setTextColor:[NSColor secondaryLabelColor]];[key_heading setTextColor:[NSColor secondaryLabelColor]];
    [content addSubview:button_heading];[content addSubview:key_heading];
    for(unsigned i=0;i<matcha::keyboard::button_count;++i) {
        const auto name=native_key_text(std::string(matcha::keyboard::button_names[i]));
        const CGFloat y=390-static_cast<CGFloat>(i)*30;
        NSTextField *row=label(name,NSMakeRect(0,y+5,195,20));[content addSubview:row];
        NSButton *key=[NSButton buttonWithTitle:@"" target:self action:@selector(changeKey:)];
        [key setFrame:NSMakeRect(200,y,332,28)];[key setBezelStyle:NSBezelStyleRounded];[key setTag:i];
        [key setAccessibilityLabel:[NSString stringWithFormat:@"%@ key assignment",name]];
        [key setAccessibilityHelp:@"Choose this button, then press the replacement key."];
        [content addSubview:key];key_buttons_[i]=key;
    }
    NSButton *balanced=[NSButton buttonWithTitle:@"Balanced" target:self action:@selector(balancedPreset:)];
    [balanced setFrame:NSMakeRect(0,72,150,30)];[balanced setBezelStyle:NSBezelStyleRounded];
    [balanced setAccessibilityHelp:@"Use WASD, L and K, Q and I, Enter and Space."];
    NSButton *classic=[NSButton buttonWithTitle:@"Classic" target:self action:@selector(classicPreset:)];
    [classic setFrame:NSMakeRect(158,72,150,30)];[classic setBezelStyle:NSBezelStyleRounded];
    [classic setAccessibilityHelp:@"Use arrows, Z and X, Q and W, Enter and Shift."];
    NSButton *all=[NSButton buttonWithTitle:@"Set all keys…" target:self action:@selector(setAllKeys:)];
    [all setFrame:NSMakeRect(340,72,192,30)];[all setBezelStyle:NSBezelStyleRounded];
    [content addSubview:balanced];[content addSubview:classic];[content addSubview:all];
    status_=label(@"",NSMakeRect(0,4,532,60));[status_ setMaximumNumberOfLines:3];
    [[status_ cell]setWraps:YES];[[status_ cell]setScrollable:NO];
    [status_ setAccessibilityLabel:@"Keyboard mapping status"];
    [content addSubview:status_];[alert_ setAccessoryView:content];
    [self refresh];return self;
}
- (void)dealloc {
    [self stopCapture];[alert_ release];[super dealloc];
}
- (void)refresh {
    for(unsigned i=0;i<matcha::keyboard::button_count;++i) {
        NSString *title=capture_index_==static_cast<NSInteger>(i)?@"Press a key…":native_key_text(matcha::keyboard::key_name(draft_[i]));
        [key_buttons_[i]setTitle:title];
    }
    const auto error=matcha::keyboard::validate_mapping(draft_);
    [[alert_ buttons][0]setEnabled:capture_index_<0&&error.empty()];
    if(capture_index_>=0) {
        const auto name=matcha::keyboard::button_names[static_cast<unsigned>(capture_index_)];
        const std::string prompt="Press a key for "+std::string(name)+". Esc cancels capture.";
        [status_ setStringValue:native_key_text(error.empty()?prompt:prompt+"\n"+error)];
        [status_ setTextColor:error.empty()?[NSColor secondaryLabelColor]:[NSColor systemRedColor]];
    } else {
        [status_ setStringValue:error.empty()?@"Bindings use physical key positions. Both Enter keys and both Shift keys are equivalent.":native_key_text(error)];
        [status_ setTextColor:error.empty()?[NSColor secondaryLabelColor]:[NSColor systemRedColor]];
    }
}
- (void)stopCapture {
    if(monitor_){[NSEvent removeMonitor:monitor_];[monitor_ release];monitor_=nil;}
    capture_index_=-1;sequential_=NO;
}
- (void)beginCapture:(NSInteger)index {
    if(index<0||index>=static_cast<NSInteger>(matcha::keyboard::button_count))return;
    capture_index_=index;
    if(!monitor_) {
        // A local monitor needs no Accessibility/Input Monitoring permission.
        // Removal by the presenter breaks this block's temporary self retain.
        monitor_=[[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown|NSEventMaskFlagsChanged
            handler:^NSEvent *(NSEvent *event){return [self captureEvent:event];}]retain];
    }
    [[alert_ window]makeFirstResponder:key_buttons_[static_cast<unsigned>(index)]];
    [self refresh];
}
- (NSEvent *)captureEvent:(NSEvent *)event {
    if(capture_index_<0||[event window]!=[alert_ window])return event;
    const auto key=static_cast<unsigned>([event keyCode]);
    const auto modifiers=[event modifierFlags];
    if([event type]==NSEventTypeKeyDown) {
        if([event isARepeat])return nil;
        if(key==53){[self stopCapture];[self refresh];return nil;}
    } else {
        // Only a Shift press is assignable through flagsChanged. Releases and
        // unrelated modifier transitions do not become controller bindings.
        if((key!=56&&key!=60)||!(modifiers&NSEventModifierFlagShift))return event;
    }
    if(modifiers&(NSEventModifierFlagCommand|NSEventModifierFlagControl|NSEventModifierFlagOption)) {
        [status_ setStringValue:@"Choose a physical key without Command, Control, or Option held."];
        [status_ setTextColor:[NSColor systemRedColor]];return nil;
    }
    if(matcha::keyboard::reserved_key(key)||!matcha::keyboard::supported_key(key)) {
        const auto message=matcha::keyboard::key_name(key)+(matcha::keyboard::reserved_key(key)?" is reserved for emulator controls.":" cannot be used as a game button.");
        [status_ setStringValue:native_key_text(message)];[status_ setTextColor:[NSColor systemRedColor]];return nil;
    }
    draft_[static_cast<unsigned>(capture_index_)]=matcha::keyboard::canonical_key(key);
    if(sequential_&&capture_index_+1<static_cast<NSInteger>(matcha::keyboard::button_count)) {
        ++capture_index_;[self beginCapture:capture_index_];
    } else {[self stopCapture];[self refresh];}
    return nil;
}
- (void)changeKey:(id)sender {
    [self stopCapture];[self beginCapture:[sender tag]];
}
- (void)balancedPreset:(id)sender {
    (void)sender;[self stopCapture];draft_=matcha::keyboard::balanced();[self refresh];
}
- (void)classicPreset:(id)sender {
    (void)sender;[self stopCapture];draft_=matcha::keyboard::classic();[self refresh];
}
- (void)setAllKeys:(id)sender {
    (void)sender;[self stopCapture];sequential_=YES;[self beginCapture:0];
}
@end

bool matcha::keyboard::show_settings(NSWindow *parent,Mapping &draft_out) {
    MatchaKeyboardSettingsController *controller=[[MatchaKeyboardSettingsController alloc]initWithMapping:draft_out];
    struct Cleanup {
        MatchaKeyboardSettingsController *controller;
        ~Cleanup(){[controller stopCapture];[controller release];}
    } cleanup{controller};
    // AppKit sizes the alert to its accessory view. Center on the game's
    // display instead of opening settings on a different attached monitor.
    [controller->alert_ layout];
    if(parent&&[parent screen]) {
        NSWindow *dialog=[controller->alert_ window];const NSRect parent_frame=[parent frame];
        const NSRect visible=[[parent screen]visibleFrame];const NSRect bounds=[dialog frame];
        const CGFloat x=std::max(NSMinX(visible),std::min(NSMidX(parent_frame)-bounds.size.width/2,NSMaxX(visible)-bounds.size.width));
        const CGFloat y=std::max(NSMinY(visible),std::min(NSMidY(parent_frame)-bounds.size.height/2,NSMaxY(visible)-bounds.size.height));
        [dialog setFrameOrigin:NSMakePoint(x,y)];
    }
    const auto result=[controller->alert_ runModal];
    if(result!=NSAlertFirstButtonReturn||controller->capture_index_>=0||!validate_mapping(controller->draft_).empty())return false;
    draft_out=controller->draft_;return true;
}
