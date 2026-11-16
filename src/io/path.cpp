// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/io/path.hpp"

#include <cstdint>
#include <string>

#if defined(_WIN32)
// Win32 conversion for the local ANSI code page (GBK on Chinese systems).
// Declared here instead of including windows.h to avoid its macro pollution.
extern "C" {
int MultiByteToWideChar(unsigned int code_page, unsigned long flags,
                        const char* multi_byte, int multi_byte_size,
                        wchar_t* wide, int wide_size);
}
#endif

namespace figtools::io {
namespace {

#if defined(_WIN32)
constexpr unsigned int kCpAcp = 0;  // CP_ACP: active ANSI code page

std::wstring ansi_to_wide(const std::string& text) {
  const int size = MultiByteToWideChar(kCpAcp, 0UL, text.c_str(),
                                       static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) {
    return {};
  }
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(kCpAcp, 0UL, text.c_str(), static_cast<int>(text.size()),
                      result.data(), size);
  return result;
}

// Decode UTF-8 bytes into UTF-16 code units. Hand-rolled instead of
// wstring_convert (deprecated) or windows.h (would leak macros into the core).
std::wstring utf8_to_wide(const std::string& text) {
  std::wstring result;
  result.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i]);
    std::uint32_t code = 0;
    int extra = 0;
    if (lead < 0x80) {
      code = lead;
    } else if ((lead & 0xE0) == 0xC0) {
      code = lead & 0x1F;
      extra = 1;
    } else if ((lead & 0xF0) == 0xE0) {
      code = lead & 0x0F;
      extra = 2;
    } else if ((lead & 0xF8) == 0xF0) {
      code = lead & 0x07;
      extra = 3;
    } else {
      result.push_back(static_cast<wchar_t>(lead));
      ++i;
      continue;
    }
    ++i;
    for (int n = 0; n < extra && i < text.size(); ++n, ++i) {
      const auto byte = static_cast<unsigned char>(text[i]);
      if ((byte & 0xC0) != 0x80) {
        break;
      }
      code = (code << 6) | (byte & 0x3F);
    }
    if (code <= 0xFFFF) {
      result.push_back(static_cast<wchar_t>(code));
    } else {
      code -= 0x10000;
      result.push_back(static_cast<wchar_t>(0xD800 + (code >> 10)));
      result.push_back(static_cast<wchar_t>(0xDC00 + (code & 0x3FF)));
    }
  }
  return result;
}

std::string wide_to_utf8(const std::wstring& text) {
  std::string result;
  for (std::size_t i = 0; i < text.size(); ++i) {
    std::uint32_t code = static_cast<std::uint32_t>(text[i]);
    if (code >= 0xD800 && code <= 0xDBFF && i + 1 < text.size()) {
      const std::uint32_t low = static_cast<std::uint32_t>(text[i + 1]);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        code = 0x10000U + ((code - 0xD800U) << 10) + (low - 0xDC00U);
        ++i;
      }
    }
    if (code < 0x80) {
      result.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      result.push_back(static_cast<char>(0xC0U | (code >> 6)));
      result.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else if (code < 0x10000) {
      result.push_back(static_cast<char>(0xE0U | (code >> 12)));
      result.push_back(static_cast<char>(0x80U | ((code >> 6) & 0x3FU)));
      result.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    } else {
      result.push_back(static_cast<char>(0xF0U | (code >> 18)));
      result.push_back(static_cast<char>(0x80U | ((code >> 12) & 0x3FU)));
      result.push_back(static_cast<char>(0x80U | ((code >> 6) & 0x3FU)));
      result.push_back(static_cast<char>(0x80U | (code & 0x3FU)));
    }
  }
  return result;
}
#endif

}  // namespace

#if defined(_WIN32)
// Two sources of narrow paths exist on Windows:
//   * GUI / JSON / Qt  -> UTF-8 bytes
//   * C runtime argv   -> the local ANSI code page (GBK on Chinese systems)
// Detect valid UTF-8 (with at least one non-ASCII byte) and decode it
// explicitly; otherwise hand the bytes to the standard library, which uses
// the active code page. This makes both GUI and CLI paths work with Chinese
// filenames without callers having to know which encoding they hold.
bool looks_like_utf8(const std::string& text) {
  bool non_ascii = false;
  std::size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i]);
    if (lead < 0x80) {
      ++i;
      continue;
    }
    non_ascii = true;
    int extra = 0;
    if ((lead & 0xE0) == 0xC0) {
      extra = 1;
    } else if ((lead & 0xF0) == 0xE0) {
      extra = 2;
    } else if ((lead & 0xF8) == 0xF0) {
      extra = 3;
    } else {
      return false;
    }
    if (i + static_cast<std::size_t>(extra) >= text.size()) {
      return false;
    }
    for (int n = 1; n <= extra; ++n) {
      if ((static_cast<unsigned char>(text[i + static_cast<std::size_t>(n)]) & 0xC0) != 0x80) {
        return false;
      }
    }
    i += static_cast<std::size_t>(extra) + 1U;
  }
  return non_ascii;
}
#endif

std::filesystem::path native_path(const std::string& utf8) {
#if defined(_WIN32)
  if (looks_like_utf8(utf8)) {
    return std::filesystem::path(utf8_to_wide(utf8));
  }
  // GBK argv: convert through the active ANSI code page explicitly. Passing
  // GBK bytes straight to std::filesystem::path would throw "Illegal byte
  // sequence" because libstdc++ transcodes narrow paths using the C locale.
  return std::filesystem::path(ansi_to_wide(utf8));
#else
  return std::filesystem::path(utf8);
#endif
}

std::string utf8_path(const std::filesystem::path& path) {
#if defined(_WIN32)
  return wide_to_utf8(path.native());
#else
  return path.string();
#endif
}

}  // namespace figtools::io
