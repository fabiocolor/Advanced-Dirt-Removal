// Viewer painting: strokes and rectangles painted in the host viewer, turned into
// a deterministic matte. Independent of the cleaning engine.
#pragma once
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace paint {
// Extra frame history is optional: never exceed 512 MiB, even on large scans.
inline size_t cacheFrames(size_t rgbBytes){return rgbBytes?std::min<size_t>(8,(size_t(512)<<20)/rgbBytes):0;}

enum class Shape {Round=0,Square=1,Rectangle=2};
struct Point { double x=0,y=0,pressure=1; };
struct Stroke {
 int id=0,first=0,last=0; bool enabled=true,erase=false;
 double diameter=80,softness=.5,opacity=1;
 std::vector<Point> points;
 Shape shape=Shape::Round;
};
struct Document {
 int width=0,height=0,nextId=1;
 std::vector<Stroke> strokes;
};
inline void need(bool v){if(!v)throw std::runtime_error("Invalid paint data; the saved strokes are kept");}
inline bool unit(double v){return std::isfinite(v)&&v>=0&&v<=1;}
inline void validate(const Document& d){
 need(d.strokes.size()<=4096&&d.width>=0&&d.height>=0&&d.width<=16384&&d.height<=16384&&d.nextId>0);
 if(!d.strokes.empty())need(d.width>0&&d.height>0);
 size_t count=0;int previous=0;
 for(const auto&s:d.strokes){
  need(s.id>previous&&s.id<d.nextId&&s.first<=s.last&&std::abs(double(s.first))<=1e9&&std::abs(double(s.last))<=1e9);
  need(std::isfinite(s.diameter)&&s.diameter>=1&&s.diameter<=32768&&unit(s.softness)&&unit(s.opacity)&&!s.points.empty());
  need(int(s.shape)>=0&&int(s.shape)<=2&&(s.shape!=Shape::Rectangle||s.points.size()==2));
  previous=s.id;count+=s.points.size();need(count<=262144);
  for(auto&p:s.points)need(std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=16&&std::abs(p.y)<=16&&unit(p.pressure));
 }
}
inline std::string encode(const Document& d){
 validate(d);if(d.strokes.empty()&&d.width==0)return "";
 std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(17);
 bool shapes=std::any_of(d.strokes.begin(),d.strokes.end(),[](const Stroke&s){return s.shape!=Shape::Round;});
 o<<(shapes?"R2 ":"R1 ")<<d.width<<' '<<d.height<<' '<<d.nextId<<' '<<d.strokes.size()<<'\n';
 for(auto&s:d.strokes){o<<s.id<<' '<<s.first<<' '<<s.last<<' '<<s.enabled<<' '<<s.erase<<' '<<s.diameter<<' '<<s.softness<<' '<<s.opacity<<' ';if(shapes)o<<int(s.shape)<<' ';o<<s.points.size();for(auto&p:s.points)o<<' '<<p.x<<' '<<p.y<<' '<<p.pressure;o<<'\n';}
 auto text=o.str();need(text.size()<=16*1024*1024);return text;
}
inline Document decode(const std::string& text){
 Document d;if(text.empty())return d;need(text.size()<=16*1024*1024);
 std::istringstream in(text);in.imbue(std::locale::classic());std::string version;size_t n=0,total=0;
 need(bool(in>>version>>d.width>>d.height>>d.nextId>>n)&&(version=="R1"||version=="R2")&&n<=4096);
 for(size_t j=0;j<n;++j){Stroke s;size_t points=0;int enabled=0,erase=0;
  need(bool(in>>s.id>>s.first>>s.last>>enabled>>erase>>s.diameter>>s.softness>>s.opacity));
  if(version=="R2"){int shape;need(bool(in>>shape)&&shape>=0&&shape<=2);s.shape=Shape(shape);}need(bool(in>>points));
  need((enabled==0||enabled==1)&&(erase==0||erase==1)&&points>0&&points<=262144-total);total+=points;s.enabled=enabled;s.erase=erase;s.points.resize(points);
  for(auto&p:s.points)need(bool(in>>p.x>>p.y>>p.pressure));d.strokes.push_back(std::move(s));
 }
 in>>std::ws;need(in.eof());validate(d);return d;
}
// Distance to the continuous swept path, with pressure interpolated along it.
// A stroke applies opacity once, independent of event density/brush speed.
inline double dabCoverage(const Stroke&s,double x,double y,double px,double py,double pressure){
 double radius=.5*s.diameter*pressure;if(radius<=0)return 0;
 double r=(s.shape==Shape::Square?std::max(std::abs(x-px),std::abs(y-py)):std::hypot(x-px,y-py))/radius;if(r>=1)return 0;
 double inner=1-s.softness,v=1;
 if(r>inner&&s.softness>0){double t=(1-r)/s.softness;v=t*t*(3-2*t);}
 return v*s.opacity;
}
inline double segmentCoverage(const Stroke&s,const Point&a,const Point&b,double x,double y,int w,int h){
 double ax=a.x*w,ay=a.y*h,dx=(b.x-a.x)*w,dy=(b.y-a.y)*h;
 double den=dx*dx+dy*dy;
 if(s.shape==Shape::Square){
  // The L-infinity/pressure ratio is monotone between the residual zeros and
  // abs(x)==abs(y) breakpoints. Check these and both endpoints exactly.
  double qx=x-ax,qy=y-ay,best=0;
  auto at=[&](double t){if(t>=0&&t<=1)best=std::max(best,dabCoverage(s,x,y,ax+t*dx,ay+t*dy,a.pressure+t*(b.pressure-a.pressure)));};
  at(0);at(1);if(dx)at(qx/dx);if(dy)at(qy/dy);if(dx!=dy)at((qx-qy)/(dx-dy));if(dx!=-dy)at((qx+qy)/(dx+dy));return best;
 }
 if(a.pressure==b.pressure){
  // Retain the original constant-radius arithmetic exactly.
  double t=den>0?std::clamp(((x-ax)*dx+(y-ay)*dy)/den,0.,1.):0;
  return dabCoverage(s,x,y,ax+t*dx,ay+t*dy,a.pressure);
 }
 // A growing brush may cover a point with its larger endpoint even when the
 // nearest path position's smaller dab does not. Minimize squared distance /
 // radius squared along the segment, checking both endpoints and its sole
 // interior stationary point. Diameter cancels out of the stationary equation.
 double best=std::max(dabCoverage(s,x,y,ax,ay,a.pressure),dabCoverage(s,x,y,b.x*w,b.y*h,b.pressure));
 double qx=x-ax,qy=y-ay,dot=qx*dx+qy*dy,q2=qx*qx+qy*qy,dp=b.pressure-a.pressure;
 double divisor=den*a.pressure+dp*dot;
 if(divisor!=0){double t=(dot*a.pressure+dp*q2)/divisor;
  if(t>0&&t<1)best=std::max(best,dabCoverage(s,x,y,ax+t*dx,ay+t*dy,a.pressure+t*dp));
 }
 return best;
}
inline double coverage(const Stroke&s,double x,double y,int w,int h){
 if(s.shape==Shape::Rectangle){
  const auto&a=s.points[0];const auto&b=s.points[1];
  double l=std::min(a.x,b.x)*w,r=std::max(a.x,b.x)*w,lo=std::min(a.y,b.y)*h,hi=std::max(a.y,b.y)*h;
  double edge=std::min({x-l,r-x,y-lo,hi-y});if(edge<=0)return 0;
  double feather=s.softness*.5*std::min(r-l,hi-lo),v=1;
  if(feather>0&&edge<feather){double t=edge/feather;v=t*t*(3-2*t);}return v*s.opacity;
 }
 double best=0;
 const auto&ps=s.points;
 if(ps.size()==1)return dabCoverage(s,x,y,ps[0].x*w,ps[0].y*h,ps[0].pressure);
 for(size_t j=1;j<ps.size();++j)best=std::max(best,segmentCoverage(s,ps[j-1],ps[j],x,y,w,h));
 return best;
}
inline void rectangleSize(Stroke&s,int w,int h,double width,double height){
 need(s.shape==Shape::Rectangle&&s.points.size()==2&&w>0&&h>0&&std::isfinite(width)&&std::isfinite(height)&&width>=1&&height>=1);
 double x=(s.points[0].x+s.points[1].x)*.5,y=(s.points[0].y+s.points[1].y)*.5;
 s.points={{x-width/(2*w),y-height/(2*h),1},{x+width/(2*w),y+height/(2*h),1}};
}
inline void changeShape(Stroke&s,Shape shape,int w,int h){
 if(s.shape==shape)return;
 if(shape==Shape::Rectangle){
  double l=s.points[0].x,r=l,b=s.points[0].y,t=b;
  for(auto&p:s.points){l=std::min(l,p.x);r=std::max(r,p.x);b=std::min(b,p.y);t=std::max(t,p.y);}
  double rx=s.diameter/(2*w),ry=s.diameter/(2*h);s.points={{l-rx,b-ry,1},{r+rx,t+ry,1}};
 }else if(s.shape==Shape::Rectangle){
  auto a=s.points[0],b=s.points[1];s.diameter=std::clamp(std::max(std::abs(a.x-b.x)*w,std::abs(a.y-b.y)*h),1.,2048.);s.points={{(a.x+b.x)*.5,(a.y+b.y)*.5,1}};
 }
 s.shape=shape;
}
// Paint layers, top-down rows. value: how much is painted (paint raises it,
// erase lowers it), premultiplied by touched. touched: how much any stroke,
// paint or erase, covers the pixel. Auto recovery uses touched to let erase
// strokes override its matte; without auto recovery only value matters.
struct Layers {std::vector<float> value,touched;};
inline Layers layers(const Document&d,int frame,int w,int h,int fullWidth=0,int fullHeight=0){
 if(!fullWidth)fullWidth=w;if(!fullHeight)fullHeight=h;
 need(w>0&&h>0&&fullWidth>0&&fullHeight>0);
 need(d.strokes.empty()||(d.width==fullWidth&&d.height==fullHeight));
 const double sx=double(w)/fullWidth,sy=double(h)/fullHeight;
 Layers out;auto&m=out.value;m.assign(size_t(w)*h,0.f);out.touched.assign(size_t(w)*h,0.f);
 for(auto&s:d.strokes){if(!s.enabled||frame<s.first||frame>s.last||s.opacity==0)continue;
  double x0=w,y0=h,x1=0,y1=0,r=s.shape==Shape::Rectangle?0:.5*s.diameter;
  for(auto&p:s.points){x0=std::min(x0,p.x*w-r*sx);x1=std::max(x1,p.x*w+r*sx);y0=std::min(y0,p.y*h-r*sy);y1=std::max(y1,p.y*h+r*sy);}
  int l=int(std::clamp(std::floor(x0),0.,double(w))),rr=int(std::clamp(std::ceil(x1),0.,double(w)));
  int b=int(std::clamp(std::floor(y0),0.,double(h))),t=int(std::clamp(std::ceil(y1),0.,double(h)));
  if(l>=rr||b>=t)continue;
  // Rasterize each segment only over its own brush bounds. Evaluating every
  // segment at every pixel of a long stroke's overall rectangle made ordinary
  // tablet strokes cost seconds. Max coverage within this temporary matte
  // preserves the original single-opacity application, including overlaps.
  const size_t stride=size_t(rr-l);
  std::vector<float> stroke(stride*size_t(t-b),0.f);
  auto rasterize=[&](const Point&a,const Point&z,bool single){
   double radius=.5*s.diameter*std::max(a.pressure,z.pressure);
   if(radius<=0)return;
   int sl=int(std::clamp(std::floor(std::min(a.x,z.x)*w-radius*sx),double(l),double(rr)));
   int sr=int(std::clamp(std::ceil(std::max(a.x,z.x)*w+radius*sx),double(l),double(rr)));
   int sb=int(std::clamp(std::floor(std::min(a.y,z.y)*h-radius*sy),double(b),double(t)));
   int st=int(std::clamp(std::ceil(std::max(a.y,z.y)*h+radius*sy),double(b),double(t)));
   for(int y=sb;y<st;++y)for(int x=sl;x<sr;++x){
    double px=(x+.5)/sx,py=(y+.5)/sy;
    float value=float(single?dabCoverage(s,px,py,a.x*fullWidth,a.y*fullHeight,a.pressure):segmentCoverage(s,a,z,px,py,fullWidth,fullHeight));
    auto&v=stroke[size_t(y-b)*stride+size_t(x-l)];v=std::max(v,value);
   }
  };
  if(s.shape==Shape::Rectangle){for(int y=b;y<t;++y)for(int x=l;x<rr;++x)stroke[size_t(y-b)*stride+size_t(x-l)]=float(coverage(s,(x+.5)/sx,(y+.5)/sy,fullWidth,fullHeight));}
  else if(s.points.size()==1)rasterize(s.points.front(),s.points.front(),true);
  else for(size_t j=1;j<s.points.size();++j)rasterize(s.points[j-1],s.points[j],false);
  for(int y=b;y<t;++y)for(int x=l;x<rr;++x){float a=stroke[size_t(y-b)*stride+size_t(x-l)];const size_t k=size_t(h-1-y)*w+x;auto&v=m[k];v=s.erase?v*(1-a):v+(1-v)*a;auto&c=out.touched[k];c=c+(1-c)*a;}
 }
 return out;
}
inline std::vector<float> matte(const Document&d,int frame,int w,int h,int fullWidth=0,int fullHeight=0){return layers(d,frame,w,h,fullWidth,fullHeight).value;}
inline bool anyOn(const Document&d,int frame){for(auto&s:d.strokes)if(s.enabled&&s.opacity>0&&frame>=s.first&&frame<=s.last)return true;return false;}
inline void invertMatte(std::vector<float>&mask){for(auto&v:mask)v=1.f-v;}
// Exact endpoints avoid introducing rounding changes to unpainted/source pixels.
inline float blend(float cleaned,float source,float mask){return mask<=0?cleaned:mask>=1?source:cleaned+(source-cleaned)*mask;}
}
