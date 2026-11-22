// Port of app.freerouting.rules.ViaInfos
#pragma once

#include "board/actions/ItemInfoPrinter.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Contains the lists of different ViaInfo's, which can be used in interactive and automatic routing. */
class ViaInfos : public virtual ItemInfoPrinter_Printable {
 public:
  ViaInfos();

  /** Adds a via info consisting of padstack, clearance class and drill-to-smd setting. Returns
   * false if a via info with the same name already exists. */
  bool add(ViaInfo* viaInfo);

  /** Returns the number of different vias, which can be used for routing. */
  int32_t count();

  /** Returns the index-th via info. */
  ViaInfo* get(int32_t index);

  /** Returns the via info with name, or null, if no such via exists. */
  ViaInfo* get(const std::string& name);

  /** Returns true, if a via info with the input name exists. */
  bool nameExists(const std::string& name);

  /** Removes the input via from the list. Returns false, if the via was not found. */
  bool remove(ViaInfo* viaInfo);

  /** PORT NOTE: java.util.Locale -> std::string language tag. */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

 private:
  /** Java: LinkedList. */
  jrt::List<ViaInfo*>* list;
};

}  // namespace fr
