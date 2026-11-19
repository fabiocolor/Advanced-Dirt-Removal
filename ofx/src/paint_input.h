#pragma once
// Sample modifiers during host-delivered pointer callbacks only.
// Keyboard commands remain with Resolve; no key hooks or event replacement.
#ifdef _WIN32
#include <windows.h>
#endif
namespace paint_input {
enum Modifier : unsigned {Shift=1,Alt=2,HostCommand=4};
#ifdef __APPLE__
unsigned nativeModifiers();
#elif defined(_WIN32)
inline unsigned nativeModifiers(){return (GetKeyState(VK_SHIFT)<0?Shift:0u)|(GetKeyState(VK_MENU)<0?Alt:0u)|((GetKeyState(VK_CONTROL)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0)?HostCommand:0u);}
#else
inline unsigned nativeModifiers(){return 0;}
#endif
}
