TARGET		:= MUSI00001
TITLE		:= Meu Player MP3
APPID		:= MUSI00001
CONTENTID	:= UP0001-MUSI00001_00-0000000000000000

ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif

include $(PSL1GHT)/ppu_rules

minimp3.h:
	curl -L https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h -o minimp3.h

main.o: main.c minimp3.h

CFLAGS		+= -O2 -Wall -mcpu=cell -mhard-float -fmodulo-sched -ffunction-sections -fdata-sections -I$(PSL1GHT)/ppu/include -I$(PS3DEV)/portlibs/ppu/include
CXXFLAGS	+= $(CFLAGS)
LDFLAGS		+= -L$(PSL1GHT)/ppu/lib -L$(PS3DEV)/portlibs/ppu/lib
LDLIBS		+= -lrsx -lgcm_sys -lio -laudio -lsysutil -lm -lnet -lsysmodule

OBJS		:= main.o

all: $(TARGET).pkg

$(TARGET).elf: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

%.pkg: %.elf
	$(PSL1GHT)/bin/make_self $< $(TARGET).self
	$(PSL1GHT)/bin/pkg.py --contentid $(CONTENTID) --title "$(TITLE)" $(TARGET).pkg $(TARGET).self

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).self $(TARGET).pkg