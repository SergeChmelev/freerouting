// Port of app.freerouting.core.library.LogicalPart
#pragma once

#include "board/actions/ItemInfoPrinter.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes a pin belonging to a logical part. */
class LogicalPart_PartPin : public virtual jrt::Object {
 public:
  /** Creates a part pin with its gate and swap metadata. */
  LogicalPart_PartPin(int32_t pinIndex, const std::string& pinName, const std::string& gateName,
                      int32_t gateSwapCode, const std::string& gatePinName,
                      int32_t gatePinSwapCode)
      : pinIndex(pinIndex),
        pinName(pinName),
        gateName(gateName),
        gateSwapCode(gateSwapCode),
        gatePinName(gatePinName),
        gatePinSwapCode(gatePinSwapCode) {}

  /** Comparable<PartPin>: by pinIndex. */
  virtual int32_t compareTo(LogicalPart_PartPin* other) { return this->pinIndex - other->pinIndex; }

  /** The index of the part pin. Must be the same index as in the components library package. */
  const int32_t pinIndex;

  /** The name of the part pin. Must be the same name as in the components library package. */
  const std::string pinName;

  /** The name of the gate this pin belongs to. */
  const std::string gateName;

  /** The gate swap code. Gates with the same code can be swapped; codes <= 0 are not swappable. */
  const int32_t gateSwapCode;

  /** The identifier of the pin in the gate. */
  const std::string gatePinName;

  /** The pin swap code of the gate. Pins with the same code can be swapped inside a gate. */
  const int32_t gatePinSwapCode;
};

/** Contains information for gate swap and pin swap for a single component. */
class LogicalPart : public virtual ItemInfoPrinter_Printable {
 public:
  // Stage E: owns its part pins (runs only when the object is disposed).
  ~LogicalPart() override;
  /**
   * Creates a new instance of LogicalPart. The part pins are sorted by pinIndex. The pinIndex's of
   * the part pins must be the same index as in the components' library package.
   */
  LogicalPart(const std::string& name, int32_t id, jrt::Array<LogicalPart_PartPin*>* partPinArr)
      : name(name), id(id), partPinArr(partPinArr) {}

  /** Returns the number of pins in this logical part. */
  int32_t pinCount() { return partPinArr->length; }

  /** Returns the pin with the specified index. Pin indices range from 0 to pinCount - 1. */
  LogicalPart_PartPin* getPin(int32_t pinIndex);

  /** PORT NOTE: java.util.Locale -> std::string language tag. */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  const std::string name;
  const int32_t id;

 private:
  jrt::Array<LogicalPart_PartPin*>* partPinArr;
};

}  // namespace fr
