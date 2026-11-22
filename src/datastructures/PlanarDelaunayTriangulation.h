// Port of app.freerouting.datastructures.PlanarDelaunayTriangulation
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Interface with functionality required for objects to be used in a planar triangulation. */
class PlanarDelaunayTriangulation_Storable : public virtual jrt::Object {
 public:
  /** Returns an array of corners, which can be used in a planar triangulation. */
  virtual jrt::Array<Point*>* getTriangulationCorners() = 0;
};

/** Describes a line segment in the result of the Delaunay Triangulation. */
class PlanarDelaunayTriangulation_ResultEdge final : public virtual jrt::Object {
 public:
  /** The start point of the line segment. */
  Point* startPoint;

  /** The object at the start point of the line segment. */
  PlanarDelaunayTriangulation_Storable* startObject;

  /** The end point of the line segment. */
  Point* endPoint;

  /** The object at the end point of the line segment. */
  PlanarDelaunayTriangulation_Storable* endObject;

 private:
  friend class PlanarDelaunayTriangulation;

  PlanarDelaunayTriangulation_ResultEdge(Point* startPoint,
                                         PlanarDelaunayTriangulation_Storable* startObject,
                                         Point* endPoint,
                                         PlanarDelaunayTriangulation_Storable* endObject)
      : startPoint(startPoint), startObject(startObject), endPoint(endPoint), endObject(endObject) {}
};

/** Contains a corner point together with the objects this corner belongs to. */
class PlanarDelaunayTriangulation_Corner : public virtual jrt::Object {
 public:
  PlanarDelaunayTriangulation_Corner(PlanarDelaunayTriangulation_Storable* object, Point* coor)
      : object(object), coor(coor) {}

  /**
   * Returns Side.ON_THE_LEFT, if this corner is on the left of the line from p1 to p2;
   * Side.ON_THE_RIGHT, if on the right; and Side.COLLINEAR, if collinear.
   */
  Side* sideOf(PlanarDelaunayTriangulation_Corner* p1, PlanarDelaunayTriangulation_Corner* p2);

  PlanarDelaunayTriangulation_Storable* object;  // final in Java
  Point* coor;                                   // final in Java
};

/**
 * Directed acyclic graph for finding the triangle containing a search point p. The leaves contain
 * the triangles of the current triangulation. The internal nodes are triangles, that were part of
 * the triangulation at some earlier stage, but have been replaced their children.
 */
class PlanarDelaunayTriangulation_TriangleGraph : public virtual jrt::Object {
 public:
  explicit PlanarDelaunayTriangulation_TriangleGraph(PlanarDelaunayTriangulation_Triangle* triangle);

  void insert(PlanarDelaunayTriangulation_Triangle* triangle,
              PlanarDelaunayTriangulation_Triangle* parent);

  /**
   * Search for the leaf triangle containing corner. It will not be unique, if corner lies on a
   * triangle edge.
   */
  PlanarDelaunayTriangulation_Triangle* positionLocate(PlanarDelaunayTriangulation_Corner* corner);

  PlanarDelaunayTriangulation_Triangle* anchor = nullptr;  // private in Java (read by outer class)

 private:
  /** Recursive part of position_locate. */
  PlanarDelaunayTriangulation_Triangle* positionLocateReku(
      PlanarDelaunayTriangulation_Corner* corner, PlanarDelaunayTriangulation_Triangle* triangle);
};

/**
 * Describes an edge between two triangles in the triangulation. The unique ids are for making
 * edges comparable. (Inner class in Java: holds the outer triangulation for newEdgeId().)
 */
class PlanarDelaunayTriangulation_Edge : public virtual jrt::Object {
 public:
  PlanarDelaunayTriangulation_Edge(PlanarDelaunayTriangulation* outer,
                                   PlanarDelaunayTriangulation_Corner* startCorner,
                                   PlanarDelaunayTriangulation_Corner* endCorner);

  /** Comparable<Edge>. */
  virtual int32_t compareTo(PlanarDelaunayTriangulation_Edge* other) {
    return this->id - other->id;
  }

  PlanarDelaunayTriangulation_Triangle* getLeftTriangle() { return leftTriangle; }
  void setLeftTriangle(PlanarDelaunayTriangulation_Triangle* triangle) { leftTriangle = triangle; }
  PlanarDelaunayTriangulation_Triangle* getRightTriangle() { return rightTriangle; }
  void setRightTriangle(PlanarDelaunayTriangulation_Triangle* triangle) {
    rightTriangle = triangle;
  }

  /** Returns the common corner of this edge and other, or null, if no common corner exists. */
  PlanarDelaunayTriangulation_Corner* commonCorner(PlanarDelaunayTriangulation_Edge* other);

  /**
   * Returns the neighbour triangle of this edge, which is different from triangle. If triangle is
   * not a neighbour of this edge, null is returned.
   */
  PlanarDelaunayTriangulation_Triangle* otherNeighbour(
      PlanarDelaunayTriangulation_Triangle* triangle);

  /** Returns true, if this is a legal edge of the Delaunay Triangulation. */
  bool isLegal();

  /**
   * Flips this edge line to the edge line between the opposite corners of the adjacent triangles.
   * Returns the new constructed Edge.
   */
  PlanarDelaunayTriangulation_Edge* flip();

  /** Checks the consistency of this edge in its database. Used for debugging purposes. */
  bool validate();

  PlanarDelaunayTriangulation* outer;
  PlanarDelaunayTriangulation_Corner* startCorner;  // final in Java
  PlanarDelaunayTriangulation_Corner* endCorner;    // final in Java

  // private in Java; accessed by the enclosing class and Triangle.
  /** The unique ID of this edge. */
  int32_t id;
  /** The triangle on the left side of this edge. */
  PlanarDelaunayTriangulation_Triangle* leftTriangle = nullptr;
  /** The triangle on the right side of this edge. */
  PlanarDelaunayTriangulation_Triangle* rightTriangle = nullptr;
};

/**
 * Describes a triangle in the triangulation. edgeLines is an array of dimension 3, sorted in
 * counter clock sense around the border of this triangle. children points to the children of this
 * triangle, when used as a node in the search graph. (Inner class in Java.)
 */
class PlanarDelaunayTriangulation_Triangle : public virtual jrt::Object {
 public:
  PlanarDelaunayTriangulation_Triangle(PlanarDelaunayTriangulation* outer,
                                       jrt::Array<PlanarDelaunayTriangulation_Edge*>* edgeLines,
                                       PlanarDelaunayTriangulation_Triangle* firstParent);

  /** Returns true, if this triangle node is a leaf, and false, if it is an inner node. */
  bool isLeaf();

  /** Gets the corner with index no. */
  PlanarDelaunayTriangulation_Corner* getCorner(int32_t no);

  /**
   * Calculates the opposite corner of this triangle to edgeLine. Returns null, if edgeLine is not
   * an edge line of this triangle.
   */
  PlanarDelaunayTriangulation_Corner* oppositeCorner(PlanarDelaunayTriangulation_Edge* edgeLine);

  /** Checks if point is inside or on the border of this triangle. */
  bool contains(PlanarDelaunayTriangulation_Corner* corner);

  /** Puts the edges of all leafs below this node into the list resultEdges. */
  void getLeafEdges(jrt::Set<PlanarDelaunayTriangulation_Edge*>* resultEdges);

  /**
   * Split this triangle into 3 new triangles by adding edges from the corners of this triangle to
   * corner, corner has to be located in the interior of this triangle.
   */
  jrt::Array<PlanarDelaunayTriangulation_Triangle*>* splitAtInnerPoint(
      PlanarDelaunayTriangulation_Corner* corner);

  /**
   * Split this triangle and neighbourToSplit into 4 new triangles by adding edges from the corners
   * of the triangles to corner. Returns null if corner is not on the common edge line. The first 2
   * result triangles split this triangle, the last 2 split neighbourToSplit.
   */
  jrt::Array<PlanarDelaunayTriangulation_Triangle*>* splitAtBorderPoint(
      PlanarDelaunayTriangulation_Corner* corner,
      PlanarDelaunayTriangulation_Triangle* neighbourToSplit);

  /** Checks the consistency of this triangle and its children. Used for debugging purposes. */
  bool validate();

  /**
   * Must be done as long as this triangle node is a leaf and after for all its edge lines the
   * leftTriangle or the rightTriangle reference is set to this triangle. (private in Java, called
   * by TriangleGraph.)
   */
  void initializeIsOnTheLeftOfEdgeLineArray();

  PlanarDelaunayTriangulation* outer;

  // private in Java; accessed by the enclosing class, Edge and TriangleGraph.
  /** The 3 edge lines of this triangle sorted in counter clock sense around the border. */
  jrt::Array<PlanarDelaunayTriangulation_Edge*>* edgeLines;
  /** First parent, used when traversing the graph to avoid visiting children more than once. */
  PlanarDelaunayTriangulation_Triangle* firstParent;
  /** The children of this triangle when used as a node in the triangle search graph. */
  jrt::Collection<PlanarDelaunayTriangulation_Triangle*>* children;
  /** Indicates, if this triangle is on the left of the i-th edge line for i = 0 to 2. */
  jrt::Array<bool>* isOnTheLeftOfEdgeLine = nullptr;
};

/**
 * Creates a Delaunay triangulation in the plane for the input objects. The objects in the input
 * list must implement PlanarDelaunayTriangulation_Storable. The result can be read by the function
 * getEdgeLines(). The algorithm is from Chapter 9.3 of "Computational Geometry, Algorithms and
 * Applications" (de Berg, van Kreveld, Overmars, Schwarzkopf).
 */
class PlanarDelaunayTriangulation : public virtual jrt::Object {
 public:
  /** Creates a new instance of PlanarDelaunayTriangulation from objectList. */
  explicit PlanarDelaunayTriangulation(
      jrt::Collection<PlanarDelaunayTriangulation_Storable*>* objectList);

  /** Returns all edge lines of the result of the Delaunay Triangulation. */
  jrt::Collection<PlanarDelaunayTriangulation_ResultEdge*>* getEdgeLines();

  /** Checks the consistency of the triangles in this triangulation. Used for debugging purposes. */
  bool validate();

  /** Creates a new unique edge id number. (private in Java, used by Edge.) */
  int32_t newEdgeId();

 private:
  /** Random seed to shuffle the input corners; fixed to make the results reproducible. */
  static constexpr int32_t seed = 99;

  /**
   * Splits triangle into 3 new triangles at corner, if corner lies in the interior. If corner lies
   * on the border, triangle and the corresponding neighbour are split into 2 new triangles each.
   * Returns false if nothing is split.
   */
  bool split(PlanarDelaunayTriangulation_Triangle* triangle,
             PlanarDelaunayTriangulation_Corner* corner);

  /**
   * Flips edge, if it is no legal edge of the Delaunay Triangulation. corner is the last inserted
   * corner of the triangulation. Returns true, if the triangulation was changed.
   */
  bool legalizeEdge(PlanarDelaunayTriangulation_Corner* corner,
                    PlanarDelaunayTriangulation_Edge* edge);

  /** The structure for searching the triangle containing a given input corner. */
  PlanarDelaunayTriangulation_TriangleGraph* searchGraph;

  /** The edges of the triangulation, where the start corner and end corner are equal. */
  jrt::Collection<PlanarDelaunayTriangulation_Edge*>* degenerateEdges;

  /** Id numbers implementing an ordering on the Edges so that they can be used in a set. */
  int32_t lastEdgeId = 0;
};

}  // namespace fr
