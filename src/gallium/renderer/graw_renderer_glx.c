
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include <stdio.h>
#include "graw_renderer_glx.h"

Display *graw_dpy;
Window graw_win;
GLXContext graw_ctx;

int inited;
void graw_renderer_fini_glx(void)
{
   glXMakeCurrent(graw_dpy, None, NULL);
   glXDestroyContext(graw_dpy, graw_ctx);
   XDestroyWindow(graw_dpy, graw_win);
   XCloseDisplay(graw_dpy);
   inited = 0;
}

void graw_renderer_init_glx(int localrender)
{
   int scrnum;
   Window root;
   XVisualInfo *visinfo;
   int attribs[64];
   int i = 0;
   char *displayName = NULL;
   XSetWindowAttributes attr;
   unsigned long mask;

   if (inited)
      return;
   graw_dpy = XOpenDisplay(displayName);

   scrnum = DefaultScreen(graw_dpy);
   root = RootWindow(graw_dpy, scrnum);

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

   visinfo = glXChooseVisual(graw_dpy, scrnum, attribs);
   if (!visinfo) {
      fprintf(stderr,"no suitable visual\n");
      return;
   }
      
   graw_ctx = glXCreateContext( graw_dpy, visinfo, NULL, True );
   if (!graw_ctx) {
      fprintf(stderr,"Error: glXCreateContext failed\n");
      return;
   }

   attr.background_pixel = 0;
   attr.border_pixel = 0;
   attr.colormap = XCreateColormap(graw_dpy, root, visinfo->visual, AllocNone);
   attr.event_mask = StructureNotifyMask | ExposureMask;
   mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
   graw_win = XCreateWindow(graw_dpy, root, 0, 0, 1024, 768,
		       0, visinfo->depth, InputOutput,
		       visinfo->visual, mask, &attr);

   glXMakeCurrent(graw_dpy, graw_win, graw_ctx);
   inited = 1;

   if (localrender) {
      XMapWindow(graw_dpy, graw_win);
   }
}

int process_x_event(void)
{
   if (!graw_dpy)
      return;
   if (XPending(graw_dpy))
   {
      XEvent event;
      XNextEvent(graw_dpy, &event);
   }
}

int swap_buffers(void)
{
   glXSwapBuffers(graw_dpy, graw_win);
}
