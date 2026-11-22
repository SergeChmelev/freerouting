#include "autoroute/pipeline/RipupMemory.h"

#include "board/model/items/Item.h"

namespace fr {

// PORT NOTE: literal constant from the Java source, checked here.
static_assert(RipupMemory::MAX_FAILURES_WHILE_KEPT_AWAY == 2);

jrt::Set<int32_t>* RipupMemory::recordRipup(int32_t ripperNet, jrt::Collection<Item*>* rippedItems, int32_t pass) {
  jrt::Set<int32_t>* rippedNets = new (jrt::kOp) jrt::TreeSet<int32_t>();
  for (Item* item : *rippedItems) {
    for (int32_t i = 0; i < item->netCount(); i++) {
      rippedNets->add(item->getNetNumber(i));
    }
  }
  return recordRipupOfNets(ripperNet, rippedNets, pass);
}

jrt::Set<int32_t>* RipupMemory::recordRipupOfNets(int32_t ripperNet, jrt::Collection<int32_t>* rippedNets, int32_t pass) {
  jrt::Set<int32_t>* started = new (jrt::kOp) jrt::TreeSet<int32_t>();
  for (int32_t rippedNet : *(new (jrt::kOp) jrt::TreeSet<int32_t>(rippedNets))) {
    if (rippedNet == ripperNet || rippedNet <= 0) {
      continue;
    }
    int64_t pair = pairKey(ripperNet, rippedNet);
    if (retiredPairs->contains(pair) || flippedPairs->contains(pair)) {
      continue;
    }
    jrt::Map<int32_t, int32_t>* byRipped = seen->computeIfAbsent(
        ripperNet, [](const int32_t&) -> jrt::Map<int32_t, int32_t>* { return new (jrt::kOp) jrt::TreeMap<int32_t, int32_t>(); });
    // Java: Integer previousPass = byRipped.put(rippedNet, pass) (null when absent)
    std::optional<int32_t> previousPass = byRipped->getOpt(rippedNet);
    byRipped->put(rippedNet, pass);
    jrt::Map<int32_t, RipupMemory_Entry*>* byKeptFrom = active->get(rippedNet);
    RipupMemory_Entry* entry = byKeptFrom == nullptr ? nullptr : byKeptFrom->get(ripperNet);
    if (entry == nullptr) {
      if (!previousPass.has_value() || *previousPass == pass) {
        // the first rip-up of B by A is only remembered; more items in the same pass are the same
        continue;
      }
      byKeptFrom = active->computeIfAbsent(rippedNet, [](const int32_t&) -> jrt::Map<int32_t, RipupMemory_Entry*>* {
        return new (jrt::kOp) jrt::TreeMap<int32_t, RipupMemory_Entry*>();
      });
      entry = new (jrt::kOp) RipupMemory_Entry(ripperNet, rippedNet, pass, false);
      byKeptFrom->put(ripperNet, entry);
      started->add(rippedNet);
    }
    entry->lastPass = pass;
    entry->ripupCount++;
  }
  return started;
}

jrt::Set<int32_t>* RipupMemory::netsNotToRip(int32_t net) {
  jrt::Map<int32_t, RipupMemory_Entry*>* byKeptFrom = active->get(net);
  if (byKeptFrom == nullptr || byKeptFrom->isEmpty()) {
    return jrt::Collections::emptySet<int32_t>();
  }
  return new (jrt::kOp) jrt::TreeSet<int32_t>(byKeptFrom->keySet());
}

bool RipupMemory::isKeptAway(int32_t net) {
  jrt::Map<int32_t, RipupMemory_Entry*>* byKeptFrom = active->get(net);
  return byKeptFrom != nullptr && !byKeptFrom->isEmpty();
}

RipupMemory_Outcome RipupMemory::recordOutcome(int32_t net, int32_t keptFromNet, bool routed, int32_t pass) {
  jrt::Map<int32_t, RipupMemory_Entry*>* byKeptFrom = active->get(net);
  RipupMemory_Entry* entry = byKeptFrom == nullptr ? nullptr : byKeptFrom->get(keptFromNet);
  if (entry == nullptr) {
    return RipupMemory_Outcome::NONE;
  }
  if (routed) {
    remove(net, keptFromNet);
    return RipupMemory_Outcome::RESOLVED;
  }
  if (entry->lastFailurePass == pass) {
    return RipupMemory_Outcome::NONE;
  }
  entry->lastFailurePass = pass;
  entry->failuresWhileKeptAway++;
  if (entry->failuresWhileKeptAway < MAX_FAILURES_WHILE_KEPT_AWAY) {
    return RipupMemory_Outcome::NONE;
  }
  remove(net, keptFromNet);
  int64_t pair = pairKey(net, keptFromNet);
  if (entry->flipped) {
    retiredPairs->add(pair);
    return RipupMemory_Outcome::RETIRED;
  }
  flippedPairs->add(pair);
  active
      ->computeIfAbsent(keptFromNet, [](const int32_t&) -> jrt::Map<int32_t, RipupMemory_Entry*>* {
        return new (jrt::kOp) jrt::TreeMap<int32_t, RipupMemory_Entry*>();
      })
      ->put(net, new (jrt::kOp) RipupMemory_Entry(net, keptFromNet, pass, true));
  return RipupMemory_Outcome::FLIPPED;
}

bool RipupMemory::isRetired(int32_t netA, int32_t netB) { return retiredPairs->contains(pairKey(netA, netB)); }

int32_t RipupMemory::activeCount() {
  int32_t count = 0;
  for (jrt::Map<int32_t, RipupMemory_Entry*>* byKeptFrom : *active->values()) {
    count += byKeptFrom->size();
  }
  return count;
}

void RipupMemory::remove(int32_t keptAwayNet, int32_t keptFromNet) {
  jrt::Map<int32_t, RipupMemory_Entry*>* byKeptFrom = active->get(keptAwayNet);
  if (byKeptFrom == nullptr) {
    return;
  }
  byKeptFrom->remove(keptFromNet);
  if (byKeptFrom->isEmpty()) {
    active->remove(keptAwayNet);
  }
}

int64_t RipupMemory::pairKey(int32_t netA, int32_t netB) {
  int32_t low = jrt::Math::min(netA, netB);
  int32_t high = jrt::Math::max(netA, netB);
  return (static_cast<int64_t>(static_cast<uint64_t>(static_cast<int64_t>(low)) << 32)) |
         (static_cast<int64_t>(high) & 0xffffffffLL);
}

}  // namespace fr
