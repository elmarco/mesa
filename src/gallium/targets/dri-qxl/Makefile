TOP = ../../../..
include $(TOP)/configs/current

LIBNAME = qxl_dri.so

PIPE_DRIVERS = \
	$(TOP)/src/gallium/drivers/qxl/libqxl.a \
	$(TOP)/src/gallium/state_trackers/dri/drm/libdridrm.a \
	$(TOP)/src/gallium/winsys/qxl/drm/libqxlwinsys.a \
	$(TOP)/src/gallium/drivers/trace/libtrace.a \
	$(TOP)/src/gallium/drivers/rbug/librbug.a \
	$(TOP)/src/gallium/drivers/noop/libnoop.a \
        $(TOP)/src/gallium/winsys/sw/wrapper/libwsw.a \
        $(TOP)/src/gallium/drivers/softpipe/libsoftpipe.a \
        $(TOP)/src/gallium/drivers/rempipe/librempipe.a 

C_SOURCES = \
	target.c \
	$(COMMON_GALLIUM_SOURCES) \
	$(DRIVER_SOURCES)

DRIVER_DEFINES = \
	-DGALLIUM_RBUG -DGALLIUM_TRACE -DGALLIUM_NOOP -DGALLIUM_SOFTPIPE -DGALLIUM_REMPIPE

include ../Makefile.dri

symlinks:
