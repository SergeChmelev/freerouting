// Port of app.freerouting.datastructures.ArrayStack
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"
#include "logger/FRLogger.h"

namespace fr {

/**
 * Implementation of a stack as an array. Header-only template: T is the full C++ element type
 * (usually a pointer, e.g. ArrayStack<ShapeTree_TreeNode*>). pop() returns T{} (nullptr) when the
 * stack is exhausted, like Java's null.
 */
template <class T>
class ArrayStack : public virtual jrt::Object {
 public:
  /** Creates a new ArrayStack with an initial capacity of maxStackDepth elements. */
  explicit ArrayStack(int32_t maxStackDepth) : nodeArr(new jrt::Array<T>(maxStackDepth)) {}

  /** Sets the stack to empty. */
  void reset() {
    for (int32_t i = 0; i <= level; i++) {
      (*nodeArr)[i] = T{};
    }
    level = -1;
  }

  /** Pushes element onto the stack. */
  void push(T element) {
    ++level;
    if (level >= nodeArr->length) {
      reallocate();
    }
    (*nodeArr)[level] = element;
  }

  /** Pops the next element from the top of the stack. Returns T{} (null), if exhausted. */
  T pop() {
    if (level < 0) {
      return T{};
    }
    T result = (*nodeArr)[level];
    (*nodeArr)[level] = T{};
    --level;
    return result;
  }

 private:
  static constexpr int32_t MAX_STACK_DEPTH = 40000;

  int32_t level = -1;
  jrt::Array<T>* nodeArr;

  void reallocate() {
    int32_t oldLength = this->nodeArr->length;
    if (oldLength >= MAX_STACK_DEPTH) {
      throw jrt::IllegalStateException("ArrayStack maximum depth of " + jrt::str(MAX_STACK_DEPTH) +
                                       " exceeded");
    }
    int32_t newLength = jrt::Math::min(MAX_STACK_DEPTH, 4 * oldLength);
    FRLogger::debug("ArrayStack capacity grew from " + jrt::str(oldLength) + " to " +
                    jrt::str(newLength));
    auto* newArray = new jrt::Array<T>(newLength);
    jrt::arraycopy(nodeArr, 0, newArray, 0, nodeArr->length);
    this->nodeArr = newArray;
  }
};

}  // namespace fr
