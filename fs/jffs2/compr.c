/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by Arjan van de Ven <arjanv@redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: compr.c,v 1.27 2003/10/04 08:33:06 dwmw2 Exp $
 *
 */

#if defined(__KERNEL__) || defined (__ECOS)
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/types.h>
#else 
#define KERN_DEBUG
#define KERN_NOTICE
#define KERN_WARNING
#define printk printf
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#endif

#include <linux/jffs2.h>

int jffs2_zlib_compress(unsigned char *data_in, unsigned char *cpage_out, uint32_t *sourcelen, uint32_t *dstlen);
void jffs2_zlib_decompress(unsigned char *data_in, unsigned char *cpage_out, uint32_t srclen, uint32_t destlen);
int jffs2_rtime_compress(unsigned char *data_in, unsigned char *cpage_out, uint32_t *sourcelen, uint32_t *dstlen);
void jffs2_rtime_decompress(unsigned char *data_in, unsigned char *cpage_out, uint32_t srclen, uint32_t destlen);
int jffs2_rubinmips_compress(unsigned char *data_in, unsigned char *cpage_out, uint32_t *sourcelen, uint32_t *dstlen);
void jffs2_rubinmips_decompress(unsigned char *data_in, unsigned char *cpage_out, uint32_t srclen, uint32_t destlen);
int jffs2_dynrubin_compress(unsigned char *data_in, unsigned char *cpage_out, uint32_t *sourcelen, uint32_t *dstlen);
void jffs2_dynrubin_decompress(unsigned char *data_in, unsigned char *cpage_out, uint32_t srclen, uint32_t destlen);


/* jffs2_compress:
 * @data: Pointer to uncompressed data
 * @cdata: Pointer to buffer for compressed data
 * @datalen: On entry, holds the amount of data available for compression.
 *	On exit, expected to hold the amount of data actually compressed.
 * @cdatalen: On entry, holds the amount of space available for compressed
 *	data. On exit, expected to hold the actual size of the compressed
 *	data.
 *
 * Returns: Byte to be stored with data indicating compression type used.
 * Zero is used to show that the data could not be compressed - the 
 * compressed version was actually larger than the original.
 *
 * If the cdata buffer isn't large enough to hold all the uncompressed data,
 * jffs2_compress should compress as much as will fit, and should set 
 * *datalen accordingly to show the amount of data which were compressed.
 */
unsigned char jffs2_compress(unsigned char *data_in, unsigned char *cpage_out, 
		    uint32_t *datalen, uint32_t *cdatalen)
{
	int ret;

	ret = jffs2_zlib_compress(data_in, cpage_out, datalen, cdatalen);
	if (!ret) {
		return JFFS2_COMPR_ZLIB;
	}
#if 0 /* Disabled 23/9/1. With zlib it hardly ever gets a look in */
	ret = jffs2_dynrubin_compress(data_in, cpage_out, datalen, cdatalen);
	if (!ret) {
		return JFFS2_COMPR_DYNRUBIN;
	}
#endif
#if 0 /* Disabled 26/2/1. Obsoleted by dynrubin */
	ret = jffs2_rubinmips_compress(data_in, cpage_out, datalen, cdatalen);
	if (!ret) {
		return JFFS2_COMPR_RUBINMIPS;
	}
#endif
	/* rtime does manage to recompress already-compressed data */
	ret = jffs2_rtime_compress(data_in, cpage_out, datalen, cdatalen);
	if (!ret) {
		return JFFS2_COMPR_RTIME;
	}
#if 0
	/* We don't need to copy. Let the caller special-case the COMPR_NONE case. */
	/* If we get here, no compression is going to work */
	/* But we might want to use the fragmentation part -- Arjan */
	memcpy(cpage_out,data_in,min(*datalen,*cdatalen));
	if (*datalen > *cdatalen)
		*datalen = *cdatalen;
#endif		
	return JFFS2_COMPR_NONE; /* We failed to compress */

}


int jffs2_decompress(unsigned char comprtype, unsigned char *cdata_in, 
		     unsigned char *data_out, uint32_t cdatalen, uint32_t datalen)
{
	switch (comprtype) {
	case JFFS2_COMPR_NONE:
		/* This should be special-cased elsewhere, but we might as well deal with it */
		memcpy(data_out, cdata_in, datalen);
		break;

	case JFFS2_COMPR_ZERO:
		memset(data_out, 0, datalen);
		break;

	case JFFS2_COMPR_ZLIB:
		jffs2_zlib_decompress(cdata_in, data_out, cdatalen, datalen);
		break;

	case JFFS2_COMPR_RTIME:
		jffs2_rtime_decompress(cdata_in, data_out, cdatalen, datalen);
		break;

	case JFFS2_COMPR_RUBINMIPS:
#if 0 /* Disabled 23/9/1 */
		jffs2_rubinmips_decompress(cdata_in, data_out, cdatalen, datalen);
#else
		printk(KERN_WARNING "JFFS2: Rubinmips compression encountered but support not compiled in!\n");
#endif
		break;
	case JFFS2_COMPR_DYNRUBIN:
#if 1 /* Phase this one out */
		jffs2_dynrubin_decompress(cdata_in, data_out, cdatalen, datalen);
#else
		printk(KERN_WARNING "JFFS2: Dynrubin compression encountered but support not compiled in!\n");
#endif
		break;

	default:
		printk(KERN_NOTICE "Unknown JFFS2 compression type 0x%02x\n", comprtype);
		return -EIO;
	}
	return 0;
}
