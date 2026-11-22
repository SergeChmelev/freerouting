#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * Narrow callback interface used internally by the DSN parser to delegate board creation and
 * board-lifecycle events. No dependency on the interactive or gui packages. The only production
 * implementation is ReadScopeParameter_MinimalBoardManager.
 */
class BoardParserCallback : public virtual jrt::Object {
 public:
  /** Returns the board that was created, or null if the board has not been constructed yet. */
  virtual fr::RoutingBoard* getRoutingBoard() = 0;

  /**
   * Called by the structure reader once it has parsed the board outline, layers, and rules.
   * Implementations should create and store a new RoutingBoard from the given parameters.
   * outlineClearanceClassName is nullable in Java -> std::optional.
   */
  virtual void createBoard(fr::IntBox* boundingBox, fr::LayerStructure* layerStructure,
                           jrt::Array<fr::PolylineShape*>* outlineShapes,
                           const std::optional<std::string>& outlineClearanceClassName,
                           fr::BoardRules* rules, fr::Communication* boardCommunication) = 0;

  /**
   * Returns the active RoutingJob associated with this parse context, or null when operating in
   * isolation (pure DSN-reader mode without a routing job).
   */
  virtual fr::RoutingJob* getCurrentRoutingJob() = 0;
};

}  // namespace fr::dsn
