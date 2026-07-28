#include "fpp-pch.h"

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <chrono>
#include <utility>
#include <jsoncpp/json/json.h>

#include "Plugin.h"
#include "MultiSync.h"
#include "Warnings.h"

// FPP's plugin HTTP surface exists in two shapes.  Releases up to and
// including 10.x use libhttpserver directly; the drogon migration replaced it
// with a source-compatible shim in the FPP tree's own fpphttp.h, which keeps
// the libhttpserver-style `registerApis(webserver*)` override working and
// routes it into drogon (fpphttp_compat.cpp).  Implementing the older
// signature therefore gives ONE code path across every supported build.
#if __has_include("fpphttp.h")
#include "fpphttp.h"
#else
#include <httpserver.hpp>
#endif

// The two shapes declare render_GET with different top-level cv-qualification
// on the return type, and a mismatch there is not a warning — it is a hard
// error that would take the whole plugin down on one line while building
// cleanly on the other.  Derive the type from the base rather than asserting
// it, so the override is correct by construction on any build.
using PmHttpResponse = decltype(std::declval<httpserver::http_resource&>().render_GET(
    std::declval<const httpserver::http_request&>()));

#include "PendingInsertMirror.h"

// `playlistInserted` was added to FPP's plugin API on 2024-12-07 and first
// shipped in 8.5 — it is absent from 8.0 and 7.0, which this plugin still
// supports.  The Makefile greps the Plugin.h actually being compiled against
// and defines this when it is there, so on an older build the mirror compiles
// away to a member nothing calls and `pending_insert_introspection` is
// honestly false rather than quietly broken.
#ifdef PM_HAVE_PLAYLIST_INSERTED
#define PM_INSERTED_OVERRIDE override
#else
#define PM_INSERTED_OVERRIDE
#endif

// The route lands at /api/plugin-apis/PulseMesh/pending-insert.  It is NOT at
// /api/playlists/pending: apache sends everything under /api/ that is not one
// of a handful of explicitly-proxied prefixes to www/api/index.php, so
// /api/playlists/* is owned by the PHP layer and a plugin cannot reach it.
// `^plugin-apis/(.*)$ -> localhost:32322/$1` (etc/apache2.site) is the one
// rewrite that reaches an fppd-registered route, and it is present on both
// probed lines.
#define PM_MIRROR_PATH "/PulseMesh/pending-insert"

class FPPPulseMeshPlugin : public FPPPlugin, public MultiSyncPlugin, public httpserver::http_resource
{
public:
    FPPPulseMeshPlugin()
        : FPPPlugin("fpp-PulseMesh"),
          m_sockfd(-1),
          m_lastMediaHalfSecond(-1),
          m_sendErrorCount(0),
          m_mirror(wallClockMs())
    {
        LogInfo(VB_PLUGIN, "Initializing PulseMesh Connector Plugin\n");

        MultiSync::INSTANCE.addMultiSyncPlugin(this);

        if (!MultiSync::INSTANCE.isMultiSyncEnabled())
        {
            WarningHolder::AddWarning("PulseMesh Connector Plugin enabled, but MultiSync is not enabled. Please enable MultiSync to use PulseMesh Connector.");
        }

        try {
            initSocket();
        } catch (const std::exception &e) {
            LogErr(VB_PLUGIN, "Initialization failed: " + std::string(e.what()) + "\n");
            m_socketInitialized = false;
        }
    }

    virtual ~FPPPulseMeshPlugin()
    {
        closeSocket();
        MultiSync::INSTANCE.removeMultiSyncPlugin(this);
    }

    // §13.1's pending-insert mirror.  fppd announces an insert here and never
    // mentions it again: the slot is private state, consumed at the next item
    // boundary with no callback, and the failure branch consumes it with the
    // inserted playlist never starting.  See PendingInsertMirror.h.
    void playlistInserted(const std::string& playlist, const int position, int endPosition, bool immediate) PM_INSERTED_OVERRIDE
    {
        {
            std::lock_guard<std::mutex> lock(m_mirrorMutex);
            m_mirror.onInserted(playlist, position, endPosition, immediate, monotonicMs());
        }
        LogDebug(VB_PLUGIN, "PulseMesh mirror: insert announced '%s' [%d..%d] immediate=%d\n",
                 playlist.c_str(), position, endPosition, immediate ? 1 : 0);
    }

    virtual void playlistCallback(const Json::Value& playlist, const std::string& action, const std::string& section, int item) override
    {
        int size = 0;
        std::string name;

        // The mirror is fed BEFORE the size>1 filter below.  An inserted
        // single-item range is trimmed by Playlist::Load, so the inserted
        // playlist reports size 1 — the exact event the mirror needs to see a
        // consumption is the one that filter drops.
        if (playlist.isMember("name") && playlist["name"].isString())
        {
            std::lock_guard<std::mutex> lock(m_mirrorMutex);
            m_mirror.onPlaylistEvent(playlist["name"].asString(),
                                     pulsemesh::actionFromString(action), monotonicMs());
        }

        if (playlist.isMember("size") && playlist["size"].isInt())
        {
            size = playlist["size"].asInt();
        }
        else
        {
            LogErr(VB_PLUGIN, "Playlist JSON does not contain a valid 'size' field.\n");
            return;
        }

        if (playlist.isMember("name") && playlist["name"].isString())
        {
            name = playlist["name"].asString();
        }
        else
        {
            LogErr(VB_PLUGIN, "Playlist JSON does not contain a valid 'name' field.\n");
            return;
        }

        if (size > 1 && (action == "playing" || action == "start"))
        {
            std::string sanitizedName = sanitizeString(name);
            std::string sanitizedSection = sanitizeString(section);
            std::string sanitizedItem = std::to_string(item);

            std::string message = "SendPlaylistUpdate/" + sanitizedName + "/" + sanitizedSection + "/" + sanitizedItem;

            if (!writeToSocket(message))
            {
                LogErr(VB_PLUGIN, "Failed to send SendPlaylistUpdate message.\n");
            }
        }
    }

    virtual void SendMediaOpenPacket(const std::string &filename) override
    {
        std::string message = "SendMediaOpenPacket/" + filename;
        writeToSocket(message);
    }

    virtual void SendMediaSyncStartPacket(const std::string &filename) override
    {
        std::string message = "SendMediaSyncStartPacket/" + filename;
        writeToSocket(message);
    }

    virtual void SendMediaSyncStopPacket(const std::string &filename) override
    {
        std::string message = "SendMediaSyncStopPacket/" + filename;
        writeToSocket(message);
    }

    virtual void SendMediaSyncPacket(const std::string &filename, float seconds) override
    {
        int curTS = static_cast<int>(seconds * 2.0f);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_lastMediaHalfSecond == curTS)
            {
                return;
            }
            m_lastMediaHalfSecond = curTS;
        }
        std::string message = "SendMediaSyncPacket/" + filename + "/" + std::to_string(seconds);
        writeToSocket(message);
    }

    // Overriding the libhttpserver-shaped signature deliberately; on builds
    // where it carries [[deprecated]] the shim behind it is what registers the
    // route, so the deprecation is the supported path rather than a mistake.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    void registerApis(httpserver::webserver* ws) override
    {
#ifdef PM_HAVE_PLAYLIST_INSERTED
        ws->register_resource(PM_MIRROR_PATH, this, false);
        LogInfo(VB_PLUGIN, "PulseMesh mirror registered at /api/plugin-apis" PM_MIRROR_PATH "\n");
#else
        // On a build whose plugin API has no `playlistInserted`, nothing ever
        // feeds the mirror — so serving it would answer `empty` with
        // confidence `exact` on a box where an insert may well be pending.
        // That is a worse answer than no answer: §13.2's repair path acts on
        // `exact`.  Leave the route unregistered so the capability probe's 404
        // reports pending_insert_introspection honestly false.
        (void)ws;
        LogInfo(VB_PLUGIN,
                "PulseMesh mirror unavailable: this FPP build's plugin API has no "
                "playlistInserted callback (added in 8.5)\n");
#endif
    }

    void unregisterApis(httpserver::webserver* ws) override
    {
#ifdef PM_HAVE_PLAYLIST_INSERTED
        // One argument, not two: real libhttpserver declares only
        // `unregister_resource(const std::string&)` (webserver.hpp:98).  FPP's
        // drogon shim adds a two-argument form, so the one-argument call is
        // the only spelling that exists on BOTH.
        ws->unregister_resource(PM_MIRROR_PATH);
#else
        (void)ws;
#endif
    }
#pragma GCC diagnostic pop

    PmHttpResponse render_GET(const httpserver::http_request& req) override
    {
        (void)req;
        std::string body;
        {
            std::lock_guard<std::mutex> lock(m_mirrorMutex);
            const int64_t now = monotonicMs();
            // Deadlines are evaluated here, against the reader's own moment:
            // the machine has no thread and no timer, so a read is exactly as
            // truthful as an event.
            body = pulsemesh::toJson(m_mirror.view(now), now);
        }
        return std::make_shared<httpserver::string_response>(body, 200, "application/json");
    }

private:
    int m_sockfd;
    struct sockaddr_un m_addr;
    int m_lastMediaHalfSecond;
    mutable std::mutex m_mutex;
    mutable std::mutex m_logMutex;
    mutable int m_sendErrorCount;
    bool m_socketInitialized = true;
    mutable std::mutex m_mirrorMutex;
    pulsemesh::PendingInsertMirror m_mirror;

    static int64_t monotonicMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    static int64_t wallClockMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    void initSocket()
    {
        m_sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (m_sockfd < 0)
        {
            throw std::runtime_error("Socket creation error: " + std::string(strerror(errno)));
        }

        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sun_family = AF_UNIX;
        
        std::string socket_path = "/tmp/PULSE";
        if (socket_path.length() >= sizeof(m_addr.sun_path)) {
            throw std::runtime_error("Socket path too long");
        }
        std::copy(socket_path.begin(), socket_path.end(), m_addr.sun_path);
        m_addr.sun_path[socket_path.length()] = '\0';

        if (access(socket_path.c_str(), W_OK) != 0) {
            throw std::runtime_error("Cannot access socket path: " + std::string(strerror(errno)));
        }
    }

    void closeSocket()
    {
        if (m_sockfd >= 0)
        {
            close(m_sockfd);
            m_sockfd = -1;
        }
    }

    bool writeToSocket(const std::string &message) const
    {
        if (m_sockfd < 0)
        {
            LogErr(VB_PLUGIN, "Cannot send message: Socket not connected\n");
            return false;
        }

        ssize_t sent = sendto(m_sockfd, message.c_str(), message.size(), 0, 
                              reinterpret_cast<const struct sockaddr*>(&m_addr), sizeof(m_addr));
        if (sent < 0)
        {
            {
                std::lock_guard<std::mutex> lock(m_logMutex);
                m_sendErrorCount++;
                if (m_sendErrorCount <= 10) {
                    LogErr(VB_PLUGIN, "Failed to send message: " + message + ": " + std::string(strerror(errno)) + "\n");
                } else if (m_sendErrorCount == 11) {
                    LogErr(VB_PLUGIN, "Further send errors suppressed to prevent log flooding.\n");
                }
            }
            return false;
        }
        else if (static_cast<size_t>(sent) < message.size())
        {
            LogWarn(VB_PLUGIN, "Message truncated: sent " + std::to_string(sent) + " of " + std::to_string(message.size()) + " bytes\n");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_logMutex);
            m_sendErrorCount = 0;
        }
        return true;
    }

    std::string sanitizeString(const std::string& input) const
    {
        std::string sanitized = input;
        // Replace any '/' characters to prevent message format issues
        std::replace(sanitized.begin(), sanitized.end(), '/', '_');
        return sanitized;
    }
};

extern "C"
{
    FPPPlugin *createPlugin()
    {
        return new FPPPulseMeshPlugin();
    }
}
