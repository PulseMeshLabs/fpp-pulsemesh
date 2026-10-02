#pragma once
/*
 * PulseMesh — which of fppd's media position reports reach the connector.
 *
 * fppd hands EVERY position report to plugins (MultiSync::SendMediaSyncPacket
 * calls the plugins before its own half-second network throttle), and its
 * media outputs report on every Process() pass — far more often than the
 * connector needs.  Forwarding one report per half-second bucket keeps the
 * socket quiet, but on its own it forwards the WORST report of every item:
 *
 * FPP 10's GStreamerOut backs its position up by the audio sink's queue delay
 * and clamps the result at 0 (GStreamerOut.cpp, "correcting reported position
 * by N ms of audio sink latency"), so an item's first reports say 0.000 for
 * the ~85 ms before position 0 is actually heard.  The first of them opens
 * bucket 0 and is forwarded; every accurate report in [0, 0.5) then lands in
 * the same bucket and is dropped.  Both connectors anchor the item's timeline
 * on that clamped 0, so phones start ~85 ms early and either jump back when
 * the 0.5 s report arrives (v2's start settle) or play early until the
 * server's next position (v1).
 *
 * So the gate holds an item's reports until the first one past 0 — the first
 * the output has not clamped — forwards that one immediately, whatever its
 * bucket, and only then throttles.  On outputs that never clamp, this costs
 * the reports at exactly 0, i.e. one report interval.
 *
 * Deliberately free of FPP dependencies (host-tested in test/); not
 * thread-safe — the plugin calls it under its own mutex.
 */

#include <string>

namespace pulsemesh {

class MediaSyncGate {
public:
    /* A position report going backwards by more than this is a restart or a
     * seek, not jitter: the item's start is unclamped again. */
    static constexpr float kRestartBackwardSec = 1.0f;

    /* An item started or stopped (SendMediaSyncStartPacket /
     * SendMediaSyncStopPacket): the next report is a new item's first. */
    void reset()
    {
        m_file.clear();
        m_awaitingFirst = true;
        m_lastHalfSecond = -1;
        m_lastForwardedSec = 0.0f;
    }

    /* True when this report should be forwarded to the connector. */
    bool shouldForward(const std::string& file, float seconds)
    {
        if (file != m_file) {
            reset();
            m_file = file;
        } else if (!m_awaitingFirst && seconds + kRestartBackwardSec < m_lastForwardedSec) {
            reset();
            m_file = file;
        }

        int halfSecond = static_cast<int>(seconds * 2.0f);
        if (m_awaitingFirst) {
            if (!(seconds > 0.0f)) {
                return false; // clamped (or not yet moving): not the item's position
            }
            m_awaitingFirst = false;
        } else if (halfSecond == m_lastHalfSecond) {
            return false;
        }
        m_lastHalfSecond = halfSecond;
        m_lastForwardedSec = seconds;
        return true;
    }

private:
    std::string m_file;
    bool m_awaitingFirst = true;
    int m_lastHalfSecond = -1;
    float m_lastForwardedSec = 0.0f;
};

} // namespace pulsemesh
