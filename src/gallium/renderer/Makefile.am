include Makefile.sources

noinst_PROGRAMS=tri

AM_LDFLAGS = $(GLUT_LIBS) $(GLEW_LIBS) $(GL_LIBS)

AM_CFLAGS = \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/drivers \
	-I$(top_srcdir)/include \
	$(DEFINES) \
	$(PIC_FLAGS) \
	$(VISIBILITY_CFLAGS) \
        $(GLUT_CFLAGS)

tri_SOURCES = \
	$(C_SOURCES) \
        graw_util.c \
        graw_renderer.c
