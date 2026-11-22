// Port of app.freerouting.rules.NetClass
#pragma once

#include "board/actions/ItemInfoPrinter.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes routing rules for individual nets. */
class NetClass : public virtual ItemInfoPrinter_Printable {
 public:
  /** Creates a new instance of NetClass. */
  NetClass(const std::string& name, LayerStructure* layerStructure,
           ClearanceMatrix* clearanceMatrix, bool ignoredByAutorouter);

  std::string toString() override { return this->name; }

  /** Gets the name of this net class. */
  std::string getName() { return this->name; }

  /** Changes the name of this net class. */
  void setName(const std::string& name) { this->name = name; }

  /** Sets the trace half width used for routing to value on all layers. */
  void setTraceHalfWidth(int32_t value);

  /** Sets the trace half width used for routing to value on the input layer. */
  void setTraceHalfWidth(int32_t layer, int32_t value);

  /** Sets the trace half width used for routing to value on all inner layers. */
  void setTraceHalfWidthOnInner(int32_t value);

  /** Returns the number of layers of this net class. */
  int32_t layerCount() { return traceHalfWidthArr->length; }

  /** Returns the half width of the traces of this class on layer (0 if out of range). */
  int32_t getTraceHalfWidth(int32_t layer);

  /** Copies all settings of other (except the name) into this net class. */
  void copySettingsFrom(NetClass* other);

  /** The index of the clearance class of traces with this net class. */
  int32_t getTraceClearanceClass() { return this->traceClearanceClass; }

  /** Sets the index of the clearance class of traces with this net class. */
  void setTraceClearanceClass(int32_t clearanceClass) {
    this->traceClearanceClass = clearanceClass;
  }

  /** Returns the via rule of this net class. */
  ViaRule* getViaRule() { return this->viaRule; }

  /** Sets the via rule of this net class. */
  void setViaRule(ViaRule* viaRule) { this->viaRule = viaRule; }

  /** Returns, if the traces and vias of this class are fixed against shoves. */
  bool isShoveFixed() { return this->shoveFixed; }

  /** Sets, if the traces and vias of this class are fixed against shoves. */
  void setShoveFixed(bool value) { this->shoveFixed = value; }

  /** Returns, if traces of this net class are pulled tight. */
  bool getPullTight() { return this->pullTight; }

  /** Sets, if traces of this net class are pulled tight. */
  void setPullTight(bool value) { this->pullTight = value; }

  /** Returns, if cycles containing conduction areas are ignored. */
  bool getIgnoreCyclesWithAreas() { return this->ignoreCyclesWithAreas; }

  /** Sets, if cycles containing conduction areas are ignored. */
  void setIgnoreCyclesWithAreas(bool value) { this->ignoreCyclesWithAreas = value; }

  /** Returns the minimum trace length of this net class (<= 0: no restriction). */
  double getMinimumTraceLength() { return minimumTraceLength; }

  /** Sets the minimum trace length of this net class. */
  void setMinimumTraceLength(double value) { minimumTraceLength = value; }

  /** Returns the maximum trace length of this net class (<= 0: no restriction). */
  double getMaximumTraceLength() { return maximumTraceLength; }

  /** Sets the maximum trace length of this net class. */
  void setMaximumTraceLength(double value) { maximumTraceLength = value; }

  /** Returns the trace length constraint of this net class (a new record). */
  NetLengthConstraint* getLengthConstraint();

  /** Sets the trace length constraint of this net class (null: unconstrained). */
  void setLengthConstraint(NetLengthConstraint* constraint);

  /** Returns the meander constraint of this net class, never null. */
  NetMeanderConstraint* getMeanderConstraint();

  /** Sets the meander constraint of this net class (null: UNCONSTRAINED). */
  void setMeanderConstraint(NetMeanderConstraint* constraint);

  /** Returns true if this net class has a meander constraint. */
  bool hasMeanderConstraint();

  /** Returns if the layer with layerNumber is active for routing this net class. */
  bool isActiveRoutingLayer(int32_t layerNumber);

  /** Sets the layer with layerNumber to active or inactive. */
  void setActiveRoutingLayer(int32_t layerNumber, bool active);

  /** Activates or deactivates all layers for routing. */
  void setAllLayersActive(bool value);

  /** Activates or deactivates all inner layers for routing. */
  void setAllInnerLayersActive(bool value);

  /** PORT NOTE: java.util.Locale -> std::string language tag. */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  /** Returns true, if the trace width of this class is not equal on all signal layers. */
  bool traceWidthIsLayerDependent();

  /** Returns true, if the trace width of this class is not equal on all inner signal layers. */
  bool traceWidthIsInnerLayerDependent();

  /** The clearance classes of the item types, if this net class comes from a class in a DSN file. */
  DefaultItemClearanceClasses* defaultItemClearanceClasses;

  /** If true, the autorouter ignores the nets of this class. */
  bool isIgnoredByAutorouter = false;

  /** If true, the nets of this class are routed first (router.autorouter.priority_net_classes). */
  bool isRoutedFirst = false;

 private:
  ClearanceMatrix* clearanceMatrix;     // final
  LayerStructure* boardLayerStructure;  // final
  jrt::Array<int32_t>* traceHalfWidthArr;
  jrt::Array<bool>* activeRoutingLayerArr;

  std::string name;
  ViaRule* viaRule = nullptr;
  int32_t traceClearanceClass = 0;

  /** If true, the traces and vias of this class are fixed against shoves. */
  bool shoveFixed = false;

  /** If true, the traces of this class are pulled tight. */
  bool pullTight = true;

  /** If true, cycles containing conduction areas are ignored. */
  bool ignoreCyclesWithAreas = false;
  double minimumTraceLength = 0;
  double maximumTraceLength = 0;
  NetMeanderConstraint* meanderConstraint;  // initialised to UNCONSTRAINED in the constructor
};

}  // namespace fr
