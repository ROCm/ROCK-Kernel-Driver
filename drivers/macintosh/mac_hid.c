/*
 * drivers/macintosh/mac_hid.c
 *
 * HID support stuff for Macintosh computers.
 *
 * Copyright (C) 2000 Franz Sirl.
 *
 * This file will soon be removed in favor of an uinput userspace tool.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/input.h>
#include <linux/module.h>


static struct input_dev emumousebtn;
static void emumousebtn_input_register(void);
static int mouse_emulate_buttons = 0;
static int mouse_button2_keycode = KEY_RIGHTCTRL;	/* right control key */
static int mouse_button3_keycode = KEY_RIGHTALT;	/* right option key */
static int mouse_last_keycode = 0;

#if defined(CONFIG_SYSCTL)
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

int mac_hid_mouse_emulate_buttons(int caller, unsigned int keycode, int down)
{
	switch (caller) {
	case 1:
		/* Called from keyboard.c */
		if (mouse_emulate_buttons
		    && (keycode == mouse_button2_keycode
			|| keycode == mouse_button3_keycode)) {
			if (mouse_emulate_buttons == 1) {
			 	input_report_key(&emumousebtn,
						 keycode == mouse_button2_keycode ? BTN_MIDDLE : BTN_RIGHT,
						 down);
				input_sync(&emumousebtn);
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

int __init mac_hid_init(void)
{

	emumousebtn_input_register();

#if defined(CONFIG_SYSCTL)
	mac_hid_sysctl_header = register_sysctl_table(mac_hid_root_dir, 1);
#endif /* CONFIG_SYSCTL */

	return 0;
}

device_initcall(mac_hid_init);
