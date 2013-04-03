
#ifndef QXL_SCREEN_H
#define QXL_SCREEN_H

#include "util/u_transfer.h"
#include "pipe/p_screen.h"

#include "qxl_public.h"

struct qxl_transfer {
   struct pipe_transfer b;
};

struct qxl_texture {
   struct u_resource b;
   int stride;
   struct qxl_winsys_buffer *buffer;
};


static INLINE struct qxl_texture *qxl_texture(struct pipe_resource *resource)
{
   struct qxl_texture *tex = (struct qxl_texture *)resource;
   return tex;
}
struct qxl_screen
{
   struct pipe_screen base;

   struct qxl_winsys *qws;

};


static INLINE struct qxl_screen *
qxl_screen(struct pipe_screen *pscreen)
{
   return (struct qxl_screen *)pscreen;
}

void qxl_screen_texture_functions_init(struct qxl_screen *qs);
#endif
