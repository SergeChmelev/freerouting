#include "io/BoardReadResult.h"

#include "board/facade/BasicBoard.h"
#include "io/BoardMetadata.h"

namespace fr::io {

namespace {
// Java record toString of a reference component: String.valueOf(component).
template <class T>
std::string componentStr(T* p) {
  return p ? static_cast<jrt::Object*>(p)->toString() : std::string("null");
}
template <class T>
int32_t componentHash(T* p) {
  return p ? static_cast<jrt::Object*>(p)->hashCode() : 0;
}
template <class T>
bool componentEquals(T* a, T* b) {
  if (a == b) return true;
  if (a == nullptr || b == nullptr) return false;
  return static_cast<jrt::Object*>(a)->equals(static_cast<jrt::Object*>(b));
}
}  // namespace

bool BoardReadResult_Success::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<BoardReadResult_Success>(o);
  if (other == nullptr) return false;
  return componentEquals(board_, other->board_) && componentEquals(metadata_, other->metadata_) &&
         componentEquals(warnings_, other->warnings_);
}

int32_t BoardReadResult_Success::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, componentHash(board_));
  h = jrt::hashCombine(h, componentHash(metadata_));
  h = jrt::hashCombine(h, componentHash(warnings_));
  return h;
}

std::string BoardReadResult_Success::toString() {
  return "Success[board=" + componentStr(board_) + ", metadata=" + componentStr(metadata_) +
         ", warnings=" + componentStr(warnings_) + "]";
}

bool BoardReadResult_OutlineMissing::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<BoardReadResult_OutlineMissing>(o);
  if (other == nullptr) return false;
  return componentEquals(board_, other->board_) && componentEquals(metadata_, other->metadata_) &&
         componentEquals(warnings_, other->warnings_);
}

int32_t BoardReadResult_OutlineMissing::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, componentHash(board_));
  h = jrt::hashCombine(h, componentHash(metadata_));
  h = jrt::hashCombine(h, componentHash(warnings_));
  return h;
}

std::string BoardReadResult_OutlineMissing::toString() {
  return "OutlineMissing[board=" + componentStr(board_) + ", metadata=" + componentStr(metadata_) +
         ", warnings=" + componentStr(warnings_) + "]";
}

bool BoardReadResult_ParseError::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<BoardReadResult_ParseError>(o);
  if (other == nullptr) return false;
  return location_ == other->location_ && detail_ == other->detail_;
}

int32_t BoardReadResult_ParseError::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::stringHashCode(location_));
  h = jrt::hashCombine(h, jrt::stringHashCode(detail_));
  return h;
}

std::string BoardReadResult_ParseError::toString() {
  return "ParseError[location=" + location_ + ", detail=" + detail_ + "]";
}

std::string BoardReadResult_IoError::toString() {
  // Java Throwable.toString(): class name, plus ": " + message when the message is non-null.
  std::string cause = "java.io.IOException";
  if (cause_.hasMessage()) cause += ": " + cause_.getMessage();
  return "IoError[cause=" + cause + "]";
}

}  // namespace fr::io
