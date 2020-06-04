/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_BACKLIGHT_H
#define AMDKCL_BACKLIGHT_H

#include <linux/backlight.h>
#ifndef HAVE_BACKLIGHT_DEVICE_SET_BRIGHTNESS
int backlight_device_set_brightness(struct backlight_device *bd,
				    unsigned long brightness);
#endif /* HAVE_BACKLIGHT_DEVICE_SET_BRIGHTNESS */
#endif
