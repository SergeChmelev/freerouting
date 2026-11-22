#include "io/specctra/parser/PolygonPath.h"

#include "core/library/PadOval.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/PolygonShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/Rectangle.h"

namespace fr::dsn {

void PolygonPath::writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) {
  file->startScope();
  file->write("path ");
  identifierType->write(this->layer->name, file);
  file->write(" ");
  file->write(jrt::str(this->width));
  int32_t cornerCount = coordinateArr->length / 2;
  for (int32_t i = 0; i < cornerCount; i++) {
    file->newLine();
    file->write(jrt::str((*coordinateArr)[2 * i]));
    file->write(" ");
    file->write(jrt::str((*coordinateArr)[2 * i + 1]));
  }
  file->endScope();
}

void PolygonPath::writeScopeInt(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) {
  file->startScope();
  file->write("path ");
  identifierType->write(this->layer->name, file);
  file->write(" ");
  file->write(jrt::str(this->width));
  int32_t cornerCount = coordinateArr->length / 2;
  for (int32_t i = 0; i < cornerCount; i++) {
    file->newLine();
    int32_t currentCoor = jrt::l2i(jrt::Math::round((*coordinateArr)[2 * i]));
    file->write(jrt::str(currentCoor));
    file->write(" ");
    currentCoor = jrt::l2i(jrt::Math::round((*coordinateArr)[2 * i + 1]));
    file->write(jrt::str(currentCoor));
  }
  file->endScope();
}

fr::Shape* PolygonPath::transformToBoard(fr::io::CoordinateTransform* coordinateTransform) {
  jrt::Array<fr::FloatPoint*>* corners =
      new jrt::Array<fr::FloatPoint*>(this->coordinateArr->length / 2);
  jrt::Array<double>* currentPoint = new jrt::Array<double>(2);
  for (int32_t i = 0; i < corners->length; i++) {
    (*currentPoint)[0] = (*this->coordinateArr)[2 * i];
    (*currentPoint)[1] = (*this->coordinateArr)[2 * i + 1];
    (*corners)[i] = coordinateTransform->dsnToBoard(currentPoint);
  }
  const double offset = coordinateTransform->dsnToBoard(this->width) / 2;
  if (corners->length <= 2) {
    fr::IntOctagon* boundingOct = fr::FloatPoint::boundingOctagon(corners);
    return boundingOct->enlarge(offset);
  }
  // PORT NOTE: Java IntPoint[] passed as Point[] -> Array<Point*>.
  jrt::Array<fr::Point*>* roundedCornerArr = new jrt::Array<fr::Point*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*roundedCornerArr)[i] = (*corners)[i]->round();
  }
  fr::Shape* result = new fr::PolygonShape(roundedCornerArr);
  if (offset > 0) {
    result = result->boundingTile()->enlarge(offset);
  }
  return result;
}

fr::PadOval* PolygonPath::padOvalRel(fr::io::CoordinateTransform* coordinateTransform) {
  int32_t cornerCount = this->coordinateArr->length / 2;
  double radius = coordinateTransform->dsnToBoard(this->width) / 2;
  if (cornerCount < 1 || cornerCount > 2 || !(radius > 0)) {
    return nullptr;
  }
  fr::FloatPoint* start = coordinateTransform->dsnToBoardRel(
      jrt::Array<double>::of({(*this->coordinateArr)[0], (*this->coordinateArr)[1]}));
  fr::FloatPoint* end = coordinateTransform->dsnToBoardRel(
      jrt::Array<double>::of({(*this->coordinateArr)[2 * cornerCount - 2],
                              (*this->coordinateArr)[2 * cornerCount - 1]}));
  return new fr::PadOval(start, end, radius);
}

fr::Shape* PolygonPath::transformToBoardRel(fr::io::CoordinateTransform* coordinateTransform) {
  jrt::Array<fr::FloatPoint*>* corners =
      new jrt::Array<fr::FloatPoint*>(this->coordinateArr->length / 2);
  jrt::Array<double>* currentPoint = new jrt::Array<double>(2);
  for (int32_t i = 0; i < corners->length; i++) {
    (*currentPoint)[0] = (*this->coordinateArr)[2 * i];
    (*currentPoint)[1] = (*this->coordinateArr)[2 * i + 1];
    (*corners)[i] = coordinateTransform->dsnToBoardRel(currentPoint);
  }
  const double offset = coordinateTransform->dsnToBoard(this->width) / 2;
  if (corners->length <= 2) {
    fr::IntOctagon* boundingOct = fr::FloatPoint::boundingOctagon(corners);
    return boundingOct->enlarge(offset);
  }
  jrt::Array<fr::Point*>* roundedCornerArr = new jrt::Array<fr::Point*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*roundedCornerArr)[i] = (*corners)[i]->round();
  }
  fr::Shape* result = new fr::PolygonShape(roundedCornerArr);
  if (offset > 0) {
    result = result->boundingTile()->enlarge(offset);
  }
  return result;
}

Rectangle* PolygonPath::boundingBox() {
  const double offset = this->width / 2;
  jrt::Array<double>* bounds = new jrt::Array<double>(4);
  (*bounds)[0] = jrt::Integer::MAX_VALUE;
  (*bounds)[1] = jrt::Integer::MAX_VALUE;
  (*bounds)[2] = jrt::Integer::MIN_VALUE;
  (*bounds)[3] = jrt::Integer::MIN_VALUE;
  for (int32_t i = 0; i < coordinateArr->length; i++) {
    if (i % 2 == 0) {
      // x coordinate
      (*bounds)[0] = jrt::Math::min((*bounds)[0], (*coordinateArr)[i] - offset);
      (*bounds)[2] = jrt::Math::max((*bounds)[2], (*coordinateArr)[i]) + offset;
    } else {
      // x coordinate
      (*bounds)[1] = jrt::Math::min((*bounds)[1], (*coordinateArr)[i] - offset);
      (*bounds)[3] = jrt::Math::max((*bounds)[3], (*coordinateArr)[i] + offset);
    }
  }
  return new Rectangle(layer, bounds);
}

}  // namespace fr::dsn
