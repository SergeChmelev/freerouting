#pragma once

#include "fwd.h"
#include "io/specctra/parser/Shape.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Describes a polygon in a Specctra dsn file. */
class Polygon : public Shape {
 public:
  jrt::Array<double>* coor;

  /**
   * Creates a new Polygon. coor is an array of dimension 2 * point_count; the x coordinates are at
   * the even indices, the y coordinates at the odd indices.
   */
  Polygon(Layer* layer, jrt::Array<double>* coor) : Shape(layer), coor(coor) {}

  fr::Shape* transformToBoard(fr::io::CoordinateTransform* coordinateTransform) override;

  fr::Shape* transformToBoardRel(fr::io::CoordinateTransform* coordinateTransform) override;

  Rectangle* boundingBox() override;

  /** Writes this polygon as a scope to an output dsn-file. */
  void writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) override;

  void writeScopeInt(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) override;
};

}  // namespace fr::dsn
