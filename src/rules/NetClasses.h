// Port of app.freerouting.rules.NetClasses
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Contains the array of net classes for interactive and autorouting. */
class NetClasses : public virtual jrt::Object {
 public:
  NetClasses();

  /** Returns the number of classes in this array. */
  int32_t count();

  /** Returns the net class with the input index. */
  NetClass* get(int32_t index);

  /** Returns the net class with the input name or null, if no such class exists. */
  NetClass* get(const std::string& name);

  /** Appends a new net class. */
  NetClass* append(const std::string& name, LayerStructure* layerStructure,
                   ClearanceMatrix* clearanceMatrix, bool ignoredByAutorouter);

  /** Appends a new net class with an internally created unique name ("class<n>"). */
  NetClass* append(LayerStructure* layerStructure, ClearanceMatrix* clearanceMatrix);

  /**
   * Looks for a net class with trace half width traceHalfWidth on all layers, clearance class
   * traceClearanceClass and via rule viaRule. Returns null, if not found.
   */
  NetClass* find(int32_t traceHalfWidth, int32_t traceClearanceClass, ViaRule* viaRule);

  /** As find, with the per-layer trace half widths traceHalfWidthArr. */
  NetClass* find(jrt::Array<int32_t>* traceHalfWidthArr, int32_t traceClearanceClass,
                 ViaRule* viaRule);

  /** Removes the net class from this array. Returns false if it was not found. */
  bool remove(NetClass* netClass);

 private:
  friend class BoardRules;  // PORT NOTE: BoardRules::deepCopy fills the copied class list

  /** Java: Vector. */
  jrt::ArrayList<NetClass*>* classArr;
};

}  // namespace fr
