
noinst_PROGRAMS=graw-renderer
#graw-shm-renderer

AM_LDFLAGS = ../auxiliary/libgallium.la $(GLEW_LIBS) $(GL_LIBS) -lm -ldl

AM_CFLAGS = \
        -I$(top_srcdir)/src/gallium/drivers/rempipe \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/drivers \
	-I$(top_srcdir)/include \
	$(DEFINES) \
	$(PIC_FLAGS) \
	$(VISIBILITY_CFLAGS)

graw_renderer_SOURCES = \
        graw_pipe_renderer.c \
        graw_renderer_glx.c

graw_shm_renderer_SOURCES = \
        graw_shm_renderer.c \
        graw_renderer_glx.c \
        send_scm.c

graw_renderer_LDADD = libgrend.la -lX11
graw_shm_renderer_LDADD = libgrend.la -lrt -lX11

libgrend_la_SOURCES = \
        graw_renderer.c \
        graw_shader.c \
        graw_object.c \
        graw_decode.c \
	graw_cursor.c \
        iov.c

lib_LTLIBRARIES = libgmoney.la
noinst_LTLIBRARIES = libgrend.la

GM_LDFLAGS = -Wl,-Bsymbolic -version-number 1:2 -no-undefined
libgmoney_la_SOURCES = graw_qemu_lib_renderer.c
libgmoney_la_LIBADD = libgrend.la
libgmoney_la_LDFLAGS = $(GM_LDFLAGS)

