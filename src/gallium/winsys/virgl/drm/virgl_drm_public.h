#ifndef VIRGL_DRM_PUBLIC_H
#define VIRGL_DRM_PUBLIC_H

struct virgl_winsys;

struct virgl_winsys *virgl_drm_winsys_create(int drmFD);

#endif
