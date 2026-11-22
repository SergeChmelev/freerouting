#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * A logical part mapping (name + sorted component names).
 * PORT NOTE: the Java constructor is private (only PartLibrary creates instances); kept public in
 * C++ since the hoisted type cannot see its outer class' privileges otherwise.
 */
class PartLibrary_LogicalPartMapping final : public virtual jrt::Object {
 public:
  PartLibrary_LogicalPartMapping(const std::string& name, jrt::TreeSet<std::string>* components)
      : name(name), components(components) {}

  /** The name of the mapping. */
  std::string name;

  /** The components belonging to the mapping. */
  jrt::TreeSet<std::string>* components;
};

/** A pin of a logical part with its gate swap information. */
class PartLibrary_PartPin final : public virtual jrt::Object {
 public:
  PartLibrary_PartPin(const std::string& pinName, const std::string& gateName,
                      int32_t gateSwapCode, const std::string& gatePinName,
                      int32_t gatePinSwapCode)
      : pinName(pinName),
        gateName(gateName),
        gateSwapCode(gateSwapCode),
        gatePinName(gatePinName),
        gatePinSwapCode(gatePinSwapCode) {}

  std::string pinName;
  std::string gateName;
  int32_t gateSwapCode;
  std::string gatePinName;
  int32_t gatePinSwapCode;
};

/** A logical part (name + pins). */
class PartLibrary_LogicalPart final : public virtual jrt::Object {
 public:
  PartLibrary_LogicalPart(const std::string& name, jrt::Collection<PartLibrary_PartPin*>* partPins)
      : name(name), partPins(partPins) {}

  /** The name of the mapping. */
  std::string name;

  /** The pins of this logical part. */
  jrt::Collection<PartLibrary_PartPin*>* partPins;
};

/** Class for reading and writing part library scopes from dsn-files. */
class PartLibrary : public ScopeKeyword {
 public:
  /** Creates a new instance of PartLibrary. */
  PartLibrary() : ScopeKeyword("part_library") {}

  /** Throws jrt::IOException. */
  static void writeScope(WriteScopeParameter* scopeParameter);

  bool readScope(ReadScopeParameter* scopeParameter) override;

 private:
  /** Reads the component list of a logical part mapping. Returns null, if an error occurred. */
  PartLibrary_LogicalPartMapping* readLogicalPartMapping(IJFlexScanner* scanner);

  PartLibrary_LogicalPart* readLogicalPart(IJFlexScanner* scanner);

  PartLibrary_PartPin* readPartPin(IJFlexScanner* scanner);
};

}  // namespace fr::dsn
