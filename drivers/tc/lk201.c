/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kbd_ll.h>
#include <asm/wbflush.h>
#include <asm/dec/tc.h>
#include <asm/dec/machtype.h>

#include "zs.h"
#include "lk201.h"

/* Simple translation table for the SysRq keys */

#ifdef CONFIG_MAGIC_SYSRQ
/*
 * Actually no translation at all, at least until we figure out
 * how to define SysRq for LK201 and friends. --macro
 */
unsigned char lk201_sysrq_xlate[128];
unsigned char *kbd_sysrq_xlate = lk201_sysrq_xlate;
#endif

#define KEYB_LINE	3

static int __init lk201_init(struct dec_serial *);
static void __init lk201_info(struct dec_serial *);
static void lk201_kbd_rx_char(unsigned char, unsigned char);

struct zs_hook lk201_kbdhook = {
	.init_channel   = lk201_init,
	.init_info      = lk201_info,
	.cflags         = B4800 | CS8 | CSTOPB | CLOCAL
};

/*
 * This is used during keyboard initialisation
 */
static unsigned char lk201_reset_string[] = {
	LK_CMD_LEDS_ON, LK_PARAM_LED_MASK(0xf),	/* show we are resetting */
	LK_CMD_SET_DEFAULTS,
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 1),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 2),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 3),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 4),
	LK_CMD_MODE(LK_MODE_DOWN_UP, 5),
	LK_CMD_MODE(LK_MODE_DOWN_UP, 6),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 7),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 8),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 9),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 10),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 11),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 12),
	LK_CMD_MODE(LK_MODE_DOWN, 13),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 14),
	LK_CMD_ENB_RPT,
	LK_CMD_DIS_KEYCLK,
	LK_CMD_RESUME,
	LK_CMD_ENB_BELL, LK_PARAM_VOLUME(4),
	LK_CMD_LEDS_OFF, LK_PARAM_LED_MASK(0xf)
};

static int __init lk201_reset(struct dec_serial *info)
{
	int i;

	for (i = 0; i < sizeof(lk201_reset_string); i++)
		if (info->hook->poll_tx_char(info, lk201_reset_string[i])) {
			printk("%s transmit timeout\n", __FUNCTION__);
			return -EIO;
		}
	return 0;
}

void kbd_leds(unsigned char leds)
{
	return;
}

int kbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	return -EINVAL;
}

int kbd_getkeycode(unsigned int scancode)
{
	return -EINVAL;
}

int kbd_translate(unsigned char scancode, unsigned char *keycode,
		  char raw_mode)
{
	*keycode = scancode;
	return 1;
}

char kbd_unexpected_up(unsigned char keycode)
{
	return 0x80;
}

static void lk201_kbd_rx_char(unsigned char ch, unsigned char stat)
{
	static int shift_state = 0;
	static int prev_scancode;
	unsigned char c = scancodeRemap[ch];

	if (!stat || stat == 4) {
		switch (ch) {
		case LK_KEY_ACK:
			break;
		case LK_KEY_LOCK:
			shift_state ^= LK_LOCK;
			handle_scancode(c, shift_state && LK_LOCK ? 1 : 0);
			break;
		case LK_KEY_SHIFT:
			shift_state ^= LK_SHIFT;
			handle_scancode(c, shift_state && LK_SHIFT ? 1 : 0);
			break;
		case LK_KEY_CTRL:
			shift_state ^= LK_CTRL;
			handle_scancode(c, shift_state && LK_CTRL ? 1 : 0);
			break;
		case LK_KEY_COMP:
			shift_state ^= LK_COMP;
			handle_scancode(c, shift_state && LK_COMP ? 1 : 0);
			break;
		case LK_KEY_RELEASE:
			if (shift_state & LK_SHIFT)
				handle_scancode(scancodeRemap[LK_KEY_SHIFT], 0);
			if (shift_state & LK_CTRL)
				handle_scancode(scancodeRemap[LK_KEY_CTRL], 0);
			if (shift_state & LK_COMP)
				handle_scancode(scancodeRemap[LK_KEY_COMP], 0);
			if (shift_state & LK_LOCK)
				handle_scancode(scancodeRemap[LK_KEY_LOCK], 0);
			shift_state = 0;
			break;
		case LK_KEY_REPEAT:
			handle_scancode(prev_scancode, 1);
			break;
		default:
			prev_scancode = c;
			handle_scancode(c, 1);
			break;
		}
	} else
		printk("Error reading LKx01 keyboard: 0x%02x\n", stat);
}

static void __init lk201_info(struct dec_serial *info)
{
}

static int __init lk201_init(struct dec_serial *info)
{
	unsigned int ch, id = 0;
	int result;

	printk("DECstation LK keyboard driver v0.04... ");

	result = lk201_reset(info);
	if (result)
		return result;
	mdelay(10);

	/*
	 * Detect whether there is an LK201 or an LK401
	 * The LK401 has ALT keys...
	 */
	info->hook->poll_tx_char(info, LK_CMD_REQ_ID);
	while ((ch = info->hook->poll_rx_char(info)) > 0)
		id = ch;

	switch (id) {
	case 1:
		printk("LK201 detected\n");
		break;
	case 2:
		printk("LK401 detected\n");
		break;
	default:
		printk("unknown keyboard, ID %d,\n", id);
		printk("... please report to <linux-mips@oss.sgi.com>\n");
	}

	/*
	 * now we're ready
	 */
	info->hook->rx_char = lk201_kbd_rx_char;

	return 0;
}

void __init kbd_init_hw(void)
{
	extern int register_zs_hook(unsigned int, struct zs_hook *);
	extern int unregister_zs_hook(unsigned int);

	if (TURBOCHANNEL) {
		if (mips_machtype != MACH_DS5000_XX) {
			/*
			 * This is not a MAXINE, so:
			 *
			 * kbd_init_hw() is being called before
			 * rs_init() so just register the kbd hook
			 * and let zs_init do the rest :-)
			 */
			if (mips_machtype == MACH_DS5000_200)
				printk("LK201 Support for DS5000/200 not yet ready ...\n");
			else
				if(!register_zs_hook(KEYB_LINE, &lk201_kbdhook))
					unregister_zs_hook(KEYB_LINE);
		}
	} else {
		/*
		 * TODO: modify dz.c to allow similar hooks
		 * for LK201 handling on DS2100, DS3100, and DS5000/200
		 */
		printk("LK201 Support for DS3100 not yet ready ...\n");
	}
}




