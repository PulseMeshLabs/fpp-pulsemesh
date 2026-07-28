#!/usr/bin/env python3
"""Structural checks on the plugin glue.

The state machine is unit-tested; the ~30 lines that wire it into fppd are not,
because exercising them needs a real fppd.  These are the two properties of
that glue whose failure is SILENT and TOTAL — the mirror keeps answering, it
just answers from nothing — so they are asserted structurally rather than left
to the §22.5 rig alone.  A lint is a weaker instrument than a test; it is here
because the alternative for these two is no instrument.
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

# ---------------------------------------------------------------------------
# 2. The route is not served on a build that cannot feed it.
#
# `playlistInserted` first shipped in FPP 8.5.  On an older plugin API nothing
# ever announces an insert, so an unguarded route would answer `empty` with
# confidence `exact` on a box where an insert may well be pending — and §13.2's
# repair path acts on `exact`.  That is worse than no answer: unregistered, the
# capability probe's 404 reports pending_insert_introspection honestly false.
# ---------------------------------------------------------------------------
reg = find(r"register_resource\(PM_MIRROR_PATH")
if reg is None:
    fail("the mirror route is never registered")
else:
    guard = find(r"#ifdef PM_HAVE_PLAYLIST_INSERTED", max(0, reg - 6), reg)
    if guard is None:
        fail("register_resource is not guarded by #ifdef PM_HAVE_PLAYLIST_INSERTED")

if failures:
    for f in failures:
        print(f"  FAIL  glue: {f}")
    print(f"\n{len(failures)} glue check(s) failed")
    sys.exit(1)

print("plugin glue: all checks passed")
