// Port of app.freerouting.core.library.BoardLibrary
#include "core/library/BoardLibrary.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/DrillItem.h"
#include "board/model/structure/LayerStructure.h"
#include "core/library/LogicalParts.h"
#include "core/library/Package.h"
#include "core/library/Packages.h"
#include "core/library/Padstack.h"
#include "core/library/Padstacks.h"
#include "datastructures/UndoableObjects.h"

namespace fr {

BoardLibrary::BoardLibrary(Padstacks* padstacks, Packages* packages)
    : logicalParts(new (jrt::kBoard) LogicalParts()) {
  this->padstacks = padstacks;
  this->packages = packages;
  logicalParts = new (jrt::kBoard) LogicalParts();
}

BoardLibrary::BoardLibrary() : logicalParts(new (jrt::kBoard) LogicalParts()) {}

int32_t BoardLibrary::viaPadstackCount() {
  if (this->viaPadstacks == nullptr) {
    return 0;
  }
  return this->viaPadstacks->size();
}

Padstack* BoardLibrary::getViaPadstack(int32_t no) {
  if (this->viaPadstacks == nullptr || no < 0 || no >= this->viaPadstacks->size()) {
    return nullptr;
  }
  return this->viaPadstacks->get(no);
}

Padstack* BoardLibrary::getViaPadstack(const std::string& name) {
  if (this->viaPadstacks == nullptr) {
    return nullptr;
  }
  for (Padstack* currentPadstack : *this->viaPadstacks) {
    if (currentPadstack->name == name) {
      return currentPadstack;
    }
  }
  return nullptr;
}

jrt::Array<Padstack*>* BoardLibrary::getViaPadstacks() {
  if (this->viaPadstacks == nullptr) {
    return new (jrt::kOp) jrt::Array<Padstack*>(0);
  }
  auto* result = new (jrt::kOp) jrt::Array<Padstack*>(viaPadstacks->size());
  for (int32_t i = 0; i < result->length; i++) {
    (*result)[i] = viaPadstacks->get(i);
  }
  return result;
}

void BoardLibrary::setViaPadstacks(jrt::Array<Padstack*>* padstacks) {
  this->viaPadstacks = new (jrt::kBoard) jrt::ArrayList<Padstack*>(padstacks->toVector());
}

bool BoardLibrary::addViaPadstack(Padstack* padstack) {
  if (getViaPadstack(padstack->name) != nullptr) {
    return false;
  }

  if (this->viaPadstacks == nullptr) {
    this->viaPadstacks = new (jrt::kBoard) jrt::ArrayList<Padstack*>();
  }

  this->viaPadstacks->add(padstack);
  return true;
}

bool BoardLibrary::removeViaPadstack(Padstack* padstack, BasicBoard* board) {
  if (viaPadstacks == nullptr) {
    throw jrt::NullPointerException("BoardLibrary.removeViaPadstack: viaPadstacks is null");
  }
  return viaPadstacks->remove(padstack);
}

Padstack* BoardLibrary::getMirroredViaPadstack(Padstack* viaPadstack) {
  int32_t layerCount = this->padstacks->boardLayerStructure->layers->length;
  if (viaPadstack->fromLayer() == 0 && viaPadstack->toLayer() == layerCount - 1) {
    return viaPadstack;
  }
  int32_t newFromLayer = layerCount - viaPadstack->toLayer() - 1;
  int32_t newToLayer = layerCount - viaPadstack->fromLayer() - 1;
  if (viaPadstacks == nullptr) {
    throw jrt::NullPointerException("BoardLibrary.getMirroredViaPadstack: viaPadstacks is null");
  }
  for (Padstack* currentViaPadstack : *viaPadstacks) {
    if (currentViaPadstack->fromLayer() == newFromLayer &&
        currentViaPadstack->toLayer() == newToLayer) {
      return currentViaPadstack;
    }
  }
  return nullptr;
}

bool BoardLibrary::isUsed(Padstack* padstack, BasicBoard* board) {
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = board->itemList->startReadObject();
  for (;;) {
    UndoableObjects_Storable* currentItem = board->itemList->readObject(it);
    if (currentItem == nullptr) {
      break;
    }
    if (auto* item = jrt::fast_cast<DrillItem>(currentItem)) {
      if (item->getPadstack() == padstack) {
        return true;
      }
    }
  }
  for (int32_t i = 1; i <= this->packages->count(); i++) {
    Package* currentPackage = this->packages->get(i);
    for (int32_t j = 0; j < currentPackage->pinCount(); j++) {
      if (currentPackage->getPin(j)->padstackId == padstack->id) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace fr
