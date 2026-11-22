#pragma once

#include "fwd.h"
#include "io/specctra/parser/Shape.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for reading and writing circle scopes from dsn-files. */
class Circle : public Shape {
 public:
  jrt::Array<double>* coor;

  /**
   * Creates a new circle from the input parameters. coor is an array of dimension 3: coor[0] is the
   * "radius" (Java doc; DSN circle scopes carry the diameter), coor[1] / coor[2] the center x / y.
   */
  Circle(Layer* layer, jrt::Array<double>* coor) : Shape(layer), coor(coor) {}

  Circle(Layer* layer, double radius, double centerX, double centerY)
      : Shape(layer), coor(jrt::Array<double>::of({radius, centerX, centerY})) {}

  fr::Shape* transformToBoard(fr::io::CoordinateTransform* coordinateTransform) override;

  fr::Shape* transformToBoardRel(fr::io::CoordinateTransform* coordinateTransform) override;

  Rectangle* boundingBox() override;

  void writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) override;

  void writeScopeInt(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) override;
};

}  // namespace fr::dsn
