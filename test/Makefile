# Host tests for the pending-insert mirror.
#
#   make -C test check
#
# mirror_test needs nothing beyond a compiler — the state machine in
# ../src/PendingInsertMirror.h is deliberately dependency-free.  The compat
# builds need a local FPP checkout (FPP_SRC): the header sets are extracted
# from it at test time rather than vendored, so this repo distributes no FPP
# (LGPL) code.  See fppapi/PROVENANCE.

CXX ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror

FPP_SRC ?= $(HOME)/projects/fpp

# The plugin's REAL translation unit, compiled against the REAL FPP plugin
# headers from every API shape.  Not a copy of the override set — a copy would
# drift, and drift is exactly the failure being checked for: a virtual whose
# signature no longer matches its base is silently not an override, and the
# mirror would then be fed by nothing at all.
#
# Third-party libraries FPP itself pulls in (jsoncpp, libhttpserver, drogon)
# are stubbed under fppapi/stub.
COMPAT_FLAGS = -std=c++17 -fsyntax-only -Wall -Wextra -Werror -Wno-unused-parameter \
               -Ifppapi/stub -I../src

all: mirror_test

mirror_test: mirror_test.cpp ../src/PendingInsertMirror.h
	$(CXX) $(CXXFLAGS) -o $@ mirror_test.cpp

# Extracted fresh on every compat run (cheap, and never stale relative to the
# checkout).  Tags pin the released lines; origin/* for the moving ones so the
# builds track what upstream actually ships — the printed hashes say what was
# tested.  10.0-beta2 is pinned deliberately: it is the last migration-window
# shape (no-arg registerApis() without registerPluginApi(), libhttpserver
# shims still in fpphttp.h) — API 6 (beta3+) removed all of that.
FPPAPI_REFS = 8.0 8.5.1 9.5.3 10.0-beta2 origin/v10.0-beta origin/master

fppapi-headers:
	@test -e $(FPP_SRC)/.git || { \
	    echo "compat needs an FPP checkout: git clone https://github.com/FalconChristmas/fpp $(FPP_SRC)"; \
	    echo "(or set FPP_SRC=/path/to/fpp)"; exit 1; }
	@for ref in $(FPPAPI_REFS); do \
	    git -C $(FPP_SRC) rev-parse -q --verify "$$ref^{commit}" >/dev/null || { \
	        echo "ref $$ref not found in $(FPP_SRC) — run: git -C $(FPP_SRC) fetch origin --tags"; exit 1; }; done
	@mkdir -p fppapi/v80 fppapi/v8 fppapi/v9 fppapi/v10mw fppapi/v10 fppapi/vmaster
	@git -C $(FPP_SRC) show 8.0:src/Plugin.h > fppapi/v80/Plugin.h
	@git -C $(FPP_SRC) show 8.5.1:src/Plugin.h > fppapi/v8/Plugin.h
	@git -C $(FPP_SRC) show 9.5.3:src/Plugin.h > fppapi/v9/Plugin.h
	@for f in Plugin.h fpphttp.h fpphttp_types.h fpp-json-fwd.h; do \
	    git -C $(FPP_SRC) show 10.0-beta2:src/$$f > fppapi/v10mw/$$f; done
	@for f in Plugin.h fpphttp.h fpphttp_types.h fpp-json-fwd.h log.h; do \
	    git -C $(FPP_SRC) show origin/v10.0-beta:src/$$f > fppapi/v10/$$f; done
	@for f in Plugin.h fpphttp.h fpphttp_types.h fpp-json-fwd.h log.h; do \
	    git -C $(FPP_SRC) show origin/master:src/$$f > fppapi/vmaster/$$f; done
	@echo "fppapi: 8.0 8.5.1 9.5.3 10.0-beta2" \
	    "v10.0-beta@$$(git -C $(FPP_SRC) rev-parse --short origin/v10.0-beta)" \
	    "master@$$(git -C $(FPP_SRC) rev-parse --short origin/master)"

# One build per plugin-API shape the plugin ships to.  The 8.5.1 pair proves
# the decltype-derived render_GET return binds under either cv-qualification;
# the 10.0-beta2 build is the migration window, where the probe correctly
# stays off and the libhttpserver signature routes through fpphttp.h's
# [[deprecated]] shims; the v10/master builds are plugin API 6
# (registerPluginApi, shims removed).
compat: fppapi-headers ../src/FPPPulseMesh.cpp ../src/PendingInsertMirror.h
	@echo "  8.5.1      (libhttpserver, const return)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_HAVE_PLAYLIST_INSERTED=1 -DPM_STUB_CONST_RETURN \
	    -Ifppapi/v8 ../src/FPPPulseMesh.cpp
	@echo "  8.5.1      (libhttpserver, plain return)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_HAVE_PLAYLIST_INSERTED=1 \
	    -Ifppapi/v8 ../src/FPPPulseMesh.cpp
	@echo "  9.5.3      (libhttpserver, const return)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_HAVE_PLAYLIST_INSERTED=1 -DPM_STUB_CONST_RETURN \
	    -Ifppapi/v9 ../src/FPPPulseMesh.cpp
	@echo "  10.0-beta2 (migration window: fpphttp.h shims route the libhttpserver signature)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_HAVE_PLAYLIST_INSERTED=1 \
	    -Ifppapi/v10mw ../src/FPPPulseMesh.cpp
	@echo "  10.0-beta  (plugin API 6: registerPluginApi)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_HAVE_PLAYLIST_INSERTED=1 -DPM_HAVE_NOARG_REGISTER_APIS=1 \
	    -Ifppapi/v10 ../src/FPPPulseMesh.cpp
# Only reachable if the playlistInserted grep misses (every Plugin.h with the
# no-arg registerApis also carries it).  PM_INSERTED_OVERRIDE then empties
# while the base virtual exists — hence the suppressed warning; what must not
# regress is that the route stays unregistered.
	@echo "  10.0-beta  (playlistInserted probe missed: route stays unregistered)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_HAVE_NOARG_REGISTER_APIS=1 \
	    -Wno-inconsistent-missing-override \
	    -Ifppapi/v10 ../src/FPPPulseMesh.cpp
	@echo "  master     (plugin API 6: registerPluginApi, real log.h)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_HAVE_PLAYLIST_INSERTED=1 -DPM_HAVE_NOARG_REGISTER_APIS=1 \
	    -Ifppapi/vmaster ../src/FPPPulseMesh.cpp
	@echo "  8.0        (plugin API has no playlistInserted at all)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_STUB_CONST_RETURN \
	    -Ifppapi/v80 ../src/FPPPulseMesh.cpp
	@echo "plugin API compat: all shapes bind"

glue:
	@python3 glue_lint.py

check: mirror_test compat glue
	./mirror_test

clean:
	rm -f mirror_test
	rm -rf mirror_test.dSYM
	rm -rf fppapi/v80 fppapi/v8 fppapi/v9 fppapi/v10mw fppapi/v10 fppapi/vmaster

.PHONY: all check compat glue clean fppapi-headers
