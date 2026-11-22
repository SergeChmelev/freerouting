#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

// Private nested classes of ClearanceBaseline, hoisted. Their members are public because Java
// lets the enclosing top-level class access private members of nested classes.

/** (Java private static final class ClearanceBaseline.ShapeGeometry) */
class ClearanceBaseline_ShapeGeometry : public virtual jrt::Object {
 public:
  int32_t layer;
  /** Border line end points sorted by Point::compareXY; nullptr if the shape was null. */
  jrt::Array<Point*>* corners;

  ClearanceBaseline_ShapeGeometry(int32_t layer, TileShape* shape);

  bool matches(ClearanceBaseline_ShapeGeometry* other);
};

/** (Java private static final class ClearanceBaseline.ItemGeometry) */
class ClearanceBaseline_ItemGeometry : public virtual jrt::Object {
 public:
  /** Java getClass().getName(); PORT NOTE: use jrt::className(item). */
  std::string type;
  jrt::Array<int32_t>* nets;
  jrt::Array<ClearanceBaseline_ShapeGeometry*>* shapes;

  explicit ClearanceBaseline_ItemGeometry(Item* item);

  bool matches(ClearanceBaseline_ItemGeometry* other);
};

/** (Java private static final class ClearanceBaseline.Entry) */
class ClearanceBaseline_Entry : public virtual jrt::Object {
 public:
  ClearanceBaseline_ItemGeometry* first;
  ClearanceBaseline_ItemGeometry* second;
  int32_t layer;
  double expected;
  double actual;

  ClearanceBaseline_Entry(ClearanceBaseline_ItemGeometry* first,
                          ClearanceBaseline_ItemGeometry* second, int32_t layer, double expected,
                          double actual)
      : first(first), second(second), layer(layer), expected(expected), actual(actual) {}

  bool matches(ClearanceBaseline_Entry* other);

 private:
  static bool matchesItems(ClearanceBaseline_ItemGeometry* a, ClearanceBaseline_ItemGeometry* b);
};

/**
 * Immutable input-board violations for conservative result validation. An exemption requires
 * the same item geometry, nets, layer, and clearance measurements; item IDs may change on SES
 * import. Changed pre-existing copper is deliberately rechecked rather than exempted.
 */
class ClearanceBaseline final : public virtual jrt::Object {
 private:
  jrt::Array<ClearanceBaseline_Entry*>* entries;

 public:
  explicit ClearanceBaseline(jrt::Collection<ClearanceViolation*>* violations);

  virtual int32_t countNewOrChanged(jrt::Collection<ClearanceViolation*>* violations);

  /** Violations requiring repair, excluding only exact unchanged input exemptions. */
  virtual jrt::List<ClearanceViolation*>* getNewOrChanged(
      jrt::Collection<ClearanceViolation*>* violations);

 private:
  static jrt::Array<ClearanceBaseline_Entry*>* capture(
      jrt::Collection<ClearanceViolation*>* violations);
};

}  // namespace fr
