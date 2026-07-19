#pragma once

#include "types.hpp"
#include <cstddef>

namespace path {

bool join(char* dest, size_t destCap, const char* dir, const char* name);
bool parent(char* dest, size_t destCap, const char* path);
void fileName(char* dest, size_t destCap, const char* path);
bool hasExtInsensitive(const char* name, const char* extWithDot);
EntryKind kindFromName(const char* name);
void toLowerAscii(char* s);

}  // namespace path
