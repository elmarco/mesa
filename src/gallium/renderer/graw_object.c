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

static struct util_hash_table *handle_hash, *res_hash;
static uint32_t next_handle;

struct graw_object {
   enum graw_object_type type;
   uint32_t handle;
   void *data;
};

void
graw_object_init_hash(void)
{
   if (!handle_hash)
      handle_hash = util_hash_table_create(hash_func, compare);

   if (!res_hash)
      res_hash = util_hash_table_create(hash_func, compare);
}

void graw_object_fini_hash(void)
{
   if (handle_hash)
      util_hash_table_destroy(handle_hash);
   handle_hash = NULL;
   if (res_hash)
      util_hash_table_destroy(res_hash);
   res_hash = NULL;
}

uint32_t
graw_object_create(void *data, uint32_t length, enum graw_object_type type)
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
graw_object_insert(void *data, uint32_t length, uint32_t handle, enum graw_object_type type)
{
   struct graw_object *obj = CALLOC_STRUCT(graw_object);

   if (!obj)
      return 0;
   obj->handle = handle;
   obj->data = data;
   obj->type = type;
   if (type == GRAW_RESOURCE)
      util_hash_table_set(res_hash, intptr_to_pointer(obj->handle), obj);
   else
      util_hash_table_set(handle_hash, intptr_to_pointer(obj->handle), obj);
   return obj->handle;
}

void
graw_object_destroy(uint32_t handle, enum graw_object_type type)
{
   struct graw_object *obj;

   if (type == GRAW_RESOURCE) {
      obj = util_hash_table_get(res_hash, intptr_to_pointer(handle));
      if (!obj)
         return;
      util_hash_table_remove(res_hash, intptr_to_pointer(handle));
   } else {
      obj = util_hash_table_get(handle_hash, intptr_to_pointer(handle));
      if (!obj)
         return;
      util_hash_table_remove(handle_hash, intptr_to_pointer(handle));
   }
   free(obj);
      
}

void *graw_object_lookup(uint32_t handle, enum graw_object_type type)
{
   struct graw_object *obj;

   if (type == GRAW_RESOURCE)
      obj = util_hash_table_get(res_hash, intptr_to_pointer(handle));
   else
      obj = util_hash_table_get(handle_hash, intptr_to_pointer(handle));
   if (!obj) {
      assert(0);
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
