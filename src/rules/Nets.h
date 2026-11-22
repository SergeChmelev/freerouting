// Port of app.freerouting.rules.Nets
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes the electrical nets on a board. */
class Nets : public virtual jrt::Object {
 public:
  /** The maximum legal net number for nets. */
  static constexpr int32_t max_legal_net_number = 9999999;

  /** Auxiliary net number for internal use. */
  static constexpr int32_t hidden_net_number = 10000001;

  /** Creates a new empty net list. */
  Nets();

  /** Returns true, if netNumber is not the hidden net number. */
  static bool isNormalNetNumber(int32_t netNumber) {
    return netNumber > 0 && netNumber <= max_legal_net_number;
  }

  /** Returns the biggest net number used on the board. */
  int32_t maxNetNumber();

  /** Returns the net with the input name and subnetNumber, or null, if no such net exists. */
  Net* get(const std::string& name, int32_t subnetNumber);

  /** Returns all subnets with the input name (empty if none). */
  jrt::Collection<Net*>* get(const std::string& name);

  /** Returns the net with the input net number or null, if no such net exists. */
  Net* get(int32_t netNumber);

  /**
   * Generates a new net number. PORT NOTE: java.util.Locale -> std::string; the localized "net#"
   * prefix comes from TextManager.
   */
  Net* newNet(const std::string& locale);

  /**
   * Adds a new net with default properties with the input name. subnetNumber is used only if a net
   * is divided internally because of fromto rules for example. For normal nets it is always 1.
   */
  Net* add(const std::string& name, int32_t subnetNumber, bool containsPlane);

  /** Returns the Board of this net list. */
  BasicBoard* getBoard() { return this->board; }

  /** Sets the Board of this net list. Used for example to get access to the Items. */
  void setBoard(BasicBoard* board) { this->board = board; }

 private:
  /** The list of electrical nets on the board (Java: Vector). */
  jrt::ArrayList<Net*>* nets;

  BasicBoard* board = nullptr;
};

}  // namespace fr
