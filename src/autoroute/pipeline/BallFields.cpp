// Port of app.freerouting.autoroute.pipeline.BallFields
#include "autoroute/pipeline/BallFields.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Components.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"

namespace fr {

BallFields::BallFields(jrt::List<BallFields_Field*>* fields, double cost)
    : fields(new jrt::ArrayList<BallFields_Field*>(fields)), cost(cost) {}

BallFields* BallFields::of(BasicBoard* board, double cost) {
  if (!(cost > 0)) {
    return nullptr;
  }
  jrt::List<BallFields_Field*>* found = new jrt::ArrayList<BallFields_Field*>();
  for (int32_t id = 1; id <= board->components->count(); id++) {
    jrt::Collection<Pin*>* pins = board->getComponentPins(id);
    BallFields_Field* field = fieldOf(pins);
    if (field != nullptr) {
      found->add(field);
    }
  }
  return found->isEmpty() ? nullptr : new BallFields(found, cost);
}

BallFields_Field* BallFields::fieldOf(jrt::Collection<Pin*>* pinCollection) {
  if (pinCollection == nullptr || pinCollection->size() < MIN_BALLS) {
    return nullptr;
  }
  jrt::List<Pin*>* pins = new jrt::ArrayList<Pin*>(pinCollection);
  int32_t layer = pins->get(0)->firstLayer();
  jrt::List<FloatPoint*>* centres = new jrt::ArrayList<FloatPoint*>(pins->size());
  for (Pin* pin : *pins) {
    if (pin->firstLayer() != layer || pin->lastLayer() != layer) {
      return nullptr;
    }
    centres->add(pin->getCenter()->toFloat());
  }
  return fieldOf(centres, layer);
}

BallFields_Field* BallFields::fieldOf(jrt::List<FloatPoint*>* centres, int32_t layer) {
  if (centres == nullptr || centres->size() < MIN_BALLS) {
    return nullptr;
  }
  // The pitch: the smallest distance between two balls.
  double pitch = jrt::Double::MAX_VALUE;
  for (int32_t i = 0; i < centres->size(); i++) {
    for (int32_t j = i + 1; j < centres->size(); j++) {
      double d = centres->get(i)->distance(centres->get(j));
      if (d > 0 && d < pitch) {
        pitch = d;
      }
    }
  }
  if (pitch == jrt::Double::MAX_VALUE) {
    return nullptr;
  }
  jrt::List<double>* ys = new jrt::ArrayList<double>(centres->size());
  jrt::List<double>* xs = new jrt::ArrayList<double>(centres->size());
  double minX = jrt::Double::MAX_VALUE;
  double maxX = -jrt::Double::MAX_VALUE;
  double minY = jrt::Double::MAX_VALUE;
  double maxY = -jrt::Double::MAX_VALUE;
  for (FloatPoint* c : *centres) {
    ys->add(c->y);
    xs->add(c->x);
    if (c->x < minX) {
      minX = c->x;
    }
    if (c->x > maxX) {
      maxX = c->x;
    }
    if (c->y < minY) {
      minY = c->y;
    }
    if (c->y > maxY) {
      maxY = c->y;
    }
  }
  int32_t rows = distinct(ys, pitch / 4.0);
  int32_t columns = distinct(xs, pitch / 4.0);
  if (rows < 3 || columns < 3 || centres->size() * 2 < rows * columns) {
    return nullptr;
  }
  return new BallFields_Field(layer, minX - pitch, minY - pitch, maxX + pitch, maxY + pitch);
}

int32_t BallFields::distinct(jrt::List<double>* values, double tolerance) {
  int32_t count = 0;
  double last = jrt::Double::NEGATIVE_INFINITY;
  jrt::List<double>* sorted = new jrt::ArrayList<double>(values);
  sorted->sort([](double a, double b) { return jrt::Double::compare(a, b); });
  for (double v : *sorted) {
    if (v - last > tolerance) {
      count++;
    }
    last = v;
  }
  return count;
}

double BallFields::pathCost(int32_t layer, double fromX, double fromY, double toX, double toY) {
  double inside = 0;
  for (BallFields_Field* field : *fields) {
    if (field->layer() == layer) {
      inside += insideLength(field, fromX, fromY, toX, toY);
    }
  }
  return inside * cost;
}

double BallFields::insideLength(
    BallFields_Field* field, double fromX, double fromY, double toX, double toY) {
  double dx = toX - fromX;
  double dy = toY - fromY;
  jrt::Array<double>* t = jrt::Array<double>::of({0.0, 1.0});
  if (!clip(-dx, fromX - field->x0(), t)
      || !clip(dx, field->x1() - fromX, t)
      || !clip(-dy, fromY - field->y0(), t)
      || !clip(dy, field->y1() - fromY, t)) {
    return 0.0;
  }
  return (*t)[1] > (*t)[0] ? ((*t)[1] - (*t)[0]) * jrt::Math::sqrt(dx * dx + dy * dy) : 0.0;
}

bool BallFields::clip(double p, double q, jrt::Array<double>* t) {
  if (p == 0) {
    return q >= 0;
  }
  double r = q / p;
  if (p < 0) {
    if (r > (*t)[1]) {
      return false;
    }
    if (r > (*t)[0]) {
      (*t)[0] = r;
    }
  } else {
    if (r < (*t)[0]) {
      return false;
    }
    if (r < (*t)[1]) {
      (*t)[1] = r;
    }
  }
  return true;
}

}  // namespace fr
