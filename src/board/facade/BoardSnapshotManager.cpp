// Port of app.freerouting.board.facade.BoardSnapshotManager
#include "board/facade/BoardSnapshotManager.h"

#include <cstdio>

#include "board/facade/BasicBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/FixedState.h"
#include "board/trace/PolylineTrace.h"
#include "core/library/Padstack.h"
#include "datastructures/UndoableObjects.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"

namespace fr {

BasicBoard* BoardSnapshotManager::serialize(bool basicProfile) {
  // PORT NOTE: both profiles produce a frozen independent copy of the board (see header).
  (void)basicProfile;
  return board->copyAsDeserialized(nullptr);
}

BasicBoard* BoardSnapshotManager::deserialize(BasicBoard* snapshot) {
  if (snapshot == nullptr) {
    return nullptr;
  }
  return snapshot->copyAsDeserialized(nullptr);
}

namespace {

/** PORT NOTE (new): 64-bit FNV-1a over the bytes of s, starting from basis. */
uint64_t fnv1a64(const std::string& s, uint64_t basis) {
  uint64_t h = basis;
  for (unsigned char c : s) {
    h ^= static_cast<uint64_t>(c);
    h *= 1099511628211ULL;
  }
  return h;
}

std::string intArrayToString(jrt::Array<int32_t>* arr) {
  std::string result = "[";
  if (arr != nullptr) {
    for (int32_t i = 0; i < arr->length; i++) {
      if (i > 0) {
        result += ",";
      }
      result += jrt::str((*arr)[i]);
    }
  }
  result += "]";
  return result;
}

std::string boxToString(IntBox* box) {
  if (box == nullptr) {
    return "null";
  }
  return "(" + jrt::str(box->ll->x) + "," + jrt::str(box->ll->y) + "," + jrt::str(box->ur->x) +
         "," + jrt::str(box->ur->y) + ")";
}

}  // namespace

std::string BoardSnapshotManager::basicProfileDump() {
  std::string result;
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = board->itemList->startReadObject();
  for (;;) {
    UndoableObjects_Storable* currentObject = board->itemList->readObject(it);
    if (currentObject == nullptr) {
      break;
    }
    auto* item = jrt::fast_cast<Item>(currentObject);
    if (item == nullptr) {
      continue;
    }
    result += jrt::className(item);
    result += "#" + jrt::str(item->getId());
    result += " nets=" + intArrayToString(item->netNumbers);
    result += " layers=" + jrt::str(item->firstLayer()) + ".." + jrt::str(item->lastLayer());
    result += " cl=" + jrt::str(item->clearanceClassIndex());
    result += " fixed=" + FixedState_name(item->getFixedState());
    result += " comp=" + jrt::str(item->getComponentId());
    if (auto* trace = jrt::fast_cast<PolylineTrace>(item)) {
      result += " hw=" + jrt::str(trace->getHalfWidth());
      Polyline* polyline = trace->polyline();
      result += " corners=";
      for (int32_t i = 0; i < polyline->cornerCount(); i++) {
        FloatPoint* corner = polyline->cornerApprox(i);
        result += "(" + jrt::str(corner->x) + "," + jrt::str(corner->y) + ")";
      }
    } else if (auto* via = jrt::fast_cast<Via>(item)) {
      Padstack* padstack = via->getPadstack();
      result += " padstack=" + (padstack != nullptr ? padstack->name : std::string("null"));
      FloatPoint* center = via->getCenter()->toFloat();
      result += " center=(" + jrt::str(center->x) + "," + jrt::str(center->y) + ")";
    } else {
      result += " box=" + boxToString(item->boundingBox());
    }
    result += "\n";
  }
  return result;
}

std::string BoardSnapshotManager::getHash() {
  // PORT NOTE: Java hashes (MD5) the serialized trace-state profile; the port hashes a canonical
  // text dump of the items with two independent 64-bit FNV-1a passes (32 hex digits). Only hash
  // equality is used by callers.
  std::string dump = basicProfileDump();
  uint64_t h1 = fnv1a64(dump, 14695981039346656037ULL);
  uint64_t h2 = fnv1a64(dump, 0x84222325cbf29ce4ULL);
  char buffer[33];
  std::snprintf(buffer, sizeof(buffer), "%016llx%016llx", static_cast<unsigned long long>(h1),
                static_cast<unsigned long long>(h2));
  return std::string(buffer);
}

void BoardSnapshotManager::generateSnapshot() {
  board->itemList->generateSnapshot();
  board->components->generateSnapshot();
}

bool BoardSnapshotManager::popSnapshot() {
  bool itemPopped = board->itemList->popSnapshot();
  bool componentPopped = board->components->popSnapshot();
  return itemPopped && componentPopped;
}

int32_t BoardSnapshotManager::diffTraces(BasicBoard* compareTo) {
  int32_t result = 0;
  jrt::HashSet<int32_t>* traceIds = new jrt::HashSet<int32_t>();
  for (Trace* trace : *board->getTraces()) {
    traceIds->add(trace->getId());
  }
  for (Trace* trace : *compareTo->getTraces()) {
    if (!traceIds->contains(trace->getId())) {
      result++;
    } else {
      traceIds->remove(trace->getId());
    }
  }
  return result + traceIds->size();
}

}  // namespace fr
