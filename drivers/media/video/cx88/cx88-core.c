/*
 * device driver for Conexant 2388x based TV cards
 * driver core
 *
 * (c) 2003 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/sound.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/videodev.h>

#include "cx88.h"

MODULE_DESCRIPTION("v4l2 driver module for cx2388x based TV cards");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------------ */

#if 0
static unsigned int gpio_tracking = 0;
MODULE_PARM(gpio_tracking,"i");
MODULE_PARM_DESC(gpio_tracking,"enable debug messages [gpio]");

static unsigned int ts_nr = -1;
MODULE_PARM(ts_nr,"i");
MODULE_PARM_DESC(ts_nr,"ts device number");

static unsigned int vbi_nr = -1;
MODULE_PARM(vbi_nr,"i");
MODULE_PARM_DESC(vbi_nr,"vbi device number");

static unsigned int radio_nr = -1;
MODULE_PARM(radio_nr,"i");
MODULE_PARM_DESC(radio_nr,"radio device number");

static unsigned int oss = 0;
MODULE_PARM(oss,"i");
MODULE_PARM_DESC(oss,"register oss devices (default: no)");

static unsigned int dsp_nr = -1;
MODULE_PARM(dsp_nr,"i");
MODULE_PARM_DESC(dsp_nr,"oss dsp device number");

static unsigned int mixer_nr = -1;
MODULE_PARM(mixer_nr,"i");
MODULE_PARM_DESC(mixer_nr,"oss mixer device number");
#endif

static unsigned int core_debug = 0;
MODULE_PARM(core_debug,"i");
MODULE_PARM_DESC(core_debug,"enable debug messages [core]");

#define dprintk(fmt, arg...)	if (core_debug) \
	printk(KERN_DEBUG "%s/core: " fmt, dev->name , ## arg)

/* ------------------------------------------------------------------ */
/* debug help functions                                               */

static const char *v4l1_ioctls[] = {
	"0", "CGAP", "GCHAN", "SCHAN", "GTUNER", "STUNER", "GPICT", "SPICT",
	"CCAPTURE", "GWIN", "SWIN", "GFBUF", "SFBUF", "KEY", "GFREQ",
	"SFREQ", "GAUDIO", "SAUDIO", "SYNC", "MCAPTURE", "GMBUF", "GUNIT",
	"GCAPTURE", "SCAPTURE", "SPLAYMODE", "SWRITEMODE", "GPLAYINFO",
	"SMICROCODE", "GVBIFMT", "SVBIFMT" };
#define V4L1_IOCTLS ARRAY_SIZE(v4l1_ioctls)

static const char *v4l2_ioctls[] = {
	"QUERYCAP", "1", "ENUM_PIXFMT", "ENUM_FBUFFMT", "G_FMT", "S_FMT",
	"G_COMP", "S_COMP", "REQBUFS", "QUERYBUF", "G_FBUF", "S_FBUF",
	"G_WIN", "S_WIN", "PREVIEW", "QBUF", "16", "DQBUF", "STREAMON",
	"STREAMOFF", "G_PERF", "G_PARM", "S_PARM", "G_STD", "S_STD",
	"ENUMSTD", "ENUMINPUT", "G_CTRL", "S_CTRL", "G_TUNER", "S_TUNER",
	"G_FREQ", "S_FREQ", "G_AUDIO", "S_AUDIO", "35", "QUERYCTRL",
	"QUERYMENU", "G_INPUT", "S_INPUT", "ENUMCVT", "41", "42", "43",
	"44", "45",  "G_OUTPUT", "S_OUTPUT", "ENUMOUTPUT", "G_AUDOUT",
	"S_AUDOUT", "ENUMFX", "G_EFFECT", "S_EFFECT", "G_MODULATOR",
	"S_MODULATOR"
};
#define V4L2_IOCTLS ARRAY_SIZE(v4l2_ioctls)

void cx88_print_ioctl(char *name, unsigned int cmd)
{
	char *dir;

	switch (_IOC_DIR(cmd)) {
	case _IOC_NONE:              dir = "--"; break;
	case _IOC_READ:              dir = "r-"; break;
	case _IOC_WRITE:             dir = "-w"; break;
	case _IOC_READ | _IOC_WRITE: dir = "rw"; break;
	default:                     dir = "??"; break;
	}
	switch (_IOC_TYPE(cmd)) {
	case 'v':
		printk(KERN_DEBUG "%s: ioctl 0x%08x (v4l1, %s, VIDIOC%s)\n",
		       name, cmd, dir, (_IOC_NR(cmd) < V4L1_IOCTLS) ?
		       v4l1_ioctls[_IOC_NR(cmd)] : "???");
		break;
	case 'V':
		printk(KERN_DEBUG "%s: ioctl 0x%08x (v4l2, %s, VIDIOC_%s)\n",
		       name, cmd, dir, (_IOC_NR(cmd) < V4L2_IOCTLS) ?
		       v4l2_ioctls[_IOC_NR(cmd)] : "???");
		break;
	default:
		printk(KERN_DEBUG "%s: ioctl 0x%08x (???, %s, #%d)\n",
		       name, cmd, dir, _IOC_NR(cmd));
	}
}

/* ------------------------------------------------------------------ */

static u32* cx88_risc_field(u32 *rp, struct scatterlist *sglist,
			    unsigned int offset, u32 sync_line,
			    unsigned int bpl, unsigned int padding,
			    unsigned int lines)
{
	struct scatterlist *sg;
	unsigned int line,todo;

	/* sync instruction */
	*(rp++) = cpu_to_le32(RISC_RESYNC | sync_line);
	
	/* scan lines */
	sg = sglist;
	for (line = 0; line < lines; line++) {
		while (offset && offset >= sg_dma_len(sg)) {
			offset -= sg_dma_len(sg);
			sg++;
		}
		if (bpl <= sg_dma_len(sg)-offset) {
			/* fits into current chunk */
                        *(rp++)=cpu_to_le32(RISC_WRITE|RISC_SOL|RISC_EOL|bpl);
                        *(rp++)=cpu_to_le32(sg_dma_address(sg)+offset);
                        offset+=bpl;
		} else {
			/* scanline needs to be splitted */
                        todo = bpl;
                        *(rp++)=cpu_to_le32(RISC_WRITE|RISC_SOL|
					    (sg_dma_len(sg)-offset));
                        *(rp++)=cpu_to_le32(sg_dma_address(sg)+offset);
                        todo -= (sg_dma_len(sg)-offset);
                        offset = 0;
                        sg++;
                        while (todo > sg_dma_len(sg)) {
                                *(rp++)=cpu_to_le32(RISC_WRITE|
						    sg_dma_len(sg));
                                *(rp++)=cpu_to_le32(sg_dma_address(sg));
				todo -= sg_dma_len(sg);
				sg++;
			}
                        *(rp++)=cpu_to_le32(RISC_WRITE|RISC_EOL|todo);
			*(rp++)=cpu_to_le32(sg_dma_address(sg));
			offset += todo;
		}
		offset += padding;
	}

	return rp;
}

int cx88_risc_buffer(struct pci_dev *pci, struct btcx_riscmem *risc,
		     struct scatterlist *sglist,
		     unsigned int top_offset, unsigned int bottom_offset,
		     unsigned int bpl, unsigned int padding, unsigned int lines)
{
	u32 instructions,fields;
	u32 *rp;
	int rc;

	fields = 0;
	if (UNSET != top_offset)
		fields++;
	if (UNSET != bottom_offset)
		fields++;

	/* estimate risc mem: worst case is one write per page border +
	   one write per scan line + syncs + jump (all 2 dwords) */
	instructions  = (bpl * lines * fields) / PAGE_SIZE + lines * fields;
	instructions += 3 + 4;
	if ((rc = btcx_riscmem_alloc(pci,risc,instructions*8)) < 0)
		return rc;

	/* write risc instructions */
	rp = risc->cpu;
	if (UNSET != top_offset)
		rp = cx88_risc_field(rp, sglist, top_offset, 0,
				     bpl, padding, lines);
	if (UNSET != bottom_offset)
		rp = cx88_risc_field(rp, sglist, bottom_offset, 0x200,
				     bpl, padding, lines);

	/* save pointer to jmp instruction address */
	risc->jmp = rp;
	return 0;
}

int cx88_risc_stopper(struct pci_dev *pci, struct btcx_riscmem *risc,
		      u32 reg, u32 mask, u32 value)
{
	u32 *rp;
	int rc;

	if ((rc = btcx_riscmem_alloc(pci, risc, 4*16)) < 0)
		return rc;

	/* write risc instructions */
	rp = risc->cpu;
	*(rp++) = cpu_to_le32(RISC_WRITECR  | RISC_IRQ2 | RISC_IMM);
	*(rp++) = cpu_to_le32(reg);
	*(rp++) = cpu_to_le32(value);
	*(rp++) = cpu_to_le32(mask);
	*(rp++) = cpu_to_le32(RISC_JUMP);
	*(rp++) = cpu_to_le32(risc->dma);
	return 0;
}

void
cx88_free_buffer(struct pci_dev *pci, struct cx88_buffer *buf)
{
	if (in_interrupt())
		BUG();
	videobuf_waiton(&buf->vb,0,0);
	videobuf_dma_pci_unmap(pci, &buf->vb.dma);
	videobuf_dma_free(&buf->vb.dma);
	btcx_riscmem_free(pci, &buf->risc);
	buf->vb.state = STATE_NEEDS_INIT;
}

/* ------------------------------------------------------------------ */
/* our SRAM memory layout                                             */

/* we are going to put all thr risc programs into host memory, so we
 * can use the whole SDRAM for the DMA fifos.  To simplify things, we
 * use a static memory layout.  That surely will waste memory in case
 * we don't use all DMA channels at the same time (which will be the
 * case most of the time).  But that still gives us enougth FIFO space
 * to be able to deal with insane long pci latencies ...
 *
 * FIFO space allocations:
 *    channel  21    (y video)  - 10.0k
 *    channel  24    (vbi)      -  4.0k
 *    channels 25+26 (audio)    -  0.5k
 *    everything else           -  2.0k
 *    TOTAL                     = 29.0k
 *
 * Every channel has 160 bytes control data (64 bytes instruction
 * queue and 6 CDT entries), which is close to 2k total.
 * 
 * Address layout:
 *    0x0000 - 0x03ff    CMDs / reserved
 *    0x0400 - 0x0bff    instruction queues + CDs
 *    0x0c00 -           FIFOs
 */

struct sram_channel cx88_sram_channels[] = {
	[SRAM_CH21] = {
		.name       = "video y / packed",
		.cmds_start = 0x180040,
		.ctrl_start = 0x180400,
	        .cdt        = 0x180400 + 64,
		.fifo_start = 0x180c00,
		.fifo_size  = 0x002800,
		.ptr1_reg   = MO_DMA21_PTR1,
		.ptr2_reg   = MO_DMA21_PTR2,
		.cnt1_reg   = MO_DMA21_CNT1,
		.cnt2_reg   = MO_DMA21_CNT2,
	},
	[SRAM_CH22] = {
		.name       = "video u",
		.cmds_start = 0x180080,
		.ctrl_start = 0x1804a0,
	        .cdt        = 0x1804a0 + 64,
		.fifo_start = 0x183400,
		.fifo_size  = 0x000800,
		.ptr1_reg   = MO_DMA22_PTR1,
		.ptr2_reg   = MO_DMA22_PTR2,
		.cnt1_reg   = MO_DMA22_CNT1,
		.cnt2_reg   = MO_DMA22_CNT2,
	},
	[SRAM_CH23] = {
		.name       = "video v",
		.cmds_start = 0x1800c0,
		.ctrl_start = 0x180540,
	        .cdt        = 0x180540 + 64,
		.fifo_start = 0x183c00,
		.fifo_size  = 0x000800,
		.ptr1_reg   = MO_DMA23_PTR1,
		.ptr2_reg   = MO_DMA23_PTR2,
		.cnt1_reg   = MO_DMA23_CNT1,
		.cnt2_reg   = MO_DMA23_CNT2,
	},
	[SRAM_CH24] = {
		.name       = "vbi",
		.cmds_start = 0x180100,
		.ctrl_start = 0x1805e0,
	        .cdt        = 0x1805e0 + 64,
		.fifo_start = 0x184400,
		.fifo_size  = 0x001000,
		.ptr1_reg   = MO_DMA24_PTR1,
		.ptr2_reg   = MO_DMA24_PTR2,
		.cnt1_reg   = MO_DMA24_CNT1,
		.cnt2_reg   = MO_DMA24_CNT2,
	},
	[SRAM_CH25] = {
		.name       = "audio from",
		.cmds_start = 0x180140,
		.ctrl_start = 0x180680,
	        .cdt        = 0x180680 + 64,
		.fifo_start = 0x185400,
		.fifo_size  = 0x000200,
		.ptr1_reg   = MO_DMA25_PTR1,
		.ptr2_reg   = MO_DMA25_PTR2,
		.cnt1_reg   = MO_DMA25_CNT1,
		.cnt2_reg   = MO_DMA25_CNT2,
	},
	[SRAM_CH26] = {
		.name       = "audio to",
		.cmds_start = 0x180180,
		.ctrl_start = 0x180720,
	        .cdt        = 0x180680 + 64,  /* same as audio IN */
		.fifo_start = 0x185400,       /* same as audio IN */
		.fifo_size  = 0x000200,       /* same as audio IN */
		.ptr1_reg   = MO_DMA26_PTR1,
		.ptr2_reg   = MO_DMA26_PTR2,
		.cnt1_reg   = MO_DMA26_CNT1,
		.cnt2_reg   = MO_DMA26_CNT2,
	},
};

int cx88_sram_channel_setup(struct cx8800_dev *dev,
			    struct sram_channel *ch,
			    unsigned int bpl, u32 risc)
{
	unsigned int i,lines;
	u32 cdt;

	bpl   = (bpl + 7) & ~7; /* alignment */
	cdt   = ch->cdt;
	lines = ch->fifo_size / bpl;
	if (lines > 6)
		lines = 6;
	BUG_ON(lines < 2);

	/* write CDT */
	for (i = 0; i < lines; i++)
		cx_write(cdt + 16*i, ch->fifo_start + bpl*i);

	/* write CMDS */
	cx_write(ch->cmds_start +  0, risc);
	cx_write(ch->cmds_start +  4, cdt);
	cx_write(ch->cmds_start +  8, (lines*16) >> 3);
	cx_write(ch->cmds_start + 12, ch->ctrl_start);
	cx_write(ch->cmds_start + 16, 64 >> 2);
	for (i = 20; i < 64; i += 4)
		cx_write(ch->cmds_start + i, 0);

	/* fill registers */
	cx_write(ch->ptr1_reg, ch->fifo_start);
	cx_write(ch->ptr2_reg, cdt);
	cx_write(ch->cnt1_reg, bpl >> 3);
	cx_write(ch->cnt2_reg, (lines*16) >> 3);

	dprintk("sram setup %s: bpl=%d lines=%d\n", ch->name, bpl, lines);
	return 0;
}

/* ------------------------------------------------------------------ */
/* debug helper code                                                  */

int cx88_risc_decode(u32 risc)
{
	static char *instr[16] = {
		[ RISC_SYNC    >> 28 ] = "sync",
		[ RISC_WRITE   >> 28 ] = "write",
		[ RISC_WRITEC  >> 28 ] = "writec",
		[ RISC_READ    >> 28 ] = "read",
		[ RISC_READC   >> 28 ] = "readc",
		[ RISC_JUMP    >> 28 ] = "jump",
		[ RISC_SKIP    >> 28 ] = "skip",
		[ RISC_WRITERM >> 28 ] = "writerm",
		[ RISC_WRITECM >> 28 ] = "writecm",
		[ RISC_WRITECR >> 28 ] = "writecr",
	};
	static int incr[16] = {
		[ RISC_WRITE   >> 28 ] = 2,
		[ RISC_JUMP    >> 28 ] = 2,
		[ RISC_WRITERM >> 28 ] = 3,
		[ RISC_WRITECM >> 28 ] = 3,
		[ RISC_WRITECR >> 28 ] = 4,
	};
	static char *bits[] = {
		"12",   "13",   "14",   "resync",
		"cnt0", "cnt1", "18",   "19",
		"20",   "21",   "22",   "23",
		"irq1", "irq2", "eol",  "sol",
	};
	int i;

	printk("0x%08x [ %s", risc,
	       instr[risc >> 28] ? instr[risc >> 28] : "INVALID");
	for (i = ARRAY_SIZE(bits)-1; i >= 0; i--)
		if (risc & (1 << (i + 12)))
			printk(" %s",bits[i]);
	printk(" count=%d ]\n", risc & 0xfff);
	return incr[risc >> 28] ? incr[risc >> 28] : 1;
}

void cx88_risc_disasm(struct cx8800_dev *dev,
		      struct btcx_riscmem *risc)
{
	unsigned int i,j,n;
	
	printk("%s: risc disasm: %p [dma=0x%08lx]\n",
	       dev->name, risc->cpu, (unsigned long)risc->dma);
	for (i = 0; i < (risc->size >> 2); i += n) {
		printk("%s:   %04d: ", dev->name, i);
		n = cx88_risc_decode(risc->cpu[i]);
		for (j = 1; j < n; j++)
			printk("%s:   %04d: 0x%08x [ arg #%d ]\n",
			       dev->name, i+j, risc->cpu[i+j], j);
		if (risc->cpu[i] == RISC_JUMP)
			break;
	}
}

void cx88_sram_channel_dump(struct cx8800_dev *dev,
			    struct sram_channel *ch)
{
	static char *name[] = {
		"initial risc",
		"cdt base",
		"cdt size",
		"iq base",
		"iq size",
		"risc pc",
		"iq wr ptr",
		"iq rd ptr",
		"cdt current",
		"pci target",
		"line / byte",
	};
	u32 risc;
	unsigned int i,j,n;

	printk("%s: %s - dma channel status dump\n",dev->name,ch->name);
	for (i = 0; i < ARRAY_SIZE(name); i++)
		printk("%s:   cmds: %-12s: 0x%08x\n",
		       dev->name,name[i],
		       cx_read(ch->cmds_start + 4*i));
	for (i = 0; i < 4; i++) {
		risc = cx_read(ch->cmds_start + 4 * (i+11));
		printk("%s:   risc%d: ", dev->name, i);
		cx88_risc_decode(risc);
	}
	for (i = 0; i < 16; i += n) {
		risc = cx_read(ch->ctrl_start + 4 * i);
		printk("%s:   iq %x: ", dev->name, i);
		n = cx88_risc_decode(risc);
		for (j = 1; j < n; j++) {
			risc = cx_read(ch->ctrl_start + 4 * (i+j));
			printk("%s:   iq %x: 0x%08x [ arg #%d ]\n",
			       dev->name, i+j, risc, j);
		}
	}

	printk("%s: fifo: 0x%08x -> 0x%x\n",
	       dev->name, ch->fifo_start, ch->fifo_start+ch->fifo_size);
	printk("%s: ctrl: 0x%08x -> 0x%x\n",
	       dev->name, ch->ctrl_start, ch->ctrl_start+6*16);
	printk("%s:   ptr1_reg: 0x%08x\n",
	       dev->name,cx_read(ch->ptr1_reg));
	printk("%s:   ptr2_reg: 0x%08x\n",
	       dev->name,cx_read(ch->ptr2_reg));
	printk("%s:   cnt1_reg: 0x%08x\n",
	       dev->name,cx_read(ch->cnt1_reg));
	printk("%s:   cnt2_reg: 0x%08x\n",
	       dev->name,cx_read(ch->cnt2_reg));
}

char *cx88_pci_irqs[32] = {
	"vid", "aud", "ts", "vip", "hst", "5", "6", "tm1", 
	"src_dma", "dst_dma", "risc_rd_err", "risc_wr_err",
	"brdg_err", "src_dma_err", "dst_dma_err", "ipb_dma_err",
	"i2c", "i2c_rack", "ir_smp", "gpio0", "gpio1"
};
char *cx88_vid_irqs[32] = {
	"y_risci1", "u_risci1", "v_risci1", "vbi_risc1", 
	"y_risci2", "u_risci2", "v_risci2", "vbi_risc2",
	"y_oflow",  "u_oflow",  "v_oflow",  "vbi_oflow",
	"y_sync",   "u_sync",   "v_sync",   "vbi_sync",
	"opc_err",  "par_err",  "rip_err",  "pci_abort",
};

void cx88_print_irqbits(char *name, char *tag, char **strings,
			u32 bits, u32 mask)
{
	unsigned int i;

	printk(KERN_DEBUG "%s: %s [0x%x]", name, tag, bits);
	for (i = 0; i < 32; i++) {
		if (!(bits & (1 << i)))
			continue;
		printk(" %s",strings[i]);
		if (!(mask & (1 << i)))
			continue;
		printk("*");
	}
	printk("\n");
}

/* ------------------------------------------------------------------ */

int cx88_pci_quirks(char *name, struct pci_dev *pci, unsigned int *latency)
{
	u8 ctrl = 0;
	u8 value;

	if (0 == pci_pci_problems)
		return 0;

	if (pci_pci_problems & PCIPCI_TRITON) {
		printk(KERN_INFO "%s: quirk: PCIPCI_TRITON -- set TBFX\n",
		       name);
		ctrl |= CX88X_EN_TBFX;
	}
	if (pci_pci_problems & PCIPCI_NATOMA) {
		printk(KERN_INFO "%s: quirk: PCIPCI_NATOMA -- set TBFX\n",
		       name);
		ctrl |= CX88X_EN_TBFX;
	}
	if (pci_pci_problems & PCIPCI_VIAETBF) {
		printk(KERN_INFO "%s: quirk: PCIPCI_VIAETBF -- set TBFX\n",
		       name);
		ctrl |= CX88X_EN_TBFX;
	}
	if (pci_pci_problems & PCIPCI_VSFX) {
		printk(KERN_INFO "%s: quirk: PCIPCI_VSFX -- set VSFX\n",
		       name);
		ctrl |= CX88X_EN_VSFX;
	}
#ifdef PCIPCI_ALIMAGIK
	if (pci_pci_problems & PCIPCI_ALIMAGIK) {
		printk(KERN_INFO "%s: quirk: PCIPCI_ALIMAGIK -- latency fixup\n",
		       name);
		*latency = 0x0A;
	}
#endif
	if (ctrl) {
		pci_read_config_byte(pci, CX88X_DEVCTRL, &value);
		value |= ctrl;
		pci_write_config_byte(pci, CX88X_DEVCTRL, value);
	}
	return 0;
}

/* ------------------------------------------------------------------ */

EXPORT_SYMBOL(cx88_print_ioctl);
EXPORT_SYMBOL(cx88_pci_irqs);
EXPORT_SYMBOL(cx88_vid_irqs);
EXPORT_SYMBOL(cx88_print_irqbits);

EXPORT_SYMBOL(cx88_risc_buffer);
EXPORT_SYMBOL(cx88_risc_stopper);
EXPORT_SYMBOL(cx88_free_buffer);

EXPORT_SYMBOL(cx88_risc_disasm);

EXPORT_SYMBOL(cx88_sram_channels);
EXPORT_SYMBOL(cx88_sram_channel_setup);
EXPORT_SYMBOL(cx88_sram_channel_dump);

EXPORT_SYMBOL(cx88_pci_quirks);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
