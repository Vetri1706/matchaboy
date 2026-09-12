#pragma once
#include "macos_keyboard.hpp"

#ifdef __OBJC__
@class NSWindow;
namespace matcha::keyboard {
// UI thread only. The caller handles game pause/input clearing. Cancel leaves
// the supplied mapping unchanged; Apply returns one fully validated mapping.
bool show_settings(NSWindow *parent,Mapping &draft_out);
}
#endif
