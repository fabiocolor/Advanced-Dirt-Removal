// CPU backend: runs the single-source kernels on the host's cores, for machines
// without a usable GPU. Results are identical to the GPU backends; speed is
// much lower. Work-groups are spread over a thread pool (ADR_CPU_THREADS
// overrides the thread count). Kernels with barriers run each work item on a
// fiber: a small private stack switched in user mode (assembly on macOS and
// Linux, Windows fibers on Windows).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "gpu/cpu_kernels.h"
#include "gpu/device.h"

// ---------------------------------------------------------------------------
// User-mode context switch. adr_cpu_switch(save, load) stores the callee-saved
// registers and stack pointer of the running context in *save and resumes the
// context whose stack pointer is load. A new context starts in
// adr_cpu_trampoline, which calls adr_cpu_fiber_entry(fiber).
#if !defined(_WIN32)
extern "C" {
void adr_cpu_switch(void** save, void* load);
void adr_cpu_trampoline();
__attribute__((visibility("hidden"))) void adr_cpu_fiber_entry(void* fiber);
}
#if defined(__APPLE__)
#define ADR_SYM(x) "_" #x
#define ADR_HIDDEN(x) ".private_extern _" #x "\n"
#else
#define ADR_SYM(x) #x
#define ADR_HIDDEN(x) ".hidden " #x "\n"
#endif

#if defined(__aarch64__)
// AAPCS64: x19-x28, fp, lr and d8-d15 are callee-saved. Frame: 176 bytes.
asm(".text\n.p2align 2\n.globl " ADR_SYM(adr_cpu_switch) "\n" ADR_HIDDEN(adr_cpu_switch)
    ADR_SYM(adr_cpu_switch) ":\n"
    "  sub sp, sp, #176\n"
    "  stp x19, x20, [sp, #0]\n  stp x21, x22, [sp, #16]\n  stp x23, x24, [sp, #32]\n"
    "  stp x25, x26, [sp, #48]\n  stp x27, x28, [sp, #64]\n  stp x29, x30, [sp, #80]\n"
    "  stp d8, d9, [sp, #96]\n  stp d10, d11, [sp, #112]\n  stp d12, d13, [sp, #128]\n  stp d14, d15, [sp, #144]\n"
    "  mov x9, sp\n  str x9, [x0]\n  mov sp, x1\n"
    "  ldp x19, x20, [sp, #0]\n  ldp x21, x22, [sp, #16]\n  ldp x23, x24, [sp, #32]\n"
    "  ldp x25, x26, [sp, #48]\n  ldp x27, x28, [sp, #64]\n  ldp x29, x30, [sp, #80]\n"
    "  ldp d8, d9, [sp, #96]\n  ldp d10, d11, [sp, #112]\n  ldp d12, d13, [sp, #128]\n  ldp d14, d15, [sp, #144]\n"
    "  add sp, sp, #176\n  ret\n"
    ".p2align 2\n.globl " ADR_SYM(adr_cpu_trampoline) "\n" ADR_HIDDEN(adr_cpu_trampoline)
    ADR_SYM(adr_cpu_trampoline) ":\n"
    "  mov x0, x19\n  bl " ADR_SYM(adr_cpu_fiber_entry) "\n  brk #0\n");
#elif defined(__x86_64__)
// System V x86-64: rbx, rbp, r12-r15 are callee-saved, plus MXCSR and the x87
// control word. Frame: 8 (control words) + 48 (registers) + return address.
asm(".text\n.p2align 4\n.globl " ADR_SYM(adr_cpu_switch) "\n" ADR_HIDDEN(adr_cpu_switch)
    ADR_SYM(adr_cpu_switch) ":\n"
    "  pushq %rbp\n  pushq %rbx\n  pushq %r12\n  pushq %r13\n  pushq %r14\n  pushq %r15\n"
    "  subq $8, %rsp\n  stmxcsr (%rsp)\n  fnstcw 4(%rsp)\n"
    "  movq %rsp, (%rdi)\n  movq %rsi, %rsp\n"
    "  ldmxcsr (%rsp)\n  fldcw 4(%rsp)\n  addq $8, %rsp\n"
    "  popq %r15\n  popq %r14\n  popq %r13\n  popq %r12\n  popq %rbx\n  popq %rbp\n  ret\n"
    ".p2align 4\n.globl " ADR_SYM(adr_cpu_trampoline) "\n" ADR_HIDDEN(adr_cpu_trampoline)
    ADR_SYM(adr_cpu_trampoline) ":\n"
    "  movq %r12, %rdi\n  call " ADR_SYM(adr_cpu_fiber_entry) "\n  ud2\n");
#else
#error "CPU backend: no context switch for this architecture (build with -DADR_WITH_CPU=OFF)"
#endif
#endif  // !_WIN32

namespace adr {
namespace {

constexpr size_t kFiberStack = 64 * 1024;

struct Fiber {
  cpu::Item item;
  bool done = true;
#if defined(_WIN32)
  void* handle = nullptr;
#else
  void* sp = nullptr;
  std::unique_ptr<unsigned char[]> stack;
#endif
};

// Per-worker state for running barrier kernels.
struct Worker {
  std::vector<std::unique_ptr<Fiber>> fibers;
  const cpu::KernelEntry* kernel = nullptr;
  const cpu::LaunchView* view = nullptr;
  Fiber* current = nullptr;
#if !defined(_WIN32)
  void* scheduler_sp = nullptr;
#endif
};
thread_local Worker* tls_worker = nullptr;
#if defined(_WIN32)
// The scheduler fiber of this OS thread (the calling thread may be a host thread).
thread_local void* tls_scheduler = nullptr;
#endif
thread_local bool tls_barrier_misuse = false;

#if defined(_WIN32)
void CALLBACK fiber_proc(void* arg) {
  Fiber* f = static_cast<Fiber*>(arg);
  for (;;) {
    Worker* w = tls_worker;
    w->kernel->item(*w->view, f->item);
    f->done = true;
    SwitchToFiber(tls_scheduler);
  }
}
#endif

void resume(Worker& w, Fiber& f) {
  w.current = &f;
#if defined(_WIN32)
  SwitchToFiber(f.handle);
#else
  adr_cpu_switch(&w.scheduler_sp, f.sp);
#endif
  w.current = nullptr;
}

Fiber& fiber_at(Worker& w, size_t index) {
  while (w.fibers.size() <= index) {
    auto f = std::make_unique<Fiber>();
#if defined(_WIN32)
    f->handle = CreateFiberEx(kFiberStack, kFiberStack, 0, fiber_proc, f.get());
    if (!f->handle) throw Error("CPU backend: cannot create a fiber");
#else
    f->stack.reset(new unsigned char[kFiberStack]);
    uintptr_t top = (reinterpret_cast<uintptr_t>(f->stack.get()) + kFiberStack) & ~uintptr_t(15);
#if defined(__aarch64__)
    uintptr_t sp = top - 176;
    std::memset(reinterpret_cast<void*>(sp), 0, 176);
    reinterpret_cast<uintptr_t*>(sp)[0] = reinterpret_cast<uintptr_t>(f.get());                    // x19
    reinterpret_cast<uintptr_t*>(sp)[11] = reinterpret_cast<uintptr_t>(&adr_cpu_trampoline);     // x30
#else
    uintptr_t sp = top - 64 - 16;  // trampoline starts with a 16-byte aligned stack
    std::memset(reinterpret_cast<void*>(sp), 0, 80);
    uint32_t mxcsr = 0;
    uint16_t fpcw = 0;
    asm volatile("stmxcsr %0" : "=m"(mxcsr));
    asm volatile("fnstcw %0" : "=m"(fpcw));
    std::memcpy(reinterpret_cast<void*>(sp), &mxcsr, 4);
    std::memcpy(reinterpret_cast<void*>(sp + 4), &fpcw, 2);
    uintptr_t* regs = reinterpret_cast<uintptr_t*>(sp + 8);  // r15, r14, r13, r12, rbx, rbp, ret
    regs[3] = reinterpret_cast<uintptr_t>(f.get());
    regs[6] = reinterpret_cast<uintptr_t>(&adr_cpu_trampoline);
#endif
    f->sp = reinterpret_cast<void*>(sp);
#endif
    w.fibers.push_back(std::move(f));
  }
  return *w.fibers[index];
}

void run_barrier_group(Worker& w, const cpu::KernelEntry& kernel, const cpu::LaunchView& view, const int grp[3]) {
#if defined(_WIN32)
  if (!tls_scheduler) {
    tls_scheduler = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiberEx(nullptr, 0);
    if (!tls_scheduler) throw Error("CPU backend: cannot convert the worker thread to a fiber");
  }
#endif
  w.kernel = &kernel;
  w.view = &view;
  const int n = view.lsz[0] * view.lsz[1] * view.lsz[2];
  for (int i = 0; i < n; ++i) {
    Fiber& f = fiber_at(w, size_t(i));
    const int x = i % view.lsz[0], y = (i / view.lsz[0]) % view.lsz[1], z = i / (view.lsz[0] * view.lsz[1]);
    const int lid[3] = {x, y, z};
    for (int d = 0; d < 3; ++d) {
      f.item.lid[d] = lid[d];
      f.item.grp[d] = grp[d];
      f.item.lsz[d] = view.lsz[d];
      f.item.gid[d] = grp[d] * view.lsz[d] + lid[d];
    }
    f.done = false;
  }
  for (int remaining = n; remaining > 0;) {
    for (int i = 0; i < n; ++i) {
      Fiber& f = *w.fibers[size_t(i)];
      if (f.done) continue;
      resume(w, f);
      if (f.done) --remaining;
    }
  }
}

// Fixed pool of worker threads; the calling thread also works.
class Pool {
 public:
  explicit Pool(int threads) {
    for (int i = 1; i < threads; ++i) threads_.emplace_back([this, i] { loop(i); });
    workers_.resize(size_t(threads));
    for (auto& w : workers_) w = std::make_unique<Worker>();
  }
  ~Pool() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    wake_.notify_all();
    for (auto& t : threads_) t.join();
  }
  int size() const { return int(workers_.size()); }
  // Calls fn(index, worker) for index in [0, count) across all threads.
  void run(size_t count, const std::function<void(size_t, Worker&)>& fn) {
    if (count == 0) return;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      job_ = &fn;
      count_ = count;
      next_.store(0);
      active_ = int(threads_.size());
      error_.clear();
      ++generation_;
    }
    wake_.notify_all();
    work(*workers_[0]);
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [this] { return active_ == 0; });
    job_ = nullptr;
    if (!error_.empty()) throw Error(error_);
  }

 private:
  void work(Worker& w) {
    tls_worker = &w;
    for (size_t i; (i = next_.fetch_add(1)) < count_;) {
      try {
        (*job_)(i, w);
      } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        if (error_.empty()) error_ = e.what();
        next_.store(count_);
      }
    }
  }
  void loop(int index) {
    uint64_t seen = 0;
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
        if (stop_) return;
        seen = generation_;
      }
      work(*workers_[size_t(index)]);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (--active_ == 0) done_.notify_all();
      }
    }
  }
  std::vector<std::thread> threads_;
  std::vector<std::unique_ptr<Worker>> workers_;
  std::mutex mutex_, error_mutex_;
  std::condition_variable wake_, done_;
  const std::function<void(size_t, Worker&)>* job_ = nullptr;
  size_t count_ = 0;
  std::atomic<size_t> next_{0};
  int active_ = 0;
  uint64_t generation_ = 0;
  bool stop_ = false;
  std::string error_;
};

int thread_count() {
  if (const char* v = std::getenv("ADR_CPU_THREADS")) {
    const int n = std::atoi(v);
    if (n > 0) return std::min(n, 256);
  }
  return std::max(1, std::min(64, int(std::thread::hardware_concurrency())));
}

class CpuDevice final : public Device {
 public:
  CpuDevice() : pool_(thread_count()) {}
  ~CpuDevice() override {
    release_resources();
    if (!timings_.empty()) {  // ADR_CPU_PROFILE=1
      std::vector<std::pair<double, std::string>> rows;
      for (auto& t : timings_) rows.emplace_back(t.second, t.first);
      std::sort(rows.rbegin(), rows.rend());
      for (auto& r : rows) std::fprintf(stderr, "cpu kernel %-28s %9.1f ms\n", r.second.c_str(), r.first);
    }
  }

  std::string name() const override { return "CPU (" + std::to_string(pool_.size()) + " threads)"; }
  std::string backend() const override { return "cpu"; }

  void upload(const Buffer& dst, const void* src, size_t bytes, size_t offset) override {
    if (bytes) std::memcpy(static_cast<char*>(dst.native()) + offset, src, bytes);
  }
  void download(void* dst, const Buffer& src, size_t bytes, size_t offset) override {
    if (bytes) std::memcpy(dst, static_cast<const char*>(src.native()) + offset, bytes);
  }
  void fill(const Buffer& dst, uint8_t value, size_t bytes, size_t offset) override {
    if (bytes) std::memset(static_cast<char*>(dst.native()) + offset, value, bytes);
  }
  void copy(const Buffer& dst, const Buffer& src, size_t bytes, size_t dst_offset, size_t src_offset) override {
    if (bytes) std::memmove(static_cast<char*>(dst.native()) + dst_offset, static_cast<const char*>(src.native()) + src_offset, bytes);
  }

  void launch(KernelHandle* kernel, Dim3 threads, Dim3 group, std::initializer_list<const Buffer*> buffers,
              const void* params, size_t params_bytes) override {
    if (threads.x <= 0 || threads.y <= 0 || threads.z <= 0) return;
    std::lock_guard<std::mutex> lock(launch_mutex_);
    const auto* entry = static_cast<const cpu::KernelEntry*>(kernel->native);
    std::vector<void*> pointers;
    for (const Buffer* b : buffers) pointers.push_back(b && *b ? b->native() : nullptr);
    std::vector<std::max_align_t> storage((params_bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t) + 1);
    if (params_bytes) std::memcpy(storage.data(), params, params_bytes);
    cpu::LaunchView view{pointers.data(), storage.data(), {group.x, group.y, group.z}};
    const int gx = (threads.x + group.x - 1) / group.x, gy = (threads.y + group.y - 1) / group.y,
              gz = (threads.z + group.z - 1) / group.z;
    const size_t groups = size_t(gx) * gy * gz;
    const auto start = profile_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    struct Timer {
      CpuDevice* d; const char* name; std::chrono::steady_clock::time_point start;
      ~Timer() { if (d->profile_) d->timings_[name] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); }
    } timer{this, entry->name, start};
    pool_.run(groups, [&](size_t index, Worker& w) {
      const int grp[3] = {int(index % gx), int((index / gx) % gy), int(index / (size_t(gx) * gy))};
      if (entry->barrier) {
        run_barrier_group(w, *entry, view, grp);
      } else {
        tls_barrier_misuse = false;
        entry->group(view, grp);
        if (tls_barrier_misuse) throw Error(std::string("CPU backend: kernel ") + entry->name + " uses a barrier but was not marked");
      }
    });
  }
  void flush() override {}
  void synchronize() override {}

 protected:
  BufferHandle allocate_native(size_t bytes) override {
    BufferHandle handle;
    handle.native = ::operator new(bytes, std::align_val_t(64), std::nothrow);
    if (!handle.native) throw Error("CPU backend: out of memory");
    handle.bytes = bytes;
    return handle;
  }
  void release_native(BufferHandle handle) override { ::operator delete(handle.native, std::align_val_t(64)); }
  void* load_kernel(const std::string& name) override {
    return const_cast<cpu::KernelEntry*>(cpu::find_kernel(name));
  }

 private:
  bool profile_ = std::getenv("ADR_CPU_PROFILE") != nullptr;
  std::map<std::string, double> timings_;
  std::mutex launch_mutex_;
  Pool pool_;
};

}  // namespace

namespace cpu {
void barrier() {
  Worker* w = tls_worker;
  if (!w || !w->current) {
    tls_barrier_misuse = true;
    return;
  }
#if defined(_WIN32)
  SwitchToFiber(tls_scheduler);
#else
  adr_cpu_switch(&w->current->sp, w->scheduler_sp);
#endif
}
}  // namespace cpu

std::unique_ptr<Device> create_cpu_device(int) { return std::make_unique<CpuDevice>(); }

}  // namespace adr

#if !defined(_WIN32)
extern "C" void adr_cpu_fiber_entry(void* arg) {
  auto* f = static_cast<adr::Fiber*>(arg);
  for (;;) {
    adr::Worker* w = adr::tls_worker;
    w->kernel->item(*w->view, f->item);
    f->done = true;
    adr_cpu_switch(&f->sp, w->scheduler_sp);
  }
}
#endif
