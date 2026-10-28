#include "htmlEntities.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace {

const std::unordered_map<std::string, char> kNamedEntities = {
    {"amp", '&'},
    {"apos", '\''},
    {"gt", '>'},
    {"lt", '<'},
    {"nbsp", ' '},
    {"quot", '"'},
};

char decode_numeric_entity(const std::string &entity)
{
    if (entity.size() < 2 || entity[0] != '#') {
        return '\0';
    }

    char *end = nullptr;
    long value = 0;
    if (entity[1] == 'x' || entity[1] == 'X') {
        value = strtol(entity.c_str() + 2, &end, 16);
    } else {
        value = strtol(entity.c_str() + 1, &end, 10);
    }

    if (end == nullptr || *end != '\0') {
        return '\0';
    }

    if (value == 160) {
        return ' ';
    }
    if (value >= 32 && value <= 126) {
        return static_cast<char>(value);
    }
    return '\0';
}

}  // namespace

std::string replace_html_entities(const std::string &text)
{
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            result.push_back(text[i]);
            continue;
        }

        const size_t end = text.find(';', i + 1);
        if (end == std::string::npos) {
            result.push_back(text[i]);
            continue;
        }

        const std::string entity = text.substr(i + 1, end - i - 1);
        auto named = kNamedEntities.find(entity);
        if (named != kNamedEntities.end()) {
            result.push_back(named->second);
            i = end;
            continue;
        }

        const char numeric = decode_numeric_entity(entity);
        if (numeric != '\0') {
            result.push_back(numeric);
            i = end;
            continue;
        }

        result.append(text, i, end - i + 1);
        i = end;
    }

    return result;
}
