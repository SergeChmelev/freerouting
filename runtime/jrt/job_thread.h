// jrt::JobThread: std::thread for threads that run port code (routing jobs, optimizer workers,
// server calls).
//
// The routing code recurses deeply (Java gives its threads 1-2 MB of stack). Linux threads get
// 8 MB (the stack ulimit) and std::thread is used as is there and on Windows; macOS gives
// secondary threads only 512 KB, so there a JobThread is a pthread with kStackSize bytes of stack.
#pragma once

#include <cstddef>
#include <exception>
#include <type_traits>
#include <utility>

#if defined(__APPLE__)
#include <pthread.h>
#else
#include <thread>
#endif

namespace jrt {

class JobThread {
 public:
  static constexpr std::size_t kStackSize = std::size_t{8} << 20;

  JobThread() noexcept = default;

  template <class F, class = std::enable_if_t<!std::is_same_v<std::decay_t<F>, JobThread>>>
  explicit JobThread(F&& f) {
#if defined(__APPLE__)
    start(new TaskOf<std::decay_t<F>>(std::forward<F>(f)));
#else
    thread_ = std::thread(std::forward<F>(f));
#endif
  }

  JobThread(JobThread&& other) noexcept { swap(other); }
  JobThread& operator=(JobThread&& other) noexcept {
    if (joinable()) std::terminate();
    swap(other);
    return *this;
  }
  JobThread(const JobThread&) = delete;
  JobThread& operator=(const JobThread&) = delete;
  ~JobThread() {
    if (joinable()) std::terminate();
  }

#if defined(__APPLE__)
  bool joinable() const noexcept { return started_; }
  void join();
#else
  bool joinable() const noexcept { return thread_.joinable(); }
  void join() { thread_.join(); }
#endif

 private:
#if defined(__APPLE__)
  struct Task {
    virtual ~Task() = default;
    virtual void run() = 0;
  };
  template <class Fn>
  struct TaskOf final : Task {
    template <class G>
    explicit TaskOf(G&& g) : fn(std::forward<G>(g)) {}
    void run() override { fn(); }
    Fn fn;
  };
  /** Starts the thread on `task` (owned; deleted when it has run or if the start fails). */
  void start(Task* task);
  static void* trampoline(void* task);

  pthread_t handle_{};
  bool started_ = false;
  void swap(JobThread& other) noexcept {
    std::swap(handle_, other.handle_);
    std::swap(started_, other.started_);
  }
#else
  std::thread thread_;
  void swap(JobThread& other) noexcept { thread_.swap(other.thread_); }
#endif
};

}  // namespace jrt
