#pragma once
/*
 * PulseMesh — the pending-insert mirror (plan-16 §13.1).
 *
 * fppd's pending insert is PRIVATE state: `m_insertedPlaylist` /
 * `m_insertedPlaylistPosition` / `m_insertedPlaylistEndPosition`
 * (Playlist.cpp:1119-1121), exposed by NO status field and clearable by NO
 * API.  The one stock hook is the `playlistInserted` plugin callback fired at
 * insert time (Playlist.cpp:1115 / :1126) — and that callback is an EVENT, not
 * pending-insert state, so this is a defined state machine rather than a
 * one-callback latch.  Three source paths refute the latch:
 *
 *   1. the callback fires BEFORE fppd's idle check, so an idle player plays
 *      the insert immediately and nothing is ever pending (:1113-1118);
 *   2. boundary consumption clears the slot with NO corresponding callback
 *      (`SwitchToInsertedPlaylist`, :1012);
 *   3. the failure branch consumes the slot with the inserted playlist never
 *      starting (:1013-1019).
 *
 * A latch reports all three as a stale "pending" forever, and a stale
 * "pending" is the one answer §13.2's repair path must never receive: acting
 * on it DOUBLE-INSERTS, which is worse than the wrong item it repairs.  Hence
 * `mirror_confidence` — the repair acts on `exact` only.
 *
 * This header is deliberately free of every FPP and jsoncpp dependency so the
 * machine can be compiled and tested on a host that has no FPP tree.  All
 * clock input arrives as a parameter; nothing here reads a clock, a file or a
 * socket.
 */

#include <cstdint>
#include <string>

namespace pulsemesh {

/* fppd's own vocabulary, as observed at the callback boundary. */
enum class PlaylistAction {
    Start,     // Playlist.cpp:638 — emitted only when the object was NOT already
               // playing.  A freshly constructed inserted Playlist is
               // m_currentState("idle") at construction (Playlist.cpp:70), so an
               // insert's own start is ALWAYS "start", never "playing".
    Playing,   // Playlist.cpp:636/738/974 — an object that was already playing.
               // Naming the slot playlist, this is the PARENT moving on, i.e. a
               // boundary passed WITHOUT the insert starting.
    Stop,      // Playlist.cpp:1052
    QueryNext, // Playlist.cpp:825 — fires BEFORE SwitchToInsertedPlaylist is even
               // attempted (:850).  Never a boundary-passing event; treating it
               // as one resolves consumed_unresolved before the switch tried.
    Other,
};

enum class MirrorState {
    Empty,
    Announced,     // inside the idle-settle window; pending's rules already apply
    Pending,       // settled: the slot verifiably sits until the next boundary
    AwaitingStart, // immediate-while-playing; resolves by start or timeout only
};

enum class Confidence {
    Exact,
    Unresolved,
};

enum class Resolution {
    None,
    ImmediatePlayed,    // the idle branch's synchronous Play (:1116 / :1127)
    Consumed,           // SwitchToInsertedPlaylist's success path
    Superseded,         // a later insert overwrote the slot (:1119 is unconditional)
    ConsumedUnresolved, // a boundary passed without the insert's start (:1013-1019)
    Unresolved,         // awaiting_start timed out: consumed-but-failed and
                        // stuck-mid-pause are indistinguishable from events alone
};

struct Announcement {
    std::string playlist;
    /* fppd-internal, ZERO-based.  PlaylistCommands.cpp:283/310 has already
     * applied `start > 0 ? start - 1 : -1`, so this is the same index base as
     * the connector's `sequence_ref.index` and needs no conversion to compare.
     * An arg of "0" — §22.1's named cliff, the whole-playlist insert — arrives
     * here as -1 and is therefore visible in the mirror rather than silent. */
    int position = -1;
    int endPosition = -1;
    bool immediate = false;
    int64_t announcedAtMs = 0;
    uint64_t seq = 0;
};

struct ResolvedAnnouncement {
    Announcement announcement;
    Resolution resolution = Resolution::None;
    int64_t resolvedAtMs = 0;
};

struct MirrorView {
    MirrorState state = MirrorState::Empty;
    Confidence confidence = Confidence::Exact;
    bool hasPending = false;
    Announcement pending;
    bool hasLastResolution = false;
    ResolvedAnnouncement lastResolution;
    uint64_t announcementSeq = 0;
    int64_t epochMs = 0;
};

struct MirrorConfig {
    /* The idle branch calls Play() synchronously on the same thread under the
     * same recursive mutex, so the start callback that proves it lands within
     * microseconds of the announcement.  This window is many orders of
     * magnitude of headroom; a start arriving after it is read as a boundary
     * consumption instead, which is the correct reading of a start that late. */
    int64_t idleSettleMs = 250;
    /* Generous multiples of fppd's process tick. */
    int64_t immediateStartTimeoutMs = 2000;
};

class PendingInsertMirror {
public:
    explicit PendingInsertMirror(int64_t epochMs, MirrorConfig config = MirrorConfig())
        : m_config(config), m_epochMs(epochMs) {}

    /* From FPPPlugins::PlaylistEventPlugin::playlistInserted.  fppd's slot is a
     * single field both insert commands overwrite UNCONDITIONALLY
     * (Playlist.cpp:1119 and :1131), so a second announcement supersedes the
     * first here exactly as it does there. */
    void onInserted(const std::string& playlist, int position, int endPosition,
                    bool immediate, int64_t nowMs) {
        settle(nowMs);
        if (m_state != MirrorState::Empty) {
            resolve(Resolution::Superseded, nowMs);
        }
        m_pending = Announcement{playlist, position, endPosition, immediate, nowMs, ++m_seq};
        m_hasPending = true;
        if (immediate) {
            /* Never rests at pending: fppd pauses the current item synchronously
             * and Process() consumes the slot at its next tick (:812).  If the
             * inserted playlist fails to start, the silent-cleanup branch leaves
             * the original item PAUSED — and a paused player emits no boundary
             * events, so a mirror resting at pending/exact would be stale
             * forever and §13.2's replace-repair would double-insert. */
            m_state = MirrorState::AwaitingStart;
            m_deadlineMs = nowMs + m_config.immediateStartTimeoutMs;
        } else {
            m_state = MirrorState::Announced;
            m_deadlineMs = nowMs + m_config.idleSettleMs;
        }
    }

    /* From FPPPlugins::PlaylistEventPlugin::playlistCallback.  `playlist` is
     * GetInfo()["name"]. */
    void onPlaylistEvent(const std::string& playlist, PlaylistAction action, int64_t nowMs) {
        const bool namesSlot = m_hasPending && playlist == m_pending.playlist;

        if (m_state == MirrorState::AwaitingStart) {
            /* Resolves exactly two ways: the start, or the timeout.  Nothing
             * else is evidence — the failure branch is EVENTLESS by
             * construction, so an unrelated event here means only that
             * something else happened, never that the insert did or did not. */
            if (namesSlot && action == PlaylistAction::Start) {
                resolve(Resolution::ImmediatePlayed, nowMs);
            } else {
                settle(nowMs);
            }
            return;
        }

        if (m_state == MirrorState::Announced || m_state == MirrorState::Pending) {
            if (namesSlot && action == PlaylistAction::Start) {
                /* Inside the settle window this is the idle branch's synchronous
                 * Play; after it, it is SwitchToInsertedPlaylist's success path.
                 * Both mean the insert played; they differ only in WHEN. */
                resolve(m_state == MirrorState::Announced ? Resolution::ImmediatePlayed
                                                          : Resolution::Consumed,
                        nowMs);
                return;
            }
            if (isBoundaryPassing(action)) {
                /* A boundary passed without the insert's own start: the switch's
                 * silent failure branch (:1013-1019) cleared the slot and the
                 * inserted playlist never played.  Note a `playing` NAMING the
                 * slot playlist lands here too, and must: that is the parent
                 * moving on (an object already playing emits "playing", not
                 * "start"), which is the §13.2 case of inserting the playlist
                 * that is already running. */
                resolve(Resolution::ConsumedUnresolved, nowMs);
                return;
            }
        }
        settle(nowMs);
    }

    /* Pure: deadlines are evaluated against the caller's clock, so a read is
     * as truthful as an event.  Nothing in this machine expires on its own —
     * there is no thread and no timer to trust. */
    MirrorView view(int64_t nowMs) {
        settle(nowMs);
        MirrorView v;
        v.state = m_state;
        v.hasPending = m_hasPending;
        v.pending = m_pending;
        v.hasLastResolution = m_lastResolution.resolution != Resolution::None;
        v.lastResolution = m_lastResolution;
        v.announcementSeq = m_seq;
        v.epochMs = m_epochMs;
        v.confidence = confidence();
        return v;
    }

private:
    void settle(int64_t nowMs) {
        if (m_state == MirrorState::Announced && nowMs >= m_deadlineMs) {
            /* The idle-branch Play did not happen, so the slot verifiably sits
             * until the current item's natural boundary — which always emits
             * events.  A truthful rest state. */
            m_state = MirrorState::Pending;
        } else if (m_state == MirrorState::AwaitingStart && nowMs >= m_deadlineMs) {
            resolve(Resolution::Unresolved, nowMs);
        }
    }

    void resolve(Resolution resolution, int64_t nowMs) {
        m_lastResolution = ResolvedAnnouncement{m_pending, resolution, nowMs};
        m_hasPending = false;
        m_pending = Announcement{};
        m_state = MirrorState::Empty;
        m_deadlineMs = 0;
    }

    /* The ONE place that decides what counts as a boundary having passed.
     * QueryNext is excluded here and nowhere else: it fires at :825 when an
     * entry finished, BEFORE the boundary's SwitchToInsertedPlaylist is even
     * attempted (:850), so it proves nothing about the slot — but an earlier
     * draft ALSO short-circuited it at the top of onPlaylistEvent, and that
     * second copy was dead: a probe deleting it changed no output, because
     * this list already excluded it.  A check no test misses when it is
     * deleted is not a check; it is where the two copies drift apart. */
    static bool isBoundaryPassing(PlaylistAction action) {
        return action == PlaylistAction::Start || action == PlaylistAction::Playing ||
               action == PlaylistAction::Stop;
    }

    Confidence confidence() const {
        switch (m_state) {
        case MirrorState::Announced:
        case MirrorState::AwaitingStart:
            /* Still deciding.  §13.2's repair declines here, which costs at most
             * the settle window and never costs a double insert. */
            return Confidence::Unresolved;
        case MirrorState::Pending:
            return Confidence::Exact;
        case MirrorState::Empty:
            break;
        }
        switch (m_lastResolution.resolution) {
        case Resolution::ConsumedUnresolved:
        case Resolution::Unresolved:
            /* The slot is empty either way — SwitchToInsertedPlaylist clears it
             * before it checks IsPlaying — but whether the announced item PLAYED
             * is unknown, and that is what the repair path would be deciding on.
             * Downgrade to the declared-honesty path. */
            return Confidence::Unresolved;
        default:
            return Confidence::Exact;
        }
    }

    MirrorConfig m_config;
    int64_t m_epochMs = 0;
    MirrorState m_state = MirrorState::Empty;
    Announcement m_pending;
    bool m_hasPending = false;
    ResolvedAnnouncement m_lastResolution;
    uint64_t m_seq = 0;
    int64_t m_deadlineMs = 0;
};

inline const char* toString(MirrorState s) {
    switch (s) {
    case MirrorState::Empty: return "empty";
    case MirrorState::Announced: return "announced";
    case MirrorState::Pending: return "pending";
    case MirrorState::AwaitingStart: return "awaiting_start";
    }
    return "empty";
}

inline const char* toString(Confidence c) {
    return c == Confidence::Exact ? "exact" : "unresolved";
}

inline const char* toString(Resolution r) {
    switch (r) {
    case Resolution::None: return "none";
    case Resolution::ImmediatePlayed: return "immediate_played";
    case Resolution::Consumed: return "consumed";
    case Resolution::Superseded: return "superseded";
    case Resolution::ConsumedUnresolved: return "consumed_unresolved";
    case Resolution::Unresolved: return "unresolved";
    }
    return "none";
}

/* fppd's action strings, mapped once. */
inline PlaylistAction actionFromString(const std::string& action) {
    if (action == "start") return PlaylistAction::Start;
    if (action == "playing") return PlaylistAction::Playing;
    if (action == "stop") return PlaylistAction::Stop;
    if (action == "query_next") return PlaylistAction::QueryNext;
    return PlaylistAction::Other;
}

/* Minimal JSON string escaping — playlist names are operator-chosen and reach
 * this file verbatim.  Written here rather than borrowed from jsoncpp so the
 * machine and its serialization stay host-testable together. */
inline std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                static const char* hex = "0123456789abcdef";
                out += "\\u00";
                out += hex[(c >> 4) & 0xF];
                out += hex[c & 0xF];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

inline std::string announcementJson(const Announcement& a) {
    std::string s = "{\"playlist\":\"" + jsonEscape(a.playlist) + "\"";
    s += ",\"position\":" + std::to_string(a.position);
    s += ",\"end_position\":" + std::to_string(a.endPosition);
    s += ",\"immediate\":" + std::string(a.immediate ? "true" : "false");
    s += ",\"announced_at_ms\":" + std::to_string(a.announcedAtMs);
    s += ",\"seq\":" + std::to_string(a.seq);
    s += "}";
    return s;
}

/* Two clocks, each for what it is good at.  Every timestamp inside the mirror
 * (`announced_at_ms`, `resolved_at_ms`, `uptime_ms`) is MONOTONIC: deadlines
 * must not jump when NTP steps the wall clock, which on a Pi that boots
 * without a battery-backed RTC is an ordinary event rather than an exotic one.
 * `mirror_epoch_ms` is the wall clock at construction — it names the process,
 * so a changed epoch is proof that an older answer is VOID rather than stale,
 * and `mirror_epoch_ms + announced_at_ms` converts a mirror timestamp back to
 * wall time. */
inline std::string toJson(const MirrorView& v, int64_t nowMs) {
    std::string s = "{\"mirror_version\":1";
    s += ",\"mirror_epoch_ms\":" + std::to_string(v.epochMs);
    s += ",\"uptime_ms\":" + std::to_string(nowMs);
    s += ",\"announcement_seq\":" + std::to_string(v.announcementSeq);
    s += ",\"state\":\"" + std::string(toString(v.state)) + "\"";
    s += ",\"mirror_confidence\":\"" + std::string(toString(v.confidence)) + "\"";
    if (v.hasPending) {
        s += ",\"pending\":" + announcementJson(v.pending);
    }
    if (v.hasLastResolution) {
        s += ",\"last_resolution\":{\"resolution\":\"" +
             std::string(toString(v.lastResolution.resolution)) + "\"";
        s += ",\"resolved_at_ms\":" + std::to_string(v.lastResolution.resolvedAtMs);
        s += ",\"announcement\":" + announcementJson(v.lastResolution.announcement);
        s += "}";
    }
    s += "}";
    return s;
}

} // namespace pulsemesh
