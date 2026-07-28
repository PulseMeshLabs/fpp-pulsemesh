# Host tests for the pending-insert mirror.  No FPP tree required — the state
# machine in ../src/PendingInsertMirror.h is deliberately dependency-free so it
# can be proven right before it is loaded into a show box.
#
#   make -C test check

CXX ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror

FPP_SRC ?= $(HOME)/projects/fpp

# The plugin's REAL translation unit, compiled against the REAL FPP plugin
# headers from both API shapes.  Not a copy of the override set — a copy would
# drift, and drift is exactly the failure being checked for: a virtual whose
# signature no longer matches its base is silently not an override, and the
# mirror would then be fed by nothing at all.
#
# Third-party libraries FPP itself pulls in (jsoncpp, libhttpserver, drogon)
# are stubbed; everything under fppapi/v8 and fppapi/vmaster is verbatim FPP.
# See fppapi/PROVENANCE.
COMPAT_FLAGS = -std=c++17 -fsyntax-only -Wall -Wextra -Werror -Wno-unused-parameter \
               -Ifppapi/stub -I../src

all: mirror_test

mirror_test: mirror_test.cpp ../src/PendingInsertMirror.h
	$(CXX) $(CXXFLAGS) -o $@ mirror_test.cpp

# Four builds: {8.5.1, master} x {const, non-const render_GET return}.  The
# plugin derives that return type from the base with decltype rather than
# asserting it, and this is what proves the derivation binds either way.
compat: ../src/FPPPulseMesh.cpp ../src/PendingInsertMirror.h
	@echo "  8.5.1   (libhttpserver, const return)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_HAVE_PLAYLIST_INSERTED=1 -DPM_STUB_CONST_RETURN \
	    -Ifppapi/v8 ../src/FPPPulseMesh.cpp
	@echo "  8.5.1   (libhttpserver, plain return)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_HAVE_PLAYLIST_INSERTED=1 \
	    -Ifppapi/v8 ../src/FPPPulseMesh.cpp
	@echo "  master  (drogon shim, real fpphttp.h)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_HAVE_PLAYLIST_INSERTED=1 \
	    -Ifppapi/vmaster ../src/FPPPulseMesh.cpp
	@echo "  8.0     (plugin API has no playlistInserted at all)"
	@$(CXX) $(COMPAT_FLAGS) -DPM_STUB_CONST_RETURN \
	    -Ifppapi/v80 ../src/FPPPulseMesh.cpp
	@echo "plugin API compat: all shapes bind"

glue:
	@python3 glue_lint.py

check: mirror_test compat glue
	./mirror_test

refresh-fppapi:
	git -C $(FPP_SRC) show 8.0:src/Plugin.h > fppapi/v80/Plugin.h
	git -C $(FPP_SRC) show 8.5.1:src/Plugin.h > fppapi/v8/Plugin.h
	git -C $(FPP_SRC) show master:src/Plugin.h > fppapi/vmaster/Plugin.h
	git -C $(FPP_SRC) show master:src/fpphttp.h > fppapi/vmaster/fpphttp.h

clean:
	rm -f mirror_test
	rm -rf mirror_test.dSYM

.PHONY: all check compat glue clean refresh-fppapi
