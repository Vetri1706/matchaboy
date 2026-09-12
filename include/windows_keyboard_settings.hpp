#pragma once
#include <windows.h>
#include "keyboard_mapping.hpp"

namespace matcha::keyboard {

// A native, window-scoped keyboard editor. It services all thread messages so
// the player's network timer continues while the owner window is disabled.
// Apply updates draft_out; Cancel and window close leave it unchanged.
bool show_windows_settings(HWND parent, Mapping &draft_out);

// Preferences belong only to this application and the current Windows user.
// Malformed or missing saved data selects the Balanced preset.
Mapping load_windows_mapping();
void save_windows_mapping(const Mapping &mapping);

} // namespace matcha::keyboard
