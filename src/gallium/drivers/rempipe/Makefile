TOP = ../../../..
include $(TOP)/configs/current

LIBNAME = rempipe

C_SOURCES = \
	graw_encode.c \
	graw_context.c \
        rempipe_screen.c \
        graw_drm_winsys.c

CFLAGS += $(LIBDRM_CFLAGS)
include ../../Makefile.template
