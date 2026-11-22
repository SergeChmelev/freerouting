// Port of app.freerouting.board.model.items.PrintableShape (+ nested Circle, Rectangle, Polygon).
#include "board/model/items/PrintableShape.h"

#include "geometry/planar/FloatPoint.h"
#include "util/TextManager.h"

namespace fr {

namespace {
/**
 * NumberFormat.getInstance(locale) with maximumFractionDigits(4), applied to a float value.
 * PORT NOTE: root locale, no grouping separators (values are small coordinates); printf rounding
 * of the binary value instead of DecimalFormat's HALF_EVEN; trailing zeros dropped. Display only.
 */
std::string formatMax4(float value) {
  std::string s = jrt::format("%.4f", static_cast<double>(value));
  size_t dot = s.find('.');
  if (dot != std::string::npos) {
    size_t end = s.find_last_not_of('0');
    if (end == dot) {
      end--;
    }
    s.erase(end + 1);
  }
  return s;
}
}  // namespace

std::string PrintableShape_Circle::toString() {
  TextManager* tm = new TextManager(jrt::className(this), this->locale);

  std::string result = tm->getText("circle") + ": ";
  if (center->x != 0 || center->y != 0) {
    // PORT NOTE: FloatPoint.toString(Locale) -> FloatPoint::toString().
    std::string centerString = tm->getText("center") + " =" + center->toString();
    result += centerString;
  }
  std::string radiusString =
      tm->getText("radius") + " = " + formatMax4(static_cast<float>(radius));
  result += radiusString;
  return result;
}

std::string PrintableShape_Rectangle::toString() {
  TextManager* tm = new TextManager(jrt::className(this), this->locale);

  return tm->getText("rectangle") + ": " + tm->getText("lowerLeft") + " = " +
         lowerLeft->toString() + ", " + tm->getText("upperRight") + " = " +
         upperRight->toString();
}

std::string PrintableShape_Polygon::toString() {
  TextManager* tm = new TextManager(jrt::className(this), this->locale);

  std::string joined;
  for (int32_t i = 0; i < corners->length; i++) {
    if (i > 0) {
      joined += ", ";
    }
    joined += (*corners)[i]->toString();
  }
  return tm->getText("polygon") + ": " + joined;
}

}  // namespace fr
