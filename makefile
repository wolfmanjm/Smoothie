#!/usr/bin/make

DIRS = mbed src
DIRSCLEAN = $(addsuffix .clean,$(DIRS))

all:
	@echo "#define BUILD_VERSION_STRING \"`git symbolic-ref HEAD 2> /dev/null | cut -b 12-`-`git log --pretty=format:\"%h\" -1`\"" > src/build_version.h
	@echo "#define BUILD_DATE_STRING __DATE__ \" \" __TIME__" >> src/build_version.h
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

