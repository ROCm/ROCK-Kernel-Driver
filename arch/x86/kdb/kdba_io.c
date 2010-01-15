/*
 * Kernel Debugger Architecture Dependent Console I/O handler
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/ctype.h>
#include <linux/keyboard.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>

#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include "pc_keyb.h"

#ifdef	CONFIG_VT_CONSOLE
#undef	KDB_BLINK_LED
#else
#undef	KDB_BLINK_LED
#endif

#ifdef	CONFIG_KDB_USB

struct kdb_usb_kbd_info kdb_usb_kbds[KDB_USB_NUM_KEYBOARDS];
EXPORT_SYMBOL(kdb_usb_kbds);

extern int kdb_no_usb;

static unsigned char kdb_usb_keycode[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43, 84, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117, 85, 89, 90, 91, 92, 93, 94, 95,
	120,121,122,123,134,138,130,132,128,129,131,137,133,135,136,113,
	115,114,  0,  0,  0,124,  0,181,182,183,184,185,186,187,188,189,
	190,191,192,193,194,195,196,197,198,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
	150,158,159,128,136,177,178,176,142,152,173,140
};

/*
 * kdb_usb_keyboard_attach()
 * Attach a USB keyboard to kdb.
 */
int
kdb_usb_keyboard_attach(struct urb *urb, unsigned char *buffer,
			void *poll_func, void *compl_func,
			kdb_hc_keyboard_attach_t kdb_hc_keyboard_attach,
			kdb_hc_keyboard_detach_t kdb_hc_keyboard_detach,
			unsigned int bufsize,
			struct urb *hid_urb)
{
        int     i;
        int     rc = -1;

        if (kdb_no_usb)
                return 0;

        /*
         * Search through the array of KDB USB keyboards (kdb_usb_kbds)
         * looking for a free index. If found, assign the keyboard to
         * the array index.
         */

        for (i = 0; i < KDB_USB_NUM_KEYBOARDS; i++) {
                if (kdb_usb_kbds[i].urb) /* index is already assigned */
                        continue;

                /* found a free array index */
                kdb_usb_kbds[i].urb = urb;
                kdb_usb_kbds[i].buffer = buffer;
                kdb_usb_kbds[i].poll_func = poll_func;

		kdb_usb_kbds[i].kdb_hc_urb_complete = compl_func;
		kdb_usb_kbds[i].kdb_hc_keyboard_attach = kdb_hc_keyboard_attach;
		kdb_usb_kbds[i].kdb_hc_keyboard_detach = kdb_hc_keyboard_detach;

		/* USB Host Controller specific Keyboadr attach callback.
		 * Currently only UHCI has this callback.
		 */
		if (kdb_usb_kbds[i].kdb_hc_keyboard_attach)
			kdb_usb_kbds[i].kdb_hc_keyboard_attach(i, bufsize);

                rc = 0; /* success */

                break;
        }

        return rc;
}
EXPORT_SYMBOL_GPL (kdb_usb_keyboard_attach);

/*
 * kdb_usb_keyboard_detach()
 * Detach a USB keyboard from kdb.
 */
int
kdb_usb_keyboard_detach(struct urb *urb)
{
        int     i;
        int     rc = -1;

        if (kdb_no_usb)
                return 0;

        /*
         * Search through the array of KDB USB keyboards (kdb_usb_kbds)
         * looking for the index with the matching URB. If found,
         * clear the array index.
         */

        for (i = 0; i < KDB_USB_NUM_KEYBOARDS; i++) {
		if ((kdb_usb_kbds[i].urb != urb) &&
		    (kdb_usb_kbds[i].hid_urb != urb))
                        continue;

                /* found it, clear the index */

		/* USB Host Controller specific Keyboard detach callback.
		 * Currently only UHCI has this callback.
		 */
		if (kdb_usb_kbds[i].kdb_hc_keyboard_detach)
			kdb_usb_kbds[i].kdb_hc_keyboard_detach(urb, i);

                kdb_usb_kbds[i].urb = NULL;
                kdb_usb_kbds[i].buffer = NULL;
                kdb_usb_kbds[i].poll_func = NULL;
                kdb_usb_kbds[i].caps_lock = 0;
		kdb_usb_kbds[i].hid_urb = NULL;

                rc = 0; /* success */

                break;
        }

        return rc;
}
EXPORT_SYMBOL_GPL (kdb_usb_keyboard_detach);

/*
 * get_usb_char
 * This function drives the USB attached keyboards.
 * Fetch the USB scancode and decode it.
 */
static int
get_usb_char(void)
{
        int     i;
        unsigned char keycode, spec;
        extern u_short plain_map[], shift_map[], ctrl_map[];
        int     ret = 1;
	int     ret_key = -1, j, max;

        if (kdb_no_usb)
                return -1;

        /*
         * Loop through all the USB keyboard(s) and return
         * the first character obtained from them.
         */

        for (i = 0; i < KDB_USB_NUM_KEYBOARDS; i++) {
                /* skip uninitialized keyboard array entries */
                if (!kdb_usb_kbds[i].urb || !kdb_usb_kbds[i].buffer ||
                    !kdb_usb_kbds[i].poll_func)
                        continue;

                /* Transfer char */
                ret = (*kdb_usb_kbds[i].poll_func)(kdb_usb_kbds[i].urb);
                if (ret == -EBUSY && kdb_usb_kbds[i].poll_ret != -EBUSY)
                        kdb_printf("NOTICE: USB HD driver BUSY. "
                            "USB keyboard has been disabled.\n");

                kdb_usb_kbds[i].poll_ret = ret;

                if (ret < 0) /* error or no characters, try the next kbd */
                        continue;

		/* If 2 keys was pressed simultaneously,
		 * both keycodes will be in buffer.
		 * Last pressed key will be last non
		 * zero byte.
		 */
		for (j=0; j<4; j++){
			if (!kdb_usb_kbds[i].buffer[2+j])
				break;
		}
		/* Last pressed key */
		max = j + 1;

                spec = kdb_usb_kbds[i].buffer[0];
                keycode = kdb_usb_kbds[i].buffer[2];
                kdb_usb_kbds[i].buffer[0] = (char)0;
                kdb_usb_kbds[i].buffer[2] = (char)0;

		ret_key = -1;

                /* A normal key is pressed, decode it */
                if(keycode)
                        keycode = kdb_usb_keycode[keycode];

                /* 2 Keys pressed at one time ? */
                if (spec && keycode) {
                        switch(spec)
                        {
                        case 0x2:
                        case 0x20: /* Shift */
                                ret_key = shift_map[keycode];
				break;
                        case 0x1:
                        case 0x10: /* Ctrl */
                                ret_key = ctrl_map[keycode];
				break;
                        case 0x4:
                        case 0x40: /* Alt */
                                break;
                        }
                } else if (keycode) { /* If only one key pressed */
                        switch(keycode)
                        {
                        case 0x1C: /* Enter */
                                ret_key = 13;
				break;

                        case 0x3A: /* Capslock */
                                kdb_usb_kbds[i].caps_lock = !(kdb_usb_kbds[i].caps_lock);
                                break;
                        case 0x0E: /* Backspace */
                                ret_key = 8;
				break;
                        case 0x0F: /* TAB */
                                ret_key = 9;
				break;
                        case 0x77: /* Pause */
                                break ;
                        default:
                                if(!kdb_usb_kbds[i].caps_lock) {
                                        ret_key = plain_map[keycode];
                                }
                                else {
                                        ret_key = shift_map[keycode];
                                }
                        }
                }

		if (ret_key != 1) {
			/* Key was pressed, return keycode */

			/* Clear buffer before urb resending */
			if (kdb_usb_kbds[i].buffer)
				for(j=0; j<8; j++)
					kdb_usb_kbds[i].buffer[j] = (char)0;

			/* USB Host Controller specific Urb complete callback.
			 * Currently only UHCI has this callback.
			 */
			if (kdb_usb_kbds[i].kdb_hc_urb_complete)
				(*kdb_usb_kbds[i].kdb_hc_urb_complete)((struct urb *)kdb_usb_kbds[i].urb);

			return ret_key;
		}
        }



        /* no chars were returned from any of the USB keyboards */

        return -1;
}
#endif	/* CONFIG_KDB_USB */

/*
 * This module contains code to read characters from the keyboard or a serial
 * port.
 *
 * It is used by the kernel debugger, and is polled, not interrupt driven.
 *
 */

#ifdef	KDB_BLINK_LED
/*
 * send:  Send a byte to the keyboard controller.  Used primarily to
 * 	  alter LED settings.
 */

static void
kdb_kbdsend(unsigned char byte)
{
	int timeout;
	for (timeout = 200 * 1000; timeout && (inb(KBD_STATUS_REG) & KBD_STAT_IBF); timeout--);
	outb(byte, KBD_DATA_REG);
	udelay(40);
	for (timeout = 200 * 1000; timeout && (~inb(KBD_STATUS_REG) & KBD_STAT_OBF); timeout--);
	inb(KBD_DATA_REG);
	udelay(40);
}

static void
kdb_toggleled(int led)
{
	static int leds;

	leds ^= led;

	kdb_kbdsend(KBD_CMD_SET_LEDS);
	kdb_kbdsend((unsigned char)leds);
}
#endif	/* KDB_BLINK_LED */

#if defined(CONFIG_SERIAL_8250_CONSOLE) || defined(CONFIG_SERIAL_CORE_CONSOLE)
#define CONFIG_SERIAL_CONSOLE
#endif

#if defined(CONFIG_SERIAL_CONSOLE)

struct kdb_serial kdb_serial;

static unsigned int
serial_inp(struct kdb_serial *kdb_serial, unsigned long offset)
{
	offset <<= kdb_serial->ioreg_shift;

	switch (kdb_serial->io_type) {
	case SERIAL_IO_MEM:
		return readb((void __iomem *)(kdb_serial->iobase + offset));
		break;
	default:
		return inb(kdb_serial->iobase + offset);
		break;
	}
}

/* Check if there is a byte ready at the serial port */
static int get_serial_char(void)
{
	unsigned char ch;

	if (kdb_serial.iobase == 0)
		return -1;

	if (serial_inp(&kdb_serial, UART_LSR) & UART_LSR_DR) {
		ch = serial_inp(&kdb_serial, UART_RX);
		if (ch == 0x7f)
			ch = 8;
		return ch;
	}
	return -1;
}
#endif /* CONFIG_SERIAL_CONSOLE */

#ifdef	CONFIG_VT_CONSOLE

static int kbd_exists;

/*
 * Check if the keyboard controller has a keypress for us.
 * Some parts (Enter Release, LED change) are still blocking polled here,
 * but hopefully they are all short.
 */
static int get_kbd_char(void)
{
	int scancode, scanstatus;
	static int shift_lock;	/* CAPS LOCK state (0-off, 1-on) */
	static int shift_key;	/* Shift next keypress */
	static int ctrl_key;
	u_short keychar;
	extern u_short plain_map[], shift_map[], ctrl_map[];

	if (KDB_FLAG(NO_I8042) || KDB_FLAG(NO_VT_CONSOLE) ||
	    (inb(KBD_STATUS_REG) == 0xff && inb(KBD_DATA_REG) == 0xff)) {
		kbd_exists = 0;
		return -1;
	}
	kbd_exists = 1;

	if ((inb(KBD_STATUS_REG) & KBD_STAT_OBF) == 0)
		return -1;

	/*
	 * Fetch the scancode
	 */
	scancode = inb(KBD_DATA_REG);
	scanstatus = inb(KBD_STATUS_REG);

	/*
	 * Ignore mouse events.
	 */
	if (scanstatus & KBD_STAT_MOUSE_OBF)
		return -1;

	/*
	 * Ignore release, trigger on make
	 * (except for shift keys, where we want to
	 *  keep the shift state so long as the key is
	 *  held down).
	 */

	if (((scancode&0x7f) == 0x2a) || ((scancode&0x7f) == 0x36)) {
		/*
		 * Next key may use shift table
		 */
		if ((scancode & 0x80) == 0) {
			shift_key=1;
		} else {
			shift_key=0;
		}
		return -1;
	}

	if ((scancode&0x7f) == 0x1d) {
		/*
		 * Left ctrl key
		 */
		if ((scancode & 0x80) == 0) {
			ctrl_key = 1;
		} else {
			ctrl_key = 0;
		}
		return -1;
	}

	if ((scancode & 0x80) != 0)
		return -1;

	scancode &= 0x7f;

	/*
	 * Translate scancode
	 */

	if (scancode == 0x3a) {
		/*
		 * Toggle caps lock
		 */
		shift_lock ^= 1;

#ifdef	KDB_BLINK_LED
		kdb_toggleled(0x4);
#endif
		return -1;
	}

	if (scancode == 0x0e) {
		/*
		 * Backspace
		 */
		return 8;
	}

	/* Special Key */
	switch (scancode) {
	case 0xF: /* Tab */
		return 9;
	case 0x53: /* Del */
		return 4;
	case 0x47: /* Home */
		return 1;
	case 0x4F: /* End */
		return 5;
	case 0x4B: /* Left */
		return 2;
	case 0x48: /* Up */
		return 16;
	case 0x50: /* Down */
		return 14;
	case 0x4D: /* Right */
		return 6;
	}

	if (scancode == 0xe0) {
		return -1;
	}

	/*
	 * For Japanese 86/106 keyboards
	 * 	See comment in drivers/char/pc_keyb.c.
	 * 	- Masahiro Adegawa
	 */
	if (scancode == 0x73) {
		scancode = 0x59;
	} else if (scancode == 0x7d) {
		scancode = 0x7c;
	}

	if (!shift_lock && !shift_key && !ctrl_key) {
		keychar = plain_map[scancode];
	} else if (shift_lock || shift_key) {
		keychar = shift_map[scancode];
	} else if (ctrl_key) {
		keychar = ctrl_map[scancode];
	} else {
		keychar = 0x0020;
		kdb_printf("Unknown state/scancode (%d)\n", scancode);
	}
	keychar &= 0x0fff;
	if (keychar == '\t')
		keychar = ' ';
	switch (KTYP(keychar)) {
	case KT_LETTER:
	case KT_LATIN:
		if (isprint(keychar))
			break;		/* printable characters */
		/* drop through */
	case KT_SPEC:
		if (keychar == K_ENTER)
			break;
		/* drop through */
	default:
		return(-1);	/* ignore unprintables */
	}

	if ((scancode & 0x7f) == 0x1c) {
		/*
		 * enter key.  All done.  Absorb the release scancode.
		 */
		while ((inb(KBD_STATUS_REG) & KBD_STAT_OBF) == 0)
			;

		/*
		 * Fetch the scancode
		 */
		scancode = inb(KBD_DATA_REG);
		scanstatus = inb(KBD_STATUS_REG);

		while (scanstatus & KBD_STAT_MOUSE_OBF) {
			scancode = inb(KBD_DATA_REG);
			scanstatus = inb(KBD_STATUS_REG);
		}

		if (scancode != 0x9c) {
			/*
			 * Wasn't an enter-release,  why not?
			 */
			kdb_printf("kdb: expected enter got 0x%x status 0x%x\n",
			       scancode, scanstatus);
		}

		kdb_printf("\n");
		return 13;
	}

	return keychar & 0xff;
}
#endif	/* CONFIG_VT_CONSOLE */

#ifdef	KDB_BLINK_LED

/* Leave numlock alone, setting it messes up laptop keyboards with the keypad
 * mapped over normal keys.
 */
static int kdba_blink_mask = 0x1 | 0x4;

#define BOGOMIPS (boot_cpu_data.loops_per_jiffy/(500000/HZ))
static int blink_led(void)
{
	static long delay;

	if (kbd_exists == 0)
		return -1;

	if (--delay < 0) {
		if (BOGOMIPS == 0)	/* early kdb */
			delay = 150000000/1000;     /* arbitrary bogomips */
		else
			delay = 150000000/BOGOMIPS; /* Roughly 1 second when polling */
		kdb_toggleled(kdba_blink_mask);
	}
	return -1;
}
#endif

get_char_func poll_funcs[] = {
#if defined(CONFIG_VT_CONSOLE)
	get_kbd_char,
#endif
#if defined(CONFIG_SERIAL_CONSOLE)
	get_serial_char,
#endif
#ifdef	KDB_BLINK_LED
	blink_led,
#endif
#ifdef	CONFIG_KDB_USB
	get_usb_char,
#endif
	NULL
};

/*
 * On some Compaq Deskpro's, there is a keyboard freeze many times after
 * exiting from the kdb. As kdb's keyboard handler is not interrupt-driven and
 * uses a polled interface, it makes more sense to disable motherboard keyboard
 * controller's OBF interrupts during kdb's polling.In case, of interrupts
 * remaining enabled during kdb's polling, it may cause un-necessary
 * interrupts being signalled during keypresses, which are also sometimes seen
 * as spurious interrupts after exiting from kdb. This hack to disable OBF
 * interrupts before entry to kdb and re-enabling them at kdb exit point also
 * solves the keyboard freeze issue. These functions are called from
 * kdb_local(), hence these are arch. specific setup and cleanup functions
 * executing only on the local processor - ashishk@sco.com
 */

void kdba_local_arch_setup(void)
{
#ifdef	CONFIG_VT_CONSOLE
	int timeout;
	unsigned char c;

	while (kbd_read_status() & KBD_STAT_IBF);
	kbd_write_command(KBD_CCMD_READ_MODE);
	mdelay(1);
	while (kbd_read_status() & KBD_STAT_IBF);
	for (timeout = 200 * 1000; timeout &&
			(!(kbd_read_status() & KBD_STAT_OBF)); timeout--);
	c = kbd_read_input();
	c &= ~KBD_MODE_KBD_INT;
	while (kbd_read_status() & KBD_STAT_IBF);
	kbd_write_command(KBD_CCMD_WRITE_MODE);
	mdelay(1);
	while (kbd_read_status() & KBD_STAT_IBF);
	kbd_write_output(c);
	mdelay(1);
	while (kbd_read_status() & KBD_STAT_IBF);
	mdelay(1);
#endif	/* CONFIG_VT_CONSOLE */
}

void kdba_local_arch_cleanup(void)
{
#ifdef	CONFIG_VT_CONSOLE
	int timeout;
	unsigned char c;

	while (kbd_read_status() & KBD_STAT_IBF);
	kbd_write_command(KBD_CCMD_READ_MODE);
	mdelay(1);
	while (kbd_read_status() & KBD_STAT_IBF);
	for (timeout = 200 * 1000; timeout &&
			(!(kbd_read_status() & KBD_STAT_OBF)); timeout--);
	c = kbd_read_input();
	c |= KBD_MODE_KBD_INT;
	while (kbd_read_status() & KBD_STAT_IBF);
	kbd_write_command(KBD_CCMD_WRITE_MODE);
	mdelay(1);
	while (kbd_read_status() & KBD_STAT_IBF);
	kbd_write_output(c);
	mdelay(1);
	while (kbd_read_status() & KBD_STAT_IBF);
	mdelay(1);
#endif	/* CONFIG_VT_CONSOLE */
}
