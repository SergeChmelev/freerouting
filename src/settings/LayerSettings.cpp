#include "settings/LayerSettings.h"

#include <typeinfo>

#include "settings/SettingsCopy.h"

namespace fr {

namespace {
int32_t hashOf(const std::optional<bool>& v) { return v ? jrt::Boolean::hashCode(*v) : 0; }
int32_t hashOf(const std::optional<double>& v) { return v ? jrt::Double::hashCode(*v) : 0; }
// java.util.Objects.equals on boxed Doubles uses Double.equals (bit comparison).
bool doubleEquals(const std::optional<double>& a, const std::optional<double>& b) {
  if (!a.has_value() || !b.has_value()) return a.has_value() == b.has_value();
  return jrt::Double::doubleToLongBits(*a) == jrt::Double::doubleToLongBits(*b);
}
}  // namespace

LayerSettings* LayerSettings::clone() {
  auto* copy = new LayerSettings();
  copy->routable = this->routable;
  copy->preferredDirectionHorizontal = this->preferredDirectionHorizontal;
  copy->bendCost = this->bendCost;
  copy->preferredDirectionTraceCost = this->preferredDirectionTraceCost;
  copy->undesiredDirectionTraceCost = this->undesiredDirectionTraceCost;
  return copy;
}

bool LayerSettings::equals(jrt::Object* other) {
  if (this == other) {
    return true;
  }
  if (other == nullptr || typeid(*this) != typeid(*other)) {
    return false;
  }
  auto* that = jrt::fast_cast<LayerSettings>(other);
  return routable == that->routable &&
         preferredDirectionHorizontal == that->preferredDirectionHorizontal &&
         doubleEquals(bendCost, that->bendCost) &&
         doubleEquals(preferredDirectionTraceCost, that->preferredDirectionTraceCost) &&
         doubleEquals(undesiredDirectionTraceCost, that->undesiredDirectionTraceCost);
}

int32_t LayerSettings::hashCode() {
  // java.util.Objects.hash(...) == Arrays.hashCode(Object[])
  int32_t h = 1;
  h = jrt::hashCombine(h, hashOf(routable));
  h = jrt::hashCombine(h, hashOf(preferredDirectionHorizontal));
  h = jrt::hashCombine(h, hashOf(bendCost));
  h = jrt::hashCombine(h, hashOf(preferredDirectionTraceCost));
  h = jrt::hashCombine(h, hashOf(undesiredDirectionTraceCost));
  return h;
}

int32_t LayerSettings::copyNewValuesFrom(LayerSettings* s) {
  int32_t n = 0;
  n += settings_copy::value(routable, s->routable);
  n += settings_copy::value(preferredDirectionHorizontal, s->preferredDirectionHorizontal);
  n += settings_copy::value(bendCost, s->bendCost);
  n += settings_copy::value(preferredDirectionTraceCost, s->preferredDirectionTraceCost);
  n += settings_copy::value(undesiredDirectionTraceCost, s->undesiredDirectionTraceCost);
  return n;
}

}  // namespace fr
