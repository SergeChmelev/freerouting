#include "io/specctra/parser/Rectangle.h"

#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/Layer.h"

namespace fr::dsn {

Rectangle* Rectangle::union_(Rectangle* other) {
  auto* resultCoor = new (jrt::kOp) jrt::Array<double>(4);
  (*resultCoor)[0] = jrt::Math::min((*this->coor)[0], (*other->coor)[0]);
  (*resultCoor)[1] = jrt::Math::min((*this->coor)[1], (*other->coor)[1]);
  (*resultCoor)[2] = jrt::Math::max((*this->coor)[2], (*other->coor)[2]);
  (*resultCoor)[3] = jrt::Math::max((*this->coor)[3], (*other->coor)[3]);
  return new (jrt::kOp) Rectangle(this->layer, resultCoor);
}

fr::Shape* Rectangle::transformToBoardRel(fr::io::CoordinateTransform* coordinateTransform) {
  auto* boxCoor = new (jrt::kOp) jrt::Array<int32_t>(4);
  for (int32_t i = 0; i < 4; i++) {
    (*boxCoor)[i] =
        static_cast<int32_t>(jrt::Math::round(coordinateTransform->dsnToBoard((*this->coor)[i])));
  }

  fr::IntBox* result;
  if ((*boxCoor)[1] <= (*boxCoor)[3]) {
    // boxCoor describe lower left and upper right corner
    result = new (jrt::kGeom) fr::IntBox((*boxCoor)[0], (*boxCoor)[1], (*boxCoor)[2], (*boxCoor)[3]);
  } else {
    // boxCoor describe upper left and lower right corner
    result = new (jrt::kGeom) fr::IntBox((*boxCoor)[0], (*boxCoor)[3], (*boxCoor)[2], (*boxCoor)[1]);
  }
  return result;
}

fr::Shape* Rectangle::transformToBoard(fr::io::CoordinateTransform* coordinateTransform) {
  auto* currentPoint = new (jrt::kOp) jrt::Array<double>(2);
  (*currentPoint)[0] = jrt::Math::min((*coor)[0], (*coor)[2]);
  (*currentPoint)[1] = jrt::Math::min((*coor)[1], (*coor)[3]);
  fr::FloatPoint* lowerLeft = coordinateTransform->dsnToBoard(currentPoint);
  (*currentPoint)[0] = jrt::Math::max((*coor)[0], (*coor)[2]);
  (*currentPoint)[1] = jrt::Math::max((*coor)[1], (*coor)[3]);
  fr::FloatPoint* upperRight = coordinateTransform->dsnToBoard(currentPoint);
  return new (jrt::kGeom) fr::IntBox(lowerLeft->round(), upperRight->round());
}

void Rectangle::writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifier) {
  file->newLine();
  file->write("(rect ");
  identifier->write(this->layer->name, file);
  for (int32_t i = 0; i < coor->length; i++) {
    file->write(" ");
    file->write(jrt::str((*coor)[i]));
  }
  file->write(")");
}

void Rectangle::writeScopeInt(fr::IndentFileWriter* file, fr::IdentifierType* identifier) {
  file->newLine();
  file->write("(rect ");
  identifier->write(this->layer->name, file);
  for (int32_t i = 0; i < coor->length; i++) {
    file->write(" ");
    int32_t currentCoor = static_cast<int32_t>(jrt::Math::round((*coor)[i]));
    file->write(jrt::str(currentCoor));
  }
  file->write(")");
}

}  // namespace fr::dsn
