#pragma once
/* Stand-in for FPP's WarningHolder: only the members the plugin touches. */

#include <string>

class WarningHolder {
public:
    static void AddWarning(const std::string&) {}
    static void AddWarningTimeout(const std::string&, int) {}
};
