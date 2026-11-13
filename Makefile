.SUFFIXES:

ifeq ($(strip $(PSL1GHT)),)
$(error "Defina PSL1GHT no ambiente")
endif

include $(PSL1GHT)/ppu_rules
.RECIPEPREFIX = >

TARGET    := flipdiving
BUILD     := build
SOURCE    := source
INCLUDE   := include
DATA      := data

TITLE     := Salto Mortal PS3
APPID     := FLIP00001
CONTENTID := UP0001-$(APPID)_00-0000000000000000

SFOXML       ?= $(PSL1GHT)/data/sfo.xml
SFO          ?= $(PSL1GHT)/bin/sfo.py
PKG          ?= $(PSL1GHT)/bin/pkg.py
SELF_NPDRM   ?= $(PSL1GHT)/bin/make_self_npdrm

CFLAGS    = -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDES)
CXXFLAGS  = $(CFLAGS)
LDFLAGS   = $(MACHDEP) -Wl,-Map,$(notdir $@).map
LIBS      := -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm
LIBDIRS   := $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export VPATH    := $(foreach dir,$(SOURCE),$(CURDIR)/$(dir))
export BUILDDIR := $(CURDIR)/$(BUILD)
export DEPSDIR  := $(BUILDDIR)

CFILES   := $(foreach dir,$(SOURCE),$(notdir $(wildcard $(dir)/*.c)))
export OFILES   := $(CFILES:.c=.o)
export INCLUDES := $(foreach dir,$(INCLUDE),-I$(CURDIR)/$(dir)) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export LD       := $(CC)

ICON0 := $(if $(wildcard $(CURDIR)/ICON0.PNG),$(CURDIR)/ICON0.PNG,$(PSL1GHT)/data/ICON0.PNG)

.PHONY: $(BUILD) clean pkg

$(BUILD):
> @[ -d $@ ] || mkdir -p $@
> @$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
> @echo limpando ...
> @rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT).pkg

pkg: $(BUILD)
> @echo Criando PKG ...
> @mkdir -p $(BUILDDIR)/pkg/USRDIR
> @cp $(ICON0) $(BUILDDIR)/pkg/ICON0.PNG
> @$(SELF_NPDRM) $(OUTPUT).elf $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN $(CONTENTID) >> /dev/null
> @$(SFO) --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) $(BUILDDIR)/pkg/PARAM.SFO
> @$(PKG) --contentid $(CONTENTID) $(BUILDDIR)/pkg/ $(OUTPUT).pkg >> /dev/null

else

DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)

endif