#ifndef VIRGL_VTEST_PUBLIC_H
#define VIRGL_VTEST_PUBLIC_H

struct virgl_winsys;

struct virgl_winsys *virgl_vtest_winsys_wrap(struct sw_winsys *sws);

#endif
