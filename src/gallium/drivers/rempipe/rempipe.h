#ifndef REM_PIPE_H
#define REM_PIPE_H

struct rempipe_screen {
   struct pipe_screen base;
   struct sw_winsys *winsys;

};


static INLINE struct rempipe_screen *
rempipe_screen( struct pipe_screen *pipe )
{
   return (struct rempipe_screen *)pipe;
}
#endif
