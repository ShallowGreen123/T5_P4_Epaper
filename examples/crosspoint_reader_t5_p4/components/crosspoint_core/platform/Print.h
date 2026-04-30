#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t b) = 0;

    virtual size_t write(const uint8_t *buffer, size_t size)
    {
        size_t written = 0;
        while (written < size) {
            written += write(buffer[written]);
        }
        return written;
    }

    size_t write(const char *str)
    {
        return str ? write(reinterpret_cast<const uint8_t *>(str), strlen(str)) : 0;
    }

    size_t print(const char *str) { return write(str); }
};
