SRCDIR ?= /opt/fpp/src
include $(SRCDIR)/makefiles/common/setup.mk
include $(SRCDIR)/makefiles/platform/*.mk

all: libfpp-PulseMesh.$(SHLIB_EXT)
debug: all


OBJECTS_fpp_PulseMesh_so += src/FPPPulseMesh.o
LIBS_fpp_PulseMesh_so += -L$(SRCDIR) -lfpp -ljsoncpp -lcurl
CXXFLAGS_src/FPPPulseMesh.o += -I$(SRCDIR)

# The `playlistInserted` plugin callback (plan-16 §13.1's pending-insert
# mirror) was added to FPP on 2024-12-07 and first shipped in 8.5.  It is
# absent from 8.0 and 7.0, which pluginInfo.json still declares support for.
# Read the header actually being compiled against rather than parsing a version
# string: the answer needed is "does this build's plugin API carry it", and the
# header is that answer.  Without it the mirror compiles away and the connector
# reports pending_insert_introspection honestly false.
ifneq ($(shell grep -c playlistInserted $(SRCDIR)/Plugin.h 2>/dev/null),0)
CXXFLAGS_src/FPPPulseMesh.o += -DPM_HAVE_PLAYLIST_INSERTED=1
endif

# FPP 10.0 added a no-argument registerApis() and demoted the libhttpserver-
# shaped one to a [[deprecated]] shim that raises a UI warning per registered
# route.  Same probe discipline as above: grep the header for the new virtual
# itself, not a version string.
ifneq ($(shell grep -c 'virtual void registerApis()' $(SRCDIR)/Plugin.h 2>/dev/null),0)
CXXFLAGS_src/FPPPulseMesh.o += -DPM_HAVE_NOARG_REGISTER_APIS=1
# the native path calls drogon::app() from the plugin; same pair fpp_so.mk links
LIBS_fpp_PulseMesh_so += -ldrogon -ltrantor
endif


%.o: %.cpp Makefile $(wildcard src/*.h)
	$(CCACHE) $(CC) $(CFLAGS) $(CXXFLAGS) $(CXXFLAGS_$@) -c $< -o $@

libfpp-PulseMesh.$(SHLIB_EXT): $(OBJECTS_fpp_PulseMesh_so) $(SRCDIR)/libfpp.$(SHLIB_EXT)
	$(CCACHE) $(CC) -shared $(CFLAGS_$@) $(OBJECTS_fpp_PulseMesh_so) $(LIBS_fpp_PulseMesh_so) $(LDFLAGS) -o $@

clean:
	rm -f libfpp-PulseMesh.$(SHLIB_EXT) $(OBJECTS_fpp_PulseMesh_so)
