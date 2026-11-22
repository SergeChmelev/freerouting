#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::io {

/** Supported board and routing file formats. */
enum class FileFormat : int32_t {
  UNKNOWN,
  DSN,
  FRB,
  SES,
  RULES,
  SCR,
  DRC_JSON,
  KICAD_DESIGN_JSON,
  KICAD_SESSION_JSON
};

inline std::string FileFormat_name(FileFormat e) {
  switch (e) {
    case FileFormat::UNKNOWN: return "UNKNOWN";
    case FileFormat::DSN: return "DSN";
    case FileFormat::FRB: return "FRB";
    case FileFormat::SES: return "SES";
    case FileFormat::RULES: return "RULES";
    case FileFormat::SCR: return "SCR";
    case FileFormat::DRC_JSON: return "DRC_JSON";
    case FileFormat::KICAD_DESIGN_JSON: return "KICAD_DESIGN_JSON";
    case FileFormat::KICAD_SESSION_JSON: return "KICAD_SESSION_JSON";
  }
  return "";
}

inline std::string FileFormat_toString(FileFormat e) { return FileFormat_name(e); }

inline jrt::Array<FileFormat>* FileFormat_values() {
  return jrt::Array<FileFormat>::of(
      {FileFormat::UNKNOWN, FileFormat::DSN, FileFormat::FRB, FileFormat::SES, FileFormat::RULES,
       FileFormat::SCR, FileFormat::DRC_JSON, FileFormat::KICAD_DESIGN_JSON,
       FileFormat::KICAD_SESSION_JSON});
}

inline FileFormat FileFormat_valueOf(const std::string& name) {
  for (FileFormat e : *FileFormat_values()) {
    if (FileFormat_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException("No enum constant app.freerouting.io.FileFormat." + name);
}

}  // namespace fr::io
