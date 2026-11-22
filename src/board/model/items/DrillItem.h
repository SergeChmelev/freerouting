// Port of app.freerouting.board.model.items.DrillItem (+ nested ExactCopperEntry, TraceInfo).
#pragma once

#include "board/model/items/Connectable.h"
#include "board/model/items/Item.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Cached copper per shape index, with the tree shape it was measured behind; invalidated with the
 * tree shape (clearDerivedData).
 */
class DrillItem_ExactCopperEntry : public virtual jrt::Object {
 public:
  TileShape* treeShape;  // final
  ExactCopper* copper;   // final

  DrillItem_ExactCopperEntry(TileShape* treeShape, ExactCopper* copper)
      : treeShape(treeShape), copper(copper) {}
};

/** Auxiliary class used in the method move_by (private static nested class in Java). */
class DrillItem_TraceInfo : public virtual jrt::Object {
 public:
  int32_t layer;
  int32_t halfWidth;
  int32_t clearanceClassIndex;

  DrillItem_TraceInfo(int32_t layer, int32_t halfWidth, int32_t clearanceClassIndex)
      : layer(layer), halfWidth(halfWidth), clearanceClassIndex(clearanceClassIndex) {}

  /** Implements the comparable interface (descending layer order). */
  virtual int32_t compareTo(DrillItem_TraceInfo* other) { return other->layer - this->layer; }
};

/** Common superclass for Pins and Vias. */
class DrillItem : public Item, public virtual Connectable {
 private:
  /** The center point of the drillitem. */
  Point* center;

  /** Precalculated minimal width of the shapes on all layers; < 0 if not yet calculated. */
  double precalculatedMinWidth = -1;

  /** Precalculated first layer containing a pad shape; < 0 if not yet calculated. */
  int32_t precalculatedFirstLayer = -1;

  /** Precalculated last layer containing a pad shape; < 0 if not yet calculated. */
  int32_t precalculatedLastLayer = -1;

 protected:
  /** DrillItem. */
  DrillItem(Point* center, jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
            int32_t id, int32_t groupId, FixedState fixedState, BasicBoard* board);

 public:
  // --- PORT NOTE: Connectable final overriders forwarding to Item (see Connectable.h) ---
  bool containsNet(int32_t netNumber) override { return Item::containsNet(netNumber); }
  bool sharesNetNo(jrt::Array<int32_t>* netNumbers) override { return Item::sharesNetNo(netNumbers); }
  jrt::Set<Item*>* getAllContacts() override { return Item::getAllContacts(); }
  jrt::Set<Item*>* getAllContacts(int32_t layer) override { return Item::getAllContacts(layer); }
  using Item::getConnectedSet;
  jrt::Set<Item*>* getConnectedSet(int32_t netNumber) override {
    return Item::getConnectedSet(netNumber);
  }

  /** Works only for symmetric DrillItems. */
  void translateBy(Vector* vector) override;

  void turn90Degree(int32_t factor, IntPoint* pole) override;

  void rotateApprox(double angleInDegree, FloatPoint* pole) override;

  void changePlacementSide(IntPoint* pole) override;

  void moveBy(Vector* vector) override;

  int32_t shapeLayer(int32_t index) override;

  bool isOnLayer(int32_t layer) override { return layer >= firstLayer() && layer <= lastLayer(); }

  int32_t firstLayer() override;

  int32_t lastLayer() override;

  /** Get shape. */
  virtual Shape* getShape(int32_t index) = 0;

  IntBox* boundingBox() override;

  int32_t tileShapeCount() override;

 protected:
  jrt::Array<TileShape*>* calculateTreeShapes(ShapeSearchTree* searchTree) override;

 public:
  /** Returns the smallest distance from the center to the border of the shape on any layer. */
  virtual double smallestRadius();

  /** Returns the center point of this DrillItem. */
  virtual Point* getCenter() { return center; }

 protected:
  virtual void setCenter(Point* center);

 public:
  /** Returns the padstack of this drillitem. */
  virtual Padstack* getPadstack() = 0;

  /** Get tree shape on layer. */
  virtual TileShape* getTreeShapeOnLayer(ShapeSearchTree* tree, int32_t layer);

  /** Returns the tile shape on the given layer. */
  virtual TileShape* getTileShapeOnLayer(int32_t layer);

  /** Returns the shape on the given layer. */
  virtual Shape* getShapeOnLayer(int32_t layer);

  /** Exact copper containment, independent of clearance-compensated search shapes. */
  virtual bool containsCopper(Point* point, int32_t layer);

  jrt::Set<Item*>* getNormalContacts() override;

 private:
  /** Checks if two points are within the specified tolerance (Manhattan distance). */
  bool isWithinTolerance(Point* p1, Point* p2, int32_t tolerance);

 public:
  Point* normalContactPoint(Item* other) override;

  /** (package-private in Java) */
  Point* normalContactPoint(DrillItem* other) override;

  /** (package-private in Java) */
  Point* normalContactPoint(Trace* trace) override;

  jrt::Array<Point*>* getRatsnestCorners() override;

  TileShape* getTraceConnectionShape(ShapeSearchTree* searchTree, int32_t index) override;

  /** False, if this drillitem is places on the back side of the board. */
  virtual bool isPlacedOnFront() { return true; }

  /** Return the minimal width of the shapes of this DrillItem on all signal layers. */
  virtual double minWidth();

  void clearDerivedData() override;

  /** Cached exact copper per shape index (transient in Java). */
  jrt::Array<DrillItem_ExactCopperEntry*>* exactCopperCache = nullptr;
};

}  // namespace fr
