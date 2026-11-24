// Port of app.freerouting.rules.ClearanceMatrix
#pragma once

#include "board/actions/ItemInfoPrinter.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Represents a single entry of the clearance matrix. (Inner class in Java.) */
class ClearanceMatrix_MatrixEntry final : public virtual jrt::Object {
 public:
  explicit ClearanceMatrix_MatrixEntry(ClearanceMatrix* outer);

  /** PORT NOTE: copy helper for ClearanceMatrix::deepCopy (takes over layer). */
  ClearanceMatrix_MatrixEntry(ClearanceMatrix* outer, jrt::Array<int32_t>* layer);

  using jrt::Object::equals;

  /** Returns true if all clearance values of this entry and other are equal. */
  bool equals(ClearanceMatrix_MatrixEntry* other);

  /** Returns true if not all layer values are equal. */
  bool isLayerDependent();

  ClearanceMatrix* outer;
  jrt::Array<int32_t>* layer;
};

/** Contains a row of entries of the clearance matrix. (Inner class in Java.) */
class ClearanceMatrix_Row final : public virtual ItemInfoPrinter_Printable {
 public:
  /** PORT NOTE: java.util.Locale -> std::string language tag. */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  ClearanceMatrix* outer;
  const std::string name;
  jrt::Array<ClearanceMatrix_MatrixEntry*>* column;  // final
  jrt::Array<int32_t>* maxValue;

 private:
  friend class ClearanceMatrix;

  ClearanceMatrix_Row(ClearanceMatrix* outer, const std::string& name);

  /** PORT NOTE: copy helper for ClearanceMatrix::deepCopy (empty columns, maxValue null). */
  ClearanceMatrix_Row(ClearanceMatrix* outer, const std::string& name, int32_t columnCount);
};

/**
 * NxN Matrix describing the spacing restrictions between N clearance classes on a fixed set of
 * layers.
 */
class ClearanceMatrix : public virtual jrt::Object {
 public:
  /** Stage E: frees a matrix made by deepCopy() that nobody refers to any more. */
  static void disposeCopy(ClearanceMatrix* m);
  static constexpr int32_t clearance_safety_margin = 16;

  /**
   * Creates a new instance for classCount clearance classes on the layers in layerStructure. names
   * must have one entry for each class.
   */
  ClearanceMatrix(int32_t classCount, LayerStructure* layerStructure,
                  jrt::Array<std::string>* names);

  /**
   * Creates a new instance with the 2 clearance classes "null" and "default" and initializes it
   * with defaultValue.
   */
  static ClearanceMatrix* getDefaultInstance(LayerStructure* layerStructure, int32_t defaultValue);

  /** Returns the number of the clearance class with the input name (ignoring case), or -1. */
  int32_t getNo(const std::string& name);

  /**
   * Gets the name of the clearance class with the input number.
   * PORT NOTE: returns "" where Java returns null (index out of range, after a warning), so that
   * the many callers can pass the result on as a std::string.
   */
  std::string getName(int32_t clearanceClassIndex);

  /** Sets the value of all clearance classes with number >= 1 to value. */
  void setDefaultValue(int32_t value);

  /** Sets the value of all clearance classes with number >= 1 to value on layer. */
  void setDefaultValue(int32_t layer, int32_t value);

  /** Sets the value of an entry in the clearance matrix to value on all layers. */
  void setValue(int32_t classI, int32_t classJ, int32_t value);

  /** Sets the value of an entry (rounded up to even) in the clearance matrix to value. */
  void setValue(int32_t classI, int32_t classJ, int32_t layer, int32_t value);

  /** Sets the value of an entry in the clearance matrix to value on all inner layers. */
  void setInnerValue(int32_t classI, int32_t classJ, int32_t value);

  /**
   * Gets the required spacing of clearance classes classI and classJ on layer (0 if out of
   * bounds). This value will be always an even integer.
   */
  int32_t getValue(int32_t classI, int32_t classJ, int32_t layer, bool addSafetyMargin);

  /** Returns the maximal required spacing of classI to all other classes on layer. */
  int32_t maxValue(int32_t classI, int32_t layer);

  /** Returns the maximum clearance value on the given layer. */
  int32_t maxValue(int32_t layer);

  /** Returns true if the values in column classI and row classJ are not equal on all layers. */
  bool isLayerDependent(int32_t classI, int32_t classJ);

  /** Returns true if the values in column classI and row classJ differ on the inner layers. */
  bool isInnerLayerDependent(int32_t classI, int32_t classJ);

  /** Returns the row with the given index. */
  ClearanceMatrix_Row* getRow(int32_t index);

  /** Returns the number of clearance classes. */
  int32_t getClassCount() { return this->classCount; }

  /** Returns the layer count of this clearance matrix. */
  int32_t getLayerCount();

  /** Returns the clearance compensation value of the given class on the given layer. */
  int32_t clearanceCompensationValue(int32_t clearanceClassIndex, int32_t layer) {
    return (this->getValue(clearanceClassIndex, clearanceClassIndex, layer, false) + 1) / 2;
  }

  /**
   * Appends a new clearance class initialized with the values of the default class. Returns false
   * if a clearance class with the given name already exists.
   */
  bool appendClass(const std::string& className);

  /** Removes the class with the given index from the clearance matrix. */
  void removeClass(int32_t index);

  /** Returns true if all clearance values of class first are equal to those of class second. */
  bool isEqual(int32_t first, int32_t second);

  /**
   * PORT NOTE (board deepCopy hook, used by BoardRules::deepCopy): independent copy of the matrix
   * preserving the identities inside it. The layer structure is shared.
   */
  ClearanceMatrix* deepCopy();

 private:
  /** PORT NOTE: uninitialised instance for deepCopy. */
  ClearanceMatrix() = default;

  friend class ClearanceMatrix_Row;
  friend class ClearanceMatrix_MatrixEntry;

  /** Recomputes row and layer maximum clearance values after a class is removed. */
  void recomputeMaxValues();

  LayerStructure* layerStructure;      // final
  jrt::Array<int32_t>* maxValueOnLayer;  // maximum clearance value for each layer

  /** The count of clearance classes. */
  int32_t classCount;

  jrt::Array<ClearanceMatrix_Row*>* row;  // classCount rows of the clearance matrix
};

}  // namespace fr
