#Required libs:	
#-gstreamer-plugins-bad
# gstreamer -libav
#ensure PKG_CONF_PATH is correct for your system

OBJNAME = dualpipeline
FILENAME = $(OBJNAME).c
PKG_CONF_PATH = /usr/x86_64-linux-gnu/pkgconfig

.PHONY: make_rel make_dbg

make:
	export PKG_CONFIG_PATH=$(PKG_CONF_PATH)	
	gcc -Wall $(FILENAME) -o $(OBJNAME) `pkg-config --cflags --libs gstreamer-1.0`

