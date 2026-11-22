// Port of app.freerouting.core.library.Package
#include "core/library/Package.h"

#include "core/library/Packages.h"
#include "core/library/Padstack.h"
#include "core/library/Padstacks.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "util/TextManager.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// Package_Pin
// ---------------------------------------------------------------------------------------------

Package_Pin::Package_Pin(const std::string& name, int32_t padstackId, Vector* relativeLocation,
                         double rotationInDegree)
    : Package_Pin(name, padstackId, relativeLocation->toFloat(), rotationInDegree) {}

Package_Pin::Package_Pin(const std::string& name, int32_t padstackId,
                         FloatPoint* relativeLocation, double rotationInDegree)
    : name(name),
      padstackId(padstackId),
      relativeLocation(relativeLocation->round()->differenceBy(static_cast<Point*>(Point::ZERO()))),
      rotationInDegree(rotationInDegree),
      exactRelativeLocation(relativeLocation) {}

FloatPoint* Package_Pin::getExactRelativeLocation() {
  if (this->exactRelativeLocation != nullptr) {
    return this->exactRelativeLocation;
  }
  return this->relativeLocation != nullptr ? this->relativeLocation->toFloat()
                                           : FloatPoint::ZERO();
}

// ---------------------------------------------------------------------------------------------
// Package
// ---------------------------------------------------------------------------------------------

int32_t Package::compareTo(Package* other) {
  if (other == nullptr) {
    throw jrt::NullPointerException("other package cannot be null");
  }
  int32_t res = jrt::compareStringsIgnoreCase(this->name, other->name);
  if (res != 0) {
    return res;
  }
  res = jrt::compareStrings(this->name, other->name);
  if (res != 0) {
    return res;
  }
  return jrt::Integer::compare(this->id, other->id);
}

Package_Pin* Package::getPin(int32_t pinIndex) {
  if (pinIndex < 0 || pinIndex >= pins->length) {
    FRLogger::warn("Package.getPin: pinIndex out of range");
    return nullptr;
  }
  return (*pins)[pinIndex];
}

int32_t Package::getPinIndex(const std::string& name) {
  for (int32_t i = 0; i < pins->length; i++) {
    if ((*pins)[i]->name == name) {
      return i;
    }
  }
  return -1;
}

void Package::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new TextManager("app.freerouting.core.library.Package", locale);

  printer->appendBold(tm->getText("package") + " ");
  printer->appendBold(this->name);
  for (int32_t i = 0; i < this->pins->length; i++) {
    Package_Pin* currentPin = (*this->pins)[i];
    printer->newline();
    printer->indent();
    printer->append(tm->getText("pin") + " ");
    printer->append(currentPin->name);
    printer->append(", " + tm->getText("padstack") + " ");
    Padstack* currentPadstack = this->packageList->padstackList->get(currentPin->padstackId);
    printer->append(currentPadstack->name, tm->getText("padstack_info"), currentPadstack);
    printer->append(" " + tm->getText("at") + " ");
    printer->append(currentPin->getExactRelativeLocation());
    printer->append(", " + tm->getText("rotation") + " ");
    printer->appendWithoutTransforming(currentPin->rotationInDegree);
  }
  printer->newline();
}

}  // namespace fr
