#include "core/facade/HeadlessRoutingRequest.h"

#include <iterator>
#include <vector>

#include "core/facade/CancellationToken.h"
#include "core/facade/HeadlessRoutingProgressListener.h"
#include "settings/RouterSettings.h"

namespace fr {

namespace {

// String.getBytes(StandardCharsets.UTF_8): std::string already holds UTF-8.
jrt::Array<int8_t>* utf8Bytes(const std::string& s) {
  auto* result = new jrt::Array<int8_t>(static_cast<int32_t>(s.size()));
  for (std::size_t i = 0; i < s.size(); i++) {
    (*result)[static_cast<int32_t>(i)] = static_cast<int8_t>(s[i]);
  }
  return result;
}

bool isBlank(const std::optional<std::string>& s) {
  return !s.has_value() || jrt::strip(*s).empty();
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------------------------

HeadlessRoutingRequest_Builder::HeadlessRoutingRequest_Builder() {
  this->cancellationToken_ = CancellationToken::none();
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::dsnBytes(
    jrt::Array<int8_t>* dsnBytes) {
  this->dsnBytes_ = dsnBytes;
  return this;
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::dsnContent(
    const std::optional<std::string>& dsnContent) {
  if (dsnContent.has_value()) {
    this->dsnBytes_ = utf8Bytes(*dsnContent);
  }
  return this;
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::dsnStream(std::istream* stream) {
  if (stream != nullptr) {
    std::vector<char> bytes((std::istreambuf_iterator<char>(*stream)),
                            std::istreambuf_iterator<char>());
    if (stream->bad()) {
      throw jrt::IOException("error reading the DSN stream");
    }
    auto* data = new jrt::Array<int8_t>(static_cast<int32_t>(bytes.size()));
    for (std::size_t i = 0; i < bytes.size(); i++) {
      (*data)[static_cast<int32_t>(i)] = static_cast<int8_t>(bytes[i]);
    }
    this->dsnBytes_ = data;
  }
  return this;
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::designName(
    const std::optional<std::string>& designName) {
  this->designName_ = designName;
  return this;
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::rulesBytes(
    jrt::Array<int8_t>* rulesBytes) {
  this->rulesBytes_ = rulesBytes;
  return this;
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::rulesContent(
    const std::optional<std::string>& rulesContent) {
  if (rulesContent.has_value()) {
    this->rulesBytes_ = utf8Bytes(*rulesContent);
  }
  return this;
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::rulesName(
    const std::optional<std::string>& rulesName) {
  this->rulesName_ = rulesName;
  return this;
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::settings(
    RouterSettings* settings) {
  this->settings_ = settings;
  return this;
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::cancellationToken(
    CancellationToken* cancellationToken) {
  this->cancellationToken_ = cancellationToken;
  return this;
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::progressListener(
    HeadlessRoutingProgressListener* progressListener) {
  this->progressListener_ = progressListener;
  return this;
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::runDrc(bool runDrc) {
  this->runDrc_ = runDrc;
  return this;
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest_Builder::computeSes(bool computeSes) {
  this->computeSes_ = computeSes;
  return this;
}

HeadlessRoutingRequest* HeadlessRoutingRequest_Builder::build() {
  auto* request = new HeadlessRoutingRequest(dsnBytes_, designName_, rulesBytes_, rulesName_,
                                             settings_, cancellationToken_, progressListener_,
                                             runDrc_, computeSes_);
  request->boardObserver = boardObserver_;
  request->presetBoard = presetBoard_;
  return request;
}

// ---------------------------------------------------------------------------------------------
// HeadlessRoutingRequest
// ---------------------------------------------------------------------------------------------

HeadlessRoutingRequest::HeadlessRoutingRequest(
    jrt::Array<int8_t>* dsnBytes, const std::optional<std::string>& designName,
    jrt::Array<int8_t>* rulesBytes, const std::optional<std::string>& rulesName,
    RouterSettings* settings, CancellationToken* cancellationToken,
    HeadlessRoutingProgressListener* progressListener, bool runDrc, bool computeSes) {
  if (dsnBytes == nullptr || dsnBytes->length == 0) {
    throw jrt::IllegalArgumentException("dsnBytes must not be null or empty");
  }
  this->dsnBytes_ = dsnBytes->clone();
  this->rulesBytes_ = rulesBytes != nullptr ? rulesBytes->clone() : nullptr;
  this->settings_ = settings != nullptr ? settings->clone() : nullptr;
  this->designName_ = isBlank(designName) ? std::optional<std::string>("design.dsn") : designName;
  this->cancellationToken_ =
      cancellationToken != nullptr ? cancellationToken : CancellationToken::none();
  this->rulesName_ = rulesName;
  this->progressListener_ = progressListener;
  this->runDrc_ = runDrc;
  this->computeSes_ = computeSes;
}

jrt::Array<int8_t>* HeadlessRoutingRequest::dsnBytes() { return dsnBytes_->clone(); }

jrt::Array<int8_t>* HeadlessRoutingRequest::rulesBytes() {
  return rulesBytes_ != nullptr ? rulesBytes_->clone() : nullptr;
}

RouterSettings* HeadlessRoutingRequest::settings() {
  return settings_ != nullptr ? settings_->clone() : nullptr;
}

bool HeadlessRoutingRequest::equals(jrt::Object* o) {
  if (this == o) {
    return true;
  }
  auto* that = jrt::fast_cast<HeadlessRoutingRequest>(o);
  if (that == nullptr) {
    return false;
  }
  return runDrc_ == that->runDrc_ && computeSes_ == that->computeSes_ &&
         jrt::Arrays::equals(dsnBytes_, that->dsnBytes_) && designName_ == that->designName_ &&
         jrt::Arrays::equals(rulesBytes_, that->rulesBytes_) && rulesName_ == that->rulesName_ &&
         jrt::equalsOf<RouterSettings*>(settings_, that->settings_) &&
         jrt::equalsOf<CancellationToken*>(cancellationToken_, that->cancellationToken_) &&
         jrt::equalsOf<HeadlessRoutingProgressListener*>(progressListener_,
                                                         that->progressListener_);
}

int32_t HeadlessRoutingRequest::hashCode() {
  int32_t result = jrt::hashAll(designName_, rulesName_, settings_, cancellationToken_,
                                progressListener_, runDrc_, computeSes_);
  result = jrt::hashCombine(result, jrt::Arrays::hashCode(dsnBytes_));
  result = jrt::hashCombine(result, jrt::Arrays::hashCode(rulesBytes_));
  return result;
}

std::string HeadlessRoutingRequest::toString() {
  return "HeadlessRoutingRequest[dsnBytes=" + jrt::str(static_cast<jrt::Object*>(dsnBytes_)) +
         ", designName=" + jrt::str(designName_) +
         ", rulesBytes=" + jrt::str(static_cast<jrt::Object*>(rulesBytes_)) +
         ", rulesName=" + jrt::str(rulesName_) +
         ", settings=" + jrt::str(static_cast<jrt::Object*>(settings_)) +
         ", cancellationToken=" + jrt::str(static_cast<jrt::Object*>(cancellationToken_)) +
         ", progressListener=" + jrt::str(static_cast<jrt::Object*>(progressListener_)) +
         ", runDrc=" + jrt::str(runDrc_) + ", computeSes=" + jrt::str(computeSes_) + "]";
}

HeadlessRoutingRequest_Builder* HeadlessRoutingRequest::builder() {
  return new HeadlessRoutingRequest_Builder();
}

HeadlessRoutingRequest* HeadlessRoutingRequest::of(jrt::Array<int8_t>* dsnBytes) {
  return builder()->dsnBytes(dsnBytes)->build();
}

HeadlessRoutingRequest* HeadlessRoutingRequest::of(const std::string& dsnContent) {
  return builder()->dsnContent(dsnContent)->build();
}

HeadlessRoutingRequest* HeadlessRoutingRequest::of(std::istream& dsnStream) {
  return builder()->dsnStream(&dsnStream)->build();
}

}  // namespace fr
