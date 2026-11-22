#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Temporary data stored in board Items used in the autoroute algorithm. */
class ItemAutorouteInfo : public virtual jrt::Object {
 private:
  Item* item;

  /** Defines, if this item belongs to the start or destination set of the maze search. */
  bool startInfo = false;

  bool destinationInfo = false;

  Connection* precalculatedConnection = nullptr;

  /** ExpansionRoom for pushing or ripping this object for each tree shape. */
  jrt::Array<ObstacleExpansionRoom*>* expansionRoomArr = nullptr;

 public:
  /** Constructs an ItemAutorouteInfo for the given item. */
  explicit ItemAutorouteInfo(Item* item) : item(item) {}

  /** Looks, if the corresponding item belongs to the start or destination set. */
  virtual bool isStartInfo() { return startInfo; }

  /** Sets, if the corresponding item belongs to the start or destination set. */
  virtual void setStartInfo(bool value) { startInfo = value; }

  /** Returns true if this item belongs to the destination set of the maze search algorithm. */
  virtual bool isDestinationInfo() { return destinationInfo; }

  /** Sets whether this item belongs to the destination set of the maze search algorithm. */
  virtual void setDestinationInfo(bool value) { destinationInfo = value; }

  /** Returns the precalculated connection of this item or null, if it is not yet precalculated. */
  virtual Connection* getPrecalculatedConnection() { return this->precalculatedConnection; }

  /** Sets the precalculated connection of this item. */
  virtual void setPrecalculatedConnection(Connection* connection) {
    this->precalculatedConnection = connection;
  }

  /** Gets the ExpansionRoom of index index. Creates it, if it is not yet existing. */
  virtual ObstacleExpansionRoom* getExpansionRoom(int32_t index, ShapeSearchTree* autorouteTree);

  /** Resets the expansion rooms for autorouting the next connection. */
  virtual void resetDoors();

  /** Emits optional diagnostics for the expansion rooms of this info. */
  virtual void emitDiagnostics(AutorouteDiagnostic_Sink* sink, double intensity);
};

}  // namespace fr
