// Port of app.freerouting.core.library.BoardLibrary
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes a board library of packages and padstacks. */
class BoardLibrary : public virtual jrt::Object {
 public:
  /** Creates a new instance of BoardLibrary. */
  BoardLibrary(Padstacks* padstacks, Packages* packages);

  /** Creates a new instance of BoardLibrary. */
  BoardLibrary();

  /** The count of padstacks from this.padstacks, which can be used in routing. */
  int32_t viaPadstackCount();

  /** Gets the via padstack for routing with the specified index. */
  Padstack* getViaPadstack(int32_t no);

  /** Gets the via padstack with name name, or null, if no such padstack exists. */
  Padstack* getViaPadstack(const std::string& name);

  /** Returns the via padstacks, which can be used for routing. */
  jrt::Array<Padstack*>* getViaPadstacks();

  /** Sets the subset of padstacks, which can be used in routing for inserting vias. */
  void setViaPadstacks(jrt::Array<Padstack*>* padstacks);

  /**
   * Appends a padstack to the list of via padstacks. Returns false if the list already contains a
   * padstack with the same name.
   */
  bool addViaPadstack(Padstack* padstack);

  /** Removes a padstack from the via padstack list. Returns false if it was not found. */
  bool removeViaPadstack(Padstack* padstack, BasicBoard* board);

  /**
   * Gets the via padstack mirrored to the back side of the board. Returns null, if no such via
   * padstack exists.
   */
  Padstack* getMirroredViaPadstack(Padstack* viaPadstack);

  /** Returns whether the input padstack is used on the board in a package or drill. */
  bool isUsed(Padstack* padstack, BasicBoard* board);

  Padstacks* padstacks = nullptr;
  Packages* packages = nullptr;

  /** Contains information for gate swap and pin swap in the Specctra-dsn format. */
  LogicalParts* logicalParts;

 private:
  /** The subset of padstacks in the board library, which can be used in routing for vias. */
  jrt::List<Padstack*>* viaPadstacks = nullptr;
};

}  // namespace fr
