#pragma once
// Host-delivered pointer modifiers and viewer-scoped keyboard input.
// The Mac adapter observes local app events; it never injects input.
#ifdef _WIN32
#include <windows.h>
#endif
namespace paint_input {
enum Modifier : unsigned {Shift=1,Alt=2,HostCommand=4};
#ifdef __APPLE__
unsigned nativeModifiers();
using KeyHandler=bool(*)(void*,int,bool);
void registerKeys(void* owner,KeyHandler handler);
void focusKeys(void* owner);
void blurKeys(void* owner);
void unregisterKeys(void* owner);
void shutdownKeys();
#elif defined(_WIN32)
inline unsigned nativeModifiers(){return (GetKeyState(VK_SHIFT)<0?Shift:0u)|(GetKeyState(VK_MENU)<0?Alt:0u)|((GetKeyState(VK_CONTROL)<0||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0)?HostCommand:0u);}
#else
inline unsigned nativeModifiers(){return 0;}
#endif
#ifndef __APPLE__
using KeyHandler=bool(*)(void*,int,bool);
inline void registerKeys(void*,KeyHandler){}
inline void focusKeys(void*){}
inline void blurKeys(void*){}
inline void unregisterKeys(void*){}
inline void shutdownKeys(){}
#endif
}
