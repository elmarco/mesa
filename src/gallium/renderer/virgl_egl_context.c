/* create our own EGL offscreen rendering context via gbm and rendernodes */

/* if we are using EGL and rendernodes then we talk via file descriptors to the remote
   node */

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>

#include "virglrenderer.h"
#include "virgl_egl.h"

struct virgl_egl {
    int fd;
    struct gbm_device *gbm_dev;
    EGLDisplay egl_display;
    EGLConfig egl_conf;
    EGLContext egl_ctx;
};

static int egl_rendernode_open(void)
{
    DIR *dir;
    struct dirent *e;
    int r, fd;
    char *p;
    dir = opendir("/dev/dri");
    if (!dir)
	return -1;

    fd = -1;
    while ((e = readdir(dir))) {
	if (e->d_type != DT_CHR)
	    continue;

	if (strncmp(e->d_name, "renderD", 7))
	    continue;

	r = asprintf(&p, "/dev/dri/%s", e->d_name);
	if (r < 0)
	    return -1;
	
	r = open(p, O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
	if (r < 0){
	    free(p);
	    continue;
	}
	fd = r;
	fprintf(stderr, "using render node %s\n", p);
	free(p);
	break;
    }
    
    if (fd < 0)
	return -1;
    return fd;
}

struct virgl_egl *virgl_egl_init(void)
{
	static const EGLint conf_att[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_NONE,
	};
	static const EGLint ctx_att[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLBoolean b;
	EGLenum api;
	EGLint major, minor, n;
        struct virgl_egl *d;

        d = malloc(sizeof(struct virgl_egl));
        if (!d)
           return NULL;
           
	d->fd = egl_rendernode_open();
	d->gbm_dev = gbm_create_device(d->fd);
	if (!d->gbm_dev)
	    goto fail;

	d->egl_display = eglGetDisplay((EGLNativeDisplayType)d->gbm_dev);
	if (!d->egl_display)
	    goto fail;

	b = eglInitialize(d->egl_display, &major, &minor);
	if (!b)
	    goto fail;

	fprintf(stderr, "EGL major/minor: %d.%d\n", major, minor);
	fprintf(stderr, "EGL version: %s\n",
		eglQueryString(d->egl_display, EGL_VERSION));
	fprintf(stderr, "EGL vendor: %s\n",
		eglQueryString(d->egl_display, EGL_VENDOR));
	fprintf(stderr, "EGL extensions: %s\n",
		eglQueryString(d->egl_display, EGL_EXTENSIONS));
		
	api = EGL_OPENGL_API;
	b = eglBindAPI(api);
	if (!b)
	    goto fail;

	b = eglChooseConfig(d->egl_display, conf_att, &d->egl_conf,
			    1, &n);

	if (!b || n != 1)
	    goto fail;

	d->egl_ctx = eglCreateContext(d->egl_display,
					    d->egl_conf,
					    EGL_NO_CONTEXT,
					    ctx_att);
	if (!d->egl_ctx)
	    goto fail;


	eglMakeCurrent(d->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       d->egl_ctx);
	return d;
 fail:
        free(d);
        return NULL;
}

void virgl_egl_destroy(struct virgl_egl *d)
{
	eglMakeCurrent(d->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
	eglDestroyContext(d->egl_display, d->egl_ctx);
	eglTerminate(d->egl_display);
	gbm_device_destroy(d->gbm_dev);
	close(d->fd);
        free(d);
}

virgl_gl_context virgl_egl_create_context(struct virgl_egl *ve)
{
    EGLContext eglctx;
    static const EGLint ctx_att[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
    };
    eglctx = eglCreateContext(ve->egl_display,
			      ve->egl_conf,
			      EGL_NO_CONTEXT,
			      ctx_att);
    return (virgl_gl_context)eglctx;
}

void virgl_egl_destroy_context(struct virgl_egl *ve, virgl_gl_context virglctx)
{
    EGLContext eglctx = (EGLContext)virglctx;
    eglDestroyContext(ve->egl_display, eglctx);
}

int virgl_egl_make_context_current(struct virgl_egl *ve, virgl_gl_context virglctx)
{
    EGLContext eglctx = (EGLContext)virglctx;

    return eglMakeCurrent(ve->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                             eglctx);
}

virgl_gl_context virgl_egl_get_current_context(struct virgl_egl *ve)
{
   EGLContext eglctx = eglGetCurrentContext();
   return (virgl_gl_context)eglctx;
}
