#include "util/u_pointer.h"
#include "util/u_memory.h"
#include "util/u_hash_table.h"

#include "graw_object.h"

static unsigned
hash_func(void *key)
{
   intptr_t ip = pointer_to_intptr(key);
   return (unsigned)(ip & 0xffffffff);
}

static int
compare(void *key1, void *key2)
{
   if (key1 < key2)
      return -1;
   if (key1 > key2)
      return 1;
   else
      return 0;
}

static struct util_hash_table *res_hash;
static uint32_t next_handle;

struct graw_object {
   enum graw_object_type type;
   uint32_t handle;
   void *data;
};

struct util_hash_table *graw_init_ctx_hash(void)
{
   struct util_hash_table *ctx_hash;
   ctx_hash = util_hash_table_create(hash_func, compare);
   return ctx_hash;
}

static void graw_free_object(struct grend_context *ctx, struct graw_object *obj)
{
   switch (obj->type) {
   case GRAW_QUERY:
      grend_destroy_query((struct grend_query *)obj->data);
      break;
   case GRAW_SURFACE:
      grend_destroy_surface(ctx, (struct grend_surface *)obj->data);
      break;
   case GRAW_OBJECT_VS:
   case GRAW_OBJECT_FS:
      grend_destroy_shader((struct grend_shader_state *)obj->data);
      break;
   default:
      break;
   }
   free(obj->data);
   free(obj);
}

static enum pipe_error free_cb(void *key, void *value, void *data)
{
   struct graw_object *obj = value;
   struct grend_context *ctx = data;
   graw_free_object(ctx, obj);
   return PIPE_OK;
}

void graw_fini_ctx_hash(struct util_hash_table *ctx_hash, struct grend_context *ctx)
{
   if (!ctx_hash)
      return;
   
   util_hash_table_foreach(ctx_hash, free_cb, ctx);
   util_hash_table_destroy(ctx_hash);
}

void
graw_object_init_resource_hash(void)
{
   if (!res_hash)
      res_hash = util_hash_table_create(hash_func, compare);
}

void graw_object_fini_resource_hash(void)
{
   if (res_hash)
      util_hash_table_destroy(res_hash);
   res_hash = NULL;
}

uint32_t
graw_object_create(struct util_hash_table *handle_hash,
                   void *data, uint32_t length, enum graw_object_type type)
{
   struct graw_object *obj = CALLOC_STRUCT(graw_object);

   if (!obj)
      return 0;
   obj->handle = next_handle++;
   obj->data = data;
   obj->type = type;
   util_hash_table_set(handle_hash, intptr_to_pointer(obj->handle), obj);
   return obj->handle;
}

uint32_t
graw_object_insert(struct util_hash_table *handle_hash,
                   void *data, uint32_t length, uint32_t handle, enum graw_object_type type)
{
   struct graw_object *obj = CALLOC_STRUCT(graw_object);

   if (!obj)
      return 0;
   obj->handle = handle;
   obj->data = data;
   obj->type = type;
   util_hash_table_set(handle_hash, intptr_to_pointer(obj->handle), obj);
   return obj->handle;
}

void
graw_object_destroy(struct grend_context *ctx,
                    struct util_hash_table *handle_hash,
                    uint32_t handle, enum graw_object_type type)
{
   struct graw_object *obj;

   obj = util_hash_table_get(handle_hash, intptr_to_pointer(handle));
   if (!obj)
      return;
   util_hash_table_remove(handle_hash, intptr_to_pointer(handle));
   graw_free_object(ctx, obj);
}

void *graw_object_lookup(struct util_hash_table *handle_hash,
                         uint32_t handle, enum graw_object_type type)
{
   struct graw_object *obj;

   obj = util_hash_table_get(handle_hash, intptr_to_pointer(handle));
   if (!obj) {
      return NULL;
   }

   if (obj->type != type)
      return NULL;
   return obj->data;
}

uint32_t graw_object_assign_handle(void)
{
   return next_handle++;
}

void *graw_insert_resource(void *data, uint32_t length, uint32_t handle)
{
   struct graw_object *obj = CALLOC_STRUCT(graw_object);

   if (!obj)
      return 0;
   obj->handle = handle;
   obj->data = data;
   obj->type = GRAW_RESOURCE;
   util_hash_table_set(res_hash, intptr_to_pointer(obj->handle), obj);
   return obj->handle;
}

void graw_destroy_resource(uint32_t handle)
{
   struct graw_object *obj;

   obj = util_hash_table_get(res_hash, intptr_to_pointer(handle));
   if (!obj)
      return;
   util_hash_table_remove(res_hash, intptr_to_pointer(handle));
   free(obj->data);
   free(obj);
}

void *graw_lookup_resource(uint32_t handle, uint32_t ctx_id)
{
   struct graw_object *obj;
   obj = util_hash_table_get(res_hash, intptr_to_pointer(handle));
   if (!obj)
      return NULL;
   return obj->data;
}
