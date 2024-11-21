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
#include <jsoncpp/json/json.h> // Ensure jsoncpp is included
#include <fstream> // For file operations

#include "Plugin.h"
#include "MultiSync.h"
#include "Warnings.h"

class FPPPulseMeshPlugin : public FPPPlugin, public MultiSyncPlugin
{
public:
    FPPPulseMeshPlugin() 
        : FPPPlugin("fpp-PulseMesh"), 
          m_lastMediaHalfSecond(-1), 
          m_sockfd(-1),
          m_sendErrorCount(0)
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

    // Override PlaylistEventPlugin's playlistCallback
    virtual void playlistCallback(const Json::Value& playlist, const std::string& action, const std::string& section, int item) override
    {
        // Serialize the Json::Value to a string
        Json::StreamWriterBuilder writer;
        writer["indentation"] = ""; // Optional: remove indentation for compact logging
        std::string playlistStr = Json::writeString(writer, playlist);

        // Log additional parameters
        LogInfo(VB_PLUGIN, "Action: " + action + "\n");
        LogInfo(VB_PLUGIN, "Section: " + section + "\n");
        LogInfo(VB_PLUGIN, "Item: " + std::to_string(item) + "\n");

        // Write the playlist to a file in /tmp
        writePlaylistToFile(playlistStr);

        // Extract 'size' and 'name' from the playlist JSON
        int size = 0;
        std::string name;

        if (playlist.isMember("size") && playlist["size"].isInt())
        {
            size = playlist["size"].asInt();
        }
        else
        {
            LogErr(VB_PLUGIN, "Playlist JSON does not contain a valid 'size' field.\n");
            return; // Exit early if 'size' is invalid
        }

        if (playlist.isMember("name") && playlist["name"].isString())
        {
            name = playlist["name"].asString();
        }
        else
        {
            LogErr(VB_PLUGIN, "Playlist JSON does not contain a valid 'name' field.\n");
            return; // Exit early if 'name' is invalid
        }

        // Check the conditions
        if (size > 1 && (action == "playing" || action == "start"))
        {
            // Sanitize inputs to prevent message formatting issues
            std::string sanitizedName = sanitizeString(name);
            std::string sanitizedSection = sanitizeString(section);
            std::string sanitizedItem = std::to_string(item); // 'item' is already an integer

            // Compose the message
            std::string message = "SendPlaylistUpdate/" + sanitizedName + "/" + sanitizedSection + "/" + sanitizedItem;

            // Send the message to the socket
            if (!writeToSocket(message))
            {
                LogErr(VB_PLUGIN, "Failed to send SendPlaylistUpdate message.\n");
            }
            else
            {
                LogInfo(VB_PLUGIN, "SendPlaylistUpdate message sent: " + message + "\n");
            }
        }
    }

    virtual void SendMediaOpenPacket(const std::string &filename) override
    {
        if (!m_socketInitialized) return;
        std::string message = "SendMediaOpenPacket/" + filename;
        writeToSocket(message);
    }

    virtual void SendMediaSyncStartPacket(const std::string &filename) override
    {
        if (!m_socketInitialized) return;
        std::string message = "SendMediaSyncStartPacket/" + filename;
        writeToSocket(message);
    }

    virtual void SendMediaSyncStopPacket(const std::string &filename) override
    {
        if (!m_socketInitialized) return;
        std::string message = "SendMediaSyncStopPacket/" + filename;
        writeToSocket(message);
    }

    virtual void SendMediaSyncPacket(const std::string &filename, float seconds) override
    {
        if (!m_socketInitialized) return;
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

private:
    int m_sockfd;
    struct sockaddr_un m_addr;
    int m_lastMediaHalfSecond;
    mutable std::mutex m_mutex;
    mutable std::mutex m_logMutex;
    mutable int m_sendErrorCount;
    bool m_socketInitialized = true;

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

    void writePlaylistToFile(const std::string& playlistStr) const
    {
        // Define the file path
        const std::string filePath = "/tmp/fpp_pulsemesh_playlist.json";

        // Open the file in append mode
        std::ofstream outFile(filePath, std::ios::out | std::ios::app);
        if (!outFile.is_open())
        {
            std::lock_guard<std::mutex> lock(m_logMutex);
            LogErr(VB_PLUGIN, "Failed to open file for writing playlist: " + filePath + "\n");
            return;
        }

        // Write the playlist string with a timestamp
        outFile << "----- Playlist Callback at " << getCurrentTimestamp() << " -----\n";
        outFile << playlistStr << "\n\n";
        outFile.close();

        // Optionally, log that the playlist was written to the file
        LogInfo(VB_PLUGIN, "Playlist written to " + filePath + "\n");
    }

    std::string getCurrentTimestamp() const
    {
        // Get current time
        std::time_t now = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        return std::string(buf);
    }

    // Helper method to sanitize strings for socket messages
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
