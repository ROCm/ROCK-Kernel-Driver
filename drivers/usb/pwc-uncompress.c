/* Linux driver for Philips webcam 
   Decompression frontend.
   (C) 1999-2001 Nemosoft Unv. (webcam@smcc.demon.nl)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/*
   This is where the decompression routines register and unregister 
   themselves. It also has a decompressor wrapper function.
*/

#include "ccvt.h"
#include "vcvt.h"
#include "pwc.h"
#include "pwc-uncompress.h"


/* This contains a list of all registered decompressors */
LIST_HEAD(pwc_decompressor_list);

/* Should the pwc_decompress structure ever change, we increase the 
   version number so that we don't get nasty surprises, or can 
   dynamicly adjust our structure.
 */
const int pwc_decompressor_version = PWC_MAJOR;

/* Add decompressor to list, ignoring duplicates */
void pwc_register_decompressor(struct pwc_decompressor *pwcd)
{
	if (pwc_find_decompressor(pwcd->type) == NULL) {
		Debug("Adding decompressor for model %d.\n", pwcd->type);
		list_add_tail(&pwcd->pwcd_list, &pwc_decompressor_list);
	}
}

/* Remove decompressor from list */
void pwc_unregister_decompressor(int type)
{
	struct pwc_decompressor *find;
	
	find = pwc_find_decompressor(type);
	if (find != NULL) {
		Debug("Removing decompressor for model %d.\n", type);
		list_del(&find->pwcd_list);
	}
}

/* Find decompressor in list */
struct pwc_decompressor *pwc_find_decompressor(int type)
{
	struct list_head *tmp;
	struct pwc_decompressor *pwcd;

	list_for_each(tmp, &pwc_decompressor_list) {
		pwcd  = list_entry(tmp, struct pwc_decompressor, pwcd_list);
		if (pwcd->type == type)
			return pwcd;
	}
	return NULL;
}



int pwc_decompress(struct pwc_device *pdev)
{
	struct pwc_frame_buf *fbuf;
	int n, l, c, w;
	void *yuv, *image, *dst;
	
	if (pdev == NULL)
		return -EFAULT;
#if defined(__KERNEL__) && defined(PWC_MAGIC)
	if (pdev->magic != PWC_MAGIC) {
		Err("pwc_decompress(): magic failed.\n");
		return -EFAULT;
	}
#endif

	fbuf = pdev->read_frame;
	if (fbuf == NULL)
		return -EFAULT;
	image = pdev->image_ptr[pdev->fill_image];
	if (!image)
		return -EFAULT;

#if PWC_DEBUG
	/* This is a quickie */
	if (pdev->vpalette == VIDEO_PALETTE_RAW) {
		memcpy(image, fbuf->data, pdev->frame_size);
		return 0;
	}
#endif

	/* Compressed formats are decompressed in decompress_buffer, then 
	 * transformed into the desired format 
	 */
	yuv = pdev->decompress_buffer;
	n = 0;
	if (pdev->vbandlength == 0) { /* uncompressed */
		yuv = fbuf->data + pdev->frame_header_size;  /* Skip header */
	}
	else {
		if (pdev->decompressor)
			n = pdev->decompressor->decompress(pdev->decompress_data, pdev->image.x, pdev->image.y, pdev->vbandlength, yuv, fbuf->data + pdev->frame_header_size, 0);
		else
			n = -ENXIO; /* No such device or address: missing decompressor */
	}
	if (n < 0) {
		Err("Error in decompression engine: %d\n", n);
		return n;
	}

	/* At this point 'yuv' always points to the uncompressed, non-scaled YUV420I data */
	if (pdev->image.x == pdev->view.x && pdev->image.y == pdev->view.y) {
		/* Sizes matches; make it quick */
		switch(pdev->vpalette) {
		case VIDEO_PALETTE_RGB24 | 0x80:
			ccvt_420i_rgb24(pdev->image.x, pdev->image.y, yuv, image);
			break;
		case VIDEO_PALETTE_RGB24:
			ccvt_420i_bgr24(pdev->image.x, pdev->image.y, yuv, image);
			break;
		case VIDEO_PALETTE_RGB32 | 0x80:
			ccvt_420i_rgb32(pdev->image.x, pdev->image.y, yuv, image);
			break;
		case VIDEO_PALETTE_RGB32:
			ccvt_420i_bgr32(pdev->image.x, pdev->image.y, yuv, image);
			break;
		case VIDEO_PALETTE_YUYV:
		case VIDEO_PALETTE_YUV422:
			ccvt_420i_yuyv(pdev->image.x, pdev->image.y, yuv, image); 
			break;
		case VIDEO_PALETTE_YUV420:
			memcpy(image, yuv, pdev->image.size);
			break;
		case VIDEO_PALETTE_YUV420P:
			n = pdev->image.x * pdev->image.y;
			ccvt_420i_420p(pdev->image.x, pdev->image.y, yuv, image, image + n, image + n + (n / 4));
			break;
		}
	}
	else {
 		/* Size mismatch; use viewport conversion routines */
	 	switch(pdev->vpalette) {
 		case VIDEO_PALETTE_RGB24 | 0x80:
 			vcvt_420i_rgb24(pdev->image.x, pdev->image.y, pdev->view.x, yuv, image + pdev->offset.size);
	 		break;
 		case VIDEO_PALETTE_RGB24:
 			vcvt_420i_bgr24(pdev->image.x, pdev->image.y, pdev->view.x, yuv, image + pdev->offset.size);
	 		break;
 		case VIDEO_PALETTE_RGB32 | 0x80:
 			vcvt_420i_rgb32(pdev->image.x, pdev->image.y, pdev->view.x, yuv, image + pdev->offset.size);
	 		break;
 		case VIDEO_PALETTE_RGB32:
 			vcvt_420i_bgr32(pdev->image.x, pdev->image.y, pdev->view.x, yuv, image + pdev->offset.size);
 			break;
		case VIDEO_PALETTE_YUYV:
		case VIDEO_PALETTE_YUV422:
			vcvt_420i_yuyv(pdev->image.x, pdev->image.y, pdev->view.x, yuv, image + pdev->offset.size);
			break;
		case VIDEO_PALETTE_YUV420:
			dst = image + pdev->offset.size;
			w = pdev->view.x * 6;
			c = pdev->image.x * 6;
			for (l = 0; l < pdev->image.y; l++) {
				memcpy(dst, yuv, c);
				dst += w;
				yuv += c;
			}
			break;
		case VIDEO_PALETTE_YUV420P:
			n = pdev->view.x * pdev->view.y;
			vcvt_420i_420p(pdev->image.x, pdev->image.y, pdev->view.x, yuv,
					image +             pdev->offset.size,
					image + n +         pdev->offset.size / 4,
					image + n + n / 4 + pdev->offset.size / 4);
			break;
 		}
 	}
	return 0;
}


/* wrapper functions.
   By using these wrapper functions and exporting them with no VERSIONING,
   I can be sure the pwcx.o module will load on all systems.
*/

void *pwc_kmalloc(size_t size, int priority)
{
	return kmalloc(size, priority);
}

void pwc_kfree(const void *pointer)
{
	kfree(pointer);
}

/* Make sure these functions are available for the decompressor plugin
   both when this code is compiled into the kernel or as as module.
   We are using unversioned names!
 */
EXPORT_SYMBOL_NOVERS(pwc_decompressor_version);
EXPORT_SYMBOL_NOVERS(pwc_register_decompressor);
EXPORT_SYMBOL_NOVERS(pwc_unregister_decompressor);
EXPORT_SYMBOL_NOVERS(pwc_find_decompressor);
EXPORT_SYMBOL_NOVERS(pwc_kmalloc);
EXPORT_SYMBOL_NOVERS(pwc_kfree);
