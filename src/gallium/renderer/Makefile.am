
AM_LDFLAGS = ../auxiliary/libgallium.la $(EPOXY_LIBS) $(GL_LIBS) -lgbm -lm -ldl

AM_CFLAGS = \
        -I$(top_srcdir)/src/gallium/drivers/virgl \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/drivers \
	-I$(top_srcdir)/include \
	$(DEFINES) \
	$(PIC_FLAGS) \
	$(LIBDRM_CFLAGS) \
	$(EPOXY_CFLAGS) \
	$(VISIBILITY_CFLAGS)

libgrend_la_SOURCES = \
        graw_renderer.c \
        graw_shader.c \
        vrend_object.c \
        graw_decode.c \
        graw_formats.c \
        iov.c \
        graw_renderer_helper.c \
	virglrenderer.c \
	virgl_egl_context.c

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = virglrenderer.pc

lib_LTLIBRARIES = libvirglrenderer.la
noinst_LTLIBRARIES = libgrend.la

GM_LDFLAGS = -Wl,-Bsymbolic -version-number 0:1 -no-undefined

libvirglrenderer_la_SOURCES = graw_virtio_lib_renderer.c
libvirglrenderer_ladir = $(libdir)
libvirglrenderer_la_LIBADD = libgrend.la
libvirglrenderer_la_LDFLAGS = $(GM_LDFLAGS) $(EPOXY_LDFLAGS)

if HAVE_MESA_LLVM
libvirglrenderer_la_LDFLAGS += $(LLVM_LDFLAGS)
libvirglrenderer_la_LIBADD += $(LLVM_LIBS)
endif


libvirglrendererincludedir = ${includedir}
libvirglrendererinclude_HEADERS = virglrenderer.h virgl_helper.h
