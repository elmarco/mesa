#ifndef VREND_BLITTER
#define VREND_BLITTER

/* shaders for blitting */

static const char *vs_passthrough = {
   "#version 130\n"
   "in vec4 arg0;\n"
   "in vec4 arg1;\n"
   "out vec4 tc;\n"
   "void main() {\n"
   "   gl_Position = arg0;\n"
   "   tc = arg1;\n"
   "}\n"
};

static const char *fs_texfetch_ds = {
   "#version 130\n"
   "uniform sampler%s samp;\n"
   "in vec4 tc;\n"
   "void main() {\n"
   "   gl_FragDepth = float(texture(samp, tc%s).x);\n"
   "}\n"
};
   
#endif
