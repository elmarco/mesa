include Makefile.sources

noinst_PROGRAMS=tri quad-tex tri-instanced graw-renderer graw-shm-renderer

AM_LDFLAGS = ../auxiliary/libgallium.a $(GLUT_LIBS) $(GLEW_LIBS) $(GL_LIBS) -lm -ldl

AM_CFLAGS = \
        -I$(top_srcdir)/src/gallium/drivers/rempipe \
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
        graw_renderer_glut.c \
        graw_shader.c \
        graw_object.c \
        graw_encode.c \
        graw_decode.c \
        graw_context.c

quad_tex_SOURCES = \
	quad-tex.c \
        graw_util.c \
        graw_renderer.c \
        graw_renderer_glut.c \
        graw_shader.c \
        graw_object.c \
        graw_encode.c \
        graw_decode.c \
        graw_context.c

tri_SOURCES = \
	$(C_SOURCES) \
        graw_util.c \
        graw_renderer.c \
        graw_renderer_glut.c \
        graw_shader.c \
        graw_object.c \
        graw_encode.c \
        graw_decode.c \
        graw_context.c

graw_renderer_SOURCES = \
        graw_pipe_renderer.c \
        graw_renderer.c \
        graw_shader.c \
        graw_object.c \
        graw_decode.c \
        graw_renderer_glut.c \
        graw_renderer_glx.c

graw_shm_renderer_SOURCES = \
        graw_shm_renderer.c \
        graw_renderer.c \
        graw_shader.c \
        graw_object.c \
        graw_decode.c \
        graw_renderer_glut.c \
        graw_renderer_glx.c

graw_renderer_LDADD = -lX11
graw_shm_renderer_LDADD = -lrt -lX11
