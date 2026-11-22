#include "autoroute/fanout/BgaEscapeSolver.h"

#include "autoroute/fanout/BgaEscapeDescriptor.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "geometry/planar/IntPoint.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// BgaEscapeSolver_PinEscapeProposal (Java record)
// ---------------------------------------------------------------------------------------------

bool BgaEscapeSolver_PinEscapeProposal::equals(jrt::Object* o) {
  if (this == o) return true;
  auto* other = jrt::fast_cast<BgaEscapeSolver_PinEscapeProposal>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf<BgaEscapeDescriptor_BgaPinDescriptor*>(pinDescriptor_,
                                                              other->pinDescriptor_) &&
         jrt::equalsOf<IntPoint*>(viaCenter_, other->viaCenter_) &&
         targetLayer_ == other->targetLayer_ && cost_ == other->cost_ &&
         escaped_ == other->escaped_;
}

int32_t BgaEscapeSolver_PinEscapeProposal::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf<BgaEscapeDescriptor_BgaPinDescriptor*>(pinDescriptor_));
  h = jrt::hashCombine(h, jrt::hashOf<IntPoint*>(viaCenter_));
  h = jrt::hashCombine(h, targetLayer_);
  h = jrt::hashCombine(h, cost_);
  h = jrt::hashCombine(h, jrt::Boolean::hashCode(escaped_));
  return h;
}

std::string BgaEscapeSolver_PinEscapeProposal::toString() {
  return "PinEscapeProposal[pinDescriptor=" + jrt::str(pinDescriptor_) +
         ", viaCenter=" + jrt::str(viaCenter_) + ", targetLayer=" + jrt::str(targetLayer_) +
         ", cost=" + jrt::str(cost_) + ", escaped=" + jrt::str(escaped_) + "]";
}

// ---------------------------------------------------------------------------------------------
// BgaEscapeSolver
// ---------------------------------------------------------------------------------------------

int32_t BgaEscapeSolver::computeGridStep(RoutingBoard* board, double pitchUm,
                                         double pitchBoardUnits) {
  double res = board->communication->getResolution(Unit::UM);
  int32_t p = jrt::l2i(jrt::Math::round(pitchBoardUnits));

  int32_t minWidth = board->rules->getMinAllowedTraceHalfWidth() * 2;
  int32_t minClearance = board->rules->clearanceMatrix->getValue(0, 0, 0, false);
  if (minClearance <= 0) {
    minClearance = jrt::l2i(jrt::Math::round(100.0 * res));
  }

  int32_t candidate = jrt::Math::min(p / 8, (minWidth + minClearance) / 2);

  int32_t deltaMin;
  if (pitchUm <= 500.0) {
    int32_t fiveUm = jrt::l2i(jrt::Math::max(static_cast<int64_t>(1), jrt::Math::round(5.0 * res)));
    deltaMin = jrt::Math::max(fiveUm, p / 32);
  } else {
    int32_t tenUm = jrt::l2i(jrt::Math::max(static_cast<int64_t>(1), jrt::Math::round(10.0 * res)));
    deltaMin = jrt::Math::max(tenUm, p / 16);
  }
  deltaMin = jrt::Math::max(1, deltaMin);

  int32_t deltaMax = jrt::Math::max(deltaMin, p / 4);

  // Java: Math.clamp(long value, int min, int max)
  return jrt::Math::clamp(static_cast<int64_t>(candidate), deltaMin, deltaMax);
}

jrt::List<BgaEscapeSolver_PinEscapeProposal*>* BgaEscapeSolver::solve(RoutingBoard* board,
                                                                     BgaEscapeDescriptor* bga) {
  return solve(board, bga, nullptr);
}

jrt::List<BgaEscapeSolver_PinEscapeProposal*>* BgaEscapeSolver::solve(
    RoutingBoard* board, BgaEscapeDescriptor* bga,
    std::function<void(const std::string&)> infoLogger) {
  if (board == nullptr || bga == nullptr || bga->pins() == nullptr || bga->pins()->isEmpty()) {
    return jrt::Collections::emptyList<BgaEscapeSolver_PinEscapeProposal*>();
  }

  int32_t gridStep = computeGridStep(board, bga->pitchUm(), bga->pitchBoardUnits());
  jrt::List<BgaEscapeSolver_PinEscapeProposal*>* proposals =
      executeDeterministicWavefront(bga, gridStep);

  int32_t escapedCount = 0;
  for (BgaEscapeSolver_PinEscapeProposal* proposal : *proposals) {
    if (proposal->escaped()) {
      escapedCount++;
    }
  }

  std::string msg = "BGA escape solver: generated " + jrt::str(escapedCount) + "/" +
                    jrt::str(proposals->size()) + " CPU escape proposal(s) for " +
                    bga->component()->name + ".";
  FRLogger::info(msg);
  if (infoLogger) {
    infoLogger(msg);
  }

  return proposals;
}

jrt::List<BgaEscapeSolver_PinEscapeProposal*>* BgaEscapeSolver::executeDeterministicWavefront(
    BgaEscapeDescriptor* bga, int32_t gridStepBoardUnits) {
  if (bga == nullptr || bga->pins() == nullptr || bga->pins()->isEmpty()) {
    return jrt::Collections::emptyList<BgaEscapeSolver_PinEscapeProposal*>();
  }

  jrt::List<BgaEscapeDescriptor_BgaPinDescriptor*>* pins = bga->pins();
  int32_t pinCount = pins->size();
  int32_t maxRing = bga->maxRing();

  int32_t pitch = jrt::l2i(jrt::Math::round(bga->pitchBoardUnits()));
  int32_t pinLayer = pins->get(0)->pin()->firstLayer();
  int32_t halfPitch = pitch / 2;
  double centerU = (bga->minX() + bga->maxX()) / 2.0;
  double centerV = (bga->minY() + bga->maxY()) / 2.0;

  jrt::List<BgaEscapeSolver_PinEscapeProposal*>* proposals =
      new (jrt::kOp) jrt::ArrayList<BgaEscapeSolver_PinEscapeProposal*>();

  // Track occupied dogbone target sites (quantized cell key -> winning pin index)
  jrt::Map<int64_t, int32_t>* cellWinners = new (jrt::kOp) jrt::HashMap<int64_t, int32_t>();

  // Process concentric rings from outer perimeter (ring 0) inward (ring maxRing)
  for (int32_t r = 0; r <= maxRing; r++) {
    for (int32_t i = 0; i < pinCount; i++) {
      BgaEscapeDescriptor_BgaPinDescriptor* pd = pins->get(i);
      if (pd->ring() != r ||
          pd->disposition() == BgaEscapeDescriptor_PinDisposition::PASSIVE_OBSTACLE) {
        continue;
      }

      // If targetLayer is pinLayer, this pin escapes directly on surface with NO via
      if (pd->targetLayer() == pinLayer &&
          pd->disposition() == BgaEscapeDescriptor_PinDisposition::SIGNAL_ESCAPE) {
        proposals->add(
            new (jrt::kOp) BgaEscapeSolver_PinEscapeProposal(pd, nullptr, pd->targetLayer(), 0, true));
        continue;
      }

      int32_t pinU = jrt::l2i(jrt::Math::round(pd->localU()));
      int32_t pinV = jrt::l2i(jrt::Math::round(pd->localV()));

      int32_t bestCost = 999999;
      IntPoint* bestVia = nullptr;
      int64_t bestCell = -1;

      int32_t qU = (pd->localU() >= centerU) ? 1 : -1;
      int32_t qV = (pd->localV() >= centerV) ? 1 : -1;

      // 4-Quadrant 45-degree interstitial dogbone sites: (+-p/2, +-p/2)
      jrt::Array<int32_t>* dX;
      jrt::Array<int32_t>* dY;
      if (pd->breakoutSector() == 0 ||
          pd->breakoutSector() == 2) {  // East/West: outward axis is U
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
      jrt::Array<int32_t>* siteCosts = jrt::Array<int32_t>::of({1000, 1500, 1500, 2500});

      int32_t cellStep = jrt::Math::max(1, gridStepBoardUnits * 2);
      for (int32_t s = 0; s < 4; s++) {
        int32_t vx = pinU + (*dX)[s];
        int32_t vy = pinV + (*dY)[s];
        int32_t scost = (*siteCosts)[s];

        // PLANE_DROP sharing incentive
        if (pd->disposition() == BgaEscapeDescriptor_PinDisposition::PLANE_DROP) {
          scost /= 2;
        }

        // Quantized cell hash in local coordinate frame (floor division for negative coords)
        int64_t cellKey =
            jrt::shl(static_cast<int64_t>(jrt::Math::floorDiv(vx, cellStep)), 32) |
            (static_cast<int64_t>(jrt::Math::floorDiv(vy, cellStep)) & 0xFFFFFFFFLL);

        if (!cellWinners->containsKey(cellKey)) {
          if (scost < bestCost) {
            bestCost = scost;
            bestVia = new (jrt::kGeom) IntPoint(vx, vy);
            bestCell = cellKey;
          }
        }
      }

      if (bestVia != nullptr && bestCell != -1) {
        cellWinners->put(bestCell, i);
        proposals->add(
            new (jrt::kOp) BgaEscapeSolver_PinEscapeProposal(pd, bestVia, pd->targetLayer(), bestCost, true));
      } else {
        proposals->add(
            new (jrt::kOp) BgaEscapeSolver_PinEscapeProposal(pd, nullptr, pd->targetLayer(), 999999, false));
      }
    }
  }

  return proposals;
}

}  // namespace fr
