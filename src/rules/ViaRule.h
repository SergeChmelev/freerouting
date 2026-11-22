// Port of app.freerouting.rules.ViaRule
#pragma once

#include "board/actions/ItemInfoPrinter.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Contains an array of vias used for routing. Vias at the beginning of the array are preferred to
 * later vias.
 */
class ViaRule : public virtual ItemInfoPrinter_Printable {
 public:
  /** Empty via rule. Must not be changed. */
  static ViaRule* EMPTY();

  explicit ViaRule(const std::string& name);

  void appendVia(ViaInfo* via);

  /** Removes via from the rule. Returns false, if via was not found in the rule. */
  bool removeVia(ViaInfo* via);

  int32_t viaCount();

  ViaInfo* getVia(int32_t index);

  std::string toString() override { return this->name; }

  /** Returns true, if this rule contains viaInfo (identity comparison). */
  bool contains(ViaInfo* viaInfo);

  /** Returns true, if this rule contains a via with the input padstack (identity). */
  bool containsPadstack(Padstack* padstack);

  /** Searches a via in this rule with first layer = fromLayer and last layer = toLayer. */
  ViaInfo* getLayerRange(int32_t fromLayer, int32_t toLayer);

  /** Swaps the locations of first and second in the rule. Returns false, if not found. */
  bool swap(ViaInfo* first, ViaInfo* second);

  /** PORT NOTE: java.util.Locale -> std::string language tag. */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  const std::string name;

 private:
  friend class BoardRules;  // PORT NOTE: BoardRules::deepCopy fills the copied list

  /** Java: LinkedList. */
  jrt::List<ViaInfo*>* list;
};

}  // namespace fr
