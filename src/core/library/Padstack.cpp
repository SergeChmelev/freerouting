// Port of app.freerouting.core.library.Padstack
#include "core/library/Padstack.h"

#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "core/library/PadOval.h"
#include "core/library/Padstacks.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntOctagon.h"
#include "logger/FRLogger.h"
#include "util/TextManager.h"

namespace fr {

namespace {
// Java s.replaceAll("[^0-9.]", "")
std::string keepDigitsAndDots(const std::string& s) {
  std::string r;
  for (char c : s) {
    if ((c >= '0' && c <= '9') || c == '.') {
      r += c;
    }
  }
  return r;
}

// Java String[] index access with ArrayIndexOutOfBoundsException.
const std::string& elementAt(const std::vector<std::string>& v, size_t i) {
  if (i >= v.size()) {
    throw jrt::ArrayIndexOutOfBoundsException("Index " + std::to_string(i) +
                                              " out of bounds for length " +
                                              std::to_string(v.size()));
  }
  return v[i];
}
}  // namespace

int32_t Padstack::compareTo(Padstack* other) {
  if (other == nullptr) {
    throw jrt::NullPointerException("other padstack cannot be null");
  }
  int32_t res = jrt::compareStringsIgnoreCase(this->name, other->name);
  if (res != 0) {
    return res;
  }
  res = jrt::compareStrings(this->name, other->name);
  if (res != 0) {
    return res;
  }
  return jrt::Integer::compare(this->id, other->id);
}

double Padstack::getDrillRadius() {
  if (this->explicitDrillRadius.has_value()) {
    return *this->explicitDrillRadius;
  }
  if (cachedDrillRadius.has_value()) {
    return *cachedDrillRadius;
  }
  double result;
  {
    size_t colonPos = name.find(':');
    if (colonPos != std::string::npos) {
      int32_t colonIndex = static_cast<int32_t>(colonPos);
      size_t underscorePos = name.find('_', colonPos);
      int32_t underscoreIndex =
          underscorePos == std::string::npos ? -1 : static_cast<int32_t>(underscorePos);
      std::string drillStr;
      if (underscoreIndex > colonIndex) {
        drillStr = name.substr(colonIndex + 1, underscoreIndex - (colonIndex + 1));
      } else {
        drillStr = name.substr(colonIndex + 1);
      }
      try {
        drillStr = keepDigitsAndDots(drillStr);
        double drillDia = jrt::Double::parseDouble(drillStr);
        size_t lastUnderscorePos = name.rfind('_', colonPos);
        int32_t lastUnderscore =
            lastUnderscorePos == std::string::npos ? -1 : static_cast<int32_t>(lastUnderscorePos);
        if (lastUnderscore >= 0) {
          std::string outerStr = name.substr(lastUnderscore + 1, colonIndex - (lastUnderscore + 1));
          double outerDia;
          if (jrt::contains(outerStr, "x")) {
            std::vector<std::string> parts = jrt::split(outerStr, 'x');
            double w = jrt::Double::parseDouble(keepDigitsAndDots(elementAt(parts, 0)));
            double h = jrt::Double::parseDouble(keepDigitsAndDots(elementAt(parts, 1)));
            outerDia = jrt::Math::min(w, h);
          } else {
            outerDia = jrt::Double::parseDouble(keepDigitsAndDots(outerStr));
          }
          if (outerDia > 0) {
            double actualOuterRadius = getSmallestRadius();
            if (actualOuterRadius > 0) {
              result = actualOuterRadius * (drillDia / outerDia);
              cachedDrillRadius = result;
              return *cachedDrillRadius;
            }
          }
        }
      } catch (const jrt::NumberFormatException&) {
        // Ignore
      } catch (const jrt::ArrayIndexOutOfBoundsException&) {
        // Ignore
      }
    }
  }
  result = getSmallestRadius() * 0.45;
  cachedDrillRadius = result;
  return *cachedDrillRadius;
}

double Padstack::getSmallestRadius() {
  double minRadius = jrt::Double::MAX_VALUE;
  for (ConvexShape* shape : *shapes) {
    if (shape != nullptr) {
      double radius =
          jrt::Math::min(shape->boundingBox()->width(), shape->boundingBox()->height()) / 2.0;
      if (radius < minRadius) {
        minRadius = radius;
      }
    }
  }
  return minRadius == jrt::Double::MAX_VALUE ? 0.0 : minRadius;
}

ConvexShape* Padstack::getShape(int32_t layer) {
  if (layer < 0 || layer >= shapes->length) {
    FRLogger::warn("Padstack.get_layer layer out of range");
    return nullptr;
  }
  return (*shapes)[layer];
}

PadOval* Padstack::getOval(int32_t layer) {
  if (ovals == nullptr || layer < 0 || layer >= ovals->length) {
    return nullptr;
  }
  return (*ovals)[layer];
}

jrt::Array<ConvexShape*>* Padstack::getAuxiliaryShapes(int32_t layer) {
  if (auxiliaryShapes == nullptr || layer < 0 || layer >= auxiliaryShapes->length) {
    return nullptr;
  }
  return (*auxiliaryShapes)[layer];
}

bool Padstack::hasAuxiliaryShapes() {
  if (auxiliaryShapes == nullptr) {
    return false;
  }
  for (jrt::Array<ConvexShape*>* aux : *auxiliaryShapes) {
    if (aux != nullptr && aux->length > 0) {
      return true;
    }
  }
  return false;
}

int32_t Padstack::fromLayer() {
  int32_t result = 0;
  while (result < shapes->length && (*shapes)[result] == nullptr) {
    ++result;
  }
  return result;
}

int32_t Padstack::toLayer() {
  int32_t result = shapes->length - 1;
  while (result >= 0 && (*shapes)[result] == nullptr) {
    --result;
  }
  return result;
}

jrt::Collection<Direction*>* Padstack::getTraceExitDirections(int32_t layer, double factor) {
  jrt::Collection<Direction*>* result = new jrt::LinkedList<Direction*>();
  if (layer < 0 || layer >= shapes->length) {
    return result;
  }
  ConvexShape* currentShape = (*shapes)[layer];
  if (currentShape == nullptr) {
    return result;
  }
  if (!(dynamic_cast<IntBox*>(currentShape) != nullptr ||
        dynamic_cast<IntOctagon*>(currentShape) != nullptr)) {
    return result;
  }
  IntBox* currentBox = currentShape->boundingBox();

  bool allDirs = jrt::Math::max(currentBox->width(), currentBox->height()) <
                 factor * jrt::Math::min(currentBox->width(), currentBox->height());

  if (allDirs || currentBox->width() >= currentBox->height()) {
    result->add(Direction::RIGHT());
    result->add(Direction::LEFT());
  }
  if (allDirs || currentBox->width() <= currentBox->height()) {
    result->add(Direction::UP());
    result->add(Direction::DOWN());
  }
  return result;
}

void Padstack::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new TextManager("app.freerouting.core.library.Padstack", locale);

  printer->appendBold(tm->getText("padstack") + " ");
  printer->appendBold(this->name);
  for (int32_t i = 0; i < shapes->length; i++) {
    if ((*shapes)[i] != nullptr) {
      printer->newline();
      printer->indent();
      printer->append(static_cast<Shape*>((*shapes)[i]), locale);
      printer->append(" " + tm->getText("on_layer") + " ");
      printer->append((*padstackList->boardLayerStructure->layers)[i]->name);
    }
  }
  printer->newline();
}

}  // namespace fr
