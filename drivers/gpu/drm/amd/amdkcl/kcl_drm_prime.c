// SPDX-License-Identifier: GPL-2.0-only
/*
 *  driver/drm/drm_prime.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
#include <drm/drm_prime.h>

#ifndef HAVE_DRM_GEM_PRIME_HANDLE_TO_FD
int (*_kcl_drm_gem_prime_handle_to_fd)(struct drm_device *dev,
                               struct drm_file *file_priv, uint32_t handle,
                               uint32_t flags,
                               int *prime_fd);
EXPORT_SYMBOL(_kcl_drm_gem_prime_handle_to_fd);

int (*_kcl_drm_gem_prime_fd_to_handle)(struct drm_device *dev,
                               struct drm_file *file_priv, int prime_fd,
                               uint32_t *handle);
EXPORT_SYMBOL(_kcl_drm_gem_prime_fd_to_handle);
#endif

void amdkcl_prime_init(void)
{
#ifndef HAVE_DRM_GEM_PRIME_HANDLE_TO_FD
	_kcl_drm_gem_prime_handle_to_fd = amdkcl_fp_setup("drm_gem_prime_handle_to_fd", NULL);
	_kcl_drm_gem_prime_fd_to_handle = amdkcl_fp_setup("drm_gem_prime_fd_to_handle", NULL);
#endif
}
