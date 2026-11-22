// Port of app.freerouting.rules.DifferentialPairs
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Collection and bidirectional index for differential signal pairs on a board: lookups by base
 * name, by Net, or by net number. (synchronized dropped: single-threaded port.)
 */
class DifferentialPairs : public virtual jrt::Object {
 public:
  /** Creates an empty DifferentialPairs collection. */
  DifferentialPairs();

  /**
   * Adds a differential pair, or updates constraints if a pair with the same base name and nets is
   * already registered. Throws NullPointerException if pair is null and IllegalArgumentException
   * if either net or the base name is already registered with different nets.
   */
  void add(DifferentialPair* pair);

  /** Retrieves a differential pair by its base name (exact, then case-insensitive), or null. */
  DifferentialPair* get(const std::string& baseName);

  /** Finds the differential pair containing the given net, or null if single-ended. */
  DifferentialPair* findForNet(Net* net);

  /** Finds the differential pair containing the net with the given number, or null. */
  DifferentialPair* findForNet(int32_t netNumber);

  /** Removes the given pair. Returns true if it was present and removed. */
  bool remove(DifferentialPair* pair);

  /** Returns an unmodifiable snapshot collection of all registered differential pairs. */
  jrt::Collection<DifferentialPair*>* all();

  /** Returns the total count of registered differential pairs. */
  int32_t count();

  /** Returns true if no differential pairs are registered. */
  bool isEmpty();

  /** Clears all differential pairs from the registry. */
  void clear();

 private:
  friend class BoardRules;  // PORT NOTE: BoardRules::deepCopy fills the copied indices

  static bool hasSameIdentity(DifferentialPair* a, DifferentialPair* b);

  void checkNetAvailability(Net* net, DifferentialPair* pair);

  void indexSubnets(Net* net, DifferentialPair* pair);

  void removeSubnets(Net* net);

  /** Primary storage keyed by base name to preserve insertion order (LinkedHashMap). */
  jrt::Map<std::string, DifferentialPair*>* pairsByName;

  /** Fast index mapping net numbers to their differential pair (HashMap). */
  jrt::Map<int32_t, DifferentialPair*>* pairsByNetNumber;
};

}  // namespace fr
