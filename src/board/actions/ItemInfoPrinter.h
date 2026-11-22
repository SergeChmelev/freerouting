// Port of app.freerouting.board.actions.ItemInfoPrinter (+ nested Printable).
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

// PORT NOTE: java.util.Locale has no runtime equivalent; every `Locale locale` parameter of the
// port is passed as `const std::string& locale` (the language tag, e.g. "en").

/** Functionality needed for objects to print information into an ItemInfoPrinter. */
class ItemInfoPrinter_Printable : public virtual jrt::Object {
 public:
  /** Prints information about this object into the target printer. */
  virtual void printInfo(ItemInfoPrinter* printer, const std::string& locale) = 0;
};

/** Output printer interface for printing detailed human-readable information about items. */
class ItemInfoPrinter : public virtual jrt::Object {
 public:
  /** Appends string to the output printer. Returns false if that was not possible. */
  virtual bool append(const std::string& string) = 0;

  /**
   * Appends a link for opening/creating a new window with the information of object. Returns
   * false if that was not possible.
   */
  virtual bool append(const std::string& linkName, const std::string& windowTitle,
                      ItemInfoPrinter_Printable* object) = 0;

  /** Appends value after transforming it to the user coordinate system. */
  virtual bool append(double value) = 0;

  /** Appends point after transforming it to the user coordinate system. */
  virtual bool append(FloatPoint* point) = 0;

  /** Appends shape after transforming it to the user coordinate system. */
  virtual bool append(Shape* shape, const std::string& locale) = 0;

  /** Appends value without transforming it to the user coordinate system. */
  virtual bool appendWithoutTransforming(double value) = 0;

  /** Appends string in bold style to the output printer. */
  virtual bool appendBold(const std::string& string) = 0;

  /** Begins a new line in the output printer. */
  virtual bool newline() = 0;

  /** Appends a fixed number of spaces to the output printer. */
  virtual bool indent() = 0;

  /** Appends a link for creating a new window with the information of items. */
  virtual bool appendItems(const std::string& linkName, const std::string& windowTitle,
                           jrt::Collection<Item*>* items) = 0;

  /** Appends a link for creating a new window with the information of printable objects. */
  virtual bool appendObjects(const std::string& buttonName, const std::string& windowTitle,
                             jrt::Collection<ItemInfoPrinter_Printable*>* objects) = 0;
};

}  // namespace fr
