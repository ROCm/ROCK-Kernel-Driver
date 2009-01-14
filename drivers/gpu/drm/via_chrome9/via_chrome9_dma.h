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
#ifndef _VIA_CHROME9_DMA_H_
#define _VIA_CHROME9_DMA_H_

#define MAX_BCI_BUFFER_SIZE (16 * 1024 * 1024)

enum cmd_request_type {
	CM_REQUEST_BCI,
	CM_REQUEST_DMA,
	CM_REQUEST_RB,
	CM_REQUEST_RB_FORCED_DMA,
	CM_REQUEST_NOTAVAILABLE
};

struct cmd_get_space {
	unsigned int            dwRequestSize;
	enum cmd_request_type      hint;
	__volatile__ unsigned int   *pCmdData;
};

struct cmd_release_space {
	unsigned int  dwReleaseSize;
};

extern int via_chrome9_hw_init(struct drm_device *dev,
	struct drm_via_chrome9_init *init);
extern int via_chrome9_ioctl_flush(struct drm_device *dev, void *data,
	struct drm_file *file_priv);
extern int via_chrome9_ioctl_free(struct drm_device *dev, void *data,
	struct drm_file *file_prev);
extern int via_chrome9_ioctl_wait_chip_idle(struct drm_device *dev,
	void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_flush_cache(struct drm_device *dev,
	void *data, struct drm_file *file_priv);
extern int via_chrome9_ioctl_flush(struct drm_device *dev, void *data,
	struct drm_file *file_priv);
extern int via_chrome9_ioctl_free(struct drm_device *dev, void *data,
	struct drm_file *file_priv);
extern unsigned int ProtectSizeValue(unsigned int size);
extern void SetAGPDoubleCmd_inv(struct drm_device *dev);
extern void SetAGPRingCmdRegs_inv(struct drm_device *dev);
extern void via_chrome9_dma_init_inv(struct drm_device *dev);

#endif
