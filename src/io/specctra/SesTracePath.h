#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * Encodes SES trace paths without collapsing nonzero segments or moving real point copper.
 * All "throws IOException" methods throw jrt::IOException.
 */
class SesTracePath final : public virtual jrt::Object {
 public:
  static SesTracePath* from(fr::PolylineTrace* wire, fr::io::CoordinateTransform* transform);

  static SesTracePath* from(fr::PolylineTrace* wire, fr::io::CoordinateTransform* transform,
                            const jrt::BigFraction& scale);

  void write(const std::string& layer, double width, fr::IdentifierType* identifier,
             fr::IndentFileWriter* file);

  void write(const std::string& layer, const jrt::BigFraction& width,
             fr::IdentifierType* identifier, fr::IndentFileWriter* file);

  /**
   * Snaps an endpoint only to the center of a contacted pad/via when it lies inside the pad
   * inradius. Returns null when no snapping applies.
   */
  static fr::FloatPoint* snappedEndpoint(fr::PolylineTrace* wire, bool startSide);

 private:
  explicit SesTracePath(jrt::Array<jrt::BigFraction>* coordinates) : coordinates(coordinates) {}

  static bool collapsesSegment(jrt::Array<jrt::BigFraction>* coordinates,
                               jrt::Array<fr::Point*>* original);

  static bool isExactPoint(jrt::Array<fr::Point*>* corners);

  static jrt::Array<jrt::BigFraction>* encode(jrt::Array<fr::Point*>* corners,
                                              fr::io::CoordinateTransform* transform,
                                              const jrt::BigFraction& scale);

  jrt::Array<jrt::BigFraction>* coordinates;
};

}  // namespace fr::dsn
