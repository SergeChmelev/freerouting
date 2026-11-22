// Differential-testing helper: C++ twin of tools/parity/BoardDump.java (identical output format).
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "board/facade/BasicBoard.h"
#include "board/model/items/Item.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/FixedState.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/TileShape.h"
#include "io/BoardReadResult.h"
#include "io/specctra/DsnReader.h"
#include "jrt/jrt.h"
#include "rules/BoardRules.h"

using namespace fr;

int main(int argc, char** argv) {
  std::ifstream in(argv[1], std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto* bytes = new jrt::Array<int8_t>(static_cast<int32_t>(data.size()));
  std::memcpy(bytes->data(), data.data(), data.size());
  io::BoardReadResult* r = dsn::DsnReader::readBoard(bytes, nullptr, nullptr, std::string("dump"));
  BasicBoard* b = nullptr;
  if (auto* s = dynamic_cast<io::BoardReadResult_Success*>(r)) b = s->board();
  else if (auto* o = dynamic_cast<io::BoardReadResult_OutlineMissing*>(r)) b = o->board();
  else {
    std::cerr << "read failed\n";
    return 1;
  }
  std::cout << "angle " << AngleRestriction_toString(b->rules->getTraceAngleRestriction()) << "\n";
  std::cout << "layers " << b->getLayerCount() << "\n";
  for (Item* it : *b->getItems()) {
    std::string sb = jrt::className(it) + " id=" + jrt::str(it->getId()) + " nets=";
    for (int32_t i = 0; i < it->netCount(); i++) sb += (i > 0 ? "," : "") + jrt::str(it->getNetNumber(i));
    sb += " cl=" + jrt::str(it->clearanceClassIndex());
    sb += " layers=" + jrt::str(it->firstLayer()) + "-" + jrt::str(it->lastLayer());
    sb += " fixed=" + FixedState_toString(it->getFixedState());
    std::cout << sb << "\n";
    for (int32_t i = 0; i < it->tileShapeCount(); i++) {
      TileShape* t = it->getTileShape(i);
      IntBox* bb = t->boundingBox();
      std::cout << "  shape " << i << " bbox=" << bb->ll->x << "," << bb->ll->y << "," << bb->ur->x << ","
                << bb->ur->y << " " << jrt::str(static_cast<jrt::Object*>(t)) << "\n";
    }
  }
  return 0;
}
