// The few legacy OpenGL calls the paint overlay draws with. The host owns the
// GL context. On Linux the functions are looked up in the host process, so the
// plugin still loads on render nodes without a GL library.
#pragma once
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#include <dlfcn.h>
#endif
namespace paint_gl {
#if defined(__APPLE__) || defined(_WIN32)
inline bool ready(){return true;}
inline void pushAttributes(){glPushAttrib(GL_CURRENT_BIT|GL_LINE_BIT|GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT);}
inline void popAttributes(){glPopAttrib();}
inline void prepare(){glDisable(GL_TEXTURE_2D);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glLineWidth(1);}
inline void lineWidth(float w){glLineWidth(w);}
inline void color(float r,float g,float b,float a){glColor4f(r,g,b,a);}
inline void loop(){glBegin(GL_LINE_LOOP);}
inline void strip(){glBegin(GL_LINE_STRIP);}
inline void lines(){glBegin(GL_LINES);}
inline void end(){glEnd();}
inline void vertex(double x,double y){glVertex2d(x,y);}
#else
// GL enums used here (legacy GL 1.x values).
enum : unsigned {CurrentBit=0x1,LineBit=0x4,EnableBit=0x2000,ColorBufferBit=0x4000,Texture2D=0x0DE1,DepthTest=0x0B71,Blend=0x0BE2,SrcAlpha=0x0302,OneMinusSrcAlpha=0x0303,LineLoop=2,LineStrip=3,Lines=1};
struct Table {
 void(*pushAttrib)(unsigned)=nullptr;void(*popAttrib)()=nullptr;void(*disable)(unsigned)=nullptr;void(*enable)(unsigned)=nullptr;
 void(*blendFunc)(unsigned,unsigned)=nullptr;void(*lineWidth)(float)=nullptr;void(*color4f)(float,float,float,float)=nullptr;
 void(*begin)(unsigned)=nullptr;void(*end)()=nullptr;void(*vertex2d)(double,double)=nullptr;bool ok=false;
 Table(){
  void* lib=dlopen("libGL.so.1",RTLD_LAZY|RTLD_NOLOAD);
  auto get=[&](const char*n)->void*{void*p=dlsym(RTLD_DEFAULT,n);if(!p&&lib)p=dlsym(lib,n);return p;};
  pushAttrib=(void(*)(unsigned))get("glPushAttrib");popAttrib=(void(*)())get("glPopAttrib");disable=(void(*)(unsigned))get("glDisable");enable=(void(*)(unsigned))get("glEnable");
  blendFunc=(void(*)(unsigned,unsigned))get("glBlendFunc");lineWidth=(void(*)(float))get("glLineWidth");color4f=(void(*)(float,float,float,float))get("glColor4f");
  begin=(void(*)(unsigned))get("glBegin");end=(void(*)())get("glEnd");vertex2d=(void(*)(double,double))get("glVertex2d");
  ok=pushAttrib&&popAttrib&&disable&&enable&&blendFunc&&lineWidth&&color4f&&begin&&end&&vertex2d;
 }
};
inline Table& gl(){static Table t;return t;}
inline bool ready(){return gl().ok;}
inline void pushAttributes(){gl().pushAttrib(CurrentBit|LineBit|EnableBit|ColorBufferBit);}
inline void popAttributes(){gl().popAttrib();}
inline void prepare(){gl().disable(Texture2D);gl().disable(DepthTest);gl().enable(Blend);gl().blendFunc(SrcAlpha,OneMinusSrcAlpha);gl().lineWidth(1);}
inline void lineWidth(float w){gl().lineWidth(w);}
inline void color(float r,float g,float b,float a){gl().color4f(r,g,b,a);}
inline void loop(){gl().begin(LineLoop);}
inline void strip(){gl().begin(LineStrip);}
inline void lines(){gl().begin(Lines);}
inline void end(){gl().end();}
inline void vertex(double x,double y){gl().vertex2d(x,y);}
#endif
// Restores the host's GL state even if drawing throws.
struct Scope {Scope(){pushAttributes();prepare();}~Scope(){popAttributes();}Scope(const Scope&)=delete;Scope&operator=(const Scope&)=delete;};
}
