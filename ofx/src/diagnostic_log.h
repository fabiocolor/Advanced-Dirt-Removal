// Diagnostic log for the Advanced Dirt Removal OFX plugin: on by default during the early releases, so a
// user's report can say what the host, the machine and the plugin did. One file per host
// process, in the user's log folder; no image data or file names are written.
//   ADR_LOG=<folder>  write there instead
//   ADR_LOG=off       disable (also 0, false, no)
// The newest 30 logs are kept. Past 32 MB a file keeps only warnings and errors.
// Logging never throws and never fails a render.
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#else
#include <dlfcn.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#elif defined(_MSC_VER)
#include <intrin.h>
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace adrlog {
namespace fs = std::filesystem;
constexpr size_t kMaxFiles = 30;
constexpr size_t kMaxBytes = size_t(32) << 20;

inline std::string env(const char* name) { const char* v = std::getenv(name); return v ? v : ""; }
inline int pid() {
#ifdef _WIN32
  return _getpid();
#else
  return int(getpid());
#endif
}
inline std::tm localTime(std::time_t t) {
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  return tm;
}
// Platform log folder; empty when logging is off or no home folder is known.
inline std::string defaultFolder() {
#ifdef _WIN32
  std::string base = env("LOCALAPPDATA");
  if (base.empty()) base = env("TEMP");
  return base.empty() ? "" : base + "\\FabioColor\\AdvancedDirtRemoval\\Logs";
#elif defined(__APPLE__)
  std::string home = env("HOME");
  return home.empty() ? "" : home + "/Library/Logs/FabioColor/AdvancedDirtRemoval";
#else
  std::string state = env("XDG_STATE_HOME"), home = env("HOME");
  if (!state.empty()) return state + "/fabiocolor/advanced-dirt-removal/logs";
  return home.empty() ? "" : home + "/.local/state/fabiocolor/advanced-dirt-removal/logs";
#endif
}
inline std::string configuredFolder() {
  std::string v = env("ADR_LOG");
  std::string lower = v;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (lower == "off" || lower == "0" || lower == "false" || lower == "no") return "";
  return v.empty() ? defaultFolder() : v;
}

struct State {
  std::mutex mutex;
  FILE* file = nullptr;
  std::string path;
  size_t bytes = 0;
  bool opened = false, capped = false;
};
inline State& state() { static State s; return s; }

// Called with the state lock held.
inline void open(State& s) {
  s.opened = true;
  try {
    std::string folder = configuredFolder();
    if (folder.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::u8path(folder), ec);
    // Keep the newest kMaxFiles - 1 logs, then add this one. Only adr-<date> files are touched.
    std::vector<fs::path> old;
    for (auto& entry : fs::directory_iterator(fs::u8path(folder), ec)) {
      std::string name = entry.path().filename().u8string();
      if (name.size() > 8 && name.compare(0, 4, "adr-") == 0 && std::isdigit((unsigned char)name[4]) && entry.path().extension() == ".log") old.push_back(entry.path());
    }
    std::sort(old.begin(), old.end());
    for (size_t k = 0; k + kMaxFiles - 1 < old.size(); ++k) fs::remove(old[k], ec);
    char stamp[32];
    std::tm tm = localTime(std::time(nullptr));
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    s.path = (fs::u8path(folder) / ("adr-" + std::string(stamp) + "-pid" + std::to_string(pid()) + ".log")).u8string();
    s.file = std::fopen(s.path.c_str(), "a");
    if (!s.file) s.path.clear();
  } catch (...) {
    s.file = nullptr;
    s.path.clear();
  }
}
inline void write(const char* level, const std::string& text) {
  State& s = state();
  std::lock_guard<std::mutex> guard(s.mutex);
  if (!s.opened) open(s);
  if (!s.file) return;
  const bool important = std::strcmp(level, "INFO") != 0;
  if (s.capped && !important) return;
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
  char stamp[40];
  std::tm tm = localTime(t);
  std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
  unsigned thread = unsigned(std::hash<std::thread::id>()(std::this_thread::get_id()) % 100000);
  char head[96];
  std::snprintf(head, sizeof head, "%s.%03d t%05u %-5s ", stamp, ms, thread, level);
  std::string line = head + text + "\n";
  std::fputs(line.c_str(), s.file);
  s.bytes += line.size();
  if (!s.capped && s.bytes > kMaxBytes) {
    s.capped = true;
    std::fputs("log size limit reached: only warnings and errors from here on\n", s.file);
  }
  std::fflush(s.file);
}
inline void info(const std::string& text) { try { write("INFO", text); } catch (...) {} }
inline void warn(const std::string& text) { try { write("WARN", text); } catch (...) {} }
inline void error(const std::string& text) { try { write("ERROR", text); } catch (...) {} }
// Current file, or empty when logging is off or failed. Opens the log if needed.
inline std::string path() {
  State& s = state();
  std::lock_guard<std::mutex> guard(s.mutex);
  if (!s.opened) open(s);
  return s.path;
}
// Closes the file; the next line (a later Load in the same process) opens a new one.
inline void close() {
  State& s = state();
  std::lock_guard<std::mutex> guard(s.mutex);
  if (s.file) std::fclose(s.file);
  s.file = nullptr;
  s.opened = s.capped = false;
  s.bytes = 0;
  s.path.clear();
}

#ifdef __APPLE__
inline std::string sysctlString(const char* name) {
  char buffer[256] = {};
  size_t size = sizeof buffer - 1;
  return sysctlbyname(name, buffer, &size, nullptr, 0) == 0 ? std::string(buffer) : std::string();
}
#endif
inline std::string osName() {
#ifdef _WIN32
  using Fn = LONG(WINAPI*)(OSVERSIONINFOW*);
  OSVERSIONINFOW v{};
  v.dwOSVersionInfoSize = sizeof v;
  if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
    if (auto get = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion"))); get && get(&v) == 0)
      // Windows 11 still reports 10.0; its builds start at 22000.
      return std::string(v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000 ? "Windows 11" : "Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion)) + " build " + std::to_string(v.dwBuildNumber);
  return "Windows";
#elif defined(__APPLE__)
  return "macOS " + sysctlString("kern.osproductversion") + " (" + sysctlString("kern.osversion") + ")";
#else
  std::string pretty;
  std::ifstream release("/etc/os-release");
  for (std::string line; std::getline(release, line);)
    if (line.rfind("PRETTY_NAME=", 0) == 0) { pretty = line.substr(12); pretty.erase(std::remove(pretty.begin(), pretty.end(), '"'), pretty.end()); }
  utsname u{};
  std::string kernel = uname(&u) == 0 ? std::string(u.sysname) + " " + u.release : "Linux";
  return pretty.empty() ? kernel : pretty + ", " + kernel;
#endif
}
inline std::string cpuName() {
#ifdef __APPLE__
  std::string name = sysctlString("machdep.cpu.brand_string");
  int translated = 0;
  size_t size = sizeof translated;
  if (sysctlbyname("sysctl.proc_translated", &translated, &size, nullptr, 0) == 0 && translated) name += " (x86-64 under Rosetta)";
  return name;
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
  char brand[49] = {};
  for (unsigned k = 0; k < 3; ++k) {
    unsigned r[4] = {};
#ifdef _MSC_VER
    __cpuid(reinterpret_cast<int*>(r), int(0x80000002u + k));
#else
    __get_cpuid(0x80000002u + k, &r[0], &r[1], &r[2], &r[3]);
#endif
    std::memcpy(brand + 16 * k, r, 16);
  }
  std::string name(brand);
  name.erase(0, name.find_first_not_of(' '));
  name.erase(name.find_last_not_of(' ') + 1);
  return name.empty() ? "unknown x86 CPU" : name;
#else
  return "unknown CPU";
#endif
}
inline double memoryGiB() {
#ifdef _WIN32
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof m;
  return GlobalMemoryStatusEx(&m) ? double(m.ullTotalPhys) / (1ull << 30) : 0;
#elif defined(__APPLE__)
  uint64_t bytes = 0;
  size_t size = sizeof bytes;
  return sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 ? double(bytes) / (1ull << 30) : 0;
#else
  long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
  return pages > 0 && page > 0 ? double(pages) * double(page) / (1ull << 30) : 0;
#endif
}
inline std::string executablePath() {
#ifdef _WIN32
  char buffer[MAX_PATH * 2] = {};
  return GetModuleFileNameA(nullptr, buffer, DWORD(sizeof buffer)) ? buffer : "";
#elif defined(__APPLE__)
  char buffer[4096] = {};
  uint32_t size = sizeof buffer;
  return _NSGetExecutablePath(buffer, &size) == 0 ? buffer : "";
#else
  std::error_code ec;
  return fs::read_symlink("/proc/self/exe", ec).string();
#endif
}
// The loaded plugin binary: shows which installed copy the host picked up.
inline std::string pluginPath() {
#ifdef _WIN32
  HMODULE module = nullptr;
  char buffer[MAX_PATH * 2] = {};
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&pluginPath), &module) &&
      GetModuleFileNameA(module, buffer, DWORD(sizeof buffer)))
    return buffer;
  return "";
#else
  Dl_info info{};
  return dladdr(reinterpret_cast<void*>(&pluginPath), &info) && info.dli_fname ? info.dli_fname : "";
#endif
}
// Environment variables that change what the plugin does.
inline std::string overrides() {
  std::string out;
  for (const char* name : {"ADR_BACKEND", "ADR_RECIPROCAL_TABLE", "ADR_HASH_LOG", "ADR_DUMP_DIR",
                           "ADR_TRACE", "ADR_LOG", "ADR_DEBUG", "ADR_PROFILE", "OFX_PLUGIN_PATH"})
    if (const char* v = std::getenv(name); v && *v) out += std::string(out.empty() ? "" : ", ") + name + "=" + v;
  return out.empty() ? "none" : out;
}
}  // namespace adrlog
