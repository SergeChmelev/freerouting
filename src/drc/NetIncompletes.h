#pragma once

#include "datastructures/PlanarDelaunayTriangulation.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Wrapper for an Item used in the Delaunay triangulation, including its connected set.
 * (Java private static class NetIncompletes.NetItem; members public for the enclosing class.)
 */
class NetIncompletes_NetItem : public virtual jrt::Object,
                               public virtual PlanarDelaunayTriangulation_Storable {
 public:
  Item* item;
  jrt::Collection<Item*>* connectedSet;

  NetIncompletes_NetItem(Item* item, jrt::Collection<Item*>* connectedSet)
      : item(item), connectedSet(connectedSet) {}

  jrt::Array<Point*>* getTriangulationCorners() override;
};

/**
 * Represents a potential edge between two NetItems in the Delaunay triangulation. Sortable by
 * length (Comparable<Edge>) to find the shortest connections (MST-like approach).
 * (Java private static final class NetIncompletes.Edge)
 */
class NetIncompletes_Edge final : public virtual jrt::Object {
 public:
  NetIncompletes_NetItem* fromItem;
  FloatPoint* fromCorner;
  NetIncompletes_NetItem* toItem;
  FloatPoint* toCorner;
  double lengthSquare;

  // Java constructor is private (only NetIncompletes creates edges).
  NetIncompletes_Edge(NetIncompletes_NetItem* fromItem, FloatPoint* fromCorner,
                      NetIncompletes_NetItem* toItem, FloatPoint* toCorner);

  int32_t compareTo(NetIncompletes_Edge* other);
};

/**
 * Computes and holds the incomplete connections (ratsnest) for a single net, using a Delaunay
 * triangulation + Kruskal-like MST over the connected groups of the net. Also tracks length
 * violations if the net has length constraints.
 */
class NetIncompletes : public virtual jrt::Object {
 public:
  /** Collection of AirLine representing the incomplete connections. (Java package-private) */
  jrt::Collection<AirLine*>* incompletes;

 private:
  /** The net for which the incompletes are calculated. */
  Net* net;

  /** The radius of the markers drawn at the ends of airlines or layer changes. */
  double drawMarkerRadius;

  /**
   * The length of the violation of the length restriction of the net. > 0: too long, < 0: too
   * short, 0: ok or no restriction.
   */
  double lengthViolation = 0;

  /** Number of connected groups in this net at calculation time. */
  int32_t connectedGroupCount = 0;

 public:
  /** Calculates the incomplete connections (ratsnest) for the given net items. */
  NetIncompletes(int32_t netNumber, jrt::Collection<Item*>* netItems, BasicBoard* board);

  /** Returns the collection of airlines (incomplete connections) for this net. */
  virtual jrt::Collection<AirLine*>* getIncompletes() { return this->incompletes; }

  /** Returns the net associated with these incompletes. */
  virtual Net* getNet() { return this->net; }

  /** Returns the radius used for drawing markers (e.g., layer changes). */
  virtual double getMarkerRadius() { return this->drawMarkerRadius; }

  /** Returns the number of incompletes/airlines of this net. */
  virtual int32_t count() { return incompletes->size(); }

  /** Returns the number of connected groups used to compute airlines. */
  virtual int32_t getConnectedGroupCount() { return this->connectedGroupCount; }

  /**
   * Recalculates the length violations. Return false, if the length violation has not changed.
   * (Java package-private)
   */
  virtual bool calcLengthViolation();

  /** Returns the length violation: positive if too long, negative if too short, 0 if valid. */
  virtual double getLengthViolation() { return this->lengthViolation; }

 private:
  /**
   * Calculates an array of Item-connectedSet pairs for the items of this net, grouping
   * physically connected items into the same connected set (ordered by item id).
   */
  jrt::Array<NetIncompletes_NetItem*>* calculateNetItems(jrt::Collection<Item*>* itemList);

  /**
   * Joins fromConnectedSet to toConnectedSet and updates the connected sets of the items in
   * netItems (Kruskal merge step).
   */
  void joinConnectedSets(jrt::Array<NetIncompletes_NetItem*>* netItems,
                         jrt::Collection<Item*>* fromConnectedSet,
                         jrt::Collection<Item*>* toConnectedSet);
};

}  // namespace fr
