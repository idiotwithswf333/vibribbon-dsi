# UC Vib-Ribbon DSi port. Build with devkitARM + libnds (docker image devkitpro/devkitarm), run: make
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set")
endif
include $(DEVKITARM)/ds_rules

TARGET     := vibribbon
GAME_TITLE := UC Vib-Ribbon
GAME_SUBTITLE1 := DSi port
GAME_SUBTITLE2 := static recomp
export GAME_ICON := $(CURDIR)/icon.bmp

ARCH     := -march=armv5te -mtune=arm946e-s
INC      := -I$(DEVKITPRO)/libnds/include -I$(DEVKITPRO)/calico/include -Isrc -Isource
LIBDIRS  := -L$(DEVKITPRO)/libnds/lib -L$(DEVKITPRO)/calico/lib
CXXFLAGS := -g -Wall -O2 $(ARCH) -fomit-frame-pointer -ffunction-sections -fdata-sections \
            -fno-rtti -fno-exceptions -std=gnu++17 -D__NDS__ -DARM9 -DVIB_NDS -Wno-format $(INC)
LDFLAGS  := -specs=ds_arm9.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(TARGET).map
LIBS     := -lfat -lnds9

all: $(TARGET).nds

build/main.o: source/main.cpp $(wildcard src/*.h)
	@mkdir -p build
	$(PREFIX)g++ $(CXXFLAGS) -c $< -o $@

$(TARGET).elf: build/main.o
	$(PREFIX)g++ $(LDFLAGS) $^ $(LIBDIRS) $(LIBS) -o $@

clean:
	rm -rf build $(TARGET).elf $(TARGET).nds $(TARGET).map
.PHONY: all clean
