
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include <stdio.h>
#include "graw_renderer_glx.h"

void graw_renderer_init_glx(void)
{
   Display *dpy;
   int scrnum;
   Window root;
   Window win;
   GLXContext ctx;
   XVisualInfo *visinfo;
   int attribs[64];
   int i = 0;
   char *displayName = NULL;
   XSetWindowAttributes attr;
   unsigned long mask;

   dpy = XOpenDisplay(displayName);

   root = RootWindow(dpy, scrnum);

   attribs[i++] = GLX_RGBA;
   attribs[i++] = GLX_DOUBLEBUFFER;
   attribs[i++] = GLX_RED_SIZE;
   attribs[i++] = 1;
   attribs[i++] = GLX_GREEN_SIZE;
   attribs[i++] = 1;
   attribs[i++] = GLX_BLUE_SIZE;
   attribs[i++] = 1;
   attribs[i++] = GLX_DEPTH_SIZE;
   attribs[i++] = 1;
   attribs[i++] = None;

   visinfo = glXChooseVisual(dpy, scrnum, attribs);
   if (!visinfo) {
      fprintf(stderr,"no suitable visual\n");
      return;
   }
      
   ctx = glXCreateContext( dpy, visinfo, NULL, True );
   if (!ctx) {
      fprintf(stderr,"Error: glXCreateContext failed\n");
      return;
   }

   attr.background_pixel = 0;
   attr.border_pixel = 0;
   attr.colormap = XCreateColormap(dpy, root, visinfo->visual, AllocNone);
   attr.event_mask = StructureNotifyMask | ExposureMask;
   mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
   win = XCreateWindow(dpy, root, 0, 0, 1, 1, 
		       0, visinfo->depth, InputOutput,
		       visinfo->visual, mask, &attr);

   glXMakeCurrent(dpy, win, ctx);
}

