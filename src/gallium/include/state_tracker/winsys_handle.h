
#ifndef _WINSYS_HANDLE_H_
#define _WINSYS_HANDLE_H_

#ifdef __cplusplus
extern "C" {
#endif

#define WINSYS_HANDLE_TYPE_SHARED 0
#define WINSYS_HANDLE_TYPE_KMS    1
#define WINSYS_HANDLE_TYPE_FD     2

/**
 * For use with pipe_screen::{texture_from_handle|texture_get_handle}.
 */
struct winsys_handle
{
   /**
    * Input for texture_from_handle, valid values are
    * WINSYS_HANDLE_TYPE_SHARED or WINSYS_HANDLE_TYPE_FD.
    * Input to texture_get_handle,
    * to select handle for kms, flink, or prime.
    */
   unsigned type;
   /**
    * Input to texture_from_handle.
    * Output for texture_get_handle.
    */
   unsigned handle;
   /**
    * Input to texture_from_handle.
    * Output for texture_get_handle.
    */
   unsigned stride;
};

#ifdef __cplusplus
}
#endif

#endif /* _WINSYS_HANDLE_H_ */
