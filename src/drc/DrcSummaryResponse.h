#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

// PORT NOTE: gson @SerializedName and swagger @Schema annotations dropped. All three types are
// pure data holders, fully implemented in this header (no .cpp).

/** Represents an individual violation with human/LLM readable context. */
class DrcSummaryResponse_DiagnosticViolation : public virtual jrt::Object {
 public:
  /** Violation type (e.g. clearance, unconnected_net, dangling_trace). */
  std::string type;
  /** Severity level (e.g. error, warning). */
  std::string severity;
  /** Layer name where violation occurred. */
  std::string layer;
  /** Natural-language explanation with component/pin and net names. */
  std::string explanation;
  /** Expected clearance in mm, if applicable. */
  std::optional<double> expectedClearanceMm;
  /** Actual measured clearance in mm, if applicable. */
  std::optional<double> actualClearanceMm;
  /** Clearance shortfall in mm, if applicable. */
  std::optional<double> shortfallMm;
  /** Descriptions of the involved items. */
  jrt::List<std::string>* items = new jrt::ArrayList<std::string>();

  DrcSummaryResponse_DiagnosticViolation() {}

  DrcSummaryResponse_DiagnosticViolation(const std::string& type, const std::string& severity,
                                         const std::string& layer,
                                         const std::string& explanation,
                                         std::optional<double> expectedClearanceMm,
                                         std::optional<double> actualClearanceMm,
                                         std::optional<double> shortfallMm,
                                         jrt::List<std::string>* items)
      : type(type),
        severity(severity),
        layer(layer),
        explanation(explanation),
        expectedClearanceMm(expectedClearanceMm),
        actualClearanceMm(actualClearanceMm),
        shortfallMm(shortfallMm) {
    if (items != nullptr) {
      this->items->addAll(items);
    }
  }
};

/** Represents a spatial congestion cluster where multiple violations occur near each other. */
class DrcSummaryResponse_CongestionZone : public virtual jrt::Object {
 public:
  /** Center X coordinate in mm. */
  double centerXmm = 0;
  /** Center Y coordinate in mm. */
  double centerYmm = 0;
  /** Radius of the congestion zone in mm. */
  double radiusMm = 0;
  /** Number of violations within this cluster. */
  int32_t violationCount = 0;
  /** Description of the congestion hotspot. */
  std::string description;

  DrcSummaryResponse_CongestionZone() {}

  DrcSummaryResponse_CongestionZone(double centerXmm, double centerYmm, double radiusMm,
                                    int32_t violationCount, const std::string& description)
      : centerXmm(centerXmm),
        centerYmm(centerYmm),
        radiusMm(radiusMm),
        violationCount(violationCount),
        description(description) {}
};

/**
 * High-level diagnostic DRC summary designed for fast LLM/agent comprehension and concise API
 * reporting.
 */
class DrcSummaryResponse : public virtual jrt::Object {
 public:
  /** Total number of clearance violations. */
  int32_t clearanceViolationsCount = 0;
  /** Total number of unconnected nets or incomplete connections. */
  int32_t unconnectedNetsCount = 0;
  /** Detailed list of diagnostic violation summaries. */
  jrt::List<DrcSummaryResponse_DiagnosticViolation*>* violations =
      new jrt::ArrayList<DrcSummaryResponse_DiagnosticViolation*>();
  /** Clustered areas with high concentration of violations or routing bottlenecks. */
  jrt::List<DrcSummaryResponse_CongestionZone*>* congestionZones =
      new jrt::ArrayList<DrcSummaryResponse_CongestionZone*>();
  /** Actionable suggestions for router settings or layout auto-corrections. */
  jrt::List<std::string>* hints = new jrt::ArrayList<std::string>();
};

}  // namespace fr
