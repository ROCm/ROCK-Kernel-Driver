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
#include "via_chrome9_mm.h"
#include "via_chrome9_dma.h"
#include "via_chrome9_3d_reg.h"

#define VIA_CHROME9DRM_VIDEO_STARTADDRESS_ALIGNMENT 10

void *via_chrome9_dev_v4l;
void *via_chrome9_filepriv_v4l;

void __via_chrome9ke_udelay(unsigned long usecs)
{
	unsigned long start;
	unsigned long stop;
	unsigned long period;
	unsigned long wait_period;
	struct timespec tval;

#ifdef NDELAY_LIMIT
#define UDELAY_LIMIT    (NDELAY_LIMIT/1000) /* supposed to be 10 msec */
#else
#define UDELAY_LIMIT    (10000)             /* 10 msec */
#endif

	if (usecs > UDELAY_LIMIT) {
		start = jiffies;
		tval.tv_sec = usecs / 1000000;
		tval.tv_nsec = (usecs - tval.tv_sec * 1000000) * 1000;
		wait_period = timespec_to_jiffies(&tval);
		do {
			stop = jiffies;

			if (stop < start)
				period = ((unsigned long)-1 - start) + stop + 1;
			else
				period = stop - start;

		} while (period < wait_period);
	} else
		udelay(usecs);  /* delay value might get checked once again */
}

int via_chrome9_ioctl_process_exit(struct drm_device *dev, void *data,
	struct drm_file *file_priv)
{
	return 0;
}

int via_chrome9_ioctl_restore_primary(struct drm_device *dev,
	void *data, struct drm_file *file_priv)
{
	return 0;
}

void Initialize3DEngine(struct drm_via_chrome9_private *dev_priv)
{
	int i;
	unsigned int StageOfTexture;

	if (dev_priv->chip_sub_index == CHIP_H5 ||
		dev_priv->chip_sub_index == CHIP_H5S1) {
		SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			0x00010000);

		for (i = 0; i <= 0x8A; i++) {
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(unsigned int) i << 24);
		}

		/* Initial Texture Stage Setting*/
		for (StageOfTexture = 0; StageOfTexture < 0xf;
		StageOfTexture++) {
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
				(0x00020000 | 0x00000000 |
				(StageOfTexture & 0xf)<<24));
		/*  *((unsigned int volatile*)(pMapIOPort+HC_REG_TRANS_SET)) =
		(0x00020000 | HC_ParaSubType_Tex0 | (StageOfTexture &
		0xf)<<24);*/
			for (i = 0 ; i <= 0x30 ; i++) {
				SetMMIORegister(dev_priv->mmio->handle,
				0x440, (unsigned int) i << 24);
			}
		}

		/* Initial Texture Sampler Setting*/
		for (StageOfTexture = 0; StageOfTexture < 0xf;
		StageOfTexture++) {
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
				(0x00020000 | 0x00020000 |
				(StageOfTexture & 0xf)<<24));
			/* *((unsigned int volatile*)(pMapIOPort+
			HC_REG_TRANS_SET)) = (0x00020000 | 0x00020000 |
			( StageOfTexture & 0xf)<<24);*/
			for (i = 0 ; i <= 0x30 ; i++) {
				SetMMIORegister(dev_priv->mmio->handle,
				0x440, (unsigned int) i << 24);
			}
		}

		SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			(0x00020000 | 0xfe000000));
		/* *((unsigned int volatile*)(pMapIOPort+HC_REG_TRANS_SET)) =
			(0x00020000 | HC_ParaSubType_TexGen);*/
		for (i = 0 ; i <= 0x13 ; i++) {
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(unsigned int) i << 24);
			/* *((unsigned int volatile*)(pMapIOPort+
			HC_REG_Hpara0)) = ((unsigned int) i << 24);*/
		}

		/* Initial Gamma Table Setting*/
		/* Initial Gamma Table Setting*/
		/* 5 + 4 = 9 (12) dwords*/
		/* sRGB texture is not directly support by H3 hardware.
		We have to set the deGamma table for texture sampling.*/

		/* degamma table*/
		SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			(0x00030000 | 0x15000000));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(0x40000000 | (30 << 20) | (15 << 10) | (5)));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			((119 << 20) | (81 << 10) | (52)));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			((283 << 20) | (219 << 10) | (165)));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			((535 << 20) | (441 << 10) | (357)));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			((119 << 20) | (884 << 20) | (757 << 10) |
			(640)));

		/* gamma table*/
		SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			(0x00030000 | 0x17000000));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(0x40000000 | (13 << 20) | (13 << 10) | (13)));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(0x40000000 | (26 << 20) | (26 << 10) | (26)));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(0x40000000 | (39 << 20) | (39 << 10) | (39)));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			((51 << 20) | (51 << 10) | (51)));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			((71 << 20) | (71 << 10) | (71)));
		SetMMIORegister(dev_priv->mmio->handle,
			0x440, (87 << 20) | (87 << 10) | (87));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(113 << 20) | (113 << 10) | (113));
		SetMMIORegister(dev_priv->mmio->handle,
			0x440, (135 << 20) | (135 << 10) | (135));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(170 << 20) | (170 << 10) | (170));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(199 << 20) | (199 << 10) | (199));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(246 << 20) | (246 << 10) | (246));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(284 << 20) | (284 << 10) | (284));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(317 << 20) | (317 << 10) | (317));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(347 << 20) | (347 << 10) | (347));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(373 << 20) | (373 << 10) | (373));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(398 << 20) | (398 << 10) | (398));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(442 << 20) | (442 << 10) | (442));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(481 << 20) | (481 << 10) | (481));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(517 << 20) | (517 << 10) | (517));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(550 << 20) | (550 << 10) | (550));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(609 << 20) | (609 << 10) | (609));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(662 << 20) | (662 << 10) | (662));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(709 << 20) | (709 << 10) | (709));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(753 << 20) | (753 << 10) | (753));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(794 << 20) | (794 << 10) | (794));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(832 << 20) | (832 << 10) | (832));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(868 << 20) | (868 << 10) | (868));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(902 << 20) | (902 << 10) | (902));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(934 << 20) | (934 << 10) | (934));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(966 << 20) | (966 << 10) | (966));
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			(996 << 20) | (996 << 10) | (996));


		/*
		For Interrupt Restore only All types of write through
		regsiters should be write header data to hardware at
		least before it can restore. H/W will automatically
		record the header to write through state buffer for
		resture usage.
		By Jaren:
		HParaType = 8'h03, HParaSubType = 8'h00
						8'h11
						8'h12
						8'h14
						8'h15
						8'h17
		HParaSubType 8'h12, 8'h15 is initialized.
		[HWLimit]
		1. All these write through registers can't be partial
		update.
		2. All these write through must be AGP command
		16 entries : 4 128-bit data */

		 /* Initialize INV_ParaSubType_TexPal  	 */
		SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			(0x00030000 | 0x00000000));
		for (i = 0; i < 16; i++) {
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x00000000);
		}

		/* Initialize INV_ParaSubType_4X4Cof */
		/* 32 entries : 8 128-bit data */
		SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			(0x00030000 | 0x11000000));
		for (i = 0; i < 32; i++) {
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x00000000);
		}

		/* Initialize INV_ParaSubType_StipPal */
		/* 5 entries : 2 128-bit data */
		SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			(0x00030000 | 0x14000000));
		for (i = 0; i < (5+3); i++) {
			SetMMIORegister(dev_priv->mmio->handle,
				0x440, 0x00000000);
		}

		/* primitive setting & vertex format*/
		SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			(0x00040000 | 0x14000000));
		for (i = 0; i < 52; i++) {
			SetMMIORegister(dev_priv->mmio->handle,
			0x440, ((unsigned int) i << 24));
		}
		SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			0x00fe0000);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x4000840f);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x47000400);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x44000000);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x46000000);

		/* setting Misconfig*/
		SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			0x00fe0000);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x00001004);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x0800004b);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x0a000049);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x0b0000fb);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x0c000001);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x0d0000cb);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x0e000009);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x10000000);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x110000ff);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x12000000);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x130000db);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x14000000);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x15000000);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x16000000);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x17000000);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x18000000);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x19000000);
		SetMMIORegister(dev_priv->mmio->handle, 0x440,
			0x20000000);
		} else if (dev_priv->chip_sub_index == CHIP_H6S2) {
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
				0x00010000);
			for (i = 0; i <= 0x9A; i++) {
				SetMMIORegister(dev_priv->mmio->handle, 0x440,
					(unsigned int) i << 24);
			}

			/* Initial Texture Stage Setting*/
			for (StageOfTexture = 0; StageOfTexture <= 0xf;
			StageOfTexture++) {
				SetMMIORegister(dev_priv->mmio->handle, 0x43C,
					(0x00020000 | 0x00000000 |
					(StageOfTexture & 0xf)<<24));
				for (i = 0 ; i <= 0x30 ; i++) {
					SetMMIORegister(dev_priv->mmio->handle,
					0x440, (unsigned int) i << 24);
				}
			}

			/* Initial Texture Sampler Setting*/
			for (StageOfTexture = 0; StageOfTexture <= 0xf;
			StageOfTexture++) {
				SetMMIORegister(dev_priv->mmio->handle, 0x43C,
					(0x00020000 | 0x20000000 |
					(StageOfTexture & 0xf)<<24));
				for (i = 0 ; i <= 0x36 ; i++) {
					SetMMIORegister(dev_priv->mmio->handle,
						0x440, (unsigned int) i << 24);
				}
			}

			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
				(0x00020000 | 0xfe000000));
			for (i = 0 ; i <= 0x13 ; i++) {
				SetMMIORegister(dev_priv->mmio->handle, 0x440,
					(unsigned int) i << 24);
				/* *((unsigned int volatile*)(pMapIOPort+
				HC_REG_Hpara0)) =((unsigned int) i << 24);*/
			}

			/* Initial Gamma Table Setting*/
			/* Initial Gamma Table Setting*/
			/* 5 + 4 = 9 (12) dwords*/
			/* sRGB texture is not directly support by
			H3 hardware.*/
			/* We have to set the deGamma table for texture
			sampling.*/

			/* degamma table*/
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
				(0x00030000 | 0x15000000));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(0x40000000 | (30 << 20) | (15 << 10) | (5)));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				((119 << 20) | (81 << 10) | (52)));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				((283 << 20) | (219 << 10) | (165)));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				((535 << 20) | (441 << 10) | (357)));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				((119 << 20) | (884 << 20) | (757 << 10)
				| (640)));

			/* gamma table*/
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
				(0x00030000 | 0x17000000));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(0x40000000 | (13 << 20) | (13 << 10) | (13)));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(0x40000000 | (26 << 20) | (26 << 10) | (26)));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(0x40000000 | (39 << 20) | (39 << 10) | (39)));
			SetMMIORegister(dev_priv->mmio->handle,
				0x440, ((51 << 20) | (51 << 10) | (51)));
			SetMMIORegister(dev_priv->mmio->handle,
				0x440, ((71 << 20) | (71 << 10) | (71)));
			SetMMIORegister(dev_priv->mmio->handle,
				0x440, (87 << 20) | (87 << 10) | (87));
			SetMMIORegister(dev_priv->mmio->handle,
				0x440, (113 << 20) | (113 << 10) | (113));
			SetMMIORegister(dev_priv->mmio->handle,
				0x440, (135 << 20) | (135 << 10) | (135));
			SetMMIORegister(dev_priv->mmio->handle,
				0x440, (170 << 20) | (170 << 10) | (170));
			SetMMIORegister(dev_priv->mmio->handle,
				0x440, (199 << 20) | (199 << 10) | (199));
			SetMMIORegister(dev_priv->mmio->handle,
				0x440, (246 << 20) | (246 << 10) | (246));
			SetMMIORegister(dev_priv->mmio->handle,
				0x440, (284 << 20) | (284 << 10) | (284));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(317 << 20) | (317 << 10) | (317));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(347 << 20) | (347 << 10) | (347));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(373 << 20) | (373 << 10) | (373));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(398 << 20) | (398 << 10) | (398));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(442 << 20) | (442 << 10) | (442));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(481 << 20) | (481 << 10) | (481));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(517 << 20) | (517 << 10) | (517));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(550 << 20) | (550 << 10) | (550));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(609 << 20) | (609 << 10) | (609));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(662 << 20) | (662 << 10) | (662));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(709 << 20) | (709 << 10) | (709));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(753 << 20) | (753 << 10) | (753));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(794 << 20) | (794 << 10) | (794));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(832 << 20) | (832 << 10) | (832));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(868 << 20) | (868 << 10) | (868));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(902 << 20) | (902 << 10) | (902));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(934 << 20) | (934 << 10) | (934));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(966 << 20) | (966 << 10) | (966));
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				(996 << 20) | (996 << 10) | (996));


			/* For Interrupt Restore only
			All types of write through regsiters should be write
			header data to hardware at least before it can restore.
			H/W will automatically record the header to write
			through state buffer for restureusage.
			By Jaren:
			HParaType = 8'h03, HParaSubType = 8'h00
			     8'h11
			     8'h12
			     8'h14
			     8'h15
			     8'h17
			HParaSubType 8'h12, 8'h15 is initialized.
			[HWLimit]
			1. All these write through registers can't be partial
			update.
			2. All these write through must be AGP command
			16 entries : 4 128-bit data */

			/* Initialize INV_ParaSubType_TexPal  	 */
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
				(0x00030000 | 0x00000000));
			for (i = 0; i < 16; i++) {
				SetMMIORegister(dev_priv->mmio->handle, 0x440,
					0x00000000);
			}

			/* Initialize INV_ParaSubType_4X4Cof */
			/* 32 entries : 8 128-bit data */
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
				(0x00030000 | 0x11000000));
			for (i = 0; i < 32; i++) {
				SetMMIORegister(dev_priv->mmio->handle, 0x440,
					0x00000000);
			}

			/* Initialize INV_ParaSubType_StipPal */
			/* 5 entries : 2 128-bit data */
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
				(0x00030000 | 0x14000000));
			for (i = 0; i < (5+3); i++) {
				SetMMIORegister(dev_priv->mmio->handle, 0x440,
					0x00000000);
			}

			/* primitive setting & vertex format*/
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			(0x00040000));
			for (i = 0; i <= 0x62; i++) {
				SetMMIORegister(dev_priv->mmio->handle, 0x440,
					((unsigned int) i << 24));
			}

			/*ParaType 0xFE - Configure and Misc Setting*/
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			(0x00fe0000));
			for (i = 0; i <= 0x47; i++) {
				SetMMIORegister(dev_priv->mmio->handle, 0x440,
					((unsigned int) i << 24));
			}
			/*ParaType 0x11 - Frame Buffer Auto-Swapping and
			Command Regulator Misc*/
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			(0x00110000));
			for (i = 0; i <= 0x20; i++) {
				SetMMIORegister(dev_priv->mmio->handle, 0x440,
					((unsigned int) i << 24));
			}
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
				0x00fe0000);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x4000840f);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x47000404);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x44000000);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x46000005);

			/* setting Misconfig*/
			SetMMIORegister(dev_priv->mmio->handle, 0x43C,
			0x00fe0000);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x00001004);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x08000249);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x0a0002c9);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x0b0002fb);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x0c000000);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x0d0002cb);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x0e000009);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x10000049);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x110002ff);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x12000008);
			SetMMIORegister(dev_priv->mmio->handle, 0x440,
				0x130002db);
		}
}

int  via_chrome9_drm_resume(struct pci_dev *pci)
{
	struct drm_device *dev = (struct drm_device *)pci_get_drvdata(pci);
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *)dev->dev_private;

	if (!dev_priv->initialized)
		return 0;

	Initialize3DEngine(dev_priv);

	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_TRANS, 0x00110000);
	if (dev_priv->chip_sub_index == CHIP_H6S2) {
		SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
			0x06000000);
		SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
			0x07100000);
	} else{
		SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
			0x02000000);
		SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
			0x03100000);
	}


	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_TRANS,
	INV_ParaType_PreCR);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
	INV_SubA_HSetRBGID | INV_HSetRBGID_CR);

	if (dev_priv->chip_sub_index == CHIP_H6S2) {
		unsigned int i;
		/* Here restore SR66~SR6F SR79~SR7B */
		for (i = 0; i < 10; i++) {
			SetMMIORegisterU8(dev_priv->mmio->handle,
				0x83c4, 0x66 + i);
			SetMMIORegisterU8(dev_priv->mmio->handle,
				0x83c5, dev_priv->gti_backup[i]);
		}

		for (i = 0; i < 3; i++) {
			SetMMIORegisterU8(dev_priv->mmio->handle,
				0x83c4, 0x79 + i);
			SetMMIORegisterU8(dev_priv->mmio->handle,
			 0x83c5, dev_priv->gti_backup[10 + i]);
		}
	}

	via_chrome9_dma_init_inv(dev);

	return 0;
}

int  via_chrome9_drm_suspend(struct pci_dev *pci,
	pm_message_t state)
{
	int i;
	struct drm_device *dev = (struct drm_device *)pci_get_drvdata(pci);
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *)dev->dev_private;

	if (!dev_priv->initialized)
		return 0;

	if (dev_priv->chip_sub_index != CHIP_H6S2)
		return 0;

	/* Save registers from SR66~SR6F */
	for (i = 0; i < 10; i++) {
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x66 + i);
		dev_priv->gti_backup[i] =
			GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
	}

	/* Save registers from SR79~SR7B */
	for (i = 0; i < 3; i++) {
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x79 + i);
		dev_priv->gti_backup[10 + i] =
			GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
	}

	   return 0;
}

int via_chrome9_driver_load(struct drm_device *dev,
	unsigned long chipset)
{
	struct drm_via_chrome9_private *dev_priv;
	int ret = 0;
	static int associate;

	if (!associate) {
		pci_set_drvdata(dev->pdev, dev);
		dev->pdev->driver = &dev->driver->pci_driver;
		associate = 1;
	}

	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;

	dev_priv = drm_calloc(1, sizeof(struct drm_via_chrome9_private),
		DRM_MEM_DRIVER);
	if (dev_priv == NULL)
		return -ENOMEM;

	/* Clear */
	memset(dev_priv, 0, sizeof(struct drm_via_chrome9_private));

	dev_priv->dev = dev;
	dev->dev_private = (void *)dev_priv;

	dev_priv->chip_index = chipset;

	ret = drm_sman_init(&dev_priv->sman, 2, 12, 8);
	if (ret)
		drm_free(dev_priv, sizeof(*dev_priv), DRM_MEM_DRIVER);
	return ret;
}

int via_chrome9_driver_unload(struct drm_device *dev)
{
	struct drm_via_chrome9_private *dev_priv = dev->dev_private;

	drm_sman_takedown(&dev_priv->sman);

	drm_free(dev_priv, sizeof(struct drm_via_chrome9_private),
		DRM_MEM_DRIVER);

	dev->dev_private = 0;

	return 0;
}

static int via_chrome9_initialize(struct drm_device *dev,
	struct drm_via_chrome9_init *init)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *)dev->dev_private;

	dev_priv->chip_agp = init->chip_agp;
	dev_priv->chip_index = init->chip_index;
	dev_priv->chip_sub_index = init->chip_sub_index;

	dev_priv->usec_timeout = init->usec_timeout;
	dev_priv->front_offset = init->front_offset;
	dev_priv->back_offset = init->back_offset >>
		VIA_CHROME9DRM_VIDEO_STARTADDRESS_ALIGNMENT <<
		VIA_CHROME9DRM_VIDEO_STARTADDRESS_ALIGNMENT;
	dev_priv->available_fb_size = init->available_fb_size -
		(init->available_fb_size %
		(1 << VIA_CHROME9DRM_VIDEO_STARTADDRESS_ALIGNMENT));
	dev_priv->depth_offset = init->depth_offset;

	/* Find all the map added first, doing this is necessary to
	intialize hw */
	if (via_chrome9_map_init(dev, init)) {
		DRM_ERROR("function via_chrome9_map_init ERROR !\n");
		goto error;
	}

	/* Necessary information has been gathered for initialize hw */
	if (via_chrome9_hw_init(dev, init)) {
		DRM_ERROR("function via_chrome9_hw_init ERROR !\n");
		goto error;
	}

	/* After hw intialization, we have kown whether to use agp
	or to use pcie for texture */
	if (via_chrome9_heap_management_init(dev, init)) {
		DRM_ERROR("function \
			via_chrome9_heap_management_init ERROR !\n");
		goto error;
	}

	dev_priv->initialized = 1;

	return 0;

error:
	/* all the error recover has been processed in relevant function,
	so here just return error */
	return -EINVAL;
}

static void via_chrome9_cleanup(struct drm_device *dev,
	struct drm_via_chrome9_init *init)
{
	struct drm_via_chrome9_DMA_manager *lpcmDMAManager = NULL;
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *)dev->dev_private;
	DRM_DEBUG("function via_chrome9_cleanup run!\n");

	if (!dev_priv)
		return ;

	lpcmDMAManager =
		(struct drm_via_chrome9_DMA_manager *)dev_priv->dma_manager;
	if (dev_priv->pcie_vmalloc_nocache) {
		vfree((void *)dev_priv->pcie_vmalloc_nocache);
		dev_priv->pcie_vmalloc_nocache = 0;
		if (lpcmDMAManager)
			lpcmDMAManager->addr_linear = NULL;
	}

	if (dev_priv->pagetable_map.pagetable_handle) {
		iounmap(dev_priv->pagetable_map.pagetable_handle);
		dev_priv->pagetable_map.pagetable_handle = NULL;
	}

	if (lpcmDMAManager && lpcmDMAManager->addr_linear) {
		iounmap(lpcmDMAManager->addr_linear);
		lpcmDMAManager->addr_linear = NULL;
	}

	kfree(lpcmDMAManager);
	dev_priv->dma_manager = NULL;

	if (dev_priv->event_tag_info) {
		vfree(dev_priv->event_tag_info);
		dev_priv->event_tag_info = NULL;
	}

	if (dev_priv->bci_buffer) {
		vfree(dev_priv->bci_buffer);
		dev_priv->bci_buffer = NULL;
	}

	via_chrome9_memory_destroy_heap(dev, dev_priv);

	/* After cleanup, it should to set the value to null */
	dev_priv->sarea = dev_priv->mmio = dev_priv->hostBlt =
	dev_priv->fb = dev_priv->front = dev_priv->back =
	dev_priv->depth = dev_priv->agp_tex =
	dev_priv->shadow_map.shadow = 0;
	dev_priv->sarea_priv = 0;
	dev_priv->initialized = 0;
}

/*
Do almost everything intialize here,include:
1.intialize all addmaps in private data structure
2.intialize memory heap management for video agp/pcie
3.intialize hw for dma(pcie/agp) function

Note:all this function will dispatch into relevant function
*/
int via_chrome9_ioctl_init(struct drm_device *dev, void *data,
	struct drm_file *file_priv)
{
	struct drm_via_chrome9_init *init = (struct drm_via_chrome9_init *)data;

	switch (init->func) {
	case VIA_CHROME9_INIT:
		if (via_chrome9_initialize(dev, init)) {
			DRM_ERROR("function via_chrome9_initialize error\n");
			return -1;
		}
		via_chrome9_filepriv_v4l = (void *)file_priv;
		via_chrome9_dev_v4l = (void *)dev;
		break;

	case VIA_CHROME9_CLEANUP:
		via_chrome9_cleanup(dev, init);
		via_chrome9_filepriv_v4l = 0;
		via_chrome9_dev_v4l = 0;
		break;

	default:
		return -1;
	}

	return 0;
}

int via_chrome9_ioctl_allocate_event_tag(struct drm_device *dev,
	void *data, struct drm_file *file_priv)
{
	struct drm_via_chrome9_event_tag *event_tag = data;
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *)dev->dev_private;
	struct drm_clb_event_tag_info *event_tag_info =
		dev_priv->event_tag_info;
	unsigned int *event_addr = 0, i = 0;

	for (i = 0; i < NUMBER_OF_EVENT_TAGS; i++) {
		if (!event_tag_info->usage[i])
			break;
	}

	if (i < NUMBER_OF_EVENT_TAGS) {
		event_tag_info->usage[i] = 1;
		event_tag->event_offset = i;
		event_tag->last_sent_event_value.event_low = 0;
		event_tag->current_event_value.event_low = 0;
		event_addr = event_tag_info->linear_address +
		event_tag->event_offset * 4;
		*event_addr = 0;
		return 0;
	} else {
		return -7;
	}

	return 0;
}

int via_chrome9_ioctl_free_event_tag(struct drm_device *dev,
	void *data, struct drm_file *file_priv)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *)dev->dev_private;
	struct drm_clb_event_tag_info *event_tag_info =
		dev_priv->event_tag_info;
	struct drm_via_chrome9_event_tag *event_tag = data;

	event_tag_info->usage[event_tag->event_offset] = 0;
	return 0;
}

void via_chrome9_lastclose(struct drm_device *dev)
{
	via_chrome9_cleanup(dev, 0);
	return ;
}

static int via_chrome9_do_wait_vblank(struct drm_via_chrome9_private
		*dev_priv)
{
	int i;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		VIA_CHROME9_WRITE8(0x83d4, 0x34);
		if ((VIA_CHROME9_READ8(0x83d5)) & 0x8)
			return 0;
		__via_chrome9ke_udelay(1);
	}

	return -1;
}

void via_chrome9_preclose(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	struct drm_via_chrome9_sarea *sarea_priv = NULL;

	if (!dev_priv)
		return ;

	sarea_priv = dev_priv->sarea_priv;
	if (!sarea_priv)
		return ;

	if ((sarea_priv->page_flip == 1) &&
		(sarea_priv->current_page != VIA_CHROME9_FRONT)) {
		__volatile__ unsigned long *bci_base;
		if (via_chrome9_do_wait_vblank(dev_priv))
			return;

		bci_base = (__volatile__ unsigned long *)(dev_priv->bci);

		BCI_SET_STREAM_REGISTER(bci_base, 0x81c4, 0xc0000000);
		BCI_SET_STREAM_REGISTER(bci_base, 0x81c0,
			dev_priv->front_offset);
		BCI_SEND(bci_base, 0x64000000);/* wait vsync */

		sarea_priv->current_page = VIA_CHROME9_FRONT;
	}
}

int via_chrome9_is_agp(struct drm_device *dev)
{
	/* filter out pcie group which has no AGP device */
	if (dev->pci_device == 0x1122 || dev->pci_device == 0x5122) {
		dev->driver->driver_features &=
		~(DRIVER_USE_AGP | DRIVER_USE_MTRR | DRIVER_REQUIRE_AGP);
		return 0;
	}
	return 1;
}

