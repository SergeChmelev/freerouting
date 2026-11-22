#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Records resource usage for a routing job. Pure data, no .cpp. */
// PORT NOTE: gson annotations dropped. How the counters are measured (JVM CPU time / allocated
// bytes) is up to the code that fills them.
class RouterJobResourceUsage : public virtual jrt::Object {
 public:
  // "cpu_time": total CPU time used in seconds
  float cpuTimeUsed = 0.0F;
  // "max_memory": total amount of memory allocated in MB (not the currently used memory)
  float maxMemoryUsed = 0.0F;
  // "peak_memory": peak memory usage across all threads in MB
  float peakMemoryUsed = 0.0F;
  // "io_read": total IO read in MB, including input uploads
  float ioRead = 0.0F;
  // "io_written": total IO write in MB, including output and snapshot downloads
  float ioWrite = 0.0F;
};

}  // namespace fr
