#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Lattice topology classification. */
enum class BgaEscapeDescriptor_LatticeClass : int32_t { ORTHOGONAL_GRID, DEPOPULATED_CAVITY_GRID };

inline std::string BgaEscapeDescriptor_LatticeClass_name(BgaEscapeDescriptor_LatticeClass e) {
  switch (e) {
    case BgaEscapeDescriptor_LatticeClass::ORTHOGONAL_GRID: return "ORTHOGONAL_GRID";
    case BgaEscapeDescriptor_LatticeClass::DEPOPULATED_CAVITY_GRID: return "DEPOPULATED_CAVITY_GRID";
  }
  return "";
}
inline std::string BgaEscapeDescriptor_LatticeClass_toString(BgaEscapeDescriptor_LatticeClass e) {
  return BgaEscapeDescriptor_LatticeClass_name(e);
}
inline jrt::Array<BgaEscapeDescriptor_LatticeClass>* BgaEscapeDescriptor_LatticeClass_values() {
  auto* a = new jrt::Array<BgaEscapeDescriptor_LatticeClass>(2);
  for (int32_t i = 0; i < 2; ++i) (*a)[i] = static_cast<BgaEscapeDescriptor_LatticeClass>(i);
  return a;
}
inline BgaEscapeDescriptor_LatticeClass BgaEscapeDescriptor_LatticeClass_valueOf(const std::string& s) {
  for (int32_t i = 0; i < 2; ++i) {
    auto e = static_cast<BgaEscapeDescriptor_LatticeClass>(i);
    if (BgaEscapeDescriptor_LatticeClass_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant BgaEscapeDescriptor.LatticeClass." + s);
}

/** Disposition category for each BGA ball. */
enum class BgaEscapeDescriptor_PinDisposition : int32_t {
  /** Mechanical / unrouted NC balls or nets belonging to ignore_net_classes. */
  PASSIVE_OBSTACLE,
  /** Power/ground return pads on plane nets (eligible for plane drops or shared vias). */
  PLANE_DROP,
  /** Standard signal pins that require an escape stub to the perimeter. */
  SIGNAL_ESCAPE
};

inline std::string BgaEscapeDescriptor_PinDisposition_name(BgaEscapeDescriptor_PinDisposition e) {
  switch (e) {
    case BgaEscapeDescriptor_PinDisposition::PASSIVE_OBSTACLE: return "PASSIVE_OBSTACLE";
    case BgaEscapeDescriptor_PinDisposition::PLANE_DROP: return "PLANE_DROP";
    case BgaEscapeDescriptor_PinDisposition::SIGNAL_ESCAPE: return "SIGNAL_ESCAPE";
  }
  return "";
}
inline std::string BgaEscapeDescriptor_PinDisposition_toString(BgaEscapeDescriptor_PinDisposition e) {
  return BgaEscapeDescriptor_PinDisposition_name(e);
}
inline jrt::Array<BgaEscapeDescriptor_PinDisposition>* BgaEscapeDescriptor_PinDisposition_values() {
  auto* a = new jrt::Array<BgaEscapeDescriptor_PinDisposition>(3);
  for (int32_t i = 0; i < 3; ++i) (*a)[i] = static_cast<BgaEscapeDescriptor_PinDisposition>(i);
  return a;
}
inline BgaEscapeDescriptor_PinDisposition BgaEscapeDescriptor_PinDisposition_valueOf(const std::string& s) {
  for (int32_t i = 0; i < 3; ++i) {
    auto e = static_cast<BgaEscapeDescriptor_PinDisposition>(i);
    if (BgaEscapeDescriptor_PinDisposition_name(e) == s) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant BgaEscapeDescriptor.PinDisposition." + s);
}

/**
 * Per-pin descriptor within the component-local coordinate system (Java record):
 * pin, localU/localV (rotated around the component centroid), Chebyshev ring, canonicalIndex,
 * disposition, netNo, clearanceClassIdx, targetLayer, breakoutSector.
 */
class BgaEscapeDescriptor_BgaPinDescriptor : public virtual jrt::Object {
 private:
  Pin* pin_;
  double localU_;
  double localV_;
  int32_t ring_;
  int32_t canonicalIndex_;
  BgaEscapeDescriptor_PinDisposition disposition_;
  int32_t netNo_;
  int32_t clearanceClassIdx_;
  int32_t targetLayer_;
  int32_t breakoutSector_;

 public:
  BgaEscapeDescriptor_BgaPinDescriptor(
      Pin* pin,
      double localU,
      double localV,
      int32_t ring,
      int32_t canonicalIndex,
      BgaEscapeDescriptor_PinDisposition disposition,
      int32_t netNo,
      int32_t clearanceClassIdx,
      int32_t targetLayer,
      int32_t breakoutSector)
      : pin_(pin),
        localU_(localU),
        localV_(localV),
        ring_(ring),
        canonicalIndex_(canonicalIndex),
        disposition_(disposition),
        netNo_(netNo),
        clearanceClassIdx_(clearanceClassIdx),
        targetLayer_(targetLayer),
        breakoutSector_(breakoutSector) {}
  /** Backwards-compatible constructor defaulting targetLayer and breakoutSector to 0. */
  BgaEscapeDescriptor_BgaPinDescriptor(
      Pin* pin,
      double localU,
      double localV,
      int32_t ring,
      int32_t canonicalIndex,
      BgaEscapeDescriptor_PinDisposition disposition,
      int32_t netNo,
      int32_t clearanceClassIdx)
      : BgaEscapeDescriptor_BgaPinDescriptor(
            pin, localU, localV, ring, canonicalIndex, disposition, netNo, clearanceClassIdx, 0, 0) {}

  Pin* pin() { return pin_; }
  double localU() { return localU_; }
  double localV() { return localV_; }
  int32_t ring() { return ring_; }
  int32_t canonicalIndex() { return canonicalIndex_; }
  BgaEscapeDescriptor_PinDisposition disposition() { return disposition_; }
  int32_t netNo() { return netNo_; }
  int32_t clearanceClassIdx() { return clearanceClassIdx_; }
  int32_t targetLayer() { return targetLayer_; }
  int32_t breakoutSector() { return breakoutSector_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

/**
 * Geometric, topological, and net disposition descriptor for an area-array BGA package.
 * Encapsulates component-local (u, v) pin geometry, rotation angle, pitch P, and ring depths.
 * (Java record.)
 */
class BgaEscapeDescriptor : public virtual jrt::Object {
 private:
  Component* component_;
  double rotationDeg_;
  double pitchUm_;
  double pitchBoardUnits_;
  BgaEscapeDescriptor_LatticeClass latticeClass_;
  jrt::List<BgaEscapeDescriptor_BgaPinDescriptor*>* pins_;
  double minX_;
  double maxX_;
  double minY_;
  double maxY_;
  int32_t maxRing_;

 public:
  BgaEscapeDescriptor(
      Component* component,
      double rotationDeg,
      double pitchUm,
      double pitchBoardUnits,
      BgaEscapeDescriptor_LatticeClass latticeClass,
      jrt::List<BgaEscapeDescriptor_BgaPinDescriptor*>* pins,
      double minX,
      double maxX,
      double minY,
      double maxY,
      int32_t maxRing)
      : component_(component),
        rotationDeg_(rotationDeg),
        pitchUm_(pitchUm),
        pitchBoardUnits_(pitchBoardUnits),
        latticeClass_(latticeClass),
        pins_(pins),
        minX_(minX),
        maxX_(maxX),
        minY_(minY),
        maxY_(maxY),
        maxRing_(maxRing) {}

  Component* component() { return component_; }
  double rotationDeg() { return rotationDeg_; }
  double pitchUm() { return pitchUm_; }
  double pitchBoardUnits() { return pitchBoardUnits_; }
  BgaEscapeDescriptor_LatticeClass latticeClass() { return latticeClass_; }
  jrt::List<BgaEscapeDescriptor_BgaPinDescriptor*>* pins() { return pins_; }
  double minX() { return minX_; }
  double maxX() { return maxX_; }
  double minY() { return minY_; }
  double maxY() { return maxY_; }
  int32_t maxRing() { return maxRing_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;
};

}  // namespace fr
