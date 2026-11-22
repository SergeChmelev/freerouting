// Port of app.freerouting.datastructures.IndentFileWriter
#include "datastructures/IndentFileWriter.h"

namespace fr {

// PORT NOTE: std::ostream does not throw on write failure by default, so the Java
// IOException handlers (FRLogger.error) have no counterpart here.

void IndentFileWriter::startScope(bool newLine) {
  if (newLine) {
    this->newLine();
  }

  write(BEGIN_SCOPE);
  ++currentIndentLevel;
}

void IndentFileWriter::endScope() {
  --currentIndentLevel;
  newLine();
  write(END_SCOPE);
}

void IndentFileWriter::newLine() {
  write("\n");
  for (int32_t i = 0; i < currentIndentLevel; i++) {
    write(INDENT_STRING);
  }
}

}  // namespace fr
