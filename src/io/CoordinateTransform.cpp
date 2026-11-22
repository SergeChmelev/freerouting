#include "io/CoordinateTransform.h"

#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/Vector.h"
#include "io/specctra/parser/Circle.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/Polygon.h"
#include "io/specctra/parser/Rectangle.h"
#include "io/specctra/parser/Shape.h"
#include "logger/FRLogger.h"

namespace fr::io {

jrt::Array<double>* CoordinateTransform::boardToDsn(fr::FloatPoint* point) {
  auto* result = new jrt::Array<double>(2);
  (*result)[0] = boardToDsn(point->x) + baseX;
  (*result)[1] = boardToDsn(point->y) + baseY;
  return result;
}

jrt::Array<double>* CoordinateTransform::boardToDsn(jrt::Array<fr::FloatPoint*>* points) {
  auto* result = new jrt::Array<double>(2 * points->length);
  for (int32_t i = 0; i < points->length; i++) {
    (*result)[2 * i] = boardToDsn((*points)[i]->x) + baseX;
    (*result)[2 * i + 1] = boardToDsn((*points)[i]->y) + baseY;
  }
  return result;
}

jrt::Array<double>* CoordinateTransform::boardToDsn(jrt::Array<fr::Line*>* lines) {
  auto* result = new jrt::Array<double>(4 * lines->length);
  for (int32_t i = 0; i < lines->length; i++) {
    fr::FloatPoint* a = (*lines)[i]->a->toFloat();
    fr::FloatPoint* b = (*lines)[i]->b->toFloat();
    (*result)[4 * i] = boardToDsn(a->x) + baseX;
    (*result)[4 * i + 1] = boardToDsn(a->y) + baseY;
    (*result)[4 * i + 2] = boardToDsn(b->x) + baseX;
    (*result)[4 * i + 3] = boardToDsn(b->y) + baseY;
  }
  return result;
}

jrt::Array<double>* CoordinateTransform::boardToDsn(fr::Vector* vector) {
  auto* result = new jrt::Array<double>(2);
  fr::FloatPoint* value = vector->toFloat();
  (*result)[0] = boardToDsn(value->x);
  (*result)[1] = boardToDsn(value->y);
  return result;
}

jrt::Array<double>* CoordinateTransform::boardToDsn(fr::IntBox* box) {
  auto* result = new jrt::Array<double>(4);
  (*result)[0] = box->ll->x / scaleFactor + baseX;
  (*result)[1] = box->ll->y / scaleFactor + baseY;
  (*result)[2] = box->ur->x / scaleFactor + baseX;
  (*result)[3] = box->ur->y / scaleFactor + baseY;
  return result;
}

fr::dsn::Shape* CoordinateTransform::boardToDsn(fr::Shape* boardShape, fr::dsn::Layer* layer) {
  fr::dsn::Shape* result;
  if (auto* box = jrt::fast_cast<fr::IntBox>(boardShape)) {
    result = new fr::dsn::Rectangle(layer, boardToDsn(box));
  } else if (jrt::fast_cast<fr::PolylineShape>(boardShape) != nullptr) {
    jrt::Array<fr::FloatPoint*>* corners = boardShape->cornerApproxArr();
    jrt::Array<double>* coordinates = boardToDsn(corners);
    result = new fr::dsn::Polygon(layer, coordinates);
  } else if (auto* boardCircle = jrt::fast_cast<fr::Circle>(boardShape)) {
    double diameter = 2 * boardToDsn(static_cast<double>(boardCircle->radius));
    jrt::Array<double>* centerCoordinates = boardToDsn(boardCircle->center->toFloat());
    result = new fr::dsn::Circle(layer, diameter, (*centerCoordinates)[0], (*centerCoordinates)[1]);
  } else {
    FRLogger::warn("CoordinateTransform.board_to_dsn not yet implemented for boardShape");
    result = nullptr;
  }
  return result;
}

jrt::Array<double>* CoordinateTransform::boardToDsnRel(fr::FloatPoint* point) {
  auto* result = new jrt::Array<double>(2);
  (*result)[0] = boardToDsn(point->x);
  (*result)[1] = boardToDsn(point->y);
  return result;
}

jrt::Array<double>* CoordinateTransform::boardToDsnRel(jrt::Array<fr::FloatPoint*>* points) {
  auto* result = new jrt::Array<double>(2 * points->length);
  for (int32_t i = 0; i < points->length; i++) {
    (*result)[2 * i] = boardToDsn((*points)[i]->x);
    (*result)[2 * i + 1] = boardToDsn((*points)[i]->y);
  }
  return result;
}

jrt::Array<double>* CoordinateTransform::boardToDsnRel(fr::IntBox* box) {
  auto* result = new jrt::Array<double>(4);
  (*result)[0] = box->ll->x / scaleFactor;
  (*result)[1] = box->ll->y / scaleFactor;
  (*result)[2] = box->ur->x / scaleFactor;
  (*result)[3] = box->ur->y / scaleFactor;
  return result;
}

fr::dsn::Shape* CoordinateTransform::boardToDsnRel(fr::Shape* boardShape,
                                                   fr::dsn::Layer* layer) {
  fr::dsn::Shape* result;
  if (auto* box = jrt::fast_cast<fr::IntBox>(boardShape)) {
    result = new fr::dsn::Rectangle(layer, boardToDsnRel(box));
  } else if (jrt::fast_cast<fr::PolylineShape>(boardShape) != nullptr) {
    jrt::Array<fr::FloatPoint*>* corners = boardShape->cornerApproxArr();
    jrt::Array<double>* coordinates = boardToDsnRel(corners);
    result = new fr::dsn::Polygon(layer, coordinates);
  } else if (auto* boardCircle = jrt::fast_cast<fr::Circle>(boardShape)) {
    double diameter = 2 * boardToDsn(static_cast<double>(boardCircle->radius));
    jrt::Array<double>* centerCoordinates = boardToDsnRel(boardCircle->center->toFloat());
    result = new fr::dsn::Circle(layer, diameter, (*centerCoordinates)[0], (*centerCoordinates)[1]);
  } else {
    FRLogger::warn("CoordinateTransform.board_to_dsn not yet implemented for boardShape");
    result = nullptr;
  }
  return result;
}

fr::FloatPoint* CoordinateTransform::dsnToBoard(jrt::Array<double>* tuple) {
  double x = dsnToBoard((*tuple)[0] - baseX);
  double y = dsnToBoard((*tuple)[1] - baseY);
  return new fr::FloatPoint(x, y);
}

fr::FloatPoint* CoordinateTransform::dsnToBoardRel(jrt::Array<double>* tuple) {
  double x = dsnToBoard((*tuple)[0]);
  double y = dsnToBoard((*tuple)[1]);
  return new fr::FloatPoint(x, y);
}

}  // namespace fr::io
