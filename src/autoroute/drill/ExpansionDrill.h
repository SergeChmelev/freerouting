#pragma once

#include "autoroute/expansion/ExpandableObject.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Layer change expansion object in the maze search algorithm. */
class ExpansionDrill : public virtual ExpandableObject {
 public:
  /** The location, where the drill is checked. */
  Point* location;

  /** The first layer of the drill. */
  int32_t firstLayer;

  /** The last layer of the drill. */
  int32_t lastLayer;

  /** Array of dimension lastLayer - firstLayer + 1. */
  jrt::Array<CompleteExpansionRoom*>* roomArr;

 private:
  jrt::Array<MazeSearchElement*>* mazeSearchElements;

  /** The shape of the drill. */
  TileShape* shape;

 public:
  /** Creates a new instance of Drill. */
  ExpansionDrill(TileShape* shape, Point* location, int32_t firstLayer, int32_t lastLayer);

  /**
   * Looks for the expansion room of this drill on each layer. Creates a
   * CompleteFreeSpaceExpansionRoom if no expansion room is found. Returns false if that was not
   * possible because of an obstacle at location on some layer in the compensated search tree.
   */
  virtual bool calculateExpansionRooms(AutorouteEngine* autorouteEngine);

  TileShape* getShape() override { return this->shape; }

  int32_t getDimension() override { return 2; }

  CompleteExpansionRoom* otherRoom(CompleteExpansionRoom* room) override { return nullptr; }

  int32_t mazeSearchElementCount() override { return this->mazeSearchElements->length; }

  MazeSearchElement* getMazeSearchElement(int32_t index) override {
    return (*this->mazeSearchElements)[index];
  }

  void reset() override;

  int32_t getId() override;

  /** Emits the shape of this drill for an optional diagnostic consumer. */
  virtual void emitDiagnostic(AutorouteDiagnostic_Sink* sink, double intensity);
};

}  // namespace fr
