// Port of app.freerouting.board.model.items.ConductionArea.
#pragma once

#include "board/model/items/Connectable.h"
#include "board/model/items/ObstacleArea.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** A ObstacleArea, which can be electrically connected to other items. */
class ConductionArea : public ObstacleArea, public virtual Connectable {
 private:
  static constexpr double PLANE_HATCH_OPACITY = 0.85;

  /** PORT NOTE: renamed from isObstacle (clashes with method isObstacle(Item)). */
  bool isObstacle_;
  // PORT NOTE (board deep copy): isFilled is not carried by copy(id) (as in Java); serialization
  // would keep it.
  bool isFilled = true;

  /** The clearance the fill of the zone keeps to foreign copper, in board units; 0 if unknown. */
  int32_t fillClearance = 0;

  /** The narrowest neck the fill of the zone keeps, in board units; 0 if unknown. */
  int32_t fillMinWidth = 0;

  /** How the fill connects the pads of the net; null (nullopt) if unknown. */
  std::optional<PadPlaneConnection_Mode> padConnection;

  /** The gap of the thermal reliefs of the fill, in board units. */
  int32_t thermalGap = 0;

  /** The width of the spokes of the thermal reliefs of the fill, in board units. */
  int32_t thermalSpokeWidth = 0;

  /** The distance the fill keeps to the edge of the board, in board units; 0 if unknown. */
  int32_t fillEdgeClearance = 0;

  /** Whether the corners of the outline are rounded before the fill (fillets). */
  bool cornerFillets = false;

  /**
   * The radius of the fillets, or the distance of the chamfers, of the corners of the outline
   * before the fill, in board units; 0 for sharp corners.
   */
  int32_t cornerSmoothing = 0;

  /** The outline with its corners smoothed, built on first use. */
  Area* smoothedArea = nullptr;  // transient

 public:
  /**
   * Sets how the host CAD system fills this area, when the design file does not say it: the
   * clearance the fill keeps to the copper of other nets, and the minimum width of the fill.
   */
  virtual void setFillRules(int32_t clearance, int32_t minWidth);

  /** The clearance set by setFillRules, in board units; 0 if unknown. */
  virtual int32_t getFillClearance() { return this->fillClearance; }

  /** The minimum width set by setFillRules, in board units; 0 if unknown. */
  virtual int32_t getFillMinWidth() { return this->fillMinWidth; }

  /**
   * Sets how the host CAD system connects the pads of the net to the fill of this area, when the
   * design file does not say it: the kind of connection, and for thermal reliefs gap and spoke
   * width. padConnection may be null (nullopt).
   */
  virtual void setPadConnectionRules(std::optional<PadPlaneConnection_Mode> padConnection,
                                     int32_t thermalGap, int32_t thermalSpokeWidth);

  /** Sets the distance the fill keeps to the edge of the board, in board units. */
  virtual void setFillEdgeClearance(int32_t edgeClearance);

  /** The distance set by setFillEdgeClearance, in board units; 0 if unknown. */
  virtual int32_t getFillEdgeClearance() { return this->fillEdgeClearance; }

  /**
   * Sets how the host CAD system smooths the corners of the outline before it fills it: fillets of
   * this radius, or chamfers at this distance from each corner, in board units.
   */
  virtual void setFillCornerSmoothing(bool fillets, int32_t distance);

  /** Whether the corners are rounded, by setFillCornerSmoothing. */
  virtual bool hasCornerFillets() { return this->cornerFillets; }

  /** The distance set by setFillCornerSmoothing, in board units; 0 for sharp corners. */
  virtual int32_t getCornerSmoothing() { return this->cornerSmoothing; }

  /**
   * The outline that the fill of the host CAD system follows: the area with its corners smoothed
   * as setFillCornerSmoothing says, or the area itself.
   */
  virtual Area* getFillOutline();

  /** The connection set by setPadConnectionRules; null (nullopt) if unknown. */
  virtual std::optional<PadPlaneConnection_Mode> getPadConnection() { return this->padConnection; }

  /** The thermal gap set by setPadConnectionRules, in board units. */
  virtual int32_t getThermalGap() { return this->thermalGap; }

  /** The spoke width set by setPadConnectionRules, in board units. */
  virtual int32_t getThermalSpokeWidth() { return this->thermalSpokeWidth; }

  /**
   * The distance from the copper of a foreign item within which the fill of this area carries no
   * current: the larger of clearance and the fill clearance, plus half the minimum fill width.
   */
  virtual double fillKeepout(double clearance) {
    return jrt::Math::max(clearance, static_cast<double>(this->fillClearance)) +
           this->fillMinWidth / 2.0;
  }

  virtual bool getIsFilled() { return this->isFilled; }

  /** SetIsFilled. */
  virtual void setIsFilled(bool value);

 private:
  int32_t cachedBoardRevision = -1;                                    // transient
  PlanarArea* cachedBoardFillArea = nullptr;                           // transient
  int32_t cachedFractureRevision = -1;                                 // transient
  int64_t cachedNetAssignmentStamp = -1;                               // transient
  FastPlaneConnectivityGrid_PlaneFractureResult* cachedFractureResult = nullptr;  // transient
  FastPlaneConnectivityGrid_FractureMemo* fractureMemo = nullptr;      // transient

 public:
  /** Creates a new instance of ConductionArea. */
  ConductionArea(Area* area, int32_t layer, Vector* translation, double rotationInDegree,
                 bool sideChanged, jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                 int32_t id, int32_t groupId, const std::optional<std::string>& name,
                 bool isObstacle, FixedState fixedState, BasicBoard* board);

  // --- PORT NOTE: Connectable final overriders forwarding to Item (see Connectable.h) ---
  bool containsNet(int32_t netNumber) override { return Item::containsNet(netNumber); }
  bool sharesNetNo(jrt::Array<int32_t>* netNumbers) override { return Item::sharesNetNo(netNumbers); }
  jrt::Set<Item*>* getAllContacts() override { return Item::getAllContacts(); }
  jrt::Set<Item*>* getAllContacts(int32_t layer) override { return Item::getAllContacts(layer); }
  using Item::getConnectedSet;
  jrt::Set<Item*>* getConnectedSet(int32_t netNumber) override {
    return Item::getConnectedSet(netNumber);
  }

  using Item::isObstacle;
  using Item::isTraceObstacle;

  void clearDerivedData() override;

  /** Pre-computes detailed plane-fill geometry (GUI paint cache). */
  virtual void warmDetailedFillCache();

  /**
   * Returns the detailed fill geometry for this conduction area, computing clearances against all
   * overlapping items if needed.
   */
  virtual PlanarArea* getDetailedFillArea();

  /** Returns the cached detailed fill geometry for renderer-owned painting. */
  virtual PlanarArea* getDetailedFillArea(double maxClearanceLookupBoard, int32_t layerIndex);

 private:
  void ensureDetailedFillCache(double maxClearanceLookupBoard, int32_t layerIndex);

 public:
  Item* copy(int32_t id) override;

  /**
   * Whether this conduction area is dead copper: a board-attached copper pour fragment that has no
   * normal contacts with any other item.
   */
  virtual bool isDeadCopper();

  jrt::Set<Item*>* getNormalContacts() override;

 private:
  /**
   * The points of object of which one must lie in a tile of this area for a contact, or null if
   * object cannot be a contact or is a pin, looked up by its shape.
   */
  jrt::Array<Point*>* contactPoints(SearchTreeObject* object);

  /**
   * Whether object is a pin of the net of this area, which is a contact when its copper overlaps a
   * tile.
   */
  bool isPinCandidate(SearchTreeObject* object);

  bool isContactInTile(SearchTreeObject* object, TileShape* tile);

  /** Whether the copper of drillItem on the layer of this area overlaps tile with a positive area. */
  bool overlapsTile(DrillItem* drillItem, TileShape* tile);

 public:
  /**
   * Whether drillItem, a pin of the net of this area, overlaps a tile of this area with a positive
   * area (package-private in Java).
   */
  virtual bool overlapsCopperOf(DrillItem* drillItem);

  /**
   * Returns true if the connected set reaches an unfractured plane of the net, and no other
   * unfractured plane of the net that carries a pin is left in the unconnected set.
   */
  static bool coversAllPlanesOfNet(jrt::Collection<Item*>* connectedSet,
                                   jrt::Collection<Item*>* unconnectedSet, int32_t netNo);

  /** Returns true if no pin of the net is reached from this conduction area. */
  virtual bool isFloatingFragment(int32_t netNo);

  /** Returns true if any island of this fractured conduction area contains a pin. */
  virtual bool hasPinOnAnyIsland() { return hasPinOnAnyIsland(0); }

  /**
   * Returns true if any island of this fractured conduction area contains a pin belonging to netNo
   * (or any pin when netNo <= 0).
   */
  virtual bool hasPinOnAnyIsland(int32_t netNo);

  /** Returns true if this fractured area has an island containing a pin not in connectedSet. */
  virtual bool hasUnreachedPinIsland(jrt::Collection<Item*>* connectedSet);

  /** Returns true if a pin of the net can be reached from this conduction area through items of the net. */
  virtual bool reachesPinOfNet(int32_t netNo);

  /** Collects one representative Pin from each island of this fractured area that contains a pin. */
  virtual void collectPinsOnFracturedIslands(jrt::Collection<Pin*>* out);

  /** Returns true if this conduction area is split into multiple isolated copper islands. */
  virtual bool isFractured();

  /**
   * Returns what a copy of this conduction area may reuse of its fracture analysis, or null.
   * Called while the board is written for a deep copy.
   */
  virtual FastPlaneConnectivityGrid_FractureMemo* fractureMemoForCopy();

  /**
   * Gives this conduction area, the deep copy of another one, the fracture memo of its original
   * (see fractureMemoForCopy).
   */
  virtual void adoptFractureMemo(FastPlaneConnectivityGrid_FractureMemo* detached);

  /** Returns the cached or freshly computed fracture analysis result for this conduction area. */
  virtual FastPlaneConnectivityGrid_PlaneFractureResult* getFractureResult();

  /** (package-private in Java) */
  void getConnectedSetRecu(jrt::Set<Item*>* result, int32_t netNumber, bool stopAtPlane) override;

  /**
   * Returns true if the first or last corner of a trace of the net touches the fill of this area.
   */
  virtual bool touchesEndOf(Trace* trace, bool first);

  /**
   * Returns true if this area joins the two ends of a trace of its net, so that the trace closes a
   * cycle with it.
   */
  virtual bool joinsEndsOf(Trace* trace);

  /**
   * Completes a connected set that passes through fractured conduction areas (package-private
   * static in Java).
   */
  static void addLaterReachedIslands(jrt::Set<Item*>* result, int32_t netNumber, bool stopAtPlane);

  /** (package-private in Java) */
  bool isCycleRecu(jrt::Set<Item*>* visitedItems, Item* searchItem, Item* comeFromItem,
                   bool ignoreAreas) override;

  TileShape* getTraceConnectionShape(ShapeSearchTree* searchTree, int32_t index) override;

  jrt::Array<Point*>* getRatsnestCorners() override;

  bool isObstacle(Item* other) override;

  /** Returns if this conduction area is regarded as obstacle to traces of foreign nets. */
  virtual bool getIsObstacle() { return this->isObstacle_; }

  /** Sets, if this conduction area is regarded as obstacle to traces and vias of foreign nets. */
  virtual void setIsObstacle(bool value);

  bool isTraceObstacle(int32_t netNumber) override {
    return this->isObstacle_ && !this->containsNet(netNumber);
  }

  bool isDrillable(int32_t netNumber) override {
    return !this->isObstacle_ || this->containsNet(netNumber);
  }

  bool isSelectedByFilter(ItemSelectionFilter* filter) override;

  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;
};

}  // namespace fr
