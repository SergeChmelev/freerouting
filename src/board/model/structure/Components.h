// Port of app.freerouting.board.model.structure.Components
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Contains the lists of components on the board. */
class Components : public virtual jrt::Object {
 public:
  // PORT NOTE (board deepCopy hook): independent deep copy including the undo/redo state.
  Components* deepCopy();
  Components();

  /** PORT NOTE (stage E, board teardown): frees the components (all versions in the undo
   * history) and the lists. Only for the components of a board that is freed as a whole. */
  void disposeAll();

  /**
   * Inserts a component into the list. The items of the component have to be inserted separately
   * into the board. If onFront is false, the component will be placed on the back side.
   */
  Component* addPrecise(const std::string& name, FloatPoint* exactLocation,
                        double rotationInDegree, bool onFront, Package* packageFront,
                        Package* packageBack, bool positionFixed,
                        const std::optional<std::string>& partNumber);

  /** As addPrecise, with an integer location (may be null). */
  Component* add(const std::string& name, Point* location, double rotationInDegree, bool onFront,
                 Package* packageFront, Package* packageBack, bool positionFixed,
                 const std::optional<std::string>& partNumber);

  /** Adds a component to this object. The component name is generated internally. */
  Component* add(Point* location, double rotation, bool onFront, Package* componentPackage);

  /** Copies original translated by translation with a generated name. */
  Component* copy(Component* original, Vector* translation, Package* componentPackage);

  /** Returns the component with the input name or null, if no such component exists. */
  Component* get(const std::string& name);

  /** Returns the component with the input component number. */
  Component* get(int32_t componentId);

  /** Returns the number of components on the board. */
  int32_t count();

  /** Returns all components (Java: Iterable<Component> over the internal Vector). */
  jrt::Collection<Component*>* getAll();

  /** Generates a snapshot for the undo algorithm. */
  void generateSnapshot();

  /** Removes the top snapshot from the undo stack. */
  bool popSnapshot();

  /** Restores the situation at the previous snapshot. Returns false, if no more undo is possible. */
  bool undo(BoardObservers* observers);

  /** Restores the situation before the last undo. Returns false, if no more redo is possible. */
  bool redo(BoardObservers* observers);

  /** Moves the component with number componentId by vector. */
  void move(int32_t componentId, Vector* vector);

  /** Turns the component with number componentId by factor times 90 degree around pole. */
  void turn90Degree(int32_t componentId, int32_t factor, IntPoint* pole);

  /** Rotates the component with number componentId by rotationInDegree around pole. */
  void rotate(int32_t componentId, double rotationInDegree, IntPoint* pole);

  /** Changes the placement side of the component with number componentId. */
  void changeSide(int32_t componentId, IntPoint* pole);

  /** If true, components on the back side are rotated before mirroring, else mirrored first. */
  bool getFlipStyleRotateFirst() { return flipStyleRotateFirst; }

  /** Sets the flip style (see getFlipStyleRotateFirst). */
  void setFlipStyleRotateFirst(bool value) { flipStyleRotateFirst = value; }

 private:
  /** Restores the component array from the undo list (in undo list order). */
  void restoreComponentArrFromUndoList(BoardObservers* observers);

  UndoableObjects* undoList;
  /** Java: Vector. */
  jrt::ArrayList<Component*>* componentArr;

  /** If true, components on the back side are rotated before mirroring, else mirrored first. */
  bool flipStyleRotateFirst = false;
};

}  // namespace fr
