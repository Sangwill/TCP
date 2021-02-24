.PHONY: all compile clean

ifeq ($(shell which meson),)
    $(error Please install meson(>=0.49.2) first!)
endif

ifeq ($(shell which ninja),)
    $(error Please install ninja first!)
endif

all: builddir compile

builddir:
	meson setup builddir

compile: builddir
	ninja -C builddir

clean: builddir
	ninja -C builddir clean
