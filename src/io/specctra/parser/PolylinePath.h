#pragma once

#include "fwd.h"
#include "io/specctra/parser/Path.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Describes a path defined by a sequence of lines instead of a sequence of corners. */
class PolylinePath : public Path {
 public:
  /** Creates a new instance of PolylinePath. */
  PolylinePath(Layer* layer, double width, jrt::Array<double>* corners)
      : Path(layer, width, corners) {}

  /** Writes this path as a scope to an output dsn-file. */
  void writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifier) override;

  void writeScopeInt(fr::IndentFileWriter* file, fr::IdentifierType* identifier) override;

  fr::Shape* transformToBoardRel(fr::io::CoordinateTransform* coordinateTransform) override;

  fr::Shape* transformToBoard(fr::io::CoordinateTransform* coordinateTransform) override;

  Rectangle* boundingBox() override;
};

}  // namespace fr::dsn
