/*
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice
 * (including the next paragraph) shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL VIA, S3 GRAPHICS, AND/OR
 * ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "via_chrome9_drm.h"
#include "via_chrome9_drv.h"
#include "via_chrome9_dma.h"
#include "via_chrome9_mm.h"
#include "via_chrome9_3d_reg.h"

#define RING_BUFFER_INIT_FLAG 1
#define RING_BUFFER_CLEANUP_FLAG 2

static int dri_library_name(struct drm_device *dev, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "via_chrome9");
}

int via_chrome9_drm_authmagic(struct drm_device *dev, void *data,
	struct drm_file *file_priv)
{
	return 0;
}

int via_chrome9_drm_get_pci_id(struct drm_device *dev,
	void *data, struct drm_file *file_priv)
{
	unsigned int *reg_val = data;
	outl(0x8000002C, 0xCF8);
	*reg_val = inl(0xCFC);
	outl(0x8000012C, 0xCF8);
	*(reg_val+1) = inl(0xCFC);
	outl(0x8000022C, 0xCF8);
	*(reg_val+2) = inl(0xCFC);
	outl(0x8000052C, 0xCF8);
	*(reg_val+3) = inl(0xCFC);

    return 0;
}
int via_chrome9_drm_judge(struct drm_device *dev, void *data,
	struct drm_file *file_priv)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;

	if (dev_priv->initialized)
		*(int *)data = 1;
	else
		*(int *)data = -1;
	return 0;
}

int via_chrome9_dma_init(struct drm_device *dev, void *data,
	struct drm_file *file_priv)
{
	int tmp;
	unsigned char sr6c;
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *)dev->dev_private;
	tmp = *((int *)data);

	switch (tmp) {
	case RING_BUFFER_INIT_FLAG:
		via_chrome9_dma_init_inv(dev);
		break;
	case RING_BUFFER_CLEANUP_FLAG:
		if (dev_priv->chip_sub_index == CHIP_H6S2) {
			SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6c);
			sr6c = GetMMIORegisterU8(dev_priv->mmio->handle,
				0x83c5);
			sr6c &= 0x7F;
			SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6c);
		}
	break;
	}
	return 0;
}



struct drm_ioctl_desc via_chrome9_ioctls[] = {
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_INIT, via_chrome9_ioctl_init,
		DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),/* via_chrome9_map.c*/
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_FLUSH, via_chrome9_ioctl_flush, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_FREE, via_chrome9_ioctl_free, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_ALLOCATE_EVENT_TAG,
		via_chrome9_ioctl_allocate_event_tag, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_FREE_EVENT_TAG,
		via_chrome9_ioctl_free_event_tag, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_ALLOCATE_APERTURE,
		via_chrome9_ioctl_allocate_aperture, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_FREE_APERTURE,
		via_chrome9_ioctl_free_aperture, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_ALLOCATE_VIDEO_MEM,
	via_chrome9_ioctl_allocate_mem_wrapper,	DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_FREE_VIDEO_MEM,
		via_chrome9_ioctl_free_mem_wrapper, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_WAIT_CHIP_IDLE,
		via_chrome9_ioctl_wait_chip_idle, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_PROCESS_EXIT,
		via_chrome9_ioctl_process_exit, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_RESTORE_PRIMARY,
		via_chrome9_ioctl_restore_primary, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_FLUSH_CACHE,
		via_chrome9_ioctl_flush_cache, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_ALLOCMEM,
		via_chrome9_ioctl_allocate_mem_base, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_FREEMEM,
		via_chrome9_ioctl_freemem_base, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_CHECKVIDMEMSIZE,
		via_chrome9_ioctl_check_vidmem_size, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_PCIEMEMCTRL,
		via_chrome9_ioctl_pciemem_ctrl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_AUTH_MAGIC, via_chrome9_drm_authmagic, 0),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_GET_PCI_ID,
		via_chrome9_drm_get_pci_id, 0),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_INIT_JUDGE, via_chrome9_drm_judge, 0),
	DRM_IOCTL_DEF(DRM_VIA_CHROME9_DMA, via_chrome9_dma_init, 0)
};

int via_chrome9_max_ioctl = DRM_ARRAY_SIZE(via_chrome9_ioctls);

static struct pci_device_id pciidlist[] = {
	{0x1106, 0x3225, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x3230, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VIA_CHROME9_DX9_0},
	{0x1106, 0x3371, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x1122, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VIA_CHROME9_PCIE_GROUP},
	{0x1106, 0x5122, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VIA_CHROME9_PCIE_GROUP},
	{0, 0, 0}
};

int  via_chrome9_driver_open(struct drm_device *dev,
	struct drm_file *priv)
{
	priv->authenticated = 1;
	return 0;
}

static struct drm_driver driver = {
	.driver_features = DRIVER_USE_AGP | DRIVER_REQUIRE_AGP |
		DRIVER_HAVE_DMA | DRIVER_FB_DMA | DRIVER_USE_MTRR,
	.open = via_chrome9_driver_open,
	.load = via_chrome9_driver_load,
	.unload = via_chrome9_driver_unload,
	.device_is_agp = via_chrome9_is_agp,
	.dri_library_name = dri_library_name,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.reclaim_buffers_locked = NULL,
	.reclaim_buffers_idlelocked = via_chrome9_reclaim_buffers_locked,
	.lastclose = via_chrome9_lastclose,
	.preclose = via_chrome9_preclose,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.ioctls = via_chrome9_ioctls,
	.fops = {
		 .owner = THIS_MODULE,
		 .open = drm_open,
		 .release = drm_release,
		 .ioctl = drm_ioctl,
		 .mmap = drm_mmap,
		 .poll = drm_poll,
		 .fasync = drm_fasync,
	},
	.pci_driver = {
		 .name = DRIVER_NAME,
		 .id_table = pciidlist,
		 .resume = via_chrome9_drm_resume,
		 .suspend = via_chrome9_drm_suspend,
	},

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int __init via_chrome9_init(void)
{
	driver.num_ioctls = via_chrome9_max_ioctl;
#if VIA_CHROME9_VERIFY_ENABLE
	via_chrome9_init_command_verifier();
	DRM_INFO("via_chrome9 verify function enabled. \n");
#endif
	driver.dev_priv_size = sizeof(struct drm_via_chrome9_private);
	return drm_init(&driver);
}

static void __exit via_chrome9_exit(void)
{
	drm_exit(&driver);
}

module_init(via_chrome9_init);
module_exit(via_chrome9_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
