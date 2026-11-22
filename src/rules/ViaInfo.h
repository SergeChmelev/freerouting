// Port of app.freerouting.rules.ViaInfo
#pragma once

#include "board/actions/ItemInfoPrinter.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Information about a combination of padstack, clearance class and drill-to-smd setting. */
class ViaInfo : public virtual ItemInfoPrinter_Printable {
 public:
  ViaInfo(const std::string& name, Padstack* padstack, int32_t clearanceClassIndex,
          bool drillToSmdAllowed, BoardRules* boardRules)
      : boardRules(boardRules),
        name(name),
        padstack(padstack),
        clearanceClassIndex(clearanceClassIndex),
        attachSmdAllowed_(drillToSmdAllowed) {}

  std::string getName() { return name; }

  void setName(const std::string& name) { this->name = name; }

  std::string toString() override { return this->name; }

  Padstack* getPadstack() { return padstack; }

  void setPadstack(Padstack* padstack) { this->padstack = padstack; }

  int32_t getClearanceClassIndex() { return clearanceClassIndex; }

  void setClearanceClassIndex(int32_t clearanceClassIndex) {
    this->clearanceClassIndex = clearanceClassIndex;
  }

  bool attachSmdAllowed() { return attachSmdAllowed_; }

  void setAttachSmdAllowed(bool attachSmdAllowed) { this->attachSmdAllowed_ = attachSmdAllowed; }

  /** Comparable<ViaInfo>: by name (String.compareTo). */
  virtual int32_t compareTo(ViaInfo* other) {
    return jrt::compareStrings(this->name, other->name);
  }

  /** PORT NOTE: java.util.Locale -> std::string language tag. */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

 private:
  BoardRules* boardRules;  // final
  std::string name;
  Padstack* padstack;
  int32_t clearanceClassIndex;
  /** PORT NOTE: renamed from attachSmdAllowed (clashes with the method of that name). */
  bool attachSmdAllowed_;
};

}  // namespace fr
