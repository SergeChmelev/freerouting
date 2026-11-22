#pragma once

#include "fwd.h"
#include "io/specctra/parser/Shape.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for writing path scopes from dsn-files. */
class Path : public Shape {
 public:
  double width;
  jrt::Array<double>* coordinateArr;

  /** Writes this path as a scope to an output dsn-file. Throws jrt::IOException. */
  void writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifier) override = 0;

 protected:
  /** Creates a new instance of Path (package-private in Java). */
  Path(Layer* layer, double width, jrt::Array<double>* coordinateArr)
      : Shape(layer), width(width), coordinateArr(coordinateArr) {}
};

}  // namespace fr::dsn
