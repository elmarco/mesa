/**************************************************************************
 *
 * Copyright (C) 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/* create our own EGL offscreen rendering context via gbm and rendernodes */


/* if we are using EGL and rendernodes then we talk via file descriptors to the remote
   node */
#define EGL_EGLEXT_PROTOTYPES
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <epoxy/egl.h>
#include <gbm.h>
#include <xf86drm.h>
#include "virglrenderer.h"
#include "virgl_egl.h"

#include "virgl_hw.h"
struct virgl_egl {
    int fd;
    struct gbm_device *gbm_dev;
    EGLDisplay egl_display;
    EGLConfig egl_conf;
    EGLContext egl_ctx;
    bool have_mesa_drm_image;
    bool have_mesa_dma_buf_img_export;
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

static bool virgl_egl_has_extension_in_string(const char *haystack, const char *needle)
{
        const unsigned needle_len = strlen(needle);

        if (needle_len == 0)
                return false;

        while (true) {
                const char *const s = strstr(haystack, needle);

                if (s == NULL)
                        return false;

                if (s[needle_len] == ' ' || s[needle_len] == '\0') {
                        return true;
                }

                /* strstr found an extension whose name begins with
                 * needle, but whose name is not equal to needle.
                 * Restart the search at s + needle_len so that we
                 * don't just find the same extension again and go
                 * into an infinite loop.
                 */
                haystack = s + needle_len;
        }

        return false;
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
        const char *extension_list;
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

        extension_list = eglQueryString(d->egl_display, EGL_EXTENSIONS);
	fprintf(stderr, "EGL major/minor: %d.%d\n", major, minor);
	fprintf(stderr, "EGL version: %s\n",
		eglQueryString(d->egl_display, EGL_VERSION));
	fprintf(stderr, "EGL vendor: %s\n",
		eglQueryString(d->egl_display, EGL_VENDOR));
        fprintf(stderr, "EGL extensions: %s\n", extension_list);

        /* require surfaceless context */
        if (!virgl_egl_has_extension_in_string(extension_list, "EGL_KHR_surfaceless_context"))
            goto fail;

	d->have_mesa_drm_image = false;
	d->have_mesa_dma_buf_img_export = false;
        if (virgl_egl_has_extension_in_string(extension_list, "EGL_MESA_drm_image"))
           d->have_mesa_drm_image = true;

        if (virgl_egl_has_extension_in_string(extension_list, "EGL_MESA_image_dma_buf_export"))
           d->have_mesa_dma_buf_img_export = true;

        if (d->have_mesa_drm_image == false && d->have_mesa_dma_buf_img_export == false) {
           fprintf(stderr, "failed to find drm image extensions\n");
           goto fail;
        }

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

virgl_renderer_gl_context virgl_egl_create_context(struct virgl_egl *ve, struct virgl_gl_ctx_param *vparams)
{
    EGLContext eglctx;
    EGLint ctx_att[] = {
        EGL_CONTEXT_CLIENT_VERSION, vparams->major_ver,
        EGL_CONTEXT_MINOR_VERSION_KHR, vparams->minor_ver,
	EGL_NONE
    };
    eglctx = eglCreateContext(ve->egl_display,
			      ve->egl_conf,
                              vparams->shared ? eglGetCurrentContext() : EGL_NO_CONTEXT,
			      ctx_att);
    return (virgl_renderer_gl_context)eglctx;
}

void virgl_egl_destroy_context(struct virgl_egl *ve, virgl_renderer_gl_context virglctx)
{
    EGLContext eglctx = (EGLContext)virglctx;
    eglDestroyContext(ve->egl_display, eglctx);
}

int virgl_egl_make_context_current(struct virgl_egl *ve, virgl_renderer_gl_context virglctx)
{
    EGLContext eglctx = (EGLContext)virglctx;

    return eglMakeCurrent(ve->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                             eglctx);
}

virgl_renderer_gl_context virgl_egl_get_current_context(struct virgl_egl *ve)
{
   EGLContext eglctx = eglGetCurrentContext();
   return (virgl_renderer_gl_context)eglctx;
}

int virgl_egl_get_fd_for_texture(struct virgl_egl *ve, uint32_t tex_id, int *fd)
{
   EGLImageKHR image;
   EGLint stride;
   EGLBoolean b;

   image = eglCreateImageKHR(ve->egl_display, eglGetCurrentContext(), EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)(unsigned long)tex_id, NULL);

   if (!image)
      return -1;

   if (ve->have_mesa_dma_buf_img_export) {
#ifdef EGL_MESA_image_dma_buf_export
	b = eglExportDMABUFImageMESA(ve->egl_display,
				     image,
				     fd,
				     &stride);
#else
        return -1;
#endif
   } else {
#ifdef EGL_MESA_drm_image
       EGLint handle;
       int r;
       b = eglExportDRMImageMESA(ve->egl_display,
                                 image,
                                 NULL, &handle,
                                 &stride);

       if (!b)
           return -1;

       fprintf(stderr,"image exported %d %d\n", handle, stride);

       r = drmPrimeHandleToFD(ve->fd, handle, DRM_CLOEXEC, fd);
       if (r < 0)
           return -1;
#else
       return -1;
#endif
   }
   return 0;
}

uint32_t virgl_egl_get_gbm_format(uint32_t format)
{
   switch (format) {
   case VIRGL_FORMAT_B8G8R8X8_UNORM:
      return GBM_FORMAT_XBGR8888;
   case VIRGL_FORMAT_B8G8R8A8_UNORM:
      return GBM_FORMAT_ABGR8888;
   default:
      fprintf(stderr, "unknown format to convert to GBM %d\n", format);
      return 0;
   }
}
