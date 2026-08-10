#!/usr/bin/env python3
"""Structural checks on the plugin glue.

The state machine is unit-tested; the ~30 lines that wire it into fppd are not,
because exercising them needs a real fppd.  These are the properties of that
glue whose failure is SILENT and TOTAL — the mirror keeps answering, it just
answers from nothing — so they are asserted structurally rather than left to
the §22.5 rig alone.  A lint is a weaker instrument than a test; it is here
because the alternative for these is no instrument.

Every check must survive the tamper that motivates it, and TWO of them did not
until 2026-07-28.  The announcement feed — `playlistInserted`, the wire the
whole machine is named after — was not checked at all: deleting
`m_mirror.onInserted(...)` left every instrument green (the host tests drive
the machine directly, the four compat builds are -fsyntax-only and cannot see
a deleted statement, and `override` still binds), while on a live box the
mirror would answer `state: empty, mirror_confidence: exact` forever.  `exact`
is the single word §13.2 acts on, so the connector would read the unchanged
`announcement_seq` as "fppd never ran the command" and flag
`insert_not_announced` on every SUCCESSFUL insert.  And check 3's route guard
was verified by PROXIMITY rather than polarity: swapping the #ifdef branches
left the guard line within its search window, so a build that serves the route
only where nothing can feed it passed.
"""
import pathlib
import re
import sys

SRC = pathlib.Path(__file__).resolve().parent.parent / "src" / "FPPPulseMesh.cpp"

failures = []


def fail(msg):
    failures.append(msg)


text = SRC.read_text()
lines = text.splitlines()


def find(pattern, start=0, end=None):
    """1-based line number of the first line matching pattern, or None."""
    for i, line in enumerate(lines[start:end if end is None else end], start=start):
        if re.search(pattern, line):
            return i
    return None


# ---------------------------------------------------------------------------
# 1. The mirror is fed before playlistCallback can return early.
#
# playlistCallback validates `size` and `name` and RETURNS when either is
# missing, and then ignores everything with size <= 1.  An inserted single-item
# range is trimmed by Playlist::Load, so the inserted playlist reports size 1 —
# the consumption event the mirror most needs to see is exactly the one that
# filter drops.  Feeding the mirror after any of that loses it silently.
# ---------------------------------------------------------------------------
cb_start = find(r"void playlistCallback\(")
if cb_start is None:
    fail("playlistCallback not found at all")
else:
    cb_end = find(r"^    virtual void SendMediaOpenPacket", cb_start)
    feed = find(r"m_mirror\.onPlaylistEvent", cb_start, cb_end)
    first_return = find(r"^\s+return;", cb_start, cb_end)
    size_filter = find(r"if \(size > 1", cb_start, cb_end)
    if feed is None:
        fail("playlistCallback never feeds the mirror")
    else:
        if first_return is not None and feed > first_return:
            fail("the mirror is fed AFTER playlistCallback's first early return")
        if size_filter is not None and feed > size_filter:
            fail("the mirror is fed AFTER the size>1 filter that drops a 1-item insert")
        # The size is the machine's ONLY discriminator between the inserted
        # range starting and the whole playlist being started afresh — the rig
        # showed that without it an operator's `Start Playlist` retires a live
        # announcement as `consumed`, at confidence `exact`. Nothing a compiler
        # or a host test can see: `onPlaylistEvent` takes an int either way, and
        # the tests call it directly. So it is checked here, structurally.
        before_feed = "\n".join(lines[cb_start:feed])
        call = "\n".join(lines[feed:min(feed + 4, cb_end or len(lines))])
        if not re.search(r'playlist\["size"\]\.asInt\(\)', before_feed):
            fail("the mirror feed does not READ size from the callback's JSON")
        if re.search(r"onPlaylistEvent\(.*?,\s*-1\s*,", call, re.S):
            fail("the mirror is fed a hardcoded size, not the reported one")

# ---------------------------------------------------------------------------
# 2. The announcement reaches the mirror, with the values fppd reported.
#
# This is the machine's only input for "an insert exists".  Without it every
# read answers `empty/exact` — a durable false `exact`, which is worse than
# no route at all, because §13.2 acts on `exact` alone.  Nothing else can see
# this: the host tests call `onInserted` directly and the compat builds are
# syntax-only.
# ---------------------------------------------------------------------------
ins_start = find(r"void playlistInserted\(")
if ins_start is None:
    fail("playlistInserted is not implemented at all")
else:
    ins_end = find(r"^    virtual void playlistCallback\(", ins_start)
    feed = find(r"m_mirror\.onInserted", ins_start, ins_end)
    if feed is None:
        fail("playlistInserted never feeds the mirror")
    else:
        body = "\n".join(lines[ins_start:ins_end or len(lines)])
        # Each argument must be the one fppd handed us. A hardcoded `immediate`
        # freezes a Pending announcement at `exact` forever — no boundary event
        # can degrade what was never announced correctly — and a hardcoded
        # position aims §13.2's repair at the wrong entry.
        call = re.search(r"m_mirror\.onInserted\(([^;]*)\)", body, re.S)
        args = [a.strip() for a in call.group(1).split(",")] if call else []
        expected = ["playlist", "position", "endPosition", "immediate"]
        if len(args) < 5:
            fail("the mirror announcement is missing arguments")
        else:
            for want, got in zip(expected, args):
                if got != want:
                    fail(f"the mirror announcement passes {got!r} where fppd reported {want!r}")
        if not re.search(r"std::lock_guard<std::mutex>\s+\w+\(m_mirrorMutex\)", body):
            fail("playlistInserted feeds the mirror without holding m_mirrorMutex")

# ---------------------------------------------------------------------------
# 3. The route is not served on a build that cannot feed it.
#
# `playlistInserted` first shipped in FPP 8.5.  On an older plugin API nothing
# ever announces an insert, so an unguarded route would answer `empty` with
# confidence `exact` on a box where an insert may well be pending — and §13.2's
# repair path acts on `exact`.  That is worse than no answer: unregistered, the
# capability probe's 404 reports pending_insert_introspection honestly false.
# ---------------------------------------------------------------------------
# Two registration sites, one per transport: register_resource() on the
# libhttpserver path, FPPPlugins::registerPluginApi() on the plugin-API-6
# path.  Guarded with the right POLARITY, not mere proximity (the docstring's
# second 2026-07-28 failure): the NEAREST preprocessor conditional above each
# call must be the #ifdef itself — any #else/#elif/#endif in between means the
# wrong branch.  The pattern is anchored with FPPPlugins:: so it cannot match
# the unconditional unregisterPluginApi() teardown call instead.
SITES = [
    ("register_resource", r"register_resource\(PM_MIRROR_PATH"),
    ("registerPluginApi", r"FPPPlugins::registerPluginApi\(PM_MIRROR_PATH"),
]
for label, pattern in SITES:
    reg = find(pattern)
    if reg is None:
        fail(f"the mirror route is never registered via {label}")
        continue
    guard = None
    for i in range(reg - 1, -1, -1):
        if re.match(r"\s*#\s*(ifdef|ifndef|if|else|elif|endif)\b", lines[i]):
            guard = i
            break
    if guard is None or not re.search(r"#ifdef PM_HAVE_PLAYLIST_INSERTED", lines[guard]):
        fail(f"{label}(PM_MIRROR_PATH...) is not immediately inside "
             "#ifdef PM_HAVE_PLAYLIST_INSERTED — the route would be served "
             "on a build where nothing can feed the mirror")

if failures:
    for f in failures:
        print(f"  FAIL  glue: {f}")
    print(f"\n{len(failures)} glue check(s) failed")
    sys.exit(1)

print("plugin glue: all checks passed")
