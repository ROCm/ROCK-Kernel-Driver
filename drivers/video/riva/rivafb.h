#ifndef __RIVAFB_H
#define __RIVAFB_H

#include <linux/config.h>
#include <linux/fb.h>
#include <video/vga.h>
#include "riva_hw.h"

/* GGI compatibility macros */
#define NUM_SEQ_REGS		0x05
#define NUM_CRT_REGS		0x41
#define NUM_GRC_REGS		0x09
#define NUM_ATC_REGS		0x15

/* holds the state of the VGA core and extended Riva hw state from riva_hw.c.
 * From KGI originally. */
struct riva_regs {
	u8 attr[NUM_ATC_REGS];
	u8 crtc[NUM_CRT_REGS];
	u8 gra[NUM_GRC_REGS];
	u8 seq[NUM_SEQ_REGS];
	u8 misc_output;
	RIVA_HW_STATE ext;
};

typedef struct {
	unsigned char red, green, blue, transp;
} riva_cfb8_cmap_t;

struct riva_par {
	RIVA_HW_INST riva;	/* interface to riva_hw.c */

	caddr_t ctrl_base;	/* virtual control register base addr */
	unsigned dclk_max;	/* max DCLK */

	struct riva_regs initial_state;	/* initial startup video mode */
	struct riva_regs current_state;
	struct vgastate state;
	atomic_t ref_count;
	riva_cfb8_cmap_t cmap[256];	/* VGA DAC palette cache */
	u32 riva_palette[16];
	u32 cursor_data[32 * 32/4];
	int cursor_reset;
#ifdef CONFIG_MTRR
	struct { int vram; int vram_valid; } mtrr;
#endif
};

#endif /* __RIVAFB_H */
