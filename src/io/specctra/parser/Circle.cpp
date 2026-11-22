#include "io/specctra/parser/Circle.h"

#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/Rectangle.h"

namespace fr::dsn {

fr::Shape* Circle::transformToBoard(fr::io::CoordinateTransform* coordinateTransform) {
  jrt::Array<double>* location = new jrt::Array<double>(2);
  (*location)[0] = (*coor)[1];
  (*location)[1] = (*coor)[2];
  fr::IntPoint* center = coordinateTransform->dsnToBoard(location)->round();
  int32_t radius = jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard((*coor)[0]) / 2));
  return new fr::Circle(center, radius);
}

fr::Shape* Circle::transformToBoardRel(fr::io::CoordinateTransform* coordinateTransform) {
  jrt::Array<int32_t>* newCoor = new jrt::Array<int32_t>(3);
  (*newCoor)[0] = jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard((*coor)[0]) / 2));
  for (int32_t i = 1; i < 3; i++) {
    (*newCoor)[i] = jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard((*coor)[i])));
  }
  return new fr::Circle(new fr::IntPoint((*newCoor)[1], (*newCoor)[2]), (*newCoor)[0]);
}

Rectangle* Circle::boundingBox() {
  jrt::Array<double>* bounds = new jrt::Array<double>(4);
  (*bounds)[0] = (*coor)[1] - (*coor)[0];
  (*bounds)[1] = (*coor)[2] - (*coor)[0];
  (*bounds)[2] = (*coor)[1] + (*coor)[0];
  (*bounds)[3] = (*coor)[2] + (*coor)[0];
  return new Rectangle(layer, bounds);
}

void Circle::writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) {
  file->newLine();
  file->write("(circle ");
  identifierType->write(this->layer->name, file);
  for (int32_t i = 0; i < coor->length; i++) {
    file->write(" ");
    file->write(jrt::str((*coor)[i]));
  }
  file->write(")");
}

void Circle::writeScopeInt(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) {
  file->newLine();
  file->write("(circle ");
  identifierType->write(this->layer->name, file);
  for (int32_t i = 0; i < coor->length; i++) {
    file->write(" ");
    int32_t currentCoor = jrt::l2i(jrt::Math::round((*coor)[i]));
    file->write(jrt::str(currentCoor));
  }
  file->write(")");
}

}  // namespace fr::dsn
