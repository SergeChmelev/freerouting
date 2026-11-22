// Port of app.freerouting.board.state.CoordinateTransform
#include "board/state/CoordinateTransform.h"

#include "board/model/items/PrintableShape.h"
#include "board/model/structure/Unit.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"
#include "logger/FRLogger.h"

namespace fr {

CoordinateTransform::CoordinateTransform(double userUnitFactor, Unit userUnit,
                                         double boardUnitFactor, Unit boardUnit)
    : userUnit(userUnit),
      userUnitFactor(userUnitFactor),
      boardUnit(boardUnit),
      boardUnitFactor(boardUnitFactor),
      scaleFactor(boardUnitFactor / userUnitFactor) {
  if (userUnitFactor != 1.0) {
    throw jrt::RuntimeException("userUnitFactor must be 1.0");
  }
}

double CoordinateTransform::boardToUser(double value) {
  return Unit_scale(value * scaleFactor, boardUnit, userUnit);
}

FloatPoint* CoordinateTransform::boardToUser(FloatPoint* point) {
  return new FloatPoint(boardToUser(point->x), boardToUser(point->y));
}

PrintableShape* CoordinateTransform::boardToUser(Shape* shape, const std::string& locale) {
  PrintableShape* result;
  if (auto* circle = jrt::fast_cast<Circle>(shape)) {
    result = boardToUser(circle, locale);
  } else if (auto* box = jrt::fast_cast<IntBox>(shape)) {
    result = boardToUser(box, locale);
  } else if (auto* polylineShape = jrt::fast_cast<PolylineShape>(shape)) {
    result = boardToUser(polylineShape, locale);
  } else {
    FRLogger::warn("CoordinateTransform.board_to_user not yet implemented for shape");
    result = nullptr;
  }
  return result;
}

PrintableShape_Circle* CoordinateTransform::boardToUser(Circle* circle,
                                                        const std::string& locale) {
  return new PrintableShape_Circle(boardToUser(circle->center->toFloat()),
                                   boardToUser(static_cast<double>(circle->radius)), locale);
}

PrintableShape_Rectangle* CoordinateTransform::boardToUser(IntBox* box,
                                                           const std::string& locale) {
  return new PrintableShape_Rectangle(boardToUser(box->ll->toFloat()),
                                      boardToUser(box->ur->toFloat()), locale);
}

PrintableShape_Polygon* CoordinateTransform::boardToUser(PolylineShape* shape,
                                                         const std::string& locale) {
  jrt::Array<FloatPoint*>* corners = shape->cornerApproxArr();
  auto* transformedCorners = new jrt::Array<FloatPoint*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*transformedCorners)[i] = boardToUser((*corners)[i]);
  }
  return new PrintableShape_Polygon(transformedCorners, locale);
}

double CoordinateTransform::userToBoard(double value) {
  return Unit_scale(value / scaleFactor, userUnit, boardUnit);
}

FloatPoint* CoordinateTransform::userToBoard(FloatPoint* point) {
  return new FloatPoint(userToBoard(point->x), userToBoard(point->y));
}

}  // namespace fr
