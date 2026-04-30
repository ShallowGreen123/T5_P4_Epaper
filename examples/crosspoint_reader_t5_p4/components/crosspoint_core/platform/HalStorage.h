#pragma once

#include "Print.h"
#include "WString.h"

#include <memory>
#include <stdio.h>
#include <string>
#include <vector>

using oflag_t = int;
constexpr oflag_t O_RDONLY_COMPAT = 0x01;
constexpr oflag_t O_WRONLY_COMPAT = 0x02;
constexpr oflag_t O_RDWR_COMPAT = 0x04;
constexpr oflag_t O_CREAT_COMPAT = 0x08;
constexpr oflag_t O_TRUNC_COMPAT = 0x10;

class HalFile : public Print {
public:
    using Print::write;

    HalFile() = default;
    ~HalFile();
    HalFile(HalFile &&other) noexcept;
    HalFile &operator=(HalFile &&other) noexcept;
    HalFile(const HalFile &) = delete;
    HalFile &operator=(const HalFile &) = delete;

    bool open(const char *path, const char *mode);
    void flush();
    size_t getName(char *name, size_t len);
    size_t size();
    size_t fileSize();
    bool seek(size_t pos);
    bool seekCur(int64_t offset);
    bool seekSet(size_t offset);
    int available() const;
    size_t position() const;
    int read(void *buf, size_t count);
    int read();
    size_t write(const void *buf, size_t count);
    size_t write(const uint8_t *buf, size_t count) override { return write(static_cast<const void *>(buf), count); }
    size_t write(uint8_t b) override;
    bool rename(const char *newPath);
    bool isDirectory() const { return false; }
    void rewindDirectory() {}
    bool close();
    HalFile openNextFile() { return HalFile(); }
    bool isOpen() const { return file_ != nullptr; }
    operator bool() const { return isOpen(); }

private:
    FILE *file_ = nullptr;
    std::string path_;
};

using FsFile = HalFile;

class HalStorage {
public:
    bool begin() { return true; }
    bool ready() const { return true; }
    std::vector<String> listFiles(const char *path = "/", int maxFiles = 200);
    String readFile(const char *path);
    bool readFileToStream(const char *path, Print &out, size_t chunkSize = 256);
    size_t readFileToBuffer(const char *path, char *buffer, size_t bufferSize, size_t maxBytes = 0);
    bool writeFile(const char *path, const String &content);
    bool ensureDirectoryExists(const char *path);

    HalFile open(const char *path, const oflag_t oflag = O_RDONLY_COMPAT);
    bool mkdir(const char *path, const bool pFlag = true);
    bool exists(const char *path);
    bool remove(const char *path);
    bool rename(const char *oldPath, const char *newPath);
    bool rmdir(const char *path);

    bool openFileForRead(const char *moduleName, const char *path, HalFile &file);
    bool openFileForRead(const char *moduleName, const std::string &path, HalFile &file);
    bool openFileForRead(const char *moduleName, const String &path, HalFile &file);
    bool openFileForWrite(const char *moduleName, const char *path, HalFile &file);
    bool openFileForWrite(const char *moduleName, const std::string &path, HalFile &file);
    bool openFileForWrite(const char *moduleName, const String &path, HalFile &file);
    bool removeDir(const char *path);

    static HalStorage &getInstance();
};

#define Storage HalStorage::getInstance()
