#include "../src/paint_source.h"
#include <functional>
#include <iostream>
#include <random>
#include <iterator>

void check(bool value, const char* name) {
    if (!value) throw std::runtime_error(name);
}
void rejects(const std::function<void()>& action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Expected cache rejection");
}
std::string readBytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    check(bool(in), "Read test evidence");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void writeBytes(const std::filesystem::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary);
    out.write(bytes.data(), std::streamsize(bytes.size()));
    check(bool(out), "Write test fixture");
}
int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    using namespace paint_source;
    check(argc == 2, "Usage: paint_source_test new-test-directory");
    fs::path dir = fs::u8path(argv[1]);
    check(!fs::exists(dir), "New evidence directory required");
    fs::create_directories(dir);
    auto sourceFolder=dir/"additional-source";
    writeSourceInfo(sourceFolder.string(),256,256,25);
    auto sourceMeta=sourceInfo(sourceFolder.string());
    check(sourceMeta.width==256&&sourceMeta.height==256&&sourceMeta.fps==25,"Prepared source metadata roundtrip");
    rejects([&]{requireSourceFrame(sourceFolder.string(),0);});
    writeBytes(sourceFolder/"source-ready.txt","ADRREADY1 5 3\n");
    requireSourceFrame(sourceFolder.string(),5);requireSourceFrame(sourceFolder.string(),7);
    rejects([&]{requireSourceFrame(sourceFolder.string(),4);});
    rejects([&]{requireSourceFrame(sourceFolder.string(),8);});
    auto sourceStamp=fs::last_write_time(sourceFolder/"source-info.txt");
    writeSourceInfo(sourceFolder.string(),256,256,25);
    check(fs::last_write_time(sourceFolder/"source-info.txt")==sourceStamp,"Prepared source metadata is immutable");
    rejects([&]{writeSourceInfo(sourceFolder.string(),256,256,24);});
    rejects([&]{writeSourceInfo(sourceFolder.string(),512,256,25);});
    rejects([&]{sourceInfo((dir/"missing-source").string());});
    std::mt19937 rng(20261019);
    std::vector<float> raw(10000), changed(raw.size());
    for (size_t j=0;j<raw.size();++j) {
        uint32_t v=rng();
        v=(v&0x807fffffu)|((v%254u)<<23);
        std::memcpy(&raw[j],&v,4);
        changed[j]=j%4?raw[j]:raw[j]*.5f;
    }
    raw[0]=-0.f; changed[0]=0.f;
    auto packed=packOriginal(raw,changed);
    check(hash(unpackOriginal(packed,changed))==hash(raw),
          "Exact source recovery across finite float bit patterns and signed zero");
    rejects([&] { unpackOriginal({0,0},changed); });
    rejects([&] { unpackOriginal({0,255,255,255,255,15},changed); });
    rejects([&] { unpackOriginal({},changed); });
    rejects([&] { auto extra=packed;extra.push_back(1);unpackOriginal(extra,changed); });
    rejects([&] { size_t pos=0;unvarint({255,255,255,255,255},pos); });
    rejects([&] { size_t pos=0;unvarint({128},pos); });

    std::vector<float> source(256*256*3,.35f), cleaned(source.size(),.36f);
    source[0]=-.0235f;cleaned[0]=1.12166f;
    source[1]=-0.f;cleaned[1]=0.f;
    check(write(dir.string(),12,256,256,source,cleaned), "First cache publication");
    auto file=path(dir.string(),12);
    auto stamp=fs::last_write_time(file);
    const auto pristine=readBytes(file);
    auto native=readPair(dir.string(),12,256,256,&source);
    check(hash(native.cleaned)==hash(cleaned)&&hash(native.original)==hash(source),
          "Both images roundtrip bitwise including excursions and signed zero");
    check(!write(dir.string(),12,256,256,source,cleaned)&&fs::last_write_time(file)==stamp,
          "Reusing bake never rewrites frame");
    FrameCache cache;
    auto first=cache.get(dir.string(),12,256,256,source);
    auto second=cache.get(dir.string(),12,256,256,source);
    check(first==second&&cache.reads==1&&cache.hits==1,
          "Brush rerender reuses validated pixels without reading disk");
    rejects([&] { auto wrong=source;wrong[0]=0;cache.get(dir.string(),12,256,256,wrong); });
    auto display=preview(cleaned,256,256,128,64);
    check(display.size()==128*64*3&&display.back()==cleaned.back(), "Preview dimensions and constant samples");
    FrameCache previewCache;
    auto pair=previewCache.getPreview(dir.string(),12,256,256);
    check(hash(*pair.cleaned)==hash(cleaned)&&hash(*pair.original)==hash(source),
          "Preview verifies both paired payloads");
    rejects([&] { auto wrong=source;wrong[0]=0;previewCache.get(dir.string(),12,256,256,wrong); });
    rejects([&] { auto wrong=source;wrong[0]=0;read(dir.string(),12,256,256,wrong); });
    rejects([&] { auto wrong=cleaned;wrong[0]=0;write(dir.string(),12,256,256,source,wrong); });
    check(readBytes(file)==pristine, "Rejected rebake preserves original file");
    rejects([&] { read(dir.string(),13,256,256,source); });
    rejects([&] { read(dir.string(),12,512,256,source); });
    rejects([&] { write(dir.string(),13,0,256,source,cleaned); });
    rejects([&] { auto shortSource=source;shortSource.pop_back();read(dir.string(),12,256,256,shortSource); });

    // Random-access requests and parallel frame rendering share immutable images.
    check(write(dir.string(),-3,256,256,source,source), "Negative local frame numbers are supported");
    check(hash(*cache.get(dir.string(),-3,256,256,source))==hash(source), "Seek to different bake frame");
    check(hash(*cache.get(dir.string(),12,256,256,source))==hash(cleaned), "Seek back restores correct image");
    std::atomic<bool> readersOK{true};
    std::vector<std::thread> readers;
    const auto expected=hash(cleaned);
    for(int j=0;j<4;++j)readers.emplace_back([&] {
        try { for(int n=0;n<4;++n)if(hash(*cache.get(dir.string(),12,256,256,source))!=expected)readersOK=false; }
        catch(...) { readersOK=false; }
    });
    for(auto& reader:readers)reader.join();
    check(readersOK, "Concurrent renders return intact shared images");
    check(readBytes(file)==pristine&&fs::last_write_time(file)==stamp,
          "Reads, seeks and concurrent brush renders leave bake bytes and timestamp untouched");

    size_t payload=0;
    for(int j=0;j<4;++j){payload=pristine.find('\n',payload);check(payload!=std::string::npos,"Find fixture header");++payload;}
    // Every malformed fixture lives separately; the successful reference is retained.
    auto corruptCase=[&](const char* name,std::string bytes) {
        auto folder=dir/name;fs::create_directory(folder);writeBytes(path(folder.string(),12),bytes);
        rejects([&] { readPair(folder.string(),12,256,256,&source); });
    };
    corruptCase("truncated-header", "ADRS2 256");
    corruptCase("truncated-clean", pristine.substr(0,payload+3));
    corruptCase("truncated-delta", pristine.substr(0,pristine.size()-1));
    corruptCase("trailing-data", pristine+"X");
    auto bad=pristine;bad[payload]^=1;corruptCase("corrupt-clean",bad);
    bad=pristine;bad.back()^=1;corruptCase("corrupt-delta",bad);
    bad=pristine;bad.replace(0,5,"ADRS9");corruptCase("unknown-version",bad);
    bad=pristine;bad.replace(bad.find(" 12\n"),4," 13\n");corruptCase("wrong-frame",bad);
    bad="ADRS2 256 256 12\n"+hash(source)+"\n"+hash(cleaned)+"\n18446744073709551615\n";
    corruptCase("oversized-delta",bad);

    auto external=dir/"external-edit";fs::create_directory(external);
    auto externalFile=path(external.string(),12);writeBytes(externalFile,pristine);
    FrameCache externalCache;externalCache.get(external.string(),12,256,256,source);
    auto externalStamp=fs::last_write_time(externalFile);
    bad=pristine;bad[payload]^=1;writeBytes(externalFile,bad);
    fs::last_write_time(externalFile,externalStamp+std::chrono::seconds(2));
    rejects([&] { externalCache.get(external.string(),12,256,256,source); });
    // Exercise the operating-system publication path, including a competing frame.
    auto publishing=dir/"publication";fs::create_directory(publishing);
    auto staged=publishing/"staged",destination=publishing/"published";
    writeBytes(staged,"new-frame");writeBytes(destination,"existing-frame");
    check(bool(publish(staged,destination)),"Atomic publication refuses an existing frame");
    check(readBytes(destination)=="existing-frame"&&readBytes(staged)=="new-frame",
          "Competing publication preserves existing destination and staged evidence");
    auto fresh=publishing/"new-destination";
    check(!publish(staged,fresh)&&readBytes(fresh)=="new-frame","Atomic publication succeeds at a new destination");

    // Older development bakes can render native pixels, but cannot supply an original preview.
    auto legacy=dir/"legacy";fs::create_directory(legacy);
    std::string legacyBytes="ADRS1 256 256 12\n"+hash(source)+"\n"+hash(cleaned)+"\n";
    legacyBytes.append(reinterpret_cast<const char*>(cleaned.data()),cleaned.size()*sizeof(float));
    writeBytes(path(legacy.string(),12),legacyBytes);
    check(hash(read(legacy.string(),12,256,256,source))==hash(cleaned), "Legacy native read remains exact");
    FrameCache legacyCache;
    rejects([&] { legacyCache.getPreview(legacy.string(),12,256,256); });
    std::cout << "Recovery bake: exact paired RGB32, immutable reuse, cache seeks/concurrency, external-edit revalidation, legacy handling and source/frame/geometry/header/payload corruption checks passed\n";
}
