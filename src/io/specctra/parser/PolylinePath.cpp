#include "io/specctra/parser/PolylinePath.h"

#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/Rectangle.h"
#include "logger/FRLogger.h"

namespace fr::dsn {

void PolylinePath::writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifier) {
  file->startScope();
  file->write("polyline_path ");
  identifier->write(this->layer->name, file);
  file->write(" ");
  file->write(jrt::str(this->width));
  int32_t lineCount = coordinateArr->length / 4;
  for (int32_t i = 0; i < lineCount; i++) {
    file->newLine();
    for (int32_t j = 0; j < 4; j++) {
      file->write(jrt::str((*coordinateArr)[4 * i + j]));
      file->write(" ");
    }
  }
  file->endScope();
}

void PolylinePath::writeScopeInt(fr::IndentFileWriter* file, fr::IdentifierType* identifier) {
  file->startScope();
  file->write("polyline_path ");
  identifier->write(this->layer->name, file);
  file->write(" ");
  file->write(jrt::str(this->width));
  int32_t lineCount = coordinateArr->length / 4;
  for (int32_t i = 0; i < lineCount; i++) {
    file->newLine();
    for (int32_t j = 0; j < 4; j++) {
      int32_t currentCoor = static_cast<int32_t>(jrt::Math::round((*coordinateArr)[4 * i + j]));
      file->write(jrt::str(currentCoor));
      file->write(" ");
    }
  }
  file->endScope();
}

fr::Shape* PolylinePath::transformToBoardRel(fr::io::CoordinateTransform* coordinateTransform) {
  FRLogger::warn("PolylinePath.transform_to_board_rel not implemented");
  return nullptr;
}

fr::Shape* PolylinePath::transformToBoard(fr::io::CoordinateTransform* coordinateTransform) {
  FRLogger::warn("PolylinePath.transform_to_board not implemented");
  return nullptr;
}

Rectangle* PolylinePath::boundingBox() {
  FRLogger::warn("PolylinePath.boundingBox not implemented");
  return nullptr;
}

}  // namespace fr::dsn
