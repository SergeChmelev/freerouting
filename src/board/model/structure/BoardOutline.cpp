// Port of app.freerouting.board.model.structure.BoardOutline
#include "board/model/structure/BoardOutline.h"

#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/LayerStructure.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "datastructures/ShapeTree.h"
#include "datastructures/UndoableObjects.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/PolylineArea.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"
#include "util/TextManager.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// BoardOutline_EdgeWindow (record)
// ---------------------------------------------------------------------------------------------

bool BoardOutline_EdgeWindow::equals(jrt::Object* o) {
  if (this == o) {
    return true;
  }
  auto* other = jrt::fast_cast<BoardOutline_EdgeWindow>(o);
  if (other == nullptr) {
    return false;
  }
  return jrt::equalsOf<IntBox*>(box_, other->box_) && layer_ == other->layer_ &&
         nets_ == other->nets_;
}

int32_t BoardOutline_EdgeWindow::hashCode() {
  int32_t h = jrt::hashOf<IntBox*>(box_);
  h = jrt::hashCombine(h, layer_);
  return jrt::hashCombine(h, jrt::System::identityHashCode(nets_));
}

std::string BoardOutline_EdgeWindow::toString() {
  std::string netsStr =
      nets_ == nullptr ? std::string("null")
                       : "[I@" + jrt::Integer::toHexString(jrt::System::identityHashCode(nets_));
  return "EdgeWindow[box=" + jrt::str(box_) + ", layer=" + jrt::str(layer_) +
         ", nets=" + netsStr + "]";
}

// ---------------------------------------------------------------------------------------------
// BoardOutline
// ---------------------------------------------------------------------------------------------

BoardOutline::BoardOutline(jrt::Array<PolylineShape*>* shapes, int32_t clearanceClassIndex,
                           int32_t id, BasicBoard* board)
    : Item(new jrt::Array<int32_t>(0), clearanceClassIndex, id, 0, FixedState::SYSTEM_FIXED,
           board) {
  this->shapes = shapes;
}

int32_t BoardOutline::tileShapeCount() {
  int32_t result;
  if (this->keepoutOutsideOutline) {
    jrt::Array<TileShape*>* tileShapes = this->getKeepoutTiles();
    if (tileShapes == nullptr) {
      // an error occurred while dividing the area
      result = 0;
    } else {
      result = tileShapes->length * this->board->layerStructure->layers->length;
    }
  } else {
    result = this->lineSliceCount() * this->board->layerStructure->layers->length;
  }
  return result;
}

int32_t BoardOutline::shapeLayer(int32_t index) {
  int32_t shapeCount = this->tileShapeCount();
  int32_t result;
  if (shapeCount > 0) {
    result = index * this->board->layerStructure->layers->length / shapeCount;
  } else {
    result = 0;
  }
  if (result < 0 || result >= this->board->layerStructure->layers->length) {
    FRLogger::warn("BoardOutline.shapeLayer: index out of range");
  }
  return result;
}

jrt::Set<int32_t>* BoardOutline::getEdgePinNets() {
  int32_t currentItemCount = -1;
  if (this->board != nullptr) {
    int64_t version = this->board->itemList->getVersion();
    if (version != this->countedVersion || this->countedList != this->board->itemList) {
      this->countedPins = 0;
      jrt::Iterator<UndoableObjects_UndoableObjectNode*>* iterator =
          this->board->itemList->startReadObject();
      for (;;) {
        auto* item = jrt::fast_cast<Item>(this->board->itemList->readObject(iterator));
        if (item == nullptr) {
          break;
        }
        if (jrt::fast_cast<Pin>(item) != nullptr) {
          this->countedPins++;
        }
      }
      this->countedVersion = version;
      this->countedList = this->board->itemList;
    }
    currentItemCount = this->countedPins;
  }
  if (this->edgePinNets == nullptr || this->cachedItemCount != currentItemCount) {
    this->cachedItemCount = currentItemCount;
    jrt::Set<int32_t>* set = new jrt::HashSet<int32_t>();
    if (this->board != nullptr) {
      for (Pin* pin : *this->board->getPins()) {
        if (this->isEdgePin(pin)) {
          for (int32_t net : *pin->netNumbers) {
            set->add(net);
          }
        }
      }
    }
    this->edgePinNets = set;
  }
  return this->edgePinNets;
}

double BoardOutline::minimumPinGap() {
  double result = jrt::Double::POSITIVE_INFINITY;
  if (this->board == nullptr) {
    return result;
  }
  for (Pin* pin : *this->board->getPins()) {
    for (int32_t layer = pin->firstLayer(); layer <= pin->lastLayer(); layer++) {
      Shape* shape = pin->getShape(layer - pin->firstLayer());
      if (shape == nullptr) {
        continue;
      }
      result = jrt::Math::min(result, cornerGap(shape));
      if (result <= 0) {
        return 0;
      }
    }
  }
  return result;
}

double BoardOutline::cornerGap(Shape* padShape) {
  double result = jrt::Double::POSITIVE_INFINITY;
  if (padShape->isEmpty() || !padShape->isBounded()) {
    return result;
  }
  if (auto* polylineShape = jrt::fast_cast<PolylineShape>(padShape)) {
    for (int32_t c = 0; c < polylineShape->borderLineCount(); c++) {
      result = jrt::Math::min(result, distanceToOutline(polylineShape->cornerApprox(c)));
    }
  } else {
    IntBox* box = padShape->boundingBox();
    for (int32_t c = 0; c < 4; c++) {
      result = jrt::Math::min(result, distanceToOutline(box->corner(c)->toFloat()));
    }
  }
  return result;
}

double BoardOutline::distanceToOutline(FloatPoint* point) {
  if (!this->contains(point)) {
    return 0;
  }
  double result = jrt::Double::POSITIVE_INFINITY;
  for (PolylineShape* outlineShape : *this->shapes) {
    // PolygonShape.borderDistance is not implemented, so measure to the corner-to-corner
    // segments directly.
    int32_t cornerCount = outlineShape->borderLineCount();
    for (int32_t i = 0; i < cornerCount; i++) {
      FloatPoint* from = outlineShape->corner(i)->toFloat();
      FloatPoint* to = outlineShape->corner((i + 1) % cornerCount)->toFloat();
      result = jrt::Math::min(result, (new FloatLine(from, to))->segmentDistance(point));
    }
  }
  return result;
}

void BoardOutline::invalidateEdgePinNets() {
  this->edgePinNets = nullptr;
  this->cachedItemCount = -1;
  this->countedVersion = -1;
  this->countedList = nullptr;
}

bool BoardOutline::isTraceObstacle(int32_t netNumber) {
  if ((edgePinNetsCrossOutline || keepoutOutsideOutline) && netNumber > 0 &&
      getEdgePinNets()->contains(netNumber)) {
    return false;
  }
  return true;
}

bool BoardOutline::isTraceObstacle(int32_t netNumber, int32_t shapeIndex) {
  if (!isTraceObstacle(netNumber)) {
    return false;
  }
  return !exemptsPiece(shapeIndex, netNumber);
}

bool BoardOutline::exemptsPiece(int32_t shapeIndex, int32_t netNumber) {
  if (netNumber <= 0 || this->keepoutOutsideOutline || this->board == nullptr) {
    return false;
  }
  int32_t perLayer = this->lineSliceCount();
  if (perLayer <= 0 || shapeIndex < 0 || this->pieceWindows == nullptr) {
    return false;
  }
  int32_t layer = shapeIndex / perLayer;
  jrt::Array<int32_t>* windows = (*this->pieceWindows)[shapeIndex % perLayer];
  if (windows == nullptr) {
    return false;
  }
  for (int32_t w : *windows) {
    BoardOutline_EdgeWindow* window = (*this->edgeWindows)[w];
    if (window->layer() != layer) {
      continue;
    }
    for (int32_t net : *window->nets()) {
      if (net == netNumber) {
        return true;
      }
    }
  }
  return false;
}

bool BoardOutline::exemptsTrace(int32_t shapeIndex, jrt::Array<int32_t>* netNumbers) {
  if (netNumbers == nullptr || netNumbers->length == 0) {
    return false;
  }
  for (int32_t net : *netNumbers) {
    if (isTraceObstacle(net, shapeIndex)) {
      return false;
    }
  }
  return true;
}

bool BoardOutline::blocksNets(jrt::Array<int32_t>* netNumbers) {
  if (netNumbers == nullptr || netNumbers->length == 0) {
    return true;
  }
  for (int32_t netNo : *netNumbers) {
    if (isTraceObstacle(netNo)) {
      return true;
    }
  }
  return false;
}

bool BoardOutline::blocksTraceAt(TileShape* traceShape, int32_t layer,
                                 jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex) {
  if (!blocksNets(netNumbers)) {
    return false;
  }
  if (this->board == nullptr || this->lineSliceCount() <= 0 || this->pieceWindows == nullptr) {
    return true;
  }
  jrt::Collection<ShapeTree_TreeEntry*>* entries = new jrt::ArrayList<ShapeTree_TreeEntry*>();
  this->board->searchTreeManager->getDefaultTree()->overlappingTreeEntriesWithClearance(
      static_cast<ConvexShape*>(traceShape), layer, new jrt::Array<int32_t>(0),
      clearanceClassIndex, entries);
  bool reached = false;
  for (ShapeTree_TreeEntry* entry : *entries) {
    if (entry->object != static_cast<ShapeTree_Storable*>(this)) {
      continue;
    }
    reached = true;
    if (!exemptsTrace(entry->shapeIndexInObject, netNumbers)) {
      return true;
    }
  }
  return !reached;
}

bool BoardOutline::isObstacle(Item* other) {
  if (jrt::fast_cast<BoardOutline>(other) != nullptr ||
      jrt::fast_cast<ObstacleArea>(other) != nullptr) {
    return false;
  }
  if (auto* otherTrace = jrt::fast_cast<Trace>(other)) {
    if (!blocksNets(otherTrace->netNumbers)) {
      return false;
    }
  }
  return true;
}

IntBox* BoardOutline::boundingBox() {
  IntBox* result = IntBox::EMPTY();
  for (PolylineShape* currentShape : *this->shapes) {
    result = result->union_(currentShape->boundingBox());
  }
  return result;
}

int32_t BoardOutline::firstLayer() { return 0; }

int32_t BoardOutline::lastLayer() { return this->board->layerStructure->layers->length - 1; }

bool BoardOutline::isOnLayer(int32_t layer) { return true; }

void BoardOutline::translateBy(Vector* vector) {
  auto* newShapes = new jrt::Array<PolylineShape*>(this->shapes->length);
  for (int32_t i = 0; i < this->shapes->length; i++) {
    (*newShapes)[i] = (*this->shapes)[i]->translateBy(vector);
  }
  this->shapes = newShapes;
  if (keepoutArea != nullptr) {
    keepoutArea = keepoutArea->translateBy(vector);
  }
  keepoutLines = nullptr;
  keepoutTiles = nullptr;
  lineSlices_ = nullptr;
  clearLineCuts();
  sliceTolerance_ = 0;
  invalidateEdgePinNets();
}

void BoardOutline::turn90Degree(int32_t factor, IntPoint* pole) {
  auto* newShapes = new jrt::Array<PolylineShape*>(this->shapes->length);
  for (int32_t i = 0; i < this->shapes->length; i++) {
    (*newShapes)[i] = (*this->shapes)[i]->turn90Degree(factor, pole);
  }
  this->shapes = newShapes;
  if (keepoutArea != nullptr) {
    keepoutArea = keepoutArea->turn90Degree(factor, pole);
  }
  keepoutLines = nullptr;
  keepoutTiles = nullptr;
  lineSlices_ = nullptr;
  clearLineCuts();
  sliceTolerance_ = 0;
  invalidateEdgePinNets();
}

void BoardOutline::rotateApprox(double angleInDegree, FloatPoint* pole) {
  double angle = jrt::Math::toRadians(angleInDegree);
  auto* newShapes = new jrt::Array<PolylineShape*>(this->shapes->length);
  for (int32_t i = 0; i < this->shapes->length; i++) {
    (*newShapes)[i] = (*this->shapes)[i]->rotateApprox(angle, pole);
  }
  this->shapes = newShapes;
  if (keepoutArea != nullptr) {
    keepoutArea = keepoutArea->rotateApprox(angle, pole);
  }
  keepoutLines = nullptr;
  keepoutTiles = nullptr;
  lineSlices_ = nullptr;
  clearLineCuts();
  sliceTolerance_ = 0;
  invalidateEdgePinNets();
}

void BoardOutline::changePlacementSide(IntPoint* pole) {
  auto* newShapes = new jrt::Array<PolylineShape*>(this->shapes->length);
  for (int32_t i = 0; i < this->shapes->length; i++) {
    (*newShapes)[i] = (*this->shapes)[i]->mirrorVertical(pole);
  }
  this->shapes = newShapes;
  if (keepoutArea != nullptr) {
    keepoutArea = keepoutArea->mirrorVertical(pole);
  }
  keepoutLines = nullptr;
  keepoutTiles = nullptr;
  lineSlices_ = nullptr;
  clearLineCuts();
  sliceTolerance_ = 0;
  invalidateEdgePinNets();
}

int32_t BoardOutline::shapeCount() { return this->shapes->length; }

PolylineShape* BoardOutline::getShape(int32_t index) {
  if (index < 0 || index >= this->shapes->length) {
    FRLogger::warn("BoardOutline.get_shape: index out of range");
    return nullptr;
  }
  return (*this->shapes)[index];
}

bool BoardOutline::isSelectedByFilter(ItemSelectionFilter* filter) {
  if (!this->isSelectedByFixedFilter(filter)) {
    return false;
  }
  return filter->isSelected(ItemSelectionFilter_SelectableChoices::BOARD_OUTLINE);
}

Area* BoardOutline::getKeepoutArea() {
  if (this->keepoutArea == nullptr) {
    jrt::Array<PolylineShape*>* holeArr = this->shapes->clone();
    keepoutArea = new PolylineArea(this->board->boundingBox, holeArr);
  }
  return this->keepoutArea;
}

jrt::Array<TileShape*>* BoardOutline::getKeepoutTiles() {
  if (this->keepoutTiles == nullptr) {
    jrt::Array<TileShape*>* convexShapes = this->getKeepoutArea()->splitToConvex();
    if (convexShapes == nullptr) {
      return nullptr;
    }
    this->keepoutTiles = TileShape::splitToTightOctagons(convexShapes, this->sliceTolerance());
  }
  return this->keepoutTiles;
}

jrt::Array<TileShape*>* BoardOutline::getKeepoutLines() {
  if (this->keepoutLines == nullptr) {
    this->keepoutLines = new jrt::Array<TileShape*>(0);
  }
  return this->keepoutLines;
}

Item* BoardOutline::copy(int32_t id) {
  // Java copy() passes only the shapes. The board deep copy restores edgePinNetsCrossOutline and
  // the keepout-outside state itself (BasicBoard::copyItemListFrom).
  return new BoardOutline(this->shapes, this->clearanceClassIndex(), id, this->board);
}

void BoardOutline::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new TextManager("app.freerouting.board.model.structure.BoardOutline", locale);
  printer->appendBold(tm->getText("boardOutline"));
  printClearanceInfo(printer, locale);
  printer->newline();
}

void BoardOutline::generateKeepoutOutside(bool value) {
  if (value == keepoutOutsideOutline) {
    return;
  }
  keepoutOutsideOutline = value;
  if (this->board == nullptr || this->board->searchTreeManager == nullptr) {
    return;
  }
  this->board->searchTreeManager->remove(this);
  this->board->searchTreeManager->insert(this);
}

int32_t BoardOutline::lineCount() {
  int32_t result = 0;
  for (PolylineShape* currentShape : *this->shapes) {
    result += currentShape->borderLineCount();
  }
  return result;
}

TileShape* BoardOutline::lineShape(int32_t lineIndex, int32_t halfWidth) {
  int32_t index = lineIndex;
  for (PolylineShape* currentShape : *this->shapes) {
    int32_t borderLineCount = currentShape->borderLineCount();
    if (index < borderLineCount) {
      auto* lines = jrt::Array<Line*>::of(
          {currentShape->borderLine((index + borderLineCount - 1) % borderLineCount),
           currentShape->borderLine(index),
           currentShape->borderLine((index + 1) % borderLineCount)});
      return (new Polyline(lines))->offsetShape(halfWidth, 0);
    }
    index -= borderLineCount;
  }
  throw jrt::IndexOutOfBoundsException("outline line " + jrt::str(lineIndex));
}

int32_t BoardOutline::lineSlices(int32_t lineIndex) {
  if (this->lineSlices_ == nullptr) {
    auto* slices = new jrt::Array<int32_t>(this->lineCount());
    double tolerance = this->sliceTolerance();
    for (int32_t i = 0; i < slices->length; i++) {
      double excess = this->lineShape(i, HALF_WIDTH)->boundingOctagonExcess();
      (*slices)[i] = jrt::d2i(jrt::Math::min(
          static_cast<double>(MAX_LINE_SLICES),
          jrt::Math::max(1.0, jrt::Math::ceil(excess / tolerance))));
    }
    this->cutAtEdgeWindows(slices, this->computeEdgeWindows());
    this->lineSlices_ = slices;
  }
  return (*this->lineSlices_)[lineIndex];
}

void BoardOutline::clearLineCuts() {
  this->edgeWindows = nullptr;
  this->lineCuts = nullptr;
  this->lineCutAlongX = nullptr;
  this->pieceWindows = nullptr;
  this->lineSliceTotal = -1;
}

void BoardOutline::cutAtEdgeWindows(jrt::Array<int32_t>* slices,
                                    jrt::Array<BoardOutline_EdgeWindow*>* windows) {
  this->edgeWindows = windows;
  this->lineCuts = new jrt::Array<jrt::Array<int32_t>*>(slices->length);
  this->lineCutAlongX = new jrt::Array<bool>(slices->length);
  if (windows->length == 0) {
    this->pieceWindows = nullptr;
    return;
  }
  std::vector<jrt::Array<int32_t>*> pieces;
  for (int32_t i = 0; i < slices->length; i++) {
    TileShape* base = this->lineShape(i, HALF_WIDTH);
    IntBox* box = base->boundingBox();
    bool alongX = box->width() >= box->height();
    int32_t low = alongX ? box->ll->x : box->ll->y;
    int32_t high = alongX ? box->ur->x : box->ur->y;
    auto* cuts = new jrt::TreeSet<int32_t>();
    for (BoardOutline_EdgeWindow* window : *windows) {
      if (!window->box()->intersects(box) ||
          base->intersectionWithSimplify(window->box())->dimension() < 2) {
        continue;
      }
      cuts->add(alongX ? window->box()->ll->x : window->box()->ll->y);
      cuts->add(alongX ? window->box()->ur->x : window->box()->ur->y);
    }
    cuts->removeIf([low, high](int32_t c) { return c <= low || c >= high; });
    if (cuts->isEmpty()) {
      for (int32_t k = 0; k < (*slices)[i]; k++) {
        pieces.push_back(nullptr);
      }
      continue;
    }
    int64_t extent = static_cast<int64_t>(high) - low;
    for (int32_t k = 1; k < (*slices)[i]; k++) {
      cuts->add(jrt::l2i(low + extent * k / (*slices)[i]));
    }
    auto* cutArray = new jrt::Array<int32_t>(cuts->size());
    {
      int32_t n = 0;
      for (int32_t c : *cuts) {
        (*cutArray)[n++] = c;
      }
    }
    (*this->lineCuts)[i] = cutArray;
    (*this->lineCutAlongX)[i] = alongX;
    (*slices)[i] = cutArray->length + 1;
    for (int32_t k = 0; k <= cutArray->length; k++) {
      int32_t from = k == 0 ? low : (*cutArray)[k - 1];
      int32_t to = k == cutArray->length ? high : (*cutArray)[k];
      IntBox* slab = alongX ? new IntBox(from, box->ll->y, to, box->ur->y)
                            : new IntBox(box->ll->x, from, box->ur->x, to);
      TileShape* piece = base->intersectionWithSimplify(slab);
      std::vector<int32_t> inside;
      if (piece != nullptr && !piece->isEmpty() && piece->dimension() >= 2) {
        IntBox* pieceBox = piece->boundingBox();
        for (int32_t w = 0; w < windows->length; w++) {
          if ((*windows)[w]->box()->contains(static_cast<RegularTileShape*>(pieceBox))) {
            inside.push_back(w);
          }
        }
      }
      pieces.push_back(inside.empty() ? nullptr : new jrt::Array<int32_t>(inside));
    }
  }
  this->pieceWindows = new jrt::Array<jrt::Array<int32_t>*>(pieces);
}

jrt::Array<TileShape*>* BoardOutline::splitLine(int32_t lineIndex, TileShape* lineShape,
                                                int32_t widening) {
  int32_t count = this->lineSlices(lineIndex);
  jrt::Array<int32_t>* cuts = this->lineCuts == nullptr ? nullptr : (*this->lineCuts)[lineIndex];
  if (cuts == nullptr) {
    return lineShape->splitIntoSlices(count);
  }
  bool alongX = (*this->lineCutAlongX)[lineIndex];
  IntBox* box = lineShape->boundingBox();
  int32_t low = alongX ? box->ll->x : box->ll->y;
  int32_t high = alongX ? box->ur->x : box->ur->y;
  auto* result = new jrt::Array<TileShape*>(count);
  for (int32_t k = 0; k < count; k++) {
    int32_t from = k == 0 ? low : jrt::Math::max(low, (*cuts)[k - 1] - widening);
    int32_t to = k == cuts->length ? high : jrt::Math::min(high, (*cuts)[k] + widening);
    IntBox* slab = alongX ? new IntBox(from, box->ll->y, to, box->ur->y)
                          : new IntBox(box->ll->x, from, box->ur->x, to);
    TileShape* piece = to > from ? lineShape->intersectionWithSimplify(slab) : nullptr;
    (*result)[k] =
        piece == nullptr || piece->isEmpty() || piece->dimension() < 2 ? lineShape : piece;
  }
  return result;
}

jrt::Array<BoardOutline_EdgeWindow*>* BoardOutline::computeEdgeWindows() {
  if (this->board == nullptr || this->keepoutOutsideOutline || this->board->rules == nullptr) {
    return new jrt::Array<BoardOutline_EdgeWindow*>(0);
  }
  std::vector<BoardOutline_EdgeWindow*> result;
  for (Pin* pin : *this->board->getPins()) {
    std::vector<int32_t> netVec;
    for (int32_t n : *pin->netNumbers) {
      if (n > 0) {
        netVec.push_back(n);
      }
    }
    auto* nets = new jrt::Array<int32_t>(netVec);
    if (nets->length == 0 || !this->isEdgePin(pin)) {
      continue;
    }
    for (int32_t layer = pin->firstLayer(); layer <= pin->lastLayer(); layer++) {
      Shape* shape = pin->getShape(layer - pin->firstLayer());
      if (shape == nullptr || shape->isEmpty() || !shape->isBounded()) {
        continue;
      }
      int32_t margin = 0;
      for (int32_t net : *nets) {
        int32_t traceClass =
            this->board->rules->nets->get(net)->getNetClass()->getTraceClearanceClass();
        int32_t clearance = this->board->rules->clearanceMatrix->getValue(
            this->clearanceClassIndex(), traceClass, layer, false);
        margin = jrt::Math::max(
            margin, clearance + this->board->rules->getTraceHalfWidth(net, layer) + HALF_WIDTH);
      }
      result.push_back(
          new BoardOutline_EdgeWindow(shape->boundingBox()->offset(margin), layer, nets));
    }
  }
  return new jrt::Array<BoardOutline_EdgeWindow*>(result);
}

bool BoardOutline::isEdgePin(Pin* pin) {
  Point* center = pin->getCenter();
  if (center != nullptr && !this->contains(center)) {
    return true;
  }
  for (int32_t layer = pin->firstLayer(); layer <= pin->lastLayer(); layer++) {
    Shape* shape = pin->getShape(layer - pin->firstLayer());
    if (auto* tileShape = jrt::fast_cast<TileShape>(shape)) {
      for (int32_t c = 0; c < tileShape->borderLineCount(); c++) {
        if (!this->contains(tileShape->corner(c))) {
          return true;
        }
      }
    }
  }
  return false;
}

bool BoardOutline::sameWindows(jrt::Array<BoardOutline_EdgeWindow*>* a,
                               jrt::Array<BoardOutline_EdgeWindow*>* b) {
  if (a == nullptr || b == nullptr || a->length != b->length) {
    return a == b;
  }
  for (int32_t i = 0; i < a->length; i++) {
    if ((*a)[i]->layer() != (*b)[i]->layer() ||
        !(*a)[i]->box()->ll->equals((*b)[i]->box()->ll) ||
        !(*a)[i]->box()->ur->equals((*b)[i]->box()->ur) ||
        !jrt::Arrays::equals((*a)[i]->nets(), (*b)[i]->nets())) {
      return false;
    }
  }
  return true;
}

int32_t BoardOutline::sliceTolerance() {
  if (this->sliceTolerance_ == 0) {
    this->sliceTolerance_ = currentSliceTolerance();
  }
  return this->sliceTolerance_;
}

int32_t BoardOutline::currentSliceTolerance() {
  return jrt::Math::max(HALF_WIDTH, this->board->rules->getMinNetClassTraceHalfWidth());
}

void BoardOutline::updateSlicesToTraceWidths() {
  if (this->lineSlices_ == nullptr || this->sliceTolerance_ == 0) {
    return;
  }
  if (this->sliceTolerance_ == currentSliceTolerance() &&
      sameWindows(this->edgeWindows, this->computeEdgeWindows())) {
    return;
  }
  bool inTrees = this->isOnTheBoard() && this->board->searchTreeManager != nullptr;
  if (inTrees) {
    this->board->additionalUpdateAfterChange(this);
    this->board->searchTreeManager->remove(this);
  }
  this->keepoutTiles = nullptr;
  this->lineSlices_ = nullptr;
  clearLineCuts();
  this->sliceTolerance_ = 0;
  this->clearDerivedData();
  if (inTrees) {
    this->board->searchTreeManager->insert(this);
    this->board->additionalUpdateAfterChange(this);
  }
}

int32_t BoardOutline::lineSliceCount() {
  if (this->lineSlices_ == nullptr || this->lineSliceTotal < 0) {
    int32_t result = 0;
    for (int32_t i = 0; i < this->lineCount(); i++) {
      result += this->lineSlices(i);
    }
    this->lineSliceTotal = result;
  }
  return this->lineSliceTotal;
}

jrt::Array<TileShape*>* BoardOutline::calculateTreeShapes(ShapeSearchTree* searchTree) {
  return searchTree->calculateTreeShapes(this);
}

bool BoardOutline::contains(Point* point) {
  if (point == nullptr) {
    return false;
  }
  for (PolylineShape* currentShape : *this->shapes) {
    if (currentShape->contains(point)) {
      return true;
    }
  }
  return false;
}

bool BoardOutline::contains(FloatPoint* point) {
  if (point == nullptr) {
    return false;
  }
  for (PolylineShape* currentShape : *this->shapes) {
    if (currentShape->contains(point)) {
      return true;
    }
  }
  return false;
}

}  // namespace fr
