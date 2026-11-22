#pragma once

#include <ostream>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * Writes a Specctra session (.ses) file from a BasicBoard. No dependency on BoardManager,
 * RoutingJob or any GUI class. Public write entry point for Specctra session files.
 */
class SesWriter final : public virtual jrt::Object {
 public:
  /**
   * Serialises the routing result from board to Specctra SES format on the given stream. The
   * stream is flushed but not closed. Throws jrt::IOException on I/O errors.
   * PORT NOTE: OutputStream -> std::ostream&; Java's null-stream check cannot occur and is dropped.
   */
  static void write(fr::BasicBoard* board, std::ostream& out, const std::string& designName);

  /**
   * Formats component rotation in the style KiCad exports: whole degrees as integers, fractional
   * degrees with minimal decimal precision, never trailing zeros.
   * PORT NOTE: Java BigDecimal.valueOf(degrees).stripTrailingZeros().toPlainString(); i.e. the
   * Double.toString digits of degrees, written without exponent and without trailing zeros
   * (jrt::BigDecimal has no stripTrailingZeros yet).
   */
  static std::string formatPlacementRotation(double degrees);

 private:
  SesWriter() = default;

  static void writeSessionScope(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                                fr::IndentFileWriter* file, const std::string& sessionName,
                                const std::string& designName);

  static void writePlacement(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                             fr::io::CoordinateTransform* coordinateTransform,
                             fr::IndentFileWriter* file);

  /** Writes all components with the given package to the session file. */
  static void writeComponents(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                              fr::io::CoordinateTransform* coordinateTransform,
                              fr::IndentFileWriter* file, fr::Package* pkg);

  static void writeComponent(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                             fr::io::CoordinateTransform* coordinateTransform,
                             fr::IndentFileWriter* file, fr::Component* component);

  static void writeWasIs(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                         fr::IndentFileWriter* file);

  static void writeRoutes(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                          fr::io::CoordinateTransform* coordinateTransform,
                          fr::IndentFileWriter* file);

  static void writeLibrary(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                           fr::io::CoordinateTransform* coordinateTransform,
                           fr::IndentFileWriter* file);

  static void writePadstack(fr::Padstack* padstack, fr::BasicBoard* board,
                            fr::IdentifierType* identifierType,
                            fr::io::CoordinateTransform* coordinateTransform,
                            fr::IndentFileWriter* file);

  static void writeNetwork(fr::BasicBoard* board, fr::IdentifierType* identifierType,
                           fr::io::CoordinateTransform* coordinateTransform,
                           fr::IndentFileWriter* file);

  static void writeNet(int32_t netNumber, fr::BasicBoard* board,
                       fr::IdentifierType* identifierType,
                       fr::io::CoordinateTransform* coordinateTransform,
                       fr::IndentFileWriter* file);

  static void writeWire(fr::PolylineTrace* wire, fr::BasicBoard* board,
                        fr::IdentifierType* identifierType,
                        fr::io::CoordinateTransform* coordinateTransform,
                        fr::IndentFileWriter* file);

  static void writeVia(fr::Via* via, fr::BasicBoard* board, fr::IdentifierType* identifierType,
                       fr::io::CoordinateTransform* coordinateTransform,
                       fr::IndentFileWriter* file);

  static void writeFixedState(fr::IndentFileWriter* file, fr::FixedState fixedState);

  static void writeConductionArea(fr::ConductionArea* conductionArea, fr::BasicBoard* board,
                                  fr::IdentifierType* identifierType,
                                  fr::io::CoordinateTransform* coordinateTransform,
                                  fr::IndentFileWriter* file);
};

}  // namespace fr::dsn
