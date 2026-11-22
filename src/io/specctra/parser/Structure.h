#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Kinds of keepouts in a structure / image scope. */
enum class Structure_KeepoutType : int32_t { keepout, via_keepout, wire_keepout, place_keepout };

inline std::string Structure_KeepoutType_name(Structure_KeepoutType e) {
  switch (e) {
    case Structure_KeepoutType::keepout: return "keepout";
    case Structure_KeepoutType::via_keepout: return "via_keepout";
    case Structure_KeepoutType::wire_keepout: return "wire_keepout";
    case Structure_KeepoutType::place_keepout: return "place_keepout";
  }
  return "";
}

inline std::string Structure_KeepoutType_toString(Structure_KeepoutType e) {
  return Structure_KeepoutType_name(e);
}

inline jrt::Array<Structure_KeepoutType>* Structure_KeepoutType_values() {
  return jrt::Array<Structure_KeepoutType>::of(
      {Structure_KeepoutType::keepout, Structure_KeepoutType::via_keepout,
       Structure_KeepoutType::wire_keepout, Structure_KeepoutType::place_keepout});
}

inline Structure_KeepoutType Structure_KeepoutType_valueOf(const std::string& name) {
  for (Structure_KeepoutType e : *Structure_KeepoutType_values()) {
    if (Structure_KeepoutType_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.io.specctra.parser.Structure.KeepoutType." + name);
}

/** Collected information for constructing the board (private Java nested class). */
class Structure_BoardConstructionInfo : public virtual jrt::Object {
 public:
  /**
   * Initializes layerInfo, outlineShapes, defaultRules and layerDependentRules with new
   * LinkedLists (defined in the .cpp: the element types are incomplete here).
   */
  Structure_BoardConstructionInfo();

  jrt::Collection<Layer*>* layerInfo;
  Shape* boundingShape = nullptr;
  jrt::List<Shape*>* outlineShapes;
  /** Nullable in Java. */
  std::optional<std::string> outlineClearanceClassName;
  int32_t foundLayerCount = 0;
  jrt::Collection<Rule*>* defaultRules;
  jrt::Collection<Structure_LayerRule*>* layerDependentRules;
};

/** Rules belonging to a named layer (private Java nested class). */
class Structure_LayerRule : public virtual jrt::Object {
 public:
  Structure_LayerRule(const std::string& layerName, jrt::Collection<Rule*>* rule)
      : layerName(layerName), rule(rule) {}

  std::string layerName;
  jrt::Collection<Rule*>* rule;
};

/** Used to separate the holes in the outline (private Java nested class). */
class Structure_OutlineShape : public virtual jrt::Object {
 public:
  explicit Structure_OutlineShape(fr::PolylineShape* shape);

  fr::PolylineShape* shape;
  fr::IntBox* boundingBox;
  jrt::Array<fr::TileShape*>* convexShapes;
  bool isHole = false;

  /** Returns true, if this shape contains all corners of otherShape. */
  bool containsAllCorners(Structure_OutlineShape* otherShape);
};

/** Class for reading and writing structure scopes from dsn-files. */
class Structure : public ScopeKeyword {
 public:
  /** Creates a new instance of Structure. */
  Structure() : ScopeKeyword("structure") {}

  /** Throws jrt::IOException. */
  static void writeScope(WriteScopeParameter* scopeParameter);

  static void writeLayers(WriteScopeParameter* scopeParameter);

  static void writeDefaultRules(WriteScopeParameter* scopeParameter);

  static bool readLayerScope(IJFlexScanner* scanner,
                             Structure_BoardConstructionInfo* boardConstructionInfo,
                             const std::string& stringQuote);

  static jrt::Collection<std::string>* readViaPadstacks(IJFlexScanner* scanner);

  /** Returns the snap angle, or null (nullopt) on error. */
  static std::optional<fr::AngleRestriction> readSnapAngle(IJFlexScanner* scanner);

  static void writeSnapAngle(fr::IndentFileWriter* file, fr::AngleRestriction angleRestriction);

  /** Sets the clearance rule into the board rules. Returns false on error. */
  static bool setClearanceRule(Rule_ClearanceRule* rule, int32_t layerIndex,
                               fr::io::CoordinateTransform* coordinateTransform,
                               fr::BoardRules* boardRules, const std::string& stringQuote);

  static bool containsWireClearancePair(jrt::Collection<std::string>* clearancePairs);

  bool readScope(ReadScopeParameter* scopeParameter) override;

 private:
  friend class Structure_OutlineShape;

  static void writeConductionAreas(WriteScopeParameter* scopeParameter);

  static void writeKeepouts(WriteScopeParameter* scopeParameter);

  static void writeBoundaries(WriteScopeParameter* scopeParameter);

  static void writeViaPadstacks(fr::BoardLibrary* library, fr::IndentFileWriter* file,
                                fr::IdentifierType* identifierType);

  static void writeControlScope(fr::BoardRules* rules, fr::IndentFileWriter* file);

  static void writeKeepoutScope(WriteScopeParameter* scopeParameter, fr::ObstacleArea* keepout);

  static bool readBoundaryScope(IJFlexScanner* scanner,
                                Structure_BoardConstructionInfo* boardConstructionInfo);

  static void addBoundaryShape(Structure_BoardConstructionInfo* boardConstructionInfo,
                               Shape* shape);

  static bool readControlScope(ReadScopeParameter* scopeParameter);

  static void insertMissingPowerPlanes(jrt::Collection<Layer*>* layerInfo, NetList* netlist,
                                       fr::BasicBoard* board,
                                       jrt::Set<std::string>* layersWithWireCopper);

  /** Separates the holes in the outline shapes. */
  static jrt::Collection<fr::PolylineShape*>* separateHoles(
      jrt::Collection<fr::PolylineShape*>* outlineShapes);

  /** Updates the board rules from the rules read from the dsn file. */
  static void updateBoardRules(ReadScopeParameter* scopeParameter,
                               Structure_BoardConstructionInfo* boardConstructionInfo,
                               fr::BoardRules* boardRules);

  static void createDefaultClearanceClasses(fr::BoardRules* boardRules);

  static int32_t appendClearanceClass(fr::BoardRules* boardRules, const std::string& name);

  /** Returns true, if all clearance values on the 2 input layers are equal. */
  static bool clearanceEquals(fr::ClearanceMatrix* clMatrix, int32_t layer1, int32_t layer2);

  static bool insertKeepout(Shape_ReadAreaScopeResult* area, ReadScopeParameter* scopeParameter,
                            Structure_KeepoutType keepoutType, fr::FixedState fixedState);

  static void insertKeepout(fr::BasicBoard* board, fr::Area* area, int32_t layer,
                            const std::optional<std::string>& clearanceClassName,
                            Structure_KeepoutType keepoutType, fr::FixedState fixedState,
                            const std::optional<std::string>& name);

  /**
   * Turns (type power) layers into signal layers when the DSN file does not define any plane for
   * them (neither a (plane ...) scope nor a (use_net ...) declaration on the layer).
   */
  static void promotePowerLayersWithoutPlane(
      ReadScopeParameter* scopeParameter, Structure_BoardConstructionInfo* boardConstructionInfo);

  bool createBoard(ReadScopeParameter* scopeParameter,
                   Structure_BoardConstructionInfo* boardConstructionInfo);
};

}  // namespace fr::dsn
