#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::io {

/**
 * Computes transformations between board coordinates and external coordinates, such as Specctra
 * DSN or KiCad JSON coordinates.
 */
class CoordinateTransform : public virtual jrt::Object {
 public:
  /** Creates a new instance of CoordinateTransform. */
  CoordinateTransform(double scaleFactor, double baseX, double baseY)
      : scaleFactor(scaleFactor), baseX(baseX), baseY(baseY) {}

  /** Scales a value from the board to the external coordinate system. */
  virtual double boardToDsn(double value) { return value / scaleFactor; }

  /** Transforms a point from the board to the external coordinate system. */
  virtual jrt::Array<double>* boardToDsn(fr::FloatPoint* point);

  /** Transforms points from the board to the external coordinate system. */
  virtual jrt::Array<double>* boardToDsn(jrt::Array<fr::FloatPoint*>* points);

  /** Transforms lines from the board to the external coordinate system. */
  virtual jrt::Array<double>* boardToDsn(jrt::Array<fr::Line*>* lines);

  /** Transforms a vector from the board to the external coordinate system. */
  virtual jrt::Array<double>* boardToDsn(fr::Vector* vector);

  /** Transforms a box from the board to the external coordinate system. */
  virtual jrt::Array<double>* boardToDsn(fr::IntBox* box);

  /** Transforms a board shape to an external shape (null if not supported). */
  virtual fr::dsn::Shape* boardToDsn(fr::Shape* boardShape, fr::dsn::Layer* layer);

  /** Transforms a point to relative external (vector) coordinates. */
  virtual jrt::Array<double>* boardToDsnRel(fr::FloatPoint* point);

  /** Transforms points to relative external (vector) coordinates. */
  virtual jrt::Array<double>* boardToDsnRel(jrt::Array<fr::FloatPoint*>* points);

  /** Transforms a box to relative external (vector) coordinates. */
  virtual jrt::Array<double>* boardToDsnRel(fr::IntBox* box);

  /** Transforms a board shape to a relative external shape (null if not supported). */
  virtual fr::dsn::Shape* boardToDsnRel(fr::Shape* boardShape, fr::dsn::Layer* layer);

  /** Scales a value from the external to the board coordinate system. */
  virtual double dsnToBoard(double value) { return value * scaleFactor; }

  /** Transforms an external tuple to a board point. */
  virtual fr::FloatPoint* dsnToBoard(jrt::Array<double>* tuple);

  /** Transforms an external tuple to a board point in relative coordinates. */
  virtual fr::FloatPoint* dsnToBoardRel(jrt::Array<double>* tuple);

 private:
  double scaleFactor;
  double baseX;
  double baseY;
};

}  // namespace fr::io
