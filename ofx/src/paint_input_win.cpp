#include "paint_input.h"
#include <UIAutomation.h>
#include <map>

// Resolve handles several letter shortcuts before dispatching OFX key events.
// Observe only the viewer's UI thread, before the host translates its messages.
// A pointer event disarms shortcuts until an OFX pointer callback re-arms them.
namespace paint_input {
namespace {
std::map<void*,KeyHandler> handlers;
HHOOK hook=nullptr;
void* active=nullptr;
HWND window=nullptr,focus=nullptr;
POINT pointer{};
bool releasedKeys[256]{};
IUIAutomation* automation=nullptr;
bool inspecting=false;

bool textEditorFocused(){
 if(!automation)return true; // Never take keys when native focus is unknown.
 IUIAutomationElement* element=nullptr;
 if(FAILED(automation->GetFocusedElement(&element))||!element)return true;
 CONTROLTYPEID type=0;const HRESULT result=element->get_CurrentControlType(&type);
 element->Release();
 return FAILED(result)||type==UIA_EditControlTypeId||type==UIA_ComboBoxControlTypeId||type==UIA_DocumentControlTypeId;
}
LRESULT CALLBACK messages(int code,WPARAM removed,LPARAM data){
 if(code<0||removed!=PM_REMOVE||inspecting)return CallNextHookEx(hook,code,removed,data);
 auto&msg=*reinterpret_cast<MSG*>(data);
 if(msg.message==WM_KEYUP&&msg.wParam<256&&releasedKeys[msg.wParam]){releasedKeys[msg.wParam]=false;msg.wParam=VK_F24;msg.lParam&=~(0xffLL<<16);return CallNextHookEx(hook,code,removed,data);}
 const bool motion=msg.message==WM_MOUSEMOVE||msg.message==WM_NCMOUSEMOVE||msg.message==WM_POINTERUPDATE;
 POINT current{};GetCursorPos(&current);
 if((!motion||current.x!=pointer.x||current.y!=pointer.y)&&((msg.message>=WM_MOUSEFIRST&&msg.message<=WM_MOUSELAST)||
    (msg.message>=WM_NCMOUSEMOVE&&msg.message<=WM_NCXBUTTONDBLCLK)||
    (msg.message>=WM_POINTERUPDATE&&msg.message<=WM_POINTERLEAVE)||
    msg.message==WM_KILLFOCUS||(msg.message==WM_ACTIVATEAPP&&!msg.wParam)))active=nullptr;
 if(!active||msg.message!=WM_KEYDOWN)return CallNextHookEx(hook,code,removed,data);
 if(msg.wParam==VK_SHIFT||msg.wParam==VK_CONTROL||msg.wParam==VK_MENU||msg.wParam==VK_LWIN||msg.wParam==VK_RWIN)return CallNextHookEx(hook,code,removed,data);
 if(GetActiveWindow()!=window||GetFocus()!=focus){active=nullptr;return CallNextHookEx(hook,code,removed,data);}
 inspecting=true;const bool editing=textEditorFocused();inspecting=false;
 if(editing)return CallNextHookEx(hook,code,removed,data);
 const bool shift=GetKeyState(VK_SHIFT)<0,ctrl=GetKeyState(VK_CONTROL)<0,alt=GetKeyState(VK_MENU)<0;
 int key=0;
 if(msg.wParam=='Z'&&ctrl&&!alt)key=shift?RedoKey:UndoKey;
 else if(ctrl||alt||GetKeyState(VK_LWIN)<0||GetKeyState(VK_RWIN)<0){active=nullptr;return CallNextHookEx(hook,code,removed,data);}
 else if(msg.wParam==VK_DELETE)key=0xffff;
 else if(msg.wParam==VK_BACK)key=0xff08;
 else{
  BYTE state[256]{};WCHAR chars[8]{};
  if(GetKeyboardState(state)&&ToUnicodeEx(static_cast<UINT>(msg.wParam),static_cast<UINT>((msg.lParam>>16)&255),state,chars,8,4,GetKeyboardLayout(0))==1)key=chars[0];
  if(shift&&key!='+'&&key!='='){active=nullptr;return CallNextHookEx(hook,code,removed,data);}
 }
 auto found=handlers.find(active);
 const bool handled=found!=handlers.end()&&found->second(active,key,(msg.lParam&(1LL<<30))!=0);
 if(handled){
  // Keep a neutral native key dispatch so Resolve enters its OFX interact
  // callback and flushes the queued command. The original key cannot become
  // a host shortcut. No pointer or button event is manufactured.
  if(msg.wParam<256)releasedKeys[msg.wParam]=true;
  msg.wParam=VK_F24;msg.lParam&=~(0xffLL<<16);
  return 0;
 }
 if(msg.wParam==VK_TAB||msg.wParam==VK_ESCAPE)active=nullptr;
 return CallNextHookEx(hook,code,removed,data);
}
}
void registerKeys(void*owner,KeyHandler handler){
 handlers[owner]=handler;
}
void focusKeys(void*owner){if(handlers.count(owner)){
 // Resolve may create the overlay on another thread. Install only when a
 // native pointer callback identifies the actual viewer message thread.
 if(!hook){
  if(!automation)CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation));
  hook=SetWindowsHookExW(WH_GETMESSAGE,messages,nullptr,GetCurrentThreadId());
 }
 active=owner;window=GetActiveWindow();focus=GetFocus();GetCursorPos(&pointer);
}}
void blurKeys(void*owner){if(active==owner){active=nullptr;window=focus=nullptr;}}
void shutdownKeys(){
 active=nullptr;window=focus=nullptr;handlers.clear();for(auto&key:releasedKeys)key=false;
 if(hook){UnhookWindowsHookEx(hook);hook=nullptr;}
 if(automation){automation->Release();automation=nullptr;}
}
void unregisterKeys(void*owner){blurKeys(owner);handlers.erase(owner);if(handlers.empty())shutdownKeys();}
}
