#pragma once

#include <string>

#include "board/actions/ItemInfoPrinter.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Categorization of clearance violations by item fixability and participant types. */
enum class ClearanceViolation_Category : int32_t {
  PIN_TO_PIN,
  PIN_TO_OUTLINE_OR_KEEPOUT,
  FIXED_ROUTE,
  OTHER_UNFIXABLE,
  POTENTIALLY_FIXABLE
};

inline std::string ClearanceViolation_Category_name(ClearanceViolation_Category e) {
  switch (e) {
    case ClearanceViolation_Category::PIN_TO_PIN:
      return "PIN_TO_PIN";
    case ClearanceViolation_Category::PIN_TO_OUTLINE_OR_KEEPOUT:
      return "PIN_TO_OUTLINE_OR_KEEPOUT";
    case ClearanceViolation_Category::FIXED_ROUTE:
      return "FIXED_ROUTE";
    case ClearanceViolation_Category::OTHER_UNFIXABLE:
      return "OTHER_UNFIXABLE";
    case ClearanceViolation_Category::POTENTIALLY_FIXABLE:
      return "POTENTIALLY_FIXABLE";
  }
  return "";
}

inline std::string ClearanceViolation_Category_toString(ClearanceViolation_Category e) {
  return ClearanceViolation_Category_name(e);
}

inline jrt::Array<ClearanceViolation_Category>* ClearanceViolation_Category_values() {
  return jrt::Array<ClearanceViolation_Category>::of(
      {ClearanceViolation_Category::PIN_TO_PIN,
       ClearanceViolation_Category::PIN_TO_OUTLINE_OR_KEEPOUT,
       ClearanceViolation_Category::FIXED_ROUTE, ClearanceViolation_Category::OTHER_UNFIXABLE,
       ClearanceViolation_Category::POTENTIALLY_FIXABLE});
}

inline ClearanceViolation_Category ClearanceViolation_Category_valueOf(const std::string& name) {
  auto* vals = ClearanceViolation_Category_values();
  for (int32_t i = 0; i < vals->length; ++i) {
    if (ClearanceViolation_Category_name((*vals)[i]) == name) return (*vals)[i];
  }
  throw jrt::IllegalArgumentException("No enum constant ClearanceViolation.Category." + name);
}

/** Information of a clearance violation between 2 items. */
class ClearanceViolation : public virtual jrt::Object, public virtual ItemInfoPrinter_Printable {
 public:
  /** The first item of the clearance violation. */
  Item* firstItem;

  /** The second item of the clearance violation. */
  Item* secondItem;

  /** The shape of the clearance violation. */
  ConvexShape* shape;

  /** The layer of the clearance violation. */
  int32_t layer;

  double expectedClearance;
  double actualClearance;

  /**
   * The two items' drill holes are closer than the board's hole-to-hole minimum (KiCad's
   * hole_to_hole), measured edge to edge, not a copper clearance.
   */
  bool holeToHole;

  /** Creates a new instance of ClearanceViolation. */
  ClearanceViolation(Item* firstItem, Item* secondItem, ConvexShape* shape, int32_t layer,
                     double expectedClearance, double actualClearance)
      : ClearanceViolation(firstItem, secondItem, shape, layer, expectedClearance,
                           actualClearance, false) {}

  /** Creates a new instance of ClearanceViolation including the hole-to-hole flag. */
  ClearanceViolation(Item* firstItem, Item* secondItem, ConvexShape* shape, int32_t layer,
                     double expectedClearance, double actualClearance, bool holeToHole)
      : firstItem(firstItem),
        secondItem(secondItem),
        shape(shape),
        layer(layer),
        expectedClearance(expectedClearance),
        actualClearance(actualClearance),
        holeToHole(holeToHole) {}

  /**
   * Returns true if neither participant in this clearance violation is routable, meaning the
   * violation cannot be resolved by ripping up or rerouting traces or vias.
   */
  virtual bool isUnfixable();

  /** Categorizes this clearance violation based on participant item types and fixability. */
  virtual ClearanceViolation_Category getCategory();

 private:
  static bool isOutlineOrKeepout(Item* item);

 public:
  /**
   * Aggregates the clearance violations of the given items into a single list, sorted by
   * violation severity (largest expected-minus-actual shortfall first). A violation between A
   * and B is reported once on each item (double counted). Side effect: each item's
   * smallestClearance field is populated by Item::clearanceViolations().
   */
  static jrt::List<ClearanceViolation*>* aggregateSortedBySeverity(
      jrt::Collection<Item*>* items);

  /**
   * Returns the smallest clearance among the given items, or Double::MAX_VALUE if none has been
   * computed (reads each item's smallestClearance field).
   */
  static double smallestClearance(jrt::Collection<Item*>* items);

  /** PORT NOTE: java.util.Locale -> const std::string& (GUI/info printing only). */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;
};

}  // namespace fr
