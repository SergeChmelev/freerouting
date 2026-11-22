// Port of app.freerouting.board.model.items.Connectable.
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Functionality required for items, which can be electrical connected to other items.
 *
 * PORT NOTE: in Java, Item already implements containsNet / sharesNetNo / getAllContacts /
 * getConnectedSet, and the Connectable subclasses (Trace, DrillItem, ConductionArea) inherit those
 * implementations. In C++ Item does not derive from Connectable, so these subclasses declare
 * small forwarding overrides (`return Item::containsNet(n);`) to provide the final overriders.
 */
class Connectable : public virtual jrt::Object {
 public:
  /** Returns true if this item belongs to the net with number netNumber. */
  virtual bool containsNet(int32_t netNumber) = 0;

  /** Returns true if the net number array of this and netNumbers have a common number. */
  virtual bool sharesNetNo(jrt::Array<int32_t>* netNumbers) = 0;

  /** Returns a list of all connectable items overlapping and sharing a net with this item. */
  virtual jrt::Set<Item*>* getAllContacts() = 0;

  /**
   * Returns a list of all connectable items overlapping with this item on the input layer and
   * sharing a net with this item.
   */
  virtual jrt::Set<Item*>* getAllContacts(int32_t layer) = 0;

  /**
   * Returns the list of all contacts of a connectable item located at defined connection points.
   * Connection points of traces are there endpoints, connection points of drill_items there center
   * points, and connection points of conduction areas are points on there border.
   */
  virtual jrt::Set<Item*>* getNormalContacts() = 0;

  /**
   * Returns all connectable items of the net with number netNumber, which can be reached
   * recursively from this item via normal contacts. if netNumber <= 0, the net number is ignored.
   */
  virtual jrt::Set<Item*>* getConnectedSet(int32_t netNumber) = 0;

  /**
   * Returns for each convex shape of a connectable item the subshape of points, where traces can be
   * connected to that item.
   */
  virtual TileShape* getTraceConnectionShape(ShapeSearchTree* tree, int32_t index) = 0;
};

}  // namespace fr
