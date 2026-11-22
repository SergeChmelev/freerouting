#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Represents the routing result of a single item, comparing metrics before and after routing.
 * Java: implements Comparable<ItemRouteResult>.
 *
 * PORT NOTE: fields itemId, improved, improvementPercentage clash with the accessor methods of the
 * same name and are renamed itemId_, improved_, improvementPercentage_.
 */
class ItemRouteResult : public virtual jrt::Object {
 private:
  int32_t itemId_;
  float improvementPercentage_;
  int32_t viaCountBefore;
  int32_t viaCountAfter;
  double traceLengthBefore;
  double traceLengthAfter;
  int32_t incompleteCountBefore_;  // clashes with method incompleteCountBefore()
  int32_t incompleteCountAfter;
  bool improved_ = false;

 public:
  /** Constructs an unimproved ItemRouteResult for the given item ID. */
  explicit ItemRouteResult(int32_t itemId);

  /** Constructs an ItemRouteResult comparing metrics before and after routing. */
  ItemRouteResult(int32_t itemId, int32_t viaCountBefore, int32_t viaCountAfter,
                  double traceLengthBefore, double traceLengthAfter,
                  int32_t incompleteCountBefore, int32_t incompleteCountAfter);

  virtual int32_t compareTo(ItemRouteResult* r);

  /** Returns true if this result represents an improvement over r. */
  virtual bool improvedOver(ItemRouteResult* r) { return this->compareTo(r) < 0; }

  /** Returns the ID of the routed item. */
  virtual int32_t itemId() { return this->itemId_; }

  /** Returns true if the routing result was improved. */
  virtual bool improved() { return this->improved_; }

  /** Returns the calculated improvement percentage. */
  virtual float improvementPercentage() { return this->improvementPercentage_; }

  /** Returns the via count after routing. */
  virtual int32_t viaCount() { return viaCountAfter; }

  /** Returns the total trace length after routing. */
  virtual double traceLength() { return traceLengthAfter; }

  /** Returns the count of incomplete connections after routing. */
  virtual int32_t incompleteCount() { return incompleteCountAfter; }

  /** Returns the net reduction in via count. */
  virtual int32_t viaCountReduced() { return viaCountBefore - viaCountAfter; }

  /** Returns the net reduction in trace length. */
  virtual double lengthReduced() { return traceLengthBefore - traceLengthAfter; }

  /** Updates the improved flag for this result. */
  virtual void updateImproved(bool improved) { this->improved_ = improved; }

  /** Returns the count of incomplete connections before routing. */
  virtual int32_t incompleteCountBefore() { return incompleteCountBefore_; }
};

}  // namespace fr
