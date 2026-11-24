#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** How a pad is connected to the fill of a zone ({@code zone_connect} of KiCad). */
enum class KiCadBoardZones_PadConnection : int32_t {
  /** Not set here: the footprint, then the zone, decides. */
  INHERITED,
  /** The fill keeps its clearance to the pad. */
  NONE,
  /** The fill stops at the thermal gap; up to four spokes cross it. */
  THERMAL,
  /** The fill covers the pad. */
  SOLID,
  /** A thermal relief for a through-hole pad, a solid connection for any other pad. */
  THT_THERMAL
};

inline std::string KiCadBoardZones_PadConnection_name(KiCadBoardZones_PadConnection e) {
  switch (e) {
    case KiCadBoardZones_PadConnection::INHERITED:
      return "INHERITED";
    case KiCadBoardZones_PadConnection::NONE:
      return "NONE";
    case KiCadBoardZones_PadConnection::THERMAL:
      return "THERMAL";
    case KiCadBoardZones_PadConnection::SOLID:
      return "SOLID";
    case KiCadBoardZones_PadConnection::THT_THERMAL:
      return "THT_THERMAL";
  }
  return "";
}

inline KiCadBoardZones_PadConnection KiCadBoardZones_PadConnection_fromKiCad(const std::string& value) {
  if (value == "0") return KiCadBoardZones_PadConnection::NONE;
  if (value == "1") return KiCadBoardZones_PadConnection::THERMAL;
  if (value == "2") return KiCadBoardZones_PadConnection::SOLID;
  if (value == "3") return KiCadBoardZones_PadConnection::THT_THERMAL;
  return KiCadBoardZones_PadConnection::INHERITED;
}

/** How KiCad smooths the corners of the outline of a zone before it fills it. */
enum class KiCadBoardZones_CornerSmoothing : int32_t { NONE, FILLET, CHAMFER };

/**
 * One pad of a footprint of the board, with what decides its connection to a zone (Java record).
 * Positions are absolute, in the frame of KiCad (the y axis points down); angles are in degrees,
 * counter-clockwise on screen. See the Java documentation of the components.
 */
class KiCadBoardZones_Pad : public virtual jrt::Object {
 public:
  KiCadBoardZones_Pad(double xUm, double yUm, double shapeXUm, double shapeYUm, double angleDeg,
                      double sizeXUm, double sizeYUm, bool circular, bool throughHole,
                      jrt::List<std::string>* layers, KiCadBoardZones_PadConnection connection,
                      double thermalGapUm, double thermalBridgeWidthUm, double spokeAngleDeg)
      : xUm_(xUm),
        yUm_(yUm),
        shapeXUm_(shapeXUm),
        shapeYUm_(shapeYUm),
        angleDeg_(angleDeg),
        sizeXUm_(sizeXUm),
        sizeYUm_(sizeYUm),
        circular_(circular),
        throughHole_(throughHole),
        layers_(layers),
        connection_(connection),
        thermalGapUm_(thermalGapUm),
        thermalBridgeWidthUm_(thermalBridgeWidthUm),
        spokeAngleDeg_(spokeAngleDeg) {}

  double xUm() { return xUm_; }
  double yUm() { return yUm_; }
  double shapeXUm() { return shapeXUm_; }
  double shapeYUm() { return shapeYUm_; }
  double angleDeg() { return angleDeg_; }
  double sizeXUm() { return sizeXUm_; }
  double sizeYUm() { return sizeYUm_; }
  bool circular() { return circular_; }
  bool throughHole() { return throughHole_; }
  jrt::List<std::string>* layers() { return layers_; }
  KiCadBoardZones_PadConnection connection() { return connection_; }
  double thermalGapUm() { return thermalGapUm_; }
  double thermalBridgeWidthUm() { return thermalBridgeWidthUm_; }
  double spokeAngleDeg() { return spokeAngleDeg_; }

  /** Whether the pad has copper on the layer of this name. */
  bool isOnLayer(const std::string& layerName);

 private:
  double xUm_;
  double yUm_;
  double shapeXUm_;
  double shapeYUm_;
  double angleDeg_;
  double sizeXUm_;
  double sizeYUm_;
  bool circular_;
  bool throughHole_;
  jrt::List<std::string>* layers_;
  KiCadBoardZones_PadConnection connection_;
  double thermalGapUm_;
  double thermalBridgeWidthUm_;
  double spokeAngleDeg_;
};

/**
 * One copper zone of the board (Java record): net name (empty without net), layers as written,
 * clearance and minimum width (0 if not set), the bounding box of its outline in micrometres
 * (KiCad frame, y down), its pad connection, thermal gap and spoke width, and its corner
 * smoothing with the radius / chamfer distance.
 */
class KiCadBoardZones_Zone : public virtual jrt::Object {
 public:
  KiCadBoardZones_Zone(const std::string& netName, jrt::List<std::string>* layers,
                       double clearanceUm, double minThicknessUm, double minXUm, double minYUm,
                       double maxXUm, double maxYUm, KiCadBoardZones_PadConnection padConnection,
                       double thermalGapUm, double thermalBridgeWidthUm,
                       KiCadBoardZones_CornerSmoothing cornerSmoothing, double cornerRadiusUm)
      : netName_(netName),
        layers_(layers),
        clearanceUm_(clearanceUm),
        minThicknessUm_(minThicknessUm),
        minXUm_(minXUm),
        minYUm_(minYUm),
        maxXUm_(maxXUm),
        maxYUm_(maxYUm),
        padConnection_(padConnection),
        thermalGapUm_(thermalGapUm),
        thermalBridgeWidthUm_(thermalBridgeWidthUm),
        cornerSmoothing_(cornerSmoothing),
        cornerRadiusUm_(cornerRadiusUm) {}

  std::string netName() { return netName_; }
  jrt::List<std::string>* layers() { return layers_; }
  double clearanceUm() { return clearanceUm_; }
  double minThicknessUm() { return minThicknessUm_; }
  double minXUm() { return minXUm_; }
  double minYUm() { return minYUm_; }
  double maxXUm() { return maxXUm_; }
  double maxYUm() { return maxYUm_; }
  KiCadBoardZones_PadConnection padConnection() { return padConnection_; }
  double thermalGapUm() { return thermalGapUm_; }
  double thermalBridgeWidthUm() { return thermalBridgeWidthUm_; }
  KiCadBoardZones_CornerSmoothing cornerSmoothing() { return cornerSmoothing_; }
  double cornerRadiusUm() { return cornerRadiusUm_; }

  /** Whether the zone is on the copper layer of this name. */
  bool isOnLayer(const std::string& layerName);

 private:
  std::string netName_;
  jrt::List<std::string>* layers_;
  double clearanceUm_;
  double minThicknessUm_;
  double minXUm_;
  double minYUm_;
  double maxXUm_;
  double maxYUm_;
  KiCadBoardZones_PadConnection padConnection_;
  double thermalGapUm_;
  double thermalBridgeWidthUm_;
  KiCadBoardZones_CornerSmoothing cornerSmoothing_;
  double cornerRadiusUm_;
};

/**
 * The fill settings of the copper zones of a KiCad board, read from its {@code .kicad_pcb} file.
 *
 * <p>KiCad writes a zone into a Specctra design as a plane: its outline and its net. The clearance
 * of the zone and its minimum width are lost on the way, so the plane gets the clearance of its net
 * class. When KiCad refills the zone after the routing, it keeps the copper further away from the
 * traces than Freerouting assumed and removes every neck narrower than the minimum width: a plane
 * that Freerouting saw in one piece can come back in several. This class reads the two values back.
 *
 * <p>The way a pad is connected to a zone is lost as well: solidly, through the spokes of a thermal
 * relief, or not at all. It is set on the zone, and can be overridden on a footprint or on a pad.
 * The pads are read with the settings that decide it.
 *
 * <p>Only the zones and the pads are read; the fill polygons, which can make up most of the file,
 * are skipped without being stored.
 */
class KiCadBoardZones final : public virtual jrt::Object {
 public:
  using Pad = KiCadBoardZones_Pad;
  using Zone = KiCadBoardZones_Zone;
  using PadConnection = KiCadBoardZones_PadConnection;
  using CornerSmoothing = KiCadBoardZones_CornerSmoothing;

  /**
   * The distance in micrometres that the fill of a zone keeps to the edge of the board when the
   * project does not set another one: the default of KiCad for its copper to edge clearance.
   */
  static constexpr double DEFAULT_EDGE_CLEARANCE_UM = 500.0;

  /** The pads of the footprints of the board, in file order. */
  jrt::List<Pad*>* pads() { return pads_; }

  /** The distance that the fill of the zones keeps to the edge of the board. */
  double edgeClearanceUm() { return DEFAULT_EDGE_CLEARANCE_UM; }

  /** The file name, for messages. */
  std::string sourceName() { return sourceName_; }

  /** The copper zones of the board, in file order. Rule areas (keepouts) are left out. */
  jrt::List<Zone*>* zones() { return zones_; }

  /** Reads a {@code .kicad_pcb} file (throws IOException, NoSuchFileException). */
  static KiCadBoardZones* read(const std::string& path);

  /**
   * Parses the text of a {@code .kicad_pcb} file.
   *
   * @throws IllegalArgumentException if the text is not a KiCad board
   */
  static KiCadBoardZones* parse(const std::string& text, const std::string& sourceName);

  static bool matchesLayer(jrt::List<std::string>* layers, const std::string& layerName);

 private:
  KiCadBoardZones(const std::string& sourceName, jrt::List<Zone*>* zones, jrt::List<Pad*>* pads)
      : sourceName_(sourceName), zones_(zones), pads_(pads) {}

  std::string sourceName_;
  jrt::List<Zone*>* zones_;
  jrt::List<Pad*>* pads_;
};

}  // namespace fr
