#include "io/specctra/parser/Placement.h"

#include "board/facade/BasicBoard.h"
#include "board/model/structure/Components.h"
#include "core/library/BoardLibrary.h"
#include "core/library/Packages.h"
#include "datastructures/IndentFileWriter.h"
#include "io/specctra/parser/Package.h"
#include "io/specctra/parser/WriteScopeParameter.h"

namespace fr::dsn {

void Placement::writeScope(WriteScopeParameter* scopeParameter) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("placement");
  if (scopeParameter->board->components->getFlipStyleRotateFirst()) {
    scopeParameter->file->newLine();
    scopeParameter->file->write("(place_control (flip_style rotate_first))");
  }

  if (scopeParameter->board->library->packages != nullptr) {
    for (int32_t i = 1; i <= scopeParameter->board->library->packages->count(); i++) {
      Package::writePlacementScope(scopeParameter,
                                   scopeParameter->board->library->packages->get(i));
    }
  }
  scopeParameter->file->endScope();
}

}  // namespace fr::dsn
