/*
 * Host tests for the media position gate (src/MediaSyncGate.h).
 *
 *   make -C test && test/sync_gate_test
 */

#include "../src/MediaSyncGate.h"

#include <cstdio>
#include <vector>

using namespace pulsemesh;

static int g_failures = 0;
static const char* g_case = "";

static void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::printf("  FAIL  %s: %s (line %d)\n", g_case, what, line);
        ++g_failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

/* Feed reports for one file; return the positions that were forwarded. */
static std::vector<float> feed(MediaSyncGate& g, const char* file, const std::vector<float>& secs) {
    std::vector<float> out;
    for (float s : secs) {
        if (g.shouldForward(file, s)) {
            out.push_back(s);
        }
    }
    return out;
}

/* GStreamerOut at ~40 Hz: clamped 0.000 for ~85 ms, then the true line. */
static std::vector<float> fppStart() {
    std::vector<float> v = {0.0f, 0.0f, 0.0f, 0.0f};
    for (float s = 0.015f; s < 1.6f; s += 0.025f) {
        v.push_back(s);
    }
    return v;
}

static void clampedStartForwardsFirstRealPosition() {
    g_case = "clamped start forwards the first real position, then one per half-second";
    MediaSyncGate g;
    auto out = feed(g, "a.mp3", fppStart());
    CHECK(out.size() == 4);
    CHECK(out.size() > 0 && out[0] > 0.0f && out[0] < 0.02f); // not the clamped 0, not 0.5 s later
    CHECK(out.size() > 1 && out[1] >= 0.5f && out[1] < 0.53f);
    CHECK(out.size() > 2 && out[2] >= 1.0f && out[2] < 1.03f);
    CHECK(out.size() > 3 && out[3] >= 1.5f && out[3] < 1.53f);
}

static void startPacketRearms() {
    g_case = "a start packet re-arms the same file (restart / single-item repeat)";
    MediaSyncGate g;
    feed(g, "a.mp3", fppStart());
    g.reset();
    auto out = feed(g, "a.mp3", fppStart());
    CHECK(out.size() == 4 && out[0] > 0.0f);
}

static void fileChangeRearms() {
    g_case = "a new file re-arms without a start packet";
    MediaSyncGate g;
    feed(g, "a.mp3", fppStart());
    auto out = feed(g, "b.mp3", fppStart());
    CHECK(out.size() == 4 && out[0] > 0.0f && out[0] < 0.02f);
}

static void backwardJumpRearms() {
    g_case = "the same file jumping back to 0 re-arms (no start packet)";
    MediaSyncGate g;
    feed(g, "a.mp3", {0.0f, 3.0f, 3.5f, 4.0f});
    auto out = feed(g, "a.mp3", fppStart());
    CHECK(out.size() == 4 && out[0] > 0.0f && out[0] < 0.02f);
}

static void jitterDoesNotRearm() {
    g_case = "a small backward step is jitter, not a restart";
    MediaSyncGate g;
    feed(g, "a.mp3", {0.1f, 0.6f, 1.1f});
    auto out = feed(g, "a.mp3", {1.05f, 1.2f, 1.4f, 1.6f});
    CHECK(out.size() == 1 && out[0] == 1.6f);
}

static void midItemFirstReportForwarded() {
    g_case = "a first report mid-item (connector restarted, plugin reloaded) is forwarded";
    MediaSyncGate g;
    auto out = feed(g, "a.mp3", {42.3f, 42.4f, 42.5f});
    CHECK(out.size() == 2 && out[0] == 42.3f && out[1] == 42.5f);
}

static void neverMovingForwardsNothing() {
    g_case = "an item stuck at 0 forwards nothing";
    MediaSyncGate g;
    auto out = feed(g, "a.mp3", {0.0f, 0.0f, 0.0f, 0.0f, -0.0f});
    CHECK(out.empty());
}

int main() {
    clampedStartForwardsFirstRealPosition();
    startPacketRearms();
    fileChangeRearms();
    backwardJumpRearms();
    jitterDoesNotRearm();
    midItemFirstReportForwarded();
    neverMovingForwardsNothing();
    if (g_failures) {
        std::printf("sync_gate_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("sync_gate_test: all passed\n");
    return 0;
}
