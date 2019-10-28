//
// Created by matthew on 10/26/19.
//

#include "stringutils.h"
#include "utils.h"
#include <string.h>

bool startsWith(const char *str, const char *prefix) {
    if(prefix == str)
        return true;
    if(!prefix || !str)
        return false;
    size_t lenPrefix = strlen(prefix);
    size_t lenStr = strlen(str);
    if(lenPrefix > lenStr)
        return false;
    size_t searchLen = MIN(lenPrefix, lenStr);
    for(size_t i = 0; i < searchLen; i++) {
        if(prefix[i] != str[i])
            return false;
    }
    return true;
}

bool endsWith(const char *str, const char *suffix) {
    if(suffix == str)
        return true;
    if(!suffix || !str)
        return false;
    size_t lenSuffix = strlen(suffix);
    size_t lenStr = strlen(str);
    if(lenSuffix > lenStr)
        return false;
    size_t searchLen = MIN(lenSuffix, lenStr);
    for(size_t i = 0; i < searchLen; i++) {
        if(suffix[lenSuffix - i - 1] != str[lenStr - i - 1])
            return false;
    }
    return true;
}