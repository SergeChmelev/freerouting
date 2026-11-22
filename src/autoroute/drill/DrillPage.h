#pragma once

#include "autoroute/expansion/ExpandableObject.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** A rectangular page of expansion drills used in the maze search algorithm. */
class DrillPage : public virtual ExpandableObject {
 public:
  /** The shape of the page. */
  IntBox* shape;

 private:
  jrt::Array<MazeSearchElement*>* mazeSearchElements;
  RoutingBoard* board;

  /** The list of expansion drills on this page. Null, if not yet calculated. */
  jrt::Collection<ExpansionDrill*>* drills = nullptr;

  /** The number of the net, for which the drills are calculated. */
  int32_t netNumber = -1;

  bool attachSmdCached = false;
  bool centreOfGravityCandidateCached = false;

 public:
  /** Creates a new instance of DrillPage. */
  DrillPage(IntBox* shape, RoutingBoard* board);

 private:
  /** Looks if drillShape contains the center of a drillable Pin on layer. Returns null if no
   * such Pin was found. */
  static Point* calcPinCenterInDrill(TileShape* drillShape, int32_t layer, RoutingBoard* board);

  /** For a net with a plane, offers in a free piece the via location nearest to each SMD pin of
   * the net on this page, so that a plane pad finds a via next to itself. */
  void addPlanePinCandidates(AutorouteEngine* autorouteEngine, TileShape* drillShape,
                             jrt::Collection<Pin*>* planePins, Net* net);

 public:
  /** Returns the drills on this page. If attachSmd, drilling to smd pins is allowed. */
  virtual jrt::Collection<ExpansionDrill*>* getDrills(AutorouteEngine* autorouteEngine,
                                                       bool attachSmd);

  /** Returns the drills on this page. With centreOfGravityCandidate, free pieces containing an
   * SMD pad also offer their centre of gravity as a via candidate. */
  virtual jrt::Collection<ExpansionDrill*>* getDrills(AutorouteEngine* autorouteEngine,
                                                       bool attachSmd,
                                                       bool centreOfGravityCandidate);

  TileShape* getShape() override;

  int32_t getDimension() override { return 2; }

  int32_t mazeSearchElementCount() override { return this->mazeSearchElements->length; }

  MazeSearchElement* getMazeSearchElement(int32_t index) override {
    return (*this->mazeSearchElements)[index];
  }

  /** Resets all drills of this page for autorouting the next connection. */
  void reset() override;

  /** Invalidates the drills of this page so that they are recalculated at the next call of
   * getDrills(). */
  virtual void invalidate() { this->drills = nullptr; }

  /** Emits optional diagnostics for the drills on this page. */
  virtual void emitDiagnostics(AutorouteDiagnostic_Sink* sink, double intensity);

  CompleteExpansionRoom* otherRoom(CompleteExpansionRoom* room) override { return nullptr; }

  int32_t getId() override;
};

}  // namespace fr
