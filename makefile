#!/usr/bin/make

DIRS = mbed src
DIRSCLEAN = $(addsuffix .clean,$(DIRS))

all:
	@generate-version.sh
	@echo Building mbed SDK
	@ $(MAKE) -C mbed
	@echo Building Smoothie
	@ $(MAKE) -C src

realclean: $(DIRSCLEAN)

clean: 
	@echo Cleaning $*
	@ $(MAKE) -C src clean
	
$(DIRSCLEAN): %.clean:
	@echo Cleaning $*
	@ $(MAKE) -C $*  clean

debug-store:
	@ $(MAKE) -C src debug-store

flash:
	@ $(MAKE) -C src flash

dfu:
	@ $(MAKE) -C src dfu

upload:
	@ $(MAKE) -C src upload

debug:
	@ $(MAKE) -C src debug

console:
	@ $(MAKE) -C src console

.PHONY: all $(DIRS) $(DIRSCLEAN) debug-store flash upload debug console dfu
