/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* reiser4 compression transform plugins */

#include "../../debug.h"
#include "../plugin.h"
#include "../cryptcompress.h"
#include "minilzo.h"

#include <linux/config.h>
#include <linux/zlib.h>
#include <linux/vmalloc.h>
#include <linux/types.h>

/******************************************************************************/
/*                         null compression                                   */
/******************************************************************************/
static void
null_compress(void *ctx, __u8 * src_first, unsigned src_len,
	      __u8 * dst_first, unsigned *dst_len)
{
	int i;
	assert("edward-793", ctx == NULL);
	assert("edward-794", src_first != NULL);
	assert("edward-795", dst_first != NULL);
	assert("edward-796", src_len != 0);
	assert("edward-797", dst_len != NULL);

	for (i = 0; i < NONE_NRCOPY; i++)
		xmemcpy(dst_first, src_first, src_len);
	*dst_len = src_len;
	return;
}

static void
null_decompress(void *ctx, __u8 * src_first, unsigned src_len,
		__u8 * dst_first, unsigned *dst_len)
{
	impossible("edward-798", "trying to decompress uncompressed data");
}

/******************************************************************************/
/*                         gzip1 compression                                  */
/******************************************************************************/

#define GZIP1_DEF_LEVEL		        Z_BEST_SPEED
#define GZIP1_DEF_WINBITS		15
#define GZIP1_DEF_MEMLEVEL		MAX_MEM_LEVEL

static int gzip6_overrun(unsigned src_len UNUSED_ARG)
{
	return 0;
}

static int gzip1_alloc(tfm_info_t * ctx, tfm_action act)
{
	int ret = -ENXIO;
	assert("edward-766", *ctx == NULL);
#if REISER4_GZIP_TFM
	ret = 0;
	switch (act) {
	case TFM_WRITE:	/* compress */
		*ctx = __vmalloc(zlib_deflate_workspacesize(),
				 (in_softirq()? GFP_ATOMIC : GFP_KERNEL) |
				 __GFP_HIGHMEM, PAGE_KERNEL);
		if (*ctx == NULL) {
			ret = -ENOMEM;
			break;
		}
		xmemset(*ctx, 0, zlib_deflate_workspacesize());
		break;
	case TFM_READ:		/* decompress */
		*ctx = reiser4_kmalloc(zlib_inflate_workspacesize(),
				       (in_softirq()? GFP_ATOMIC : GFP_KERNEL));
		if (*ctx == NULL) {
			ret = -ENOMEM;
			break;
		}
		xmemset(*ctx, 0, zlib_inflate_workspacesize());
		break;
	default:
		impossible("edward-767",
			   "alloc workspace for unknown tfm action");
	}
#endif
	if (ret)
		warning("edward-768",
			"alloc workspace for gzip1 (tfm action = %d) failed\n",
			act);
	return ret;
}

static void gzip1_free(tfm_info_t * ctx, tfm_action act)
{
#if REISER4_GZIP_TFM
	assert("edward-769", *ctx != NULL);

	switch (act) {
	case TFM_WRITE:	/* compress */
		vfree(*ctx);
		break;
	case TFM_READ:		/* decompress */
		reiser4_kfree(*ctx);
		break;
	default:
		impossible("edward-770",
			   "free workspace for unknown tfm action");
	}
#endif
	return;
}

static void
gzip1_compress(tfm_info_t ctx, __u8 * src_first, unsigned src_len,
	       __u8 * dst_first, unsigned *dst_len)
{
#if REISER4_GZIP_TFM
	int ret = 0;
	struct z_stream_s stream;
	compression_plugin *cplug =
	    compression_plugin_by_id(GZIP1_COMPRESSION_ID);

	xmemset(&stream, 0, sizeof(stream));

	assert("edward-842", ctx != NULL);

	if (!ctx) {
		ret = cplug->alloc(&stream.workspace, TFM_WRITE);
		if (ret)
			goto rollback;
	} else
		stream.workspace = ctx;

	ret = zlib_deflateInit2(&stream, GZIP1_DEF_LEVEL, Z_DEFLATED,
				-GZIP1_DEF_WINBITS, GZIP1_DEF_MEMLEVEL,
				Z_DEFAULT_STRATEGY);
	if (ret != Z_OK) {
		warning("edward-771", "zlib_deflateInit2 returned %d\n", ret);
		goto rollback;
	}
	ret = zlib_deflateReset(&stream);
	if (ret != Z_OK) {
		warning("edward-772", "zlib_deflateReset returned %d\n", ret);
		goto rollback;
	}
	stream.next_in = src_first;
	stream.avail_in = src_len;
	stream.next_out = dst_first;
	stream.avail_out = *dst_len;

	ret = zlib_deflate(&stream, Z_FINISH);
	if (ret != Z_STREAM_END) {
		warning("edward-773", "zlib_deflate returned %d\n", ret);
		goto rollback;
	}
	*dst_len = stream.total_out;
	if (!ctx)
		cplug->free(&stream.workspace, TFM_WRITE);
	return;
      rollback:
	if (!ctx && stream.workspace)
		cplug->free(&stream.workspace, TFM_WRITE);
	*dst_len = src_len;
#endif
	return;
}

static void
gzip1_decompress(tfm_info_t ctx, __u8 * src_first, unsigned src_len,
		 __u8 * dst_first, unsigned *dst_len)
{
#if REISER4_GZIP_TFM
	int ret = 0;
	struct z_stream_s stream;
	compression_plugin *cplug =
	    compression_plugin_by_id(GZIP1_COMPRESSION_ID);

	xmemset(&stream, 0, sizeof(stream));

	assert("edward-843", ctx == NULL);

	if (!ctx) {
		ret = cplug->alloc(&stream.workspace, TFM_READ);
		if (ret)
			goto out;
	} else
		stream.workspace = ctx;

	ret = zlib_inflateInit2(&stream, -GZIP1_DEF_WINBITS);
	if (ret != Z_OK) {
		warning("edward-774", "zlib_inflateInit2 returned %d\n", ret);
		goto out;
	}
	ret = zlib_inflateReset(&stream);
	if (ret != Z_OK) {
		warning("edward-775", "zlib_inflateReset returned %d\n", ret);
		goto out;
	}

	stream.next_in = src_first;
	stream.avail_in = src_len;
	stream.next_out = dst_first;
	stream.avail_out = *dst_len;

	ret = zlib_inflate(&stream, Z_SYNC_FLUSH);
	/*
	 * Work around a bug in zlib, which sometimes wants to taste an extra
	 * byte when being used in the (undocumented) raw deflate mode.
	 * (From USAGI).
	 */
	if (ret == Z_OK && !stream.avail_in && stream.avail_out) {
		u8 zerostuff = 0;
		stream.next_in = &zerostuff;
		stream.avail_in = 1;
		ret = zlib_inflate(&stream, Z_FINISH);
	}
	if (ret != Z_STREAM_END) {
		warning("edward-776", "zlib_inflate returned %d\n", ret);
		goto out;
	}
	*dst_len = stream.total_out;
      out:
	if (!ctx && stream.workspace)
		cplug->free(&stream.workspace, TFM_READ);
#endif
	return;
}

/******************************************************************************/
/*                            none compression                                */
/******************************************************************************/

static int none_overrun(unsigned src_len UNUSED_ARG)
{
	return 0;
}

/******************************************************************************/
/*                            lzo1 compression                                */
/******************************************************************************/

static int lzo1_overrun(unsigned in_len)
{
	return in_len / 64 + 16 + 3;
}

#define HEAP_ALLOC(var,size) \
	lzo_align_t __LZO_MMODEL var [ ((size) + (sizeof(lzo_align_t) - 1)) / sizeof(lzo_align_t) ]

static void
lzo1_compress(tfm_info_t ctx, __u8 * src_first, unsigned src_len,
	      __u8 * dst_first, unsigned *dst_len)
{
	int result;
	HEAP_ALLOC(wrkmem, LZO1X_1_MEM_COMPRESS);

	assert("edward-846", ctx == NULL);
	assert("edward-847", src_len != 0);

	result = lzo_init();

	if (result != LZO_E_OK) {
		warning("edward-848", "lzo_init() failed\n");
		goto out;
	}
	result =
	    lzo1x_1_compress(src_first, src_len, dst_first, dst_len, wrkmem);
	if (result != LZO_E_OK) {
		warning("edward-849", "lzo1x_1_compress failed\n");
		goto out;
	}
	if (*dst_len >= src_len)
		warning("edward-850",
			"lzo1x_1_compress: incompressible data\n");
	return;
      out:
	*dst_len = src_len;
	return;
}

static void
lzo1_decompress(tfm_info_t ctx, __u8 * src_first, unsigned src_len,
		__u8 * dst_first, unsigned *dst_len)
{
	int result;

	assert("edward-851", ctx == NULL);
	assert("edward-852", src_len != 0);

	result = lzo1x_decompress(src_first, src_len, dst_first, dst_len, NULL);
	if (result != LZO_E_OK)
		warning("edward-853", "lzo1x_1_decompress failed\n");
	return;
}

compression_plugin compression_plugins[LAST_COMPRESSION_ID] = {
	[NONE_COMPRESSION_ID] = {
				 .h = {
				       .type_id =
				       REISER4_COMPRESSION_PLUGIN_TYPE,
				       .id = NONE_COMPRESSION_ID,
				       .pops = NULL,
				       .label = "none",
				       .desc =
				       "absence of any compression transform",
				       .linkage = TYPE_SAFE_LIST_LINK_ZERO}
				 ,
				 .overrun = none_overrun,
				 .alloc = NULL,
				 .free = NULL,
				 .compress = NULL,
				 .decompress = NULL}
	,
	[NULL_COMPRESSION_ID] = {
				 .h = {
				       .type_id =
				       REISER4_COMPRESSION_PLUGIN_TYPE,
				       .id = NULL_COMPRESSION_ID,
				       .pops = NULL,
				       .label = "null",
				       .desc = "NONE_NRCOPY times of memcpy",
				       .linkage = TYPE_SAFE_LIST_LINK_ZERO}
				 ,
				 .overrun = none_overrun,
				 .alloc = NULL,
				 .free = NULL,
				 .compress = null_compress,
				 .decompress = null_decompress}
	,
	[LZO1_COMPRESSION_ID] = {
				 .h = {
				       .type_id =
				       REISER4_COMPRESSION_PLUGIN_TYPE,
				       .id = LZO1_COMPRESSION_ID,
				       .pops = NULL,
				       .label = "lzo1",
				       .desc = "lzo1 compression transform",
				       .linkage = TYPE_SAFE_LIST_LINK_ZERO}
				 ,
				 .overrun = lzo1_overrun,
				 .alloc = NULL,
				 .free = NULL,
				 .compress = lzo1_compress,
				 .decompress = lzo1_decompress}
	,
	[GZIP1_COMPRESSION_ID] = {
				  .h = {
					.type_id =
					REISER4_COMPRESSION_PLUGIN_TYPE,
					.id = GZIP1_COMPRESSION_ID,
					.pops = NULL,
					.label = "gzip1",
					.desc = "gzip1 compression transform",
					.linkage = TYPE_SAFE_LIST_LINK_ZERO}
				  ,
				  .overrun = gzip6_overrun,
				  .alloc = gzip1_alloc,
				  .free = gzip1_free,
				  .compress = gzip1_compress,
				  .decompress = gzip1_decompress}
};

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 120
  scroll-step: 1
  End:
*/
