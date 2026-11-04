#---------------------------------------------------------------------------------
# R4 PokéLink: DSi-mode tool that copies an R4's Pokémon saves to the 3DS SD card.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules

TARGET   := R4PokeLink
BUILD    := build
SOURCES  := source
INCLUDES := include
DATA     :=

GAME_TITLE     := R4 PokéLink
GAME_SUBTITLE1 := Copies R4 saves to the 3DS SD
GAME_SUBTITLE2 := by En3rGonix

ARCH := -march=armv5te -mtune=arm946e-s

CFLAGS   := -g -Wall -O2 -ffunction-sections -fdata-sections $(ARCH) $(INCLUDE) -DARM9
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lfat -lnds9

LIBDIRS := $(LIBNDS) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH  := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD := $(CC)
export OFILES := $(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds

else

# A DSi system title (00030005-5234504C "R4PL"): the 3DS HOME menu does not show those, so PKForge can
# install it out of sight. SCFG mask and access flags as TWiLight Menu++'s booter: Slot-1 and SD stay ours.
$(OUTPUT).nds: $(OUTPUT).elf
	$(SILENTCMD)ndstool -c $@ -9 $< $(_ARM7_ELF) -b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1);$(GAME_SUBTITLE2)" \
		-g R4PL 01 "R4POKELINK" -z 80040000 -u 00030005 -a 00000138
	$(SILENTMSG) built ... $(notdir $@)
$(OUTPUT).elf: $(OFILES)

-include $(DEPSDIR)/*.d

endif
