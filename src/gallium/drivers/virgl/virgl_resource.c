#include "util/u_inlines.h"
#include "virgl_resource.h"
#include "virgl_context.h"

static struct pipe_resource *virgl_resource_create(struct pipe_screen *screen,
                                                   const struct pipe_resource *templ)
{
    struct virgl_screen *vs = (struct virgl_screen *)screen;
    if (templ->target == PIPE_BUFFER)
        return virgl_buffer_create(vs, templ);
    else
        return virgl_texture_create(vs, templ);
}

static struct pipe_resource *virgl_resource_from_handle(struct pipe_screen *screen,
                                                        const struct pipe_resource *templ,
                                                        struct winsys_handle *whandle)
{
    struct virgl_screen *vs = (struct virgl_screen *)screen;
    if (templ->target == PIPE_BUFFER)
        return NULL;
    else
        return virgl_texture_from_handle(vs, templ, whandle);
}

void virgl_init_screen_resource_functions(struct pipe_screen *screen)
{
    screen->resource_create = virgl_resource_create;
    screen->resource_from_handle = virgl_resource_from_handle;
    screen->resource_get_handle = u_resource_get_handle_vtbl;
    screen->resource_destroy = u_resource_destroy_vtbl;
}

void virgl_init_context_resource_functions(struct pipe_context *ctx)
{
    ctx->transfer_map = u_transfer_map_vtbl;
    ctx->transfer_flush_region = u_transfer_flush_region_vtbl;
    ctx->transfer_unmap = u_transfer_unmap_vtbl;
    ctx->transfer_inline_write = u_transfer_inline_write_vtbl;
}
