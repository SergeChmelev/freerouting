// Port of app.freerouting.board.state.Communication
#include "board/state/Communication.h"

#include "board/actions/ItemIdGenerator.h"
#include "board/model/structure/Unit.h"
#include "board/state/BoardObserverAdaptor.h"
#include "io/CoordinateTransform.h"

namespace fr {

Communication* Communication::copyForBoard() {
  IdGenerator* idCopy = nullptr;
  if (auto* itemIdGenerator = jrt::fast_cast<ItemIdGenerator>(this->idGenerator)) {
    idCopy = itemIdGenerator->copy();
  } else if (this->idGenerator != nullptr) {
    // TODO(port): only ItemIdGenerator is used as IdGenerator in the port.
    throw jrt::IllegalStateException("Communication.copyForBoard: unsupported IdGenerator");
  }
  return new (jrt::kBoard) Communication(this->unit, this->resolution, this->specctraParserInfo,
                           this->coordinateTransform, idCopy,
                           // Java Communication.readObject: observers = new BoardObserverAdaptor().
                           new (jrt::kBoard) BoardObserverAdaptor());
}

Communication::Communication()
    : Communication(Unit::MIL, 1,
                    new (jrt::kBoard) Communication_SpecctraParserInfo("\"", std::nullopt, std::nullopt, nullptr,
                                                         nullptr, false),
                    new (jrt::kBoard) io::CoordinateTransform(1, 0, 0), new (jrt::kBoard) ItemIdGenerator(),
                    new (jrt::kBoard) BoardObserverAdaptor()) {}

bool Communication::hostCadIsFusion() {
  if (specctraParserInfo == nullptr || !specctraParserInfo->hostCad.has_value()) {
    return false;
  }
  std::string cad = jrt::toLowerCase(*specctraParserInfo->hostCad);
  return jrt::contains(cad, "fusion") || jrt::contains(cad, "cadsoft") ||
         jrt::contains(cad, "eagle");
}

bool Communication::hostCadIsKiCad() {
  if (specctraParserInfo == nullptr || !specctraParserInfo->hostCad.has_value()) {
    return false;
  }
  return jrt::contains(jrt::toLowerCase(*specctraParserInfo->hostCad), "kicad");
}

bool Communication::hostIsOldKicad() {
  if ((specctraParserInfo == nullptr) || (!specctraParserInfo->hostCad.has_value()) ||
      (!specctraParserInfo->hostVersion.has_value())) {
    return false;
  }

  if (jrt::contains(jrt::toLowerCase(*specctraParserInfo->hostCad), "kicad")) {
    const std::string& versionString = *specctraParserInfo->hostVersion;
    // Pattern "\\d+": first run of ASCII digits
    size_t start = 0;
    while (start < versionString.size() && !(versionString[start] >= '0' && versionString[start] <= '9')) {
      ++start;
    }
    if (start < versionString.size()) {
      size_t end = start;
      while (end < versionString.size() && versionString[end] >= '0' && versionString[end] <= '9') {
        ++end;
      }
      int32_t versionNumber = jrt::Integer::parseInt(versionString.substr(start, end - start));
      return versionNumber <= 5;
    }
  }

  return false;
}

bool Communication::hostCadExists() {
  return specctraParserInfo != nullptr && specctraParserInfo->hostCad.has_value();
}

double Communication::getResolution(Unit unit) {
  if (this->coordinateTransform != nullptr) {
    return Unit_scale(this->coordinateTransform->dsnToBoard(1), unit, this->unit);
  }
  return Unit_scale(this->resolution, unit, this->unit);
}

double Communication::physicalToBoard(double value, Unit unit) {
  if (this->coordinateTransform != nullptr) {
    return Unit_scale(value * this->coordinateTransform->dsnToBoard(1), unit, this->unit);
  }
  return Unit_scale(value * this->resolution, unit, this->unit);
}

}  // namespace fr
