#include "paint_input.h"
#import <AppKit/AppKit.h>
namespace paint_input {
unsigned nativeModifiers(){
 // Event flags also cover tablet and remote input whose modifiers have not
 // changed the global HID state. Called only during host pointer callbacks.
 NSEvent* event=NSApp.currentEvent;
 auto flags=event?event.modifierFlags:[NSEvent modifierFlags];
 return ((flags&NSEventModifierFlagShift)?Shift:0u)|((flags&NSEventModifierFlagOption)?Alt:0u)|((flags&(NSEventModifierFlagCommand|NSEventModifierFlagControl))?HostCommand:0u);
}

}
