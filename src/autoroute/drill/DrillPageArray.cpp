#include "autoroute/drill/DrillPageArray.h"

#include "autoroute/AutorouteDiagnostic.h"
#include "autoroute/drill/DrillPage.h"
#include "board/facade/RoutingBoard.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/TileShape.h"

namespace fr {

DrillPageArray::DrillPageArray(RoutingBoard* board, int32_t maxPageWidth) {
  this->bounds = board->boundingBox;
  double length = bounds->ur->x - bounds->ll->x;
  double height = bounds->ur->y - bounds->ll->y;
  this->columnCount = jrt::d2i(jrt::Math::ceil(length / maxPageWidth));
  this->rowCount = jrt::d2i(jrt::Math::ceil(height / maxPageWidth));
  this->pageWidth = jrt::d2i(jrt::Math::ceil(length / columnCount));
  this->pageHeight = jrt::d2i(jrt::Math::ceil(height / rowCount));
  this->pages = jrt::newArray2<DrillPage*>(rowCount, columnCount);
  for (int32_t j = 0; j < this->rowCount; j++) {
    for (int32_t i = 0; i < this->columnCount; i++) {
      int32_t llX = bounds->ll->x + i * pageWidth;
      int32_t urX;
      if (i == columnCount - 1) {
        urX = bounds->ur->x;
      } else {
        urX = llX + pageWidth;
      }
      int32_t llY = bounds->ll->y + j * pageHeight;
      int32_t urY;
      if (j == rowCount - 1) {
        urY = bounds->ur->y;
      } else {
        urY = llY + pageHeight;
      }
      (*(*pages)[j])[i] = new (jrt::kSearch) DrillPage(new (jrt::kGeom) IntBox(llX, llY, urX, urY), board);
    }
  }
}

void DrillPageArray::invalidate(TileShape* shape) {
  jrt::Collection<DrillPage*>* overlaps = overlappingPages(shape);
  for (DrillPage* currentPage : *overlaps) {
    currentPage->invalidate();
  }
}

jrt::Collection<DrillPage*>* DrillPageArray::overlappingPages(TileShape* shape) {
  jrt::Collection<DrillPage*>* result = new (jrt::kSearch) jrt::LinkedList<DrillPage*>();

  IntBox* shapeBox = shape->boundingBox()->intersection(this->bounds);

  int32_t minJ = jrt::d2i(
      jrt::Math::floor((static_cast<double>(shapeBox->ll->y - bounds->ll->y)) / static_cast<double>(pageHeight)));
  double maxJ = (static_cast<double>(shapeBox->ur->y - bounds->ll->y)) / static_cast<double>(pageHeight);

  int32_t minI = jrt::d2i(
      jrt::Math::floor((static_cast<double>(shapeBox->ll->x - bounds->ll->x)) / static_cast<double>(pageWidth)));
  double maxI = (static_cast<double>(shapeBox->ur->x - bounds->ll->x)) / static_cast<double>(pageWidth);

  for (int32_t j = minJ; j < maxJ; j++) {
    for (int32_t i = minI; i < maxI; i++) {
      DrillPage* currentPage = (*(*this->pages)[j])[i];
      TileShape* intersection = shape->intersection(currentPage->shape);
      if (intersection->dimension() > 1) {
        result->add((*(*this->pages)[j])[i]);
      }
    }
  }
  return result;
}

void DrillPageArray::reset() {
  for (int32_t j = 0; j < pages->length; j++) {
    jrt::Array<DrillPage*>* currentRow = (*pages)[j];
    for (int32_t i = 0; i < currentRow->length; i++) {
      (*currentRow)[i]->reset();
    }
  }
}

void DrillPageArray::emitDiagnostics(AutorouteDiagnostic_Sink* sink, double intensity) {
  if (sink == nullptr || intensity <= 0) {
    return;
  }
  for (int32_t j = 0; j < pages->length; j++) {
    jrt::Array<DrillPage*>* currentRow = (*pages)[j];
    for (int32_t i = 0; i < currentRow->length; i++) {
      (*currentRow)[i]->emitDiagnostics(sink, intensity);
    }
  }
}

}  // namespace fr
