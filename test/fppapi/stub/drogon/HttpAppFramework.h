#pragma once
/* Stand-in for drogon's HttpAppFramework: only what the plugin touches.
 * registerHandler type-checks the handler against the signature drogon
 * actually dispatches, so a lambda real drogon could not invoke fails here. */

#include "HttpRequest.h"
#include "HttpResponse.h"

#include <functional>
#include <string>
#include <type_traits>
#include <vector>

/* The real header drags in trantor's logger, which defines LOG_WARN/LOG_INFO/
 * LOG_DEBUG as macros colliding with log.h's LogLevel enumerators; fpphttp.h
 * undefines them.  Reproduce them so an include-order mistake that lets them
 * survive breaks the vmaster build the same way it would break the box. */
struct PmTrantorMacroLeak {};
#define LOG_WARN PmTrantorMacroLeak {}
#define LOG_INFO PmTrantorMacroLeak {}
#define LOG_DEBUG PmTrantorMacroLeak {}

namespace drogon {

namespace internal {
    /* Real drogon's constraint is constructible from an HttpMethod or a filter
     * name; the plugin only ever passes methods. */
    class HttpConstraint {
    public:
        HttpConstraint(HttpMethod) {}
    };
} // namespace internal

class HttpAppFramework {
public:
    template <typename FUNCTION>
    void registerHandler(const std::string&, FUNCTION&& function,
                         const std::vector<internal::HttpConstraint>& = {},
                         const std::string& = "") {
        static_assert(
            std::is_invocable_v<FUNCTION, const HttpRequestPtr&,
                                std::function<void(const HttpResponsePtr&)>&&>,
            "handler is not invocable with the (request, callback) shape drogon "
            "dispatches");
        (void)function;
    }
};

inline HttpAppFramework& app() {
    static HttpAppFramework instance;
    return instance;
}

} // namespace drogon
