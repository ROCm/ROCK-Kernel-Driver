/******************************************************************************
 * blkback/cdrom.c
 *
 * Routines for managing cdrom watch and media-present attribute of a
 * cdrom type virtual block device (VBD).
 *
 * Copyright (c) 2003-2005, Keir Fraser & Steve Hand
 * Copyright (c) 2007       Pat Campbell
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "common.h"

#undef DPRINTK
#define DPRINTK(_f, _a...)			\
	printk("(%s() file=%s, line=%d) " _f "\n",	\
		 __PRETTY_FUNCTION__, __FILE__ , __LINE__ , ##_a )


#define MEDIA_PRESENT "media-present"

static void cdrom_media_changed(struct xenbus_watch *, const char **, unsigned int);

/**
 * Writes media-present=1 attribute for the given vbd device if not
 * already there
 */
static int cdrom_xenstore_write_media_present(struct backend_info *be)
{
	struct xenbus_device *dev = be->dev;
	struct xenbus_transaction xbt;
	int err;
	int media_present;

	err = xenbus_scanf(XBT_NIL, dev->nodename, MEDIA_PRESENT, "%d",
			   &media_present);
	if (0 < err) {
		DPRINTK("already written err%d", err);
		return(0);
	}
	media_present = 1;

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		return(-1);
	}

	err = xenbus_printf(xbt, dev->nodename, MEDIA_PRESENT, "%d", media_present );
	if (err) {
		xenbus_dev_fatal(dev, err, "writing %s/%s",
			 dev->nodename, MEDIA_PRESENT);
		goto abort;
	}
	err = xenbus_transaction_end(xbt, 0);
	if (err == -EAGAIN)
		goto again;
	if (err)
		xenbus_dev_fatal(dev, err, "ending transaction");
	return 0;
 abort:
	xenbus_transaction_end(xbt, 1);
	return -1;
}

/**
 *
 */
static int cdrom_is_type(struct backend_info *be)
{
	DPRINTK("type:%x", be->blkif->vbd.type );
	return (be->blkif->vbd.type & VDISK_CDROM)
	       && (be->blkif->vbd.type & GENHD_FL_REMOVABLE);
}

/**
 *
 */
void cdrom_add_media_watch(struct backend_info *be)
{
	struct xenbus_device *dev = be->dev;
	int err;

	DPRINTK("nodename:%s", dev->nodename);
	if (cdrom_is_type(be)) {
		DPRINTK("is a cdrom");
		if ( cdrom_xenstore_write_media_present(be) == 0 ) {
			DPRINTK( "xenstore wrote OK");
			err = xenbus_watch_path2(dev, dev->nodename, MEDIA_PRESENT,
						 &be->backend_cdrom_watch,
						 cdrom_media_changed);
			if (err)
				DPRINTK( "media_present watch add failed" );
		}
	}
}

/**
 * Callback received when the "media_present" xenstore node is changed
 */
static void cdrom_media_changed(struct xenbus_watch *watch,
				const char **vec, unsigned int len)
{
	int err;
	unsigned media_present;
	struct backend_info *be
		= container_of(watch, struct backend_info, backend_cdrom_watch);
	struct xenbus_device *dev = be->dev;

	if (!cdrom_is_type(be)) {
		DPRINTK("callback not for a cdrom" );
		return;
	}

	err = xenbus_scanf(XBT_NIL, dev->nodename, MEDIA_PRESENT, "%d",
			   &media_present);
	if (err == 0 || err == -ENOENT) {
		DPRINTK("xenbus_read of cdrom media_present node error:%d",err);
		return;
	}

	if (media_present == 0)
		vbd_free(&be->blkif->vbd);
	else {
		char *p = strrchr(dev->otherend, '/') + 1;
		long handle = simple_strtoul(p, NULL, 0);

		if (!be->blkif->vbd.bdev) {
			err = vbd_create(be->blkif, handle, be->major, be->minor,
					 !strchr(be->mode, 'w'), 1);
			if (err) {
				be->major = be->minor = 0;
				xenbus_dev_fatal(dev, err, "creating vbd structure");
				return;
			}
		}
	}
}
