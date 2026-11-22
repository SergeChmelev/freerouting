#include "io/specctra/parser/KiCadRoundRectPad.h"

#include "board/model/structure/Unit.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/Polygon.h"
#include "io/specctra/parser/Shape.h"

namespace fr::dsn {

std::regex* KiCadRoundRectPad::NAME() {
  // PORT NOTE: the unescaped ']' outside a character class of the Java pattern is written '\]'
  // (same meaning; std::regex ECMAScript is stricter).
  static std::regex* v = new std::regex(
      "RoundRect\\[[^\\]]*\\](?:\\[[^\\]]*\\])?Pad_"
      "(\\d+(?:\\.\\d*)?)x(\\d+(?:\\.\\d*)?)_(\\d+(?:\\.\\d*)?)_um_"
      "(\\d+(?:\\.\\d*)?)_([0-9A-Fa-f]+)",
      std::regex::ECMAScript);
  return v;
}

Shape* KiCadRoundRectPad::correct(const std::string& padstackName, Shape* shape,
                                  std::optional<fr::Unit> dsnUnitOpt,
                                  fr::io::CoordinateTransform* coordinateTransform) {
  // PORT NOTE: padstackName is never null in C++.
  auto* polygon = jrt::fast_cast<Polygon>(shape);
  if (polygon == nullptr || !dsnUnitOpt.has_value() || coordinateTransform == nullptr ||
      polygon->coor->length < 6) {
    return shape;
  }
  fr::Unit dsnUnit = *dsnUnitOpt;
  std::smatch matcher;
  if (!std::regex_match(padstackName, matcher, *NAME()) ||
      jrt::Long::parseLong(matcher[5].str(), 16) != 0) {
    // Not a KiCad rounded-rectangle pad, or one with chamfered corners. A chamfer ratio without
    // chamfered corners (a chamfered-rectangle pad with no corner selected) is a plain one.
    return shape;
  }
  double widthUm = jrt::Double::parseDouble(matcher[1].str());
  double heightUm = jrt::Double::parseDouble(matcher[2].str());
  double radiusUm = legacyRealRadiusUm(jrt::Double::parseDouble(matcher[3].str()));
  double extraUm = jrt::Double::parseDouble(matcher[3].str()) - radiusUm;
  if (radiusUm <= 0 || extraUm <= 0 || 2 * radiusUm > jrt::Math::min(widthUm, heightUm)) {
    return shape;
  }

  double minX = jrt::Double::POSITIVE_INFINITY;
  double minY = jrt::Double::POSITIVE_INFINITY;
  double maxX = jrt::Double::NEGATIVE_INFINITY;
  double maxY = jrt::Double::NEGATIVE_INFINITY;
  for (int32_t i = 0; i + 1 < polygon->coor->length; i += 2) {
    minX = jrt::Math::min(minX, (*polygon->coor)[i]);
    maxX = jrt::Math::max(maxX, (*polygon->coor)[i]);
    minY = jrt::Math::min(minY, (*polygon->coor)[i + 1]);
    maxY = jrt::Math::max(maxY, (*polygon->coor)[i + 1]);
  }
  // A legacy export is enlarged by extraUm on every side, a fixed one is not.
  double excessX = (fr::Unit_scale(maxX - minX, dsnUnit, fr::Unit::UM) - widthUm) / 2;
  double excessY = (fr::Unit_scale(maxY - minY, dsnUnit, fr::Unit::UM) - heightUm) / 2;
  if (jrt::Math::abs(excessX - extraUm) > extraUm / 2 ||
      jrt::Math::abs(excessY - extraUm) > extraUm / 2) {
    return shape;
  }

  double centreX = (minX + maxX) / 2;
  double centreY = (minY + maxY) / 2;
  // The bounding box alone does not tell a legacy pad from another polygon of the same extent:
  // the legacy exporter writes a polygon whose vertices all lie on the enlarged rounded
  // rectangle (straight edges and chords inscribed in the arcs of radius R), once around.
  if (!isOnOutlineOnceAround(polygon->coor, centreX, centreY, (maxX - minX) / 2,
                             (maxY - minY) / 2,
                             fr::Unit_scale(radiusUm + extraUm, fr::Unit::UM, dsnUnit),
                             fr::Unit_scale(extraUm / 2, fr::Unit::UM, dsnUnit))) {
    return shape;
  }
  double halfWidth = fr::Unit_scale(widthUm / 2, fr::Unit::UM, dsnUnit);
  double halfHeight = fr::Unit_scale(heightUm / 2, fr::Unit::UM, dsnUnit);
  double radius = fr::Unit_scale(radiusUm, fr::Unit::UM, dsnUnit);
  // The straight edges go to the nearest board coordinate outside the real pad (no change for a
  // pad whose edges fall on the board grid, as they do for KiCad exports at the usual
  // resolutions), and the corner vertices one board unit further out than the tangent polygon,
  // which covers their rounding to board coordinates.
  // PORT NOTE: Java evaluates the arguments left to right; made explicit here.
  double left = outwards(centreX - halfWidth, -1, coordinateTransform);
  double bottom = outwards(centreY - halfHeight, -1, coordinateTransform);
  double right = outwards(centreX + halfWidth, 1, coordinateTransform);
  double top = outwards(centreY + halfHeight, 1, coordinateTransform);
  int32_t n = segmentsPerQuarter(radiusUm);
  double margin = coordinateTransform->boardToDsn(1);
  return new Polygon(polygon->layer, corners(left, bottom, right, top, radius, n, margin));
}

double KiCadRoundRectPad::outwards(double dsn, int32_t sign,
                                   fr::io::CoordinateTransform* coordinateTransform) {
  double board = coordinateTransform->dsnToBoard(dsn);
  double nearest = jrt::Math::rint(board);
  // tolerate the floating point error of the DSN to board scaling
  if (jrt::Math::abs(board - nearest) > 1e-6) {
    nearest = sign > 0 ? jrt::Math::ceil(board) : jrt::Math::floor(board);
  }
  return coordinateTransform->boardToDsn(nearest);
}

double KiCadRoundRectPad::legacyRealRadiusUm(double nameRadiusUm) {
  int64_t nameNm = jrt::Math::round(nameRadiusUm * 1000);
  int64_t guess = jrt::Math::round(static_cast<double>(nameNm) / (1 + LEGACY_COMPENSATION));
  for (int64_t r = guess - 2; r <= guess + 2; r++) {
    if (r > 0 && r + jrt::Math::round(static_cast<double>(r) * LEGACY_COMPENSATION) == nameNm) {
      return static_cast<double>(r) / 1000.0;
    }
  }
  return nameRadiusUm / (1 + LEGACY_COMPENSATION);
}

bool KiCadRoundRectPad::isOnOutlineOnceAround(jrt::Array<double>* coor, double centreX,
                                              double centreY, double halfWidth,
                                              double halfHeight, double radius,
                                              double tolerance) {
  int32_t count = coor->length / 2;
  jrt::Array<double>* x = new jrt::Array<double>(count);
  jrt::Array<double>* y = new jrt::Array<double>(count);
  int32_t distinct = 0;
  for (int32_t i = 0; i < count; i++) {
    double px = (*coor)[2 * i];
    double py = (*coor)[2 * i + 1];
    double qx = jrt::Math::abs(px - centreX) - (halfWidth - radius);
    double qy = jrt::Math::abs(py - centreY) - (halfHeight - radius);
    double distance = jrt::Math::hypot(jrt::Math::max(qx, 0.0), jrt::Math::max(qy, 0.0)) +
                      jrt::Math::min(jrt::Math::max(qx, qy), 0.0) - radius;
    if (jrt::Math::abs(distance) > tolerance) {
      return false;
    }
    // vertices closer than the tolerance are one (KiCad writes 0 and 0.001 at a half circle)
    if (distinct == 0 ||
        jrt::Math::hypot(px - (*x)[distinct - 1], py - (*y)[distinct - 1]) > tolerance) {
      (*x)[distinct] = px;
      (*y)[distinct] = py;
      distinct++;
    }
  }
  if (distinct > 1 &&
      jrt::Math::hypot((*x)[0] - (*x)[distinct - 1], (*y)[0] - (*y)[distinct - 1]) <= tolerance) {
    distinct--;
  }
  if (distinct < 3) {
    return false;
  }
  double turning = 0;
  for (int32_t i = 0; i < distinct; i++) {
    int32_t j = (i + 1) % distinct;
    int32_t k = (i + 2) % distinct;
    double ax = (*x)[j] - (*x)[i];
    double ay = (*y)[j] - (*y)[i];
    double bx = (*x)[k] - (*x)[j];
    double by = (*y)[k] - (*y)[j];
    turning += jrt::Math::atan2(ax * by - ay * bx, ax * bx + ay * by);
  }
  return jrt::Math::abs(jrt::Math::abs(turning) - 2 * jrt::Math::PI) < 1e-6;
}

int32_t KiCadRoundRectPad::segmentsPerQuarter(double radiusUm) {
  // The excess r * (1 / cos(PI / (4 n)) - 1) decreases to 0 as n grows, for any radius.
  int32_t n = 1;
  while (radiusUm * (1 / jrt::Math::cos(jrt::Math::PI / (4 * n)) - 1) > MAX_CORNER_EXCESS_UM) {
    n++;
  }
  return n;
}

jrt::Array<double>* KiCadRoundRectPad::corners(double left, double bottom, double right,
                                               double top, double radius, int32_t n,
                                               double margin) {
  double step = jrt::Math::PI / 2 / n;
  double along = radius * jrt::Math::tan(step / 2) + margin;
  jrt::Array<double>* edgeX = jrt::Array<double>::of({right, left, left, right});
  jrt::Array<double>* edgeY = jrt::Array<double>::of({top, top, bottom, bottom});
  jrt::Array<double>* signX = jrt::Array<double>::of({1, -1, -1, 1});
  jrt::Array<double>* signY = jrt::Array<double>::of({1, 1, -1, -1});
  jrt::Array<double>* coor = new jrt::Array<double>(8 * n);
  int32_t index = 0;
  for (int32_t quarter = 0; quarter < 4; quarter++) {
    double arcCentreX = (*edgeX)[quarter] - (*signX)[quarter] * radius;
    double arcCentreY = (*edgeY)[quarter] - (*signY)[quarter] * radius;
    for (int32_t k = 0; k < n; k++) {
      double x;
      double y;
      if (n == 1) {
        x = (*edgeX)[quarter];
        y = (*edgeY)[quarter];
      } else if (k == 0 || k == n - 1) {
        // On a straight edge: the vertical one first in quarters 0 and 2, last in 1 and 3.
        if ((k == 0) == (quarter % 2 == 0)) {
          x = (*edgeX)[quarter];
          y = arcCentreY + (*signY)[quarter] * along;
        } else {
          x = arcCentreX + (*signX)[quarter] * along;
          y = (*edgeY)[quarter];
        }
      } else {
        double angle = quarter * jrt::Math::PI / 2 + (k + 0.5) * step;
        double distance = radius / jrt::Math::cos(step / 2) + margin;
        x = arcCentreX + distance * jrt::Math::cos(angle);
        y = arcCentreY + distance * jrt::Math::sin(angle);
      }
      (*coor)[index++] = x;
      (*coor)[index++] = y;
    }
  }
  return coor;
}

}  // namespace fr::dsn
