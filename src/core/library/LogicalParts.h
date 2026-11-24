// Port of app.freerouting.core.library.LogicalParts
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** The logical parts contain information for gate swap and pin swap. */
class LogicalParts : public virtual jrt::Object {
 public:
  // Stage E: owns its parts (runs only when the object is disposed).
  ~LogicalParts() override;
  LogicalParts();

  /** Adds a logical part to the database (sorts partPinArr by pin index first). */
  LogicalPart* add(const std::string& name, jrt::Array<LogicalPart_PartPin*>* partPinArr);

  /**
   * Returns the logical part with the input name or null, if no such part exists. (Java's
   * defensive null check on name is dropped: names are std::string.)
   */
  LogicalPart* get(const std::string& name);

  /** Returns the logical part with the specified index. Part IDs start at 1. */
  LogicalPart* get(int32_t partId);

  /** Returns the count of logical parts. */
  int32_t count();

 private:
  /** The array of logical parts (Java: Vector). */
  jrt::ArrayList<LogicalPart*>* partArr;
};

}  // namespace fr
