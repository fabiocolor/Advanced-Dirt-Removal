#include "recovery_feedback.h"
#import <AppKit/AppKit.h>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <dlfcn.h>

#ifndef ADR_RECOVERY_PROGRESS_CLASS
#define ADR_RECOVERY_PROGRESS_CLASS FBCADRRecoveryProgress
#endif
#define FBCADRRecoveryProgress ADR_RECOVERY_PROGRESS_CLASS
@interface FBCADRRecoveryProgress : NSObject <NSWindowDelegate> {
@public
 std::shared_ptr<recovery_feedback::State> snapshot;
 std::chrono::steady_clock::time_point opened;
 int lastLoggedPhase,lastLoggedBucket;
}
@property(strong) NSPanel *panel;
@property(strong) NSTextField *status;
@property(strong) NSTextField *detail;
@property(strong) NSProgressIndicator *bar;
@property(strong) NSButton *button;
@property(strong) NSTimer *timer;
- (void)tick;
- (void)cancelOrClose:(id)sender;
@end
static NSMutableSet<FBCADRRecoveryProgress*> *controllers;
static std::atomic<bool> presentationUsed{false};
@implementation FBCADRRecoveryProgress
- (void)tick {
 using S=recovery_feedback::State;
 const int phase=snapshot->phase.load(),done=snapshot->done.load(),total=snapshot->total.load();
 const bool busy=phase==S::Queued||phase==S::Running;
 const double elapsed=busy?std::chrono::duration<double>(std::chrono::steady_clock::now()-opened).count():snapshot->seconds.load();
 self.bar.indeterminate=phase==S::Queued||total<=0;
 if(self.bar.indeterminate)[self.bar startAnimation:nil];else{[self.bar stopAnimation:nil];self.bar.doubleValue=100.*done/std::max(1,total);}
 if(phase==S::Queued)self.status.stringValue=snapshot->cancel?@"Cancelling…":@"Waiting for Resolve to start calculation…";
 else if(phase==S::Running)self.status.stringValue=[NSString stringWithFormat:@"%@ %d / %d frames (%.0f%%)",snapshot->cancel?@"Cancelling":@"Calculating",done,total,100.*done/std::max(1,total)];
 else if(phase==S::Complete)self.status.stringValue=[NSString stringWithFormat:@"Complete — %d frames",done];
 else if(phase==S::Cancelled)self.status.stringValue=[NSString stringWithFormat:@"Cancelled — %d / %d frames saved",done,total];
 else self.status.stringValue=@"Calculation failed — see the plugin error";
 self.detail.stringValue=[NSString stringWithFormat:@"Elapsed: %.1f seconds%@",elapsed,busy&&snapshot->cancel?@" · stopping at a safe boundary":@""];
 self.button.title=busy?@"Cancel":@"Close";self.button.enabled=!busy||!snapshot->cancel;
 if(self.panel.visible&&(phase!=lastLoggedPhase||done/10!=lastLoggedBucket)){
  std::fprintf(stderr,"ADR visible auto-recovery panel: %s | %s | %.0f%%\n",self.status.stringValue.UTF8String,self.detail.stringValue.UTF8String,self.bar.doubleValue);
  lastLoggedPhase=phase;lastLoggedBucket=done/10;
 }
 if(!busy){[self.timer invalidate];self.timer=nil;}
}
- (void)cancelOrClose:(id)sender {
 const int phase=snapshot->phase.load();
 if(phase==recovery_feedback::State::Queued||phase==recovery_feedback::State::Running){snapshot->cancel=true;[self tick];}
 else [self.panel close];
}
- (BOOL)windowShouldClose:(NSWindow*)sender {
 const int phase=snapshot->phase.load();
 if(phase==recovery_feedback::State::Queued||phase==recovery_feedback::State::Running){snapshot->cancel=true;[self tick];return NO;}
 return YES;
}
- (void)windowWillClose:(NSNotification*)notification {
 [self.timer invalidate];self.timer=nil;self.panel.delegate=nil;[controllers removeObject:self];
}
@end
namespace recovery_feedback {
void show(const std::shared_ptr<State>&state){
 // This is called by the host's parameter/UI callback, not by a render. Do
 // not create an NSApplication or dispatch host work from a test/background thread.
 if(!NSApp||![NSThread isMainThread])return;
 // AppKit objects and queued cleanup can outlive an OFX Unload callback. Keep
 // only this module's code resident until host exit; engine/frame resources
 // are still released normally. No loader pin is taken during plugin scan.
 if(!presentationUsed.exchange(true)){Dl_info image{};if(dladdr(reinterpret_cast<void*>(&show),&image)&&image.dli_fname)dlopen(image.dli_fname,RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE);}

 if(!controllers)controllers=[NSMutableSet new];
 for(FBCADRRecoveryProgress *old in [controllers copy])if(old->snapshot->phase.load()>=State::Complete)[old.panel close];
 FBCADRRecoveryProgress *ui=[FBCADRRecoveryProgress new];ui->lastLoggedPhase=-1;ui->lastLoggedBucket=-1;ui->snapshot=state;ui->opened=std::chrono::steady_clock::now();
 ui.panel=[[NSPanel alloc] initWithContentRect:NSMakeRect(0,0,490,156) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
 ui.panel.title=[NSString stringWithUTF8String:state->title.c_str()];ui.panel.releasedWhenClosed=NO;ui.panel.delegate=ui;
 ui.panel.hidesOnDeactivate=YES;ui.panel.floatingPanel=YES;ui.panel.becomesKeyOnlyIfNeeded=YES;
 ui.panel.collectionBehavior=NSWindowCollectionBehaviorFullScreenAuxiliary|NSWindowCollectionBehaviorMoveToActiveSpace;
 ui.status=[NSTextField labelWithString:@""];ui.status.frame=NSMakeRect(20,112,450,24);ui.status.font=[NSFont systemFontOfSize:14 weight:NSFontWeightMedium];
 ui.bar=[[NSProgressIndicator alloc]initWithFrame:NSMakeRect(20,86,450,16)];ui.bar.minValue=0;ui.bar.maxValue=100;
 ui.detail=[NSTextField labelWithString:@""];ui.detail.frame=NSMakeRect(20,55,450,22);
 ui.button=[NSButton buttonWithTitle:@"Cancel" target:ui action:@selector(cancelOrClose:)];ui.button.frame=NSMakeRect(370,15,100,28);ui.button.bezelStyle=NSBezelStyleRounded;
 for(NSView *v in @[ui.status,ui.bar,ui.detail,ui.button])[ui.panel.contentView addSubview:v];
 [controllers addObject:ui];[ui tick];
 ui.timer=[NSTimer timerWithTimeInterval:.1 repeats:YES block:^(NSTimer*){[ui tick];}];
 [[NSRunLoop mainRunLoop]addTimer:ui.timer forMode:NSRunLoopCommonModes];
 [ui.panel center];[ui.panel orderFront:nil];state->presented=ui.panel.visible;[ui tick];
}
void shutdown(){
 if(!presentationUsed.load())return;
 auto close=^{for(FBCADRRecoveryProgress *ui in [controllers copy]){ui->snapshot->cancel=true;ui->snapshot->phase=State::Cancelled;[ui.panel close];}[controllers removeAllObjects];};
 if([NSThread isMainThread])close();else dispatch_async(dispatch_get_main_queue(),close);
}
}
