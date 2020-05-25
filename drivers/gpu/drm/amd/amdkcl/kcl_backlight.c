/* SPDX-License-Identifier: MIT */
#include <kcl/kcl_backlight.h>
#include "kcl_common.h"

#ifndef HAVE_BACKLIGHT_DEVICE_SET_BRIGHTNESS
amdkcl_dummy_symbol(backlight_device_set_brightness, int, return 0,
	     struct backlight_device *bd, unsigned long brightness)
#endif

