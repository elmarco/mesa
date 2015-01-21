#ifndef VIRGL_EGL_H
#define VIRGL_EGL_H

struct virgl_egl;

struct virgl_egl *virgl_egl_init(void);
void virgl_egl_destroy(struct virgl_egl *ve);

virgl_gl_context virgl_egl_create_context(struct virgl_egl *ve);
void virgl_egl_destroy_context(struct virgl_egl *ve, virgl_gl_context virglctx);
int virgl_egl_make_context_current(struct virgl_egl *ve, virgl_gl_context virglctx);
virgl_gl_context virgl_egl_get_current_context(struct virgl_egl *ve);

#endif
