#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** A Specctra class_class scope: rules between the listed net classes. */
class NetClass_ClassClass : public virtual jrt::Object {
 public:
  jrt::Collection<std::string>* classNames;
  jrt::Collection<Rule*>* rules;
  jrt::Collection<Rule_LayerRule*>* layerRules;

  NetClass_ClassClass(jrt::Collection<std::string>* classNames, jrt::Collection<Rule*>* rules,
                      jrt::Collection<Rule_LayerRule*>* layerRules)
      : classNames(classNames), rules(rules), layerRules(layerRules) {}
};

/**
 * Contains the information of a Specctra Class scope.
 * PORT NOTE: traceClearanceClass and viaRule are nullable in Java -> std::optional.
 */
class NetClass : public virtual jrt::Object {
 public:
  std::string name;
  std::optional<std::string> traceClearanceClass;
  jrt::Collection<std::string>* netList;
  jrt::Collection<Rule*>* rules;
  jrt::Collection<Rule_LayerRule*>* layerRules;
  jrt::Collection<std::string>* useVia;
  jrt::Collection<std::string>* useLayer;
  std::optional<std::string> viaRule;
  bool shoveFixed;
  bool pullTight;
  double minTraceLength;
  double maxTraceLength;
  /** Nullable: only present when the class has a circuit scope. */
  Circuit_ReadScopeResult* circuit;

  /** Creates a new instance of NetClass without circuit scope. */
  NetClass(const std::string& name, const std::optional<std::string>& traceClearanceClass,
           jrt::Collection<std::string>* netList, jrt::Collection<Rule*>* rules,
           jrt::Collection<Rule_LayerRule*>* layerRules, jrt::Collection<std::string>* useVia,
           jrt::Collection<std::string>* useLayer, const std::optional<std::string>& viaRule,
           bool shoveFixed, bool pullTight, double minTraceLength, double maxTraceLength)
      : NetClass(name, traceClearanceClass, netList, rules, layerRules, useVia, useLayer, viaRule,
                 shoveFixed, pullTight, minTraceLength, maxTraceLength, nullptr) {}

  /** Creates a new instance of NetClass with circuit scope. */
  NetClass(const std::string& name, const std::optional<std::string>& traceClearanceClass,
           jrt::Collection<std::string>* netList, jrt::Collection<Rule*>* rules,
           jrt::Collection<Rule_LayerRule*>* layerRules, jrt::Collection<std::string>* useVia,
           jrt::Collection<std::string>* useLayer, const std::optional<std::string>& viaRule,
           bool shoveFixed, bool pullTight, double minTraceLength, double maxTraceLength,
           Circuit_ReadScopeResult* circuit)
      : name(name),
        traceClearanceClass(traceClearanceClass),
        netList(netList),
        rules(rules),
        layerRules(layerRules),
        useVia(useVia),
        useLayer(useLayer),
        viaRule(viaRule),
        shoveFixed(shoveFixed),
        pullTight(pullTight),
        minTraceLength(minTraceLength),
        maxTraceLength(maxTraceLength),
        circuit(circuit) {}

  /** Reads a class scope (null on error). */
  static NetClass* readScope(IJFlexScanner* scanner);

  /** Reads a class_class scope (null on error). */
  static NetClass_ClassClass* readClassClassScope(IJFlexScanner* scanner);
};

}  // namespace fr::dsn
