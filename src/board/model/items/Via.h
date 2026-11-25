// Port of app.freerouting.board.model.items.Via.
#pragma once

#include "board/model/items/DrillItem.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Class describing the functionality of an electrical Item on the board, which may have a shape on
 * several layer, whose geometry is described by a padstack.
 */
class Via : public DrillItem {
 public:
  /** True, if coppersharing of this via with smd pins of the same net is allowed. */
  bool attachAllowed;  // final

  /**
   * True if this is an escape via inserted by the escalation fanout phase to provide a layer
   * transition point from an SMD pin that could not be escaped on its primary layer.
   */
  bool isEscapeVia = false;

  /** The SMD layer on which this escape via connects to an SMD pin; -1 if not an escape via. */
  int32_t escapeViaSmdLayer = -1;

 private:
  // PORT NOTE (board deep copy): copy(id) shares this padstack reference with the original (as
  // Java's copy does); serialization would point it into the copied board library.
  Padstack* padstack;
  jrt::Array<Shape*>* precalculatedShapes = nullptr;  // transient

  /** Temporary data used in the autoroute algorithm. */
  ExpansionDrill* autorouteDrillInfo = nullptr;  // transient

 public:
  /** Creates a new instance of Via with the input parameters. */
  Via(Padstack* padstack, Point* center, jrt::Array<int32_t>* netNumbers,
      int32_t clearanceClassIndex, int32_t id, int32_t groupId, FixedState fixedState,
      bool attachAllowed, BasicBoard* board);

  using Item::isObstacle;

  Item* copy(int32_t id) override;

  jrt::Collection<ClearanceViolation*>* clearanceViolations() override;

 private:
  /**
   * The drilled items, of any net, whose holes are closer to this via's drill than the board's
   * hole-to-hole minimum (KiCad's hole_to_hole check).
   */
  void addHoleToHoleViolations(jrt::Collection<ClearanceViolation*>* violations);

 public:
  Shape* getShape(int32_t index) override;

  Padstack* getPadstack() override { return padstack; }

  virtual void setPadstack(Padstack* padstack) { this->padstack = padstack; }

  bool isRoutable() override;

  bool isObstacle(Item* other) override;

 private:
  /**
   * Whether the board rules let this via share copper with pin, an SMD pad of its own net: always
   * under via_in_pad, and under via_touching_pad while its drill hole stays outside the pad.
   * Called from search tree queries, so it must not query the tree itself.
   */
  bool ownPadAllowedByRules(Pin* pin);

  /** Whether the drill hole of this via overlaps the copper of pin on a common layer. */
  bool holeInPad(Pin* pin);

 public:
  /** Checks, if the Via has contacts on at most 1 layer. */
  bool isTail() override;

  void changePlacementSide(IntPoint* pole) override;

  /** GetAutorouteDrillInfo. */
  virtual ExpansionDrill* getAutorouteDrillInfo(ShapeSearchTree* autorouteTree);

  void clearDerivedData() override;

  void clearAutorouteInfo() override;
  bool holdsAutorouteInfo() override {
    return DrillItem::holdsAutorouteInfo() || autorouteDrillInfo != nullptr;
  }

  bool isSelectedByFilter(ItemSelectionFilter* filter) override;

  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  std::string getHoverInfo(const std::string& locale) override;
};

}  // namespace fr
