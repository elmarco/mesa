
#ifndef RP_PUBLIC_H
#define RP_PUBLIC_H

struct pipe_screen;
struct sw_winsys;
struct qxl_winsys;

struct pipe_screen *
rempipe_create_screen(struct sw_winsys *winsys);

struct pipe_screen *
rempipe_create_qxl_screen(struct qxl_winsys *qws);
#endif
