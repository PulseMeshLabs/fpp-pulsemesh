#pragma once
/* Stand-in for jsoncpp: only what FPP's Plugin.h and the plugin's callbacks
 * touch.  The mirror itself never sees a Json::Value — it takes a std::string
 * and an enum — which is why this stub can be this small. */

#include <string>

namespace Json {

class Value {
public:
    Value() = default;
    bool isMember(const std::string&) const { return false; }
    bool isInt() const { return false; }
    bool isString() const { return false; }
    int asInt() const { return 0; }
    std::string asString() const { return std::string(); }
    const Value& operator[](const std::string&) const { return *this; }
};

} // namespace Json
