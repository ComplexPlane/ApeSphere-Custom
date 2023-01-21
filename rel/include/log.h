#pragma once

#include <mkb.h>

namespace log {

void mod_assert(const char* file, s32 line, bool exp);

[[noreturn]] void abort();
[[noreturn]] void abort(const char* format, ...);

}  // namespace log

#define MOD_ASSERT(exp) (log::mod_assert(__FILE__, __LINE__, (exp)))
