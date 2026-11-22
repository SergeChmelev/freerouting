#pragma once

#include "autoroute/expansion/ExpansionRoom.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Represents a complete expansion room containing target item doors and search tree objects. */
class CompleteExpansionRoom : public virtual ExpansionRoom {
 public:
  /** Returns the list of doors to target items of this room. */
  virtual jrt::Collection<TargetItemExpansionDoor*>* getTargetDoors() = 0;

  /** Returns the object of this complete_expansion_room. */
  virtual SearchTreeObject* getObject() = 0;

  /** Emits the shape of this room for an optional diagnostic consumer. */
  virtual void emitDiagnostic(AutorouteDiagnostic_Sink* sink, double intensity) = 0;
};

}  // namespace fr
