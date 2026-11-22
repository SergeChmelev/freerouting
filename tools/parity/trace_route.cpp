// Differential-testing helper: C++ twin of tools/parity/TraceRoute.java.
// run: FR_TRACE_ALL=1 trace_route <file.dsn>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "core/facade/FreeroutingHeadlessEngine.h"
#include "core/facade/HeadlessRoutingRequest.h"
#include "core/facade/HeadlessRoutingResult.h"
#include "jrt/jrt.h"
#include "logger/FRLogger.h"

using namespace fr;

int main(int argc, char** argv) {
  FRLogger::logLevel = FRLogger::LEVEL_TRACE;
  FRLogger::granularTraceEnabled = true;
  std::ifstream in(argv[1], std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto* bytes = new jrt::Array<int8_t>(static_cast<int32_t>(data.size()));
  std::memcpy(bytes->data(), data.data(), data.size());
  std::string name = argv[1];
  name = name.substr(name.find_last_of('/') + 1);
  if (jrt::endsWith(name, ".dsn")) name = name.substr(0, name.size() - 4);
  HeadlessRoutingResult* r =
      FreeroutingHeadlessEngine::route(HeadlessRoutingRequest::builder()->dsnBytes(bytes)->designName(name)->build());
  std::cout << "RESULT unrouted=" << r->unroutedConnections() << std::endl;
  return 0;
}
