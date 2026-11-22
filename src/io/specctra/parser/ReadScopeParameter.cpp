#include "io/specctra/parser/ReadScopeParameter.h"

#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/Unit.h"
#include "io/specctra/parser/ComponentPlacement.h"
#include "io/specctra/parser/NetList.h"
#include "io/specctra/parser/PartLibrary.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/DefaultItemClearanceClasses.h"
#include "rules/NetClass.h"

namespace fr::dsn {

void ReadScopeParameter_MinimalBoardManager::createBoard(
    fr::IntBox* boundingBox, fr::LayerStructure* layerStructure,
    jrt::Array<fr::PolylineShape*>* outlineShapes,
    const std::optional<std::string>& outlineClearanceClassName, fr::BoardRules* rules,
    fr::Communication* boardCommunication) {
  int32_t outlineClearanceNo = 0;
  if (rules != nullptr) {
    if (outlineClearanceClassName.has_value() && rules->clearanceMatrix != nullptr) {
      outlineClearanceNo =
          jrt::Math::max(0, rules->clearanceMatrix->getNo(*outlineClearanceClassName));
    } else {
      outlineClearanceNo = rules->getDefaultNetClass()->defaultItemClearanceClasses->get(
          fr::DefaultItemClearanceClasses_ItemClass::AREA);
    }
  }
  board = new (jrt::kBoard) fr::RoutingBoard(boundingBox, layerStructure, outlineShapes, outlineClearanceNo,
                               rules, boardCommunication);
}

ReadScopeParameter::ReadScopeParameter(IJFlexScanner* scanner, BoardObservers* observers,
                                       IdGenerator* idGenerator)
    : ReadScopeParameter(scanner, observers, idGenerator, true) {}

ReadScopeParameter::ReadScopeParameter(IJFlexScanner* scanner, BoardObservers* observers,
                                       IdGenerator* idGenerator, bool canonicalPlacementOrder)
    : scanner(scanner),
      boardHandling(nullptr),
      netlist(new (jrt::kOp) NetList()),
      observers(observers),
      idGenerator(idGenerator),
      canonicalPlacementOrder(canonicalPlacementOrder),
      planesWithInheritedClearance(new (jrt::kOp) jrt::ArrayList<fr::ConductionArea*>()),
      placementList(new (jrt::kOp) jrt::LinkedList<ComponentPlacement*>()),
      snapAngle(fr::AngleRestriction::FORTYFIVE_DEGREE),
      logicalPartMappings(new (jrt::kOp) jrt::LinkedList<PartLibrary_LogicalPartMapping*>()),
      logicalParts(new (jrt::kOp) jrt::LinkedList<PartLibrary_LogicalPart*>()),
      unit(fr::Unit::MIL) {
  boardHandling = new (jrt::kOp) ReadScopeParameter_MinimalBoardManager();
}

fr::BasicBoard* ReadScopeParameter::getBoard() { return boardHandling->getRoutingBoard(); }

}  // namespace fr::dsn
