
#ifndef VIRGL_PUBLIC_H
#define VIRGL_PUBLIC_H

struct pipe_screen;
struct sw_winsys;
struct virgl_winsys;

struct pipe_screen *
virgl_create_screen(struct virgl_winsys *vws);
#endif
