#pragma once

#include "fwd.h"
#include "io/specctra/parser/Shape.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Describes a rectangle in a Specctra dsn file. */
class Rectangle : public Shape {
 public:
  jrt::Array<double>* coor;

  /**
   * Creates a new instance of Rectangle. coor is an array of dimension 4 and contains the
   * rectangle coordinates in the following order: lower left x, lower left y, upper right x,
   * upper right y.
   */
  Rectangle(Layer* layer, jrt::Array<double>* coor) : Shape(layer), coor(coor) {}

  Rectangle* boundingBox() override { return this; }

  /**
   * Creates the smallest rectangle containing this rectangle and other.
   * PORT NOTE: Java method union() renamed union_() (C++ keyword).
   */
  virtual Rectangle* union_(Rectangle* other);

  fr::Shape* transformToBoardRel(fr::io::CoordinateTransform* coordinateTransform) override;

  fr::Shape* transformToBoard(fr::io::CoordinateTransform* coordinateTransform) override;

  /** Writes this rectangle as a scope to an output dsn-file. */
  void writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifier) override;

  void writeScopeInt(fr::IndentFileWriter* file, fr::IdentifierType* identifier) override;
};

}  // namespace fr::dsn
