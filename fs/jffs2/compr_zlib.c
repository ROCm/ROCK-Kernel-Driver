/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * The original JFFS, from which the design for JFFS2 was derived,
 * was designed and implemented by Axis Communications AB.
 *
 * The contents of this file are subject to the Red Hat eCos Public
 * License Version 1.1 (the "Licence"); you may not use this file
 * except in compliance with the Licence.  You may obtain a copy of
 * the Licence at http://www.redhat.com/
 *
 * Software distributed under the Licence is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 * See the Licence for the specific language governing rights and
 * limitations under the Licence.
 *
 * The Original Code is JFFS2 - Journalling Flash File System, version 2
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the RHEPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the RHEPL or the GPL.
 *
 * $Id: compr_zlib.c,v 1.8 2001/09/20 15:28:31 dwmw2 Exp $
 *
 */

#include "zlib.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/mtd/compatmac.h> /* for min() */
#include <linux/slab.h>
#include <linux/jffs2.h>
#include "nodelist.h"

static void *zalloc(void *opaque, unsigned nr, unsigned size)
{
	/* How much does it request? Should we use vmalloc? Or be dynamic? */
	return kmalloc(nr * size, GFP_KERNEL);
}

static void zfree(void *opaque, void *addr)
{
	kfree(addr);
}
#else
#define min(x,y) ((x)<(y)?(x):(y))
#ifndef D1
#define D1(x)
#endif
#define KERN_DEBUG
#define KERN_NOTICE
#define KERN_WARNING
#define printk printf
#include <stdio.h>
#include <asm/types.h>
#endif

	/* Plan: call deflate() with avail_in == *sourcelen, 
		avail_out = *dstlen - 12 and flush == Z_FINISH. 
		If it doesn't manage to finish,	call it again with
		avail_in == 0 and avail_out set to the remaining 12
		bytes for it to clean up. 
	   Q: Is 12 bytes sufficient?
	*/
#define STREAM_END_SPACE 12

int zlib_compress(unsigned char *data_in, unsigned char *cpage_out, 
		   __u32 *sourcelen, __u32 *dstlen)
{
	z_stream strm;
	int ret;

	if (*dstlen <= STREAM_END_SPACE)
		return -1;

#ifdef __KERNEL__
	strm.zalloc = zalloc;
	strm.zfree = zfree;
#else
	strm.zalloc = (void *)0;
	strm.zfree = (void *)0;
#endif

	if (Z_OK != deflateInit(&strm, 3)) {
		printk(KERN_WARNING "deflateInit failed\n");
		return -1;
	}
	strm.next_in = data_in;
	strm.total_in = 0;
	
	strm.next_out = cpage_out;
	strm.total_out = 0;

	while (strm.total_out < *dstlen - STREAM_END_SPACE && strm.total_in < *sourcelen) {
		strm.avail_out = *dstlen - (strm.total_out + STREAM_END_SPACE);
		strm.avail_in = min((unsigned)(*sourcelen-strm.total_in), strm.avail_out);
		D1(printk(KERN_DEBUG "calling deflate with avail_in %d, avail_out %d\n",
			  strm.avail_in, strm.avail_out));
		ret = deflate(&strm, Z_PARTIAL_FLUSH);
		D1(printk(KERN_DEBUG "deflate returned with avail_in %d, avail_out %d, total_in %ld, total_out %ld\n", 
			  strm.avail_in, strm.avail_out, strm.total_in, strm.total_out));
		if (ret != Z_OK) {
			D1(printk(KERN_DEBUG "deflate in loop returned %d\n", ret));
			deflateEnd(&strm);
			return -1;
		}
	}
	strm.avail_out += STREAM_END_SPACE;
	strm.avail_in = 0;
	ret = deflate(&strm, Z_FINISH);
	if (ret != Z_STREAM_END) {
		D1(printk(KERN_DEBUG "final deflate returned %d\n", ret));
		deflateEnd(&strm);
		return -1;
	}
	deflateEnd(&strm);

	D1(printk(KERN_DEBUG "zlib compressed %ld bytes into %ld\n", strm.total_in, strm.total_out));

	if (strm.total_out >= strm.total_in)
		return -1;


	*dstlen = strm.total_out;
	*sourcelen = strm.total_in;
	return 0;
}

void zlib_decompress(unsigned char *data_in, unsigned char *cpage_out,
		      __u32 srclen, __u32 destlen)
{
	z_stream strm;
	int ret;

#ifdef __KERNEL__
	strm.zalloc = zalloc;
	strm.zfree = zfree;
#else
	strm.zalloc = (void *)0;
	strm.zfree = (void *)0;
#endif

	if (Z_OK != inflateInit(&strm)) {
		printk(KERN_WARNING "inflateInit failed\n");
		return;
	}
	strm.next_in = data_in;
	strm.avail_in = srclen;
	strm.total_in = 0;
	
	strm.next_out = cpage_out;
	strm.avail_out = destlen;
	strm.total_out = 0;

	while((ret = inflate(&strm, Z_FINISH)) == Z_OK)
		;
	if (ret != Z_STREAM_END) {
		printk(KERN_NOTICE "inflate returned %d\n", ret);
	}
	inflateEnd(&strm);
}
