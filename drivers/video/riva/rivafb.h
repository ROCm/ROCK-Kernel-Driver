#ifndef __RIVAFB_H
#define __RIVAFB_H

#include <linux/config.h>
#include <linux/fb.h>
#include <linux/timer.h>
#include <video/fbcon.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb32.h>
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

#define MAX_CURS                32

struct riva_cursor {
        int enable;
        int on;
        int vbl_cnt;
        int last_move_delay;
        int blink_rate;
        struct {
                u16 x, y;
        } pos, size;
        unsigned short image[MAX_CURS*MAX_CURS];
        struct timer_list *timer;
};

/* describes the state of a Riva board */
struct riva_par {
	RIVA_HW_INST riva;	/* interface to riva_hw.c */

	unsigned ram_amount;	/* amount of RAM on card, in bytes */
	unsigned dclk_max;	/* max DCLK */

	struct riva_regs initial_state;	/* initial startup video mode */
	struct riva_regs current_state;

	struct riva_cursor *cursor;
        caddr_t ctrl_base;      /* Virtual control register base addr */
#ifdef CONFIG_MTRR
        struct { int vram; int vram_valid; } mtrr;
#endif
};

#endif /* __RIVAFB_H */
