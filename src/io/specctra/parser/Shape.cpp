#include "io/specctra/parser/Shape.h"

#include "datastructures/IndentFileWriter.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/PolylineArea.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"
#include "io/specctra/parser/Circle.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/Layer.h"
#include "io/specctra/parser/LayerStructure.h"
#include "io/specctra/parser/Polygon.h"
#include "io/specctra/parser/PolygonPath.h"
#include "io/specctra/parser/PolylinePath.h"
#include "io/specctra/parser/Rectangle.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "logger/FRLogger.h"

namespace fr::dsn {

Shape* Shape::readScope(IJFlexScanner* scanner, LayerStructure* layerStructure) {
  try {
    jrt::Object* nextToken = scanner->nextToken();
    if (nextToken == Keyword::OPEN_BRACKET()) {
      // overread the open bracket
      nextToken = scanner->nextToken();
    }
    return readScopeFromKeyword(scanner, nextToken, layerStructure);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Shape.read_scope: IO error scanning file", &e);
    return nullptr;
  }
}

Shape* Shape::readScopeFromKeyword(IJFlexScanner* scanner, jrt::Object* keyword,
                                   LayerStructure* layerStructure) {
  if (keyword == Keyword::RECTANGLE()) {
    return Shape::readRectangleScope(scanner, layerStructure);
  }
  if (keyword == Keyword::POLYGON()) {
    return Shape::readPolygonScope(scanner, layerStructure);
  }
  if (keyword == Keyword::CIRCLE()) {
    return Shape::readCircleScope(scanner, layerStructure);
  }
  if (keyword == Keyword::POLYGON_PATH()) {
    return Shape::readPolygonPathScope(scanner, layerStructure);
  }
  if (keyword == Keyword::POLYLINE_PATH()) {
    return Shape::readPolylinePathScope(scanner, layerStructure);
  }
  ScopeKeyword::skipScope(scanner);
  return nullptr;
}

Layer* Shape::getLayer(LayerStructure* layerStructure, const std::string& layerName) {
  Layer* layer;

  if (layerName == Keyword::PCB_SCOPE()->getName()) {
    layer = Layer::PCB();
  } else if (layerName == Keyword::SIGNAL()->getName()) {
    layer = Layer::SIGNAL();
  } else {
    if (layerStructure == nullptr) {
      FRLogger::warn("Shape.read_circle_scope: layerStructure != null expected");
      return nullptr;
    }

    int32_t layerIndex = layerStructure->getNo(layerName);
    if (layerIndex < 0 || layerIndex >= layerStructure->layers->length) {
      FRLogger::warn("Shape.read_circle_scope: layer with name '" + layerName +
                     "' not found in layer structure.");
      return nullptr;
    } else {
      layer = (*layerStructure->layers)[layerIndex];
    }
  }

  return layer;
}

Layer* Shape::findLayerByExactName(LayerStructure* layerStructure, const std::string& layerName) {
  // PORT NOTE: layerName is never null in C++ (Java also checks layerName == null).
  if (layerStructure == nullptr) {
    return nullptr;
  }
  for (Layer* layer : *layerStructure->layers) {
    if (layerName == layer->name) {
      return layer;
    }
  }
  return nullptr;
}

namespace {

/** Java: token instanceof Double d -> d; token instanceof Integer i -> i; else failure. */
bool tokenToDouble(jrt::Object* token, double& out) {
  if (auto* double1 = jrt::fast_cast<JDouble>(token)) {
    out = double1->value;
    return true;
  } else if (auto* integer = jrt::fast_cast<JInteger>(token)) {
    out = integer->value;
    return true;
  }
  return false;
}

}  // namespace

PolylinePath* Shape::readPolylinePathScope(IJFlexScanner* scanner,
                                           LayerStructure* layerStructure) {
  try {
    std::string layerName = scanner->nextString();
    Layer* const layer = getLayer(layerStructure, layerName);

    jrt::Object* nextToken;
    jrt::Collection<jrt::Object*>* cornerList = new jrt::LinkedList<jrt::Object*>();
    // read the width and the corners of the path
    for (;;) {
      nextToken = scanner->nextToken();
      if (nextToken == Keyword::CLOSED_BRACKET()) {
        break;
      }
      cornerList->add(nextToken);
    }
    if (cornerList->size() < 5) {
      FRLogger::warn("PolylinePath.read_scope: too few numbers in scope at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    jrt::Iterator<jrt::Object*>* it = cornerList->iterator();
    double width = 0;
    jrt::Object* nextObject = it->next();
    if (!tokenToDouble(nextObject, width)) {
      FRLogger::warn("PolylinePath.read_scope: number expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    jrt::Array<double>* corners = new jrt::Array<double>(cornerList->size() - 1);
    for (int32_t i = 0; i < corners->length; i++) {
      nextObject = it->next();
      if (!tokenToDouble(nextObject, (*corners)[i])) {
        FRLogger::warn("Shape.read_polygon_path_scope: number expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
    }
    return new PolylinePath(layer, width, corners);
  } catch (const jrt::IOException& e) {
    FRLogger::error("PolylinePath.read_scope: IO error scanning file", &e);
    return nullptr;
  }
}

Shape_ReadAreaScopeResult* Shape::readAreaScope(IJFlexScanner* scanner,
                                                LayerStructure* layerStructure,
                                                bool skipWindowScopes) {
  jrt::Collection<Shape*>* shapeList = new jrt::LinkedList<Shape*>();
  std::optional<std::string> clearanceClassName;
  std::optional<std::string> areaName;
  bool resultOk = true;
  jrt::Object* nextToken;
  try {
    nextToken = scanner->nextToken();
  } catch (const jrt::IOException&) {
    FRLogger::warn("Shape.read_area_scope: IO error scanning file at '" +
                   scanner->getScopeIdentifier() + "'");
    return nullptr;
  }
  if (auto* currentName = jrt::fast_cast<JString>(nextToken)) {
    scanner->setScopeIdentifier(currentName->value);
    if (!currentName->value.empty()) {
      areaName = currentName->value;
    }
  }
  Shape* currentShape = Shape::readScope(scanner, layerStructure);
  if (currentShape == nullptr) {
    FRLogger::warn("Shape.read_area_scope: could not read shape at '" +
                   scanner->getScopeIdentifier() + "'");
    resultOk = false;
  }
  shapeList->add(currentShape);
  nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("Shape.read_area_scope: IO error scanning file", &e);
      return nullptr;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("Shape.read_area_scope: unexpected end of file at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    if (nextToken == Keyword::CLOSED_BRACKET()) {
      // end of scope
      break;
    }

    if (prevToken == Keyword::OPEN_BRACKET()) {
      // a new scope is expected
      if (nextToken == Keyword::WINDOW() && !skipWindowScopes) {
        Shape* holeShape = Shape::readScope(scanner, layerStructure);
        shapeList->add(holeShape);
        // overread closing bracket
        try {
          nextToken = scanner->nextToken();
        } catch (const jrt::IOException& e) {
          FRLogger::error("Shape.read_area_scope: IO error scanning file", &e);
          return nullptr;
        }
        if (nextToken != Keyword::CLOSED_BRACKET()) {
          FRLogger::warn("Shape.read_area_scope: closed bracket expected at '" +
                         scanner->getScopeIdentifier() + "'");
          return nullptr;
        }

      } else if (nextToken == Keyword::CLEARANCE_CLASS()) {
        clearanceClassName = DsnFile::readStringScope(scanner);
      } else {
        // skip unknown scope
        ScopeKeyword::skipScope(scanner);
      }
    }
  }
  if (!resultOk) {
    return nullptr;
  }
  return new Shape_ReadAreaScopeResult(areaName, shapeList, clearanceClassName);
}

Rectangle* Shape::readRectangleScope(IJFlexScanner* scanner, LayerStructure* layerStructure) {
  try {
    std::string layerName = scanner->nextString();
    Layer* rectLayer = getLayer(layerStructure, layerName);
    if (rectLayer == nullptr) {
      rectLayer = getLayer(layerStructure, Keyword::SIGNAL()->getName());
    }

    jrt::Object* nextToken;
    jrt::Array<double>* rectCoor = new jrt::Array<double>(4);
    // fill the rectangle
    for (int32_t i = 0; i < 4; i++) {
      nextToken = scanner->nextToken();
      if (!tokenToDouble(nextToken, (*rectCoor)[i])) {
        FRLogger::warn("Shape.read_rectangle_scope: number expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
    }
    // overread the closing bracket

    nextToken = scanner->nextToken();
    if (nextToken != Keyword::CLOSED_BRACKET()) {
      FRLogger::warn("Shape.read_rectangle_scope ) expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    if (rectLayer == nullptr) {
      return nullptr;
    }
    return new Rectangle(rectLayer, rectCoor);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Shape.read_rectangle_scope: IO error scanning file", &e);
    return nullptr;
  }
}

Polygon* Shape::readPolygonScope(IJFlexScanner* scanner, LayerStructure* layerStructure) {
  try {
    Layer* polygonLayer = nullptr;
    bool layerOk = true;
    jrt::Object* nextToken = scanner->nextToken();
    if (nextToken == Keyword::PCB_SCOPE() || nextToken == Keyword::SIGNAL()) {
      // The scanner matches keywords regardless of case, so a layer that the CAD tool named
      // "Signal" arrives here as the keyword. Like getLayer() for the other shapes, only the
      // exact keyword spelling means the layer type; another spelling names that layer when
      // the board has one.
      Layer* namedLayer = jrt::fast_cast<Keyword>(nextToken)->getName() == scanner->yytext()
                              ? nullptr
                              : findLayerByExactName(layerStructure, scanner->yytext());
      if (namedLayer != nullptr) {
        polygonLayer = namedLayer;
      } else if (nextToken == Keyword::PCB_SCOPE()) {
        polygonLayer = Layer::PCB();
      } else {
        polygonLayer = Layer::SIGNAL();
      }
    } else {
      if (layerStructure == nullptr) {
        FRLogger::warn("Shape.read_polygon_scope: only layer types pcb or signal expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      auto* layerNameToken = jrt::fast_cast<JString>(nextToken);
      if (layerNameToken == nullptr) {
        FRLogger::warn("Shape.read_polygon_scope: layer name string expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      int32_t layerIndex = layerStructure->getNo(layerNameToken->value);
      if (layerIndex < 0 || layerIndex >= layerStructure->layers->length) {
        FRLogger::warn("Shape.read_polygon_scope: layer name '" + jrt::str(nextToken) +
                       "' not found in layer structure  at '" + scanner->getScopeIdentifier() +
                       "'");
        layerOk = false;
      } else {
        polygonLayer = (*layerStructure->layers)[layerIndex];
      }
    }

    // overread the aperture width
    nextToken = scanner->nextToken();

    jrt::Collection<jrt::Object*>* coorList = new jrt::LinkedList<jrt::Object*>();

    // read the coordinates of the polygon
    for (;;) {
      nextToken = scanner->nextToken();
      if (nextToken == nullptr) {
        FRLogger::warn("Shape.read_polygon_scope: unexpected end of file at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      if (nextToken == Keyword::OPEN_BRACKET()) {
        // unknown scope
        ScopeKeyword::skipScope(scanner);
        nextToken = scanner->nextToken();
      }
      if (nextToken == Keyword::CLOSED_BRACKET()) {
        break;
      }
      coorList->add(nextToken);
    }
    if (!layerOk) {
      return nullptr;
    }
    jrt::Array<double>* coorArr = new jrt::Array<double>(coorList->size());
    jrt::Iterator<jrt::Object*>* it = coorList->iterator();
    for (int32_t i = 0; i < coorArr->length; i++) {
      jrt::Object* nextObject = it->next();
      if (!tokenToDouble(nextObject, (*coorArr)[i])) {
        FRLogger::warn("Shape.read_polygon_scope: number expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
    }
    return new Polygon(polygonLayer, coorArr);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Rectangle.read_scope: IO error scanning file", &e);
    return nullptr;
  }
}

Circle* Shape::readCircleScope(IJFlexScanner* scanner, LayerStructure* layerStructure) {
  try {
    std::string layerName = scanner->nextString();
    Layer* circleLayer = getLayer(layerStructure, layerName);

    if (circleLayer == nullptr) {
      FRLogger::warn("Circle.read_circle_scope: layer with name '" + layerName +
                     "' not found in layer structure at '" + scanner->getScopeIdentifier() +
                     "'");
    }

    // fill the coordinates
    jrt::Object* nextToken;
    jrt::Array<double>* circleCoor = new jrt::Array<double>(3);
    int32_t currentIndex = 0;
    for (;;) {
      nextToken = scanner->nextToken();
      if (nextToken == Keyword::CLOSED_BRACKET()) {
        break;
      }
      if (currentIndex > 2) {
        FRLogger::warn("Shape.read_circle_scope: closed bracket expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      if (!tokenToDouble(nextToken, (*circleCoor)[currentIndex])) {
        FRLogger::warn("Shape.read_circle_scope: number expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
      ++currentIndex;
    }

    if (circleLayer == nullptr) {
      return nullptr;
    }
    return new Circle(circleLayer, circleCoor);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Shape.read_rectangle_scope: IO error scanning file", &e);
    return nullptr;
  }
}

PolygonPath* Shape::readPolygonPathScope(IJFlexScanner* scanner, LayerStructure* layerStructure) {
  try {
    std::string layerName = scanner->nextString();
    Layer* layer = getLayer(layerStructure, layerName);

    jrt::Object* nextToken;
    jrt::Collection<jrt::Object*>* cornerList = new jrt::LinkedList<jrt::Object*>();
    // read the width and the corners of the path
    for (;;) {
      nextToken = scanner->nextToken();
      if (nextToken == Keyword::OPEN_BRACKET()) {
        // unknown scope
        ScopeKeyword::skipScope(scanner);
        nextToken = scanner->nextToken();
      }
      if (nextToken == Keyword::CLOSED_BRACKET()) {
        break;
      }
      cornerList->add(nextToken);
    }

    // cornerList contains width + coordinate pairs
    if (cornerList->size() < 5) {
      // Single-point paths are not valid traces, skip them
      FRLogger::debug(
          "Shape.read_polygon_path_scope: skipping path with too few coordinates "
          "(need at least 2 points) at '" +
          scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    if (layer == nullptr) {
      return nullptr;
    }
    jrt::Iterator<jrt::Object*>* it = cornerList->iterator();
    double width = 0;
    jrt::Object* nextObject = it->next();
    if (!tokenToDouble(nextObject, width)) {
      FRLogger::warn("Shape.read_polygon_path_scope: number expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return nullptr;
    }
    jrt::Array<double>* coordinateArr = new jrt::Array<double>(cornerList->size() - 1);
    for (int32_t i = 0; i < coordinateArr->length; i++) {
      nextObject = it->next();
      if (!tokenToDouble(nextObject, (*coordinateArr)[i])) {
        FRLogger::warn("Shape.read_polygon_path_scope: number expected at '" +
                       scanner->getScopeIdentifier() + "'");
        return nullptr;
      }
    }
    return new PolygonPath(layer, width, coordinateArr);
  } catch (const jrt::IOException& e) {
    FRLogger::error("Shape.read_polygon_path_scope: IO error scanning file", &e);
    return nullptr;
  }
}

fr::Area* Shape::transformAreaToBoard(jrt::Collection<Shape*>* area,
                                      fr::io::CoordinateTransform* coordinateTransform) {
  int32_t holeCount = area->size() - 1;
  if (holeCount <= -1) {
    FRLogger::warn("Shape.transform_area_to_board: area.size() > 0 expected");
    return nullptr;
  }
  jrt::Iterator<Shape*>* it = area->iterator();
  Shape* boundary = it->next();
  fr::Shape* boundaryShape = boundary->transformToBoard(coordinateTransform);
  fr::Area* result;
  if (holeCount == 0) {
    result = boundaryShape;
  } else {
    // Area with holes
    auto* border = jrt::fast_cast<fr::PolylineShape>(boundaryShape);
    if (border == nullptr) {
      FRLogger::warn("Shape.transform_area_to_board: PolylineShape expected");
      return nullptr;
    }
    jrt::Array<fr::PolylineShape*>* holes = new jrt::Array<fr::PolylineShape*>(holeCount);
    for (int32_t i = 0; i < holes->length; i++) {
      fr::Shape* holeShape = it->next()->transformToBoard(coordinateTransform);
      if (jrt::fast_cast<fr::PolylineShape>(holeShape) == nullptr) {
        FRLogger::warn("Shape.transform_area_to_board: PolylineShape expected");
        return nullptr;
      }
      (*holes)[i] = jrt::fast_cast<fr::PolylineShape>(holeShape);
    }
    result = new fr::PolylineArea(border, holes);
  }
  return result;
}

fr::Area* Shape::transformAreaToBoardRel(jrt::Collection<Shape*>* area,
                                         fr::io::CoordinateTransform* coordinateTransform) {
  int32_t holeCount = area->size() - 1;
  if (holeCount <= -1) {
    FRLogger::warn("Shape.transform_area_to_board_rel: area.size() > 0 expected");
    return nullptr;
  }
  jrt::Iterator<Shape*>* it = area->iterator();
  Shape* boundary = it->next();
  fr::Shape* boundaryShape = boundary->transformToBoardRel(coordinateTransform);
  fr::Area* result;
  if (holeCount == 0) {
    result = boundaryShape;
  } else {
    // Area with holes
    auto* border = jrt::fast_cast<fr::PolylineShape>(boundaryShape);
    if (border == nullptr) {
      FRLogger::warn("Shape.transform_area_to_board_rel: PolylineShape expected");
      return nullptr;
    }
    jrt::Array<fr::PolylineShape*>* holes = new jrt::Array<fr::PolylineShape*>(holeCount);
    for (int32_t i = 0; i < holes->length; i++) {
      fr::Shape* holeShape = it->next()->transformToBoardRel(coordinateTransform);
      if (jrt::fast_cast<fr::PolylineShape>(holeShape) == nullptr) {
        FRLogger::warn("Shape.transform_area_to_board: PolylineShape expected");
        return nullptr;
      }
      (*holes)[i] = jrt::fast_cast<fr::PolylineShape>(holeShape);
    }
    result = new fr::PolylineArea(border, holes);
  }
  return result;
}

void Shape::writeHoleScope(fr::IndentFileWriter* file, fr::IdentifierType* identifierType) {
  file->startScope();
  file->write("window");
  this->writeScope(file, identifierType);
  file->endScope();
}

}  // namespace fr::dsn
