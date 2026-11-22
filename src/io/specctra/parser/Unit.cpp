#include "io/specctra/parser/Unit.h"

#include "board/model/structure/Unit.h"
#include "datastructures/IndentFileWriter.h"

namespace fr::dsn {

void Unit::writeScope(fr::IndentFileWriter* file, fr::Unit unit) {
  file->newLine();
  file->write("(unit ");
  file->write(fr::Unit_toString(unit));
  file->write(")");
}

}  // namespace fr::dsn
