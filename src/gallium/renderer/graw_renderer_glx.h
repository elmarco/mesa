
#ifndef GRAW_RENDERER_GLX
#define GRAW_RENDERER_GLX
void graw_renderer_init_glx(int localrender);
void graw_renderer_fini_glx(void);
int process_x_event(void);
int swap_buffers(void);

#endif
