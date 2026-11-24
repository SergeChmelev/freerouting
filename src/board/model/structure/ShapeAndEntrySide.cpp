// Port of app.freerouting.board.model.structure.ShapeAndEntrySide
#include "board/model/structure/ShapeAndEntrySide.h"

#include "board/facade/BasicBoard.h"
#include "board/model/structure/ShapeEntrySide.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"

namespace fr {

ShapeAndEntrySide::ShapeAndEntrySide(PolylineTrace* trace, int32_t index, bool orthogonal,
                                     bool inShoveCheck) {
  ShapeSearchTree* searchTree = trace->board->searchTreeManager->getDefaultTree();
  TileShape* currentShape = trace->getTreeShape(searchTree, index);
  ShapeEntrySide* currentFromSide = nullptr;
  bool cutOffAtStart = false;
  bool cutOffAtEnd = false;
  if (orthogonal) {
    currentShape = currentShape->boundingBox();
  } else {
    // prevent dog ears at the start and the end of the substitute trace
    currentShape = currentShape->toSimplex();
    Line* endCutline = calcCutlineAtEnd(index, trace);
    if (endCutline != nullptr) {
      TileShape* cutPlane = TileShape::getInstance(endCutline);
      TileShape* tmpShape = currentShape->intersection(cutPlane);
      if (tmpShape != currentShape && !tmpShape->isEmpty()) {
        currentShape = tmpShape->toSimplex();
        cutOffAtEnd = true;
      }
    }
    Line* startCutline = calcCutlineAtStart(index, trace);
    if (startCutline != nullptr) {
      TileShape* cutPlane = TileShape::getInstance(startCutline);
      TileShape* tmpShape = currentShape->intersection(cutPlane);
      if (tmpShape != currentShape && !tmpShape->isEmpty()) {
        currentShape = tmpShape->toSimplex();
        cutOffAtStart = true;
      }
    }
    int32_t fromSideIndex = -1;
    Line* currentCutLine = nullptr;
    if (cutOffAtStart) {
      currentCutLine = startCutline;
      fromSideIndex = currentShape->borderLineIndex(currentCutLine);
    }
    if (fromSideIndex < 0 && cutOffAtEnd) {
      currentCutLine = endCutline;
      fromSideIndex = currentShape->borderLineIndex(currentCutLine);
    }
    if (fromSideIndex >= 0) {
      FloatPoint* borderIntersection =
          currentCutLine->intersectionApprox(currentShape->borderLine(fromSideIndex));
      currentFromSide = new (jrt::kOp) ShapeEntrySide(fromSideIndex, borderIntersection);
    }
  }
  if (currentFromSide == nullptr && !inShoveCheck) {
    // In inShoveCheck, using this calculation may produce an undesired stackLevel > 1 in
    // ShapeTraceEntries.
    currentFromSide = new (jrt::kBoard) ShapeEntrySide(trace->polyline(), index, currentShape);
  }
  this->shape = currentShape;
  this->fromSide = currentFromSide;
}

Line* ShapeAndEntrySide::calcCutlineAtEnd(int32_t index, PolylineTrace* trace) {
  Polyline* traceLines = trace->polyline();
  ShapeSearchTree* searchTree = trace->board->searchTreeManager->getDefaultTree();
  if (index == traceLines->lines->length - 3 ||
      traceLines->cornerApprox(traceLines->lines->length - 2)
              ->distance(traceLines->cornerApprox(index + 1)) <
          trace->getCompensatedHalfWidth(searchTree)) {
    Line* currentLine = (*traceLines->lines)[traceLines->lines->length - 1];
    FloatPoint* is = traceLines->cornerApprox(traceLines->lines->length - 3);
    Line* cutLine;
    if (currentLine->sideOf(is) == Side::ON_THE_LEFT()) {
      cutLine = currentLine->opposite();
    } else {
      cutLine = currentLine;
    }
    return cutLine;
  }
  return nullptr;
}

Line* ShapeAndEntrySide::calcCutlineAtStart(int32_t index, PolylineTrace* trace) {
  Polyline* traceLines = trace->polyline();
  ShapeSearchTree* searchTree = trace->board->searchTreeManager->getDefaultTree();
  if (index == 0 || traceLines->cornerApprox(0)->distance(traceLines->cornerApprox(index)) <
                        trace->getCompensatedHalfWidth(searchTree)) {
    Line* currentLine = (*traceLines->lines)[0];
    FloatPoint* is = traceLines->cornerApprox(1);
    Line* cutLine;
    if (currentLine->sideOf(is) == Side::ON_THE_LEFT()) {
      cutLine = currentLine->opposite();
    } else {
      cutLine = currentLine;
    }
    return cutLine;
  }
  return nullptr;
}

}  // namespace fr
