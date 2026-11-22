#pragma once

#include "fwd.h"
#include "io/specctra/parser/Path.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for reading and writing path scopes consisting of a polygon from dsn-files. */
class PolygonPath : public Path {
 public:
  /** Creates a new instance of PolygonPath. */
  PolygonPath(Layer* layer, double width, jrt::Array<double>* coordinateArr)
      : Path(layer, width, coordinateArr) {}

  /** Writes this path as a scope to an output dsn-file. */
  void writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) override;

  void writeScopeInt(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) override;

  fr::Shape* transformToBoard(fr::io::CoordinateTransform* coordinateTransform) override;

  /**
   * The exact copper of this path, in the same coordinates as transformToBoardRel, when it is a
   * round or oval pad: a wide path of one or two corners. Null otherwise.
   */
  virtual fr::PadOval* padOvalRel(fr::io::CoordinateTransform* coordinateTransform);

  fr::Shape* transformToBoardRel(fr::io::CoordinateTransform* coordinateTransform) override;

  Rectangle* boundingBox() override;
};

}  // namespace fr::dsn
