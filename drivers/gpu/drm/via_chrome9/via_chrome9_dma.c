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
#include "drm.h"
#include "via_chrome9_drm.h"
#include "via_chrome9_drv.h"
#include "via_chrome9_3d_reg.h"
#include "via_chrome9_dma.h"

#define NULLCOMMANDNUMBER 256
unsigned int NULL_COMMAND_INV[4] =
	{ 0xCC000000, 0xCD000000, 0xCE000000, 0xCF000000 };

void
via_chrome9ke_assert(int a)
{
}

unsigned int
ProtectSizeValue(unsigned int size)
{
	unsigned int i;
	for (i = 0; i < 8; i++)
		if ((size > (1 << (i + 12)))
		    && (size <= (1 << (i + 13))))
			return i + 1;
	return 0;
}

void via_chrome9_dma_init_inv(struct drm_device *dev)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *)dev->dev_private;
	struct drm_via_chrome9_DMA_manager *lpcmDMAManager =
		dev_priv->dma_manager;

	if (dev_priv->chip_sub_index == CHIP_H6S2) {
		unsigned int *pGARTTable;
		unsigned int i, entries, GARTOffset;
		unsigned char sr6a, sr6b, sr6c, sr6f, sr7b;
		unsigned int *addrlinear;
		unsigned int size, alignedoffset;

		entries = dev_priv->pagetable_map.pagetable_size /
			sizeof(unsigned int);
		pGARTTable = dev_priv->pagetable_map.pagetable_handle;

		GARTOffset = dev_priv->pagetable_map.pagetable_offset;

		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6c);
		sr6c = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
		sr6c &= (~0x80);
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6c);

		sr6a = (unsigned char)((GARTOffset & 0xff000) >> 12);
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6a);
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6a);

		sr6b = (unsigned char)((GARTOffset & 0xff00000) >> 20);
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6b);
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6b);

		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6c);
		sr6c = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
		sr6c |= ((unsigned char)((GARTOffset >> 28) & 0x01));
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6c);

		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x7b);
		sr7b = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
		sr7b &= (~0x0f);
		sr7b |= ProtectSizeValue(dev_priv->
			pagetable_map.pagetable_size);
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr7b);

		for (i = 0; i < entries; i++)
			writel(0x80000000, pGARTTable+i);

		/*flush*/
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6f);
		do {
			sr6f = GetMMIORegisterU8(dev_priv->mmio->handle,
				0x83c5);
		} while (sr6f & 0x80);

		sr6f |= 0x80;
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6f);

		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6c);
		sr6c = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
		sr6c |= 0x80;
		SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6c);

		if (dev_priv->drm_agp_type != DRM_AGP_DISABLED) {
			size = lpcmDMAManager->DMASize * sizeof(unsigned int) +
			dev_priv->agp_size;
			alignedoffset = 0;
			entries = (size + PAGE_SIZE - 1) / PAGE_SIZE;
			addrlinear =
				(unsigned int *)dev_priv->pcie_vmalloc_nocache;

			SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6c);
			sr6c =
			GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
			sr6c &= (~0x80);
			SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6c);

			SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6f);
			do {
				sr6f = GetMMIORegisterU8(dev_priv->mmio->handle,
					0x83c5);
			} while (sr6f & 0x80);

			for (i = 0; i < entries; i++)
				writel(page_to_pfn(vmalloc_to_page(
				(void *)addrlinear + PAGE_SIZE * i)) &
				0x3fffffff, pGARTTable + i + alignedoffset);

			sr6f |= 0x80;
			SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6f);

			SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6c);
			sr6c =
			GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
			sr6c |= 0x80;
			SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6c);
		}

	}

	if (dev_priv->drm_agp_type == DRM_AGP_DOUBLE_BUFFER)
		SetAGPDoubleCmd_inv(dev);
	else if (dev_priv->drm_agp_type == DRM_AGP_RING_BUFFER)
		SetAGPRingCmdRegs_inv(dev);

	return ;
}

static unsigned int
InitPCIEGART(struct drm_via_chrome9_private *dev_priv)
{
	unsigned int *pGARTTable;
	unsigned int i, entries, GARTOffset;
	unsigned char sr6a, sr6b, sr6c, sr6f, sr7b;

	if (!dev_priv->pagetable_map.pagetable_size)
		return 0;

	entries = dev_priv->pagetable_map.pagetable_size / sizeof(unsigned int);

	pGARTTable =
		ioremap_nocache(dev_priv->fb_base_address +
				dev_priv->pagetable_map.pagetable_offset,
				dev_priv->pagetable_map.pagetable_size);
	if (pGARTTable)
		dev_priv->pagetable_map.pagetable_handle = pGARTTable;
	else
		return 0;

	/*set gart table base */
	GARTOffset = dev_priv->pagetable_map.pagetable_offset;

	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6c);
	sr6c = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
	sr6c &= (~0x80);
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6c);

	sr6a = (unsigned char) ((GARTOffset & 0xff000) >> 12);
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6a);
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6a);

	sr6b = (unsigned char) ((GARTOffset & 0xff00000) >> 20);
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6b);
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6b);

	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6c);
	sr6c = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
	sr6c |= ((unsigned char) ((GARTOffset >> 28) & 0x01));
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6c);

	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x7b);
	sr7b = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
	sr7b &= (~0x0f);
	sr7b |= ProtectSizeValue(dev_priv->pagetable_map.pagetable_size);
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr7b);

	for (i = 0; i < entries; i++)
		writel(0x80000000, pGARTTable + i);
	/*flush */
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6f);
	do {
		sr6f = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
	}
	while (sr6f & 0x80)
		;

	sr6f |= 0x80;
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6f);

	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6c);
	sr6c = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
	sr6c |= 0x80;
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6c);

	return 1;
}


static unsigned int *
AllocAndBindPCIEMemory(struct drm_via_chrome9_private *dev_priv,
	unsigned int size, unsigned int offset)
{
	unsigned int *addrlinear;
	unsigned int *pGARTTable;
	unsigned int entries, alignedoffset, i;
	unsigned char sr6c, sr6f;

	if (!size)
		return NULL;

	entries = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	alignedoffset = (offset + PAGE_SIZE - 1) / PAGE_SIZE;

	if ((entries + alignedoffset) >
	    (dev_priv->pagetable_map.pagetable_size / sizeof(unsigned int)))
		return NULL;

	addrlinear =
		__vmalloc(entries * PAGE_SIZE, GFP_KERNEL | __GFP_HIGHMEM,
			  PAGE_KERNEL_NOCACHE);

	if (!addrlinear)
		return NULL;

	pGARTTable = dev_priv->pagetable_map.pagetable_handle;

	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6c);
	sr6c = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
	sr6c &= (~0x80);
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6c);

	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6f);
	do {
		sr6f = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
	}
	while (sr6f & 0x80)
		;

	for (i = 0; i < entries; i++)
		writel(page_to_pfn
		       (vmalloc_to_page((void *) addrlinear + PAGE_SIZE * i)) &
		       0x3fffffff, pGARTTable + i + alignedoffset);

	sr6f |= 0x80;
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6f);

	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c4, 0x6c);
	sr6c = GetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5);
	sr6c |= 0x80;
	SetMMIORegisterU8(dev_priv->mmio->handle, 0x83c5, sr6c);

	return addrlinear;

}

void
SetAGPDoubleCmd_inv(struct drm_device *dev)
{
	/* we now don't use double buffer */
	return;
}

void
SetAGPRingCmdRegs_inv(struct drm_device *dev)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	struct drm_via_chrome9_DMA_manager *lpcmDMAManager =
		(struct drm_via_chrome9_DMA_manager *) dev_priv->dma_manager;
	unsigned int AGPBufLinearBase = 0, AGPBufPhysicalBase = 0;
	unsigned long *pFree;
	unsigned int dwStart, dwEnd, dwPause, AGPCurrAddr, AGPCurStat, CurrAGP;
	unsigned int dwReg60, dwReg61, dwReg62, dwReg63,
		dwReg64, dwReg65, dwJump;

	lpcmDMAManager->pFree = lpcmDMAManager->pBeg;

	AGPBufLinearBase = (unsigned int) lpcmDMAManager->addr_linear;
	AGPBufPhysicalBase =
		(dev_priv->chip_agp ==
		 CHIP_PCIE) ? 0 : (unsigned int) dev->agp->base +
		lpcmDMAManager->pPhysical;
	/*add shadow offset */

	CurrAGP =
		GetMMIORegister(dev_priv->mmio->handle, INV_RB_AGPCMD_CURRADDR);
	AGPCurStat =
		GetMMIORegister(dev_priv->mmio->handle, INV_RB_AGPCMD_STATUS);

	if (AGPCurStat & INV_AGPCMD_InPause) {
		AGPCurrAddr =
			GetMMIORegister(dev_priv->mmio->handle,
					INV_RB_AGPCMD_CURRADDR);
		pFree = (unsigned long *) (AGPBufLinearBase + AGPCurrAddr -
					   AGPBufPhysicalBase);
		ADDCmdHeader2_INVI(pFree, INV_REG_CR_TRANS, INV_ParaType_Dummy);
		if (dev_priv->chip_sub_index == CHIP_H6S2)
			do {
				ADDCmdData_INVI(pFree, 0xCCCCCCC0);
				ADDCmdData_INVI(pFree, 0xDDD00000);
			}
			while ((u32)((unsigned int) pFree) & 0x7f)
				;
			/*for 8*128bit aligned */
		else
			do {
				ADDCmdData_INVI(pFree, 0xCCCCCCC0);
				ADDCmdData_INVI(pFree, 0xDDD00000);
			}
			while ((u32) ((unsigned int) pFree) & 0x1f)
				;
			/*for 256bit aligned */
		dwPause =
			(u32) (((unsigned int) pFree) - AGPBufLinearBase +
			       AGPBufPhysicalBase - 16);

		dwReg64 = INV_SubA_HAGPBpL | INV_HWBasL(dwPause);
		dwReg65 =
			INV_SubA_HAGPBpID | INV_HWBasH(dwPause) |
			INV_HAGPBpID_STOP;

		SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_TRANS,
				INV_ParaType_PreCR);
		SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
				dwReg64);
		SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
				dwReg65);

		while (GetMMIORegister(dev_priv->mmio->handle,
			INV_RB_ENG_STATUS) & INV_ENG_BUSY_ALL)
			;
	}
	dwStart =
		(u32) ((unsigned int) lpcmDMAManager->pBeg - AGPBufLinearBase +
		       AGPBufPhysicalBase);
	dwEnd = (u32) ((unsigned int) lpcmDMAManager->pEnd - AGPBufLinearBase +
		       AGPBufPhysicalBase);

	lpcmDMAManager->pFree = lpcmDMAManager->pBeg;
	if (dev_priv->chip_sub_index == CHIP_H6S2) {
		ADDCmdHeader2_INVI(lpcmDMAManager->pFree, INV_REG_CR_TRANS,
				   INV_ParaType_Dummy);
		do {
			ADDCmdData_INVI(lpcmDMAManager->pFree, 0xCCCCCCC0);
			ADDCmdData_INVI(lpcmDMAManager->pFree, 0xDDD00000);
		}
		while ((u32)((unsigned long *) lpcmDMAManager->pFree) & 0x7f)
			;
	}
	dwJump = 0xFFFFFFF0;
	dwPause =
		(u32)(((unsigned int) lpcmDMAManager->pFree) -
		16 - AGPBufLinearBase + AGPBufPhysicalBase);

	DRM_DEBUG("dwStart = %08x, dwEnd = %08x, dwPause = %08x\n", dwStart,
		  dwEnd, dwPause);

	dwReg60 = INV_SubA_HAGPBstL | INV_HWBasL(dwStart);
	dwReg61 = INV_SubA_HAGPBstH | INV_HWBasH(dwStart);
	dwReg62 = INV_SubA_HAGPBendL | INV_HWBasL(dwEnd);
	dwReg63 = INV_SubA_HAGPBendH | INV_HWBasH(dwEnd);
	dwReg64 = INV_SubA_HAGPBpL | INV_HWBasL(dwPause);
	dwReg65 = INV_SubA_HAGPBpID | INV_HWBasH(dwPause) | INV_HAGPBpID_PAUSE;

	if (dev_priv->chip_sub_index == CHIP_H6S2)
		dwReg60 |= 0x01;

	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_TRANS,
			INV_ParaType_PreCR);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg60);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg61);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg62);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg63);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg64);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg65);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
			INV_SubA_HAGPBjumpL | INV_HWBasL(dwJump));
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
			INV_SubA_HAGPBjumpH | INV_HWBasH(dwJump));

	/* Trigger AGP cycle */
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
			INV_SubA_HFthRCM | INV_HFthRCM_10 | INV_HAGPBTrig);

	/*for debug */
	CurrAGP =
		GetMMIORegister(dev_priv->mmio->handle, INV_RB_AGPCMD_CURRADDR);

	lpcmDMAManager->pInUseBySW = lpcmDMAManager->pFree;
}

/* Do hw intialization and determine whether to use dma or mmio to
talk with hw */
int
via_chrome9_hw_init(struct drm_device *dev,
	struct drm_via_chrome9_init *init)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	unsigned retval = 0;
	unsigned int *pGARTTable, *addrlinear = NULL;
	int pages;
	struct drm_clb_event_tag_info *event_tag_info;
	struct drm_via_chrome9_DMA_manager *lpcmDMAManager = NULL;

	if (init->chip_agp == CHIP_PCIE) {
		dev_priv->pagetable_map.pagetable_offset =
			init->garttable_offset;
		dev_priv->pagetable_map.pagetable_size = init->garttable_size;
		dev_priv->agp_size = init->agp_tex_size;
		/*Henry :prepare for PCIE texture buffer */
	} else {
		dev_priv->pagetable_map.pagetable_offset = 0;
		dev_priv->pagetable_map.pagetable_size = 0;
	}

	dev_priv->dma_manager =
		kmalloc(sizeof(struct drm_via_chrome9_DMA_manager), GFP_KERNEL);
	if (!dev_priv->dma_manager) {
		DRM_ERROR("could not allocate system for dma_manager!\n");
		return -ENOMEM;
	}

	lpcmDMAManager =
		(struct drm_via_chrome9_DMA_manager *) dev_priv->dma_manager;
	((struct drm_via_chrome9_DMA_manager *)
		dev_priv->dma_manager)->DMASize = init->DMA_size;
	((struct drm_via_chrome9_DMA_manager *)
		dev_priv->dma_manager)->pPhysical = init->DMA_phys_address;

	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_TRANS, 0x00110000);
	if (dev_priv->chip_sub_index == CHIP_H6S2) {
		SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
				0x06000000);
		SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
				0x07100000);
	} else {
		SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
				0x02000000);
		SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
				0x03100000);
	}

	/* Specify fence command read back ID */
	/* Default the read back ID is CR */
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_TRANS,
			INV_ParaType_PreCR);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
			INV_SubA_HSetRBGID | INV_HSetRBGID_CR);

	DRM_DEBUG("begin to init\n");

	if (dev_priv->chip_sub_index == CHIP_H6S2) {
		dev_priv->pcie_vmalloc_nocache = 0;
		if (dev_priv->pagetable_map.pagetable_size)
			retval = InitPCIEGART(dev_priv);

		if (retval && dev_priv->drm_agp_type != DRM_AGP_DISABLED) {
			addrlinear =
				AllocAndBindPCIEMemory(dev_priv,
						       lpcmDMAManager->DMASize +
						       dev_priv->agp_size, 0);
			if (addrlinear) {
				dev_priv->pcie_vmalloc_nocache = (unsigned long)
					addrlinear;
			} else {
				dev_priv->bci_buffer =
					vmalloc(MAX_BCI_BUFFER_SIZE);
				dev_priv->drm_agp_type = DRM_AGP_DISABLED;
			}
		} else {
			dev_priv->bci_buffer = vmalloc(MAX_BCI_BUFFER_SIZE);
			dev_priv->drm_agp_type = DRM_AGP_DISABLED;
		}
	} else {
		if (dev_priv->drm_agp_type != DRM_AGP_DISABLED) {
			pGARTTable = NULL;
			addrlinear = (unsigned int *)
				ioremap(dev->agp->base +
					lpcmDMAManager->pPhysical,
					lpcmDMAManager->DMASize);
			dev_priv->bci_buffer = NULL;
		} else {
			dev_priv->bci_buffer = vmalloc(MAX_BCI_BUFFER_SIZE);
			/*Homer, BCI path always use this block of memory8 */
		}
	}

	/*till here we have known whether support dma or not */
	pages = dev->sg->pages;
	event_tag_info = vmalloc(sizeof(struct drm_clb_event_tag_info));
	memset(event_tag_info, 0, sizeof(struct drm_clb_event_tag_info));
	if (!event_tag_info)
		return DRM_ERROR(" event_tag_info allocate error!");

	/* aligned to 16k alignment */
	event_tag_info->linear_address =
		(int
		 *) (((unsigned int) dev_priv->shadow_map.shadow_handle +
		      0x3fff) & 0xffffc000);
	event_tag_info->event_tag_linear_address =
		event_tag_info->linear_address + 3;
	dev_priv->event_tag_info = (void *) event_tag_info;
	dev_priv->max_apertures = NUMBER_OF_APERTURES_CLB;

	/* Initialize DMA data structure */
	lpcmDMAManager->DMASize /= sizeof(unsigned int);
	lpcmDMAManager->pBeg = addrlinear;
	lpcmDMAManager->pFree = lpcmDMAManager->pBeg;
	lpcmDMAManager->pInUseBySW = lpcmDMAManager->pBeg;
	lpcmDMAManager->pInUseByHW = lpcmDMAManager->pBeg;
	lpcmDMAManager->LastIssuedEventTag = (unsigned int) (unsigned long *)
		lpcmDMAManager->pBeg;
	lpcmDMAManager->ppInUseByHW =
		(unsigned int **) ((char *) (dev_priv->mmio->handle) +
				   INV_RB_AGPCMD_CURRADDR);
	lpcmDMAManager->bDMAAgp = dev_priv->chip_agp;
	lpcmDMAManager->addr_linear = (unsigned int *) addrlinear;

	if (dev_priv->drm_agp_type == DRM_AGP_DOUBLE_BUFFER) {
		lpcmDMAManager->MaxKickoffSize = lpcmDMAManager->DMASize >> 1;
		lpcmDMAManager->pEnd =
			lpcmDMAManager->addr_linear +
			(lpcmDMAManager->DMASize >> 1) - 1;
		SetAGPDoubleCmd_inv(dev);
		if (dev_priv->chip_sub_index == CHIP_H6S2) {
			DRM_INFO("DMA buffer initialized finished. ");
			DRM_INFO("Use PCIE Double Buffer type!\n");
			DRM_INFO("Total PCIE DMA buffer size = %8d bytes. \n",
				 lpcmDMAManager->DMASize << 2);
		} else {
			DRM_INFO("DMA buffer initialized finished. ");
			DRM_INFO("Use AGP Double Buffer type!\n");
			DRM_INFO("Total AGP DMA buffer size = %8d bytes. \n",
				 lpcmDMAManager->DMASize << 2);
		}
	} else if (dev_priv->drm_agp_type == DRM_AGP_RING_BUFFER) {
		lpcmDMAManager->MaxKickoffSize = lpcmDMAManager->DMASize;
		lpcmDMAManager->pEnd =
			lpcmDMAManager->addr_linear + lpcmDMAManager->DMASize;
		SetAGPRingCmdRegs_inv(dev);
		if (dev_priv->chip_sub_index == CHIP_H6S2) {
			DRM_INFO("DMA buffer initialized finished. \n");
			DRM_INFO("Use PCIE Ring Buffer type!");
			DRM_INFO("Total PCIE DMA buffer size = %8d bytes. \n",
				 lpcmDMAManager->DMASize << 2);
		} else {
			DRM_INFO("DMA buffer initialized finished. ");
			DRM_INFO("Use AGP Ring Buffer type!\n");
			DRM_INFO("Total AGP DMA buffer size = %8d bytes. \n",
				 lpcmDMAManager->DMASize << 2);
		}
	} else if (dev_priv->drm_agp_type == DRM_AGP_DISABLED) {
		lpcmDMAManager->MaxKickoffSize = 0x0;
		if (dev_priv->chip_sub_index == CHIP_H6S2)
			DRM_INFO("PCIE init failed! Use PCI\n");
		else
			DRM_INFO("AGP init failed! Use PCI\n");
	}
	return 0;
}

static void
kickoff_bci_inv(struct drm_device *dev,
		struct drm_via_chrome9_flush *dma_info)
{
	u32 HdType, dwQWCount, i, dwCount, Addr1, Addr2, SWPointer,
		SWPointerEnd;
	unsigned long *pCmdData;
	int result;

	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	/*pCmdData = __s3gke_vmalloc(dma_info->cmd_size<<2); */
	pCmdData = dev_priv->bci_buffer;

	if (!pCmdData)
		return;
	result = copy_from_user((int *) pCmdData, dma_info->usermode_dma_buf,
				dma_info->cmd_size << 2);
	if (result) {
		DRM_ERROR("In function kickoff_bci_inv,\
		copy_from_user is fault. \n");
		return ;
	}
#if VIA_CHROME9_VERIFY_ENABLE
	result = via_chrome9_verify_command_stream(
		(const uint32_t *)pCmdData,	dma_info->cmd_size << 2,
		dev, dev_priv->chip_sub_index == CHIP_H6S2 ? 0 : 1);
	if (result) {
		DRM_ERROR("The command has the security issue \n");
		return ;
	}
#endif
	SWPointer = 0;
	SWPointerEnd = (u32) dma_info->cmd_size;
	while (SWPointer < SWPointerEnd) {
		HdType = pCmdData[SWPointer] & INV_AGPHeader_MASK;
		switch (HdType) {
		case INV_AGPHeader0:
		case INV_AGPHeader5:
			dwQWCount = pCmdData[SWPointer + 1];
			SWPointer += 4;

			for (i = 0; i < dwQWCount; i++) {
				SetMMIORegister(dev_priv->mmio->handle,
						pCmdData[SWPointer],
						pCmdData[SWPointer + 1]);
				SWPointer += 2;
			}
			break;

		case INV_AGPHeader1:
			dwCount = pCmdData[SWPointer + 1];
			Addr1 = 0x0;
			SWPointer += 4;	/* skip 128-bit. */

			for (; dwCount > 0; dwCount--, SWPointer++,
				Addr1 += 4) {
				SetMMIORegister(dev_priv->hostBlt->handle,
						Addr1, pCmdData[SWPointer]);
			}
			break;

		case INV_AGPHeader4:
			dwCount = pCmdData[SWPointer + 1];
			Addr1 = pCmdData[SWPointer] & 0x0000FFFF;
			SWPointer += 4;	/* skip 128-bit. */

			for (; dwCount > 0; dwCount--, SWPointer++)
				SetMMIORegister(dev_priv->mmio->handle, Addr1,
						pCmdData[SWPointer]);
			break;

		case INV_AGPHeader2:
			Addr1 = pCmdData[SWPointer + 1] & 0xFFFF;
			Addr2 = pCmdData[SWPointer] & 0xFFFF;

			/* Write first data (either ParaType or whatever) to
			Addr1 */
			SetMMIORegister(dev_priv->mmio->handle, Addr1,
					pCmdData[SWPointer + 2]);
			SWPointer += 4;

			/* The following data are all written to Addr2,
			   until another header is met */
			while (!is_agp_header(pCmdData[SWPointer])
			       && (SWPointer < SWPointerEnd)) {
				SetMMIORegister(dev_priv->mmio->handle, Addr2,
						pCmdData[SWPointer]);
				SWPointer++;
			}
			break;

		case INV_AGPHeader3:
			Addr1 = pCmdData[SWPointer] & 0xFFFF;
			Addr2 = Addr1 + 4;
			dwCount = pCmdData[SWPointer + 1];

			/* Write first data (either ParaType or whatever) to
			Addr1 */
			SetMMIORegister(dev_priv->mmio->handle, Addr1,
					pCmdData[SWPointer + 2]);
			SWPointer += 4;

			for (i = 0; i < dwCount; i++) {
				SetMMIORegister(dev_priv->mmio->handle, Addr2,
						pCmdData[SWPointer]);
				SWPointer++;
			}
			break;

		case INV_AGPHeader6:
			break;

		case INV_AGPHeader7:
			break;

		default:
			SWPointer += 4;	/* Advance to next header */
		}

		SWPointer = (SWPointer + 3) & ~3;
	}
}

void
kickoff_dma_db_inv(struct drm_device *dev)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	struct drm_via_chrome9_DMA_manager *lpcmDMAManager =
		dev_priv->dma_manager;

	u32 BufferSize = (u32) (lpcmDMAManager->pFree - lpcmDMAManager->pBeg);

	unsigned int AGPBufLinearBase =
		(unsigned int) lpcmDMAManager->addr_linear;
	unsigned int AGPBufPhysicalBase =
		(unsigned int) dev->agp->base + lpcmDMAManager->pPhysical;
	/*add shadow offset */

	unsigned int dwStart, dwEnd, dwPause;
	unsigned int dwReg60, dwReg61, dwReg62, dwReg63, dwReg64, dwReg65;
	unsigned int CR_Status;

	if (BufferSize == 0)
		return;

	/* 256-bit alignment of AGP pause address */
	if ((u32) ((unsigned long *) lpcmDMAManager->pFree) & 0x1f) {
		ADDCmdHeader2_INVI(lpcmDMAManager->pFree, INV_REG_CR_TRANS,
				   INV_ParaType_Dummy);
		do {
			ADDCmdData_INVI(lpcmDMAManager->pFree, 0xCCCCCCC0);
			ADDCmdData_INVI(lpcmDMAManager->pFree, 0xDDD00000);
		}
		while (((unsigned int) lpcmDMAManager->pFree) & 0x1f)
			;
	}

	dwStart =
		(u32) (unsigned long *)lpcmDMAManager->pBeg -
		AGPBufLinearBase + AGPBufPhysicalBase;
	dwEnd = (u32) (unsigned long *)lpcmDMAManager->pEnd -
		AGPBufLinearBase + AGPBufPhysicalBase;
	dwPause =
		(u32)(unsigned long *)lpcmDMAManager->pFree -
		AGPBufLinearBase + AGPBufPhysicalBase - 4;

	dwReg60 = INV_SubA_HAGPBstL | INV_HWBasL(dwStart);
	dwReg61 = INV_SubA_HAGPBstH | INV_HWBasH(dwStart);
	dwReg62 = INV_SubA_HAGPBendL | INV_HWBasL(dwEnd);
	dwReg63 = INV_SubA_HAGPBendH | INV_HWBasH(dwEnd);
	dwReg64 = INV_SubA_HAGPBpL | INV_HWBasL(dwPause);
	dwReg65 = INV_SubA_HAGPBpID | INV_HWBasH(dwPause) | INV_HAGPBpID_STOP;

	/* wait CR idle */
	CR_Status = GetMMIORegister(dev_priv->mmio->handle, INV_RB_ENG_STATUS);
	while (CR_Status & INV_ENG_BUSY_CR)
		CR_Status =
			GetMMIORegister(dev_priv->mmio->handle,
					INV_RB_ENG_STATUS);

	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_TRANS,
			INV_ParaType_PreCR);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg60);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg61);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg62);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg63);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg64);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg65);

	/* Trigger AGP cycle */
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN,
			INV_SubA_HFthRCM | INV_HFthRCM_10 | INV_HAGPBTrig);

	if (lpcmDMAManager->pBeg == lpcmDMAManager->addr_linear) {
		/* The second AGP command buffer */
		lpcmDMAManager->pBeg =
			lpcmDMAManager->addr_linear +
			(lpcmDMAManager->DMASize >> 2);
		lpcmDMAManager->pEnd =
			lpcmDMAManager->addr_linear + lpcmDMAManager->DMASize;
		lpcmDMAManager->pFree = lpcmDMAManager->pBeg;
	} else {
		/* The first AGP command buffer */
		lpcmDMAManager->pBeg = lpcmDMAManager->addr_linear;
		lpcmDMAManager->pEnd =
			lpcmDMAManager->addr_linear +
			(lpcmDMAManager->DMASize / 2) - 1;
		lpcmDMAManager->pFree = lpcmDMAManager->pBeg;
	}
	CR_Status = GetMMIORegister(dev_priv->mmio->handle, INV_RB_ENG_STATUS);
}


void
kickoff_dma_ring_inv(struct drm_device *dev)
{
	unsigned int dwPause, dwReg64, dwReg65;

	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	struct drm_via_chrome9_DMA_manager *lpcmDMAManager =
		dev_priv->dma_manager;

	unsigned int AGPBufLinearBase =
		(unsigned int) lpcmDMAManager->addr_linear;
	unsigned int AGPBufPhysicalBase =
		(dev_priv->chip_agp ==
		 CHIP_PCIE) ? 0 : (unsigned int) dev->agp->base +
		lpcmDMAManager->pPhysical;
	/*add shadow offset */

	/* 256-bit alignment of AGP pause address */
	if (dev_priv->chip_sub_index == CHIP_H6S2) {
		if ((u32)
		    ((unsigned long *) lpcmDMAManager->pFree) & 0x7f) {
			ADDCmdHeader2_INVI(lpcmDMAManager->pFree,
					   INV_REG_CR_TRANS,
					   INV_ParaType_Dummy);
			do {
				ADDCmdData_INVI(lpcmDMAManager->pFree,
						0xCCCCCCC0);
				ADDCmdData_INVI(lpcmDMAManager->pFree,
						0xDDD00000);
			}
			while ((u32)((unsigned long *) lpcmDMAManager->pFree) &
				0x7f)
				;
		}
	} else {
		if ((u32)
		    ((unsigned long *) lpcmDMAManager->pFree) & 0x1f) {
			ADDCmdHeader2_INVI(lpcmDMAManager->pFree,
					   INV_REG_CR_TRANS,
					   INV_ParaType_Dummy);
			do {
				ADDCmdData_INVI(lpcmDMAManager->pFree,
						0xCCCCCCC0);
				ADDCmdData_INVI(lpcmDMAManager->pFree,
						0xDDD00000);
			}
			while ((u32)((unsigned long *) lpcmDMAManager->pFree) &
				0x1f)
				;
		}
	}


	dwPause = (u32) ((unsigned long *) lpcmDMAManager->pFree)
		- AGPBufLinearBase + AGPBufPhysicalBase - 16;

	dwReg64 = INV_SubA_HAGPBpL | INV_HWBasL(dwPause);
	dwReg65 = INV_SubA_HAGPBpID | INV_HWBasH(dwPause) | INV_HAGPBpID_PAUSE;

	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_TRANS,
			INV_ParaType_PreCR);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg64);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg65);

	lpcmDMAManager->pInUseBySW = lpcmDMAManager->pFree;
}

static int
waitchipidle_inv(struct drm_via_chrome9_private *dev_priv)
{
	unsigned int count = 50000;
	unsigned int eng_status;
	unsigned int engine_busy;

	do {
		eng_status =
			GetMMIORegister(dev_priv->mmio->handle,
					INV_RB_ENG_STATUS);
		engine_busy = eng_status & INV_ENG_BUSY_ALL;
		count--;
	}
	while (engine_busy && count)
		;
	if (count && engine_busy == 0)
		return 0;
	return -1;
}

void
get_space_db_inv(struct drm_device *dev,
	struct cmd_get_space *lpcmGetSpaceData)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	struct drm_via_chrome9_DMA_manager *lpcmDMAManager =
		dev_priv->dma_manager;

	unsigned int dwRequestSize = lpcmGetSpaceData->dwRequestSize;
	if (dwRequestSize > lpcmDMAManager->MaxKickoffSize) {
		DRM_INFO("too big DMA buffer request!!!\n");
		via_chrome9ke_assert(0);
		*lpcmGetSpaceData->pCmdData = (unsigned int) NULL;
		return;
	}

	if ((lpcmDMAManager->pFree + dwRequestSize) >
	    (lpcmDMAManager->pEnd - INV_CMDBUF_THRESHOLD * 2))
		kickoff_dma_db_inv(dev);

	*lpcmGetSpaceData->pCmdData = (unsigned int) lpcmDMAManager->pFree;
}

void
RewindRingAGP_inv(struct drm_device *dev)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	struct drm_via_chrome9_DMA_manager *lpcmDMAManager =
		dev_priv->dma_manager;

	unsigned int AGPBufLinearBase =
		(unsigned int) lpcmDMAManager->addr_linear;
	unsigned int AGPBufPhysicalBase =
		(dev_priv->chip_agp ==
		 CHIP_PCIE) ? 0 : (unsigned int) dev->agp->base +
		lpcmDMAManager->pPhysical;
	/*add shadow offset */

	unsigned int dwPause, dwJump;
	unsigned int dwReg66, dwReg67;
	unsigned int dwReg64, dwReg65;

	ADDCmdHeader2_INVI(lpcmDMAManager->pFree, INV_REG_CR_TRANS,
			   INV_ParaType_Dummy);
	ADDCmdData_INVI(lpcmDMAManager->pFree, 0xCCCCCCC7);
	if (dev_priv->chip_sub_index == CHIP_H6S2)
		while ((unsigned int) lpcmDMAManager->pFree & 0x7F)
			ADDCmdData_INVI(lpcmDMAManager->pFree, 0xCCCCCCC7);
	else
		while ((unsigned int) lpcmDMAManager->pFree & 0x1F)
			ADDCmdData_INVI(lpcmDMAManager->pFree, 0xCCCCCCC7);
	dwJump = ((u32) ((unsigned long *) lpcmDMAManager->pFree))
		- AGPBufLinearBase + AGPBufPhysicalBase - 16;

	lpcmDMAManager->pFree = lpcmDMAManager->pBeg;

	dwPause = ((u32) ((unsigned long *) lpcmDMAManager->pFree))
		- AGPBufLinearBase + AGPBufPhysicalBase - 16;

	dwReg64 = INV_SubA_HAGPBpL | INV_HWBasL(dwPause);
	dwReg65 = INV_SubA_HAGPBpID | INV_HWBasH(dwPause) | INV_HAGPBpID_PAUSE;

	dwReg66 = INV_SubA_HAGPBjumpL | INV_HWBasL(dwJump);
	dwReg67 = INV_SubA_HAGPBjumpH | INV_HWBasH(dwJump);

	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_TRANS,
			INV_ParaType_PreCR);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg66);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg67);

	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg64);
	SetMMIORegister(dev_priv->mmio->handle, INV_REG_CR_BEGIN, dwReg65);
	lpcmDMAManager->pInUseBySW = lpcmDMAManager->pFree;
}


void
get_space_ring_inv(struct drm_device *dev,
		   struct cmd_get_space *lpcmGetSpaceData)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	struct drm_via_chrome9_DMA_manager *lpcmDMAManager =
		dev_priv->dma_manager;
	unsigned int dwUnFlushed;
	unsigned int dwRequestSize = lpcmGetSpaceData->dwRequestSize;

	unsigned int AGPBufLinearBase =
		(unsigned int) lpcmDMAManager->addr_linear;
	unsigned int AGPBufPhysicalBase =
		(dev_priv->chip_agp ==
		 CHIP_PCIE) ? 0 : (unsigned int) dev->agp->base +
		lpcmDMAManager->pPhysical;
	/*add shadow offset */
	u32 BufStart, BufEnd, CurSW, CurHW, NextSW, BoundaryCheck;

	dwUnFlushed =
		(unsigned int) (lpcmDMAManager->pFree - lpcmDMAManager->pBeg);
	/*default bEnableModuleSwitch is on for metro,is off for rest */
	/*cmHW_Module_Switch is context-wide variable which is enough for 2d/3d
	   switch in a context. */
	/*But we must keep the dma buffer being wrapped head and tail by 3d cmds
	   when it is kicked off to kernel mode. */
	/*Get DMA Space (If requested, or no BCI space and BCI not forced. */

	if (dwRequestSize > lpcmDMAManager->MaxKickoffSize) {
		DRM_INFO("too big DMA buffer request!!!\n");
		via_chrome9ke_assert(0);
		*lpcmGetSpaceData->pCmdData = 0;
		return;
	}

	if (dwUnFlushed + dwRequestSize > lpcmDMAManager->MaxKickoffSize)
		kickoff_dma_ring_inv(dev);

	BufStart =
		(u32)((unsigned int) lpcmDMAManager->pBeg) - AGPBufLinearBase +
		AGPBufPhysicalBase;
	BufEnd = (u32)((unsigned int) lpcmDMAManager->pEnd) - AGPBufLinearBase +
		AGPBufPhysicalBase;
	dwRequestSize = lpcmGetSpaceData->dwRequestSize << 2;
	NextSW = (u32) ((unsigned int) lpcmDMAManager->pFree) + dwRequestSize +
		INV_CMDBUF_THRESHOLD * 8 - AGPBufLinearBase +
		AGPBufPhysicalBase;

	CurSW = (u32)((unsigned int) lpcmDMAManager->pFree) - AGPBufLinearBase +
		AGPBufPhysicalBase;
	CurHW = GetMMIORegister(dev_priv->mmio->handle, INV_RB_AGPCMD_CURRADDR);

	if (NextSW >= BufEnd) {
		kickoff_dma_ring_inv(dev);
		CurSW = (u32) ((unsigned int) lpcmDMAManager->pFree) -
			AGPBufLinearBase + AGPBufPhysicalBase;
		/* make sure the last rewind is completed */
		CurHW = GetMMIORegister(dev_priv->mmio->handle,
					INV_RB_AGPCMD_CURRADDR);
		while (CurHW > CurSW)
			CurHW = GetMMIORegister(dev_priv->mmio->handle,
						INV_RB_AGPCMD_CURRADDR);
		/* Sometime the value read from HW is unreliable,
		so need double confirm. */
		CurHW = GetMMIORegister(dev_priv->mmio->handle,
					INV_RB_AGPCMD_CURRADDR);
		while (CurHW > CurSW)
			CurHW = GetMMIORegister(dev_priv->mmio->handle,
						INV_RB_AGPCMD_CURRADDR);
		BoundaryCheck =
			BufStart + dwRequestSize + INV_QW_PAUSE_ALIGN * 16;
		if (BoundaryCheck >= BufEnd)
			/* If an empty command buffer can't hold
			the request data. */
			via_chrome9ke_assert(0);
		else {
			/* We need to guarntee the new commands have no chance
			to override the unexected commands or wait until there
			is no unexecuted commands in agp buffer */
			if (CurSW <= BoundaryCheck) {
				CurHW = GetMMIORegister(dev_priv->mmio->handle,
							INV_RB_AGPCMD_CURRADDR);
				while (CurHW < CurSW)
					CurHW = GetMMIORegister(
					dev_priv->mmio->handle,
					INV_RB_AGPCMD_CURRADDR);
				/*Sometime the value read from HW is unreliable,
				   so need double confirm. */
				CurHW = GetMMIORegister(dev_priv->mmio->handle,
							INV_RB_AGPCMD_CURRADDR);
				while (CurHW < CurSW) {
					CurHW = GetMMIORegister(
						dev_priv->mmio->handle,
						INV_RB_AGPCMD_CURRADDR);
				}
				RewindRingAGP_inv(dev);
				CurSW = (u32) ((unsigned long *)
					       lpcmDMAManager->pFree) -
					AGPBufLinearBase + AGPBufPhysicalBase;
				CurHW = GetMMIORegister(dev_priv->mmio->handle,
							INV_RB_AGPCMD_CURRADDR);
				/* Waiting until hw pointer jump to start
				and hw pointer will */
				/* equal to sw pointer */
				while (CurHW != CurSW) {
					CurHW = GetMMIORegister(
						dev_priv->mmio->handle,
						INV_RB_AGPCMD_CURRADDR);
				}
			} else {
				CurHW = GetMMIORegister(dev_priv->mmio->handle,
							INV_RB_AGPCMD_CURRADDR);

				while (CurHW <= BoundaryCheck) {
					CurHW = GetMMIORegister(
						dev_priv->mmio->handle,
						INV_RB_AGPCMD_CURRADDR);
				}
				CurHW = GetMMIORegister(dev_priv->mmio->handle,
							INV_RB_AGPCMD_CURRADDR);
				/* Sometime the value read from HW is
				unreliable, so need double confirm. */
				while (CurHW <= BoundaryCheck) {
					CurHW = GetMMIORegister(
						dev_priv->mmio->handle,
						INV_RB_AGPCMD_CURRADDR);
				}
				RewindRingAGP_inv(dev);
			}
		}
	} else {
		/* no need to rewind Ensure unexecuted agp commands will
		not be override by new
		agp commands */
		CurSW = (u32) ((unsigned int) lpcmDMAManager->pFree) -
			AGPBufLinearBase + AGPBufPhysicalBase;
		CurHW = GetMMIORegister(dev_priv->mmio->handle,
					INV_RB_AGPCMD_CURRADDR);

		while ((CurHW > CurSW) && (CurHW <= NextSW))
			CurHW = GetMMIORegister(dev_priv->mmio->handle,
						INV_RB_AGPCMD_CURRADDR);

		/* Sometime the value read from HW is unreliable,
		so need double confirm. */
		CurHW = GetMMIORegister(dev_priv->mmio->handle,
					INV_RB_AGPCMD_CURRADDR);
		while ((CurHW > CurSW) && (CurHW <= NextSW))
			CurHW = GetMMIORegister(dev_priv->mmio->handle,
						INV_RB_AGPCMD_CURRADDR);
	}
	/*return the space handle */
	*lpcmGetSpaceData->pCmdData = (unsigned int) lpcmDMAManager->pFree;
}

void
release_space_inv(struct drm_device *dev,
		  struct cmd_release_space *lpcmReleaseSpaceData)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	struct drm_via_chrome9_DMA_manager *lpcmDMAManager =
		dev_priv->dma_manager;
	unsigned int dwReleaseSize = lpcmReleaseSpaceData->dwReleaseSize;
	int i = 0;

	lpcmDMAManager->pFree += dwReleaseSize;

	/* aligned address */
	while (((unsigned int) lpcmDMAManager->pFree) & 0xF) {
		/* not in 4 unsigned ints (16 Bytes) align address,
		insert NULL Commands */
		*lpcmDMAManager->pFree++ = NULL_COMMAND_INV[i & 0x3];
		i++;
	}

	if ((dev_priv->chip_sub_index == CHIP_H5 ||
		dev_priv->chip_sub_index == CHIP_H6S2) &&
		(dev_priv->drm_agp_type == DRM_AGP_RING_BUFFER)) {
		ADDCmdHeader2_INVI(lpcmDMAManager->pFree, INV_REG_CR_TRANS,
				   INV_ParaType_Dummy);
		for (i = 0; i < NULLCOMMANDNUMBER; i++)
			ADDCmdData_INVI(lpcmDMAManager->pFree, 0xCC000000);
	}
}

int
via_chrome9_ioctl_flush(struct drm_device *dev, void *data,
	struct drm_file *file_priv)
{
	struct drm_via_chrome9_flush *dma_info = data;
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;
	int ret = 0;
	int result = 0;
	struct cmd_get_space getspace;
	struct cmd_release_space releasespace;
	unsigned long *pCmdData = NULL;

	switch (dma_info->dma_cmd_type) {
		/* Copy DMA buffer to BCI command buffer */
	case flush_bci:
	case flush_bci_and_wait:
		if (dma_info->cmd_size <= 0)
			return 0;
		if (dma_info->cmd_size > MAX_BCI_BUFFER_SIZE) {
			DRM_INFO("too big BCI space request!!!\n");
			return 0;
		}

		kickoff_bci_inv(dev, dma_info);
		waitchipidle_inv(dev_priv);
		break;
		/* Use DRM DMA buffer manager to kick off DMA directly */
	case dma_kickoff:
		break;

		/* Copy user mode DMA buffer to kernel DMA buffer,
		then kick off DMA */
	case flush_dma_buffer:
	case flush_dma_and_wait:
		if (dma_info->cmd_size <= 0)
			return 0;

		getspace.dwRequestSize = dma_info->cmd_size;
		if ((dev_priv->chip_sub_index == CHIP_H5 ||
			dev_priv->chip_sub_index == CHIP_H6S2) &&
			(dev_priv->drm_agp_type == DRM_AGP_RING_BUFFER))
			getspace.dwRequestSize += (NULLCOMMANDNUMBER + 4);
		/*henry:Patch for VT3293 agp ring buffer stability */
		getspace.pCmdData = (unsigned int *) &pCmdData;

		if (dev_priv->drm_agp_type == DRM_AGP_DOUBLE_BUFFER)
			get_space_db_inv(dev, &getspace);
		else if (dev_priv->drm_agp_type == DRM_AGP_RING_BUFFER)
			get_space_ring_inv(dev, &getspace);
		if (pCmdData) {
			/*copy data from userspace to kernel-dma-agp buffer */
			result = copy_from_user((int *)
						pCmdData,
						dma_info->usermode_dma_buf,
						dma_info->cmd_size << 2);
			if (result) {
				DRM_ERROR("In function via_chrome9_ioctl_flush,\
				copy_from_user is fault. \n");
				return -EINVAL;
			}

#if VIA_CHROME9_VERIFY_ENABLE
		result = via_chrome9_verify_command_stream(
			(const uint32_t *)pCmdData, dma_info->cmd_size << 2,
			dev, dev_priv->chip_sub_index == CHIP_H6S2 ? 0 : 1);
		if (result) {
			DRM_ERROR("The user command has security issue.\n");
			return -EINVAL;
		}
#endif

			releasespace.dwReleaseSize = dma_info->cmd_size;
			release_space_inv(dev, &releasespace);
			if (dev_priv->drm_agp_type == DRM_AGP_DOUBLE_BUFFER)
				kickoff_dma_db_inv(dev);
			else if (dev_priv->drm_agp_type == DRM_AGP_RING_BUFFER)
				kickoff_dma_ring_inv(dev);

			if (dma_info->dma_cmd_type == flush_dma_and_wait)
				waitchipidle_inv(dev_priv);
		} else {
			DRM_INFO("No enough DMA space");
			ret = -ENOMEM;
		}
		break;

	default:
		DRM_INFO("Invalid DMA buffer type");
		ret = -EINVAL;
		break;
	}
	return ret;
}

int
via_chrome9_ioctl_free(struct drm_device *dev, void *data,
	struct drm_file *file_priv)
{
	return 0;
}

int
via_chrome9_ioctl_wait_chip_idle(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_via_chrome9_private *dev_priv =
		(struct drm_via_chrome9_private *) dev->dev_private;

	waitchipidle_inv(dev_priv);
	/* maybe_bug here, do we always return 0 */
	return 0;
}

int
via_chrome9_ioctl_flush_cache(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	return 0;
}
