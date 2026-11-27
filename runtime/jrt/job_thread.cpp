// PORT NOTE: see job_thread.h.
#include "jrt/job_thread.h"

#if defined(__APPLE__)

#include <memory>
#include <system_error>

namespace jrt {

void* JobThread::trampoline(void* task) {
  std::unique_ptr<Task> owned(static_cast<Task*>(task));
  try {
    owned->run();
  } catch (...) {
    std::terminate();  // like std::thread: an exception may not leave the thread function
  }
  return nullptr;
}

void JobThread::start(Task* task) {
  pthread_attr_t attr;
  int rc = pthread_attr_init(&attr);
  if (rc == 0) {
    rc = pthread_attr_setstacksize(&attr, kStackSize);
    if (rc == 0) rc = pthread_create(&handle_, &attr, &JobThread::trampoline, task);
    pthread_attr_destroy(&attr);
  }
  if (rc != 0) {
    delete task;
    throw std::system_error(rc, std::generic_category(), "jrt::JobThread");
  }
  started_ = true;
}

void JobThread::join() {
  if (!started_) throw std::system_error(std::make_error_code(std::errc::invalid_argument), "jrt::JobThread::join");
  int rc = pthread_join(handle_, nullptr);
  if (rc != 0) throw std::system_error(rc, std::generic_category(), "jrt::JobThread::join");
  started_ = false;
}

}  // namespace jrt

#endif
