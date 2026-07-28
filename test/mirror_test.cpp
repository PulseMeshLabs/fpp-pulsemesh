/*
 * Host tests for the pending-insert mirror (plan-16 §13.1).
 *
 * These run on any machine with a C++17 compiler — no FPP tree, no fppd, no
 * network.  That is the point: the machine is the part that must be right
 * BEFORE it is loaded into a show box, because the failure it exists to
 * prevent (a stale "pending" that makes §13.2's repair double-insert) is
 * invisible from the outside.
 *
 *   make -C test && test/mirror_test
 */

#include "../src/PendingInsertMirror.h"

#include <cstdio>
#include <string>

using namespace pulsemesh;

static int g_failures = 0;
static const char* g_case = "";

static void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  FAIL  %s: %s (line %d)\n", g_case, what, line);
        ++g_failures;
    }
}

static void checkEq(const std::string& got, const std::string& want, const char* what, int line) {
    if (got != want) {
        std::printf("  FAIL  %s: %s — got \"%s\", want \"%s\" (line %d)\n", g_case, what,
                    got.c_str(), want.c_str(), line);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_STATE(view, want) checkEq(toString((view).state), (want), "state", __LINE__)
#define CHECK_CONF(view, want) checkEq(toString((view).confidence), (want), "confidence", __LINE__)
#define CHECK_RES(view, want) \
    checkEq(toString((view).lastResolution.resolution), (want), "resolution", __LINE__)

#define CASE(name) g_case = name

/* fppd's own thread order at an insert: playlistInserted first, then — on the
 * idle branch only — Play()'s "start", synchronously, same thread, same
 * recursive mutex (Playlist.cpp:1113-1118). */
static void idleNonImmediateNeverRestsPending() {
    CASE("idle non-immediate: announced -> immediate_played, never pending");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 0, 0, false, 1000);
    auto v = m.view(1000);
    CHECK_STATE(v, "announced");
    CHECK_CONF(v, "unresolved");

    m.onPlaylistEvent("Show RF", PlaylistAction::Start, 1, 1000);
    v = m.view(1000);
    CHECK_STATE(v, "empty");
    CHECK_CONF(v, "exact");
    CHECK_RES(v, "immediate_played");
    CHECK(!v.hasPending);
}

static void idleImmediateResolvesTheSameWay() {
    CASE("idle immediate: the synchronous Play branch (:1127)");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 3, 3, true, 1000);
    CHECK_STATE(m.view(1000), "awaiting_start");
    m.onPlaylistEvent("Show RF", PlaylistAction::Start, 1, 1000);
    auto v = m.view(1000);
    CHECK_STATE(v, "empty");
    CHECK_RES(v, "immediate_played");
    CHECK_CONF(v, "exact");
}

static void nonImmediateWhilePlayingRestsPending() {
    CASE("non-immediate while playing settles to a truthful pending");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    CHECK_STATE(m.view(1249), "announced");
    CHECK_CONF(m.view(1249), "unresolved");
    auto v = m.view(1250);
    CHECK_STATE(v, "pending");
    CHECK_CONF(v, "exact");
    CHECK(v.hasPending);
    CHECK(v.pending.position == 5);
    /* And it STAYS pending: the slot verifiably sits until the natural
     * boundary, which is minutes away on a show box. */
    CHECK_STATE(m.view(1250 + 600000), "pending");
}

static void pendingIsConsumedByItsOwnStart() {
    CASE("pending + start naming it = SwitchToInsertedPlaylist's success path");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onPlaylistEvent("Show RF", PlaylistAction::QueryNext, 6, 60000);
    m.onPlaylistEvent("Show RF", PlaylistAction::Start, 1, 60001);
    auto v = m.view(60001);
    CHECK_STATE(v, "empty");
    CHECK_RES(v, "consumed");
    CHECK_CONF(v, "exact");
}

static void queryNextIsNotABoundary() {
    CASE("query_next fires BEFORE the switch is attempted (:825 vs :850)");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onPlaylistEvent("Show", PlaylistAction::QueryNext, 6, 60000);
    auto v = m.view(60000);
    CHECK_STATE(v, "pending");
    CHECK_CONF(v, "exact");
    CHECK_RES(v, "none");
}

static void boundaryWithoutTheStartDoesNotRetireTheAnnouncement() {
    CASE("boundary without the insert's start: unknown, NOT over");
    /* MEASURED ON THE RIG, 2026-07-28.  An earlier version resolved this to a
     * terminal `consumed_unresolved` with an empty slot, and the box refuted
     * it: `Playlist::NextItem` (:1346) emits exactly this event and never goes
     * near SwitchToInsertedPlaylist — the only site in all of fppd that clears
     * the slot (:1012).  Nor does `Cleanup()` (:1080), which every stop runs.
     * So the insert stays armed and fires at the next real boundary; claiming
     * it was over would make §13.2 report `failed` for a song that then plays,
     * unattributed. */
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onPlaylistEvent("Show", PlaylistAction::Playing, 6, 60000);
    auto v = m.view(60000);
    CHECK_STATE(v, "unresolved_pending");
    CHECK_CONF(v, "unresolved");
    CHECK_RES(v, "none");
    /* The coordinates survive, so §13.2 can still see what was asked for. */
    CHECK(v.hasPending);
    CHECK(v.pending.position == 5);
}

static void playingNamingTheSlotIsTheParentMovingOn() {
    CASE("`playing` naming the slot playlist is the PARENT, not the insert");
    /* §13.2 inserts into the pool playlist, which is frequently the playlist
     * already running — so this is the ordinary case, not an exotic one.  An
     * object already playing emits "playing" (Playlist.cpp:636); a freshly
     * constructed inserted Playlist is m_currentState("idle") and therefore
     * always emits "start" (:638, :70).  Reading "playing" as the insert's own
     * start would report a play that never happened. */
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onPlaylistEvent("Show RF", PlaylistAction::Playing, 6, 60000);
    auto v = m.view(60000);
    CHECK_STATE(v, "unresolved_pending");
    CHECK_CONF(v, "unresolved");
}

static void aStopDoesNotDisarmTheSlot() {
    CASE("stop while pending — Cleanup() never touches the slot");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onPlaylistEvent("Show", PlaylistAction::Stop, 0, 60000);
    CHECK_STATE(m.view(60000), "unresolved_pending");
}

static void aDifferentPlaylistStartingIsABoundaryPassed() {
    CASE("start naming a different playlist while pending");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onPlaylistEvent("Christmas", PlaylistAction::Start, 4, 60000);
    CHECK_STATE(m.view(60000), "unresolved_pending");
}

static void aLateStartStillProvesConsumption() {
    CASE("an insert skipped past still fires at the next REAL boundary");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onPlaylistEvent("Show RF", PlaylistAction::Playing, 6, 60000); // operator skip
    CHECK_STATE(m.view(60000), "unresolved_pending");
    m.onPlaylistEvent("Show RF", PlaylistAction::Start, 1, 250000); // the real boundary
    auto v = m.view(250000);
    CHECK_STATE(v, "empty");
    CHECK_RES(v, "consumed");
    CHECK_CONF(v, "exact");
}

static void aFreshStartOfTheSamePlaylistIsNotTheInsertsOwnStart() {
    CASE("an operator restarting the pool playlist never retires the insert");
    /* MEASURED on the 8.5.1 rig, 2026-07-28.  An announcement left
     * unresolved_pending by a skipped boundary was retired as `consumed` the
     * instant a `Start Playlist` command re-started the same playlist —
     * "the insert played" said about an insert fppd had thrown away, at
     * confidence `exact`.  The discriminator is the size: Playlist::Load
     * TRIMS its copy to the requested range, so the inserted object reports
     * the range's length and a whole-playlist start reports all of it. */
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onPlaylistEvent("Show RF", PlaylistAction::Playing, 6, 60000); // operator skip
    CHECK_STATE(m.view(60000), "unresolved_pending");
    m.onPlaylistEvent("Show RF", PlaylistAction::Start, 6, 70000); // Start Playlist
    auto v = m.view(70000);
    CHECK_STATE(v, "unresolved_pending");
    CHECK_CONF(v, "unresolved");
    CHECK(v.pending.position == 5);
}

static void aFreshStartDoesNotResolveAnImmediateEither() {
    CASE("the awaiting_start window is not satisfied by a whole-playlist start");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, true, 1000);
    m.onPlaylistEvent("Show RF", PlaylistAction::Start, 6, 1200);
    CHECK_STATE(m.view(1200), "awaiting_start");
    auto v = m.view(3001); // the timeout, not the start, is what decides
    CHECK_STATE(v, "empty");
    CHECK_RES(v, "unresolved");
    CHECK_CONF(v, "unresolved");
}

static void anUnknownSizeIsNotTreatedAsAMismatch() {
    CASE("a missing size field declines to discriminate rather than refusing");
    /* Rule (c) of the asymmetry: a false `unresolved` costs one declined
     * repair, a false `consumed` is a lie — but refusing every start on a
     * build that stopped reporting `size` would strand every announcement. */
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    CHECK_STATE(m.view(60000), "pending"); // settled: this is a boundary, not the idle branch
    m.onPlaylistEvent("Show RF", PlaylistAction::Start, -1, 60000);
    auto v = m.view(60000);
    CHECK_STATE(v, "empty");
    CHECK_RES(v, "consumed");
}

static void aWholePlaylistInsertHasNoDiscriminator() {
    CASE("an open range accepts the start it cannot check");
    /* position -1 is §22.1's whole-playlist insert: nothing is trimmed, so the
     * inserted object's size IS the playlist's size and the two events are
     * genuinely identical.  Accepting is the same asymmetry as above. */
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", -1, -1, false, 1000);
    CHECK_STATE(m.view(60000), "pending");
    m.onPlaylistEvent("Show RF", PlaylistAction::Start, 6, 60000);
    CHECK_RES(m.view(60000), "consumed");
}

static void anUnresolvedPendingIsStillOverwritable() {
    CASE("a later insert supersedes an unresolved_pending");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onPlaylistEvent("Show", PlaylistAction::Stop, 0, 60000);
    m.onInserted("Show RF", 6, 6, false, 61000);
    auto v = m.view(61250);
    CHECK_RES(v, "superseded");
    CHECK(v.lastResolution.announcement.position == 5);
    CHECK_STATE(v, "pending");
    CHECK_CONF(v, "exact");
}

static void aLaterInsertOverwrites() {
    CASE("overwrite: fppd's slot is one unconditionally-assigned field (:1119)");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onInserted("Show RF", 7, 7, false, 2000);
    auto v = m.view(2000);
    CHECK_RES(v, "superseded");
    CHECK(v.lastResolution.announcement.position == 5);
    CHECK(v.lastResolution.announcement.seq == 1);
    CHECK(v.hasPending);
    CHECK(v.pending.position == 7);
    CHECK(v.pending.seq == 2);
    CHECK(v.announcementSeq == 2);
    /* Two inserts of the SAME playlist at the SAME position are otherwise
     * indistinguishable; the seq is what lets §13.2 tell its own replacement
     * from the one it replaced. */
}

static void anImmediateInsertSupersedesAPending() {
    CASE("an immediate insert overwrites a pending one");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onInserted("Show RF", 5, 5, true, 2000);
    auto v = m.view(2000);
    CHECK_RES(v, "superseded");
    CHECK_STATE(v, "awaiting_start");
    CHECK(v.pending.immediate);
}

static void awaitingStartResolvesOnlyTwoWays() {
    CASE("awaiting_start ignores unrelated events — the failure is EVENTLESS");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, true, 1000);
    m.onPlaylistEvent("Show", PlaylistAction::Playing, 6, 1100);
    m.onPlaylistEvent("Show", PlaylistAction::QueryNext, 6, 1200);
    m.onPlaylistEvent("Christmas", PlaylistAction::Start, 4, 1300);
    auto v = m.view(1400);
    CHECK_STATE(v, "awaiting_start");
    CHECK_CONF(v, "unresolved");
    /* ...and the start still resolves it. */
    m.onPlaylistEvent("Show RF", PlaylistAction::Start, 1, 2999);
    v = m.view(2999);
    CHECK_RES(v, "immediate_played");
    CHECK_CONF(v, "exact");
}

static void awaitingStartTimesOutToUnresolved() {
    CASE("the eventless branch: paused player, no boundary events, ever");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, true, 1000);
    CHECK_STATE(m.view(2999), "awaiting_start");
    auto v = m.view(3000);
    CHECK_STATE(v, "empty");
    CHECK_RES(v, "unresolved");
    /* This is the whole reason for the state.  Resting at pending/exact here
     * would be stale FOREVER — the original item sits paused and emits
     * nothing — and §13.2's replace-repair would double-insert on it. */
    CHECK_CONF(v, "unresolved");
    CHECK(!v.hasPending);
}

static void deadlinesAreEvaluatedAtReadTime() {
    CASE("a read is as truthful as an event — no thread, no timer");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, true, 1000);
    /* No event of any kind ever arrives; the view alone must resolve it. */
    CHECK_RES(m.view(9999), "unresolved");
}

static void aCleanAnnouncementRestoresExact() {
    CASE("an unresolved answer does not poison the next announcement");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, true, 1000);
    CHECK_CONF(m.view(3000), "unresolved");
    m.onInserted("Show RF", 6, 6, false, 4000);
    auto v = m.view(4250);
    CHECK_STATE(v, "pending");
    CHECK_CONF(v, "exact");
    m.onPlaylistEvent("Show RF", PlaylistAction::Start, 1, 5000);
    CHECK_CONF(m.view(5000), "exact");
    CHECK_RES(m.view(5000), "consumed");
}

static void theWholePlaylistCliffIsVisible() {
    CASE("§22.1's cliff: an arg of \"0\" reaches the plugin as position -1");
    /* PlaylistCommands.cpp:283 converts `start > 0 ? start - 1 : -1`, so the
     * unconverted-zero bug — the one that loads the ENTIRE show instead of one
     * song, with no error on either side — is not silent here.  The mirror
     * reports the coordinate fppd actually stored. */
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", -1, -1, false, 1000);
    auto v = m.view(1250);
    CHECK(v.pending.position == -1);
    CHECK(v.pending.endPosition == -1);
    /* And it survives serialization — this is the answer §13.2 would read back
     * to decide whether the thing it asked for is the thing fppd stored. */
    std::string j = toJson(v, 1250);
    CHECK(j.find("\"position\":-1") != std::string::npos);
    CHECK(j.find("\"end_position\":-1") != std::string::npos);
}

static void positionsAreFppdInternalZeroBased() {
    CASE("the mirror speaks the connector's index base natively");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 0, 0, false, 1000);
    CHECK(m.view(1250).pending.position == 0);
}

static void jsonCarriesTheContract() {
    CASE("json shape");
    PendingInsertMirror m(1234);
    /* Deliberately NOT position 0.  A JSON assertion written against the
     * zero case cannot tell a carried position from a hard-coded one — and
     * zero is the value §22.1's cliff is about, so it is the one number that
     * must never be assumed. */
    m.onInserted("Show RF", 4, 6, false, 5000);
    std::string j = toJson(m.view(5250), 5250);
    CHECK(j.find("\"mirror_version\":1") != std::string::npos);
    CHECK(j.find("\"mirror_epoch_ms\":1234") != std::string::npos);
    CHECK(j.find("\"uptime_ms\":5250") != std::string::npos);
    CHECK(j.find("\"state\":\"pending\"") != std::string::npos);
    CHECK(j.find("\"mirror_confidence\":\"exact\"") != std::string::npos);
    CHECK(j.find("\"playlist\":\"Show RF\"") != std::string::npos);
    CHECK(j.find("\"position\":4") != std::string::npos);
    CHECK(j.find("\"end_position\":6") != std::string::npos);
    CHECK(j.find("\"announced_at_ms\":5000") != std::string::npos);
    CHECK(j.find("\"immediate\":false") != std::string::npos);
    CHECK(j.find("\"seq\":1") != std::string::npos);
    CHECK(j.find("last_resolution") == std::string::npos);

    m.onPlaylistEvent("Show RF", PlaylistAction::Start, 3, 6000);
    j = toJson(m.view(6000), 6000);
    CHECK(j.find("\"state\":\"empty\"") != std::string::npos);
    CHECK(j.find("\"resolution\":\"consumed\"") != std::string::npos);
    CHECK(j.find("\"resolved_at_ms\":6000") != std::string::npos);
    CHECK(j.find("\"pending\":") == std::string::npos);
}

static void jsonEscapesOperatorChosenNames() {
    CASE("playlist names are operator-chosen and reach the wire verbatim");
    PendingInsertMirror m(0);
    m.onInserted("He said \"go\"\\now\n", 1, 1, false, 0);
    std::string j = toJson(m.view(250), 250);
    CHECK(j.find("\"playlist\":\"He said \\\"go\\\"\\\\now\\n\"") != std::string::npos);
}

static void theEpochMarksARestart() {
    CASE("a restart clears fppd's slot and this mirror together");
    /* The plugin is an in-process .so; it cannot restart independently of
     * fppd.  So a changed epoch is proof an older answer is VOID, not stale. */
    PendingInsertMirror before(1000);
    before.onInserted("Show RF", 5, 5, false, 1000);
    CHECK_STATE(before.view(1250), "pending");
    PendingInsertMirror after(7000);
    auto v = after.view(7000);
    CHECK_STATE(v, "empty");
    CHECK(v.epochMs == 7000);
    CHECK(v.announcementSeq == 0);
    CHECK(!v.hasLastResolution);
}

static void actionStringsMapOnce() {
    CASE("fppd's action strings");
    CHECK(actionFromString("start") == PlaylistAction::Start);
    CHECK(actionFromString("playing") == PlaylistAction::Playing);
    CHECK(actionFromString("stop") == PlaylistAction::Stop);
    CHECK(actionFromString("query_next") == PlaylistAction::QueryNext);
    CHECK(actionFromString("mediaOpen") == PlaylistAction::Other);
}

static void unknownActionsAreInert() {
    CASE("an action this machine does not know resolves nothing");
    PendingInsertMirror m(1000);
    m.onInserted("Show RF", 5, 5, false, 1000);
    m.onPlaylistEvent("Show", PlaylistAction::Other, 6, 60000);
    CHECK_STATE(m.view(60000), "pending");
    CHECK_RES(m.view(60000), "none");
}


static void anEmptyNameIsFppdsSentinelNotAPlaylist() {
    CASE("an empty announcement clears the slot rather than claiming one");
    /* `InsertPlaylistAsNext` fires the callback as its FIRST statement, before
     * any validation, and fppd's own empty sentinel for "slot unoccupied" is
     * the empty string (`SwitchToInsertedPlaylist` tests m_insertedPlaylist
     * != ""). Believing an empty announcement minted a pending/exact for a
     * slot that verifiably holds nothing — and on an idle box it rested there
     * forever, which is the one answer §13.2 acts on. */
    PendingInsertMirror m(1000);
    m.onInserted("", 1, 1, false, 1000);
    CHECK_STATE(m.view(1000), "empty");
    CHECK_STATE(m.view(60000), "empty");
    CHECK(m.view(60000).confidence == Confidence::Exact);

    /* And an empty assignment over a LIVE announcement really does clear it:
     * that is what fppd just did to the slot. */
    PendingInsertMirror n(1000);
    n.onInserted("Show RF", 5, 5, false, 1000);
    CHECK_STATE(n.view(1250), "pending");
    n.onInserted("", 1, 1, false, 2000);
    CHECK_STATE(n.view(2000), "empty");
    CHECK_RES(n.view(2000), "superseded");
}

int main() {
    idleNonImmediateNeverRestsPending();
    idleImmediateResolvesTheSameWay();
    nonImmediateWhilePlayingRestsPending();
    pendingIsConsumedByItsOwnStart();
    queryNextIsNotABoundary();
    boundaryWithoutTheStartDoesNotRetireTheAnnouncement();
    playingNamingTheSlotIsTheParentMovingOn();
    aStopDoesNotDisarmTheSlot();
    aDifferentPlaylistStartingIsABoundaryPassed();
    aLateStartStillProvesConsumption();
    aFreshStartOfTheSamePlaylistIsNotTheInsertsOwnStart();
    aFreshStartDoesNotResolveAnImmediateEither();
    anUnknownSizeIsNotTreatedAsAMismatch();
    aWholePlaylistInsertHasNoDiscriminator();
    anUnresolvedPendingIsStillOverwritable();
    aLaterInsertOverwrites();
    anImmediateInsertSupersedesAPending();
    awaitingStartResolvesOnlyTwoWays();
    awaitingStartTimesOutToUnresolved();
    deadlinesAreEvaluatedAtReadTime();
    aCleanAnnouncementRestoresExact();
    theWholePlaylistCliffIsVisible();
    positionsAreFppdInternalZeroBased();
    jsonCarriesTheContract();
    jsonEscapesOperatorChosenNames();
    theEpochMarksARestart();
    actionStringsMapOnce();
    unknownActionsAreInert();
    anEmptyNameIsFppdsSentinelNotAPlaylist();

    if (g_failures) {
        std::printf("\n%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("pending-insert mirror: all checks passed\n");
    return 0;
}
