#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * Result of reading an area scope: the shapes, the clearance class and the area name.
 * PORT NOTE: Java's private constructor is public here (hoisted nested type); clearanceClassName
 * and areaName are nullable -> std::optional. areaName may be generated later on, if absent.
 */
class Shape_ReadAreaScopeResult final : public virtual jrt::Object {
 public:
  Shape_ReadAreaScopeResult(std::optional<std::string> areaName,
                            jrt::Collection<Shape*>* shapeList,
                            std::optional<std::string> clearanceClassName)
      : shapeList(shapeList),
        clearanceClassName(std::move(clearanceClassName)),
        areaName(std::move(areaName)) {}

  jrt::Collection<Shape*>* shapeList;
  std::optional<std::string> clearanceClassName;
  std::optional<std::string> areaName;  // may be generated later on, if areaName is null.
};

/** Describes a shape in a Specctra dsn file. */
class Shape : public virtual jrt::Object {
 public:
  Layer* layer;

  /**
   * Reads shape scope from a Specctra dsn file. If layerStructure == null, only Layer.PCB and
   * Layer.Signal are expected, no individual layers.
   */
  static Shape* readScope(IJFlexScanner* scanner, LayerStructure* layerStructure);

  /**
   * Reads a shape scope when the shape keyword (for example {@code path}) has already been
   * scanned.
   */
  static Shape* readScopeFromKeyword(IJFlexScanner* scanner, jrt::Object* keyword,
                                     LayerStructure* layerStructure);

  /** Reads an object of type PolylinePath from the dsn-file. */
  static PolylinePath* readPolylinePathScope(IJFlexScanner* scanner,
                                             LayerStructure* layerStructure);

  /**
   * Reads a shape, which may contain holes, from a specctra dsn-file. The first shape in the
   * shapeList of the result is the border of the area; the other shapes are holes. Window scopes
   * are skipped if skipWindowScopes is true.
   */
  static Shape_ReadAreaScopeResult* readAreaScope(IJFlexScanner* scanner,
                                                  LayerStructure* layerStructure,
                                                  bool skipWindowScopes);

  /** Reads a rectangle scope from a Specctra dsn file (null on error). */
  static Rectangle* readRectangleScope(IJFlexScanner* scanner, LayerStructure* layerStructure);

  /** Reads a polygon scope from a Specctra dsn file (null on error). */
  static Polygon* readPolygonScope(IJFlexScanner* scanner, LayerStructure* layerStructure);

  /** Reads a circle scope from a Specctra dsn file. */
  static Circle* readCircleScope(IJFlexScanner* scanner, LayerStructure* layerStructure);

  /** Reads an object of type Path from the dsn-file. */
  static PolygonPath* readPolygonPathScope(IJFlexScanner* scanner, LayerStructure* layerStructure);

  /** Transforms a shape with holes to the board coordinate system. */
  static fr::Area* transformAreaToBoard(jrt::Collection<Shape*>* area,
                                        fr::io::CoordinateTransform* coordinateTransform);

  /** Transforms a shape with holes to the board coordinate system, relative coordinates. */
  static fr::Area* transformAreaToBoardRel(jrt::Collection<Shape*>* area,
                                           fr::io::CoordinateTransform* coordinateTransform);

  /** Writes a shape scope to a Specctra dsn file. Throws jrt::IOException. */
  virtual void writeScope(fr::IndentFileWriter* file, fr::IdentifierType* identifier) = 0;

  /** Writes a shape scope to a Specctra session file in integer format. */
  virtual void writeScopeInt(fr::IndentFileWriter* file, fr::IdentifierType* identifier) = 0;

  virtual void writeHoleScope(fr::IndentFileWriter* file, fr::IdentifierType* identifierType);

  /** Transforms a specctra dsn shape to a geometry.planar.Shape. */
  virtual fr::Shape* transformToBoard(fr::io::CoordinateTransform* coordinateTransform) = 0;

  /** Returns the smallest axis parallel rectangle containing this shape. */
  virtual Rectangle* boundingBox() = 0;

  /**
   * Transforms the relative coordinates of a specctra dsn shape to a geometry.planar.Shape.
   */
  virtual fr::Shape* transformToBoardRel(fr::io::CoordinateTransform* coordinateTransform) = 0;

 protected:
  explicit Shape(Layer* layer) : layer(layer) {}

 private:
  /** Gets the layer with a certain name from the layer structure (null if not found). */
  static Layer* getLayer(LayerStructure* layerStructure, const std::string& layerName);

  /** Returns the layer whose name is exactly layerName, or null if there is none. */
  static Layer* findLayerByExactName(LayerStructure* layerStructure,
                                     const std::string& layerName);
};

}  // namespace fr::dsn
