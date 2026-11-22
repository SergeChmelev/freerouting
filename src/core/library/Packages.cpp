// Port of app.freerouting.core.library.Packages
#include "core/library/Packages.h"

#include "core/library/Package.h"
#include "logger/FRLogger.h"

namespace fr {

namespace {
// SUFFIX_PATTERN.matcher(name).replaceFirst(""): removes a trailing "::<digits>" ("::\\d+$").
// Java '$' also matches before a final line terminator.
std::string removeSuffix(const std::string& name) {
  size_t end = name.size();
  size_t contentEnd = end;
  if (contentEnd > 0 && name[contentEnd - 1] == '\n') {
    --contentEnd;
    if (contentEnd > 0 && name[contentEnd - 1] == '\r') {
      --contentEnd;
    }
  } else if (contentEnd > 0 && name[contentEnd - 1] == '\r') {
    --contentEnd;
  }
  // find the leftmost match (regex search scans from the left): the first "::" followed only by
  // digits up to contentEnd (or end).
  for (size_t start = 0; start + 2 < name.size() + 1; ++start) {
    if (start + 2 > name.size() || name[start] != ':' || name[start + 1] != ':') {
      continue;
    }
    size_t p = start + 2;
    size_t digitsStart = p;
    while (p < name.size() && name[p] >= '0' && name[p] <= '9') {
      ++p;
    }
    if (p == digitsStart) {
      continue;
    }
    if (p == end || p == contentEnd) {
      return name.substr(0, start) + name.substr(p);
    }
  }
  return name;
}
}  // namespace

Packages::Packages(Padstacks* padstackList)
    : padstackList(padstackList), packages(new (jrt::kBoard) jrt::ArrayList<Package*>()) {}

Package* Packages::get(const std::string& name, bool isFront) {
  Package* otherSidePackage = nullptr;
  // match exact case first so packages differing only in case stay distinct
  for (Package* currentPackage : *packages) {
    if (currentPackage != nullptr && currentPackage->name == name) {
      if (currentPackage->isFront == isFront) {
        return currentPackage;
      }
      otherSidePackage = currentPackage;
    }
  }
  if (otherSidePackage != nullptr) {
    return otherSidePackage;
  }
  for (Package* currentPackage : *packages) {
    if (currentPackage != nullptr && jrt::equalsIgnoreCase(currentPackage->name, name)) {
      if (currentPackage->isFront == isFront) {
        return currentPackage;
      }
      otherSidePackage = currentPackage;
    }
  }
  if (otherSidePackage != nullptr) {
    return otherSidePackage;
  }
  // Safe fallback: only if no package with the exact name was found on either side
  std::string baseName = removeSuffix(name);
  if (!jrt::equalsIgnoreCase(baseName, name)) {
    for (Package* currentPackage : *packages) {
      if (currentPackage != nullptr && jrt::equalsIgnoreCase(currentPackage->name, baseName)) {
        if (currentPackage->isFront == isFront) {
          FRLogger::warn("Package '" + name + "' not found; falling back to base package '" +
                         baseName + "'");
          return currentPackage;
        }
        otherSidePackage = currentPackage;
      }
    }
    if (otherSidePackage != nullptr) {
      FRLogger::warn("Package '" + name +
                     "' not found; falling back to other-side base package '" + baseName + "'");
      return otherSidePackage;
    }
  }
  return nullptr;
}

Package* Packages::get(int32_t packageId) {
  Package* result = packages->get(packageId - 1);
  if (result != nullptr && result->id != packageId) {
    FRLogger::warn("Packages.get: inconsistent package ID");
  }
  return result;
}

int32_t Packages::count() { return packages->size(); }

Package* Packages::add(const std::string& name, jrt::Array<Package_Pin*>* pins,
                       jrt::Array<Shape*>* outline, jrt::Array<double>* outlineWidths,
                       jrt::Array<bool>* outlineIsClosed, jrt::Array<Package_Keepout*>* keepouts,
                       jrt::Array<Package_Keepout*>* viaKeepouts,
                       jrt::Array<Package_Keepout*>* placeKeepoutArr, bool isFront) {
  auto* newPackage = new (jrt::kOp) Package(name, packages->size() + 1, pins, outline, outlineWidths,
                                 outlineIsClosed, keepouts, viaKeepouts, placeKeepoutArr, isFront,
                                 this);
  packages->add(newPackage);
  return newPackage;
}

Package* Packages::add(jrt::Array<Package_Pin*>* pins) {
  std::string packageName = "Package#" + jrt::str(packages->size() + 1);

  return add(packageName, pins, nullptr, nullptr, nullptr, new (jrt::kOp) jrt::Array<Package_Keepout*>(0),
             new (jrt::kOp) jrt::Array<Package_Keepout*>(0), new (jrt::kOp) jrt::Array<Package_Keepout*>(0), true);
}

}  // namespace fr
