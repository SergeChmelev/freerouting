#pragma once

#include "fwd.h"
#include "io/specctra/parser/BoardParserCallback.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * Minimal internal shim: satisfies the BoardParserCallback contract during parsing without
 * requiring a HeadlessBoardManager or a RoutingJob (private static nested class in Java).
 */
class ReadScopeParameter_MinimalBoardManager final : public virtual BoardParserCallback {
 public:
  fr::RoutingBoard* getRoutingBoard() override { return board; }

  void createBoard(fr::IntBox* boundingBox, fr::LayerStructure* layerStructure,
                   jrt::Array<fr::PolylineShape*>* outlineShapes,
                   const std::optional<std::string>& outlineClearanceClassName,
                   fr::BoardRules* rules, fr::Communication* boardCommunication) override;

  fr::RoutingJob* getCurrentRoutingJob() override { return nullptr; }

 private:
  fr::RoutingBoard* board = nullptr;
};

/** Information for inserting a plane. */
class ReadScopeParameter_PlaneInfo : public virtual jrt::Object {
 public:
  Shape_ReadAreaScopeResult* area;
  std::string netName;

  ReadScopeParameter_PlaneInfo(Shape_ReadAreaScopeResult* area, const std::string& netName)
      : area(area), netName(netName) {}
};

/**
 * Helper class that contains some structured properties and helper functions for the DSN parser.
 * PORT NOTE: Java package-private members are public in C++.
 */
class ReadScopeParameter : public virtual jrt::Object {
 public:
  IJFlexScanner* scanner;
  BoardParserCallback* boardHandling;
  /** = new NetList() (initialized in the constructor). */
  NetList* netlist;
  BoardObservers* observers;
  IdGenerator* idGenerator;
  bool canonicalPlacementOrder;

  /**
   * Warnings collected during DSN parsing (e.g. skipped wires, missing padstacks, degenerate
   * geometry). Callers can retrieve these via getWarnings() after the read completes.
   */
  jrt::List<std::string>* warnings = new jrt::ArrayList<std::string>();

  /**
   * Collection of elements of class PlaneInfo. The plane cannot be inserted directly into the
   * boards, because the layers may not be read completely.
   */
  jrt::Collection<ReadScopeParameter_PlaneInfo*>* planeList =
      new jrt::LinkedList<ReadScopeParameter_PlaneInfo*>();

  /**
   * Nets of the (wire (polygon ...)) copper areas of the wiring scope, by layer name, found before
   * the structure scope is read (see PolygonWireScan). Empty when not scanned.
   * PORT NOTE: Java Map.of() (immutable empty map) -> empty HashMap.
   */
  jrt::Map<std::string, jrt::Set<std::string>*>* polygonWireNetsByLayer =
      new jrt::HashMap<std::string, jrt::Set<std::string>*>();

  /**
   * False when the wiring scope is never read (metadata-only reads): power layers whose copper
   * comes as wire polygons cannot be told apart from empty ones, so layer types are not final.
   */
  bool wiringIsRead = true;

  /** Planes whose inherited clearance must follow their net class after network rules are read. */
  /** = new ArrayList<>() (initialized in the constructor: element type incomplete here). */
  jrt::Collection<fr::ConductionArea*>* planesWithInheritedClearance;

  /**
   * Component placement information. It is filled while reading the placement scope and can be
   * evaluated after reading the library and network scope.
   */
  /** = new LinkedList<>() (initialized in the constructor). */
  jrt::Collection<ComponentPlacement*>* placementList;

  jrt::Collection<jrt::Array<std::string>*>* constants =
      new jrt::LinkedList<jrt::Array<std::string>*>();

  /**
   * The names of the via padstacks filled while reading the structure scope and evaluated after
   * reading the library scope.
   */
  jrt::Collection<std::string>* viaPadstackNames = nullptr;

  bool viaAtSmdAllowed = false;
  /** = AngleRestriction.FORTYFIVE_DEGREE (initialized in the constructor). */
  fr::AngleRestriction snapAngle;

  /** The logical parts are used for pin and gate swap. */
  /** = new LinkedList<>() (initialized in the constructor). */
  jrt::Collection<PartLibrary_LogicalPartMapping*>* logicalPartMappings;

  /** = new LinkedList<>() (initialized in the constructor). */
  jrt::Collection<PartLibrary_LogicalPart*>* logicalParts;

  /** The following objects are from the parser scope. */
  std::string stringQuote = "\"";

  /** Nullable in Java. */
  std::optional<std::string> hostCad;
  /** Nullable in Java. */
  std::optional<std::string> hostVersion;

  bool dsnFileGeneratedByHost = true;

  /** Set to false by the structure reader when the board outline is absent. */
  bool boardOutlineOk = true;

  fr::Communication_SpecctraParserInfo_WriteResolution* writeResolution = nullptr;

  /** The following objects will be initialised when the structure scope is read. */
  fr::io::CoordinateTransform* coordinateTransform = nullptr;

  LayerStructure* layerStructure = nullptr;

  /** Nullable: only populated when an (autoroute ...) scope is present in the DSN file. */
  fr::RouterSettings* autorouteSettings = nullptr;

  /** = Unit.MIL (initialized in the constructor). */
  fr::Unit unit;
  int32_t resolution = 100;  // default resolution

  /**
   * Creates a new instance of ReadScopeParameter without an external board manager. An internal
   * minimal shim is constructed to receive the parsed board. observers / idGenerator are nullable.
   */
  ReadScopeParameter(IJFlexScanner* scanner, BoardObservers* observers, IdGenerator* idGenerator);

  /** Creates a parser scope with an explicit placement-allocation policy. */
  ReadScopeParameter(IJFlexScanner* scanner, BoardObservers* observers, IdGenerator* idGenerator,
                     bool canonicalPlacementOrder);

  /**
   * Returns the board that was created during parsing, or null if parsing has not yet reached the
   * board-construction step.
   */
  virtual fr::BasicBoard* getBoard();

  /**
   * Returns the warnings collected during DSN parsing.
   * PORT NOTE: Java returns Collections.unmodifiableList(warnings); C++ returns the list itself.
   */
  virtual jrt::List<std::string>* getWarnings() { return warnings; }
};

}  // namespace fr::dsn
