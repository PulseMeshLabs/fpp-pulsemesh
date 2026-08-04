#pragma once
/* Stand-in for FPP's precompiled header: the standard headers it drags in, and
 * the logging macros the plugin uses.  Log calls become (void) expressions so
 * their arguments are still type-checked. */

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

/* Use the real log.h when the header set under test carries one (vmaster
 * does): real macro expansion beats the approximation below, and defining
 * both would redefine under -Werror. */
#if __has_include("log.h")
#include "log.h"
#else

enum LogMask_t { VB_PLUGIN, VB_GENERAL };

inline void pm_stub_log(int, const std::string&) {}
inline void pm_stub_log(int, const char*) {}

#define LogInfo(mask, ...) pm_stub_log((mask), pm_stub_first(__VA_ARGS__))
#define LogWarn(mask, ...) pm_stub_log((mask), pm_stub_first(__VA_ARGS__))
#define LogErr(mask, ...) pm_stub_log((mask), pm_stub_first(__VA_ARGS__))
#define LogDebug(mask, ...) pm_stub_log((mask), pm_stub_first(__VA_ARGS__))

/* FPP's real log macros are printf-style; the plugin passes either a
 * std::string or a format string plus args.  Keep the first argument
 * type-checked and swallow the rest. */
template <typename T, typename... Rest>
inline T pm_stub_first(T first, Rest...) {
    return first;
}

#endif /* !__has_include("log.h") */
