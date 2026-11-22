#include "geometry/planar/FortyfiveDegreeDirection.h"

#include "geometry/planar/Direction.h"
#include "geometry/planar/IntDirection.h"

namespace fr {

IntDirection* FortyfiveDegreeDirection_getDirection(FortyfiveDegreeDirection self) {
  switch (self) {
    case FortyfiveDegreeDirection::RIGHT: return Direction::RIGHT();
    case FortyfiveDegreeDirection::RIGHT45: return Direction::RIGHT45();
    case FortyfiveDegreeDirection::UP: return Direction::UP();
    case FortyfiveDegreeDirection::UP45: return Direction::UP45();
    case FortyfiveDegreeDirection::LEFT: return Direction::LEFT();
    case FortyfiveDegreeDirection::LEFT45: return Direction::LEFT45();
    case FortyfiveDegreeDirection::DOWN: return Direction::DOWN();
    case FortyfiveDegreeDirection::DOWN45: return Direction::DOWN45();
  }
  return nullptr;
}

}  // namespace fr
