
#ifndef GRAW_RENDERER_GLUT
#define GRAW_RENDERER_GLUT

void
graw_renderer_glut_init(int x, int y, int width, int height);

void 
graw_set_display_func( void (*draw)( void ) );

void
graw_main_loop( void );

#endif
