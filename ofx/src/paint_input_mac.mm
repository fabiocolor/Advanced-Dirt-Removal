#include "paint_input.h"
#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#include <map>
#include <cstdio>
#include <cstdlib>
namespace paint_input {
unsigned nativeModifiers(){
 // Event flags also cover tablet and remote input whose modifiers have not
 // changed the global HID state. Called only during host pointer callbacks.
 NSEvent* event=NSApp.currentEvent;
 auto flags=event?event.modifierFlags:[NSEvent modifierFlags];
 return ((flags&NSEventModifierFlagShift)?Shift:0u)|((flags&NSEventModifierFlagOption)?Alt:0u)|((flags&(NSEventModifierFlagCommand|NSEventModifierFlagControl))?HostCommand:0u);
}

namespace {
void trace(const char* message){if(std::getenv("ADR_PAINT_INPUT_TRACE"))std::fprintf(stderr,"ADR paint input: %s\n",message);}
std::map<void*,KeyHandler> handlers;
id monitor=nil;
void* active=nullptr;
__weak NSWindow* window=nil;
__weak NSResponder* responder=nil;
}
void registerKeys(void* owner,KeyHandler handler){
 handlers[owner]=handler;if(monitor)return;
 monitor=[NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskKeyDown|NSEventMaskMouseMoved|NSEventMaskLeftMouseDown|NSEventMaskRightMouseDown|NSEventMaskOtherMouseDown|NSEventMaskLeftMouseDragged|NSEventMaskRightMouseDragged) handler:^NSEvent*(NSEvent* event){
  // Pointer events start unfocused; only an ensuing OFX viewer callback arms
  // the handler again. Clicking a control, typing in an Inspector field or
  // moving outside the viewer therefore cannot invoke a brush shortcut.
  if(event.type!=NSEventTypeKeyDown){active=nullptr;return event;}
  if(!active){trace("viewer not armed");return event;}
  if(NSApp.keyWindow!=window||window.firstResponder!=responder){trace("native focus changed");return event;}
  // Qt controls can share one native responder. A hovering pen must not take
  // letters or punctuation from an Inspector editor that still has focus.
  @try {
   id focused=NSApp.accessibilityFocusedUIElement;
   NSString* role=[focused respondsToSelector:@selector(accessibilityRole)]?[focused accessibilityRole]:nil;
   trace(role?role.UTF8String:"no focused accessibility role");
   if([role isEqualToString:NSAccessibilityTextFieldRole]||[role isEqualToString:NSAccessibilityTextAreaRole]||[role isEqualToString:NSAccessibilityComboBoxRole])return event;
  } @catch(NSException*) {trace("focus query rejected");active=nullptr;return event;}

  // Undo belongs to the paint editor only while it has a reversible edit.
  // Other commands and an empty paint history remain host-owned.
  if(event.keyCode==kVK_ANSI_Z&&(event.modifierFlags&(NSEventModifierFlagCommand|NSEventModifierFlagControl))&&!(event.modifierFlags&NSEventModifierFlagOption)){
   auto found=handlers.find(active);const int command=(event.modifierFlags&NSEventModifierFlagShift)?RedoKey:UndoKey;
   if(found!=handlers.end()&&found->second(active,command,event.isARepeat))return nil;
   active=nullptr;return event;
  }
  if(event.modifierFlags&(NSEventModifierFlagCommand|NSEventModifierFlagControl|NSEventModifierFlagOption)){active=nullptr;return event;}
  // Delete can have no printable character, including synthetic/remote pen setups.
  int key=0;
  if(event.keyCode==kVK_Delete)key=0xff08;
  else if(event.keyCode==kVK_ForwardDelete)key=0xffff;
  else {NSString* chars=event.charactersIgnoringModifiers;if(!chars.length)return event;key=[chars characterAtIndex:0];if(key==127||key==8)key=0xff08;}
  if((event.modifierFlags&NSEventModifierFlagShift)&&key!='+'&&key!='='){active=nullptr;return event;}
  auto found=handlers.find(active);
  if(found!=handlers.end()&&found->second(active,key,event.isARepeat)){trace("viewer handled event");return nil;}
  trace("event left with host");
  if(key==9||key==27)active=nullptr;
  return event;
 }];
}
void focusKeys(void* owner){if(handlers.count(owner)){active=owner;window=NSApp.keyWindow;responder=window.firstResponder;}}
void blurKeys(void* owner){if(active==owner){active=nullptr;window=nil;responder=nil;}}
void shutdownKeys(){
 active=nullptr;window=nil;responder=nil;handlers.clear();
 if(monitor){[NSEvent removeMonitor:monitor];monitor=nil;}
}
void unregisterKeys(void* owner){blurKeys(owner);handlers.erase(owner);if(handlers.empty()&&monitor){[NSEvent removeMonitor:monitor];monitor=nil;}}
}
