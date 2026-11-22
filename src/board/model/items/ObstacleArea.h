// Port of app.freerouting.board.model.items.ObstacleArea.
#pragma once

#include "board/model/items/Item.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** An item on the board with a relativeArea shape, for example keepout, conduction relativeArea. */
class ObstacleArea : public Item {
 public:
  /**
   * The name of this ObstacleArea, which is null (nullopt), if the ObstacleArea does not belong to
   * a component.
   */
  std::optional<std::string> name;  // final

 private:
  Area* relativeArea;  // final

  /** The layer of this relativeArea. */
  int32_t layer;

  Area* precalculatedAbsoluteArea = nullptr;  // transient
  Vector* translation;
  double rotationInDegree;
  bool sideChanged;

 public:
  /**
   * Creates a new relativeArea item which may belong to several nets. name is null, if the
   * ObstacleArea does not belong to a component.
   */
  ObstacleArea(Area* area, int32_t layer, Vector* translation, double rotationInDegree,
               bool sideChanged, jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
               int32_t id, int32_t componentId, const std::optional<std::string>& name,
               FixedState fixedState, BasicBoard* board);

  /**
   * Creates a new relativeArea item without net. name is null, if the ObstacleArea does not belong
   * to a component.
   */
  ObstacleArea(Area* area, int32_t layer, Vector* translation, double rotationInDegree,
               bool sideChanged, int32_t clearanceClassIndex, int32_t id, int32_t groupId,
               const std::optional<std::string>& name, FixedState fixedState, BasicBoard* board);

  using Item::isObstacle;

  Item* copy(int32_t id) override;

  /** GetArea. */
  virtual Area* getArea();

  virtual Area* getRelativeArea() { return this->relativeArea; }

  bool isOnLayer(int32_t layer) override { return this->layer == layer; }

  int32_t firstLayer() override { return this->layer; }

  int32_t lastLayer() override { return this->layer; }

  virtual int32_t getLayer() { return this->layer; }

  IntBox* boundingBox() override;

  bool isObstacle(Item* other) override;

 protected:
  jrt::Array<TileShape*>* calculateTreeShapes(ShapeSearchTree* searchTree) override;

 public:
  int32_t tileShapeCount() override;

  TileShape* getTileShape(int32_t no) override;

  void translateBy(Vector* vector) override;

  void turn90Degree(int32_t factor, IntPoint* pole) override;

  void rotateApprox(double angleInDegree, FloatPoint* pole) override;

  void changePlacementSide(IntPoint* pole) override;

  bool isSelectedByFilter(ItemSelectionFilter* filter) override;

  int32_t shapeLayer(int32_t index) override { return layer; }

  virtual Vector* getTranslation() { return translation; }

  virtual double getRotationInDegree() { return rotationInDegree; }

  virtual bool getSideChanged() { return sideChanged; }

  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

 protected:
  /** Used in the implementation of print_info for this class and derived classes (final). */
  void printShapeInfo(ItemInfoPrinter* printer, const std::string& locale);

 public:
  virtual jrt::Array<TileShape*>* splitToConvex();

  void clearDerivedData() override;
};

}  // namespace fr
