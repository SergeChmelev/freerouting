// Port of app.freerouting.rules.Net
#pragma once

#include "board/actions/ItemInfoPrinter.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes properties for an individual electrical net. (equals/hashCode: identity.) */
class Net : public virtual ItemInfoPrinter_Printable {
 public:
  /** Creates a new net. Its class is the default net class of netList's board. */
  Net(const std::string& name, int32_t subnetNumber, int32_t number, Nets* netList,
      bool containsPlane);

  /** "Net #<number> (<name>)". */
  std::string toString() override {
    return "Net #" + jrt::str(this->netNumber) + " (" + this->name + ")";
  }

  /** Comparable<Net>: by name (ignoring case, then exact), subnet number, net number. */
  virtual int32_t compareTo(Net* other);

  /** Returns the class of this net. */
  NetClass* getNetClass() { return this->netClass; }

  /** True if the class of this net is routed first (router.autorouter.priority_net_classes). */
  bool isRoutedFirst();

  /** Sets the class of this net. */
  void setClass(NetClass* netClass) { this->netClass = netClass; }

  /**
   * Returns the minimum trace length of this net in board units; falls back to the net class. If
   * <= 0, there is no minimal trace length restriction.
   */
  double getMinimumTraceLength();

  /** Sets the explicit minimum trace length of this net (<= 0: none at the net level). */
  void setMinimumTraceLength(double value);

  /**
   * Returns the maximum trace length of this net in board units; falls back to the net class. If
   * <= 0, there is no maximal trace length restriction.
   */
  double getMaximumTraceLength();

  /** Sets the explicit maximum trace length of this net (<= 0: none at the net level). */
  void setMaximumTraceLength(double value);

  /** Returns the effective length constraint (net-level combined with net class). */
  NetLengthConstraint* getLengthConstraint();

  /** Sets the explicit length constraint (null or UNCONSTRAINED: inherit from the net class). */
  void setLengthConstraint(NetLengthConstraint* constraint) { this->lengthConstraint = constraint; }

  /** Returns true if this net has an explicit net-level trace length constraint. */
  bool hasExplicitLengthConstraint();

  /** Returns the explicit net-level length constraint, or null. */
  NetLengthConstraint* getExplicitLengthConstraint() { return this->lengthConstraint; }

  /**
   * Returns the effective meander constraint: explicit, else net class, else the board rules
   * default (DIFF_PAIR if part of a differential pair, SINGLE_TRACK otherwise). Never null.
   */
  NetMeanderConstraint* getMeanderConstraint();

  /** Sets the explicit meander constraint (null or UNCONSTRAINED: inherit). */
  void setMeanderConstraint(NetMeanderConstraint* constraint) {
    this->meanderConstraint = constraint;
  }

  /** Returns true if this net has an explicit net-level meander constraint. */
  bool hasExplicitMeanderConstraint();

  /** Returns the explicit net-level meander constraint, or null. */
  NetMeanderConstraint* getExplicitMeanderConstraint() { return this->meanderConstraint; }

  /** Returns the pins and conduction areas of this net. */
  jrt::Collection<Item*>* getTerminalItems();

  /** Returns the pins of this net. */
  jrt::Collection<Pin*>* getPins();

  /** Returns all items of this net. */
  jrt::Collection<Item*>* getItems();

  /** Returns the cumulative trace length of all traces on the board belonging to this net. */
  double getTraceLength();

  /** Returns the count of vias on the board belonging to this net. */
  int32_t getViaCount();

  /** Sets whether this net contains a power plane. */
  void setContainsPlane(bool value) { containsPlane_ = value; }

  /**
   * Indicates, if this net contains a power plane. Used by the autorouter for setting the via costs
   * to the cheap plane via costs.
   */
  bool containsPlane() { return containsPlane_; }

  /** PORT NOTE: java.util.Locale -> std::string language tag. */
  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;

  /** The name of the net. */
  const std::string name;

  /** Used only if a net is divided internally because of fromto rules. Normally 1. */
  const int32_t subnetNumber;

  /** The unique strict positive number of the net. */
  const int32_t netNumber;

  /** The net list, where this net belongs to. */
  Nets* netList;  // final

 private:
  friend class BoardRules;

  /**
   * PORT NOTE (BoardRules::deepCopy): copy of source belonging to netList, with all fields of
   * source except the net class (left null, set by the caller). Constraints are immutable and
   * shared.
   */
  Net(Net* source, Nets* netList);

  /**
   * Indicates whether this net contains a power plane.
   * PORT NOTE: renamed from containsPlane (clashes with the method of that name).
   */
  bool containsPlane_;

  /** The routing rule of this net. */
  NetClass* netClass;

  /** Optional explicit trace length constraint for this net, or null if inheriting. */
  NetLengthConstraint* lengthConstraint = nullptr;

  /** Optional explicit trace meander constraint for this net, or null if inheriting. */
  NetMeanderConstraint* meanderConstraint = nullptr;
};

}  // namespace fr
