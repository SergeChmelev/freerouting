// Port of app.freerouting.util.SafeObjectInputStream
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * PORT NOTE: Java serialization is dropped in the port (PORTING.md "Serialization"). This type is
 * an empty placeholder so that references compile; the board deep copies that used Java
 * serialization (RoutingBoard.deepCopy(), BoardSnapshotManager, RoutingBoardUndoFacade) are
 * re-implemented explicitly and must not use it.
 */
class SafeObjectInputStream : public virtual jrt::Object {
 public:
  SafeObjectInputStream() = default;
};

}  // namespace fr
