// Exercises the public production presenter through real AppKit modal loops.
// All NSEvents stay within this nonactivating process; no Accessibility,
// global input monitor, OS key injection, game ROM or user preferences are used.
// Requires a logged-in macOS WindowServer session. See CMake's opt-in
// MATCHA_RUN_NATIVE_GUI_TESTS option or run this executable directly.
#import <AppKit/AppKit.h>
#include "macos_keyboard_settings.hpp"
#include <iostream>
#include <stdexcept>
namespace {
unsigned assertions=0;
void check(bool yes,const char *message){++assertions;if(!yes)throw std::runtime_error(message);}
NSButton *find(NSView *view,NSString *text,bool accessibility=false){
 if([view isKindOfClass:[NSButton class]]){auto *button=static_cast<NSButton *>(view);NSString *value=accessibility?[button accessibilityLabel]:[button title];if([value isEqualToString:text])return button;}
 for(NSView *child in [view subviews])if(auto *found=find(child,text,accessibility))return found;
 return nil;
}
void key(NSWindow *window,unsigned code,NSString *text,NSEventModifierFlags flags=0,NSEventType type=NSEventTypeKeyDown,bool repeat=false){
 NSEvent *event=[NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:flags timestamp:0 windowNumber:[window windowNumber] context:nil characters:text charactersIgnoringModifiers:text isARepeat:repeat?YES:NO keyCode:static_cast<unsigned short>(code)];
 [NSApp postEvent:event atStart:NO];
}
}
@interface KeyboardProbeDriver:NSObject {
@public unsigned test_,step_,ticks_;std::string failure_;NSWindow *foreign_;
}
-(void)tick:(NSTimer *)timer;
@end
@implementation KeyboardProbeDriver
-(void)dealloc{[foreign_ release];[super dealloc];}
-(void)tick:(NSTimer *)timer{
 (void)timer;
 try {
  if(++ticks_>100)throw std::runtime_error("native dialog did not finish within four seconds");
  NSWindow *window=[NSApp modalWindow];if(!window)return;[window setAlphaValue:0];
  auto *root=[window contentView];auto *apply=find(root,@"Apply");auto *cancel=find(root,@"Cancel");
  check(apply&&cancel,"native Apply and Cancel exist");
  auto *a=find(root,@"A key assignment",true);auto *select=find(root,@"Select key assignment",true);
  check(a&&select,"native game buttons expose accessible labels");
  if(test_==0){[cancel performClick:nil];return;}
  if(test_==1){
   if(step_==0)[a performClick:nil];
   if(step_==1){check(![apply isEnabled],"Apply disabled while capturing");key(window,31,@"o");}
   if(step_==2){check([[a title]isEqualToString:@"O"]&&[apply isEnabled],"normal captured key updates assignment");[apply performClick:nil];}
  }else if(test_==2){
   if(step_==0)[a performClick:nil];
   if(step_==1)key(window,36,@"\r");
   if(step_==2){check([[a title]isEqualToString:@"Enter"]&&![apply isEnabled],"Enter binds without accidentally applying; duplicate disables Apply");[cancel performClick:nil];}
  }else if(test_==3){
   if(step_==0)[a performClick:nil];
   if(step_==1)key(window,53,@"\033");
   if(step_==2){check([[a title]isEqualToString:@"L"]&&[apply isEnabled],"first Escape cancels capture without closing dialog");[cancel performClick:nil];}
  }else if(test_==4){
   if(step_==0)[a performClick:nil];
   if(step_==1)key(window,48,@"\t");
   if(step_==2){check(![apply isEnabled]&&[[a title]isEqualToString:@"Press a key…"],"Tab stays reserved and does not navigate focus");key(window,111,@"");}
   if(step_==3){check(![apply isEnabled],"F12 stays reserved");key(window,31,@"o",NSEventModifierFlagCommand);}
   if(step_==4){check(![apply isEnabled],"Command chord does not become a binding");key(window,31,@"o",NSEventModifierFlagControl);}
   if(step_==5){check(![apply isEnabled],"Control chord does not become a binding");key(window,31,@"o",NSEventModifierFlagOption);}
   if(step_==6){check(![apply isEnabled],"Option chord does not become a binding");key(window,31,@"o");}
   if(step_==7)[apply performClick:nil];
  }else if(test_==5){
   if(step_==0)[select performClick:nil];
   if(step_==1)key(window,56,@"",0,NSEventTypeFlagsChanged);
   if(step_==2){check(![apply isEnabled],"modifier release is not captured");key(window,60,@"",NSEventModifierFlagShift,NSEventTypeFlagsChanged);}
   if(step_==3){check([[select title]isEqualToString:@"Shift"]&&[apply isEnabled],"right Shift captures canonical Shift binding");[apply performClick:nil];}
  }else if(test_==6){
   if(step_==0){auto *all=find(root,@"Set all keys…");check(all,"sequential capture control exists");[all performClick:nil];}
   if(step_>=1&&step_<=10){check(![apply isEnabled],"Apply stays disabled during sequential capture");const auto map=matcha::keyboard::balanced();key(window,map[step_-1],@"x");}
   if(step_==11){check([apply isEnabled],"complete valid sequential map enables Apply");[apply performClick:nil];}
  }else if(test_==7){
   if(step_==0){auto *classic=find(root,@"Classic");[classic performClick:nil];}
   if(step_==1){check([[a title]isEqualToString:@"Z"],"Classic preset updates UI immediately");auto *balanced=find(root,@"Balanced");[balanced performClick:nil];}
   if(step_==2){check([[a title]isEqualToString:@"L"],"Balanced preset restores user defaults");[apply performClick:nil];}
  }else if(test_==8){
   if(step_==0)[select performClick:nil];
   if(step_==1)key(window,49,@" ");
   if(step_==2){check([[select title]isEqualToString:@"Space"]&&[apply isEnabled],"Space captures without clicking the focused key button");[apply performClick:nil];}
  }else if(test_==9){
   if(step_==0)[a performClick:nil];
   if(step_==1)key(window,31,@"o",0,NSEventTypeKeyDown,true);
   if(step_==2){check(![apply isEnabled],"repeating an already-held key cannot assign a binding");key(window,31,@"o");}
   if(step_==3)[apply performClick:nil];
  }else if(test_==10){
   if(step_==0){foreign_=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,10,10) styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];[a performClick:nil];}
   if(step_==1)key(foreign_,31,@"o");
   if(step_==2){check(![apply isEnabled],"capture ignores keys addressed to a different process-local window");key(window,31,@"o");}
   if(step_==3)[apply performClick:nil];
  }else if(test_==11){
   if(step_==0)[a performClick:nil];
   if(step_==1){check(![apply isEnabled],"capture is active before cancelling the dialog");[cancel performClick:nil];}
  }
  ++step_;
 }catch(const std::exception &e){failure_=e.what();[NSApp abortModal];}
}
@end
int main(){@autoreleasepool{
 [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
 try {
  for(unsigned test=0;test<12;++test){
   KeyboardProbeDriver *driver=[[KeyboardProbeDriver alloc]init];driver->test_=test;
   NSTimer *timer=[NSTimer timerWithTimeInterval:0.04 target:driver selector:@selector(tick:) userInfo:nil repeats:YES];
   [[NSRunLoop mainRunLoop]addTimer:timer forMode:NSModalPanelRunLoopMode];
   auto map=matcha::keyboard::balanced();const bool accepted=matcha::keyboard::show_settings(nil,map);
   [timer invalidate];const auto error=driver->failure_;[driver release];if(!error.empty())throw std::runtime_error(error);
   if(test==0||test==2||test==3||test==11)check(!accepted&&map==matcha::keyboard::balanced(),"Cancel preserves caller mapping");
   else check(accepted&&matcha::keyboard::validate_mapping(map).empty(),"Apply returns valid mapping");
   if(test==1||test==4||test==9||test==10)check(map[4]==31,"O assignment persisted only on Apply");
   if(test==5)check(map[6]==56,"Shift alias is canonicalized on Apply");
  }
  std::cout<<"PASS "<<assertions<<" native keyboard presenter checks across twelve modal dialogs\n";
 }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}return 0;}
