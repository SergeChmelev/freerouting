#include "autoroute/fanout/BgaEscapeCommitter.h"

#include "autoroute/fanout/BgaEscapeDescriptor.h"
#include "autoroute/fanout/BgaEscapeSolver.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/FixedState.h"
#include "core/library/BoardLibrary.h"
#include "core/library/Padstack.h"
#include "core/library/Padstacks.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/DefaultItemClearanceClasses.h"
#include "rules/HoleSpacing.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"
#include "rules/ViaInfo.h"
#include "rules/ViaInfos.h"
#include "rules/ViaRule.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// BgaEscapeCommitter_CommitResult (Java record)
// ---------------------------------------------------------------------------------------------

BgaEscapeCommitter_CommitResult::BgaEscapeCommitter_CommitResult(int32_t pinsAttempted,
                                                                 int32_t pinsCommitted,
                                                                 int32_t viasInserted,
                                                                 int32_t tracesInserted,
                                                                 int32_t drcRejections)
    : BgaEscapeCommitter_CommitResult(pinsAttempted, pinsCommitted, viasInserted, tracesInserted,
                                      drcRejections, jrt::Collections::emptySet<Pin*>()) {}

bool BgaEscapeCommitter_CommitResult::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<BgaEscapeCommitter_CommitResult>(o);
  if (other == nullptr) return false;
  return pinsAttempted_ == other->pinsAttempted_ && pinsCommitted_ == other->pinsCommitted_ &&
         viasInserted_ == other->viasInserted_ && tracesInserted_ == other->tracesInserted_ &&
         drcRejections_ == other->drcRejections_ &&
         jrt::equalsOf<jrt::Set<Pin*>*>(committedPins_, other->committedPins_);
}

int32_t BgaEscapeCommitter_CommitResult::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, pinsAttempted_);
  h = jrt::hashCombine(h, pinsCommitted_);
  h = jrt::hashCombine(h, viasInserted_);
  h = jrt::hashCombine(h, tracesInserted_);
  h = jrt::hashCombine(h, drcRejections_);
  h = jrt::hashCombine(h, jrt::hashOf<jrt::Set<Pin*>*>(committedPins_));
  return h;
}

std::string BgaEscapeCommitter_CommitResult::toString() {
  return "CommitResult[pinsAttempted=" + jrt::str(pinsAttempted_) +
         ", pinsCommitted=" + jrt::str(pinsCommitted_) +
         ", viasInserted=" + jrt::str(viasInserted_) +
         ", tracesInserted=" + jrt::str(tracesInserted_) +
         ", drcRejections=" + jrt::str(drcRejections_) +
         ", committedPins=" + jrt::str(committedPins_) + "]";
}

// ---------------------------------------------------------------------------------------------
// BgaEscapeCommitter_PlaneViaKey (Java local record)
// ---------------------------------------------------------------------------------------------

bool BgaEscapeCommitter_PlaneViaKey::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<BgaEscapeCommitter_PlaneViaKey>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf<IntPoint*>(location_, other->location_) && netNo_ == other->netNo_;
}

int32_t BgaEscapeCommitter_PlaneViaKey::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf<IntPoint*>(location_));
  h = jrt::hashCombine(h, netNo_);
  return h;
}

std::string BgaEscapeCommitter_PlaneViaKey::toString() {
  return "PlaneViaKey[location=" + jrt::str(location_) + ", netNo=" + jrt::str(netNo_) + "]";
}

// ---------------------------------------------------------------------------------------------
// BgaEscapeCommitter_ResolvedVia (Java private record)
// ---------------------------------------------------------------------------------------------

bool BgaEscapeCommitter_ResolvedVia::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<BgaEscapeCommitter_ResolvedVia>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf<Padstack*>(padstack_, other->padstack_) &&
         clearanceClassIdx_ == other->clearanceClassIdx_;
}

int32_t BgaEscapeCommitter_ResolvedVia::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf<Padstack*>(padstack_));
  h = jrt::hashCombine(h, clearanceClassIdx_);
  return h;
}

std::string BgaEscapeCommitter_ResolvedVia::toString() {
  return "ResolvedVia[padstack=" + jrt::str(padstack_) +
         ", clearanceClassIdx=" + jrt::str(clearanceClassIdx_) + "]";
}

// ---------------------------------------------------------------------------------------------
// BgaEscapeCommitter
// ---------------------------------------------------------------------------------------------

BgaEscapeCommitter_CommitResult* BgaEscapeCommitter::commit(
    RoutingBoard* board, BgaEscapeDescriptor* bga,
    jrt::List<BgaEscapeSolver_PinEscapeProposal*>* proposals) {
  if (board == nullptr || board->rules == nullptr || bga == nullptr || proposals == nullptr ||
      proposals->isEmpty()) {
    return new BgaEscapeCommitter_CommitResult(0, 0, 0, 0, 0);
  }

  int32_t attempted = 0;
  int32_t committed = 0;
  int32_t viasInserted = 0;
  int32_t tracesInserted = 0;
  int32_t drcRejections = 0;

  double rotDeg = bga->rotationDeg();
  double rad = jrt::Math::toRadians(rotDeg);
  double cos = jrt::Math::cos(rad);
  double sin = jrt::Math::sin(rad);

  // Maintain placed plane vias for deduplication of shared ground/power vias
  jrt::Map<BgaEscapeCommitter_PlaneViaKey*, Via*>* placedPlaneVias =
      new jrt::HashMap<BgaEscapeCommitter_PlaneViaKey*, Via*>();
  // Maintain committed via locations to prevent double-via collisions
  jrt::Set<IntPoint*>* committedViaLocations = new jrt::HashSet<IntPoint*>();
  // Maintain successfully escaped pins to bypass redundant CPU fanout retries
  jrt::Set<Pin*>* committedPins = new jrt::HashSet<Pin*>();

  for (BgaEscapeSolver_PinEscapeProposal* proposal : *proposals) {
    // PORT NOTE: Thread.currentThread().isInterrupted() is always false (single-threaded port).
    if (false) {
      FRLogger::info("BGA escape committer cancelled by interrupt");
      break;
    }

    if (!proposal->escaped()) {
      continue;
    }

    BgaEscapeDescriptor_BgaPinDescriptor* pd = proposal->pinDescriptor();
    Pin* pin = pd->pin();
    if (pin == nullptr || committedPins->contains(pin)) {
      continue;
    }
    if (!pin->getNormalContacts()->isEmpty()) {
      committedPins->add(pin);
      continue;  // Pin already escaped or connected
    }

    attempted++;
    Point* pinCenter = pin->getCenter();
    if (pinCenter == nullptr) {
      drcRejections++;
      continue;
    }

    int32_t netNo = pd->netNo();
    jrt::Array<int32_t>* netNumbers =
        (netNo > 0) ? jrt::Array<int32_t>::of({netNo}) : new jrt::Array<int32_t>(0);
    int32_t pinLayer = pin->firstLayer();
    int32_t targetLayer = pd->targetLayer();
    int32_t sector = pd->breakoutSector();
    int32_t ring = pd->ring();

    Net* net = (board->rules->nets != nullptr && netNo > 0) ? board->rules->nets->get(netNo)
                                                            : nullptr;
    NetClass* nc = (net != nullptr) ? net->getNetClass() : nullptr;
    int32_t traceHalfWidthPin = (nc != nullptr) ? nc->getTraceHalfWidth(pinLayer)
                                                : board->rules->getMinAllowedTraceHalfWidth();
    int32_t traceHalfWidthTarget = (nc != nullptr) ? nc->getTraceHalfWidth(targetLayer)
                                                   : board->rules->getMinAllowedTraceHalfWidth();

    int32_t traceClClass =
        (nc != nullptr && nc->defaultItemClearanceClasses != nullptr)
            ? nc->defaultItemClearanceClasses->get(DefaultItemClearanceClasses_ItemClass::TRACE)
            : pd->clearanceClassIdx();
    int32_t viaClClass =
        (nc != nullptr && nc->defaultItemClearanceClasses != nullptr)
            ? nc->defaultItemClearanceClasses->get(DefaultItemClearanceClasses_ItemClass::VIA)
            : pd->clearanceClassIdx();

    double exitMargin = bga->pitchBoardUnits() * 1.5;
    double p = bga->pitchBoardUnits();
    double minPerimeterClearance = p * 0.25;
    double safetyMargin = jrt::Math::max(1.0, jrt::Math::min(10.0, traceHalfWidthPin * 0.1));

    // Case A: Ring 0 direct perimeter escape on pinLayer (no via)
    if (ring == 0 && targetLayer == pinLayer &&
        pd->disposition() == BgaEscapeDescriptor_PinDisposition::SIGNAL_ESCAPE) {
      double localExitU = pd->localU();
      double localExitV = pd->localV();
      if (sector == 0) {
        localExitU = bga->maxX() + exitMargin;
      } else if (sector == 1) {
        localExitV = bga->maxY() + exitMargin;
      } else if (sector == 2) {
        localExitU = bga->minX() - exitMargin;
      } else {
        localExitV = bga->minY() - exitMargin;
      }

      IntPoint* globalExit =
          toGlobalPoint(pinCenter, pd->localU(), pd->localV(), localExitU, localExitV, cos, sin);
      double maxAllowed = board->checkTraceSegment(pinCenter, globalExit, pinLayer, netNumbers,
                                                   traceHalfWidthPin, traceClClass, false);
      double fullDist = pinCenter->toFloat()->distance(globalExit->toFloat());

      if (maxAllowed < fullDist) {
        double distToPerimeter = getDistToPerimeter(pd, sector, bga);
        if (maxAllowed >= distToPerimeter + minPerimeterClearance) {
          double safeLen = jrt::Math::max(distToPerimeter + minPerimeterClearance,
                                          maxAllowed - safetyMargin);
          globalExit = interpolatePoint(pinCenter, globalExit, safeLen / fullDist);
        } else {
          drcRejections++;
          continue;
        }
      }

      Polyline* polyline = new Polyline(jrt::Array<Point*>::of({pinCenter, globalExit}));
      board->insertTrace(polyline, pinLayer, traceHalfWidthPin, netNumbers, traceClClass,
                         FixedState::USER_FIXED);
      if (pin->getNormalContacts()->isEmpty()) {
        drcRejections++;
        continue;
      }
      tracesInserted++;
      committed++;
      committedPins->add(pin);
      continue;
    }

    // Case B: Ring 1 2-segment half-pitch dogleg on pinLayer (no via)
    if (ring == 1 && targetLayer == pinLayer &&
        pd->disposition() == BgaEscapeDescriptor_PinDisposition::SIGNAL_ESCAPE) {
      double centerU = (bga->minX() + bga->maxX()) / 2.0;
      double centerV = (bga->minY() + bga->maxY()) / 2.0;
      double sgnU = (pd->localU() >= centerU) ? 1.0 : -1.0;
      double sgnV = (pd->localV() >= centerV) ? 1.0 : -1.0;

      double localJogU = pd->localU();
      double localJogV = pd->localV();
      double localExitU = pd->localU();
      double localExitV = pd->localV();

      if (sector == 0) {  // East
        localJogU = pd->localU() + p / 2.0;
        localJogV = pd->localV() + sgnV * (p / 2.0);
        localExitU = bga->maxX() + exitMargin;
        localExitV = localJogV;
      } else if (sector == 1) {  // North
        localJogU = pd->localU() + sgnU * (p / 2.0);
        localJogV = pd->localV() + p / 2.0;
        localExitU = localJogU;
        localExitV = bga->maxY() + exitMargin;
      } else if (sector == 2) {  // West
        localJogU = pd->localU() - p / 2.0;
        localJogV = pd->localV() + sgnV * (p / 2.0);
        localExitU = bga->minX() - exitMargin;
        localExitV = localJogV;
      } else {  // South (sector 3)
        localJogU = pd->localU() + sgnU * (p / 2.0);
        localJogV = pd->localV() - p / 2.0;
        localExitU = localJogU;
        localExitV = bga->minY() - exitMargin;
      }

      IntPoint* globalJog =
          toGlobalPoint(pinCenter, pd->localU(), pd->localV(), localJogU, localJogV, cos, sin);
      IntPoint* globalExit =
          toGlobalPoint(pinCenter, pd->localU(), pd->localV(), localExitU, localExitV, cos, sin);

      double maxAllowed1 = board->checkTraceSegment(pinCenter, globalJog, pinLayer, netNumbers,
                                                    traceHalfWidthPin, traceClClass, false);
      if (maxAllowed1 < pinCenter->toFloat()->distance(globalJog->toFloat())) {
        drcRejections++;
        continue;
      }

      double maxAllowed2 = board->checkTraceSegment(globalJog, globalExit, pinLayer, netNumbers,
                                                    traceHalfWidthPin, traceClClass, false);
      double dist2 = globalJog->toFloat()->distance(globalExit->toFloat());

      if (maxAllowed2 < dist2) {
        double distToPerimeter =
            jrt::Math::max(0.0, getDistToPerimeter(pd, sector, bga) - p / 2.0);
        if (maxAllowed2 >= distToPerimeter + minPerimeterClearance) {
          double safeLen = jrt::Math::max(distToPerimeter + minPerimeterClearance,
                                          maxAllowed2 - safetyMargin);
          globalExit = interpolatePoint(globalJog, globalExit, safeLen / dist2);
        } else {
          drcRejections++;
          continue;
        }
      }

      Polyline* polyline =
          new Polyline(jrt::Array<Point*>::of({pinCenter, globalJog, globalExit}));
      board->insertTrace(polyline, pinLayer, traceHalfWidthPin, netNumbers, traceClClass,
                         FixedState::USER_FIXED);
      if (pin->getNormalContacts()->isEmpty()) {
        drcRejections++;
        continue;
      }
      tracesInserted++;
      committed++;
      committedPins->add(pin);
      continue;
    }

    // Case C: Ring 2+ or via-escaped pins (4-candidate interstitial exploration)
    BgaEscapeCommitter_ResolvedVia* resolvedVia =
        resolveVia(board, pinLayer, targetLayer, nc, viaClClass);
    if (resolvedVia == nullptr) {
      FRLogger::warn(
          "BGA escape committer: no suitable via padstack configured on board for net " +
          jrt::str(netNo) + " (pinLayer " + jrt::str(pinLayer) + " to targetLayer " +
          jrt::str(targetLayer) + ")");
      drcRejections++;
      continue;
    }
    Padstack* viaPadstack = resolvedVia->padstack();
    int32_t effectiveViaClClass = resolvedVia->clearanceClassIdx();

    int32_t pinU = jrt::l2i(jrt::Math::round(pd->localU()));
    int32_t pinV = jrt::l2i(jrt::Math::round(pd->localV()));
    int32_t halfPitch = jrt::l2i(jrt::Math::round(p / 2.0));
    double centerU = (bga->minX() + bga->maxX()) / 2.0;
    double centerV = (bga->minY() + bga->maxY()) / 2.0;
    int32_t qU = (pd->localU() >= centerU) ? 1 : -1;
    int32_t qV = (pd->localV() >= centerV) ? 1 : -1;

    jrt::Array<int32_t>* dX;
    jrt::Array<int32_t>* dY;
    if (sector == 0 || sector == 2) {  // East/West: outward axis is U
      dX = jrt::Array<int32_t>::of(
          {qU * halfPitch, qU * halfPitch, -qU * halfPitch, -qU * halfPitch});
      dY = jrt::Array<int32_t>::of(
          {qV * halfPitch, -qV * halfPitch, qV * halfPitch, -qV * halfPitch});
    } else {  // North/South: outward axis is V
      dX = jrt::Array<int32_t>::of(
          {qU * halfPitch, -qU * halfPitch, qU * halfPitch, -qU * halfPitch});
      dY = jrt::Array<int32_t>::of(
          {qV * halfPitch, qV * halfPitch, -qV * halfPitch, -qV * halfPitch});
    }

    bool candidateCommitted = false;

    for (int32_t s = 0; s < 4; s++) {
      int32_t viaU = pinU + (*dX)[s];
      int32_t viaV = pinV + (*dY)[s];

      IntPoint* globalViaCenter =
          toGlobalPoint(pinCenter, pd->localU(), pd->localV(), viaU, viaV, cos, sin);

      BgaEscapeCommitter_PlaneViaKey* viaKey =
          new BgaEscapeCommitter_PlaneViaKey(globalViaCenter, netNo);
      bool isPlaneDrop = (pd->disposition() == BgaEscapeDescriptor_PinDisposition::PLANE_DROP);
      bool viaAlreadyPlaced = isPlaneDrop && placedPlaneVias->containsKey(viaKey);

      if (!viaAlreadyPlaced) {
        if (committedViaLocations->contains(globalViaCenter)) {
          continue;  // Collides with already committed via site
        }
        bool viaClear = checkViaClearance(board, viaPadstack, globalViaCenter, netNo, netNumbers,
                                          effectiveViaClClass);
        if (!viaClear) {
          continue;  // Clearance violation or hole spacing violation
        }
      }

      // Validate dogbone trace on pinLayer
      double maxAllowedDogbone = board->checkTraceSegment(
          pinCenter, globalViaCenter, pinLayer, netNumbers, traceHalfWidthPin, traceClClass, false);
      if (maxAllowedDogbone < pinCenter->toFloat()->distance(globalViaCenter->toFloat())) {
        continue;  // Dogbone obstructed on pinLayer
      }

      IntPoint* globalCorridor = nullptr;
      IntPoint* globalBreakoutExit = nullptr;

      if (!isPlaneDrop) {
        // Recompute exact 45 degree breakout jog P1 -> P2 onto sector centerline
        int32_t corridorU;
        int32_t corridorV;
        int32_t exitU;
        int32_t exitV;

        if (sector == 0) {  // East: centerline is V = pinV
          int32_t step = jrt::Math::abs(pinV - viaV);
          corridorU = viaU + step;
          corridorV = pinV;
          exitU = jrt::l2i(jrt::Math::round(bga->maxX() + exitMargin));
          exitV = pinV;
        } else if (sector == 2) {  // West: centerline is V = pinV
          int32_t step = jrt::Math::abs(pinV - viaV);
          corridorU = viaU - step;
          corridorV = pinV;
          exitU = jrt::l2i(jrt::Math::round(bga->minX() - exitMargin));
          exitV = pinV;
        } else if (sector == 1) {  // North: centerline is U = pinU
          int32_t step = jrt::Math::abs(pinU - viaU);
          corridorU = pinU;
          corridorV = viaV + step;
          exitU = pinU;
          exitV = jrt::l2i(jrt::Math::round(bga->maxY() + exitMargin));
        } else {  // South: centerline is U = pinU
          int32_t step = jrt::Math::abs(pinU - viaU);
          corridorU = pinU;
          corridorV = viaV - step;
          exitU = pinU;
          exitV = jrt::l2i(jrt::Math::round(bga->minY() - exitMargin));
        }

        globalCorridor =
            toGlobalPoint(pinCenter, pd->localU(), pd->localV(), corridorU, corridorV, cos, sin);
        globalBreakoutExit =
            toGlobalPoint(pinCenter, pd->localU(), pd->localV(), exitU, exitV, cos, sin);

        // Validate 45 degree jog on targetLayer
        double maxAllowedJog =
            board->checkTraceSegment(globalViaCenter, globalCorridor, targetLayer, netNumbers,
                                     traceHalfWidthTarget, traceClClass, false);
        if (maxAllowedJog < globalViaCenter->toFloat()->distance(globalCorridor->toFloat())) {
          continue;  // Jog obstructed on targetLayer
        }

        // Validate corridor on targetLayer with continuous okLength truncation
        double maxAllowedCorridor =
            board->checkTraceSegment(globalCorridor, globalBreakoutExit, targetLayer, netNumbers,
                                     traceHalfWidthTarget, traceClClass, false);
        double corridorDist = globalCorridor->toFloat()->distance(globalBreakoutExit->toFloat());

        if (maxAllowedCorridor < corridorDist) {
          double distToPerimeter = getDistToPerimeter(pd, sector, bga);
          if (maxAllowedCorridor >= distToPerimeter + minPerimeterClearance) {
            double safeLen = jrt::Math::max(distToPerimeter + minPerimeterClearance,
                                            maxAllowedCorridor - safetyMargin);
            globalBreakoutExit =
                interpolatePoint(globalCorridor, globalBreakoutExit, safeLen / corridorDist);
          } else {
            continue;  // Corridor blocked before exiting perimeter
          }
        }
      }

      // Commit via first so no orphaned traces exist if via placement fails
      Via* insertedVia = nullptr;
      if (!viaAlreadyPlaced) {
        insertedVia = board->insertVia(viaPadstack, globalViaCenter, netNumbers,
                                       effectiveViaClClass, FixedState::USER_FIXED, true);
        if (insertedVia == nullptr) {
          continue;
        }
      }

      // Commit dogbone trace and verify insertion before proceeding
      Polyline* dogbone = new Polyline(jrt::Array<Point*>::of({pinCenter, globalViaCenter}));
      board->insertTrace(dogbone, pinLayer, traceHalfWidthPin, netNumbers, traceClClass,
                         FixedState::USER_FIXED);
      if (pin->getNormalContacts()->isEmpty()) {
        if (insertedVia != nullptr) {
          board->removeItem(insertedVia);
        }
        continue;
      }

      // Commit breakout trace on targetLayer and verify insertion
      bool breakoutInserted = false;
      if (!isPlaneDrop && globalCorridor != nullptr && globalBreakoutExit != nullptr) {
        int32_t tracesBeforeBreakout = board->getTraces()->size();
        Polyline* breakout = new Polyline(
            jrt::Array<Point*>::of({globalViaCenter, globalCorridor, globalBreakoutExit}));
        board->insertTrace(breakout, targetLayer, traceHalfWidthTarget, netNumbers, traceClClass,
                           FixedState::USER_FIXED);
        if (board->getTraces()->size() <= tracesBeforeBreakout) {
          // Roll back dogbone traces contacting this pin and the newly inserted via
          // Java: List.copyOf(pin.getNormalContacts())
          jrt::List<Item*>* contacts = new jrt::ArrayList<Item*>(pin->getNormalContacts());
          for (Item* contact : *contacts) {
            board->removeItem(contact);
          }
          if (insertedVia != nullptr) {
            board->removeItem(insertedVia);
          }
          continue;
        }
        breakoutInserted = true;
      }

      tracesInserted++;
      if (breakoutInserted) {
        tracesInserted++;
      }

      if (insertedVia != nullptr) {
        viasInserted++;
        committedViaLocations->add(globalViaCenter);
        if (isPlaneDrop) {
          placedPlaneVias->put(viaKey, insertedVia);
        }
      }

      committed++;
      committedPins->add(pin);
      candidateCommitted = true;
      break;  // Successfully committed this pin!
    }

    if (!candidateCommitted) {
      drcRejections++;
    }
  }

  FRLogger::info("BGA escape committer: committed " + jrt::str(committed) + "/" +
                 jrt::str(attempted) + " escapes (" + jrt::str(drcRejections) +
                 " rejected by DRC)");

  return new BgaEscapeCommitter_CommitResult(attempted, committed, viasInserted, tracesInserted,
                                             drcRejections, committedPins);
}

IntPoint* BgaEscapeCommitter::toGlobalPoint(Point* pinCenter, double localPinU, double localPinV,
                                            double targetU, double targetV, double cos,
                                            double sin) {
  double dLocalU = targetU - localPinU;
  double dLocalV = targetV - localPinV;
  double dGlobalX = dLocalU * cos - dLocalV * sin;
  double dGlobalY = dLocalU * sin + dLocalV * cos;
  return new IntPoint(jrt::l2i(jrt::Math::round(pinCenter->toFloat()->x + dGlobalX)),
                      jrt::l2i(jrt::Math::round(pinCenter->toFloat()->y + dGlobalY)));
}

IntPoint* BgaEscapeCommitter::interpolatePoint(Point* from, Point* to, double ratio) {
  FloatPoint* f1 = from->toFloat();
  FloatPoint* f2 = to->toFloat();
  double clampedRatio = jrt::Math::clamp(ratio, 0.0, 1.0);
  double rx = f1->x + (f2->x - f1->x) * clampedRatio;
  double ry = f1->y + (f2->y - f1->y) * clampedRatio;
  return new IntPoint(jrt::l2i(jrt::Math::round(rx)), jrt::l2i(jrt::Math::round(ry)));
}

double BgaEscapeCommitter::getDistToPerimeter(BgaEscapeDescriptor_BgaPinDescriptor* pd,
                                              int32_t sector, BgaEscapeDescriptor* bga) {
  if (sector == 0) {  // East
    return jrt::Math::max(0.0, bga->maxX() - pd->localU());
  } else if (sector == 1) {  // North
    return jrt::Math::max(0.0, bga->maxY() - pd->localV());
  } else if (sector == 2) {  // West
    return jrt::Math::max(0.0, pd->localU() - bga->minX());
  } else {  // South
    return jrt::Math::max(0.0, pd->localV() - bga->minY());
  }
}

bool BgaEscapeCommitter::checkViaClearance(RoutingBoard* board, Padstack* viaPadstack,
                                           IntPoint* globalViaCenter, int32_t netNo,
                                           jrt::Array<int32_t>* netNumbers, int32_t viaClClass) {
  if (!HoleSpacing::clear(board, viaPadstack, globalViaCenter)) {
    return false;
  }
  for (int32_t l = viaPadstack->fromLayer(); l <= viaPadstack->toLayer(); l++) {
    ConvexShape* shape = viaPadstack->getShape(l);
    if (shape != nullptr) {
      ConvexShape* translatedShape =
          jrt::fast_cast<ConvexShape>(shape->translateBy(globalViaCenter->differenceBy(Point::ZERO())));
      TileShape* viaTile;
      if (auto* ts = jrt::fast_cast<TileShape>(translatedShape)) {
        viaTile = ts;
      } else {
        viaTile = translatedShape->boundingOctagon();
      }
      // Strict no via-in-pad: physical overlap with any Pin (starting ball pad,
      // same-net decoupling cap, or foreign pin) on layer l is strictly rejected
      for (Item* it : *board->overlappingItems(viaTile, l)) {
        if (jrt::fast_cast<Pin>(it) != nullptr) {
          return false;
        }
      }

      jrt::Collection<Item*>* obstacles =
          board->overlappingItemsWithClearance(viaTile, l, netNumbers, viaClClass);
      for (Item* obs : *obstacles) {
        if (jrt::fast_cast<Pin>(obs) != nullptr) {
          return false;
        } else if (obs != nullptr && !obs->isDrillable(netNo)) {
          return false;
        }
      }
    }
  }
  return true;
}

bool BgaEscapeCommitter::coversSpan(Padstack* ps, int32_t lowL, int32_t highL) {
  return ps != nullptr && ps->fromLayer() <= lowL && ps->toLayer() >= highL;
}

BgaEscapeCommitter_ResolvedVia* BgaEscapeCommitter::resolveVia(RoutingBoard* board,
                                                               int32_t fromLayer, int32_t toLayer,
                                                               NetClass* nc,
                                                               int32_t defaultViaClClass) {
  int32_t lowL = jrt::Math::min(fromLayer, toLayer);
  int32_t highL = jrt::Math::max(fromLayer, toLayer);

  // 1. Check NetClass via rule for an exact layer-span match (e.g. blind/microvia)
  if (nc != nullptr && nc->getViaRule() != nullptr) {
    ViaRule* vr = nc->getViaRule();
    for (int32_t i = 0; i < vr->viaCount(); i++) {
      ViaInfo* vi = vr->getVia(i);
      if (vi != nullptr && vi->getPadstack() != nullptr) {
        Padstack* ps = vi->getPadstack();
        if (ps->fromLayer() == lowL && ps->toLayer() == highL) {
          return new BgaEscapeCommitter_ResolvedVia(ps, vi->getClearanceClassIndex());
        }
      }
    }
  }

  // 2. Check board.rules.viaInfos for an exact layer-span match
  for (int32_t i = 0; i < board->rules->viaInfos->count(); i++) {
    ViaInfo* vi = board->rules->viaInfos->get(i);
    if (vi != nullptr && vi->getPadstack() != nullptr) {
      Padstack* ps = vi->getPadstack();
      if (ps->fromLayer() == lowL && ps->toLayer() == highL) {
        return new BgaEscapeCommitter_ResolvedVia(ps, vi->getClearanceClassIndex());
      }
    }
  }

  // 3. Fallback to any via whose layer span covers [lowL, highL]
  NetClass* defaultNc = board->rules->getDefaultNetClass();
  if (defaultNc != nullptr) {
    ViaRule* vr = defaultNc->getViaRule();
    if (vr != nullptr) {
      for (int32_t i = 0; i < vr->viaCount(); i++) {
        ViaInfo* vi = vr->getVia(i);
        if (vi != nullptr && coversSpan(vi->getPadstack(), lowL, highL)) {
          return new BgaEscapeCommitter_ResolvedVia(vi->getPadstack(),
                                                    vi->getClearanceClassIndex());
        }
      }
    }
  }
  for (int32_t i = 0; i < board->rules->viaInfos->count(); i++) {
    ViaInfo* vi = board->rules->viaInfos->get(i);
    if (vi != nullptr && coversSpan(vi->getPadstack(), lowL, highL)) {
      return new BgaEscapeCommitter_ResolvedVia(vi->getPadstack(), vi->getClearanceClassIndex());
    }
  }
  if (board->library != nullptr && board->library->padstacks != nullptr &&
      board->library->padstacks->count() > 0) {
    Padstack* ps = board->library->padstacks->get(1);
    if (coversSpan(ps, lowL, highL)) {
      return new BgaEscapeCommitter_ResolvedVia(ps, defaultViaClClass);
    }
  }
  return nullptr;
}

}  // namespace fr
