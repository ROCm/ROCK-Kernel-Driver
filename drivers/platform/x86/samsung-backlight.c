/*
 * Samsung N130 and NC10 Laptop Backlight driver
 *
 * Copyright (C) 2009 Greg Kroah-Hartman (gregkh@suse.de)
 * Copyright (C) 2009 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/dmi.h>

#define MAX_BRIGHT	0xff
#define OFFSET		0xf4

static struct pci_dev *pci_device;
static struct backlight_device *backlight_device;
static int offset = OFFSET;
static u8 current_brightness;

static void read_brightness(void)
{
	if (!pci_device)
		return;
	pci_read_config_byte(pci_device, offset, &current_brightness);
}

static void set_brightness(void)
{
	if (!pci_device)
		return;
	pci_write_config_byte(pci_device, offset, current_brightness);
}

static int get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int update_status(struct backlight_device *bd)
{
	if (!pci_device)
		return -ENODEV;

	current_brightness = bd->props.brightness;
	set_brightness();
	return 0;
}

static struct backlight_ops backlight_ops = {
	.get_brightness	= get_brightness,
	.update_status	= update_status,
};

static int find_video_card(void)
{
	struct pci_dev *dev = NULL;

	while ((dev = pci_get_device(0x8086, 0x27ae, dev)) != NULL) {
		/*
		 * Found one, so let's save it off and break
		 * Note that the reference is still raised on
		 * the PCI device here.
		 */
		pci_device = dev;
		break;
	}

	if (!pci_device)
		return -ENODEV;

	/* create a backlight device to talk to this one */
	backlight_device = backlight_device_register("samsung",
						     &pci_device->dev,
						     NULL, &backlight_ops);
	if (IS_ERR(backlight_device))
		return PTR_ERR(backlight_device);
	read_brightness();
	backlight_device->props.max_brightness = MAX_BRIGHT;
	backlight_device->props.brightness = current_brightness;
	backlight_device->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(backlight_device);
	return 0;
}

static void remove_video_card(void)
{
	if (!pci_device)
		return;

	backlight_device_unregister(backlight_device);
	backlight_device = NULL;

	/* we are done with the PCI device, put it back */
	pci_dev_put(pci_device);
	pci_device = NULL;
}

static int dmi_check_cb(const struct dmi_system_id *id)
{
	printk(KERN_INFO KBUILD_MODNAME ": found laptop model '%s'\n",
		id->ident);
	return 0;
}

static struct dmi_system_id __initdata samsung_dmi_table[] = {
	{
		.ident = "N130",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "N130"),
			DMI_MATCH(DMI_BOARD_NAME, "N130"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "NC10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "NC10"),
			DMI_MATCH(DMI_BOARD_NAME, "NC10"),
		},
		.callback = dmi_check_cb,
	},
	{
		.ident = "NP-Q45",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "SQ45S70S"),
			DMI_MATCH(DMI_BOARD_NAME, "SQ45S70S"),
		},
		.callback = dmi_check_cb,
	},
	{ },
};

static int __init samsung_init(void)
{
	if (!dmi_check_system(samsung_dmi_table))
		return -ENODEV;

	return find_video_card();
}

static void __exit samsung_exit(void)
{
	remove_video_card();
}

module_init(samsung_init);
module_exit(samsung_exit);

MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@suse.de>");
MODULE_DESCRIPTION("Samsung Backlight driver");
MODULE_LICENSE("GPL");
module_param(offset, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(offset, "The offset into the PCI device for the brightness control");
MODULE_ALIAS("dmi:*:svnSAMSUNGELECTRONICSCO.,LTD.:pnN130:*:rnN130:*");
MODULE_ALIAS("dmi:*:svnSAMSUNGELECTRONICSCO.,LTD.:pnNC10:*:rnNC10:*");
MODULE_ALIAS("dmi:*:svnSAMSUNGELECTRONICSCO.,LTD.:pnSQ45S70S:*:rnSQ45S70S:*");
