
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

libvrend_la_SOURCES = \
        vrend_renderer.c \
        vrend_shader.c \
        vrend_object.c \
        vrend_decode.c \
        vrend_formats.c \
	vrend_blitter.c \
        iov.c \
        vrend_renderer_helper.c \
	virgl_egl_context.c

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = virglrenderer.pc

lib_LTLIBRARIES = libvirglrenderer.la
noinst_LTLIBRARIES = libvrend.la

GM_LDFLAGS = -Wl,-Bsymbolic -version-number 0:1 -no-undefined

libvirglrenderer_la_SOURCES = virglrenderer.c
libvirglrenderer_ladir = $(libdir)
libvirglrenderer_la_LIBADD = libvrend.la
libvirglrenderer_la_LDFLAGS = $(GM_LDFLAGS) $(EPOXY_LDFLAGS)

if HAVE_MESA_LLVM
libvirglrenderer_la_LDFLAGS += $(LLVM_LDFLAGS)
libvirglrenderer_la_LIBADD += $(LLVM_LIBS)
endif


libvirglrendererincludedir = ${includedir}
libvirglrendererinclude_HEADERS = virglrenderer.h virgl_helper.h
