#include "string-utils.hpp"

wxString toWxString(const fs::path& path) {
#ifdef _WIN32
  try {
    return wxString(path.generic_wstring());
  } catch (const std::exception& ex) {
    wxLogError("path conversion failed: %s", ex.what());
    abort();
  }
#else
  const auto utf8 = path.u8string();
  return wxString::FromUTF8Unchecked(
      reinterpret_cast<const char*>(utf8.c_str()));
#endif
}

std::string toStdString(const fs::path& path) {
  const auto utf8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}
