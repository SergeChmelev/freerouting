#include "util/FileText.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace fr {

std::string FileText::readString(const std::string& path) {
  std::error_code ec;
  std::filesystem::file_status status = std::filesystem::status(path, ec);
  if (!std::filesystem::exists(status)) {
    throw NoSuchFileException(path);
  }
  if (std::filesystem::is_directory(status)) {
    throw jrt::IOException("Is a directory");
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw jrt::IOException(path + ": cannot be opened");
  }
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) {
    throw jrt::IOException(path + ": read error");
  }
  return data;
}

std::string FileText::fileName(const std::string& path) {
  std::string p = pathString(path);
  auto pos = p.find_last_of("/\\");
  return pos == std::string::npos ? p : p.substr(pos + 1);
}

std::string FileText::pathString(const std::string& path) {
  std::string result;
  for (char c : path) {
    if (c == '/' && !result.empty() && result.back() == '/') {
      continue;
    }
    result += c;
  }
  if (result.size() > 1 && result.back() == '/') {
    result.pop_back();
  }
  return result;
}

}  // namespace fr
