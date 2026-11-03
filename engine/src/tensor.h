// Device tensors and the development stage registry.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "gpu/device.h"

namespace adr {

enum class DType : int { u8 = 0, i8 = 1, i16 = 2, u16 = 3, i32 = 4, f32 = 5 };

inline size_t dtype_size(DType t) {
  switch (t) {
    case DType::u8:
    case DType::i8:
      return 1;
    case DType::i16:
    case DType::u16:
      return 2;
    default:
      return 4;
  }
}

inline void require(bool condition, const char* message) {
  if (!condition) throw Error(message);
}

struct Tensor {
  Buffer buf;
  DType dtype = DType::u8;
  std::vector<int64_t> shape;

  size_t numel() const {
    size_t n = 1;
    for (auto s : shape) n *= size_t(s);
    return n;
  }
  size_t bytes() const { return numel() * dtype_size(dtype); }
  int dim(int i) const { return int(shape[i < 0 ? shape.size() + i : i]); }
  const Buffer* b() const { return &buf; }  // for launch() buffer lists
};

inline Tensor empty(Device& device, DType dtype, std::vector<int64_t> shape) {
  Tensor t;
  t.dtype = dtype;
  t.shape = std::move(shape);
  t.buf = device.alloc(t.bytes());
  return t;
}

inline Tensor zeros(Device& device, DType dtype, std::vector<int64_t> shape) {
  Tensor t = empty(device, dtype, std::move(shape));
  device.fill(t.buf, 0, t.bytes());
  return t;
}

inline Tensor upload(Device& device, DType dtype, std::vector<int64_t> shape, const void* data) {
  Tensor t = empty(device, dtype, std::move(shape));
  device.upload(t.buf, data, t.bytes());
  return t;
}

// Copy of sub-tensor t[index] along the first dimension (buffers carry no offsets).
inline Tensor select0(Device& device, const Tensor& t, int index) {
  std::vector<int64_t> shape(t.shape.begin() + 1, t.shape.end());
  Tensor out = empty(device, t.dtype, shape);
  device.copy(out.buf, t.buf, out.bytes(), 0, out.bytes() * size_t(index));
  return out;
}

// Stack equally shaped tensors along a new first dimension.
inline Tensor stack0(Device& device, const std::vector<Tensor>& parts) {
  require(!parts.empty(), "stack0: no tensors");
  std::vector<int64_t> shape{int64_t(parts.size())};
  shape.insert(shape.end(), parts[0].shape.begin(), parts[0].shape.end());
  Tensor out = empty(device, parts[0].dtype, shape);
  for (size_t i = 0; i < parts.size(); ++i) device.copy(out.buf, parts[i].buf, parts[i].bytes(), parts[i].bytes() * i, 0);
  return out;
}

template <class T>
std::vector<T> download(Device& device, const Tensor& t) {
  std::vector<T> host(t.bytes() / sizeof(T));
  device.download(host.data(), t.buf, t.bytes());
  return host;
}

inline Dim3 grid1(size_t n) { return Dim3{int(n), 1, 1}; }
inline Dim3 grid2(int x, int y) { return Dim3{x, y, 1}; }
constexpr Dim3 kGroup1{256, 1, 1};
constexpr Dim3 kGroup2{16, 16, 1};

// Development stage registry: each stage can be executed in isolation on host
// arrays (unit tests and debugging).
using StageFn = std::function<std::vector<Tensor>(Device&, const std::vector<Tensor>&, const std::vector<double>&)>;
void register_stage(const std::string& name, StageFn fn);
const StageFn* find_stage(const std::string& name);
std::vector<std::string> stage_names();

struct StageRegistrar {
  StageRegistrar(const char* name, StageFn fn) { register_stage(name, std::move(fn)); }
};

#define ADR_CONCAT_INNER(a, b) a##b
#define ADR_CONCAT(a, b) ADR_CONCAT_INNER(a, b)
#define ADR_STAGE(name, fn) static ::adr::StageRegistrar ADR_CONCAT(adr_stage_registrar_, __LINE__)(name, fn)

}  // namespace adr
