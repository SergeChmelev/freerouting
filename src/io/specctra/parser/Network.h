#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** A class-pair clearance rule collected for ordered application (private Java record). */
class Network_ClassPairRule : public virtual jrt::Object {
 public:
  Network_ClassPairRule(fr::NetClass* firstClass, fr::NetClass* secondClass,
                        Rule_ClearanceRule* clearance, int32_t layerIndex)
      : firstClass_(firstClass),
        secondClass_(secondClass),
        clearance_(clearance),
        layerIndex_(layerIndex) {}

  fr::NetClass* firstClass() { return firstClass_; }
  fr::NetClass* secondClass() { return secondClass_; }
  Rule_ClearanceRule* clearance() { return clearance_; }
  int32_t layerIndex() { return layerIndex_; }

  int32_t specificity();

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  fr::NetClass* firstClass_;
  fr::NetClass* secondClass_;
  Rule_ClearanceRule* clearance_;
  int32_t layerIndex_;
};

/** A component placement with its library name (local Java record in insertComponents). */
class Network_PlacedComponent : public virtual jrt::Object {
 public:
  Network_PlacedComponent(std::string libraryName, ComponentPlacement_ComponentLocation* location)
      : libraryName_(std::move(libraryName)), location_(location) {}

  std::string libraryName() { return libraryName_; }
  ComponentPlacement_ComponentLocation* location() { return location_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  std::string libraryName_;
  ComponentPlacement_ComponentLocation* location_;
};

/** Class for reading and writing net network from dsn-files. */
class Network : public ScopeKeyword {
 public:
  /** Creates a new instance of Network. */
  Network() : ScopeKeyword("network") {}

  /** Throws jrt::IOException. */
  static void writeScope(WriteScopeParameter* scopeParameter);

  static void writeViaInfos(fr::BoardRules* rules, fr::IndentFileWriter* file,
                            fr::IdentifierType* identifierType);

  static void writeViaRules(fr::BoardRules* rules, fr::IndentFileWriter* file,
                            fr::IdentifierType* identifierType);

  static void writeNetClasses(WriteScopeParameter* scopeParameter);

  static void writeNetClass(fr::NetClass* netClass, WriteScopeParameter* scopeParameter);

  static fr::ViaInfo* readViaInfo(IJFlexScanner* scanner, fr::BasicBoard* board);

  static jrt::Collection<std::string>* readViaRule(IJFlexScanner* scanner, fr::BasicBoard* board);

  /** Inserts a via rule into the board. Replaces an already existing via rule with the same name. */
  static bool addViaRule(jrt::Collection<std::string>* nameList, fr::BasicBoard* board);

  static void insertNetClass(NetClass* netClass, LayerStructure* layerStructure,
                             fr::BasicBoard* board,
                             fr::io::CoordinateTransform* coordinateTransform,
                             bool viaAtSmdAllowed);

  bool readScope(ReadScopeParameter* scopeParameter) override;

 private:
  /** The Specctra item classes that can get a clearance class separate from the wires. */
  static jrt::List<std::string>* NON_WIRE_ITEM_CLASS_NAMES();

  static void writeCircuit(fr::NetClass* netClass, WriteScopeParameter* scopeParameter);

  /** Creates a sequence of subnets with 2 pins from pinList. */
  static jrt::Collection<jrt::Collection<Net_Pin*>*>* createOrderedSubnets(
      jrt::Collection<Net_Pin*>* pinList);

  static bool readNetPins(IJFlexScanner* scanner, jrt::Collection<Net_Pin*>* pinList);

  static void insertViaInfos(jrt::Collection<fr::ViaInfo*>* viaInfos, fr::RoutingBoard* board,
                             bool attachAllowed);

  static void createDefaultViaInfos(fr::BasicBoard* board, fr::NetClass* netClass,
                                    bool attachAllowed);

  static void insertViaRules(jrt::Collection<jrt::Collection<std::string>*>* viaRules,
                             fr::BasicBoard* board);

  static void insertNetClasses(jrt::Collection<NetClass*>* netClasses,
                               ReadScopeParameter* scopeParameter);

  static void insertClassPairs(jrt::Collection<NetClass_ClassClass*>* classClasses,
                               ReadScopeParameter* scopeParameter);

  static void prepareClassPairClearances(fr::BasicBoard* board, fr::NetClass* netClass);

  static int32_t copyPrivateClearanceClass(fr::ClearanceMatrix* matrix, const std::string& name,
                                           int32_t source);

  static int32_t copyClearanceClass(fr::ClearanceMatrix* matrix, const std::string& name,
                                    int32_t source);

  static void collectClassPairRules(NetClass_ClassClass* classClass, fr::NetClass* firstClass,
                                    fr::NetClass* secondClass, fr::BasicBoard* board,
                                    jrt::List<Network_ClassPairRule*>* rules);

  /** PORT NOTE: type is nullable in Java (null = all item classes) -> std::optional. */
  static jrt::Set<int32_t>* effectiveClearanceClasses(
      fr::NetClass* netClass, std::optional<fr::DefaultItemClearanceClasses_ItemClass> type);

  static jrt::Set<int32_t>* mixedClearanceClasses(fr::ClearanceMatrix* matrix,
                                                  fr::NetClass* netClass,
                                                  const std::string& itemName);

  static void setMixedClearance(fr::ClearanceMatrix* matrix, jrt::Set<int32_t>* firstClasses,
                                jrt::Set<int32_t>* secondClasses, int32_t layer, int32_t value);

  static void addMixedClearanceRule(fr::ClearanceMatrix* clearanceMatrix, fr::NetClass* firstClass,
                                    fr::NetClass* secondClass, Rule_ClearanceRule* clearanceRule,
                                    int32_t layerIndex,
                                    fr::io::CoordinateTransform* coordinateTransform);

  static void createDefaultClearanceClasses(fr::NetClass* netClass,
                                            fr::ClearanceMatrix* clearanceMatrix);

  static void createViaRule(jrt::Collection<std::string>* useVia, fr::NetClass* netClass,
                            fr::BasicBoard* board, bool attachAllowed);

  static void createActiveTraceLayers(jrt::Collection<std::string>* useLayer,
                                      LayerStructure* layerStructure, fr::NetClass* netClass);

  static void addClearanceRule(fr::BoardRules* rules, fr::NetClass* netClass,
                               Rule_ClearanceRule* rule, int32_t layerIndex,
                               fr::io::CoordinateTransform* coordinateTransform);

  /**
   * Returns the clearance class of netClass for the Specctra item class itemClassName, creating a
   * new class if necessary.
   */
  static int32_t getClearanceClass(fr::ClearanceMatrix* clearanceMatrix, fr::NetClass* netClass,
                                   const std::string& itemClassName);

  static void insertComponents(ReadScopeParameter* scopeParameter);

  /** Inserts the logical parts into the board. Returns false on error. */
  static bool insertLogicalParts(ReadScopeParameter* scopeParameter);

  /** Searches the library package of partName via the logical part mappings (null if none). */
  static fr::Package* searchLibPackage(
      const std::string& partName,
      jrt::Collection<PartLibrary_LogicalPartMapping*>* logicalPartMappings,
      fr::BasicBoard* board);

  /** Inserts all board components belonging to the input library component. */
  static void insertComponent(ComponentPlacement_ComponentLocation* location,
                              const std::string& libKey, ReadScopeParameter* scopeParameter);

  bool readNetScope(IJFlexScanner* scanner, NetList* netList, fr::RoutingBoard* board,
                    fr::io::CoordinateTransform* coordinateTransform,
                    LayerStructure* layerStructure);
};

}  // namespace fr::dsn
