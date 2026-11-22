#include "io/specctra/DsnReader.h"

#include <filesystem>
#include <iterator>
#include <sstream>

#include "board/actions/ItemIdGenerator.h"
#include "board/facade/BasicBoard.h"
#include "board/state/BoardObserverAdaptor.h"
#include "io/BoardMetadata.h"
#include "io/BoardReadResult.h"
#include "io/specctra/DsnReadOptions.h"
#include "io/specctra/parser/DsnFile.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/Keyword.h"
#include "io/specctra/parser/LayerStructure.h"
#include "io/specctra/parser/PolygonWireScan.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "io/specctra/parser/SpecctraDsnStreamReader.h"
#include "logger/FRLogger.h"

namespace fr::dsn {

namespace {

// Opens a fresh std::istringstream over the same in-memory DSN text.
class StringStreamOpener : public DsnReader_StreamOpener {
 public:
  explicit StringStreamOpener(std::string text) : text_(std::move(text)) {}
  std::unique_ptr<std::istream> open() override {
    return std::make_unique<std::istringstream>(text_);
  }

 private:
  std::string text_;
};

std::string arrayToString(jrt::Array<int8_t>* bytes) {
  return std::string(reinterpret_cast<const char*>(bytes->data()),
                     static_cast<std::size_t>(bytes->length));
}

bool isBlank(const std::optional<std::string>& s) {
  return !s.has_value() || jrt::strip(*s).empty();
}

}  // namespace

fr::io::BoardReadResult* DsnReader::readBoard(std::istream& inputStream,
                                              fr::BoardObservers* observers,
                                              fr::IdGenerator* idGenerator) {
  return readBoard(inputStream, observers, idGenerator, std::nullopt);
}

fr::io::BoardReadResult* DsnReader::readBoard(std::istream& inputStream,
                                              fr::BoardObservers* observers,
                                              fr::IdGenerator* idGenerator,
                                              const std::optional<std::string>& designName) {
  return readBoard(inputStream, observers, idGenerator, designName, DsnReadOptions::DEFAULT());
}

fr::io::BoardReadResult* DsnReader::readBoard(std::istream& inputStream,
                                              fr::BoardObservers* observers,
                                              fr::IdGenerator* idGenerator,
                                              const std::optional<std::string>& designName,
                                              DsnReadOptions* options) {
  // Apply default implementations for nullable parameters so the board's
  // Communication object is fully initialised even in lightweight test scenarios.
  if (observers == nullptr) {
    observers = new fr::BoardObserverAdaptor();
  }
  if (idGenerator == nullptr) {
    idGenerator = new fr::ItemIdGenerator();
  }

  // PORT NOTE: Java spools the stream to a temporary file so that the wiring pre-scan and the full
  // parse can both stream it. The port spools it into memory instead (same content, both passes
  // read an independent stream over it); deleteQuietly is therefore not needed here.
  std::string spooled;
  {
    std::ostringstream buffer;
    buffer << inputStream.rdbuf();
    if (inputStream.bad()) {
      closeQuietly(inputStream);
      return new fr::io::BoardReadResult_IoError(jrt::IOException("error reading the DSN stream"));
    }
    spooled = buffer.str();
  }

  std::istringstream parseStream(spooled);
  return readBoardFromStreams(parseStream, new StringStreamOpener(spooled), observers,
                              idGenerator, designName, options);
}

fr::io::BoardReadResult* DsnReader::readBoard(jrt::Array<int8_t>* dsnBytes,
                                              fr::BoardObservers* observers,
                                              fr::IdGenerator* idGenerator,
                                              const std::optional<std::string>& designName) {
  return readBoard(dsnBytes, observers, idGenerator, designName, DsnReadOptions::DEFAULT());
}

fr::io::BoardReadResult* DsnReader::readBoard(jrt::Array<int8_t>* dsnBytes,
                                              fr::BoardObservers* observers,
                                              fr::IdGenerator* idGenerator,
                                              const std::optional<std::string>& designName,
                                              DsnReadOptions* options) {
  if (dsnBytes == nullptr || dsnBytes->length == 0) {
    return new fr::io::BoardReadResult_ParseError("(pcb", "dsnBytes must not be null or empty");
  }
  if (observers == nullptr) {
    observers = new fr::BoardObserverAdaptor();
  }
  if (idGenerator == nullptr) {
    idGenerator = new fr::ItemIdGenerator();
  }
  std::string text = arrayToString(dsnBytes);
  std::istringstream parseStream(text);
  return readBoardFromStreams(parseStream, new StringStreamOpener(text), observers, idGenerator,
                              designName, options);
}

fr::io::BoardReadResult* DsnReader::readBoardFromStreams(
    std::istream& parseStream, DsnReader_StreamOpener* scanStreamOpener,
    fr::BoardObservers* observers, fr::IdGenerator* idGenerator,
    const std::optional<std::string>& designName, DsnReadOptions* options) {
  try {
    auto* scanner = new SpecctraDsnStreamReader(parseStream);

    // ---------------------------------------------------------------------
    // Validate the "(pcb <name>" header
    // ---------------------------------------------------------------------
    std::optional<std::string> pcbTokenName;
    for (int32_t i = 0; i < 3; i++) {
      jrt::Object* token = scanner->nextToken();
      bool ok = true;
      if (i == 0) {
        ok = token == static_cast<jrt::Object*>(Keyword::OPEN_BRACKET());
      } else if (i == 1) {
        ok = token == static_cast<jrt::Object*>(Keyword::PCB_SCOPE());
        // switch the scanner to NAME mode so the pcb-name token is consumed cleanly
        scanner->yybegin(SpecctraDsnStreamReader::NAME);
      } else {
        // i == 2: the design name string immediately following "(pcb"
        if (auto* s = jrt::fast_cast<JString>(token)) {
          pcbTokenName = s->value;
        }
      }
      if (!ok) {
        return new fr::io::BoardReadResult_ParseError(
            "(pcb", "Not a Specctra DSN file: expected '(pcb <name>' header");
      }
    }

    // Resolve the effective design name for log messages:
    // prefer the caller-supplied filename, then the pcb-name token, then a fallback.
    std::string effectiveDesignName;
    if (!isBlank(designName)) {
      effectiveDesignName = std::filesystem::path(*designName).filename().string();
    } else if (!isBlank(pcbTokenName)) {
      effectiveDesignName = *pcbTokenName;
    } else {
      effectiveDesignName = "unknown";
    }

    // ---------------------------------------------------------------------
    // Parse the body: the board is constructed inside ReadScopeParameter's shim
    // ---------------------------------------------------------------------
    DsnReadOptions* effectiveOptions = options != nullptr ? options : DsnReadOptions::DEFAULT();
    auto* scopeParameter = new ReadScopeParameter(scanner, observers, idGenerator,
                                                  effectiveOptions->canonicalPlacementOrder());
    {
      std::unique_ptr<std::istream> scanStream = scanStreamOpener->open();
      scopeParameter->polygonWireNetsByLayer = PolygonWireScan::netsByLayer(*scanStream);
    }
    bool readOk = Keyword::PCB_SCOPE()->readScope(scopeParameter);

    fr::BasicBoard* board = scopeParameter->getBoard();

    if (readOk) {
      DsnFile::convertPlatedSlotKeepouts(board);
      DsnFile::removePinEnclosingKeepouts(board);
      // Apply power-plane autoroute settings if the DSN had no (autoroute ...) scope
      if (scopeParameter->autorouteSettings == nullptr) {
        DsnFile::adjustPlaneAutorouteSettings(board);
      }
      jrt::List<std::string>* warnings = scopeParameter->getWarnings();
      if (!warnings->isEmpty()) {
        FRLogger::warn("DSN file '" + effectiveDesignName + "' was loaded with " +
                       jrt::str(warnings->size()) + " warning(s).");
      }
      return new fr::io::BoardReadResult_Success(board, nullptr, warnings);
    } else if (!scopeParameter->boardOutlineOk) {
      jrt::List<std::string>* warnings = scopeParameter->getWarnings();
      if (!warnings->isEmpty()) {
        FRLogger::warn("DSN file '" + effectiveDesignName + "' was loaded with " +
                       jrt::str(warnings->size()) + " warning(s).");
      }
      return new fr::io::BoardReadResult_OutlineMissing(board, nullptr, warnings);
    } else {
      return new fr::io::BoardReadResult_ParseError("(pcb", "DSN structure parsing failed");
    }
  } catch (const jrt::IOException& e) {
    return new fr::io::BoardReadResult_IoError(e);
  }
}

fr::io::BoardReadResult* DsnReader::readMetadata(std::istream& inputStream) {
  fr::BoardObservers* observers = new fr::BoardObserverAdaptor();
  fr::IdGenerator* idGenerator = new fr::ItemIdGenerator();
  auto* scanner = new SpecctraDsnStreamReader(inputStream);

  // -----------------------------------------------------------------------
  // Validate the "(pcb <name>" header: same three-token check as readBoard
  // -----------------------------------------------------------------------
  for (int32_t i = 0; i < 3; i++) {
    jrt::Object* token;
    try {
      token = scanner->nextToken();
    } catch (const jrt::IOException& e) {
      closeQuietly(inputStream);
      return new fr::io::BoardReadResult_IoError(e);
    }
    bool ok = true;
    if (i == 0) {
      ok = token == static_cast<jrt::Object*>(Keyword::OPEN_BRACKET());
    } else if (i == 1) {
      ok = token == static_cast<jrt::Object*>(Keyword::PCB_SCOPE());
      scanner->yybegin(SpecctraDsnStreamReader::NAME);
    }
    if (!ok) {
      closeQuietly(inputStream);
      return new fr::io::BoardReadResult_ParseError(
          "(pcb", "Not a Specctra DSN file: expected '(pcb <name>' header");
    }
  }

  // -----------------------------------------------------------------------
  // Custom PCB-level loop: only parse metadata-relevant scopes. Stop reading as soon as
  // (structure ...) ends, skipping all subsequent heavy scopes.
  // -----------------------------------------------------------------------
  auto* scopeParameter = new ReadScopeParameter(scanner, observers, idGenerator);
  // The wiring is never read here: power layers whose copper comes as wire polygons cannot be
  // told apart from empty ones, so the layer types are not final.
  scopeParameter->wiringIsRead = false;
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scanner->nextToken();
    } catch (const jrt::IOException& e) {
      closeQuietly(inputStream);
      return new fr::io::BoardReadResult_IoError(e);
    }
    if (nextToken == nullptr || nextToken == static_cast<jrt::Object*>(Keyword::CLOSED_BRACKET())) {
      break;  // EOF or end of (pcb ...) scope
    }
    if (prevToken == static_cast<jrt::Object*>(Keyword::OPEN_BRACKET())) {
      if (nextToken == static_cast<jrt::Object*>(Keyword::PARSER_SCOPE())) {
        // Populates scopeParameter.hostCad, hostVersion, stringQuote
        Keyword::PARSER_SCOPE()->readScope(scopeParameter);
      } else if (nextToken == static_cast<jrt::Object*>(Keyword::RESOLUTION_SCOPE())) {
        // Populates scopeParameter.unit, resolution
        Keyword::RESOLUTION_SCOPE()->readScope(scopeParameter);
      } else if (nextToken == static_cast<jrt::Object*>(Keyword::STRUCTURE_SCOPE())) {
        // Populates layerStructure, snapAngle, autorouteSettings and creates the board (if a
        // valid boundary exists). Return value is ignored.
        Keyword::STRUCTURE_SCOPE()->readScope(scopeParameter);
        break;  // stop here: skip library, placement, network, wiring
      } else {
        ScopeKeyword::skipScope(scanner);
      }
    }
  }

  closeQuietly(inputStream);

  // -----------------------------------------------------------------------
  // Build BoardMetadata from the parsed fields.
  // -----------------------------------------------------------------------
  int32_t layerCount = 0;
  if (scopeParameter->layerStructure != nullptr) {
    layerCount = scopeParameter->layerStructure->layers->length;
  } else if (scopeParameter->getBoard() != nullptr) {
    layerCount = scopeParameter->getBoard()->getLayerCount();
  }

  auto* metadata = new fr::io::BoardMetadata(
      scopeParameter->hostCad, scopeParameter->hostVersion, layerCount, scopeParameter->unit,
      scopeParameter->resolution, scopeParameter->snapAngle, scopeParameter->autorouteSettings);

  return new fr::io::BoardReadResult_Success(scopeParameter->getBoard(), metadata,
                                             scopeParameter->getWarnings());
}

void DsnReader::deleteQuietly(const std::optional<std::string>& path) {
  if (path.has_value()) {
    std::error_code ec;
    std::filesystem::remove(*path, ec);  // ignore errors
  }
}

}  // namespace fr::dsn
