SRC = $(shell find ./ -name '*.c')
OBJ = ../divinus
ifneq ($(filter divinus-owned divinus-owned-fixture,$(MAKECMDGOALS)),)
GIT_REV := b47abc428d6058731fbc256fe73e05e118d61326
else
GIT_REV := $(shell git rev-parse --short HEAD)$(shell git diff --quiet HEAD || echo "-mod")
endif

.PHONY: clean divinus
divinus: $(OBJ)

$(OBJ): $(SRC:%.c=%.o)
	$(CC) $^ -rdynamic $(OPT) -DGIT_REV=\"$(GIT_REV)\" -o $@

%.o: %.c Makefile
	$(CC) -c $< $(OPT) -DGIT_REV=\"$(GIT_REV)\" -o $@

clean:
	rm -rf $(SRC:%.c=%.o) $(OBJ)

# Explicit encoded-input application; no HAL/SDK source, downloader or activation.
PYTHON ?= python3
OWNED_DEPS ?= ../third_party/nt98513-owned-capture
ifneq ($(filter divinus-owned divinus-owned-fixture,$(MAKECMDGOALS)),)
ifneq ($(realpath $(OWNED_DEPS)),$(realpath ../third_party/nt98513-owned-capture))
$(error OWNED_DEPS must select the in-repository checked dependency tree)
endif
endif
OWNED_OUT ?= ../owned-build
OWNED_COMMON = main.c owned_input.c owned_config.c rtsp/rtsp.c rtsp/rtp.c rtsp/mime.c rtsp/owned_rtcp.c
OWNED_SESSION = $(OWNED_DEPS)/src/nt98513_capture_session.c $(OWNED_DEPS)/src/nt98513_capture_session_posix.c $(OWNED_DEPS)/src/nt98513_divinus_bridge.c $(OWNED_DEPS)/src/nt98513_annexb.c
OWNED_LDFLAGS ?= -Wl,--gc-sections
OWNED_FLAGS = -include $(OWNED_OUT)/owned_build_id.h -std=gnu11 -D_GNU_SOURCE -DDIVINUS_OWNED_INPUT -I. -I$(OWNED_DEPS)/include -pthread -O2 -g -ffunction-sections -fdata-sections
.PHONY: divinus-owned divinus-owned-fixture
divinus-owned: $(OWNED_OUT)/owned_build_id.h
	$(CC) $(OWNED_FLAGS) $(OWNED_OPT) $(OWNED_COMMON) owned_capture.c $(OWNED_SESSION) $(OWNED_LDFLAGS) -o $(OWNED_OUT)/divinus-owned
divinus-owned-fixture: $(OWNED_OUT)/owned_build_id.h
	$(CC) $(OWNED_FLAGS) $(OWNED_OPT) -include ../tests/capture_device_boundary.h -c $(OWNED_DEPS)/src/nt98513_capture_session_posix.c -o $(OWNED_OUT)/posix-device-fixture.o
	$(CC) $(OWNED_FLAGS) $(OWNED_OPT) $(OWNED_COMMON) owned_capture.c ../tests/owned_device_fixture.c $(filter-out $(OWNED_DEPS)/src/nt98513_capture_session_posix.c,$(OWNED_SESSION)) $(OWNED_OUT)/posix-device-fixture.o $(OWNED_TEST_EXTRA) $(OWNED_LDFLAGS) -o $(OWNED_OUT)/divinus-owned-fixture

# One prerequisite per make invocation; atomic rename prevents partial headers
# even when independent builds regenerate the same source identity.
.PHONY: owned-identity-force
owned-identity-force:
$(OWNED_OUT)/owned_build_id.h: owned-identity-force
	mkdir -p $(OWNED_OUT)
	@tmp=$$(mktemp "$(OWNED_OUT)/owned-id.XXXXXX"); \
	trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
	$(PYTHON) ../tools/owned_source_id.py > "$$tmp" && mv "$$tmp" "$@"
