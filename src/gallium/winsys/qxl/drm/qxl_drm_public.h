#ifndef QXL_DRM_PUBLIC_H
#define QXL_DRM_PUBLIC_H

struct qxl_winsys;

struct qxl_winsys *qxl_drm_winsys_create(int drmFD);

#endif
