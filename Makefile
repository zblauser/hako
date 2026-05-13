# HAKO — minimal cross-platform build with icon
#
# Windows: icon embedded into .exe via windres (real OS icon).
# macOS:   icon attached via Rez/SetFile if Xcode CLT installed (best-effort);
#          otherwise icon/hako.icns is shipped alongside.
# Linux:   ELF can't embed icons; icon/hako.png is shipped alongside for use
#          with a .desktop entry.

CC      ?= gcc
CFLAGS  ?= -O2 -Wall
LDLIBS  ?= -lpthread

ICON_DIR = icon
BIN      = hako

ifeq ($(OS),Windows_NT)
    PLATFORM = windows
    BIN     := hako.exe
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        PLATFORM = macos
    else
        PLATFORM = linux
    endif
endif

.PHONY: all clean icons
all: $(BIN)

# ---------- icons ----------
# Regenerate icon/hako.{icns,ico,png} from icon/hako.svg.
# Requires rsvg-convert or ImageMagick. iconutil (macOS) → .icns, magick → .ico.
icons:
	@cd $(ICON_DIR) && bash build-icons.sh

# ---------- Windows: embed icon via resource (optional — skip if .ico missing) ----------
ifeq ($(PLATFORM),windows)

HAS_ICO := $(wildcard $(ICON_DIR)/hako.ico)

ifeq ($(HAS_ICO),)
$(BIN): hako.c
	$(CC) $(CFLAGS) hako.c -o $@ $(LDLIBS)
else
hako.rc:
	@printf 'IDI_ICON1 ICON "$(ICON_DIR)/hako.ico"\n' > $@

hako.res: hako.rc $(ICON_DIR)/hako.ico
	windres $< -O coff -o $@

$(BIN): hako.c hako.res
	$(CC) $(CFLAGS) hako.c hako.res -o $@ $(LDLIBS)
endif

endif

# ---------- macOS: build, then attach icon if tools exist ----------
ifeq ($(PLATFORM),macos)

$(BIN): hako.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)
	@if command -v Rez >/dev/null 2>&1 && command -v SetFile >/dev/null 2>&1; then \
		printf 'read %c%s%c (-16455) "%s/hako.icns";\n' "'" "icns" "'" "$(ICON_DIR)" > .hako.r; \
		Rez -append .hako.r -o $(BIN) && SetFile -a C $(BIN) && \
		echo "icon attached to $(BIN)" || echo "icon attach failed (non-fatal)"; \
		rm -f .hako.r; \
	else \
		echo "Rez/SetFile not found — install Xcode CLT to attach icon (xcode-select --install)"; \
	fi

endif

# ---------- Linux: plain build, icon shipped alongside ----------
ifeq ($(PLATFORM),linux)

$(BIN): hako.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)
	@echo "built $(BIN). For a desktop icon: copy $(ICON_DIR)/hako.png to ~/.local/share/icons/ and create a .desktop entry."

endif

clean:
	rm -f hako hako.exe hako.rc hako.res .hako.r
