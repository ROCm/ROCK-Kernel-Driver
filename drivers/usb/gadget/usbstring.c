/*
 * Copyright (C) 2003 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/device.h>

#include <linux/usb_ch9.h>
#include <linux/usb_gadget.h>


/**
 * usb_gadget_get_string - fill out a string descriptor 
 * @table: of c strings using iso latin/1 characters
 * @id: string id, from low byte of wValue in get string descriptor
 * @buf: at least 256 bytes
 *
 * Finds the iso latin/1 string matching the ID, and converts it into a
 * string descriptor in utf16-le.
 * Returns length of descriptor (always even) or negative errno
 *
 * If your driver needs stings in multiple languages, you'll need to
 * to use some alternate solution for languages where the ISO 8859/1
 * (latin/1) character set can't be used.  For example, they can't be
 * used with Chinese (Big5, GB2312, etc), Japanese, Korean, or many other
 * languages.  You'd likely "switch (wIndex) { ... }"  in your ep0
 * string descriptor logic, using this routine in cases where "western
 * european" characters suffice for the strings being returned.
 */
int
usb_gadget_get_string (struct usb_gadget_strings *table, int id, u8 *buf)
{
	struct usb_string	*s;
	int			len;

	/* descriptor 0 has the language id */
	if (id == 0) {
		buf [0] = 4;
		buf [1] = USB_DT_STRING;
		buf [2] = (u8) table->language;
		buf [3] = (u8) (table->language >> 8);
		return 4;
	}
	for (s = table->strings; s && s->s; s++)
		if (s->id == id)
			break;

	/* unrecognized: stall. */
	if (!s || !s->s)
		return -EINVAL;

	/* string descriptors have length, tag, then UTF16-LE text */
	len = min ((size_t) 126, strlen (s->s));
	buf [0] = (len + 1) * 2;
	buf [1] = USB_DT_STRING;
	memset (buf + 2, 0, 2 * len);	/* zero all the high bytes */
	while (len) {
		buf [2 * len] = s->s [len - 1];
		len--;
	}
	return buf [0];
}

