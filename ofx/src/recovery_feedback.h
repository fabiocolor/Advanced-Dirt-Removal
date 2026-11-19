#pragma once
#include <atomic>
#include <memory>
#include <string>
namespace recovery_feedback {
// Presentation reads only this owned snapshot: never an OFX handle, host
// parameter or render mutex. Cancel is observed at the next safe work boundary.
struct State {
 std::string title="Advanced Dirt Removal — Auto recovery";
 enum Phase {Queued,Running,Complete,Cancelled,Failed};
 std::atomic<int> phase{Queued},done{0},total{0};
 std::atomic<double> seconds{0};
 std::atomic<bool> cancel{false},presented{false};
};
#if defined(__APPLE__) || defined(_WIN32)
void show(const std::shared_ptr<State>&);
void shutdown();
#else
inline void show(const std::shared_ptr<State>&){}
inline void shutdown(){}
#endif
}
