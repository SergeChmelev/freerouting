#include "io/specctra/parser/Net.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "core/library/Package.h"
#include "datastructures/IdentifierType.h"
#include "datastructures/IndentFileWriter.h"
#include "io/CoordinateTransform.h"
#include "io/specctra/parser/WriteScopeParameter.h"
#include "logger/FRLogger.h"
#include "rules/CornerStyle.h"
#include "rules/Net.h"
#include "rules/NetLengthConstraint.h"
#include "rules/NetMeanderConstraint.h"

namespace fr::dsn {

void Net::writeScope(WriteScopeParameter* scopeParameter, fr::Net* net,
                     jrt::Collection<fr::Pin*>* pinList) {
  scopeParameter->file->startScope();
  writeNetId(net, scopeParameter->file, scopeParameter->identifierType);
  // write the pins scope
  scopeParameter->file->startScope();
  scopeParameter->file->write("pins");
  for (fr::Pin* currentPin : *pinList) {
    if (currentPin->containsNet(net->netNumber)) {
      writePin(scopeParameter, currentPin);
    }
  }
  scopeParameter->file->endScope();
  if (net->hasExplicitLengthConstraint() || net->hasExplicitMeanderConstraint()) {
    writeCircuit(net, scopeParameter);
  }
  scopeParameter->file->endScope();
}

void Net::writeCircuit(fr::Net* net, WriteScopeParameter* scopeParameter) {
  fr::NetLengthConstraint* lengthConstraint = net->getExplicitLengthConstraint();
  fr::NetMeanderConstraint* meanderConstraint = net->getExplicitMeanderConstraint();
  bool hasLength = lengthConstraint != nullptr && lengthConstraint->isConstrained();
  bool hasMeander = meanderConstraint != nullptr && meanderConstraint->isConstrained();
  if (!hasLength && !hasMeander) {
    return;
  }
  scopeParameter->file->startScope();
  scopeParameter->file->write("circuit");
  if (hasLength) {
    const double minTraceLength = lengthConstraint->minLength();
    const double maxTraceLength = lengthConstraint->maxLength();
    scopeParameter->file->newLine();
    scopeParameter->file->write("(length ");
    double transformedMaxLength;
    if (maxTraceLength <= 0) {
      transformedMaxLength = -1;
    } else {
      transformedMaxLength = scopeParameter->coordinateTransform->boardToDsn(maxTraceLength);
    }
    scopeParameter->file->write(jrt::str(transformedMaxLength));
    scopeParameter->file->write(" ");
    double transformedMinLength;
    if (minTraceLength <= 0) {
      transformedMinLength = 0;
    } else {
      transformedMinLength = scopeParameter->coordinateTransform->boardToDsn(minTraceLength);
    }
    scopeParameter->file->write(jrt::str(transformedMinLength));
    scopeParameter->file->write(")");
  }
  if (hasMeander) {
    writeMeanderProperties(meanderConstraint, scopeParameter);
  }
  scopeParameter->file->endScope();
}

void Net::writeMeanderProperties(fr::NetMeanderConstraint* meander,
                                 WriteScopeParameter* scopeParameter) {
  if (meander->hasAmplitude()) {
    scopeParameter->file->newLine();
    scopeParameter->file->write("(length_amplitude ");
    double maxAmp = scopeParameter->coordinateTransform->boardToDsn(meander->maxAmplitude());
    double minAmp = scopeParameter->coordinateTransform->boardToDsn(meander->minAmplitude());
    scopeParameter->file->write(jrt::str(maxAmp));
    scopeParameter->file->write(" ");
    scopeParameter->file->write(jrt::str(minAmp));
    scopeParameter->file->write(")");
  }
  if (meander->hasSpacing()) {
    scopeParameter->file->newLine();
    scopeParameter->file->write("(length_gap ");
    double gap = scopeParameter->coordinateTransform->boardToDsn(meander->spacing());
    scopeParameter->file->write(jrt::str(gap));
    scopeParameter->file->write(")");
  }
  // PORT NOTE: cornerStyle() is a non-nullable enum in C++ (Java checks != null).
  {
    scopeParameter->file->newLine();
    scopeParameter->file->write("(corner_style ");
    scopeParameter->file->write(jrt::toLowerCase(fr::CornerStyle_name(meander->cornerStyle())));
    scopeParameter->file->write(")");
  }
  if (meander->radiusPercentage() >= 0) {
    scopeParameter->file->newLine();
    scopeParameter->file->write("(radius ");
    scopeParameter->file->write(jrt::str(meander->radiusPercentage()));
    scopeParameter->file->write(")");
  }
  scopeParameter->file->newLine();
  scopeParameter->file->write("(single_sided ");
  scopeParameter->file->write(meander->singleSided() ? "on" : "off");
  scopeParameter->file->write(")");
}

void Net::writeNetId(fr::Net* net, fr::IndentFileWriter* file,
                     fr::IdentifierType* identifierType) {
  file->write("net ");
  identifierType->write(net->name, file);
  file->write(" ");
  int32_t subnetNumber = net->subnetNumber;
  file->write(jrt::str(subnetNumber));
}

void Net::writePin(WriteScopeParameter* scopeParameter, fr::Pin* pin) {
  fr::Component* currentComponent = scopeParameter->board->components->get(pin->getComponentId());
  if (currentComponent == nullptr) {
    // PORT NOTE: Java dereferences the null component here (NullPointerException).
    throw jrt::NullPointerException("Net.write_scope: component not found");
  }
  fr::Package_Pin* libPin = currentComponent->getPackage()->getPin(pin->getPinIndex());
  if (libPin == nullptr) {
    FRLogger::warn("Net.write_scope:  pin number out of range at '" + currentComponent->name +
                   "'");
    return;
  }
  scopeParameter->file->newLine();
  scopeParameter->identifierType->write(currentComponent->name, scopeParameter->file);
  scopeParameter->file->write("-");
  scopeParameter->identifierType->write(libPin->name, scopeParameter->file);
}

void Net::setPins(jrt::Collection<Net_Pin*>* pinList) {
  this->pinList = new (jrt::kBoard) jrt::TreeSet<Net_Pin*>(pinList);
}

}  // namespace fr::dsn
