/*
 * drivers/input/misc/gsc_ps2.c
 *
 * Copyright (c) 2002 Laurent Canet <canetl@esiee.fr>
 * Copyright (c) 2002 Thibaut Varene <varenet@esiee.fr>
 *
 * Pieces of code based on linux-2.4's hp_mouse.c & hp_keyb.c
 * 	Copyright (c) 1999 Alex deVries <adevries@thepuffingroup.com>
 *	Copyright (c) 1999-2000 Philipp Rumpf <prumpf@tux.org>
 *	Copyright (c) 2000 Xavier Debacker <debackex@esiee.fr>
 *	Copyright (c) 2000-2001 Thomas Marteau <marteaut@esiee.fr>
 *
 * HP PS/2 Keyboard, found in PA/RISC Workstations
 * very similar to AT keyboards, but without i8042
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * STATUS:
 * 11/09: lc: Only basic keyboard is supported, mouse still needs to be done.
 * 11/12: tv: switching iomapping; cleaning code; improving module stuff.
 * 11/13: lc & tv: leds aren't working. auto_repeat/meta are. Generaly good behavior.
 * 11/15: tv: 2AM: leds ARE working !
 * 11/16: tv: 3AM: escaped keycodes emulation *handled*, some keycodes are
 *	  still deliberately ignored (18), what are they used for ?
 * 11/21: lc: mouse is now working
 * 11/29: tv: first try for error handling in init sequence
 *
 * TODO:
 * Error handling in init sequence
 * SysRq handling
 * Pause key handling
 * Intellimouse & other rodents handling (at least send an error when
 * such a mouse is plugged : it will totally fault)
 * Mouse: set scaling / Dino testing
 * Bug chasing...
 *
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ptrace.h>       /* interrupt.h wants struct pt_regs defined */
#include <linux/interrupt.h>
#include <linux/sched.h>        /* for request_irq/free_irq */        
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/kd.h>
#include <linux/pci_ids.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/parisc-device.h>

/* Debugging stuff */
#undef KBD_DEBUG
#ifdef KBD_DEBUG
	#define DPRINTK(fmt,args...) printk(KERN_DEBUG __FILE__ ":" fmt, ##args)
#else 
	#define DPRINTK(x,...)
#endif


/* 
 * Driver constants
 */

/* PS/2 keyboard and mouse constants */
#define AUX_RECONNECT		0xAA	/* PS/2 Mouse end of test successful */
#define AUX_REPLY_ACK		0xFA
#define AUX_ENABLE_DEV		0xF4	/* Enables aux device */

/* Order of the mouse bytes coming to the host */
#define PACKET_X		1
#define PACKET_Y		2
#define PACKET_CTRL		0

#define GSC_MOUSE_OFFSET	0x0100	/* offset from keyboard to mouse port */
#define GSC_DINO_OFFSET		0x800	/* offset for DINO controller versus LASI one */

#define GSC_ID			0x00	/* ID and reset port offsets */
#define GSC_RESET		0x00
#define GSC_RCVDATA		0x04	/* receive and transmit port offsets */
#define GSC_XMTDATA		0x04
#define GSC_CONTROL		0x08	/* see: control register bits */
#define GSC_STATUS		0x0C	/* see: status register bits */

/* Control register bits */
#define GSC_CTRL_ENBL		0x01	/* enable interface */
#define GSC_CTRL_LPBXR		0x02	/* loopback operation */
#define GSC_CTRL_DIAG		0x20	/* directly control clock/data line */
#define GSC_CTRL_DATDIR		0x40	/* data line direct control */
#define GSC_CTRL_CLKDIR		0x80	/* clock line direct control */

/* Status register bits */
#define GSC_STAT_RBNE		0x01	/* Receive Buffer Not Empty */
#define GSC_STAT_TBNE		0x02	/* Transmit Buffer Not Empty */
#define GSC_STAT_TERR		0x04	/* Timeout Error */
#define GSC_STAT_PERR		0x08	/* Parity Error */
#define GSC_STAT_CMPINTR	0x10	/* Composite Interrupt */
#define GSC_STAT_DATSHD		0x40	/* Data Line Shadow */
#define GSC_STAT_CLKSHD		0x80	/* Clock Line Shadow */

/* Keycode map */
#define KBD_ESCAPE0		0xe0
#define KBD_ESCAPE1		0xe1
#define KBD_RELEASE		0xf0
#define KBD_ACK			0xfa
#define KBD_RESEND		0xfe
#define KBD_UNKNOWN		0

#define KBD_TBLSIZE		512

/* Mouse */
#define MOUSE_LEFTBTN		0x1
#define MOUSE_MIDBTN		0x4
#define MOUSE_RIGHTBTN		0x2
#define MOUSE_ALWAYS1		0x8
#define MOUSE_XSIGN		0x10
#define MOUSE_YSIGN		0x20
#define MOUSE_XOVFLOW		0x40
#define MOUSE_YOVFLOW		0x80

/* Remnant of pc_keyb.h */
#define KBD_CMD_SET_LEDS	0xED	/* Sets keyboard leds */
#define KBD_CMD_SET_RATE	0xF3	/* Sets typematic rate */
#define KBD_CMD_ENABLE		0xF4	/* Enables scanning */
#define KBD_CMD_DISABLE		0xF5
#define KBD_CMD_RESET		0xFF

static unsigned char hpkeyb_keycode[KBD_TBLSIZE] =
{
	/* 00 */  KBD_UNKNOWN,  KEY_F9,        KBD_UNKNOWN,   KEY_F5,        KEY_F3,        KEY_F1,       KEY_F2,        KEY_F12,
	/* 08 */  KBD_UNKNOWN,  KEY_F10,       KEY_F8,        KEY_F6,        KEY_F4,        KEY_TAB,      KEY_GRAVE,     KBD_UNKNOWN,
	/* 10 */  KBD_UNKNOWN,  KEY_LEFTALT,   KEY_LEFTSHIFT, KBD_UNKNOWN,   KEY_LEFTCTRL,  KEY_Q,        KEY_1,         KBD_UNKNOWN,
	/* 18 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KEY_Z,         KEY_S,         KEY_A,         KEY_W,        KEY_2,         KBD_UNKNOWN,
	/* 20 */  KBD_UNKNOWN,  KEY_C,         KEY_X,         KEY_D,         KEY_E,         KEY_4,        KEY_3,         KBD_UNKNOWN,
	/* 28 */  KBD_UNKNOWN,  KEY_SPACE,     KEY_V,         KEY_F,         KEY_T,         KEY_R,        KEY_5,         KBD_UNKNOWN,
	/* 30 */  KBD_UNKNOWN,  KEY_N,         KEY_B,         KEY_H,         KEY_G,         KEY_Y,        KEY_6,         KBD_UNKNOWN,
	/* 38 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KEY_M,         KEY_J,         KEY_U,         KEY_7,        KEY_8,         KBD_UNKNOWN,
	/* 40 */  KBD_UNKNOWN,  KEY_COMMA,     KEY_K,         KEY_I,         KEY_O,         KEY_0,        KEY_9,         KBD_UNKNOWN,
	/* 48 */  KBD_UNKNOWN,  KEY_DOT,       KEY_SLASH,     KEY_L,         KEY_SEMICOLON, KEY_P,        KEY_MINUS,     KBD_UNKNOWN,
	/* 50 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KEY_APOSTROPHE,KBD_UNKNOWN,   KEY_LEFTBRACE, KEY_EQUAL,    KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 58 */  KEY_CAPSLOCK, KEY_RIGHTSHIFT,KEY_ENTER,     KEY_RIGHTBRACE,KBD_UNKNOWN,   KEY_BACKSLASH,KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 60 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KEY_BACKSPACE, KBD_UNKNOWN,
	/* 68 */  KBD_UNKNOWN,  KEY_KP1,       KBD_UNKNOWN,   KEY_KP4,       KEY_KP7,       KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 70 */  KEY_KP0,      KEY_KPDOT,     KEY_KP2,       KEY_KP5,       KEY_KP6,       KEY_KP8,      KEY_ESC,       KEY_NUMLOCK,
	/* 78 */  KEY_F11,      KEY_KPPLUS,    KEY_KP3,       KEY_KPMINUS,   KEY_KPASTERISK,KEY_KP9,      KEY_SCROLLLOCK,KEY_103RD,
	/* 80 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KEY_F7,        KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 88 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 90 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 98 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* a0 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* a8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* b0 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* b8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* c0 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* c8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* d0 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* d8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* e0 */  KBD_ESCAPE0,  KBD_ESCAPE1,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* e8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* f0 */  KBD_RELEASE,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* f8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_ACK,       KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_RESEND,    KBD_UNKNOWN,
/* These are offset for escaped keycodes */
	/* 00 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 08 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 10 */  KBD_UNKNOWN,  KEY_RIGHTALT,  KBD_UNKNOWN,   KBD_UNKNOWN,   KEY_RIGHTCTRL, KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 18 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 20 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 28 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 30 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 38 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 40 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 48 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KEY_KPSLASH,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 50 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 58 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KEY_KPENTER,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 60 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 68 */  KBD_UNKNOWN,  KEY_END,       KBD_UNKNOWN,   KEY_LEFT,      KEY_HOME,      KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 70 */  KEY_INSERT,   KEY_DELETE,    KEY_DOWN,      KBD_UNKNOWN,   KEY_RIGHT,     KEY_UP,       KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 78 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KEY_PAGEDOWN,  KBD_UNKNOWN,   KEY_SYSRQ,     KEY_PAGEUP,   KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 80 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 88 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 90 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* 98 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* a0 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* a8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* b0 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* b8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* c0 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* c8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* d0 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* d8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* e0 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* e8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* f0 */  KBD_RELEASE,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,
	/* f8 */  KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,   KBD_UNKNOWN,  KBD_UNKNOWN,   KBD_UNKNOWN
};


/* Keyboard struct */
static struct {
	struct input_dev dev;
	char * addr;
	unsigned int irq;
	unsigned int scancode;
	unsigned int escaped;
	unsigned int released;
	unsigned int initialized;
}
hpkeyb = {
	.escaped = 0,
	.released = 0,
	.initialized = 0
};

/* Mouse struct */
static struct {
   	struct input_dev dev;
	char * addr;
	unsigned long irq;
	unsigned long initialized;
	int nbread;
	unsigned char bytes[3];
	unsigned long last;
}
hpmouse = {
	.initialized = 0,
	.nbread = 0
};

static spinlock_t gscps2_lock = SPIN_LOCK_UNLOCKED;


/*
 * Various HW level routines
 */

#define gscps2_readb_input(x)		readb(x+GSC_RCVDATA)
#define gscps2_readb_control(x)		readb(x+GSC_CONTROL)
#define gscps2_readb_status(x)		readb(x+GSC_STATUS)
#define gscps2_writeb_control(x, y)	writeb(x, y+GSC_CONTROL)

static inline void gscps2_writeb_output(u8 val, char * addr)
{
	int wait = 250;			/* Keyboard is expected to react within 250ms */

	while (gscps2_readb_status(addr) & GSC_STAT_TBNE) {
		if (!--wait)
			return;		/* This should not happen */
		mdelay(1);
	}
	writeb(val, addr+GSC_XMTDATA);
}

static inline unsigned char gscps2_wait_input(char * addr)
{
	int wait = 250;			/* Keyboard is expected to react within 250ms */

	while (!(gscps2_readb_status(addr) & GSC_STAT_RBNE)) {
		if (!--wait)
			return 0;	/* This should not happen */
		mdelay(1);
	}
	return gscps2_readb_input(addr);
}

static int gscps2_writeb_safe_output(u8 val)
{
	/* This function waits for keyboard's ACK */
	u8 scanread = KBD_UNKNOWN;
	int loop = 5;
	
	while (hpkeyb_keycode[scanread]!=KBD_ACK && --loop > 0) {	
		gscps2_writeb_output(val, hpkeyb.addr);
		mdelay(5);
		scanread = gscps2_wait_input(hpkeyb.addr);
	}
	
	if (loop <= 0)
		return -1;
	
	return 0;
}

/* Reset the PS2 port */
static void __init gscps2_reset(char * addr)
{
	/* reset the interface */
	writeb(0xff, addr+GSC_RESET);
	writeb(0x0 , addr+GSC_RESET);

	/* enable it */
	gscps2_writeb_control(gscps2_readb_control(addr) | GSC_CTRL_ENBL, addr);
}


/**
 * gscps2_kbd_docode() - PS2 Keyboard basic handler
 *
 * Receives a keyboard scancode, analyses it and sends it to the input layer.
 */

static void gscps2_kbd_docode(struct pt_regs *regs)
{
	int scancode = gscps2_readb_input(hpkeyb.addr);
	DPRINTK("rel=%d scancode=%d, esc=%d ", hpkeyb.released, scancode, hpkeyb.escaped);

	/* Handle previously escaped scancodes */
	if (hpkeyb.escaped == KBD_ESCAPE0)
		scancode |= 0x100;	/* jump to the next 256 chars of the table */
		
	switch (hpkeyb_keycode[scancode]) {
		case KBD_RELEASE:
			DPRINTK("release\n");
			hpkeyb.released = 1;
			break;
		case KBD_RESEND:
			DPRINTK("resend request\n");
			break;
		case KBD_ACK:
			DPRINTK("ACK\n");
			break;
		case KBD_ESCAPE0:
		case KBD_ESCAPE1:
			DPRINTK("escape code %d\n", hpkeyb_keycode[scancode]);
			hpkeyb.escaped = hpkeyb_keycode[scancode];
			break;
		case KBD_UNKNOWN:
			DPRINTK("received unknown scancode %d, escape %d.\n",
				scancode, hpkeyb.escaped);	/* This is a DPRINTK atm since we do not handle escaped scancodes cleanly */
			if (hpkeyb.escaped)
			hpkeyb.escaped = 0;
			if (hpkeyb.released)
				hpkeyb.released = 0;
			return;
		default:
			hpkeyb.scancode = scancode;
			DPRINTK("sent=%d, rel=%d\n",hpkeyb.scancode, hpkeyb.released);
			/*input_regs(regs);*/
			input_report_key(&hpkeyb.dev, hpkeyb_keycode[hpkeyb.scancode], !hpkeyb.released);
			input_sync(&hpkeyb.dev);
			if (hpkeyb.escaped)
				hpkeyb.escaped = 0;
			if (hpkeyb.released) 
				hpkeyb.released = 0;
			break;	
	}
}


/**
 * gscps2_mouse_docode() - PS2 Mouse basic handler
 *
 * Receives mouse codes, processes them by packets of three, and sends
 * correct events to the input layer.
 */

static void gscps2_mouse_docode(struct pt_regs *regs)
{
	int xrel, yrel;

	/* process BAT (end of basic tests) command */
	if ((hpmouse.nbread == 1) && (hpmouse.bytes[0] == AUX_RECONNECT))
		hpmouse.nbread--;

	/* stolen from psmouse.c */
	if (hpmouse.nbread && time_after(jiffies, hpmouse.last + HZ/2)) {
		printk(KERN_DEBUG "%s:%d : Lost mouse synchronization, throwing %d bytes away.\n", __FILE__, __LINE__,
				hpmouse.nbread);
		hpmouse.nbread = 0;
	}

	hpmouse.last = jiffies;
	hpmouse.bytes[hpmouse.nbread++] = gscps2_readb_input(hpmouse.addr);
	
	/* process packet */
	if (hpmouse.nbread == 3) {
		
		if (!(hpmouse.bytes[PACKET_CTRL] & MOUSE_ALWAYS1))
			DPRINTK("Mouse: error on packet always1 bit checking\n");
			/* XXX should exit now, bad data on the line! */
		
		if ((hpmouse.bytes[PACKET_CTRL] & (MOUSE_XOVFLOW | MOUSE_YOVFLOW)))
			DPRINTK("Mouse: position overflow\n");
		
		/*input_regs(regs);*/

		input_report_key(&hpmouse.dev, BTN_LEFT, hpmouse.bytes[PACKET_CTRL] & MOUSE_LEFTBTN);
		input_report_key(&hpmouse.dev, BTN_MIDDLE, hpmouse.bytes[PACKET_CTRL] & MOUSE_MIDBTN);
		input_report_key(&hpmouse.dev, BTN_RIGHT, hpmouse.bytes[PACKET_CTRL] & MOUSE_RIGHTBTN);
		
		xrel = hpmouse.bytes[PACKET_X];
		yrel = hpmouse.bytes[PACKET_Y];
		
		/* Data sent by mouse are 9-bit signed, the sign bit is in the control packet */
		if (xrel && (hpmouse.bytes[PACKET_CTRL] & MOUSE_XSIGN))
			xrel = xrel - 0x100;
		if (yrel && (hpmouse.bytes[PACKET_CTRL] & MOUSE_YSIGN))
			yrel = yrel - 0x100;
		
		input_report_rel(&hpmouse.dev, REL_X, xrel);
		input_report_rel(&hpmouse.dev, REL_Y, -yrel);	/* Y axis is received upside-down */
		
		input_sync(&hpmouse.dev);
		
		hpmouse.nbread = 0;
	}
}


/**
 * gscps2_interrupt() - Interruption service routine
 *
 * This processes the list of scancodes queued and sends appropriate
 * key value to the system.
 */

static irqreturn_t gscps2_interrupt(int irq, void *dev, struct pt_regs *reg)
{
	/* process mouse actions */
	while (gscps2_readb_status(hpmouse.addr) & GSC_STAT_RBNE)
		gscps2_mouse_docode(reg);
	
	/* process keyboard scancode */
	while (gscps2_readb_status(hpkeyb.addr) & GSC_STAT_RBNE)
		gscps2_kbd_docode(reg);

	return IRQ_HANDLED;
}


/**
 * gscps2_hpkeyb_event() - Event handler
 * @return: success/error report
 *
 * Currently only updates leds on keyboard
 */

int gscps2_hpkeyb_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	DPRINTK("Calling %s, type=%d, code=%d, value=%d\n",
			__FUNCTION__, type, code, value);

	if (!hpkeyb.initialized)
		return -1;

	if (type == EV_LED) {
		u8 leds[2];

		if (gscps2_writeb_safe_output(KBD_CMD_SET_LEDS)) {
			printk(KERN_ERR "gsckbd_leds: timeout\n");
			return -1;
		}
		DPRINTK("KBD_CMD_SET_LEDS\n");

		*leds = (test_bit(LED_SCROLLL, dev->led) ? LED_SCR : 0)
			| (test_bit(LED_NUML,    dev->led) ? LED_NUM : 0)
			| (test_bit(LED_CAPSL,   dev->led) ? LED_CAP : 0);
		DPRINTK("Sending leds=%x\n", *leds);
		
		if (gscps2_writeb_safe_output(*leds)) {
			printk(KERN_ERR "gsckbd_leds: timeout\n");
			return -1;
		}
		DPRINTK("leds sent\n");
		
		if (gscps2_writeb_safe_output(KBD_CMD_ENABLE)) {
			printk(KERN_ERR "gsckbd_leds: timeout\n");
			return -1;
		}
		DPRINTK("End\n");

		return 0;

	}
	return -1;
}


/**
 * gscps2_kbd_probe() - Probes keyboard device and init input_dev structure
 * @return: number of device initialized (1, 0 on error)
 */

static int __init gscps2_kbd_probe(void)
{
	int i, res = 0;
	unsigned long flags;

	if (hpkeyb.initialized) {
		printk(KERN_ERR "GSC PS/2 keyboard driver already registered\n");
		return 0;
	}
	
	spin_lock_irqsave(&gscps2_lock, flags);
 
	if (!gscps2_writeb_safe_output(KBD_CMD_SET_LEDS)	&&
	    !gscps2_writeb_safe_output(0)			&&
	    !gscps2_writeb_safe_output(KBD_CMD_ENABLE))
		res = 1;
 
	spin_unlock_irqrestore(&gscps2_lock, flags);

	if (!res)
		printk(KERN_ERR "Keyboard initialization sequence failled\n");
	
	init_input_dev(&hpkeyb.dev);
	
	for (i = 0; i < KBD_TBLSIZE; i++)
		if (hpkeyb_keycode[i] != KBD_UNKNOWN)
			set_bit(hpkeyb_keycode[i], hpkeyb.dev.keybit);
		
	hpkeyb.dev.evbit[0]	= BIT(EV_KEY) | BIT(EV_LED) | BIT(EV_REP);
	hpkeyb.dev.ledbit[0]	= BIT(LED_NUML) | BIT(LED_CAPSL) | BIT(LED_SCROLLL);
	hpkeyb.dev.keycode	= hpkeyb_keycode;
	hpkeyb.dev.keycodesize	= sizeof(unsigned char);
	hpkeyb.dev.keycodemax	= KBD_TBLSIZE;
	hpkeyb.dev.name		= "GSC Keyboard";
	hpkeyb.dev.phys		= "hpkbd/input0";

	hpkeyb.dev.event	= gscps2_hpkeyb_event;
	
	/* TODO These need some adjustement, are they really useful ? */
	hpkeyb.dev.id.bustype	= BUS_GSC;
	hpkeyb.dev.id.vendor	= PCI_VENDOR_ID_HP;
	hpkeyb.dev.id.product	= 0x0001;
	hpkeyb.dev.id.version	= 0x0010;
	hpkeyb.initialized	= 1;

	return 1;
}


/**
 * gscps2_mouse_probe() - Probes mouse device and init input_dev structure
 * @return: number of device initialized (1, 0 on error)
 *
 * Currently no check on initialization is performed
 */

static int __init gscps2_mouse_probe(void)
{
	if (hpmouse.initialized) {
		printk(KERN_ERR "GSC PS/2 Mouse driver already registered\n");
		return 0;
	}
	
	init_input_dev(&hpmouse.dev);
	
	hpmouse.dev.name	= "GSC Mouse";
	hpmouse.dev.phys	= "hpmouse/input0";
   	hpmouse.dev.evbit[0] 	= BIT(EV_KEY) | BIT(EV_REL);
	hpmouse.dev.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT);
	hpmouse.dev.relbit[0] 	= BIT(REL_X) | BIT(REL_Y);
	hpmouse.last 		= 0;

	gscps2_writeb_output(AUX_ENABLE_DEV, hpmouse.addr);
	/* Try it a second time, this will give status if the device is available */
	gscps2_writeb_output(AUX_ENABLE_DEV, hpmouse.addr);
	
	/* TODO These need some adjustement, are they really useful ? */
	hpmouse.dev.id.bustype	= BUS_GSC;
	hpmouse.dev.id.vendor	= 0x0001;
	hpmouse.dev.id.product	= 0x0001;
	hpmouse.dev.id.version	= 0x0010;
	hpmouse.initialized = 1;
	return 1;	/* XXX: we don't check if initialization failed */
}


/**
 * gscps2_probe() - Probes PS2 devices
 * @return: success/error report
 */

static int __init gscps2_probe(struct parisc_device *dev)
{
	u8 id;
	char *addr, *name;
	int ret = 0, device_found = 0;
	unsigned long hpa = dev->hpa;

	if (!dev->irq)
		goto fail_pitifully;
	
	/* Offset for DINO PS/2. Works with LASI even */
	if (dev->id.sversion == 0x96)
		hpa += GSC_DINO_OFFSET;

	addr = ioremap(hpa, 256);
	
	if (!hpmouse.initialized || !hpkeyb.initialized)
		gscps2_reset(addr);

	ret = -EINVAL;
	id = readb(addr+GSC_ID) & 0x0f;
	switch (id) {
		case 0:				/* keyboard */
			hpkeyb.addr = addr;
			name = "keyboard";
			device_found = gscps2_kbd_probe();
			break;
		case 1:				/* mouse */
			hpmouse.addr = addr;
			name = "mouse";
			device_found = gscps2_mouse_probe();
			break;
		default:
			printk(KERN_WARNING "%s: Unsupported PS/2 port (id=%d) ignored\n",
		    		__FUNCTION__, id);
			goto fail_miserably;
	}

	/* No valid device found */
	ret = -ENODEV;
	if (!device_found)
		goto fail_miserably;

	/* Here we claim only if we have a device attached */
	/* Allocate the irq and memory region for that device */
	ret = -EBUSY;
	if (request_irq(dev->irq, gscps2_interrupt, 0, name, NULL))
		goto fail_miserably;

	if (!request_mem_region(hpa, GSC_STATUS + 4, name))
		goto fail_request_mem;
	
	/* Finalize input struct and register it */
	switch (id) {
		case 0:				/* keyboard */
			hpkeyb.irq = dev->irq;
			input_register_device(&hpkeyb.dev);	
			break;
		case 1:				/* mouse */
			hpmouse.irq = dev->irq;
			input_register_device(&hpmouse.dev);
			break;
		default:
			break;
	}

	printk(KERN_INFO "input: PS/2 %s port at 0x%08lx (irq %d) found and attached\n",
			name, hpa, dev->irq);

	return 0;
	
fail_request_mem: free_irq(dev->irq, NULL);
fail_miserably: iounmap(addr);
fail_pitifully:	return ret;
}



static struct parisc_device_id gscps2_device_tbl[] = {
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00084 }, /* LASI PS/2 */
/*	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00096 },  DINO PS/2 (XXX Not yet tested) */
	{ 0, }	/* 0 terminated list */
};

static struct parisc_driver gscps2_driver = {
	.name		= "GSC PS2",
	.id_table	= gscps2_device_tbl,
	.probe		= gscps2_probe,
};

static int __init gscps2_init(void)
{
	if (register_parisc_driver(&gscps2_driver))
		return -EBUSY;
	return 0;
}

static void __exit gscps2_exit(void)
{
	/* TODO this is probably not very good and needs to be checked */
	if (hpkeyb.initialized) {
		free_irq(hpkeyb.irq, gscps2_interrupt);
		iounmap(hpkeyb.addr);
		hpkeyb.initialized = 0;
		input_unregister_device(&hpkeyb.dev);
	}
	if (hpmouse.initialized) {
		free_irq(hpmouse.irq, gscps2_interrupt);
		iounmap(hpmouse.addr);
		hpmouse.initialized = 0;
		input_unregister_device(&hpmouse.dev);
	}
	unregister_parisc_driver(&gscps2_driver);
}


MODULE_AUTHOR("Laurent Canet <canetl@esiee.fr>, Thibaut Varene <varenet@esiee.fr>");
MODULE_DESCRIPTION("GSC PS/2 keyboard/mouse driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(parisc, gscps2_device_tbl);


module_init(gscps2_init);
module_exit(gscps2_exit);
