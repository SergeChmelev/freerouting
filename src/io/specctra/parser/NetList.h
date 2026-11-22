#pragma once

#include "fwd.h"
#include "io/specctra/parser/Net.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Describes a list of nets sorted by its names. The net number is generated internally. */
class NetList : public virtual jrt::Object {
 public:
  /** Returns true, if the netlist contains a net with the input name. */
  virtual bool contains(Net_Id* netId);

  /**
   * Adds a new net with the input name to the net list. Returns null, if a net with the input name
   * already exists in the net list. In this case no new net is added.
   */
  virtual Net* addNet(Net_Id* netId);

  /** Returns the net with the input name, or null, if the netlist does not contain a net with the
   * input name. */
  virtual Net* getNet(Net_Id* netId);

  /** Returns all nets in this net list containing the input pin. */
  virtual jrt::Collection<Net*>* getNets(const std::string& componentName,
                                         const std::string& pinName);

 private:
  /** The entries of this map are of type Net, the keys are the net_ids. */
  jrt::Map<Net_Id*, Net*>* nets = new jrt::TreeMap<Net_Id*, Net*>();
};

}  // namespace fr::dsn
