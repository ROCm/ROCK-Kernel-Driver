/*
 * drivers/macintosh/mac_hid.c
 *
 * HID support stuff for Macintosh computers.
 *
 * Copyright (C) 2000 Franz Sirl.
 *
 * Stuff inside CONFIG_MAC_EMUMOUSEBTN should really be moved to userspace.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/input.h>
#include <linux/module.h>


static unsigned char e0_keys[128] = {
	0, 0, 0, KEY_KPCOMMA, 0, KEY_INTL3, 0, 0,		/* 0x00-0x07 */
	0, 0, 0, 0, KEY_LANG1, KEY_LANG2, 0, 0,			/* 0x08-0x0f */
	0, 0, 0, 0, 0, 0, 0, 0,					/* 0x10-0x17 */
	0, 0, 0, 0, KEY_KPENTER, KEY_RIGHTCTRL, KEY_VOLUMEUP, 0,/* 0x18-0x1f */
	0, 0, 0, 0, 0, KEY_VOLUMEDOWN, KEY_MUTE, 0,		/* 0x20-0x27 */
	0, 0, 0, 0, 0, 0, 0, 0,					/* 0x28-0x2f */
	0, 0, 0, 0, 0, KEY_KPSLASH, 0, KEY_SYSRQ,		/* 0x30-0x37 */
	KEY_RIGHTALT, KEY_BRIGHTNESSUP, KEY_BRIGHTNESSDOWN, 
		KEY_EJECTCD, 0, 0, 0, 0,			/* 0x38-0x3f */
	0, 0, 0, 0, 0, 0, 0, KEY_HOME,				/* 0x40-0x47 */
	KEY_UP, KEY_PAGEUP, 0, KEY_LEFT, 0, KEY_RIGHT, 0, KEY_END, /* 0x48-0x4f */
	KEY_DOWN, KEY_PAGEDOWN, KEY_INSERT, KEY_DELETE, 0, 0, 0, 0, /* 0x50-0x57 */
	0, 0, 0, KEY_LEFTMETA, KEY_RIGHTMETA, KEY_COMPOSE, KEY_POWER, 0, /* 0x58-0x5f */
	0, 0, 0, 0, 0, 0, 0, 0,					/* 0x60-0x67 */
	0, 0, 0, 0, 0, 0, 0, KEY_MACRO,				/* 0x68-0x6f */
	0, 0, 0, 0, 0, 0, 0, 0,					/* 0x70-0x77 */
	0, 0, 0, 0, 0, 0, 0, 0					/* 0x78-0x7f */
};

#ifdef CONFIG_MAC_EMUMOUSEBTN
static struct input_dev emumousebtn;
static void emumousebtn_input_register(void);
static int mouse_emulate_buttons = 0;
static int mouse_button2_keycode = KEY_RIGHTCTRL;	/* right control key */
static int mouse_button3_keycode = KEY_RIGHTALT;	/* right option key */
static int mouse_last_keycode = 0;
#endif

#if defined(CONFIG_SYSCTL) && defined(CONFIG_MAC_EMUMOUSEBTN)
/* file(s) in /proc/sys/dev/mac_hid */
ctl_table mac_hid_files[] =
{
  {
    DEV_MAC_HID_MOUSE_BUTTON_EMULATION,
    "mouse_button_emulation", &mouse_emulate_buttons, sizeof(int),
    0644, NULL, &proc_dointvec
  },
  {
    DEV_MAC_HID_MOUSE_BUTTON2_KEYCODE,
    "mouse_button2_keycode", &mouse_button2_keycode, sizeof(int),
    0644, NULL, &proc_dointvec
  },
  {
    DEV_MAC_HID_MOUSE_BUTTON3_KEYCODE,
    "mouse_button3_keycode", &mouse_button3_keycode, sizeof(int),
    0644, NULL, &proc_dointvec
  },
  { 0 }
};

/* dir in /proc/sys/dev */
ctl_table mac_hid_dir[] =
{
  { DEV_MAC_HID, "mac_hid", NULL, 0, 0555, mac_hid_files },
  { 0 }
};

/* /proc/sys/dev itself, in case that is not there yet */
ctl_table mac_hid_root_dir[] =
{
  { CTL_DEV, "dev", NULL, 0, 0555, mac_hid_dir },
  { 0 }
};

static struct ctl_table_header *mac_hid_sysctl_header;

#endif /* endif CONFIG_SYSCTL */

int mac_hid_kbd_translate(unsigned char scancode, unsigned char *keycode,
			  char raw_mode)
{
	/* This code was copied from char/pc_keyb.c and will be
	 * superflous when the input layer is fully integrated.
	 * We don't need the high_keys handling, so this part
	 * has been removed.
	 */
	static int prev_scancode = 0;

	/* special prefix scancodes.. */
	if (scancode == 0xe0 || scancode == 0xe1) {
		prev_scancode = scancode;
		return 0;
	}

	scancode &= 0x7f;

	if (prev_scancode) {
		if (prev_scancode != 0xe0) {
			if (prev_scancode == 0xe1 && scancode == 0x1d) {
				prev_scancode = 0x100;
				return 0;
			} else if (prev_scancode == 0x100 && scancode == 0x45) {
				*keycode = KEY_PAUSE;
				prev_scancode = 0;
			} else {
				if (!raw_mode)
					printk(KERN_INFO "keyboard: unknown e1 escape sequence\n");
				prev_scancode = 0;
				return 0;
			}
		} else {
			prev_scancode = 0;
			if (scancode == 0x2a || scancode == 0x36)
				return 0;
		}
		if (e0_keys[scancode])
			*keycode = e0_keys[scancode];
		else {
			if (!raw_mode)
				printk(KERN_INFO "keyboard: unknown scancode e0 %02x\n",
				       scancode);
			return 0;
		}
	} else {
		switch (scancode) {
		case  91: scancode = KEY_LINEFEED; break;
		case  92: scancode = KEY_KPEQUAL; break;
		case 125: scancode = KEY_INTL1; break;
		}
		*keycode = scancode;
	}
	return 1;
}

char mac_hid_kbd_unexpected_up(unsigned char keycode)
{
	if (keycode == KEY_F13)
		return 0;
	else
		return 0x80;
}

#ifdef CONFIG_MAC_EMUMOUSEBTN
int mac_hid_mouse_emulate_buttons(int caller, unsigned int keycode, int down)
{
	switch (caller) {
	case 1:
		/* Called from keybdev.c */
		if (mouse_emulate_buttons
		    && (keycode == mouse_button2_keycode
			|| keycode == mouse_button3_keycode)) {
			if (mouse_emulate_buttons == 1) {
			 	input_report_key(&emumousebtn,
						 keycode == mouse_button2_keycode ? BTN_MIDDLE : BTN_RIGHT,
						 down);
				return 1;
			}
			mouse_last_keycode = down ? keycode : 0;
		}
		break;
	}
	return 0;
}

EXPORT_SYMBOL(mac_hid_mouse_emulate_buttons);

static void emumousebtn_input_register(void)
{
	emumousebtn.name = "Macintosh mouse button emulation";

	emumousebtn.evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	emumousebtn.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT);
	emumousebtn.relbit[0] = BIT(REL_X) | BIT(REL_Y);

	emumousebtn.id.bustype = BUS_ADB;
	emumousebtn.id.vendor = 0x0001;
	emumousebtn.id.product = 0x0001;
	emumousebtn.id.version = 0x0100;

	input_register_device(&emumousebtn);

	printk(KERN_INFO "input: Macintosh mouse button emulation\n");
}
#endif

int __init mac_hid_init(void)
{

#ifdef CONFIG_MAC_EMUMOUSEBTN
	emumousebtn_input_register();
#endif

#if defined(CONFIG_SYSCTL) && defined(CONFIG_MAC_EMUMOUSEBTN)
	mac_hid_sysctl_header = register_sysctl_table(mac_hid_root_dir, 1);
#endif /* CONFIG_SYSCTL */

	return 0;
}

device_initcall(mac_hid_init);
