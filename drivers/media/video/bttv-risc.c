/*
    bttv-risc.c  --  interfaces to other kernel modules

    bttv risc code handling
	- memory management
	- generation

    (c) 2000 Gerd Knorr <kraxel@bytesex.org>

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include "bttvp.h"

/* ---------------------------------------------------------- */
/* allocate/free risc memory                                  */

int  bttv_riscmem_alloc(struct pci_dev *pci,
			struct bttv_riscmem *risc,
			unsigned int size)
{
	u32 *cpu;
	dma_addr_t dma;
	
	cpu = pci_alloc_consistent(pci, size, &dma);
	if (NULL == cpu)
		return -ENOMEM;
	memset(cpu,0,size);

	if (risc->cpu && risc->size < size) {
		/* realloc (enlarge buffer) -- copy old stuff */
		memcpy(cpu,risc->cpu,risc->size);
		bttv_riscmem_free(pci,risc);
	}
	risc->cpu  = cpu;
	risc->dma  = dma;
	risc->size = size;

	return 0;
}

void bttv_riscmem_free(struct pci_dev *pci,
		       struct bttv_riscmem *risc)
{
	if (NULL == risc->cpu)
		return;
	pci_free_consistent(pci, risc->size, risc->cpu, risc->dma);
	memset(risc,0,sizeof(*risc));
}

/* ---------------------------------------------------------- */
/* risc code generators                                       */

int
bttv_risc_packed(struct bttv *btv, struct bttv_riscmem *risc,
		 struct scatterlist *sglist,
		 int offset, int bpl, int padding, int lines)
{
	int instructions,rc,line,todo;
	struct scatterlist *sg;
	u32 *rp;

	/* estimate risc mem: worst case is one write per page border +
	   one write per scan line + sync + jump (all 2 dwords) */
	instructions  = (bpl * lines) / PAGE_SIZE + lines;
	instructions += 2;
	if ((rc = bttv_riscmem_alloc(btv->dev,risc,instructions*8)) < 0)
		return rc;

	/* sync instruction */
	rp = risc->cpu;
	*(rp++) = cpu_to_le32(BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1);
	*(rp++) = cpu_to_le32(0);

	/* scan lines */
	sg = sglist;
	for (line = 0; line < lines; line++) {
		while (offset >= sg_dma_len(sg)) {
			offset -= sg_dma_len(sg);
			sg++;
		}
		if (bpl <= sg_dma_len(sg)-offset) {
			/* fits into current chunk */
                        *(rp++)=cpu_to_le32(BT848_RISC_WRITE|BT848_RISC_SOL|
					    BT848_RISC_EOL|bpl);
                        *(rp++)=cpu_to_le32(sg_dma_address(sg)+offset);
                        offset+=bpl;
		} else {
			/* scanline needs to be splitted */
                        todo = bpl;
                        *(rp++)=cpu_to_le32(BT848_RISC_WRITE|BT848_RISC_SOL|
					    (sg_dma_len(sg)-offset));
                        *(rp++)=cpu_to_le32(sg_dma_address(sg)+offset);
                        todo -= (sg_dma_len(sg)-offset);
                        offset = 0;
                        sg++;
                        while (todo > sg_dma_len(sg)) {
                                *(rp++)=cpu_to_le32(BT848_RISC_WRITE|
						    sg_dma_len(sg));
                                *(rp++)=cpu_to_le32(sg_dma_address(sg));
				todo -= sg_dma_len(sg);
				sg++;
			}
                        *(rp++)=cpu_to_le32(BT848_RISC_WRITE|BT848_RISC_EOL|
					    todo);
			*(rp++)=cpu_to_le32(sg_dma_address(sg));
			offset += todo;
		}
		offset += padding;
	}

	/* save pointer to jmp instruction address */
	risc->jmp = rp;
	return 0;
}

int
bttv_risc_planar(struct bttv *btv, struct bttv_riscmem *risc,
		 struct scatterlist *sglist,
		 int yoffset, int ybpl, int ypadding, int ylines,
		 int uoffset, int voffset, int hshift, int vshift,
		 int cpadding)
{
	int instructions,rc,line,todo,ylen,chroma;
	u32 *rp,ri;
	struct scatterlist *ysg;
	struct scatterlist *usg;
	struct scatterlist *vsg;

	/* estimate risc mem: worst case is one write per page border +
	   one write per scan line (5 dwords)
	   plus sync + jump (2 dwords) */
	instructions  = (ybpl * ylines * 2) / PAGE_SIZE + ylines;
	instructions += 2;
	if ((rc = bttv_riscmem_alloc(btv->dev,risc,instructions*4*5)) < 0)
		return rc;

	/* sync instruction */
	rp = risc->cpu;
	*(rp++) = cpu_to_le32(BT848_RISC_SYNC|BT848_FIFO_STATUS_FM3);
	*(rp++) = cpu_to_le32(0);

	/* scan lines */
	ysg = sglist;
	usg = sglist;
	vsg = sglist;
	for (line = 0; line < ylines; line++) {
		switch (vshift) {
		case 0:  chroma = 1;           break;
		case 1:  chroma = !(line & 1); break;
		case 2:  chroma = !(line & 3); break;
		default: chroma = 0;
		}
		for (todo = ybpl; todo > 0; todo -= ylen) {
			/* go to next sg entry if needed */
			while (yoffset >= sg_dma_len(ysg)) {
				yoffset -= sg_dma_len(ysg);
				ysg++;
			}
			while (uoffset >= sg_dma_len(usg)) {
				uoffset -= sg_dma_len(usg);
				usg++;
			}
			while (voffset >= sg_dma_len(vsg)) {
				voffset -= sg_dma_len(vsg);
				vsg++;
			}

			/* calculate max number of bytes we can write */
			ylen = todo;
			if (yoffset + ylen > sg_dma_len(ysg))
				ylen = sg_dma_len(ysg) - yoffset;
			if (chroma) {
				if (uoffset + (ylen>>hshift) > sg_dma_len(usg))
					ylen = (sg_dma_len(usg) - uoffset) << hshift;
				if (voffset + (ylen>>hshift) > sg_dma_len(vsg))
					ylen = (sg_dma_len(vsg) - voffset) << hshift;
				ri = BT848_RISC_WRITE123;
			} else {
				ri = BT848_RISC_WRITE1S23;
			}
			if (ybpl == todo)
				ri |= BT848_RISC_SOL;
			if (ylen == todo)
				ri |= BT848_RISC_EOL;

			/* write risc instruction */
                        *(rp++)=cpu_to_le32(ri | ylen);
                        *(rp++)=cpu_to_le32(((ylen >> hshift) << 16) |
					    (ylen >> hshift));
			*(rp++)=cpu_to_le32(sg_dma_address(ysg)+yoffset);
			yoffset += ylen;
			if (chroma) {
				*(rp++)=cpu_to_le32(sg_dma_address(usg)+uoffset);
				uoffset += ylen >> hshift;
				*(rp++)=cpu_to_le32(sg_dma_address(vsg)+voffset);
				voffset += ylen >> hshift;
			}
		}
		yoffset += ypadding;
		if (chroma) {
			uoffset += cpadding;
			voffset += cpadding;
		}
	}

	/* save pointer to jmp instruction address */
	risc->jmp = rp;
	return 0;
}

/* ---------------------------------------------------------- */

struct SKIPLIST {
	int start;
	int end;
};

int
bttv_screen_clips(int swidth, int sheight, struct v4l2_rect *win,
		  struct v4l2_clip *clips, int n)
{
	if (win->left < 0) {
		/* left */
		clips[n].c.left = 0;
		clips[n].c.top = 0;
		clips[n].c.width  = -win->left;
		clips[n].c.height = win->height;
		n++;
	}
	if (win->left + win->width > swidth) {
		/* right */
		clips[n].c.left   = swidth - win->left;
		clips[n].c.top    = 0;
		clips[n].c.width  = win->width - clips[n].c.left;
		clips[n].c.height = win->height;
		n++;
	}
	if (win->top < 0) {
		/* top */
		clips[n].c.left = 0;
		clips[n].c.top = 0;
		clips[n].c.width  = win->width;
		clips[n].c.height = -win->top;
		n++;
	}
	if (win->top + win->height > sheight) {
		/* bottom */
		clips[n].c.left = 0;
		clips[n].c.top = sheight - win->top;
		clips[n].c.width  = win->width;
		clips[n].c.height = win->height - clips[n].c.top;
		n++;
	}
	return n;
}

void
bttv_sort_clips(struct v4l2_clip *clips, int nclips)
{
	struct v4l2_clip swap;
	int i,j,n;

	for (i = nclips-2; i >= 0; i--) {
		for (n = 0, j = 0; j <= i; j++) {
			if (clips[j].c.left > clips[j+1].c.left) {
				swap = clips[j];
				clips[j] = clips[j+1];
				clips[j+1] = swap;
				n++;
			}
		}
		if (0 == n)
			break;
	}
}

static void
calc_skips(int line, int width, int *maxy,
	   struct SKIPLIST *skips, int *nskips,
	   const struct v4l2_clip *clips, int nclips)
{
	int clip,skip,maxline,end;

	skip=0;
	maxline = 9999;
	for (clip = 0; clip < nclips; clip++) {

		/* sanity checks */
		if (clips[clip].c.left + clips[clip].c.width <= 0)
			continue;
		if (clips[clip].c.left > width)
			break;
		
		/* vertical range */
		if (line > clips[clip].c.top+clips[clip].c.height-1)
			continue;
		if (line < clips[clip].c.top) {
			if (maxline > clips[clip].c.top-1)
				maxline = clips[clip].c.top-1;
			continue;
		}
		if (maxline > clips[clip].c.top+clips[clip].c.height-1)
			maxline = clips[clip].c.top+clips[clip].c.height-1;

		/* horizontal range */
		if (0 == skip || clips[clip].c.left > skips[skip-1].end) {
			/* new one */
			skips[skip].start = clips[clip].c.left;
			if (skips[skip].start < 0)
				skips[skip].start = 0;
			skips[skip].end = clips[clip].c.left + clips[clip].c.width;
			if (skips[skip].end > width)
				skips[skip].end = width;
			skip++;
		} else {
			/* overlaps -- expand last one */
			end = clips[clip].c.left + clips[clip].c.width;
			if (skips[skip-1].end < end)
				skips[skip-1].end = end;
			if (skips[skip-1].end > width)
				skips[skip-1].end = width;
		}
	}
	*nskips = skip;
	*maxy = maxline;

	if (bttv_debug) {
		printk(KERN_DEBUG "bttv: skips line %d-%d:",line,maxline);
		for (skip = 0; skip < *nskips; skip++) {
			printk(" %d-%d",skips[skip].start,skips[skip].end);
		}
		printk("\n");
	}
}

int
bttv_risc_overlay(struct bttv *btv, struct bttv_riscmem *risc,
		  const struct bttv_format *fmt, struct bttv_overlay *ov,
		  int skip_even, int skip_odd)
{
	int instructions,rc,line,maxy,start,end,skip,nskips;
	struct SKIPLIST *skips;
	u32 *rp,ri,ra;
	u32 addr;

	/* skip list for window clipping */
	if (NULL == (skips = kmalloc(sizeof(*skips) * ov->nclips,GFP_KERNEL)))
		return -ENOMEM;
	
	/* estimate risc mem: worst case is (clip+1) * lines instructions
	   + sync + jump (all 2 dwords) */
	instructions  = (ov->nclips + 1) *
		((skip_even || skip_odd) ? ov->w.height>>1 :  ov->w.height);
	instructions += 2;
	if ((rc = bttv_riscmem_alloc(btv->dev,risc,instructions*8)) < 0)
		return rc;

	/* sync instruction */
	rp = risc->cpu;
	*(rp++) = cpu_to_le32(BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1);
	*(rp++) = cpu_to_le32(0);

	addr  = (unsigned long)btv->fbuf.base;
	addr += btv->fbuf.bytesperline * ov->w.top;
	addr += ((btv->fbuf.depth+7) >> 3) * ov->w.left;

	/* scan lines */
	for (maxy = -1, line = 0; line < ov->w.height;
	     line++, addr += btv->fbuf.bytesperline) {
		if ((line%2) == 0  &&  skip_even)
			continue;
		if ((line%2) == 1  &&  skip_odd)
			continue;

		/* calculate clipping */
		if (line > maxy)
			calc_skips(line, ov->w.width, &maxy,
				   skips, &nskips, ov->clips, ov->nclips);

		/* write out risc code */
		for (start = 0, skip = 0; start < ov->w.width; start = end) {
			if (skip >= nskips) {
				ri  = BT848_RISC_WRITE;
				end = ov->w.width;
			} else if (start < skips[skip].start) {
				ri  = BT848_RISC_WRITE;
				end = skips[skip].start;
			} else {
				ri  = BT848_RISC_SKIP;
				end = skips[skip].end;
				skip++;
			}
			if (BT848_RISC_WRITE == ri)
				ra = addr + (fmt->depth>>3)*start;
			else
				ra = 0;
				
			if (0 == start)
				ri |= BT848_RISC_SOL;
			if (ov->w.width == end)
				ri |= BT848_RISC_EOL;
			ri |= (fmt->depth>>3) * (end-start);

			*(rp++)=cpu_to_le32(ri);
			if (0 != ra)
				*(rp++)=cpu_to_le32(ra);
		}
	}

	/* save pointer to jmp instruction address */
	risc->jmp = rp;

	kfree(skips);
	return 0;
}

/* ---------------------------------------------------------- */

void
bttv_calc_geo(struct bttv *btv, struct bttv_geometry *geo,
	      int width, int height, int interleaved, int norm)
{
	const struct bttv_tvnorm *tvnorm = &bttv_tvnorms[norm];
        u32 xsf, sr;
	int vdelay;

	int swidth       = tvnorm->swidth;
	int totalwidth   = tvnorm->totalwidth;
	int scaledtwidth = tvnorm->scaledtwidth;

	if (bttv_tvcards[btv->type].muxsel[btv->input] < 0) {
		swidth       = 720;
		totalwidth   = 858;
		scaledtwidth = 858;
	}

	vdelay = tvnorm->vdelay;
#if 0 /* FIXME */
	if (vdelay < btv->vbi.lines*2)
		vdelay = btv->vbi.lines*2;
#endif

        xsf = (width*scaledtwidth)/swidth;
        geo->hscale =  ((totalwidth*4096UL)/xsf-4096);
        geo->hdelay =  tvnorm->hdelayx1;
        geo->hdelay =  (geo->hdelay*width)/swidth;
        geo->hdelay &= 0x3fe;
        sr = ((tvnorm->sheight >> (interleaved?0:1))*512)/height - 512;
        geo->vscale =  (0x10000UL-sr) & 0x1fff;
        geo->crop   =  ((width>>8)&0x03) | ((geo->hdelay>>6)&0x0c) |
                ((tvnorm->sheight>>4)&0x30) | ((vdelay>>2)&0xc0);
        geo->vscale |= interleaved ? (BT848_VSCALE_INT<<8) : 0;
        geo->vdelay  =  vdelay;
        geo->width   =  width;
        geo->sheight =  tvnorm->sheight;

        if (btv->opt_combfilter) {
                geo->vtc  = (width < 193) ? 2 : ((width < 385) ? 1 : 0);
                geo->comb = (width < 769) ? 1 : 0;
        } else {
                geo->vtc  = 0;
                geo->comb = 0;
        }
}

void
bttv_apply_geo(struct bttv *btv, struct bttv_geometry *geo, int odd)
{
        int off = odd ? 0x80 : 0x00;

	if (geo->comb)
		btor(BT848_VSCALE_COMB, BT848_E_VSCALE_HI+off);
	else
		btand(~BT848_VSCALE_COMB, BT848_E_VSCALE_HI+off);

        btwrite(geo->vtc,             BT848_E_VTC+off);
        btwrite(geo->hscale >> 8,     BT848_E_HSCALE_HI+off);
        btwrite(geo->hscale & 0xff,   BT848_E_HSCALE_LO+off);
        btaor((geo->vscale>>8), 0xe0, BT848_E_VSCALE_HI+off);
        btwrite(geo->vscale & 0xff,   BT848_E_VSCALE_LO+off);
        btwrite(geo->width & 0xff,    BT848_E_HACTIVE_LO+off);
        btwrite(geo->hdelay & 0xff,   BT848_E_HDELAY_LO+off);
        btwrite(geo->sheight & 0xff,  BT848_E_VACTIVE_LO+off);
        btwrite(geo->vdelay & 0xff,   BT848_E_VDELAY_LO+off);
        btwrite(geo->crop,            BT848_E_CROP+off);
}

/* ---------------------------------------------------------- */
/* risc group / risc main loop / dma management               */

void
bttv_set_dma(struct bttv *btv, int override, int irqflags)
{
	unsigned long cmd;
	int capctl;

	btv->cap_ctl = 0;
	if (NULL != btv->top)      btv->cap_ctl |= 0x02;
	if (NULL != btv->bottom)   btv->cap_ctl |= 0x01;
	if (NULL != btv->vcurr)    btv->cap_ctl |= 0x0c;

	capctl  = 0;
	capctl |= (btv->cap_ctl & 0x03) ? 0x03 : 0x00;  /* capture  */
	capctl |= (btv->cap_ctl & 0x0c) ? 0x0c : 0x00;  /* vbi data */
	capctl |= override;

	d2printk(KERN_DEBUG
		 "bttv%d: capctl=%x irq=%d top=%08Lx/%08Lx even=%08Lx/%08Lx\n",
		 btv->nr,capctl,irqflags,
		 btv->vcurr   ? (u64)btv->vcurr->top.dma      : 0,
		 btv->top     ? (u64)btv->top->top.dma        : 0,
		 btv->vcurr   ? (u64)btv->vcurr->bottom.dma   : 0,
		 btv->bottom  ? (u64)btv->bottom->bottom.dma  : 0);
	
	cmd = BT848_RISC_JUMP;
	if (irqflags) {
		cmd |= BT848_RISC_IRQ | (irqflags << 16);
		mod_timer(&btv->timeout, jiffies+BTTV_TIMEOUT);
	} else {
		del_timer(&btv->timeout);
	}
        btv->main.cpu[RISC_SLOT_LOOP] = cpu_to_le32(cmd);
	
	btaor(capctl, ~0x0f, BT848_CAP_CTL);
	if (capctl) {
		if (btv->dma_on)
			return;
		btwrite(btv->main.dma, BT848_RISC_STRT_ADD);
		btor(3, BT848_GPIO_DMA_CTL);
		btv->dma_on = 1;
	} else {
		if (!btv->dma_on)
			return;
                btand(~3, BT848_GPIO_DMA_CTL);
		btv->dma_on = 0;
	}
	return;
}

int
bttv_risc_init_main(struct bttv *btv)
{
	int rc;
	
	if ((rc = bttv_riscmem_alloc(btv->dev,&btv->main,PAGE_SIZE)) < 0)
		return rc;
	dprintk(KERN_DEBUG "bttv%d: risc main @ %08Lx\n",
		btv->nr,(u64)btv->main.dma);

	btv->main.cpu[0] = cpu_to_le32(BT848_RISC_SYNC | BT848_RISC_RESYNC |
				       BT848_FIFO_STATUS_VRE);
	btv->main.cpu[1] = cpu_to_le32(0);
	btv->main.cpu[2] = cpu_to_le32(BT848_RISC_JUMP);
	btv->main.cpu[3] = cpu_to_le32(btv->main.dma + (4<<2));

	/* top field */
	btv->main.cpu[4] = cpu_to_le32(BT848_RISC_JUMP);
	btv->main.cpu[5] = cpu_to_le32(btv->main.dma + (6<<2));
	btv->main.cpu[6] = cpu_to_le32(BT848_RISC_JUMP);
	btv->main.cpu[7] = cpu_to_le32(btv->main.dma + (8<<2));

        btv->main.cpu[8] = cpu_to_le32(BT848_RISC_SYNC | BT848_RISC_RESYNC |
				       BT848_FIFO_STATUS_VRO);
        btv->main.cpu[9] = cpu_to_le32(0);

	/* bottom field */
        btv->main.cpu[10] = cpu_to_le32(BT848_RISC_JUMP);
	btv->main.cpu[11] = cpu_to_le32(btv->main.dma + (12<<2));
        btv->main.cpu[12] = cpu_to_le32(BT848_RISC_JUMP);
	btv->main.cpu[13] = cpu_to_le32(btv->main.dma + (14<<2));

	/* jump back to top field */
	btv->main.cpu[14] = cpu_to_le32(BT848_RISC_JUMP);
        btv->main.cpu[15] = cpu_to_le32(btv->main.dma + (0<<2));

	return 0;
}

int
bttv_risc_hook(struct bttv *btv, int slot, struct bttv_riscmem *risc,
	       int irqflags)
{
	unsigned long cmd;
	unsigned long next = btv->main.dma + ((slot+2) << 2);

	if (NULL == risc) {
		d2printk(KERN_DEBUG "bttv%d: risc=%p slot[%d]=NULL\n",
			 btv->nr,risc,slot);
		btv->main.cpu[slot+1] = cpu_to_le32(next);
	} else {
		d2printk(KERN_DEBUG "bttv%d: risc=%p slot[%d]=%08Lx irq=%d\n",
			 btv->nr,risc,slot,(u64)risc->dma,irqflags);
		cmd = BT848_RISC_JUMP;
		if (irqflags)
			cmd |= BT848_RISC_IRQ | (irqflags << 16);
		risc->jmp[0] = cpu_to_le32(cmd);
		risc->jmp[1] = cpu_to_le32(next);
		btv->main.cpu[slot+1] = cpu_to_le32(risc->dma);
	}
	return 0;
}

void
bttv_dma_free(struct bttv *btv, struct bttv_buffer *buf)
{
	if (in_interrupt())
		BUG();
	videobuf_waiton(&buf->vb,0,0);
	videobuf_dma_pci_unmap(btv->dev, &buf->vb.dma);
	videobuf_dma_free(&buf->vb.dma);
	bttv_riscmem_free(btv->dev,&buf->bottom);
	bttv_riscmem_free(btv->dev,&buf->top);
	buf->vb.state = STATE_NEEDS_INIT;
}

int
bttv_buffer_activate(struct bttv *btv,
		     struct bttv_buffer *top,
		     struct bttv_buffer *bottom)
{
	if (NULL != top  &&  NULL != bottom) {
		top->vb.state  = STATE_ACTIVE;
		bottom->vb.state = STATE_ACTIVE;
		bttv_apply_geo(btv, &top->geo, 1);
		bttv_apply_geo(btv, &bottom->geo,0);
		bttv_risc_hook(btv, RISC_SLOT_O_FIELD, &top->top,       0);
		bttv_risc_hook(btv, RISC_SLOT_E_FIELD, &bottom->bottom, 0);
		btaor((top->btformat & 0xf0) | (bottom->btformat & 0x0f),
		      ~0xff, BT848_COLOR_FMT);
		btaor((top->btswap & 0x0a) | (bottom->btswap & 0x05),
		      ~0x0f, BT848_COLOR_CTL);
	} else if (NULL != top) {
		top->vb.state  = STATE_ACTIVE;
		bttv_apply_geo(btv, &top->geo,1);
		bttv_apply_geo(btv, &top->geo,0);
		bttv_risc_hook(btv, RISC_SLOT_O_FIELD, &top->top, 0);
		bttv_risc_hook(btv, RISC_SLOT_E_FIELD, NULL,      0);
		btaor(top->btformat & 0xff, ~0xff, BT848_COLOR_FMT);
		btaor(top->btswap & 0x0f,   ~0x0f, BT848_COLOR_CTL);
	} else if (NULL != bottom) {
		bottom->vb.state = STATE_ACTIVE;
		bttv_apply_geo(btv, &bottom->geo,1);
		bttv_apply_geo(btv, &bottom->geo,0);
		bttv_risc_hook(btv, RISC_SLOT_O_FIELD, NULL,            0);
		bttv_risc_hook(btv, RISC_SLOT_E_FIELD, &bottom->bottom, 0);
		btaor(bottom->btformat & 0xff, ~0xff, BT848_COLOR_FMT);
		btaor(bottom->btswap & 0x0f,   ~0x0f, BT848_COLOR_CTL);
	} else {
		bttv_risc_hook(btv, RISC_SLOT_O_FIELD, NULL, 0);
		bttv_risc_hook(btv, RISC_SLOT_E_FIELD, NULL, 0);
	}
	return 0;
}

/* ---------------------------------------------------------- */

/* calculate geometry, build risc code */
int
bttv_buffer_risc(struct bttv *btv, struct bttv_buffer *buf)
{
	const struct bttv_tvnorm *tvnorm = bttv_tvnorms + buf->tvnorm;

	dprintk(KERN_DEBUG
		"bttv%d: buffer field: %s  format: %s  size: %dx%d\n",
		btv->nr, v4l2_field_names[buf->vb.field],
		buf->fmt->name, buf->vb.width, buf->vb.height);

	/* packed pixel modes */
	if (buf->fmt->flags & FORMAT_FLAGS_PACKED) {
		int bpl = (buf->fmt->depth >> 3) * buf->vb.width;
		int bpf = bpl * (buf->vb.height >> 1);

		bttv_calc_geo(btv,&buf->geo,buf->vb.width,buf->vb.height,
			      V4L2_FIELD_HAS_BOTH(buf->vb.field),buf->tvnorm);
		
		switch (buf->vb.field) {
		case V4L2_FIELD_TOP:
			bttv_risc_packed(btv,&buf->top,buf->vb.dma.sglist,
					 0,bpl,0,buf->vb.height);
			break;
		case V4L2_FIELD_BOTTOM:
			bttv_risc_packed(btv,&buf->bottom,buf->vb.dma.sglist,
					 0,bpl,0,buf->vb.height);
			break;
		case V4L2_FIELD_INTERLACED:
			bttv_risc_packed(btv,&buf->top,buf->vb.dma.sglist,
					 0,bpl,bpl,buf->vb.height >> 1);
			bttv_risc_packed(btv,&buf->bottom,buf->vb.dma.sglist,
					 bpl,bpl,bpl,buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_TB:
			bttv_risc_packed(btv,&buf->top,buf->vb.dma.sglist,
					 0,bpl,0,buf->vb.height >> 1);
			bttv_risc_packed(btv,&buf->bottom,buf->vb.dma.sglist,
					 bpf,bpl,0,buf->vb.height >> 1);
			break;
		default:
			BUG();
		}
	}

	/* planar modes */
	if (buf->fmt->flags & FORMAT_FLAGS_PLANAR) {
		int uoffset, voffset;
		int ypadding, cpadding, lines;

		/* calculate chroma offsets */
		uoffset = buf->vb.width * buf->vb.height;
		voffset = buf->vb.width * buf->vb.height;
		if (buf->fmt->flags & FORMAT_FLAGS_CrCb) {
			/* Y-Cr-Cb plane order */
			uoffset >>= buf->fmt->hshift;
			uoffset >>= buf->fmt->vshift;
			uoffset += voffset;
		} else {
			/* Y-Cb-Cr plane order */
			voffset >>= buf->fmt->hshift;
			voffset >>= buf->fmt->vshift;
			voffset += uoffset;
		}

		switch (buf->vb.field) {
		case V4L2_FIELD_TOP:
			bttv_calc_geo(btv,&buf->geo,buf->vb.width,
				      buf->vb.height,0,buf->tvnorm);
			bttv_risc_planar(btv, &buf->top, buf->vb.dma.sglist,
					 0,buf->vb.width,0,buf->vb.height,
					 uoffset,voffset,buf->fmt->hshift,
					 buf->fmt->vshift,0);
			break;
		case V4L2_FIELD_BOTTOM:
			bttv_calc_geo(btv,&buf->geo,buf->vb.width,
				      buf->vb.height,0,buf->tvnorm);
			bttv_risc_planar(btv, &buf->bottom, buf->vb.dma.sglist,
					 0,buf->vb.width,0,buf->vb.height,
					 uoffset,voffset,buf->fmt->hshift,
					 buf->fmt->vshift,0);
			break;
		case V4L2_FIELD_INTERLACED:
			bttv_calc_geo(btv,&buf->geo,buf->vb.width,
				      buf->vb.height,1,buf->tvnorm);
			lines    = buf->vb.height >> 1;
			ypadding = buf->vb.width;
			cpadding = buf->vb.width >> buf->fmt->hshift;
			bttv_risc_planar(btv,&buf->top,
					 buf->vb.dma.sglist,
					 0,buf->vb.width,ypadding,lines,
					 uoffset,voffset,
					 buf->fmt->hshift,
					 buf->fmt->vshift,
					 cpadding);
			bttv_risc_planar(btv,&buf->bottom,
					 buf->vb.dma.sglist,
					 ypadding,buf->vb.width,ypadding,lines,
					 uoffset+cpadding,
					 voffset+cpadding,
					 buf->fmt->hshift,
					 buf->fmt->vshift,
					 cpadding);
			break;
		default:
			BUG();
		}
	}

	/* raw data */
	if (buf->fmt->flags & FORMAT_FLAGS_RAW) {
		/* build risc code */
		buf->vb.field = V4L2_FIELD_SEQ_TB;
		bttv_calc_geo(btv,&buf->geo,tvnorm->swidth,tvnorm->sheight,
			      1,buf->tvnorm);
		bttv_risc_packed(btv, &buf->top,  buf->vb.dma.sglist,
				 0, RAW_BPL, 0, RAW_LINES);
		bttv_risc_packed(btv, &buf->bottom, buf->vb.dma.sglist,
				 buf->vb.size/2 , RAW_BPL, 0, RAW_LINES);
	}

	/* copy format info */
	buf->btformat = buf->fmt->btformat;
	buf->btswap   = buf->fmt->btswap;
	return 0;
}

/* ---------------------------------------------------------- */

/* calculate geometry, build risc code */
int
bttv_overlay_risc(struct bttv *btv,
		  struct bttv_overlay *ov,
		  const struct bttv_format *fmt,
		  struct bttv_buffer *buf)
{
	/* check interleave, bottom+top fields */
	dprintk(KERN_DEBUG
		"bttv%d: overlay fields: %s format: %s  size: %dx%d\n",
		btv->nr, v4l2_field_names[buf->vb.field],
		fmt->name,ov->w.width,ov->w.height);

	/* calculate geometry */
	bttv_calc_geo(btv,&buf->geo,ov->w.width,ov->w.height,
		      V4L2_FIELD_HAS_BOTH(ov->field), ov->tvnorm);

	/* build risc code */
	switch (ov->field) {
	case V4L2_FIELD_TOP:
		bttv_risc_overlay(btv, &buf->top,   fmt, ov, 0, 0);
		break;
	case V4L2_FIELD_BOTTOM:
		bttv_risc_overlay(btv, &buf->bottom, fmt, ov, 0, 0);
		break;
	case V4L2_FIELD_INTERLACED:
#if 0
		bttv_risc_overlay(btv, &buf->top,    fmt, ov, 1, 0);
		bttv_risc_overlay(btv, &buf->bottom, fmt, ov, 0, 1);
#else
		bttv_risc_overlay(btv, &buf->top,    fmt, ov, 0, 1);
		bttv_risc_overlay(btv, &buf->bottom, fmt, ov, 1, 0);
#endif
		break;
	default:
		BUG();
	}

	/* copy format info */
	buf->btformat = fmt->btformat;
	buf->btswap   = fmt->btswap;
	buf->vb.field = ov->field;
	return 0;
}

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
