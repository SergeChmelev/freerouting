#include "autoroute/AutorouteDiagnostic.h"

#include "geometry/planar/TileShape.h"

namespace fr {

namespace {
class LambdaAutorouteDiagnosticSink : public AutorouteDiagnostic_Sink {
 public:
  explicit LambdaAutorouteDiagnosticSink(std::function<void(AutorouteDiagnostic*)> f)
      : f_(std::move(f)) {}
  void accept(AutorouteDiagnostic* diagnostic) override { f_(diagnostic); }

 private:
  std::function<void(AutorouteDiagnostic*)> f_;
};
}  // namespace

AutorouteDiagnostic_Sink* AutorouteDiagnostic_Sink::of(
    std::function<void(AutorouteDiagnostic*)> f) {
  return new LambdaAutorouteDiagnosticSink(std::move(f));
}

AutorouteDiagnostic::AutorouteDiagnostic(AutorouteDiagnostic_Kind kind, TileShape* shape,
                                         int32_t layer, double intensity)
    : kind_(kind), shape_(shape), layer_(layer), intensity_(intensity) {
  // PORT NOTE: Objects.requireNonNull(kind) cannot fail for a C++ enum value.
  if (shape == nullptr) {
    throw jrt::NullPointerException("shape");
  }
  if (intensity < 0) {
    throw jrt::IllegalArgumentException("intensity must not be negative");
  }
}

bool AutorouteDiagnostic::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<AutorouteDiagnostic>(o);
  if (other == nullptr) return false;
  return kind_ == other->kind_ && jrt::equalsOf<TileShape*>(shape_, other->shape_) &&
         layer_ == other->layer_ && jrt::equalsOf<double>(intensity_, other->intensity_);
}

int32_t AutorouteDiagnostic::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(kind_));
  h = jrt::hashCombine(h, jrt::hashOf<TileShape*>(shape_));
  h = jrt::hashCombine(h, jrt::hashOf(layer_));
  h = jrt::hashCombine(h, jrt::hashOf(intensity_));
  return h;
}

std::string AutorouteDiagnostic::toString() {
  return "AutorouteDiagnostic[kind=" + AutorouteDiagnostic_Kind_toString(kind_) +
         ", shape=" + jrt::str(static_cast<jrt::Object*>(shape_)) + ", layer=" + jrt::str(layer_) +
         ", intensity=" + jrt::str(intensity_) + "]";
}

}  // namespace fr
