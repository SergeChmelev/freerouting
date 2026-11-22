#include "autoroute/drill/ExpansionDrill.h"

#include "autoroute/AutorouteDiagnostic.h"
#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/CompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/IncompleteFreeSpaceExpansionRoom.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "autoroute/maze/MazeSearchElement.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/TileShape.h"

namespace fr {

ExpansionDrill::ExpansionDrill(TileShape* shape, Point* location, int32_t firstLayer, int32_t lastLayer) {
  this->shape = shape;
  this->location = location;
  this->firstLayer = firstLayer;
  this->lastLayer = lastLayer;
  int32_t layerCount = lastLayer - firstLayer + 1;
  roomArr = new (jrt::kOp) jrt::Array<CompleteExpansionRoom*>(layerCount);
  mazeSearchElements = new (jrt::kOp) jrt::Array<MazeSearchElement*>(layerCount);
  for (int32_t i = 0; i < mazeSearchElements->length; i++) {
    (*mazeSearchElements)[i] = new (jrt::kOp) MazeSearchElement();
  }
}

bool ExpansionDrill::calculateExpansionRooms(AutorouteEngine* autorouteEngine) {
  TileShape* searchShape = TileShape::getInstance(location);
  jrt::Collection<SearchTreeObject*>* overlaps =
      autorouteEngine->autorouteSearchTree->overlappingObjects(searchShape, -1);
  for (int32_t i = this->firstLayer; i <= this->lastLayer; i++) {
    CompleteExpansionRoom* foundRoom = nullptr;
    jrt::Iterator<SearchTreeObject*>* it = overlaps->iterator();
    while (it->hasNext()) {
      SearchTreeObject* currentObject = it->next();
      CompleteExpansionRoom* currentRoom = jrt::fast_cast<CompleteExpansionRoom>(currentObject);
      if (currentRoom == nullptr) {
        it->remove();
        continue;
      }
      if (currentRoom->getLayer() == i) {
        foundRoom = currentRoom;
        it->remove();
        break;
      }
    }
    if (foundRoom == nullptr) {
      // create a new expansion room on this layer
      IncompleteFreeSpaceExpansionRoom* newIncompleteRoom =
          new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(nullptr, i, searchShape);
      jrt::Collection<CompleteFreeSpaceExpansionRoom*>* newRooms =
          autorouteEngine->completeExpansionRoom(newIncompleteRoom);
      if (newRooms->size() != 1) {
        // the size may be 0 because of an obstacle in the compensated tree at this.location
        return false;
      }
      jrt::Iterator<CompleteFreeSpaceExpansionRoom*>* it2 = newRooms->iterator();
      if (it2->hasNext()) {
        foundRoom = it2->next();
      }
    }
    (*this->roomArr)[i - firstLayer] = foundRoom;
  }
  return true;
}

void ExpansionDrill::reset() {
  for (MazeSearchElement* currentInfo : *mazeSearchElements) {
    currentInfo->reset();
  }
}

int32_t ExpansionDrill::getId() {
  // Stable hash of location and layers
  return 31 * (31 * location->getId() + firstLayer) + lastLayer;
}

void ExpansionDrill::emitDiagnostic(AutorouteDiagnostic_Sink* sink, double intensity) {
  if (sink != nullptr && intensity > 0) {
    sink->accept(new (jrt::kOp) AutorouteDiagnostic(AutorouteDiagnostic_Kind::EXPANSION_DRILL, this->shape, -1, intensity));
  }
}

}  // namespace fr
