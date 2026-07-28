#pragma once
/* Stand-in for FPP's MultiSync surface: only the members the plugin touches. */

#include <string>

class MultiSyncPlugin {
public:
    virtual ~MultiSyncPlugin() = default;
    virtual void SendMediaOpenPacket(const std::string& filename) {}
    virtual void SendMediaSyncStartPacket(const std::string& filename) {}
    virtual void SendMediaSyncStopPacket(const std::string& filename) {}
    virtual void SendMediaSyncPacket(const std::string& filename, float seconds) {}
};

class MultiSync {
public:
    static MultiSync INSTANCE;
    void addMultiSyncPlugin(MultiSyncPlugin*) {}
    void removeMultiSyncPlugin(MultiSyncPlugin*) {}
    bool isMultiSyncEnabled() { return true; }
};
