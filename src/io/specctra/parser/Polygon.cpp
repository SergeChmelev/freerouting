#include "io/specctra/parser/Polygon.h"

#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Polygon.h"
#include "geometry/planar/PolygonShape.h"
#include "geometry/planar/Simplex.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/Rectangle.h"

namespace fr::dsn {

fr::Shape* Polygon::transformToBoard(fr::io::CoordinateTransform* coordinateTransform) {
  // PORT NOTE: Java IntPoint[] passed as Point[] -> Array<Point*>.
  jrt::Array<fr::Point*>* corners = new jrt::Array<fr::Point*>(coor->length / 2);
  jrt::Array<double>* currentPoint = new jrt::Array<double>(2);
  for (int32_t i = 0; i < corners->length; i++) {
    (*currentPoint)[0] = (*coor)[2 * i];
    (*currentPoint)[1] = (*coor)[2 * i + 1];
    (*corners)[i] = coordinateTransform->dsnToBoard(currentPoint)->round();
  }
  // Rounded to the resolution of the board, a border stepping back by less than a unit becomes a
  // spike.
  return new fr::PolygonShape(fr::Polygon::withoutSpikes(corners));
}

fr::Shape* Polygon::transformToBoardRel(fr::io::CoordinateTransform* coordinateTransform) {
  if (coor->length < 2) {
    return fr::Simplex::EMPTY();
  }
  jrt::Array<fr::Point*>* corners = new jrt::Array<fr::Point*>(coor->length / 2);
  for (int32_t i = 0; i < corners->length; i++) {
    int32_t currentX = jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard((*coor)[2 * i])));
    int32_t currentY =
        jrt::l2i(jrt::Math::round(coordinateTransform->dsnToBoard((*coor)[2 * i + 1])));
    (*corners)[i] = new fr::IntPoint(currentX, currentY);
  }
  return new fr::PolygonShape(fr::Polygon::withoutSpikes(corners));
}

Rectangle* Polygon::boundingBox() {
  jrt::Array<double>* bounds = new jrt::Array<double>(4);
  (*bounds)[0] = jrt::Integer::MAX_VALUE;
  (*bounds)[1] = jrt::Integer::MAX_VALUE;
  (*bounds)[2] = jrt::Integer::MIN_VALUE;
  (*bounds)[3] = jrt::Integer::MIN_VALUE;
  for (int32_t i = 0; i < coor->length; i++) {
    if (i % 2 == 0) {
      // x coordinate
      (*bounds)[0] = jrt::Math::min((*bounds)[0], (*coor)[i]);
      (*bounds)[2] = jrt::Math::max((*bounds)[2], (*coor)[i]);
    } else {
      // x coordinate
      (*bounds)[1] = jrt::Math::min((*bounds)[1], (*coor)[i]);
      (*bounds)[3] = jrt::Math::max((*bounds)[3], (*coor)[i]);
    }
  }
  return new Rectangle(layer, bounds);
}

void Polygon::writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) {
  file->startScope();
  file->write("polygon ");
  identifierType->write(this->layer->name, file);
  file->write(" ");
  file->write(jrt::str(0));
  int32_t cornerCount = coor->length / 2;
  for (int32_t i = 0; i < cornerCount; i++) {
    file->newLine();
    file->write(jrt::str((*coor)[2 * i]));
    file->write(" ");
    file->write(jrt::str((*coor)[2 * i + 1]));
  }
  file->endScope();
}

void Polygon::writeScopeInt(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) {
  file->startScope();
  file->write("polygon ");
  identifierType->write(this->layer->name, file);
  file->write(" ");
  file->write(jrt::str(0));
  int32_t cornerCount = coor->length / 2;
  for (int32_t i = 0; i < cornerCount; i++) {
    file->newLine();
    int32_t currentCoor = jrt::l2i(jrt::Math::round((*coor)[2 * i]));
    file->write(jrt::str(currentCoor));
    file->write(" ");
    currentCoor = jrt::l2i(jrt::Math::round((*coor)[2 * i + 1]));
    file->write(jrt::str(currentCoor));
  }
  file->endScope();
}

}  // namespace fr::dsn
