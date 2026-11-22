// Port of app.freerouting.datastructures.PlanarDelaunayTriangulation
#include "datastructures/PlanarDelaunayTriangulation.h"

#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Side.h"
#include "logger/FRLogger.h"

namespace fr {

using PDT_Corner = PlanarDelaunayTriangulation_Corner;
using PDT_Edge = PlanarDelaunayTriangulation_Edge;
using PDT_Triangle = PlanarDelaunayTriangulation_Triangle;

// ---------------------------------------------------------------------------------------------
// PlanarDelaunayTriangulation
// ---------------------------------------------------------------------------------------------

PlanarDelaunayTriangulation::PlanarDelaunayTriangulation(
    jrt::Collection<PlanarDelaunayTriangulation_Storable*>* objectList) {
  // PORT NOTE: Java uses a LinkedList; an ArrayList gives the same order and the same
  // Collections.shuffle result (the same swap sequence) without O(n^2) index access.
  auto* cornerList = new jrt::ArrayList<PDT_Corner*>();
  for (auto it = objectList->iterator(); it->hasNext();) {
    PlanarDelaunayTriangulation_Storable* currentObject = it->next();
    jrt::Array<Point*>* currentCorners = currentObject->getTriangulationCorners();
    for (Point* currentCorner : *currentCorners) {
      cornerList->add(new PDT_Corner(currentObject, currentCorner));
    }
  }

  // create a random permutation of the corners.
  // use a fixed seed to get reproducible result without cross-worker thread interference
  jrt::Collections::shuffle<PDT_Corner*>(cornerList, new jrt::Random(seed));

  // create a big triangle containing all corners in the list to start with.

  int32_t boundingCoor = Limits::CRIT_INT;
  auto* boundingCorners = new jrt::Array<PDT_Corner*>(3);
  (*boundingCorners)[0] = new PDT_Corner(nullptr, new IntPoint(boundingCoor, 0));
  (*boundingCorners)[1] = new PDT_Corner(nullptr, new IntPoint(0, boundingCoor));
  (*boundingCorners)[2] = new PDT_Corner(nullptr, new IntPoint(-boundingCoor, -boundingCoor));

  auto* edgeLines = new jrt::Array<PDT_Edge*>(3);
  for (int32_t i = 0; i < 2; i++) {
    (*edgeLines)[i] = new PDT_Edge(this, (*boundingCorners)[i], (*boundingCorners)[i + 1]);
  }
  (*edgeLines)[2] = new PDT_Edge(this, (*boundingCorners)[2], (*boundingCorners)[0]);

  auto* startTriangle = new PDT_Triangle(this, edgeLines, nullptr);

  // Set the left triangle of the edge lines to startTriangle.
  // The right triangles remains null.
  for (PDT_Edge* currentEdge : *edgeLines) {
    currentEdge->setLeftTriangle(startTriangle);
  }

  // Initialize the search graph.

  this->searchGraph = new PlanarDelaunayTriangulation_TriangleGraph(startTriangle);
  this->degenerateEdges = new jrt::LinkedList<PDT_Edge*>();

  // Insert the corners in the corner list into the search graph.

  for (PDT_Corner* currentCorner : *cornerList) {
    PDT_Triangle* triangleToSplit = this->searchGraph->positionLocate(currentCorner);
    this->split(triangleToSplit, currentCorner);
  }
}

jrt::Collection<PlanarDelaunayTriangulation_ResultEdge*>*
PlanarDelaunayTriangulation::getEdgeLines() {
  jrt::Collection<PlanarDelaunayTriangulation_ResultEdge*>* result =
      new jrt::LinkedList<PlanarDelaunayTriangulation_ResultEdge*>();
  for (auto it = this->degenerateEdges->iterator(); it->hasNext();) {
    PDT_Edge* currentEdge = it->next();
    result->add(new PlanarDelaunayTriangulation_ResultEdge(
        currentEdge->startCorner->coor, currentEdge->startCorner->object,
        currentEdge->endCorner->coor, currentEdge->endCorner->object));
  }
  if (this->searchGraph->anchor != nullptr) {
    auto* resultEdges = new jrt::TreeSet<PDT_Edge*>();
    this->searchGraph->anchor->getLeafEdges(resultEdges);
    for (PDT_Edge* currentEdge : *resultEdges) {
      result->add(new PlanarDelaunayTriangulation_ResultEdge(
          currentEdge->startCorner->coor, currentEdge->startCorner->object,
          currentEdge->endCorner->coor, currentEdge->endCorner->object));
    }
  }
  return result;
}

bool PlanarDelaunayTriangulation::split(PDT_Triangle* triangle, PDT_Corner* corner) {
  // check, if corner is in the interior of this triangle or
  // if corner is contained in an edge line.

  PDT_Edge* containingEdge = nullptr;
  for (int32_t i = 0; i < 3; i++) {
    PDT_Edge* currentEdge = (*triangle->edgeLines)[i];
    Side* currentSide;
    if (currentEdge->leftTriangle == triangle) {
      currentSide = corner->sideOf(currentEdge->startCorner, currentEdge->endCorner);
    } else {
      currentSide = corner->sideOf(currentEdge->endCorner, currentEdge->startCorner);
    }
    if (currentSide == Side::ON_THE_RIGHT()) {
      // corner is outside this triangle
      FRLogger::warn("PlanarDelaunayTriangulation.split: corner is outside");
      return false;
    } else if (currentSide == Side::COLLINEAR()) {
      if (containingEdge != nullptr) {
        // corner is equal to a corner of this triangle

        PDT_Corner* commonCorner = currentEdge->commonCorner(containingEdge);
        if (commonCorner == nullptr) {
          FRLogger::warn("PlanarDelaunayTriangulation.split: common corner expected");
          return false;
        }
        if (corner->object == commonCorner->object) {
          return false;
        }
        this->degenerateEdges->add(new PDT_Edge(this, corner, commonCorner));
        return true;
      }
      containingEdge = currentEdge;
    }
  }

  if (containingEdge == nullptr) {
    // split triangle into 3 new triangles by adding edges from
    // the corners of  triangle to corner.

    jrt::Array<PDT_Triangle*>* newTriangles = triangle->splitAtInnerPoint(corner);

    if (newTriangles == nullptr) {
      return false;
    }

    for (PDT_Triangle* currentTriangle : *newTriangles) {
      this->searchGraph->insert(currentTriangle, triangle);
    }

    for (int32_t i = 0; i < 3; i++) {
      legalizeEdge(corner, (*triangle->edgeLines)[i]);
    }

  } else {
    // split this triangle and the neighbour triangle into 4 new triangles by adding edges from
    // the corners of the triangles to corner.

    PDT_Triangle* neighbourToSplit = containingEdge->otherNeighbour(triangle);

    jrt::Array<PDT_Triangle*>* newTriangles = triangle->splitAtBorderPoint(corner, neighbourToSplit);
    if (newTriangles == nullptr) {
      return false;
    }

    // There are exact four new triangles with the first 2 dividing triangle and
    // the last 2 dividing neighbourToSplit.
    this->searchGraph->insert((*newTriangles)[0], triangle);
    this->searchGraph->insert((*newTriangles)[1], triangle);
    this->searchGraph->insert((*newTriangles)[2], neighbourToSplit);
    this->searchGraph->insert((*newTriangles)[3], neighbourToSplit);

    for (int32_t i = 0; i < 3; i++) {
      PDT_Edge* currentEdge = (*triangle->edgeLines)[i];
      if (currentEdge != containingEdge) {
        legalizeEdge(corner, currentEdge);
      }
    }
    for (int32_t i = 0; i < 3; i++) {
      PDT_Edge* currentEdge = (*neighbourToSplit->edgeLines)[i];
      if (currentEdge != containingEdge) {
        legalizeEdge(corner, currentEdge);
      }
    }
  }
  return true;
}

bool PlanarDelaunayTriangulation::legalizeEdge(PDT_Corner* corner, PDT_Edge* edge) {
  if (edge->isLegal()) {
    return false;
  }
  PDT_Triangle* triangleToChange;
  if (edge->leftTriangle->oppositeCorner(edge) == corner) {
    triangleToChange = edge->rightTriangle;
  } else if (edge->rightTriangle->oppositeCorner(edge) == corner) {
    triangleToChange = edge->leftTriangle;
  } else {
    FRLogger::warn("PlanarDelaunayTriangulation.legalize_edge: edge lines inconsistent");
    return false;
  }
  PDT_Edge* flippedEdge = edge->flip();
  if (flippedEdge == nullptr) {
    // Java: NullPointerException on flippedEdge.leftTriangle
    throw jrt::NullPointerException("PlanarDelaunayTriangulation.legalizeEdge: flip failed");
  }

  // Update the search graph.

  this->searchGraph->insert(flippedEdge->leftTriangle, edge->leftTriangle);
  this->searchGraph->insert(flippedEdge->rightTriangle, edge->leftTriangle);
  this->searchGraph->insert(flippedEdge->leftTriangle, edge->rightTriangle);
  this->searchGraph->insert(flippedEdge->rightTriangle, edge->rightTriangle);

  // Call this function recursively for the other edge lines of triangleToChange.
  for (int32_t i = 0; i < 3; i++) {
    PDT_Edge* currentEdge = (*triangleToChange->edgeLines)[i];
    if (currentEdge != edge) {
      legalizeEdge(corner, currentEdge);
    }
  }
  return true;
}

bool PlanarDelaunayTriangulation::validate() {
  bool result = this->searchGraph->anchor->validate();
  if (result) {
    FRLogger::warn("Delaunay triangulation check passed ok");
  } else {
    FRLogger::warn("Delaunay triangulation check has detected problems");
  }
  return result;
}

int32_t PlanarDelaunayTriangulation::newEdgeId() {
  ++this->lastEdgeId;
  return this->lastEdgeId;
}

// ---------------------------------------------------------------------------------------------
// Corner
// ---------------------------------------------------------------------------------------------

Side* PlanarDelaunayTriangulation_Corner::sideOf(PDT_Corner* p1, PDT_Corner* p2) {
  return this->coor->sideOf(p1->coor, p2->coor);
}

// ---------------------------------------------------------------------------------------------
// TriangleGraph
// ---------------------------------------------------------------------------------------------

PlanarDelaunayTriangulation_TriangleGraph::PlanarDelaunayTriangulation_TriangleGraph(
    PDT_Triangle* triangle) {
  if (triangle != nullptr) {
    insert(triangle, nullptr);
  } else {
    this->anchor = nullptr;
  }
}

void PlanarDelaunayTriangulation_TriangleGraph::insert(PDT_Triangle* triangle,
                                                       PDT_Triangle* parent) {
  triangle->initializeIsOnTheLeftOfEdgeLineArray();
  if (parent == nullptr) {
    anchor = triangle;
  } else {
    parent->children->add(triangle);
  }
}

PDT_Triangle* PlanarDelaunayTriangulation_TriangleGraph::positionLocate(PDT_Corner* corner) {
  if (this->anchor == nullptr) {
    return nullptr;
  }
  if (this->anchor->children->isEmpty()) {
    return this->anchor;
  }
  for (auto it = this->anchor->children->iterator(); it->hasNext();) {
    PDT_Triangle* currentChild = it->next();
    PDT_Triangle* result = positionLocateReku(corner, currentChild);
    if (result != nullptr) {
      return result;
    }
  }
  FRLogger::warn("TriangleGraph.position_locate: containing triangle not found");
  return nullptr;
}

PDT_Triangle* PlanarDelaunayTriangulation_TriangleGraph::positionLocateReku(
    PDT_Corner* corner, PDT_Triangle* triangle) {
  if (!triangle->contains(corner)) {
    return nullptr;
  }

  if (triangle->isLeaf()) {
    return triangle;
  }
  for (auto it = triangle->children->iterator(); it->hasNext();) {
    PDT_Triangle* currentChild = it->next();
    PDT_Triangle* result = positionLocateReku(corner, currentChild);
    if (result != nullptr) {
      return result;
    }
  }
  FRLogger::warn("TriangleGraph.position_locate_reku: containing triangle not found");
  return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Edge
// ---------------------------------------------------------------------------------------------

PlanarDelaunayTriangulation_Edge::PlanarDelaunayTriangulation_Edge(
    PlanarDelaunayTriangulation* outer, PDT_Corner* startCorner, PDT_Corner* endCorner)
    : outer(outer), startCorner(startCorner), endCorner(endCorner) {
  id = outer->newEdgeId();
}

PDT_Corner* PlanarDelaunayTriangulation_Edge::commonCorner(PDT_Edge* other) {
  PDT_Corner* result = nullptr;
  if (other->startCorner->equals(this->startCorner) ||
      other->endCorner->equals(this->startCorner)) {
    result = this->startCorner;
  } else if (other->startCorner->equals(this->endCorner) ||
             other->endCorner->equals(this->endCorner)) {
    result = this->endCorner;
  }
  return result;
}

PDT_Triangle* PlanarDelaunayTriangulation_Edge::otherNeighbour(PDT_Triangle* triangle) {
  PDT_Triangle* result;
  if (triangle == this->leftTriangle) {
    result = this->rightTriangle;
  } else if (triangle == this->rightTriangle) {
    result = this->leftTriangle;
  } else {
    FRLogger::warn("Edge.other_neighbour: inconsistent neighbour triangle");
    result = nullptr;
  }
  return result;
}

bool PlanarDelaunayTriangulation_Edge::isLegal() {
  if (this->leftTriangle == nullptr || this->rightTriangle == nullptr) {
    return true;
  }
  PDT_Corner* leftOppositeCorner = this->leftTriangle->oppositeCorner(this);
  PDT_Corner* rightOppositeCorner = this->rightTriangle->oppositeCorner(this);

  bool insideCircle = rightOppositeCorner->coor->toFloat()->insideCircle(
      this->startCorner->coor->toFloat(), leftOppositeCorner->coor->toFloat(),
      this->endCorner->coor->toFloat());
  return !insideCircle;
}

PDT_Edge* PlanarDelaunayTriangulation_Edge::flip() {
  // Create the flipped edge, so that the start corner of this edge is on the left
  // and the end corner of this edge on the right.
  auto* flippedEdge = new PDT_Edge(outer, this->rightTriangle->oppositeCorner(this),
                                   this->leftTriangle->oppositeCorner(this));

  PDT_Triangle* const firstParent = this->leftTriangle;

  // Calculate the index of this edge line in the left and right adjacent triangles.

  int32_t leftIndex = -1;
  int32_t rightIndex = -1;
  for (int32_t i = 0; i < 3; i++) {
    if ((*this->leftTriangle->edgeLines)[i] == this) {
      leftIndex = i;
    }
    if ((*this->rightTriangle->edgeLines)[i] == this) {
      rightIndex = i;
    }
  }
  if (leftIndex < 0 || rightIndex < 0) {
    FRLogger::warn("Edge.flip: edge line inconsistent");
    return nullptr;
  }
  PDT_Edge* const leftPrevEdge = (*this->leftTriangle->edgeLines)[(leftIndex + 2) % 3];
  PDT_Edge* const leftNextEdge = (*this->leftTriangle->edgeLines)[(leftIndex + 1) % 3];
  PDT_Edge* const rightPrevEdge = (*this->rightTriangle->edgeLines)[(rightIndex + 2) % 3];
  PDT_Edge* const rightNextEdge = (*this->rightTriangle->edgeLines)[(rightIndex + 1) % 3];

  // Create the left triangle of the flipped edge.

  auto* currentEdgeLines = new jrt::Array<PDT_Edge*>(3);
  (*currentEdgeLines)[0] = flippedEdge;
  (*currentEdgeLines)[1] = leftPrevEdge;
  (*currentEdgeLines)[2] = rightNextEdge;
  auto* newLeftTriangle = new PDT_Triangle(outer, currentEdgeLines, firstParent);
  flippedEdge->leftTriangle = newLeftTriangle;
  if (leftPrevEdge->leftTriangle == this->leftTriangle) {
    leftPrevEdge->leftTriangle = newLeftTriangle;
  } else {
    leftPrevEdge->rightTriangle = newLeftTriangle;
  }
  if (rightNextEdge->leftTriangle == this->rightTriangle) {
    rightNextEdge->leftTriangle = newLeftTriangle;
  } else {
    rightNextEdge->rightTriangle = newLeftTriangle;
  }

  // Create the right triangle of the flipped edge.

  currentEdgeLines = new jrt::Array<PDT_Edge*>(3);
  (*currentEdgeLines)[0] = flippedEdge;
  (*currentEdgeLines)[1] = rightPrevEdge;
  (*currentEdgeLines)[2] = leftNextEdge;
  auto* newRightTriangle = new PDT_Triangle(outer, currentEdgeLines, firstParent);
  flippedEdge->rightTriangle = newRightTriangle;
  if (rightPrevEdge->leftTriangle == this->rightTriangle) {
    rightPrevEdge->leftTriangle = newRightTriangle;
  } else {
    rightPrevEdge->rightTriangle = newRightTriangle;
  }
  if (leftNextEdge->leftTriangle == this->leftTriangle) {
    leftNextEdge->leftTriangle = newRightTriangle;
  } else {
    leftNextEdge->rightTriangle = newRightTriangle;
  }

  return flippedEdge;
}

bool PlanarDelaunayTriangulation_Edge::validate() {
  bool result = true;
  if (this->leftTriangle == nullptr) {
    if (this->startCorner->object != nullptr || this->endCorner->object != nullptr) {
      FRLogger::warn("Edge.validate: left triangle may be null only for bounding edges");
      result = false;
    }
  } else {
    // check if the left triangle contains this edge
    bool found = false;
    for (int32_t i = 0; i < 3; i++) {
      if ((*leftTriangle->edgeLines)[i] == this) {
        found = true;
        break;
      }
    }
    if (!found) {
      FRLogger::warn("Edge.validate: left triangle does not contain this edge");
      result = false;
    }
  }
  if (this->rightTriangle == nullptr) {
    if (this->startCorner->object != nullptr || this->endCorner->object != nullptr) {
      FRLogger::warn("Edge.validate: right triangle may be null only for bounding edges");
      result = false;
    }
  } else {
    // check if the left triangle contains this edge
    bool found = false;
    for (int32_t i = 0; i < 3; i++) {
      if ((*rightTriangle->edgeLines)[i] == this) {
        found = true;
        break;
      }
    }
    if (!found) {
      FRLogger::warn("Edge.validate: right triangle does not contain this edge");
      result = false;
    }
  }

  return result;
}

// ---------------------------------------------------------------------------------------------
// Triangle
// ---------------------------------------------------------------------------------------------

PlanarDelaunayTriangulation_Triangle::PlanarDelaunayTriangulation_Triangle(
    PlanarDelaunayTriangulation* outer, jrt::Array<PDT_Edge*>* edgeLines,
    PDT_Triangle* firstParent)
    : outer(outer), edgeLines(edgeLines), firstParent(firstParent) {
  // create an empty list for the children.
  this->children = new jrt::LinkedList<PDT_Triangle*>();
}

bool PlanarDelaunayTriangulation_Triangle::isLeaf() { return this->children->isEmpty(); }

PDT_Corner* PlanarDelaunayTriangulation_Triangle::getCorner(int32_t no) {
  if (no < 0 || no >= 3) {
    FRLogger::warn("Triangle.get_corner: no out of range");
    return nullptr;
  }
  PDT_Edge* currentEdge = (*edgeLines)[no];
  PDT_Corner* result;
  if (currentEdge->leftTriangle == this) {
    result = currentEdge->startCorner;
  } else if (currentEdge->rightTriangle == this) {
    result = currentEdge->endCorner;
  } else {
    FRLogger::warn("Triangle.get_corner: inconsistent edge lines");
    result = nullptr;
  }
  return result;
}

PDT_Corner* PlanarDelaunayTriangulation_Triangle::oppositeCorner(PDT_Edge* edgeLine) {
  int32_t edgeLineNo = -1;
  for (int32_t i = 0; i < 3; i++) {
    if ((*this->edgeLines)[i] == edgeLine) {
      edgeLineNo = i;
      break;
    }
  }
  if (edgeLineNo < 0) {
    FRLogger::warn("Triangle.opposite_corner: edgeLine not found");
    return nullptr;
  }
  PDT_Edge* nextEdge = (*this->edgeLines)[(edgeLineNo + 1) % 3];
  PDT_Corner* result;
  if (nextEdge->leftTriangle == this) {
    result = nextEdge->endCorner;
  } else {
    result = nextEdge->startCorner;
  }
  return result;
}

bool PlanarDelaunayTriangulation_Triangle::contains(PDT_Corner* corner) {
  if (this->isOnTheLeftOfEdgeLine == nullptr) {
    FRLogger::warn("Triangle.contains: array isOnTheLeftOfEdgeLine not initialized");
    return false;
  }
  for (int32_t i = 0; i < 3; i++) {
    PDT_Edge* currentEdge = (*this->edgeLines)[i];
    Side* currentSide = corner->sideOf(currentEdge->startCorner, currentEdge->endCorner);
    if ((*this->isOnTheLeftOfEdgeLine)[i]) {
      // checking currentEdge.leftTriangle == this instead will not work, if this triangle is an
      // inner node.
      if (currentSide == Side::ON_THE_RIGHT()) {
        return false;
      }
    } else {
      if (currentSide == Side::ON_THE_LEFT()) {
        return false;
      }
    }
  }
  return true;
}

void PlanarDelaunayTriangulation_Triangle::getLeafEdges(jrt::Set<PDT_Edge*>* resultEdges) {
  if (this->isLeaf()) {
    for (int32_t i = 0; i < 3; i++) {
      PDT_Edge* currentEdge = (*this->edgeLines)[i];
      if (currentEdge->startCorner->object != nullptr &&
          currentEdge->endCorner->object != nullptr) {
        // Skip edges containing a bounding corner.
        resultEdges->add(currentEdge);
      }
    }

  } else {
    for (auto it = this->children->iterator(); it->hasNext();) {
      PDT_Triangle* currentChild = it->next();
      if (currentChild->firstParent == this) {  // to prevent traversing nodes more than once
        currentChild->getLeafEdges(resultEdges);
      }
    }
  }
}

jrt::Array<PDT_Triangle*>* PlanarDelaunayTriangulation_Triangle::splitAtInnerPoint(
    PDT_Corner* corner) {
  auto* newEdges = new jrt::Array<PDT_Edge*>(3);
  for (int32_t i = 0; i < 3; i++) {
    (*newEdges)[i] = new PDT_Edge(outer, this->getCorner(i), corner);
  }

  auto* const newTriangles = new jrt::Array<PDT_Triangle*>(3);

  // construct the 3 new triangles.
  auto* currentEdgeLines = new jrt::Array<PDT_Edge*>(3);

  (*currentEdgeLines)[0] = (*this->edgeLines)[0];
  (*currentEdgeLines)[1] = new PDT_Edge(outer, this->getCorner(1), corner);
  (*currentEdgeLines)[2] = new PDT_Edge(outer, corner, this->getCorner(0));
  (*newTriangles)[0] = new PDT_Triangle(outer, currentEdgeLines, this);

  currentEdgeLines = new jrt::Array<PDT_Edge*>(3);
  (*currentEdgeLines)[0] = (*this->edgeLines)[1];
  (*currentEdgeLines)[1] = new PDT_Edge(outer, this->getCorner(2), corner);
  (*currentEdgeLines)[2] = (*(*newTriangles)[0]->edgeLines)[1];
  (*newTriangles)[1] = new PDT_Triangle(outer, currentEdgeLines, this);

  currentEdgeLines = new jrt::Array<PDT_Edge*>(3);
  (*currentEdgeLines)[0] = (*this->edgeLines)[2];
  (*currentEdgeLines)[1] = (*(*newTriangles)[0]->edgeLines)[2];
  (*currentEdgeLines)[2] = (*(*newTriangles)[1]->edgeLines)[1];
  (*newTriangles)[2] = new PDT_Triangle(outer, currentEdgeLines, this);

  // Set the new neighbour triangles of the edge lines.

  for (int32_t i = 0; i < 3; i++) {
    PDT_Edge* currentEdge = (*(*newTriangles)[i]->edgeLines)[0];
    if (currentEdge->getLeftTriangle() == this) {
      currentEdge->setLeftTriangle((*newTriangles)[i]);
    } else {
      currentEdge->setRightTriangle((*newTriangles)[i]);
    }
    // The other neighbour triangle remains valid.
  }

  PDT_Edge* currentEdge = (*(*newTriangles)[0]->edgeLines)[1];
  currentEdge->setLeftTriangle((*newTriangles)[0]);
  currentEdge->setRightTriangle((*newTriangles)[1]);

  currentEdge = (*(*newTriangles)[1]->edgeLines)[1];
  currentEdge->setLeftTriangle((*newTriangles)[1]);
  currentEdge->setRightTriangle((*newTriangles)[2]);

  currentEdge = (*(*newTriangles)[2]->edgeLines)[1];
  currentEdge->setLeftTriangle((*newTriangles)[0]);
  currentEdge->setRightTriangle((*newTriangles)[2]);
  return newTriangles;
}

jrt::Array<PDT_Triangle*>* PlanarDelaunayTriangulation_Triangle::splitAtBorderPoint(
    PDT_Corner* corner, PDT_Triangle* neighbourToSplit) {
  // look for the triangle edge of this and the neighbour triangle containing corner;
  int32_t thisTouchingEdgeNo = -1;
  int32_t neighbourTouchingEdgeNo = -1;
  PDT_Edge* touchingEdge = nullptr;
  PDT_Edge* otherTouchingEdge = nullptr;
  for (int32_t i = 0; i < 3; i++) {
    PDT_Edge* currentEdge = (*this->edgeLines)[i];
    if (corner->sideOf(currentEdge->startCorner, currentEdge->endCorner) == Side::COLLINEAR()) {
      thisTouchingEdgeNo = i;
      touchingEdge = currentEdge;
    }
    currentEdge = (*neighbourToSplit->edgeLines)[i];
    if (corner->sideOf(currentEdge->startCorner, currentEdge->endCorner) == Side::COLLINEAR()) {
      neighbourTouchingEdgeNo = i;
      otherTouchingEdge = currentEdge;
    }
  }
  if (thisTouchingEdgeNo < 0 || neighbourTouchingEdgeNo < 0) {
    FRLogger::warn("Triangle.split_at_border_point: touching edge not found");
    return nullptr;
  }
  if (touchingEdge != otherTouchingEdge) {
    FRLogger::warn("Triangle.split_at_border_point: edges inconsistent");
    return nullptr;
  }

  auto* const newTriangles = new jrt::Array<PDT_Triangle*>(4);

  PDT_Edge* firstCommonNewEdge;
  PDT_Edge* secondCommonNewEdge;
  // Construct the new edge lines that 2 split triangles of this triangle
  // will be on the left side of the new common touching edges.
  if (this == touchingEdge->leftTriangle) {
    firstCommonNewEdge = new PDT_Edge(outer, touchingEdge->startCorner, corner);
    secondCommonNewEdge = new PDT_Edge(outer, corner, touchingEdge->endCorner);
  } else {
    firstCommonNewEdge = new PDT_Edge(outer, touchingEdge->endCorner, corner);
    secondCommonNewEdge = new PDT_Edge(outer, corner, touchingEdge->startCorner);
  }

  // Construct the first split triangle of this triangle.

  PDT_Edge* prevEdge = (*this->edgeLines)[(thisTouchingEdgeNo + 2) % 3];
  PDT_Edge* thisSplittingEdge;
  // construct the splitting edge line of this triangle, so that the first split
  // triangle lies on the left side, and the second split triangle on the right side.
  if (this == prevEdge->leftTriangle) {
    thisSplittingEdge = new PDT_Edge(outer, corner, prevEdge->startCorner);
  } else {
    thisSplittingEdge = new PDT_Edge(outer, corner, prevEdge->endCorner);
  }
  auto* currentEdgeLines = new jrt::Array<PDT_Edge*>(3);
  (*currentEdgeLines)[0] = prevEdge;
  (*currentEdgeLines)[1] = firstCommonNewEdge;
  (*currentEdgeLines)[2] = thisSplittingEdge;
  (*newTriangles)[0] = new PDT_Triangle(outer, currentEdgeLines, this);
  if (this == prevEdge->leftTriangle) {
    prevEdge->setLeftTriangle((*newTriangles)[0]);
  } else {
    prevEdge->setRightTriangle((*newTriangles)[0]);
  }
  firstCommonNewEdge->setLeftTriangle((*newTriangles)[0]);
  thisSplittingEdge->setLeftTriangle((*newTriangles)[0]);

  // Construct the second split triangle of this triangle.

  PDT_Edge* const nextEdge = (*this->edgeLines)[(thisTouchingEdgeNo + 1) % 3];
  currentEdgeLines = new jrt::Array<PDT_Edge*>(3);
  (*currentEdgeLines)[0] = thisSplittingEdge;
  (*currentEdgeLines)[1] = secondCommonNewEdge;
  (*currentEdgeLines)[2] = nextEdge;
  (*newTriangles)[1] = new PDT_Triangle(outer, currentEdgeLines, this);
  thisSplittingEdge->setRightTriangle((*newTriangles)[1]);
  secondCommonNewEdge->setLeftTriangle((*newTriangles)[1]);
  if (this == nextEdge->leftTriangle) {
    nextEdge->setLeftTriangle((*newTriangles)[1]);
  } else {
    nextEdge->setRightTriangle((*newTriangles)[1]);
  }

  // construct the first split triangle of neighbourToSplit
  PDT_Edge* neighbourNextEdge = (*neighbourToSplit->edgeLines)[(neighbourTouchingEdgeNo + 1) % 3];
  PDT_Edge* neighbourSplittingEdge;
  // construct the splitting edge line of neighbourToSplit, so that the first split
  // triangle lies on the left side, and the second split triangle on the right side.
  if (neighbourToSplit == neighbourNextEdge->leftTriangle) {
    neighbourSplittingEdge = new PDT_Edge(outer, neighbourNextEdge->endCorner, corner);
  } else {
    neighbourSplittingEdge = new PDT_Edge(outer, neighbourNextEdge->startCorner, corner);
  }
  currentEdgeLines = new jrt::Array<PDT_Edge*>(3);
  (*currentEdgeLines)[0] = neighbourSplittingEdge;
  (*currentEdgeLines)[1] = firstCommonNewEdge;
  (*currentEdgeLines)[2] = neighbourNextEdge;
  (*newTriangles)[2] = new PDT_Triangle(outer, currentEdgeLines, neighbourToSplit);
  neighbourSplittingEdge->setLeftTriangle((*newTriangles)[2]);
  firstCommonNewEdge->setRightTriangle((*newTriangles)[2]);
  if (neighbourToSplit == neighbourNextEdge->leftTriangle) {
    neighbourNextEdge->setLeftTriangle((*newTriangles)[2]);
  } else {
    neighbourNextEdge->setRightTriangle((*newTriangles)[2]);
  }

  // construct the second split triangle of neighbourToSplit
  prevEdge = (*neighbourToSplit->edgeLines)[(neighbourTouchingEdgeNo + 2) % 3];
  currentEdgeLines = new jrt::Array<PDT_Edge*>(3);
  (*currentEdgeLines)[0] = prevEdge;
  (*currentEdgeLines)[1] = secondCommonNewEdge;
  (*currentEdgeLines)[2] = neighbourSplittingEdge;
  (*newTriangles)[3] = new PDT_Triangle(outer, currentEdgeLines, neighbourToSplit);
  if (neighbourToSplit == prevEdge->leftTriangle) {
    prevEdge->setLeftTriangle((*newTriangles)[3]);
  } else {
    prevEdge->setRightTriangle((*newTriangles)[3]);
  }
  secondCommonNewEdge->setRightTriangle((*newTriangles)[3]);
  neighbourSplittingEdge->setRightTriangle((*newTriangles)[3]);

  return newTriangles;
}

bool PlanarDelaunayTriangulation_Triangle::validate() {
  bool result = true;
  if (this->isLeaf()) {
    PDT_Edge* prevEdge = (*this->edgeLines)[2];
    for (int32_t i = 0; i < 3; i++) {
      PDT_Edge* currentEdge = (*this->edgeLines)[i];
      if (!currentEdge->validate()) {
        result = false;
      }
      // Check, if the ens corner of the previous line equals to the start corner of this line.
      PDT_Corner* prevEndCorner;
      if (prevEdge->leftTriangle == this) {
        prevEndCorner = prevEdge->endCorner;
      } else {
        prevEndCorner = prevEdge->startCorner;
      }
      PDT_Corner* currentStartCorner;
      if (currentEdge->leftTriangle == this) {
        currentStartCorner = currentEdge->startCorner;
      } else if (currentEdge->rightTriangle == this) {
        currentStartCorner = currentEdge->endCorner;
      } else {
        FRLogger::warn("Triangle.validate: edge inconsistent");
        return false;
      }
      if (currentStartCorner != prevEndCorner) {
        FRLogger::warn("Triangle.validate: corner inconsistent");
        result = false;
      }
      prevEdge = currentEdge;
    }
  } else {
    for (auto it = this->children->iterator(); it->hasNext();) {
      PDT_Triangle* currentChild = it->next();
      if (currentChild->firstParent == this) {  // to avoid traversing nodes more than once.
        currentChild->validate();
      }
    }
  }
  return result;
}

void PlanarDelaunayTriangulation_Triangle::initializeIsOnTheLeftOfEdgeLineArray() {
  if (this->isOnTheLeftOfEdgeLine != nullptr) {
    return;  // already initialized
  }
  this->isOnTheLeftOfEdgeLine = new jrt::Array<bool>(3);
  for (int32_t i = 0; i < 3; i++) {
    (*this->isOnTheLeftOfEdgeLine)[i] = (*this->edgeLines)[i]->leftTriangle == this;
  }
}

}  // namespace fr
