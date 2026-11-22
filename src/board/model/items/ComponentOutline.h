// Port of app.freerouting.board.model.items.ComponentOutline.
#pragma once

#include "board/model/items/Item.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** ComponentOutline. */
class ComponentOutline : public Item {
 private:
  Area* relativeArea;                         // final
  Area* precalculatedAbsoluteArea = nullptr;  // transient
  Vector* translation;
  double rotationInDegree;
  // PORT NOTE: the following fields got a trailing '_' (clash with the methods of the same name).
  bool isFront_;
  bool isCourtyard_;    // final
  bool isFabrication_;  // final
  bool isClosed_;       // final

 public:
  /** Creates a new instance of ComponentOutline. */
  ComponentOutline(Area* area, bool isFront, Vector* translation, double rotationInDegree,
                   int32_t id, int32_t componentId, bool isCourtyard, bool isFabrication,
                   bool isClosed, FixedState fixedState, BasicBoard* board);

  using Item::isObstacle;

  Item* copy(int32_t id) override;

  virtual bool isFront() { return this->isFront_; }

  virtual bool isCourtyard() { return this->isCourtyard_; }

  virtual bool isFabrication() { return this->isFabrication_; }

  virtual bool isClosed() { return this->isClosed_; }

  bool isSelectedByFilter(ItemSelectionFilter* filter) override { return false; }

  /** GetLayer. */
  virtual int32_t getLayer();

  int32_t firstLayer() override { return getLayer(); }

  int32_t lastLayer() override { return getLayer(); }

  bool isOnLayer(int32_t layer) override { return getLayer() == layer; }

  bool isObstacle(Item* item) override { return false; }

  int32_t shapeLayer(int32_t index) override { return getLayer(); }

  int32_t tileShapeCount() override { return 0; }

 protected:
  jrt::Array<TileShape*>* calculateTreeShapes(ShapeSearchTree* searchTree) override {
    return new jrt::Array<TileShape*>(0);
  }

 public:
  IntBox* boundingBox() override;

  void translateBy(Vector* vector) override;

  void changePlacementSide(IntPoint* pole) override;

  void rotateApprox(double angleInDegree, FloatPoint* pole) override;

  void turn90Degree(int32_t factor, IntPoint* pole) override;

  /** Get area. */
  virtual Area* getArea();

  void clearDerivedData() override;

  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;
};

}  // namespace fr
