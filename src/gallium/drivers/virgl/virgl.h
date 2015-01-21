#ifndef VIRGL_H
#define VIRGL_H

#include "../winsys/virgl/drm/virgl_hw.h"

#include "virgl_winsys.h"
#include "pipe/p_screen.h"
struct virgl_screen {
   struct pipe_screen base;
   struct sw_winsys *winsys;
   struct virgl_winsys *vws;

   struct virgl_drm_caps caps;
};


static INLINE struct virgl_screen *
virgl_screen( struct pipe_screen *pipe )
{
   return (struct virgl_screen *)pipe;
}
#endif
