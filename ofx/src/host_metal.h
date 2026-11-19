#pragma once
#include <cstddef>
#include <memory>

namespace adr_host {
// Lossless byte transport only. The repair engine still receives the same CPU
// pixels, independently of whether the host supplies CPU or Metal images.
class MetalImage {
public:
 virtual ~MetalImage()=default;
 virtual char* data() const noexcept=0;
 virtual void uploadRows(size_t start,size_t stride,size_t rowBytes,size_t rows)=0;
};
std::unique_ptr<MetalImage> stageMetalImage(void* commandQueue,void* image,
                                         size_t requiredBytes,bool download);
}
