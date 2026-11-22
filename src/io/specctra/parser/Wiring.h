#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Class for reading and writing wiring scopes from dsn-files. */
class Wiring : public ScopeKeyword {
 public:
  /** Creates a new instance of Wiring. */
  Wiring() : ScopeKeyword("wiring") {}

  /** Throws jrt::IOException. */
  static void writeScope(WriteScopeParameter* scopeParameter);

  static fr::FixedState calcFixed(IJFlexScanner* scanner);

  bool readScope(ReadScopeParameter* scopeParameter) override;

 private:
  static void writeViaScope(WriteScopeParameter* scopeParameter, fr::Via* via);

  static void writeWireScope(WriteScopeParameter* scopeParameter, fr::Trace* wire);

  static void writeConductionAreaScope(WriteScopeParameter* scopeParameter,
                                       fr::ConductionArea* conductionArea);

  static void writeNet(fr::Net* net, fr::IndentFileWriter* file,
                       fr::IdentifierType* identifierType);

  static void writeFixedState(fr::IndentFileWriter* file, fr::FixedState fixedState);

  static jrt::Collection<fr::Net*>* getSubnets(Net_Id* netId, fr::BoardRules* rules);

  static bool viaExists(fr::IntPoint* location, fr::Padstack* padstack,
                        jrt::Array<int32_t>* netNumbers, fr::BasicBoard* board);

  /** Reads a netId. The subnetNumber of the netId will be 0, if no subnetNumber was found. */
  static Net_Id* readNetId(IJFlexScanner* scanner);

  /**
   * Extends every wire end that lies off-centre inside a pad of its net (and is not yet connected
   * to it) by a stub to the pad centre; stubs that would violate a clearance are left out.
   */
  static void connectWireEndsInsidePads(fr::BasicBoard* board);

  static void connectEndToEnclosingPad(fr::BasicBoard* board, fr::Trace* wire, fr::Point* end);

  static bool isConnected(fr::Trace* wire, fr::DrillItem* pad);

  /** Returns a pin or via sharing a net with the wire whose copper contains the wire end. */
  static fr::DrillItem* enclosingPad(fr::BasicBoard* board, fr::Trace* wire, fr::Point* end);

  fr::Item* readWireScope(ReadScopeParameter* scopeParameter);

  /** Maybe trace of type turret without net in Mentor design: assign the net from overlaps. */
  void tryCorrectNet(fr::Item* item);

  bool readViaScope(ReadScopeParameter* scopeParameter);
};

}  // namespace fr::dsn
