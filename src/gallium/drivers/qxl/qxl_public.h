#ifndef QXL_PUBLIC_H
#define QXL_PUBLIC_H

struct qxl_winsys;
struct pipe_screen;

struct pipe_screen *qxl_screen_create(struct qxl_winsys *qws);

#endif
