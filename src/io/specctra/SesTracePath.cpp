#include "io/specctra/SesTracePath.h"

#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/trace/PolylineTrace.h"
#include "datastructures/BigIntAux.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Shape.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/SesNumbers.h"

namespace fr::dsn {

SesTracePath* SesTracePath::from(fr::PolylineTrace* wire, fr::io::CoordinateTransform* transform) {
  return from(wire, transform, SesNumbers::fromDouble(transform->dsnToBoard(1.0)));
}

SesTracePath* SesTracePath::from(fr::PolylineTrace* wire, fr::io::CoordinateTransform* transform,
                                 const jrt::BigFraction& scale) {
  jrt::Array<fr::Point*>* corners = wire->polyline()->corners();
  if (corners->length < 2) {
    throw jrt::IOException("SES trace has fewer than two source corners");
  }
  bool exactPoint = isExactPoint(corners);
  jrt::Array<fr::Point*>* original = corners->clone();
  jrt::Array<fr::Point*>* snapped = corners->clone();
  for (int32_t index = 0; index < corners->length; index++) {
    // Keep upstream pad/via endpoint snapping, but do not move point copper.
    if (!exactPoint && (index == 0 || index == corners->length - 1)) {
      fr::FloatPoint* endpoint = snappedEndpoint(wire, index == 0);
      if (endpoint != nullptr) {
        (*snapped)[index] = endpoint->round();
      }
    }
  }
  jrt::Array<jrt::BigFraction>* encoded = encode(snapped, transform, scale);
  if (collapsesSegment(encoded, corners)) {
    // Contact snapping must never erase even one nonzero segment inside a longer trace.
    encoded = encode(original, transform, scale);
    if (collapsesSegment(encoded, corners)) {
      throw jrt::IOException(
          "A nonzero SES segment cannot be represented at floating output precision");
    }
  }
  // Point copper intentionally retains two coincident endpoints for KiCad's path importer.
  return new (jrt::kOp) SesTracePath(encoded);
}

bool SesTracePath::collapsesSegment(jrt::Array<jrt::BigFraction>* coordinates,
                                    jrt::Array<fr::Point*>* original) {
  for (int32_t index = 1; index < original->length; index++) {
    if (!(*original)[index - 1]->equals((*original)[index]) &&
        (*coordinates)[2 * index - 2].doubleValue() == (*coordinates)[2 * index].doubleValue() &&
        (*coordinates)[2 * index - 1].doubleValue() ==
            (*coordinates)[2 * index + 1].doubleValue()) {
      return true;
    }
  }
  return false;
}

bool SesTracePath::isExactPoint(jrt::Array<fr::Point*>* corners) {
  for (fr::Point* corner : *corners) {
    if (!(*corners)[0]->equals(corner)) {
      return false;
    }
  }
  return true;
}

jrt::Array<jrt::BigFraction>* SesTracePath::encode(jrt::Array<fr::Point*>* corners,
                                                   fr::io::CoordinateTransform* transform,
                                                   const jrt::BigFraction& scale) {
  jrt::Array<double>* origin = transform->boardToDsn(fr::Point::ZERO()->toFloat());
  jrt::BigFraction baseX = SesNumbers::fromDouble((*origin)[0]);
  jrt::BigFraction baseY = SesNumbers::fromDouble((*origin)[1]);
  auto* encoded = new (jrt::kOp) jrt::Array<jrt::BigFraction>(corners->length * 2);
  for (int32_t index = 0; index < corners->length; index++) {
    fr::BigIntAux_RationalCoordinates* point = (*corners)[index]->exactCoordinates();
    if (point->z().signum() == 0) {
      throw jrt::IOException("Non-finite SES trace coordinate");
    }
    (*encoded)[2 * index] = jrt::BigFraction::of(point->x(), point->z()).divide(scale).add(baseX);
    (*encoded)[2 * index + 1] =
        jrt::BigFraction::of(point->y(), point->z()).divide(scale).add(baseY);
    for (int32_t axis = 0; axis < 2; axis++) {
      double value = (*encoded)[2 * index + axis].doubleValue();
      if (!jrt::Double::isFinite(value) ||
          value <= static_cast<double>(jrt::Long::MIN_VALUE) ||
          value >= static_cast<double>(jrt::Long::MAX_VALUE)) {
        throw jrt::IOException("Non-finite or out-of-range SES trace coordinate");
      }
    }
  }
  return encoded;
}

void SesTracePath::write(const std::string& layer, double width, fr::IdentifierType* identifier,
                         fr::IndentFileWriter* file) {
  if (!jrt::Double::isFinite(width) || width <= 0) {
    throw jrt::IOException("SES trace width must be finite and positive");
  }
  write(layer, SesNumbers::fromDouble(width), identifier, file);
}

void SesTracePath::write(const std::string& layer, const jrt::BigFraction& width,
                         fr::IdentifierType* identifier, fr::IndentFileWriter* file) {
  if (width.signum() <= 0) {
    throw jrt::IOException("SES trace width must be finite and positive");
  }
  file->startScope();
  file->write("path ");
  identifier->write(layer, file);
  file->write(" " + SesNumbers::format(width));
  for (int32_t index = 0; index < coordinates->length; index += 2) {
    file->newLine();
    file->write(SesNumbers::format((*coordinates)[index]) + " " +
                SesNumbers::format((*coordinates)[index + 1]));
  }
  file->endScope();
}

fr::FloatPoint* SesTracePath::snappedEndpoint(fr::PolylineTrace* wire, bool startSide) {
  fr::Point* corner = startSide ? wire->firstCorner() : wire->lastCorner();
  fr::FloatPoint* cornerFloat = corner->toFloat();
  jrt::DisposeOnExit<jrt::Set<fr::Item*>> contactsOwner(startSide ? wire->getStartContacts() : wire->getEndContacts());
  jrt::Set<fr::Item*>* contacts = contactsOwner.get();
  int32_t layer = wire->getLayer();
  for (fr::Item* contact : *contacts) {
    auto* drill = jrt::fast_cast<fr::DrillItem>(contact);
    if (drill == nullptr) {
      continue;
    }
    if (layer < drill->firstLayer() || layer > drill->lastLayer()) {
      continue;
    }
    fr::Shape* padShape = drill->getShape(layer - drill->firstLayer());
    if (padShape == nullptr) {
      continue;
    }
    fr::FloatPoint* center = drill->getCenter()->toFloat();
    double centerDistance = cornerFloat->distance(center);
    if (centerDistance <= 0.5) {
      // Already at the center (within rounding): nothing to fix.
      return nullptr;
    }
    if (centerDistance <= padShape->borderDistance(center)) {
      return center;
    }
  }
  return nullptr;
}

}  // namespace fr::dsn
