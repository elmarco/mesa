include Makefile.sources

noinst_PROGRAMS=tri quad-tex tri-instanced

AM_LDFLAGS = ../auxiliary/libgallium.a $(GLUT_LIBS) $(GLEW_LIBS) $(GL_LIBS) -lm -ldl

AM_CFLAGS = \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/drivers \
	-I$(top_srcdir)/include \
	$(DEFINES) \
	$(PIC_FLAGS) \
	$(VISIBILITY_CFLAGS) \
        $(GLUT_CFLAGS)

tri_instanced_SOURCES = \
	tri-instanced.c \
        graw_util.c \
        graw_renderer.c \
        graw_shader.c \
        graw_object.c \
        graw_encode.c \
        graw_decode.c \
        graw_context.c
quad_tex_SOURCES = \
	quad-tex.c \
        graw_util.c \
        graw_renderer.c \
        graw_shader.c \
        graw_object.c \
        graw_encode.c \
        graw_decode.c \
        graw_context.c

tri_SOURCES = \
	$(C_SOURCES) \
        graw_util.c \
        graw_renderer.c \
        graw_shader.c \
        graw_object.c \
        graw_encode.c \
        graw_decode.c \
        graw_context.c
