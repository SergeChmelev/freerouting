#include "core/facade/HeadlessStoppableThread.h"

#include "core/facade/CancellationToken.h"

namespace fr {

HeadlessStoppableThread::HeadlessStoppableThread(CancellationToken* cancellationToken)
    : HeadlessStoppableThread(cancellationToken, std::nullopt) {}

HeadlessStoppableThread::HeadlessStoppableThread(CancellationToken* cancellationToken,
                                                 std::optional<int64_t> deadlineMillis) {
  this->cancellationToken =
      cancellationToken != nullptr ? cancellationToken : CancellationToken::none();
  this->deadlineMillis = deadlineMillis;
}

bool HeadlessStoppableThread::isTimedOut() {
  checkTimeout();
  return this->timedOut;
}

bool HeadlessStoppableThread::checkTimeout() {
  if (this->deadlineMillis.has_value() &&
      jrt::System::currentTimeMillis() >= *this->deadlineMillis) {
    this->timedOut = true;
    return true;
  }
  return this->timedOut;
}

bool HeadlessStoppableThread::isStopRequested() {
  return StoppableThread::isStopRequested() || this->cancellationToken->isCancelled() ||
         checkTimeout();
}

bool HeadlessStoppableThread::isStopAutoRouterRequested() {
  return StoppableThread::isStopAutoRouterRequested() || this->cancellationToken->isCancelled() ||
         checkTimeout();
}

}  // namespace fr
