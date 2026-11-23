// Self-test of jrt::dispose and its verification mode (FR_DISPOSE_CHECK=1, MEMORY_MODEL.md
// stage C): a disposed object that is still referenced must be reported as reachable after a
// full collection, a disposed object nobody references must not. Run by ctest with the
// environment set; the expected report line is matched by PASS_REGULAR_EXPRESSION.
#include <cstdio>

#include "jrt/alloc.h"
#include "jrt/lang.h"

namespace {

struct Node {
  virtual ~Node() = default;
  Node* next = nullptr;
  int value = 0;
};

// Keeps the dangling reference in collected memory reachable from a global.
Node* volatile g_holder = nullptr;

__attribute__((noinline)) void make_garbage_disposals() {
  for (int i = 0; i < 1000; ++i) {
    jrt::dispose(new Node());  // nobody keeps these
  }
}

__attribute__((noinline)) void make_dangling_disposal() {
  Node* holder = new Node();
  holder->next = new Node();
  g_holder = holder;
  jrt::dispose(holder->next);  // holder->next now dangles: must be reported
}

}  // namespace

int main() {
  make_garbage_disposals();
  make_dangling_disposal();
  // A stack object is not a heap block: dispose leaves it alone.
  Node onStack;
  jrt::dispose(&onStack);
  std::printf("disposed %zu\n", jrt::disposed_count());
  return jrt::disposed_count() == 1001 ? 0 : 1;
}
