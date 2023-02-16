/*
 *  linux/drivers/video/fbmem.c
 *
 *  Copyright (C) 1994 Martin Schaller
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <kcl/kcl_drm_fb.h>

#ifndef HAVE_IS_FIRMWARE_FRAMEBUFFER
bool is_firmware_framebuffer(struct apertures_struct *a)
{
	pr_warn_once("%s:enable the runtime pm\n", __func__);
	return false;
}
EXPORT_SYMBOL(is_firmware_framebuffer);
#endif
