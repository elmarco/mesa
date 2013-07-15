#ifndef VIRGL_H
#define VIRGL_H

#include "virgl_winsys.h"
#include "pipe/p_screen.h"
struct virgl_screen {
   struct pipe_screen base;
   struct sw_winsys *winsys;
   struct virgl_winsys *vws;
};


static INLINE struct virgl_screen *
virgl_screen( struct pipe_screen *pipe )
{
   return (struct virgl_screen *)pipe;
}
#endif
