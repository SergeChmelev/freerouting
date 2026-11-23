#include "autoroute/fanout/BgaFootprintDetector.h"

#include "autoroute/fanout/BgaEscapeDescriptor.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/RegularTileShape.h"
#include "geometry/planar/Shape.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/NetClass.h"
#include "settings/FanoutSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// BgaFootprintDetector_LocalPoint (Java local record)
// ---------------------------------------------------------------------------------------------

bool BgaFootprintDetector_LocalPoint::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<BgaFootprintDetector_LocalPoint>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf<Pin*>(pin_, other->pin_) && jrt::equalsOf<double>(u_, other->u_) &&
         jrt::equalsOf<double>(v_, other->v_);
}

int32_t BgaFootprintDetector_LocalPoint::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf<Pin*>(pin_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(u_));
  h = jrt::hashCombine(h, jrt::Double::hashCode(v_));
  return h;
}

std::string BgaFootprintDetector_LocalPoint::toString() {
  return "LocalPoint[pin=" + jrt::str(pin_) + ", u=" + jrt::str(u_) + ", v=" + jrt::str(v_) + "]";
}

// ---------------------------------------------------------------------------------------------
// BgaFootprintDetector
// ---------------------------------------------------------------------------------------------

jrt::List<BgaEscapeDescriptor*>* BgaFootprintDetector::detect(RoutingBoard* board,
                                                             RouterSettings* settings) {
  return detect(board, settings, nullptr);
}

jrt::List<BgaEscapeDescriptor*>* BgaFootprintDetector::detect(
    RoutingBoard* board, RouterSettings* settings,
    std::function<void(const std::string&)> infoLogger) {
  if (board == nullptr || board->components == nullptr) {
    return jrt::Collections::emptyList<BgaEscapeDescriptor*>();
  }

  int32_t minPins = 9;
  if (settings != nullptr && settings->fanout != nullptr &&
      settings->fanout->bgaEscapeMinPins.has_value()) {
    minPins = jrt::Math::max(9, *settings->fanout->bgaEscapeMinPins);
  }

  // Group board SMD pins by component
  jrt::Map<int32_t, jrt::List<Pin*>*>* smdPinsByComp =
      new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Pin*>*>();

  jrt::DisposeOnExit loopSource1(board->getSmdPins());
  for (Pin* pin : *loopSource1) {
    int32_t compId = pin->getComponentId();
    if (compId <= 0) {
      continue;
    }
    // Java: computeIfAbsent(compId, _ -> new ArrayList<>()).add(pin)
    jrt::List<Pin*>* list = smdPinsByComp->get(compId);
    if (list == nullptr) {
      list = new (jrt::kOp) jrt::ArrayList<Pin*>();
      smdPinsByComp->put(compId, list);
    }
    list->add(pin);
  }

  jrt::List<BgaEscapeDescriptor*>* result = new (jrt::kOp) jrt::ArrayList<BgaEscapeDescriptor*>();
  for (int32_t compId = 1; compId <= board->components->count(); compId++) {
    Component* comp = board->components->get(compId);
    if (comp == nullptr) {
      continue;
    }
    jrt::List<Pin*>* pins = smdPinsByComp->get(compId);
    if (pins == nullptr || pins->size() < minPins) {
      continue;
    }

    int32_t unescapedCount = 0;
    for (Pin* p : *pins) {
      if (p->netCount() > 0 && !p->hasIgnoredNets() && jrt::DisposeOnExit(p->getNormalContacts())->isEmpty()) {
        unescapedCount++;
      }
    }
    if (unescapedCount == 0) {
      continue;  // Component is already fully fanned out / escaped
    }

    BgaEscapeDescriptor* descriptor = analyzeComponent(board, comp, pins, settings);
    if (descriptor != nullptr) {
      result->add(descriptor);
      std::string msg = "BGA detector: identified component '" + comp->name + "' (" +
                        jrt::str(pins->size()) +
                        " pins, pitch=" + jrt::format("%.2f", descriptor->pitchUm()) +
                        " um, lattice=" +
                        BgaEscapeDescriptor_LatticeClass_toString(descriptor->latticeClass()) +
                        ", rings=" + jrt::str(descriptor->maxRing() + 1) + ")";
      FRLogger::info(msg);
      if (infoLogger) {
        infoLogger(msg);
      }
    }
  }

  if (result->isEmpty() && !smdPinsByComp->isEmpty()) {
    std::string msg = "BGA detector: evaluated " + jrt::str(smdPinsByComp->size()) +
                      " SMD component(s); none met the BGA area-array threshold (minPins=" +
                      jrt::str(minPins) + ").";
    FRLogger::info(msg);
    if (infoLogger) {
      infoLogger(msg);
    }
  }

  return result;
}

BgaEscapeDescriptor* BgaFootprintDetector::analyzeComponent(RoutingBoard* board, Component* comp,
                                                            jrt::List<Pin*>* pins,
                                                            RouterSettings* settings) {
  if (pins->isEmpty()) {
    return nullptr;
  }

  // 1. Compute Centroid
  double sumX = 0;
  double sumY = 0;
  for (Pin* p : *pins) {
    FloatPoint* c = p->getCenter()->toFloat();
    sumX += c->x;
    sumY += c->y;
  }
  double centroidX = sumX / pins->size();
  double centroidY = sumY / pins->size();

  // 2. Local frame rotation R(-theta)
  double rotDeg = comp->getRotationInDegree();
  double rad = jrt::Math::toRadians(-rotDeg);
  double cos = jrt::Math::cos(rad);
  double sin = jrt::Math::sin(rad);

  jrt::List<BgaFootprintDetector_LocalPoint*>* localPoints =
      new (jrt::kOp) jrt::ArrayList<BgaFootprintDetector_LocalPoint*>();

  double minU = jrt::Double::MAX_VALUE;
  double maxU = -jrt::Double::MAX_VALUE;
  double minV = jrt::Double::MAX_VALUE;
  double maxV = -jrt::Double::MAX_VALUE;

  for (Pin* p : *pins) {
    FloatPoint* c = p->getCenter()->toFloat();
    double dx = c->x - centroidX;
    double dy = c->y - centroidY;
    double u = dx * cos - dy * sin;
    double v = dx * sin + dy * cos;

    if (u < minU) minU = u;
    if (u > maxU) maxU = u;
    if (v < minV) minV = v;
    if (v > maxV) maxV = v;

    localPoints->add(new (jrt::kOp) BgaFootprintDetector_LocalPoint(p, u, v));
  }

  // 3. Compute pitch: nearest-neighbor distances in (u, v)
  double minPitch = jrt::Double::MAX_VALUE;
  for (int32_t i = 0; i < localPoints->size(); i++) {
    BgaFootprintDetector_LocalPoint* pi = localPoints->get(i);
    for (int32_t j = i + 1; j < localPoints->size(); j++) {
      BgaFootprintDetector_LocalPoint* pj = localPoints->get(j);
      double du = jrt::Math::abs(pi->u_ - pj->u_);
      double dv = jrt::Math::abs(pi->v_ - pj->v_);
      double dist = jrt::Math::hypot(du, dv);
      if (dist > 1.0 && dist < minPitch) {
        minPitch = dist;
      }
    }
  }

  if (minPitch == jrt::Double::MAX_VALUE || minPitch <= 0) {
    return nullptr;
  }

  // Enforce supported BGA pitch limit (up to 1.27 mm = 1270 um)
  double resolution = board->communication->getResolution(Unit::UM);
  double pitchUm = minPitch / resolution;
  if (pitchUm > 1270.0) {
    return nullptr;
  }

  double pitchTol = minPitch * 0.35;
  jrt::List<double>* uCoords = new (jrt::kOp) jrt::ArrayList<double>();
  jrt::List<double>* vCoords = new (jrt::kOp) jrt::ArrayList<double>();
  for (BgaFootprintDetector_LocalPoint* lp : *localPoints) {
    uCoords->add(lp->u_);
    vCoords->add(lp->v_);
  }
  int32_t cols = countDistinct(uCoords, pitchTol);
  int32_t rows = countDistinct(vCoords, pitchTol);

  if (rows < 3 || cols < 3) {
    return nullptr;
  }

  // Interior pin ratio check (area-array vs perimeter-only QFP)
  // A perimeter ring has 2*(rows + cols - 2) pins. Interior pins must be >= 25%
  int32_t perimeterSlots = jrt::Math::max(1, 2 * (rows + cols - 2));
  int32_t interiorCount = jrt::Math::max(0, pins->size() - perimeterSlots);
  double interiorRatio = static_cast<double>(interiorCount) / static_cast<double>(pins->size());

  // Distinguish dense area array from perimeter QFPs
  if (interiorRatio < 0.20 && pins->size() < (rows * cols * 0.5)) {
    return nullptr;
  }

  // 4. Lattice classification
  BgaEscapeDescriptor_LatticeClass lattice = BgaEscapeDescriptor_LatticeClass::ORTHOGONAL_GRID;
  if (pins->size() < (rows * cols * 0.85)) {
    lattice = BgaEscapeDescriptor_LatticeClass::DEPOPULATED_CAVITY_GRID;
  }

  // 5. Concentric Chebyshev Ring Peeling & Layer Allocation
  int32_t pinLayer = pins->isEmpty() ? 0 : pins->get(0)->firstLayer();

  // Compute component bounding box in board coordinates
  int32_t minBx = jrt::Integer::MAX_VALUE;
  int32_t maxBx = jrt::Integer::MIN_VALUE;
  int32_t minBy = jrt::Integer::MAX_VALUE;
  int32_t maxBy = jrt::Integer::MIN_VALUE;
  for (Pin* p : *pins) {
    Point* pt = p->getCenter();
    if (pt != nullptr) {
      IntPoint* ip = pt->toFloat()->round();
      if (ip->x < minBx) minBx = ip->x;
      if (ip->x > maxBx) maxBx = ip->x;
      if (ip->y < minBy) minBy = ip->y;
      if (ip->y > maxBy) maxBy = ip->y;
    }
  }
  IntBox* bgaBox = new (jrt::kGeom) IntBox(new (jrt::kGeom) IntPoint(minBx, minBy), new (jrt::kGeom) IntPoint(maxBx, maxBy));

  // Identify inner layers containing a BGA-spanning ConductionArea (solid pour)
  jrt::Set<int32_t>* pouredLayers = new (jrt::kOp) jrt::HashSet<int32_t>();
  if (jrt::DisposeOnExit(board->getConductionAreas()).get() != nullptr) {
    jrt::DisposeOnExit loopSource2(board->getConductionAreas());
    for (ConductionArea* ca : *loopSource2) {
      int32_t l = ca->getLayer();
      if (l != pinLayer && ca->getArea() != nullptr) {
        IntBox* caBox = ca->boundingBox();
        if (caBox != nullptr && caBox->intersects(bgaBox)) {
          IntBox* intersection = caBox->intersection(bgaBox);
          double interArea = static_cast<double>(intersection->ur->x - intersection->ll->x) *
                             (intersection->ur->y - intersection->ll->y);
          double bgaArea =
              static_cast<double>(bgaBox->ur->x - bgaBox->ll->x) * (bgaBox->ur->y - bgaBox->ll->y);
          if (caBox->contains(static_cast<RegularTileShape*>(bgaBox)) ||
              (bgaArea > 0 && interArea / bgaArea >= 0.5)) {
            pouredLayers->add(l);
          }
        }
      }
    }
  }

  jrt::List<int32_t>* signalLayers = new (jrt::kOp) jrt::ArrayList<int32_t>();
  if (pinLayer == 0) {
    for (int32_t l = 0; l < board->getLayerCount(); l++) {
      bool layerActive =
          (settings == nullptr || l >= settings->getLayerCount() || settings->getLayerActive(l));
      if ((*board->layerStructure->layers)[l]->isSignal && layerActive &&
          !pouredLayers->contains(l)) {
        signalLayers->add(l);
      }
    }
  } else {
    for (int32_t l = board->getLayerCount() - 1; l >= 0; l--) {
      bool layerActive =
          (settings == nullptr || l >= settings->getLayerCount() || settings->getLayerActive(l));
      if ((*board->layerStructure->layers)[l]->isSignal && layerActive &&
          !pouredLayers->contains(l)) {
        signalLayers->add(l);
      }
    }
  }
  if (signalLayers->isEmpty()) {
    signalLayers->add(pinLayer);
  }

  // Determine if surface escape channels between Ring 0 pads can fit a trace + 2 * clearance
  Shape* sampleShape = pins->isEmpty() ? nullptr : pins->get(0)->getShape(0);
  double padRadius = (sampleShape != nullptr) ? sampleShape->smallestRadius() : (minPitch * 0.25);
  double padDiameter = 2.0 * padRadius;
  double padGap = minPitch - padDiameter;

  NetClass* defaultNc = (board->rules != nullptr) ? board->rules->getDefaultNetClass() : nullptr;
  int32_t traceWidth =
      (defaultNc != nullptr)
          ? defaultNc->getTraceHalfWidth(pinLayer) * 2
          : (board->rules != nullptr ? board->rules->getMinAllowedTraceHalfWidth() * 2 : 100);
  int32_t clearance = (board->rules != nullptr && board->rules->clearanceMatrix != nullptr)
                          ? board->rules->clearanceMatrix->getValue(0, 0, pinLayer, false)
                          : 100;
  bool surfaceChannelsFit = padGap >= (traceWidth + 2 * clearance);

  // Pre-index ConductionAreas overlapping the BGA footprint by net number
  jrt::Map<int32_t, jrt::List<ConductionArea*>*>* conductionAreasByNet =
      new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<ConductionArea*>*>();
  if (jrt::DisposeOnExit(board->getConductionAreas()).get() != nullptr) {
    jrt::DisposeOnExit loopSource3(board->getConductionAreas());
    for (ConductionArea* ca : *loopSource3) {
      if (ca->getArea() != nullptr) {
        IntBox* caBox = ca->boundingBox();
        if (caBox != nullptr && caBox->intersects(bgaBox)) {
          for (int32_t n = 0; n < ca->netCount(); n++) {
            int32_t netNum = ca->getNetNumber(n);
            if (netNum > 0) {
              jrt::List<ConductionArea*>* list = conductionAreasByNet->get(netNum);
              if (list == nullptr) {
                list = new (jrt::kOp) jrt::ArrayList<ConductionArea*>();
                conductionAreasByNet->put(netNum, list);
              }
              list->add(ca);
            }
          }
        }
      }
    }
  }

  // Chebyshev distance in (u, v) from outer bounding box
  int32_t maxRing = 0;
  jrt::List<BgaEscapeDescriptor_BgaPinDescriptor*>* pinDescriptors =
      new (jrt::kOp) jrt::ArrayList<BgaEscapeDescriptor_BgaPinDescriptor*>();

  // Sort pins in canonical order (top-to-bottom, left-to-right in local frame)
  localPoints->sort(jrt::thenComparing<BgaFootprintDetector_LocalPoint*>(
      jrt::comparingDouble<BgaFootprintDetector_LocalPoint*>(
          [](BgaFootprintDetector_LocalPoint* lp) { return lp->v_; }),
      jrt::comparingDouble<BgaFootprintDetector_LocalPoint*>(
          [](BgaFootprintDetector_LocalPoint* lp) { return lp->u_; })));

  double centerU = (minU + maxU) / 2.0;
  double centerV = (minV + maxV) / 2.0;

  for (int32_t i = 0; i < localPoints->size(); i++) {
    BgaFootprintDetector_LocalPoint* lp = localPoints->get(i);
    Pin* pin = lp->pin_;

    double distLeft = jrt::Math::abs(lp->u_ - minU);
    double distRight = jrt::Math::abs(maxU - lp->u_);
    double distBottom = jrt::Math::abs(lp->v_ - minV);
    double distTop = jrt::Math::abs(maxV - lp->v_);

    double minEdgeDist = jrt::Math::min(jrt::Math::min(distLeft, distRight),
                                        jrt::Math::min(distBottom, distTop));
    int32_t ring = jrt::l2i(jrt::Math::round(minEdgeDist / minPitch));
    if (ring > maxRing) {
      maxRing = ring;
    }

    // Net disposition: classify as PLANE_DROP only when a matching ConductionArea covers this pin
    BgaEscapeDescriptor_PinDisposition disposition =
        BgaEscapeDescriptor_PinDisposition::SIGNAL_ESCAPE;
    int32_t planeTargetLayer = -1;
    int32_t netNo = pin->netCount() > 0 ? pin->getNetNumber(0) : 0;
    if (netNo <= 0 || pin->hasIgnoredNets()) {
      disposition = BgaEscapeDescriptor_PinDisposition::PASSIVE_OBSTACLE;
    } else {
      jrt::List<ConductionArea*>* netAreas = conductionAreasByNet->get(netNo);
      Point* pinCenter = pin->getCenter();
      if (netAreas != nullptr && pinCenter != nullptr) {
        FloatPoint* fp = pinCenter->toFloat();
        int32_t bestDist = jrt::Integer::MAX_VALUE;
        for (ConductionArea* ca : *netAreas) {
          if (ca->getArea()->contains(fp)) {
            int32_t caLayer = ca->getLayer();
            int32_t layerDist = (caLayer == pinLayer) ? jrt::Integer::MAX_VALUE - 1
                                                      : jrt::Math::abs(caLayer - pinLayer);
            if (layerDist < bestDist) {
              bestDist = layerDist;
              planeTargetLayer = caLayer;
            }
          }
        }
      }
      if (planeTargetLayer >= 0) {
        disposition = BgaEscapeDescriptor_PinDisposition::PLANE_DROP;
      }
    }

    // Partition breakout direction into 4 triangular edge-sectors using diagonals
    double deltaU = lp->u_ - centerU;
    double deltaV = lp->v_ - centerV;
    int32_t sector;
    if (jrt::Math::abs(deltaU) > jrt::Math::abs(deltaV)) {
      sector = (deltaU >= 0) ? 0 : 2;  // East (0), West (2)
    } else {
      sector = (deltaV >= 0) ? 1 : 3;  // North (1), South (3)
    }

    // Assign target routing layer based on ring depth and surface channel capacity
    int32_t targetLayer;
    if (disposition == BgaEscapeDescriptor_PinDisposition::PLANE_DROP) {
      targetLayer = planeTargetLayer;
    } else if (surfaceChannelsFit) {
      if (ring == 0 || ring == 1) {
        targetLayer = pinLayer;
      } else {
        int32_t layerIdx = jrt::Math::min(ring - 1, signalLayers->size() - 1);
        targetLayer = signalLayers->get(layerIdx);
      }
    } else {
      if (ring == 0) {
        targetLayer = pinLayer;
      } else {
        int32_t layerIdx = jrt::Math::min(ring, signalLayers->size() - 1);
        targetLayer = signalLayers->get(layerIdx);
      }
    }

    pinDescriptors->add(new (jrt::kOp) BgaEscapeDescriptor_BgaPinDescriptor(
        pin, lp->u_, lp->v_, ring, i, disposition, netNo, pin->clearanceClassIndex(), targetLayer,
        sector));
  }

  return new (jrt::kOp) BgaEscapeDescriptor(comp, rotDeg, pitchUm, minPitch, lattice, pinDescriptors, minU,
                                 maxU, minV, maxV, maxRing);
}

int32_t BgaFootprintDetector::countDistinct(jrt::List<double>* values, double tolerance) {
  if (values == nullptr || values->isEmpty()) {
    return 0;
  }
  jrt::List<double>* sorted = new (jrt::kOp) jrt::ArrayList<double>(values);
  // Java: Collections.sort (natural Double ordering)
  sorted->sort(jrt::naturalOrder<double>());
  int32_t count = 1;
  double current = sorted->get(0);
  for (int32_t i = 1; i < sorted->size(); i++) {
    if (sorted->get(i) - current > tolerance) {
      count++;
      current = sorted->get(i);
    }
  }
  return count;
}

}  // namespace fr
