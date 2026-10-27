#pragma once
#include <WString.h>

#include <string>
#include <string_view>

namespace FsHelpers {

std::string normalisePath(const std::string& path);

/**
 * Check if the given filename ends with the specified extension (case-insensitive).
 */
bool checkFileExtension(std::string_view fileName, const char* extension);
inline bool checkFileExtension(const std::string& fileName, const char* extension) {
  return checkFileExtension(std::string_view{fileName}, extension);
}
inline bool checkFileExtension(const String& fileName, const char* extension) {
  return checkFileExtension(std::string_view{fileName.c_str(), fileName.length()}, extension);
}

// Check for either .jpg or .jpeg extension (case-insensitive)
bool hasJpgExtension(std::string_view fileName);
inline bool hasJpgExtension(const std::string& fileName) { return hasJpgExtension(std::string_view{fileName}); }
inline bool hasJpgExtension(const String& fileName) {
  return hasJpgExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .png extension (case-insensitive)
bool hasPngExtension(std::string_view fileName);
inline bool hasPngExtension(const std::string& fileName) { return hasPngExtension(std::string_view{fileName}); }
inline bool hasPngExtension(const String& fileName) {
  return hasPngExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .bmp extension (case-insensitive)
bool hasBmpExtension(std::string_view fileName);
inline bool hasBmpExtension(const std::string& fileName) { return hasBmpExtension(std::string_view{fileName}); }
inline bool hasBmpExtension(const String& fileName) {
  return hasBmpExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .gif extension (case-insensitive)
bool hasGifExtension(std::string_view fileName);
inline bool hasGifExtension(const std::string& fileName) { return hasGifExtension(std::string_view{fileName}); }
inline bool hasGifExtension(const String& fileName) {
  return hasGifExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .epub extension (case-insensitive)
bool hasEpubExtension(std::string_view fileName);
inline bool hasEpubExtension(const std::string& fileName) { return hasEpubExtension(std::string_view{fileName}); }
inline bool hasEpubExtension(const String& fileName) {
  return hasEpubExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for either .xtc or .xtch extension (case-insensitive)
bool hasXtcExtension(std::string_view fileName);
inline bool hasXtcExtension(const std::string& fileName) { return hasXtcExtension(std::string_view{fileName}); }
inline bool hasXtcExtension(const String& fileName) {
  return hasXtcExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .txt extension (case-insensitive)
bool hasTxtExtension(std::string_view fileName);
inline bool hasTxtExtension(const std::string& fileName) { return hasTxtExtension(std::string_view{fileName}); }
inline bool hasTxtExtension(const String& fileName) {
  return hasTxtExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .md extension (case-insensitive)
bool hasMarkdownExtension(std::string_view fileName);
inline bool hasMarkdownExtension(const std::string& fileName) {
  return hasMarkdownExtension(std::string_view{fileName});
}
inline bool hasMarkdownExtension(const String& fileName) {
  return hasMarkdownExtension(std::string_view{fileName.c_str(), fileName.length()});
}

std::string extractFolderPath(const std::string& filePath);

}  // namespace FsHelpers
