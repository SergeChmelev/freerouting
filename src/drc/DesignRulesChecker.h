#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Design Rules Checker that centralizes DRC functionality. This class is responsible for
 * detecting clearance violations and other design rule issues.
 *
 * PORT NOTE: the KiCad DRC report (generateReport / generateReportJson / the three
 * convertToDrcViolation overloads, all producing io.kicad.KiCadDrc* types + gson JSON) is out of
 * scope and dropped. The private description helpers they used are kept for diffing.
 */
class DesignRulesChecker : public virtual jrt::Object {
 private:
  BasicBoard* board;
  DesignRulesCheckerSettings* drcSettings;

 public:
  int32_t maxConnections = 0;

 private:
  // State for incomplete connections (ratsnest)
  jrt::Array<NetIncompletes*>* netIncompletes = nullptr;

 public:
  /** Creates a design rules checker for the given board and settings (settings may be null). */
  DesignRulesChecker(BasicBoard* board, DesignRulesCheckerSettings* drcSettings)
      : board(board), drcSettings(drcSettings) {}

  /** Collects all clearance violations on the board. */
  virtual jrt::Collection<ClearanceViolation*>* getAllClearanceViolations();

  /** Collects all unconnected nets (one entry per net with several disconnected groups). */
  virtual jrt::Collection<UnconnectedItems*>* getAllUnconnectedItems();

  /** Collects copper pour islands (isolated / dead copper). */
  virtual jrt::Collection<ZoneIslandViolation*>* getZoneIslandViolations();

 private:
  /** Finds a representative item from a connected set, preferring Pins, then Traces. */
  Item* findRepresentativeItem(jrt::Set<Item*>* connectedSet);

  // --- helpers of the dropped KiCad report ---
  bool isHole(Item* item);

  /** Gets a human-readable description of an item. */
  std::string getItemDescription(Item* item);

  /** Gets a detailed description of a trace including net, layer, and length. */
  std::string getDetailedTraceDescription(Item* item, const std::string& coordinateUnit);

  /** Converts a board coordinate to the specified unit ("mm", "mil", "inch", "um"). */
  double convertCoordinate(double boardCoordinate, const std::string& coordinateUnit);

 public:
  /**
   * Initializes the incomplete connection calculations for all nets on the board.
   */
  virtual void calculateAllIncompletes();

  /** Recalculates the incomplete connections (airlines) for the specified net. */
  virtual void recalculateNetIncompletes(int32_t netNumber);

  /** Recalculates the incomplete connections for the specified net using the given items. */
  virtual void recalculateNetIncompletes(int32_t netNumber, jrt::Collection<Item*>* itemList);

  /** Returns the total number of incomplete connections (airlines) across all nets. */
  virtual int32_t getIncompleteCount();

  /** Returns the net numbers that still have at least one incomplete connection. */
  virtual jrt::Set<int32_t>* incompleteNetNumbers();

  /** Returns the number of incomplete connections for a specific net. */
  virtual int32_t getIncompleteCount(int32_t netNumber);

  /** Returns the total number of nets that violate length restrictions. */
  virtual int32_t getLengthViolationCount();

  /** Returns the magnitude of the length violation for the specified net. */
  virtual double getLengthViolation(int32_t netNumber);

  /** Recalculates length matching violations for all nets; true if any status changed. */
  virtual bool recalculateLengthViolations();

  /** Retrieves all airlines (incomplete connections) for the entire board. */
  virtual jrt::Array<AirLine*>* getAllAirlines();

  /** Returns the NetIncompletes of the given net (or nullptr). */
  virtual NetIncompletes* getNetIncompletes(int32_t netNumber);

  /**
   * Generates a concise diagnostic summary of DRC violations with component context, spatial
   * congestion clustering, and layout auto-correction hints.
   */
  virtual DrcSummaryResponse* generateSummary();

 private:
  std::string formatItemWithContext(Item* item);

  jrt::List<DrcSummaryResponse_CongestionZone*>* detectCongestionZones(
      jrt::Collection<ClearanceViolation*>* violations);

  jrt::List<std::string>* generateActionableHints(
      jrt::Collection<ClearanceViolation*>* violations,
      jrt::Collection<UnconnectedItems*>* unconnecteds,
      jrt::List<DrcSummaryResponse_CongestionZone*>* zones);
};

}  // namespace fr
