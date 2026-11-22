// Port of app.freerouting.board.model.items.PadPlaneConnection.
#include "board/model/items/PadPlaneConnection.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// Spoke (record)
// ---------------------------------------------------------------------------------------------

bool PadPlaneConnection_Spoke::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<PadPlaneConnection_Spoke>(o);
  if (other == nullptr) return false;
  return jrt::Double::compare(startX_, other->startX_) == 0 &&
         jrt::Double::compare(startY_, other->startY_) == 0 &&
         jrt::Double::compare(endX_, other->endX_) == 0 &&
         jrt::Double::compare(endY_, other->endY_) == 0 &&
         jrt::Double::compare(halfWidth_, other->halfWidth_) == 0;
}

int32_t PadPlaneConnection_Spoke::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::Double::hashCode(startX_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(startY_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(endX_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(endY_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(halfWidth_));
  return h;
}

std::string PadPlaneConnection_Spoke::toString() {
  return "Spoke[startX=" + jrt::str(startX_) + ", startY=" + jrt::str(startY_) +
         ", endX=" + jrt::str(endX_) + ", endY=" + jrt::str(endY_) +
         ", halfWidth=" + jrt::str(halfWidth_) + "]";
}

// ---------------------------------------------------------------------------------------------
// PadPlaneConnection (record)
// ---------------------------------------------------------------------------------------------

PadPlaneConnection_Mode PadPlaneConnection::resolve(PadPlaneConnection_Mode planeMode) {
  PadPlaneConnection_Mode result =
      this->mode_ != PadPlaneConnection_Mode::INHERITED ? this->mode_ : planeMode;
  if (result == PadPlaneConnection_Mode::THT_THERMAL) {
    result = this->throughHole_ ? PadPlaneConnection_Mode::THERMAL : PadPlaneConnection_Mode::SOLID;
  }
  return result;
}

jrt::Array<PadPlaneConnection_Spoke*>* PadPlaneConnection::spokes(double planeGap,
                                                                  double planeSpokeWidth,
                                                                  double minWidth,
                                                                  double endSlack) {
  double width = this->spokeWidth_ > 0
                     ? jrt::Math::max(static_cast<double>(this->spokeWidth_), minWidth)
                     : planeSpokeWidth;
  if (width <= 0) {
    width = minWidth;
  }
  width = jrt::Math::min(width, 2 * jrt::Math::min(this->halfWidth_, this->halfHeight_));
  if (width < minWidth || width <= 0) {
    return new (jrt::kOp) jrt::Array<PadPlaneConnection_Spoke*>(0);
  }
  double reach = gapOn(planeGap) + minWidth / 2.0 + endSlack;
  auto* result = new (jrt::kOp) jrt::Array<PadPlaneConnection_Spoke*>(4);
  for (int32_t i = 0; i < 4; i++) {
    double inPad = jrt::Math::toRadians(this->spokeAngleDeg_ + 90.0 * i);
    double cos = jrt::Math::cos(inPad);
    double sin = jrt::Math::sin(inPad);
    double start;
    double end;
    if (this->circular_) {
      start = this->halfWidth_;
      end = this->halfWidth_ + reach;
    } else {
      start = toBoxEdge(cos, sin, this->halfWidth_, this->halfHeight_);
      end = toBoxEdge(cos, sin, this->halfWidth_ + reach, this->halfHeight_ + reach);
    }
    double onBoard = jrt::Math::toRadians(this->orientationDeg_ + this->spokeAngleDeg_ + 90.0 * i);
    double dx = jrt::Math::cos(onBoard);
    double dy = jrt::Math::sin(onBoard);
    (*result)[i] = new (jrt::kBoard) PadPlaneConnection_Spoke(
        this->centerX_ + dx * start, this->centerY_ + dy * start, this->centerX_ + dx * end,
        this->centerY_ + dy * end, width / 2.0);
  }
  return result;
}

double PadPlaneConnection::toBoxEdge(double cos, double sin, double halfWidth, double halfHeight) {
  double absCos = jrt::Math::abs(cos);
  double absSin = jrt::Math::abs(sin);
  if (absCos < 1e-9) {
    return halfHeight;
  }
  if (absSin < 1e-9) {
    return halfWidth;
  }
  return jrt::Math::min(halfWidth / absCos, halfHeight / absSin);
}

bool PadPlaneConnection::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<PadPlaneConnection>(o);
  if (other == nullptr) return false;
  return mode_ == other->mode_ && throughHole_ == other->throughHole_ &&
         jrt::Double::compare(centerX_, other->centerX_) == 0 &&
         jrt::Double::compare(centerY_, other->centerY_) == 0 &&
         jrt::Double::compare(halfWidth_, other->halfWidth_) == 0 &&
         jrt::Double::compare(halfHeight_, other->halfHeight_) == 0 &&
         jrt::Double::compare(orientationDeg_, other->orientationDeg_) == 0 &&
         jrt::Double::compare(spokeAngleDeg_, other->spokeAngleDeg_) == 0 &&
         circular_ == other->circular_ && thermalGap_ == other->thermalGap_ &&
         spokeWidth_ == other->spokeWidth_;
}

int32_t PadPlaneConnection::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, static_cast<int32_t>(mode_));  // PORT NOTE: enums hash by ordinal
  h = jrt::hashCombine(h, jrt::Boolean::hashCode(throughHole_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(centerX_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(centerY_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(halfWidth_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(halfHeight_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(orientationDeg_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(spokeAngleDeg_));
  h = jrt::hashCombine(h, jrt::Boolean::hashCode(circular_));
  h = jrt::hashCombine(h, thermalGap_);
  h = jrt::hashCombine(h, spokeWidth_);
  return h;
}

std::string PadPlaneConnection::toString() {
  return "PadPlaneConnection[mode=" + PadPlaneConnection_Mode_toString(mode_) +
         ", throughHole=" + jrt::str(throughHole_) + ", centerX=" + jrt::str(centerX_) +
         ", centerY=" + jrt::str(centerY_) + ", halfWidth=" + jrt::str(halfWidth_) +
         ", halfHeight=" + jrt::str(halfHeight_) + ", orientationDeg=" + jrt::str(orientationDeg_) +
         ", spokeAngleDeg=" + jrt::str(spokeAngleDeg_) + ", circular=" + jrt::str(circular_) +
         ", thermalGap=" + jrt::str(thermalGap_) + ", spokeWidth=" + jrt::str(spokeWidth_) + "]";
}

}  // namespace fr
