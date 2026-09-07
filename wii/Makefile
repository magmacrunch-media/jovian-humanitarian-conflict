#---------------------------------------------------------------------------------
# jovian -- a magnolia game
#---------------------------------------------------------------------------------
.SUFFIXES:
.SECONDARY:
#---------------------------------------------------------------------------------
# The host tests deliberately need no cross-compiler: requiring devkitPPC to run
# them would put them out of reach on the machine where they are most useful.
ifeq ($(filter test,$(MAKECMDGOALS)),)
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules
endif

#---------------------------------------------------------------------------------
# The magnolia engine. Checked out beside this repo -- the documented layout, and
# what a fresh clone gets -- or under an engines/ group one level up. Override with
# `make MAGNOLIA=<path>` for anywhere else.
#
# The override has to be RELATIVE to this directory. devkitPro's rules build
# include paths as $(TOPDIR)/$(dir), which only composes with a relative one, so
# an absolute path gets past the guard below and then fails at `magnolia.h: No
# such file or directory` instead.
MAGNOLIA    ?= $(firstword $(wildcard ../../magnolia ../../../engines/magnolia))
# Only a real build needs the engine present. The host tests link this game's own
# sources, and CI runs `make test` from a single checkout with no magnolia beside
# it -- so this must not fire there. Mirrors the DEVKITPPC guard above.
ifeq ($(filter test,$(MAKECMDGOALS)),)
ifeq ($(strip $(MAGNOLIA)),)
$(error magnolia not found. Expected ../../magnolia or ../../../engines/magnolia; set MAGNOLIA=<path>)
endif
endif
#---------------------------------------------------------------------------------
TARGET      := jovian
BUILD       := build
SOURCES     := source $(MAGNOLIA)/source $(MAGNOLIA)/font
DATA        :=
INCLUDES    := $(MAGNOLIA) $(MAGNOLIA)/source $(MAGNOLIA)/font source

CFLAGS      = -g -O2 -Wall $(MACHDEP) $(INCLUDE)
CXXFLAGS    = $(CFLAGS)
LDFLAGS     = -g $(MACHDEP) -Wl,-Map,$(notdir $@).map

LIBS        := -lgrrlib -lpngu -lfreetype -lpng -ljpeg -lz -lbrotlidec -lbrotlicommon -lbz2 -lfat -lasnd -lwiiuse -lbte -logc -lm
LIBDIRS     := $(PORTLIBS)

#---------------------------------------------------------------------------------
ifeq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

TOPDIR := $(CURDIR)/..

export OUTPUT   := $(CURDIR)/$(TARGET)
export VPATH    := $(foreach dir,$(SOURCES),$(TOPDIR)/$(dir))
export DEPSDIR  := $(CURDIR)

# Sprites and audio are linked into the binary rather than read from SD. Assets on
# a card can go missing, go stale, or disagree with the code -- all three have
# happened elsewhere in this org. bin2s emits one assembly blob plus a header of
# symbols; -a 32 matches ASND's DMA alignment so audio plays straight out of the
# image with no copy.
#
# There are no assets yet -- this game draws everything and the track lives on the
# website's jukebox, see ../AGENTS.md -- so the rule below writes an empty blob
# rather than invoking bin2s with no inputs, which errors out.
BIN2S       := $(DEVKITPRO)/tools/bin/bin2s
ASSETFILES  := $(wildcard $(TOPDIR)/sprites/*.png) $(wildcard $(TOPDIR)/audio/*.pcm)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(TOPDIR)/$(dir)/*.cpp)))

ifeq ($(strip $(CPPFILES)),)
    export LD  := $(CC)
else
    export LD  := $(CXX)
endif

export OFILES   := assets.o $(addsuffix .o,$(basename $(CPPFILES) $(CFILES)))
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(TOPDIR)/$(dir)) \
                   -I$(PORTLIBS_PATH)/ppc/include/freetype2 \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR) -I$(CURDIR)/$(BUILD) -I$(LIBOGC_INC)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) -L$(LIBOGC_LIB)

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

assets.s: $(ASSETFILES)
ifeq ($(strip $(ASSETFILES)),)
	@echo "no assets yet -- writing an empty blob"
	@echo "" > assets.s
	@echo "/* no assets yet */" > assets.h
else
	@echo "embedding $(words $(ASSETFILES)) assets ..."
	@$(BIN2S) -a 32 -H assets.h $(ASSETFILES) > assets.s
	@echo "" >> assets.s
endif

assets.h: assets.s

assets.o: assets.s
	@$(AS) -o $@ assets.s

# Every translation unit may include assets.h, so it must exist first.
$(filter-out assets.o,$(OFILES)): assets.h

-include $(DEPENDS)

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

.PHONY: $(BUILD) clean all deploy dolphin card wii test

# Stated explicitly: the host-test rule below would otherwise be the first
# target in the file, and a bare `make` would run the tests instead of
# building the game.
.DEFAULT_GOAL := all

# Host-side tests. Anything in source/ that is free of libogc can be tested on
# the machine you are sitting at, in a second, with no emulator in the loop --
# which is where game rules belong, because a rule that is subtly wrong looks
# exactly like a rule that is right until someone plays far enough to notice.
#
# TESTDEPS is the list of translation units that must stay engine-free. Adding a
# file here is a promise about that file; adding a GRRLIB call to one of them
# costs the entire suite.
HOSTCC   ?= cc
TESTSRC  ?= $(wildcard tests/*.c)
TESTDEPS ?= source/sim.c source/projection.c

test:
	@mkdir -p $(BUILD)
	@$(HOSTCC) -Wall -Wextra -I source -o $(BUILD)/test $(TESTSRC) $(TESTDEPS) -lm
	@$(BUILD)/test

all: $(BUILD)

# The wildcard in MAGNOLIA above is written against THIS directory, and make
# re-invokes itself with -C build, where the working directory is one level
# deeper and both candidates miss. Passing the resolved value down is what stops
# the guard firing with the engine sitting exactly where it says it looked.
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile MAGNOLIA=$(MAGNOLIA)

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).dol sdcard

# Stages a Homebrew Channel app directory. Assets are in the binary, so this is
# boot.dol and meta.xml -- but the directory is cleared rather than merged,
# because a leftover file from an older layout is exactly what makes a card
# ambiguous to debug.
SDROOT := sdcard/apps/$(TARGET)
deploy: $(BUILD)
	@rm -rf $(SDROOT)
	@mkdir -p $(SDROOT)
	@cp $(BUILD)/$(TARGET).dol $(SDROOT)/boot.dol
	@cp meta.xml $(SDROOT)/meta.xml
	@echo "Staged $(SDROOT)/ -- copy sdcard/ to the card root"

# Pushes the staged directory to the folder Dolphin reads as its SD card.
# No-ops harmlessly on a machine without /mnt/c.
DOLPHIN_SD ?= /mnt/c/Dolphin/sdcard
dolphin: deploy
	@if [ -d "$(dir $(DOLPHIN_SD))" ]; then \
	    rm -rf "$(DOLPHIN_SD)/apps/$(TARGET)"; \
	    mkdir -p "$(DOLPHIN_SD)/apps/$(TARGET)"; \
	    cp -r $(SDROOT)/. "$(DOLPHIN_SD)/apps/$(TARGET)/"; \
	    echo "Synced to $(DOLPHIN_SD)/apps/$(TARGET)/"; \
	else \
	    echo "No $(DOLPHIN_SD) here -- skipping"; \
	fi
	@echo ""
	@echo "NOTE: this clears the app directory, which deletes saved scores and"
	@echo "settings with it. Expected on a dev loop; do not mistake it for the"
	@echo "game failing to save."

# --- onto real hardware -------------------------------------------------------
#
#   make card SD=/mnt/e   install onto a mounted SD card (the permanent route)
#   make wii              send this build to a waiting console over the network
#
# `card` takes a mount point because an SD card is removable and its drive
# letter moves. In WSL, a card showing as E: is /mnt/e.
#
# Unlike `dolphin`, this MERGES rather than clearing. On the emulator a wiped
# app directory is a clean slate; on a card it is somebody's high scores.
card: deploy
	@if [ -z "$(SD)" ]; then \
	    echo "Set SD to the card's mount point:"; \
	    echo "    make card SD=/mnt/e"; \
	    echo ""; \
	    echo "Or copy $(SDROOT)/ to <card>/apps/$(TARGET)/ yourself."; \
	    exit 1; \
	fi
	@if [ ! -d "$(SD)" ]; then \
	    echo "No such path: $(SD)"; \
	    echo "Is the card mounted? In WSL a card at E: is /mnt/e."; \
	    exit 1; \
	fi
	@mkdir -p "$(SD)/apps/$(TARGET)"
	@cp $(SDROOT)/boot.dol "$(SD)/apps/$(TARGET)/boot.dol"
	@cp $(SDROOT)/meta.xml "$(SD)/apps/$(TARGET)/meta.xml"
	@echo "Installed to $(SD)/apps/$(TARGET)/"
	@echo "Saves in that directory were left alone."
	@echo "Eject the card; it appears in the Homebrew Channel as the meta.xml name."

# Sends the .dol straight to a console and runs it, without installing anything.
# The console has to be sitting on the Homebrew Channel's netloader screen --
# open the channel and press Home -- and it prints its own IP address there.
wii: $(BUILD)
	@if [ -z "$(WIILOAD)" ]; then \
	    echo "wiiload needs the console's address:"; \
	    echo "    make wii WIILOAD=tcp:192.168.1.50"; \
	    echo ""; \
	    echo "Open the Homebrew Channel, press Home for the netloader screen,"; \
	    echo "and use the IP it shows. Export WIILOAD to avoid retyping it."; \
	    exit 1; \
	fi
	@WIILOAD=$(WIILOAD) $(DEVKITPRO)/tools/bin/wiiload $(BUILD)/$(TARGET).dol
	@echo "Sent $(BUILD)/$(TARGET).dol to $(WIILOAD) -- it runs immediately."
	@echo "Nothing was installed; use 'make card' for that."

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
