#include "autoroute/pipeline/DanglingCopper.h"

#include <algorithm>
#include <vector>

#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "drc/NetIncompletes.h"

namespace fr {

std::optional<std::string> DanglingCopper::remove(RoutingBoard* board, const std::string& where) {
  jrt::List<Item*>* tails = new (jrt::kOp) jrt::ArrayList<Item*>();
  for (Item* item : *board->getItems()) {
    if (item->isRoutable() && item->netCount() == 1 && item->isTail()) {
      tails->add(item);
    }
  }
  if (tails->isEmpty()) {
    return std::nullopt;
  }
  // tails.stream().mapToInt(t -> t.getNetNumber(0)).distinct().sorted().toArray()
  std::vector<int32_t> netVec;
  for (Item* t : *tails) {
    netVec.push_back(t->getNetNumber(0));
  }
  std::sort(netVec.begin(), netVec.end());
  netVec.erase(std::unique(netVec.begin(), netVec.end()), netVec.end());
  auto* nets = new (jrt::kOp) jrt::Array<int32_t>(static_cast<int32_t>(netVec.size()));
  for (int32_t i = 0; i < nets->length; i++) {
    (*nets)[i] = netVec[i];
  }
  int32_t openBefore = open(board, nets);
  int32_t viasBefore = board->getVias()->size();
  int32_t tracesBefore = board->getTraces()->size();
  board->generateSnapshot();
  int32_t passes = 0;
  while (passes < MAX_PASSES && board->removeTraceTails(-1, Item_StopConnectionOption::NONE)) {
    passes++;
  }
  int32_t openAfter = open(board, nets);
  if (openAfter > openBefore) {
    board->undo(nullptr);
    return "Dangling copper after " + where + ": " + jrt::str(tails->size()) +
           " tail(s) left as routed, removing them would open " + jrt::str(openAfter - openBefore) +
           " connection(s) of their nets (" + jrt::str(openBefore) + " open before).";
  }
  board->popSnapshot();
  return "Dangling copper after " + where + ": removed " +
         jrt::str(viasBefore - board->getVias()->size()) + " via(s) and " +
         jrt::str(tracesBefore - board->getTraces()->size()) + " trace piece(s) behind " +
         jrt::str(tails->size()) + " tail(s) in " + jrt::str(passes) + " pass(es); " +
         jrt::str(openAfter) + " connection(s) of their nets open, as before.";
}

int32_t DanglingCopper::open(RoutingBoard* board, jrt::Array<int32_t>* nets) {
  int32_t count = 0;
  for (int32_t i = 0; i < nets->length; i++) {
    int32_t net = (*nets)[i];
    count += (new (jrt::kOp) NetIncompletes(net, board->getConnectableItems(net), board))->count();
  }
  return count;
}

}  // namespace fr
