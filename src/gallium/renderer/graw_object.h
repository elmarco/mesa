#ifndef GRAW_OBJECT_H
#define GRAW_OBJECT_H

enum graw_object_type {
   GRAW_OBJECT_BLEND,
   GRAW_OBJECT_RASTERIZER,
   GRAW_OBJECT_DSA,
   GRAW_OBJECT_VS,
   GRAW_OBJECT_FS,
   GRAW_OBJECT_VERTEX_ELEMENTS,
   GRAW_RESOURCE,
   GRAW_SURFACE,
};

void graw_object_init_hash(void);
uint32_t graw_object_create(void *object, uint32_t length, enum graw_object_type obj);
void graw_object_destroy(uint32_t handle);
void *graw_object_lookup(uint32_t handle, enum graw_object_type obj);

#endif
