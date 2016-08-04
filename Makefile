#Required libs:	
#-gstreamer-plugins-bad
# gstreamer -libav
#ensure PKG_CONF_PATH is correct for your system

OBJNAME = dualpipeline
FILENAME = $(OBJNAME).c
PKG_CONF_PATH = /usr/lib/x86_64-linux-gnu/pkgconfig


make:
	export PKG_CONFIG_PATH=$(PKG_CONF_PATH)	
	gcc -Wall $(FILENAME) -o $(OBJNAME) `pkg-config --cflags --libs gstreamer-1.0`

