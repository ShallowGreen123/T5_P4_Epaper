#include "HalStorage.h"

#include "esp_log.h"

#include <algorithm>
#include <cstdint>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr char kTag[] = "cp_storage";

std::string parent_path(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash == nullptr || slash == path) {
        return slash == path ? "/" : "";
    }
    return std::string(path, slash - path);
}

bool mkdir_recursive(const char *path)
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    std::string current;
    const std::string input(path);
    size_t start = 0;
    if (!input.empty() && input[0] == '/') {
        current = "/";
        start = 1;
    }

    while (start <= input.size()) {
        const size_t slash = input.find('/', start);
        const std::string part = input.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty()) {
            if (current.size() > 1) {
                current += "/";
            }
            current += part;
            if (::mkdir(current.c_str(), 0775) != 0 && errno != EEXIST) {
                return false;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

bool remove_recursive(const std::string &path)
{
    struct stat st = {};
    if (stat(path.c_str(), &st) != 0) {
        return true;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path.c_str()) == 0;
    }

    DIR *dir = opendir(path.c_str());
    if (dir == nullptr) {
        return false;
    }
    bool ok = true;
    while (dirent *entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        ok = remove_recursive(path + "/" + entry->d_name) && ok;
    }
    closedir(dir);
    return (rmdir(path.c_str()) == 0) && ok;
}

}  // namespace

HalFile::~HalFile()
{
    close();
}

HalFile::HalFile(HalFile &&other) noexcept : file_(other.file_), path_(std::move(other.path_))
{
    other.file_ = nullptr;
}

HalFile &HalFile::operator=(HalFile &&other) noexcept
{
    if (this != &other) {
        close();
        file_ = other.file_;
        path_ = std::move(other.path_);
        other.file_ = nullptr;
    }
    return *this;
}

bool HalFile::open(const char *path, const char *mode)
{
    close();
    file_ = fopen(path, mode);
    path_ = path ? path : "";
    return file_ != nullptr;
}

void HalFile::flush()
{
    if (file_) {
        fflush(file_);
    }
}

size_t HalFile::getName(char *name, size_t len)
{
    if (name == nullptr || len == 0) {
        return 0;
    }
    const size_t slash = path_.find_last_of('/');
    const std::string base = slash == std::string::npos ? path_ : path_.substr(slash + 1);
    snprintf(name, len, "%s", base.c_str());
    return strnlen(name, len);
}

size_t HalFile::size()
{
    if (!file_) {
        return 0;
    }
    const long pos = ftell(file_);
    fseek(file_, 0, SEEK_END);
    const long end = ftell(file_);
    fseek(file_, pos, SEEK_SET);
    return end < 0 ? 0 : static_cast<size_t>(end);
}

size_t HalFile::fileSize()
{
    return size();
}

bool HalFile::seek(size_t pos)
{
    return file_ && fseek(file_, static_cast<long>(pos), SEEK_SET) == 0;
}

bool HalFile::seekCur(int64_t offset)
{
    return file_ && fseek(file_, static_cast<long>(offset), SEEK_CUR) == 0;
}

bool HalFile::seekSet(size_t offset)
{
    return seek(offset);
}

int HalFile::available() const
{
    if (!file_) {
        return 0;
    }
    const long pos = ftell(file_);
    fseek(file_, 0, SEEK_END);
    const long end = ftell(file_);
    fseek(file_, pos, SEEK_SET);
    if (pos < 0 || end < pos) {
        return 0;
    }
    const long remaining = end - pos;
    return remaining > INT32_MAX ? INT32_MAX : static_cast<int>(remaining);
}

size_t HalFile::position() const
{
    if (!file_) {
        return 0;
    }
    const long pos = ftell(file_);
    return pos < 0 ? 0 : static_cast<size_t>(pos);
}

int HalFile::read(void *buf, size_t count)
{
    if (!file_) {
        return 0;
    }
    return static_cast<int>(fread(buf, 1, count, file_));
}

int HalFile::read()
{
    if (!file_) {
        return -1;
    }
    return fgetc(file_);
}

size_t HalFile::write(const void *buf, size_t count)
{
    if (!file_) {
        return 0;
    }
    return fwrite(buf, 1, count, file_);
}

size_t HalFile::write(uint8_t b)
{
    return write(&b, 1);
}

bool HalFile::rename(const char *newPath)
{
    close();
    return ::rename(path_.c_str(), newPath) == 0;
}

bool HalFile::close()
{
    if (!file_) {
        return true;
    }
    const int rc = fclose(file_);
    file_ = nullptr;
    return rc == 0;
}

HalStorage &HalStorage::getInstance()
{
    static HalStorage instance;
    return instance;
}

std::vector<String> HalStorage::listFiles(const char *path, int maxFiles)
{
    std::vector<String> files;
    DIR *dir = opendir(path);
    if (dir == nullptr) {
        return files;
    }
    while (dirent *entry = readdir(dir)) {
        if (files.size() >= static_cast<size_t>(maxFiles)) {
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        files.emplace_back(entry->d_name);
    }
    closedir(dir);
    return files;
}

String HalStorage::readFile(const char *path)
{
    HalFile file;
    if (!openFileForRead("STO", path, file)) {
        return {};
    }
    const size_t len = file.size();
    std::string out(len, '\0');
    if (len > 0) {
        file.read(out.data(), len);
    }
    return String(out);
}

bool HalStorage::readFileToStream(const char *path, Print &out, size_t chunkSize)
{
    HalFile file;
    if (!openFileForRead("STO", path, file)) {
        return false;
    }
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[chunkSize]);
    while (file.available() > 0) {
        const int got = file.read(buffer.get(), chunkSize);
        if (got <= 0 || out.write(buffer.get(), got) != static_cast<size_t>(got)) {
            return false;
        }
    }
    return true;
}

size_t HalStorage::readFileToBuffer(const char *path, char *buffer, size_t bufferSize, size_t maxBytes)
{
    if (buffer == nullptr || bufferSize == 0) {
        return 0;
    }
    HalFile file;
    if (!openFileForRead("STO", path, file)) {
        buffer[0] = '\0';
        return 0;
    }
    const size_t limit = maxBytes == 0 ? bufferSize - 1 : std::min(maxBytes, bufferSize - 1);
    const int got = file.read(buffer, limit);
    const size_t read = got < 0 ? 0 : static_cast<size_t>(got);
    buffer[read] = '\0';
    return read;
}

bool HalStorage::writeFile(const char *path, const String &content)
{
    HalFile file;
    if (!openFileForWrite("STO", path, file)) {
        return false;
    }
    return file.write(reinterpret_cast<const uint8_t *>(content.c_str()), content.length()) == content.length();
}

bool HalStorage::ensureDirectoryExists(const char *path)
{
    return mkdir(path, true);
}

HalFile HalStorage::open(const char *path, const oflag_t oflag)
{
    HalFile file;
    const bool write = (oflag & O_WRONLY_COMPAT) || (oflag & O_RDWR_COMPAT) || (oflag & O_CREAT_COMPAT);
    if (write) {
        openFileForWrite("STO", path, file);
    } else {
        openFileForRead("STO", path, file);
    }
    return file;
}

bool HalStorage::mkdir(const char *path, const bool pFlag)
{
    if (pFlag) {
        return mkdir_recursive(path);
    }
    return ::mkdir(path, 0775) == 0 || errno == EEXIST;
}

bool HalStorage::exists(const char *path)
{
    struct stat st = {};
    return stat(path, &st) == 0;
}

bool HalStorage::remove(const char *path)
{
    return unlink(path) == 0 || errno == ENOENT;
}

bool HalStorage::rename(const char *oldPath, const char *newPath)
{
    return ::rename(oldPath, newPath) == 0;
}

bool HalStorage::rmdir(const char *path)
{
    return ::rmdir(path) == 0 || errno == ENOENT;
}

bool HalStorage::openFileForRead(const char *moduleName, const char *path, HalFile &file)
{
    (void)moduleName;
    if (!file.open(path, "rb")) {
        ESP_LOGW(kTag, "open read failed: %s (%s)", path, strerror(errno));
        return false;
    }
    return true;
}

bool HalStorage::openFileForRead(const char *moduleName, const std::string &path, HalFile &file)
{
    return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char *moduleName, const String &path, HalFile &file)
{
    return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char *moduleName, const char *path, HalFile &file)
{
    (void)moduleName;
    const std::string parent = parent_path(path);
    if (!parent.empty()) {
        mkdir_recursive(parent.c_str());
    }
    if (!file.open(path, "wb+")) {
        ESP_LOGW(kTag, "open write failed: %s (%s)", path, strerror(errno));
        return false;
    }
    return true;
}

bool HalStorage::openFileForWrite(const char *moduleName, const std::string &path, HalFile &file)
{
    return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char *moduleName, const String &path, HalFile &file)
{
    return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char *path)
{
    return remove_recursive(path);
}
