#include "io/specctra/parser/Layer.h"

#include "board/facade/BasicBoard.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "io/specctra/parser/Rule.h"
#include "io/specctra/parser/WriteScopeParameter.h"

namespace fr::dsn {

Layer* Layer::PCB() {
  static Layer* v = new (jrt::kBoard) Layer("pcb", -1, false);
  return v;
}

Layer* Layer::SIGNAL() {
  static Layer* v = new (jrt::kBoard) Layer("signal", -1, true);
  return v;
}

void Layer::writeScope(WriteScopeParameter* scopeParameter, int32_t layerIndex, bool writeRule) {
  scopeParameter->file->startScope();
  scopeParameter->file->write("layer ");
  fr::Layer* boardLayer = (*scopeParameter->board->layerStructure->layers)[layerIndex];
  scopeParameter->identifierType->write(boardLayer->name, scopeParameter->file);
  scopeParameter->file->newLine();
  scopeParameter->file->write("(type ");
  if (boardLayer->isSignal) {
    scopeParameter->file->write("signal)");
  } else {
    scopeParameter->file->write("power)");
  }
  if (writeRule) {
    Rule::writeDefaultRule(scopeParameter, layerIndex);
  }
  scopeParameter->file->endScope();
}

}  // namespace fr::dsn
