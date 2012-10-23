#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glut.h>

#include "graw_renderer_glut.h"

static void Reshape(int width, int height)
{

}

static void key_esc(unsigned char key, int x, int y)
{
   if (key == 27) exit(0);
}

void
graw_renderer_glut_init(int x, int y, int width, int height)
{
   static int glut_inited;
   int argc = 0;

   if (!glut_inited) {
      glut_inited = 1;
      glutInit(&argc, NULL);
   }

   glutInitWindowSize(width, height);
 
   glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);

   glutCreateWindow("test");

   glutReshapeFunc(Reshape);
   glutKeyboardFunc(key_esc);
}

void 
graw_set_display_func( void (*draw)( void ) )
{
  glutDisplayFunc(draw);
}


void
graw_main_loop( void )
{
   glutMainLoop();
}

