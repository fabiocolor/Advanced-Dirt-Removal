#include "../src/paint.h"
#include "../src/paint_cache.h"
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <random>

void check(bool value, const char* name) {
    if (!value) throw std::runtime_error(name);
}
void rejects(const std::function<void()>& action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Invalid input must fail");
}
bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// Deliberately slow reference: evaluate every pixel against the whole path.
// This detects optimization regressions in segment bounds, overlaps and scaling.
std::vector<float> referenceMatte(const paint::Document& d, int frame, int w, int h) {
    std::vector<float> out(size_t(w)*h, 0.f);
    for (const auto& stroke : d.strokes) {
        if (!stroke.enabled || frame < stroke.first || frame > stroke.last) continue;
        for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
            float a=float(paint::coverage(stroke,(x+.5)*d.width/w,(y+.5)*d.height/h,d.width,d.height));
            auto& v=out[size_t(h-1-y)*w+x];
            v=stroke.erase?v*(1-a):v+(1-v)*a;
        }
    }
    return out;
}

// Independent slow reference: cover the segment with densely sampled circular dabs.
// This checks variable-pressure geometry without sharing the analytic solution.
double sampledSweep(const paint::Stroke& s, const paint::Point& a,
                    const paint::Point& b, double x, double y, int steps=10000) {
    double best=0;
    for(int j=0;j<=steps;++j){
        double t=double(j)/steps;
        double radius=s.diameter*.5*((1-t)*a.pressure+t*b.pressure);
        if(radius<=0)continue;
        double dx=x-100*((1-t)*a.x+t*b.x),dy=y-100*((1-t)*a.y+t*b.y);
        double distance=(s.shape==paint::Shape::Square?std::max(std::abs(dx),std::abs(dy)):std::hypot(dx,dy))/radius;
        if(distance>=1)continue;
        double coverage=1;
        if(distance>1-s.softness&&s.softness>0){double q=(1-distance)/s.softness;coverage=q*q*(3-2*q);}
        best=std::max(best,coverage*s.opacity);
    }
    return best;
}
// Historical constant-pressure projection, independent of segmentCoverage().
double constantSweep(const paint::Stroke& s, const paint::Point& a,
                     const paint::Point& b, double x, double y) {
    double ax=a.x*100,ay=a.y*100,dx=(b.x-a.x)*100,dy=(b.y-a.y)*100;
    double den=dx*dx+dy*dy,t=den>0?std::clamp(((x-ax)*dx+(y-ay)*dy)/den,0.,1.):0;
    double radius=.5*s.diameter*a.pressure;if(radius<=0)return 0;
    double r=std::hypot(x-(ax+t*dx),y-(ay+t*dy))/radius;if(r>=1)return 0;
    double inner=1-s.softness,v=1;
    if(r>inner&&s.softness>0){double q=(1-r)/s.softness;v=q*q*(3-2*q);}
    return v*s.opacity;
}

int main() {
 for(size_t bytes:{size_t(1),size_t(25)<<20,size_t(100)<<20,size_t(400)<<20,size_t(600)<<20,size_t(3)<<30}){auto n=paint::cacheFrames(bytes);check(n<=16&&n*bytes<=(size_t(1)<<30),"Additional paint history stays within 1 GiB at every scan size");}

    using namespace paint;
    Document d;
    check(decode(encode(d)).strokes.empty(), "Empty roundtrip");
    d.width = d.height = 256;
    Stroke s;
    s.id = d.nextId++;
    s.first = s.last = 12;
    s.diameter = 40;
    s.softness = .5;
    s.points = {{.25, .25, 1}, {.75, .25, 1}};
    d.strokes.push_back(s);
    check(encode(decode(encode(d))) == encode(d), "Lossless stroke serialization");
    auto m = matte(d, 12, 256, 256);
    auto at = [&](int x, int y) { return m[size_t(255-y)*256+x]; };
    check(at(128,64) == 1, "Continuous sweep has no holes between events");
    check(at(128,192) == 0, "Correct vertical orientation");
    check(at(128,80) > 0 && at(128,80) < 1, "Smooth feather");
    check(at(128,90) == 0, "Outside brush exactly untouched");
    auto preview = matte(d, 12, 128, 64, 256, 256);
    check(preview[size_t(47)*128+64] == 1 && preview[size_t(15)*128+64] == 0,
          "Anisotropic viewer preview uses full-image stroke coordinates");
    auto before = matte(d, 11, 256, 256), after = matte(d, 13, 256, 256);
    check(*std::max_element(before.begin(),before.end()) == 0 && before == after,
          "No leakage to adjacent frames");
    d.strokes[0].last = 14;
    check(matte(d,14,256,256) == m, "Explicit duration includes last frame");
    check(matte(d,15,256,256) == before, "Duration excludes following frame");
    d.strokes[0].enabled = false;
    check(matte(d,12,256,256) == before, "Disable stroke");
    d.strokes[0].enabled = true;
    Stroke er = s;
    er.id = d.nextId++;
    er.erase = true;
    er.diameter = 12;
    er.softness = 0;
    er.points = {{.5,.25,1}};
    d.strokes.push_back(er);
    auto erased = matte(d,12,256,256);
    check(erased[size_t(191)*256+128] == 0 && erased[size_t(191)*256+80] == 1,
          "Erase only its footprint");
    Stroke restore = er;
    restore.id = d.nextId++;
    restore.erase = false;
    restore.opacity = .5;
    d.strokes.push_back(restore);
    check(matte(d,12,256,256)[size_t(191)*256+128] == .5f,
          "Later restore can recover an erased area without changing earlier strokes");
    d.strokes.resize(1);
    d.strokes[0].opacity = .4;
    auto opacity = matte(d,12,256,256);
    check(std::abs(opacity[size_t(191)*256+128]-.4f) < 1e-7,
          "Opacity independent of path density");
    d.strokes[0].points.insert(d.strokes[0].points.begin()+1, {.5,.25,1});
    check(matte(d,12,256,256) == opacity, "Straight-stroke resampling preserves opacity");
    d.strokes[0].points = {{.5,.5,.5}};
    auto pressure = matte(d,12,256,256);
    check(pressure[size_t(127)*256+143] == 0, "Pressure scales brush radius");
    d.strokes[0].points[0].pressure = 0;
    check(matte(d,12,256,256) == before, "Zero pressure paints nothing");
    d.strokes[0].points = {{-1,-1,1}};
    check(matte(d,12,256,256) == before, "Off-canvas strokes cannot alter the image");
    d.strokes[0].points = {{0,0,1}};
    auto edge = matte(d,12,256,256);
    check(edge[size_t(255)*256] > 0 && edge[0] == 0 && edge.back() == 0,
          "Corner brush clips safely without wrapping to other edges");

    // Exact endpoints matter for untouched pixels, excursions and signed zero.
    for (float clean : {-0.f, 0.f, -.0235f, 1.12166f, std::numeric_limits<float>::denorm_min()}) {
        for (float source : {-0.f, 0.f, -.31f, 1.7f}) {
            check(sameBits(blend(clean,source,0), clean), "Unpainted pixel preserved bitwise");
            check(sameBits(blend(clean,source,1), source), "Fully recovered pixel equals source bitwise");
        }
    }
    check(std::abs(blend(-.0235f,1.12166f,.5f)-.54908f) < 1e-7, "Soft compositing without clipping");
    auto saved = encode(d);
    rejects([&] { matte(d,12,512,256); });
    rejects([&] { decode("R3 256 256 1 0"); });
    rejects([&] { decode(saved+"junk"); });
    rejects([&] { decode(saved.substr(0,saved.size()/2)); });
    rejects([&] { auto bad=d; bad.strokes[0].opacity=2; encode(bad); });
    rejects([&] { auto bad=d; bad.strokes[0].first=bad.strokes[0].last+1; encode(bad); });
    rejects([&] { auto bad=d; bad.strokes[0].points[0].x=std::numeric_limits<double>::infinity(); encode(bad); });
    rejects([&] { auto bad=d; bad.strokes.push_back(bad.strokes[0]); encode(bad); });
    rejects([&] { decode("R1 256 256 2 1 1 12 12 1 0 40 .5 1 9999999999"); });
    rejects([&] { decode("R1 256 256 2 1 1 12 12 2 0 40 .5 1 1 .5 .5 1"); });
    check(encode(d) == saved, "Failed operations preserve existing stroke document");
    Stroke growing;
    growing.diameter=100;growing.softness=0;growing.points={{.25,.5,0},{.75,.5,1}};
    check(coverage(growing,70,99,100,100)==1,"Growing brush includes its larger endpoint footprint");
    growing.points={{.5,.5,0},{.5,.5,1}};
    check(coverage(growing,50,99,100,100)==1,"Stationary pressure increase expands brush coverage");
    std::mt19937 sweepRng(20261023);
    std::uniform_real_distribution<double> randomUnit(0.,1.);
    for(int j=0;j<12;++j){
        Stroke pen;pen.diameter=20+80*randomUnit(sweepRng);pen.softness=.2+.8*randomUnit(sweepRng);pen.opacity=.1+.9*randomUnit(sweepRng);
        Point a{randomUnit(sweepRng),randomUnit(sweepRng),randomUnit(sweepRng)};
        Point b{randomUnit(sweepRng),randomUnit(sweepRng),randomUnit(sweepRng)};
        if(j==0)a.pressure=0;if(j==1)b.pressure=0;if(j==2){b.x=a.x;b.y=a.y;}
        for(int n=0;n<40;++n){
            double x=120*randomUnit(sweepRng)-10,y=120*randomUnit(sweepRng)-10;
            double actual=segmentCoverage(pen,a,b,x,y,100,100),reference=sampledSweep(pen,a,b,x,y);
            check(actual+1e-12>=reference&&actual-reference<1e-4,"Variable pressure agrees with dense circular-dab reference");
            Point fixed=b;fixed.pressure=a.pressure;
            check(segmentCoverage(pen,a,fixed,x,y,100,100)==constantSweep(pen,a,fixed,x,y),
                  "Constant-pressure geometry stays exactly equal to prior projection");
        }
        pen.shape=Shape::Square;
        for(int n=0;n<40;++n){double x=120*randomUnit(sweepRng)-10,y=120*randomUnit(sweepRng)-10;double actual=segmentCoverage(pen,a,b,x,y,100,100),reference=sampledSweep(pen,a,b,x,y);if(actual-reference>=1e-4)reference=sampledSweep(pen,a,b,x,y,1000000);if(!(actual+1e-12>=reference&&actual-reference<1e-4))std::cerr<<"square sweep actual="<<actual<<" reference="<<reference<<"\n";check(actual+1e-12>=reference&&actual-reference<1e-4,"Pressure square sweep agrees with dense independent square-dab reference");}
    }
    Document dense;
    dense.width=192;dense.height=128;
    std::mt19937 rng(20261022);
    std::uniform_real_distribution<double> unit(0.,1.);
    for (int j=0;j<16;++j) {
        Stroke stroke;stroke.id=dense.nextId++;stroke.first=-2;stroke.last=2;
        stroke.enabled=j%7!=0;stroke.erase=j%3==0;
        stroke.diameter=1+unit(rng)*72;stroke.softness=j%4?unit(rng):0.;stroke.opacity=unit(rng);
        for(int n=0;n<(j==1?1:31);++n)stroke.points.push_back({unit(rng)*1.4-.2,unit(rng)*1.4-.2,unit(rng)});
        // Repeated positions and zero-pressure endpoints are realistic tablet events.
        stroke.points.push_back(stroke.points.back());stroke.points.back().pressure=0;
        dense.strokes.push_back(stroke);
    }
    for (auto dimensions : {std::make_pair(192,128),std::make_pair(96,64),std::make_pair(96,32),std::make_pair(240,80)}) {
        auto actual=matte(dense,0,dimensions.first,dimensions.second,dense.width,dense.height);
        auto reference=referenceMatte(dense,0,dimensions.first,dimensions.second);
        check(actual==reference,"Dense soft/pressure/overlapping strokes equal full-path reference at native and anisotropic viewer scales");
    }
    // Fixed rectangle reference: 80x40 source pixels, uniform inward feather of
    // ten pixels, drawn in reverse order. Preview uses the same source grid.
    Document rect;rect.width=200;rect.height=100;Stroke region;region.id=rect.nextId++;region.first=region.last=7;region.shape=Shape::Rectangle;region.softness=.5;region.opacity=.7;region.points={{.7,.7,1},{.3,.3,1}};rect.strokes.push_back(region);
    auto rectangleSaved=encode(rect);check(rectangleSaved.rfind("R2 ",0)==0&&encode(decode(rectangleSaved))==rectangleSaved,"R2 mixed-shape state roundtrips exactly");
    for(auto dimensions:{std::make_pair(200,100),std::make_pair(100,25)}){int w=dimensions.first,h=dimensions.second;auto mask=matte(rect,7,w,h,200,100);
      for(int y=0;y<h;++y)for(int x=0;x<w;++x){double px=(x+.5)*200/w,py=(y+.5)*100/h;double edge=std::min({px-60,140-px,py-30,70-py});double t=std::clamp(edge/10.,0.,1.);float reference=float(t*t*(3-2*t)*.7);check(std::abs(mask[size_t(h-1-y)*w+x]-reference)<1e-6,"Rectangle equals independent feather reference at native/anisotropic scales");}
      auto inverted=mask;invertMatte(inverted);for(size_t j=0;j<mask.size();++j)check(inverted[j]==1-mask[j],"Spanish rectangle complements completed matte");
    }
    auto outside=matte(rect,6,200,100);check(*std::max_element(outside.begin(),outside.end())==0&&outside==matte(rect,8,200,100),"Rectangle never leaks across frame boundaries");
    rectangleSize(rect.strokes[0],200,100,40,20);check(std::abs(coverage(rect.strokes[0],100,50,200,100)-.7)<1e-9&&coverage(rect.strokes[0],125,50,200,100)==0,"Numeric rectangle resizing preserves centre");
    auto square=region;square.shape=Shape::Square;square.softness=0;square.diameter=20;square.points={{.5,.5,1}};check(coverage(square,109,59,200,100)==square.opacity,"Square includes corners outside a circular brush");square.shape=Shape::Round;check(coverage(square,109,59,200,100)==0,"Original round brush geometry remains round");
    auto originalRound=saved;check(encode(decode(originalRound))==originalRound,"Legacy R1 round strokes retain exact serialization");
    auto mixed=dense;for(size_t j=0;j<mixed.strokes.size();++j){if(j%3==1)mixed.strokes[j].shape=Shape::Square;else if(j%3==2){mixed.strokes[j].shape=Shape::Rectangle;mixed.strokes[j].points.resize(2);}}
    for(auto dimensions:{std::make_pair(192,128),std::make_pair(96,32)})check(matte(mixed,0,dimensions.first,dimensions.second,192,128)==referenceMatte(mixed,0,dimensions.first,dimensions.second),"Mixed restore/erase/disabled shapes match full-frame reference");
    rejects([&]{auto bad=rect;bad.strokes[0].points.resize(1);encode(bad);});
    rejects([&]{decode("R2 100 100 2 1 1 0 0 1 0 80 .5 1 3 1 .5 .5 1");});
    {
      LayerCache cache;auto first=cache.get(mixed,0,192,128);
      check(first->value==layers(mixed,0,192,128).value,"Cached mask matches uncached mixed paint exactly");
      check(cache.get(mixed,0,192,128)==first&&cache.hits==1,"Repeated frame reuses the same immutable mask");
      auto edited=mixed;Stroke other=edited.strokes[1];other.id=edited.nextId++;other.first=other.last=10;edited.strokes.push_back(other);
      check(cache.get(edited,0,192,128)==first,"Painting another frame preserves cached mask");
      edited.strokes[1].opacity=.123;
      auto changed=cache.get(edited,0,192,128);check(changed!=first&&changed->value==layers(edited,0,192,128).value,"Opacity edits invalidate exactly");
      check(cache.get(mixed,0,192,128)==first,"Undo reuses the previous mask");
      LayerCache tiny(1);auto a=tiny.get(mixed,0,192,128);check(tiny.get(mixed,0,192,128)!=a&&tiny.hits==0,"Oversized masks stay outside cache budget");
      cache.clear();check(cache.get(mixed,0,192,128)!=first,"Host purge releases masks without invalidating held readers");
    }
    std::cout << "Paint matte: bitwise preservation, serialization, frame boundaries, continuous soft sweep, ordered restore/erase, duration, opacity, pressure, preview geometry, edges and invalid-data checks passed\n";
}
