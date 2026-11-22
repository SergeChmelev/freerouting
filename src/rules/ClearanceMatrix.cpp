// Port of app.freerouting.rules.ClearanceMatrix
#include "rules/ClearanceMatrix.h"

#include <unordered_map>

#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "geometry/planar/Point.h"
#include "logger/FRLogger.h"
#include "util/TextManager.h"

namespace fr {

namespace {
// PORT NOTE: identity map for deepCopy. std::unordered_map instead of jrt::IdentityHashMap so that
// copying does not assign identity hash codes to the source objects.
template <class K, class V>
class IdMap {
 public:
  V get(K k) {
    auto it = m_.find(k);
    return it == m_.end() ? nullptr : it->second;
  }
  void put(K k, V v) { m_[k] = v; }

 private:
  std::unordered_map<K, V> m_;
};
}  // namespace

// ---------------------------------------------------------------------------------------------
// ClearanceMatrix
// ---------------------------------------------------------------------------------------------

ClearanceMatrix::ClearanceMatrix(int32_t classCount, LayerStructure* layerStructure,
                                 jrt::Array<std::string>* names) {
  this->classCount = jrt::Math::max(classCount, 1);
  this->layerStructure = layerStructure;
  row = new (jrt::kBoard) jrt::Array<ClearanceMatrix_Row*>(this->classCount);
  for (int32_t i = 0; i < this->classCount; i++) {
    (*row)[i] = new (jrt::kBoard) ClearanceMatrix_Row(this, (*names)[i]);
  }
  this->maxValueOnLayer = new (jrt::kBoard) jrt::Array<int32_t>(layerStructure->layers->length);
}

ClearanceMatrix* ClearanceMatrix::getDefaultInstance(LayerStructure* layerStructure,
                                                     int32_t defaultValue) {
  auto* names = new (jrt::kOp) jrt::Array<std::string>(2);
  (*names)[0] = "null";
  (*names)[1] = "default";
  ClearanceMatrix* result = new (jrt::kBoard) ClearanceMatrix(2, layerStructure, names);
  result->setDefaultValue(defaultValue);
  return result;
}

int32_t ClearanceMatrix::getNo(const std::string& name) {
  for (int32_t i = 0; i < classCount; i++) {
    if (jrt::equalsIgnoreCase((*row)[i]->name, name)) {
      return i;
    }
  }
  return -1;
}

std::string ClearanceMatrix::getName(int32_t clearanceClassIndex) {
  if (clearanceClassIndex < 0 || clearanceClassIndex >= row->length) {
    FRLogger::warn("ClearanceMatrix.get_name: clearanceClassIndex out of range");
    return "";  // PORT NOTE: Java returns null (see header).
  }
  return (*row)[clearanceClassIndex]->name;
}

void ClearanceMatrix::setDefaultValue(int32_t value) {
  for (int32_t i = 0; i < layerStructure->layers->length; i++) {
    setDefaultValue(i, value);
  }
}

void ClearanceMatrix::setDefaultValue(int32_t layer, int32_t value) {
  for (int32_t i = 1; i < classCount; i++) {
    for (int32_t j = 1; j < classCount; j++) {
      setValue(i, j, layer, value);
    }
  }
}

void ClearanceMatrix::setValue(int32_t classI, int32_t classJ, int32_t value) {
  for (int32_t layer = 0; layer < layerStructure->layers->length; layer++) {
    setValue(classI, classJ, layer, value);
  }
}

void ClearanceMatrix::setValue(int32_t classI, int32_t classJ, int32_t layer, int32_t value) {
  ClearanceMatrix_Row* currentRow = (*row)[classJ];
  ClearanceMatrix_MatrixEntry* currentEntry = (*currentRow->column)[classI];

  // assure, that the clearance value is positive and even, and round it up, if it is odd
  value = jrt::Math::max(value, 0);
  if (value % 2 != 0) {
    if (value == jrt::Integer::MAX_VALUE) {
      value--;
    } else {
      value++;
    }
  }

  (*currentEntry->layer)[layer] = value;
  (*currentRow->maxValue)[layer] = jrt::Math::max((*currentRow->maxValue)[layer], value);
  (*this->maxValueOnLayer)[layer] = jrt::Math::max((*this->maxValueOnLayer)[layer], value);
}

void ClearanceMatrix::setInnerValue(int32_t classI, int32_t classJ, int32_t value) {
  for (int32_t layer = 1; layer < layerStructure->layers->length - 1; layer++) {
    setValue(classI, classJ, layer, value);
  }
}

int32_t ClearanceMatrix::getValue(int32_t classI, int32_t classJ, int32_t layer,
                                  bool addSafetyMargin) {
  if (classI < 0 || classI >= classCount || classJ < 0 || classJ >= classCount || layer < 0 ||
      layer >= layerStructure->layers->length) {
    if (FRLogger::isGranularTraceActive()) {
      FRLogger::trace("ClearanceMatrix.get_value", "out_of_bounds",
                      "Clearance request out of bounds: class_i=" + jrt::str(classI) +
                          " (max=" + jrt::str(classCount - 1) + ")" +
                          ", class_j=" + jrt::str(classJ) + " (max=" + jrt::str(classCount - 1) +
                          ")" + ", layer=" + jrt::str(layer) +
                          " (max=" + jrt::str(layerStructure->layers->length - 1) + ")" +
                          ", returning 0",
                      "Clearance Check", new (jrt::kGeom) jrt::Array<Point*>(0));
    }
    return 0;
  }

  int32_t valueFromTheMatrix = (*(*(*row)[classJ]->column)[classI]->layer)[layer];
  int32_t finalValue =
      addSafetyMargin ? valueFromTheMatrix + clearance_safety_margin : valueFromTheMatrix;

  if (FRLogger::isGranularTraceActive()) {
    FRLogger::trace(
        "ClearanceMatrix.get_value", "clearance_retrieved",
        "Clearance value: class_i=" + jrt::str(classI) + " (" +
            (classI < row->length ? (*row)[classI]->name : std::string("?")) + ")" +
            ", class_j=" + jrt::str(classJ) + " (" +
            (classJ < row->length ? (*row)[classJ]->name : std::string("?")) + ")" +
            ", layer=" + jrt::str(layer) + " (" +
            (layer < layerStructure->layers->length ? (*layerStructure->layers)[layer]->name
                                                    : std::string("?")) +
            ")" + ", base_value=" + jrt::str(valueFromTheMatrix) + " (" +
            jrt::str(valueFromTheMatrix / 10000.0) + "mm)" + ", safety_margin=" +
            jrt::str(addSafetyMargin ? clearance_safety_margin : 0) +
            ", finalValue=" + jrt::str(finalValue) + " (" + jrt::str(finalValue / 10000.0) +
            "mm)",
        "Clearance Check", new (jrt::kGeom) jrt::Array<Point*>(0));
  }

  return finalValue;
}

int32_t ClearanceMatrix::maxValue(int32_t classI, int32_t layer) {
  int32_t i = jrt::Math::max(classI, 0);
  i = jrt::Math::min(i, classCount - 1);
  int32_t layerIndex = jrt::Math::max(layer, 0);
  layerIndex = jrt::Math::min(layerIndex, layerStructure->layers->length - 1);
  return (*(*row)[i]->maxValue)[layerIndex];
}

int32_t ClearanceMatrix::maxValue(int32_t layer) {
  int32_t layerIndex = jrt::Math::max(layer, 0);
  layerIndex = jrt::Math::min(layerIndex, layerStructure->layers->length - 1);
  return (*this->maxValueOnLayer)[layerIndex];
}

bool ClearanceMatrix::isLayerDependent(int32_t classI, int32_t classJ) {
  int32_t compareValue = (*(*(*row)[classJ]->column)[classI]->layer)[0];
  for (int32_t l = 1; l < layerStructure->layers->length; l++) {
    if ((*(*(*row)[classJ]->column)[classI]->layer)[l] != compareValue) {
      return true;
    }
  }
  return false;
}

bool ClearanceMatrix::isInnerLayerDependent(int32_t classI, int32_t classJ) {
  if (layerStructure->layers->length <= 2) {
    return false;  // no inner layers
  }
  int32_t compareValue = (*(*(*row)[classJ]->column)[classI]->layer)[1];
  for (int32_t l = 2; l < layerStructure->layers->length - 1; l++) {
    if ((*(*(*row)[classJ]->column)[classI]->layer)[l] != compareValue) {
      return true;
    }
  }
  return false;
}

ClearanceMatrix_Row* ClearanceMatrix::getRow(int32_t index) {
  if (index < 0 || index >= this->row->length) {
    FRLogger::warn("ClearanceMatrix.get_row: no out of range");
    return nullptr;
  }
  return (*this->row)[index];
}

int32_t ClearanceMatrix::getLayerCount() { return layerStructure->layers->length; }

bool ClearanceMatrix::appendClass(const std::string& className) {
  if (this->getNo(className) >= 0) {
    return false;
  }
  int32_t oldClassCount = this->classCount;
  ++this->classCount;

  auto* newRow = new (jrt::kOp) jrt::Array<ClearanceMatrix_Row*>(this->classCount);

  // append a matrix entry to each old row
  for (int32_t i = 0; i < oldClassCount; i++) {
    ClearanceMatrix_Row* currentOldRow = (*this->row)[i];
    (*newRow)[i] = new (jrt::kBoard) ClearanceMatrix_Row(this, currentOldRow->name);
    ClearanceMatrix_Row* currentNewRow = (*newRow)[i];
    currentNewRow->maxValue = currentOldRow->maxValue;
    jrt::arraycopy(currentOldRow->column, 0, currentNewRow->column, 0, oldClassCount);

    (*currentNewRow->column)[oldClassCount] = new (jrt::kBoard) ClearanceMatrix_MatrixEntry(this);
  }

  // append the new row

  (*newRow)[oldClassCount] = new (jrt::kBoard) ClearanceMatrix_Row(this, className);

  this->row = newRow;

  // Set the new matrix elements to default values.

  for (int32_t i = 0; i < oldClassCount; i++) {
    for (int32_t j = 0; j < this->layerStructure->layers->length; j++) {
      int32_t defaultValue = this->getValue(1, i, j, false);
      this->setValue(oldClassCount, i, j, defaultValue);
      this->setValue(i, oldClassCount, j, defaultValue);
    }
  }

  for (int32_t j = 0; j < this->layerStructure->layers->length; j++) {
    int32_t defaultValue = this->getValue(1, 1, j, false);
    this->setValue(oldClassCount, oldClassCount, j, defaultValue);
  }
  return true;
}

void ClearanceMatrix::removeClass(int32_t index) {
  int32_t oldClassCount = this->classCount;
  --this->classCount;

  auto* newRow = new (jrt::kOp) jrt::Array<ClearanceMatrix_Row*>(this->classCount);

  // Remove the matrix entry with the given index from each old row.
  int32_t newRowIndex = 0;
  for (int32_t i = 0; i < oldClassCount; i++) {
    if (i == index) {
      continue;
    }
    ClearanceMatrix_Row* currentOldRow = (*this->row)[i];
    (*newRow)[newRowIndex] = new (jrt::kBoard) ClearanceMatrix_Row(this, currentOldRow->name);
    ClearanceMatrix_Row* currentNewRow = (*newRow)[newRowIndex];

    int32_t newColumnIndex = 0;
    for (int32_t j = 0; j < oldClassCount; j++) {
      if (j == index) {
        continue;
      }
      (*currentNewRow->column)[newColumnIndex] = (*currentOldRow->column)[j];
      ++newColumnIndex;
    }
    ++newRowIndex;
  }
  this->row = newRow;
  recomputeMaxValues();
}

void ClearanceMatrix::recomputeMaxValues() {
  jrt::Arrays::fill(this->maxValueOnLayer, 0);
  for (ClearanceMatrix_Row* currentRow : *this->row) {
    for (int32_t layer = 0; layer < this->maxValueOnLayer->length; layer++) {
      int32_t max = 0;
      for (ClearanceMatrix_MatrixEntry* entry : *currentRow->column) {
        max = jrt::Math::max(max, (*entry->layer)[layer]);
      }
      (*currentRow->maxValue)[layer] = max;
      (*this->maxValueOnLayer)[layer] = jrt::Math::max((*this->maxValueOnLayer)[layer], max);
    }
  }
}

bool ClearanceMatrix::isEqual(int32_t first, int32_t second) {
  if (first == second) {
    return true;
  }
  if (first < 0 || second < 0 || first >= this->classCount || second >= this->classCount) {
    return false;
  }
  ClearanceMatrix_Row* row1 = (*this->row)[first];
  ClearanceMatrix_Row* row2 = (*this->row)[second];
  for (int32_t i = 1; i < classCount; i++) {
    if (!(*row1->column)[i]->equals((*row2->column)[i])) {
      return false;
    }
  }
  return true;
}

ClearanceMatrix* ClearanceMatrix::deepCopy() {
  // PORT NOTE: replacement for Java serialization of the matrix. Object identities inside the
  // matrix (rows, entries, int arrays shared between rows by appendClass) are preserved.
  auto* copy = new (jrt::kBoard) ClearanceMatrix();
  copy->layerStructure = this->layerStructure;  // shared (see RoutingBoardUndoFacade.h)
  copy->classCount = this->classCount;
  copy->maxValueOnLayer = this->maxValueOnLayer->clone();
  auto* entryMap =
      new (jrt::kOp) IdMap<ClearanceMatrix_MatrixEntry*, ClearanceMatrix_MatrixEntry*>();
  auto* arrayMap = new (jrt::kOp) IdMap<jrt::Array<int32_t>*, jrt::Array<int32_t>*>();
  auto mapArray = [arrayMap](jrt::Array<int32_t>* a) -> jrt::Array<int32_t>* {
    if (a == nullptr) {
      return nullptr;
    }
    jrt::Array<int32_t>* result = arrayMap->get(a);
    if (result == nullptr) {
      result = a->clone();
      arrayMap->put(a, result);
    }
    return result;
  };
  copy->row = new (jrt::kOp) jrt::Array<ClearanceMatrix_Row*>(this->row->length);
  for (int32_t i = 0; i < this->row->length; i++) {
    ClearanceMatrix_Row* srcRow = (*this->row)[i];
    if (srcRow == nullptr) {
      continue;
    }
    auto* newRow = new (jrt::kBoard) ClearanceMatrix_Row(copy, srcRow->name, srcRow->column->length);
    for (int32_t j = 0; j < srcRow->column->length; j++) {
      ClearanceMatrix_MatrixEntry* srcEntry = (*srcRow->column)[j];
      if (srcEntry == nullptr) {
        continue;
      }
      ClearanceMatrix_MatrixEntry* newEntry = entryMap->get(srcEntry);
      if (newEntry == nullptr) {
        newEntry = new (jrt::kBoard) ClearanceMatrix_MatrixEntry(copy, mapArray(srcEntry->layer));
        entryMap->put(srcEntry, newEntry);
      }
      (*newRow->column)[j] = newEntry;
    }
    newRow->maxValue = mapArray(srcRow->maxValue);
    (*copy->row)[i] = newRow;
  }
  return copy;
}

// ---------------------------------------------------------------------------------------------
// ClearanceMatrix_Row
// ---------------------------------------------------------------------------------------------

ClearanceMatrix_Row::ClearanceMatrix_Row(ClearanceMatrix* outer, const std::string& name)
    : outer(outer), name(name) {
  column = new (jrt::kBoard) jrt::Array<ClearanceMatrix_MatrixEntry*>(outer->classCount);
  for (int32_t i = 0; i < outer->classCount; i++) {
    (*column)[i] = new (jrt::kBoard) ClearanceMatrix_MatrixEntry(outer);
  }
  maxValue = new (jrt::kBoard) jrt::Array<int32_t>(outer->layerStructure->layers->length);
}

ClearanceMatrix_Row::ClearanceMatrix_Row(ClearanceMatrix* outer, const std::string& name,
                                         int32_t columnCount)
    : outer(outer), name(name) {
  column = new (jrt::kBoard) jrt::Array<ClearanceMatrix_MatrixEntry*>(columnCount);
  maxValue = nullptr;
}

void ClearanceMatrix_Row::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager("ClearanceMatrix$Row", locale);

  printer->appendBold(tm->getText("spacing_from_clearance_class") + " ");
  printer->appendBold(this->name);
  for (int32_t i = 1; i < this->column->length; i++) {
    printer->newline();
    printer->indent();
    printer->append(" " + tm->getText("to_class") + " ");
    printer->append((*outer->row)[i]->name);
    ClearanceMatrix_MatrixEntry* currentColumn = (*this->column)[i];
    if (currentColumn->isLayerDependent()) {
      printer->append(" " + tm->getText("on_layer") + " ");
      for (int32_t j = 0; j < outer->layerStructure->layers->length; j++) {
        printer->newline();
        printer->indent();
        printer->indent();
        printer->append((*outer->layerStructure->layers)[j]->name);
        printer->append(" = ");
        printer->append(static_cast<double>((*currentColumn->layer)[j]));
      }
    } else {
      printer->append(" = ");
      printer->append(static_cast<double>((*currentColumn->layer)[0]));
    }
  }
}

// ---------------------------------------------------------------------------------------------
// ClearanceMatrix_MatrixEntry
// ---------------------------------------------------------------------------------------------

ClearanceMatrix_MatrixEntry::ClearanceMatrix_MatrixEntry(ClearanceMatrix* outer) : outer(outer) {
  layer = new (jrt::kBoard) jrt::Array<int32_t>(outer->layerStructure->layers->length);
  for (int32_t i = 0; i < outer->layerStructure->layers->length; i++) {
    (*layer)[i] = 0;
  }
}

ClearanceMatrix_MatrixEntry::ClearanceMatrix_MatrixEntry(ClearanceMatrix* outer,
                                                         jrt::Array<int32_t>* layer)
    : outer(outer), layer(layer) {}

bool ClearanceMatrix_MatrixEntry::equals(ClearanceMatrix_MatrixEntry* other) {
  for (int32_t i = 0; i < outer->layerStructure->layers->length; i++) {
    if ((*this->layer)[i] != (*other->layer)[i]) {
      return false;
    }
  }
  return true;
}

bool ClearanceMatrix_MatrixEntry::isLayerDependent() {
  int32_t compareValue = (*layer)[0];
  for (int32_t i = 1; i < outer->layerStructure->layers->length; i++) {
    if ((*layer)[i] != compareValue) {
      return true;
    }
  }
  return false;
}

}  // namespace fr
