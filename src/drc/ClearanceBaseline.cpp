// Port of app.freerouting.drc.ClearanceBaseline
#include "drc/ClearanceBaseline.h"

#include "board/model/items/Item.h"
#include "drc/ClearanceViolation.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/TileShape.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// ClearanceBaseline
// ---------------------------------------------------------------------------------------------

ClearanceBaseline::ClearanceBaseline(jrt::Collection<ClearanceViolation*>* violations) {
  this->entries = capture(violations);
}

int32_t ClearanceBaseline::countNewOrChanged(jrt::Collection<ClearanceViolation*>* violations) {
  if (this->entries->length == 0) {
    return violations->size();
  }
  return getNewOrChanged(violations)->size();
}

jrt::List<ClearanceViolation*>* ClearanceBaseline::getNewOrChanged(
    jrt::Collection<ClearanceViolation*>* violations) {
  auto* result = new jrt::ArrayList<ClearanceViolation*>();
  if (this->entries->length == 0) {
    result->addAll(violations);
    return result;
  }
  std::vector<ClearanceViolation*> currentViolations = violations->toVector();
  jrt::Array<ClearanceBaseline_Entry*>* currentEntries = capture(violations);
  auto* used = new jrt::Array<bool>(this->entries->length);
  for (int32_t n = 0; n < currentEntries->length; n++) {
    ClearanceBaseline_Entry* current = (*currentEntries)[n];
    int32_t match = -1;
    for (int32_t i = 0; i < this->entries->length; i++) {
      if (!(*used)[i] && (*this->entries)[i]->matches(current)) {
        match = i;
        break;
      }
    }
    if (match < 0) {
      result->add(currentViolations[n]);
    } else {
      (*used)[match] = true;
    }
  }
  return result;
}

jrt::Array<ClearanceBaseline_Entry*>* ClearanceBaseline::capture(
    jrt::Collection<ClearanceViolation*>* violations) {
  auto* cache = new jrt::HashMap<Item*, ClearanceBaseline_ItemGeometry*>();
  auto* result = new jrt::Array<ClearanceBaseline_Entry*>(violations->size());
  int32_t idx = 0;
  for (ClearanceViolation* v : *violations) {
    ClearanceBaseline_ItemGeometry* first =
        v->firstItem != nullptr
            ? cache->computeIfAbsent(
                  v->firstItem, [](Item* item) { return new ClearanceBaseline_ItemGeometry(item); })
            : nullptr;
    ClearanceBaseline_ItemGeometry* second =
        v->secondItem != nullptr
            ? cache->computeIfAbsent(
                  v->secondItem,
                  [](Item* item) { return new ClearanceBaseline_ItemGeometry(item); })
            : nullptr;
    (*result)[idx++] = new ClearanceBaseline_Entry(first, second, v->layer, v->expectedClearance,
                                                   v->actualClearance);
  }
  return result;
}

// ---------------------------------------------------------------------------------------------
// ClearanceBaseline_Entry
// ---------------------------------------------------------------------------------------------

bool ClearanceBaseline_Entry::matches(ClearanceBaseline_Entry* other) {
  if (other == nullptr) {
    return false;
  }
  if (this->layer != other->layer || jrt::Double::compare(this->expected, other->expected) != 0 ||
      jrt::Double::compare(this->actual, other->actual) != 0) {
    return false;
  }
  return (matchesItems(this->first, other->first) && matchesItems(this->second, other->second)) ||
         (matchesItems(this->first, other->second) && matchesItems(this->second, other->first));
}

bool ClearanceBaseline_Entry::matchesItems(ClearanceBaseline_ItemGeometry* a,
                                           ClearanceBaseline_ItemGeometry* b) {
  if (a == nullptr) {
    return b == nullptr;
  }
  return a->matches(b);
}

// ---------------------------------------------------------------------------------------------
// ClearanceBaseline_ItemGeometry
// ---------------------------------------------------------------------------------------------

ClearanceBaseline_ItemGeometry::ClearanceBaseline_ItemGeometry(Item* item) {
  // PORT NOTE: Java getClass().getName() -> jrt::className (only compared for equality).
  this->type = jrt::className(item);
  this->nets = new jrt::Array<int32_t>(item->netCount());
  for (int32_t i = 0; i < this->nets->length; i++) {
    (*this->nets)[i] = item->getNetNumber(i);
  }
  jrt::Arrays::sort(this->nets);
  this->shapes = new jrt::Array<ClearanceBaseline_ShapeGeometry*>(item->tileShapeCount());
  for (int32_t i = 0; i < this->shapes->length; i++) {
    (*this->shapes)[i] = new ClearanceBaseline_ShapeGeometry(item->shapeLayer(i), item->getTileShape(i));
  }
}

bool ClearanceBaseline_ItemGeometry::matches(ClearanceBaseline_ItemGeometry* other) {
  if (other == nullptr) {
    return false;
  }
  if (this->type != other->type || !jrt::Arrays::equals(this->nets, other->nets)) {
    return false;
  }
  if (this->shapes->length != other->shapes->length) {
    return false;
  }
  for (int32_t i = 0; i < this->shapes->length; i++) {
    if (!(*this->shapes)[i]->matches((*other->shapes)[i])) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------------------------
// ClearanceBaseline_ShapeGeometry
// ---------------------------------------------------------------------------------------------

ClearanceBaseline_ShapeGeometry::ClearanceBaseline_ShapeGeometry(int32_t layer, TileShape* shape) {
  this->layer = layer;
  if (shape == nullptr) {
    this->corners = nullptr;
  } else {
    int32_t count = shape->borderLineCount();
    this->corners = new jrt::Array<Point*>(count * 2);
    for (int32_t i = 0; i < count; i++) {
      Line* border = shape->borderLine(i);
      (*this->corners)[i * 2] = border->a;
      (*this->corners)[i * 2 + 1] = border->b;
    }
    jrt::Arrays::sort(this->corners,
                      jrt::Comparator<Point*>([](Point* a, Point* b) { return a->compareXY(b); }));
  }
}

bool ClearanceBaseline_ShapeGeometry::matches(ClearanceBaseline_ShapeGeometry* other) {
  if (other == nullptr || this->layer != other->layer) {
    return false;
  }
  if (this->corners == nullptr) {
    return other->corners == nullptr;
  }
  if (other->corners == nullptr || this->corners->length != other->corners->length) {
    return false;
  }
  for (int32_t i = 0; i < this->corners->length; i++) {
    if (!(*this->corners)[i]->equals((*other->corners)[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace fr
