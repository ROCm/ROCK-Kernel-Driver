/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* reiser4 compression transform plugins */

#include "../../debug.h"
#include "../plugin.h"
#include "../cryptcompress.h"
#include "minilzo.h"

#include <linux/config.h>
#include <linux/zlib.h>
#include <linux/types.h>
#include <linux/hardirq.h>

/******************************************************************************/
/*                         null compression                                   */
/******************************************************************************/

#define NONE_NRCOPY 1

static int
null_min_tfm_size(void)
{
	return 1;
}

static void
null_compress(coa_t coa, __u8 * src_first, unsigned src_len,
	      __u8 * dst_first, unsigned *dst_len)
{
	int i;
	assert("edward-793", coa == NULL);
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
null_decompress(coa_t coa, __u8 * src_first, unsigned src_len,
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

static int gzip1_overrun(unsigned src_len UNUSED_ARG)
{
	return 0;
}

static coa_t
gzip1_alloc(tfm_action act)
{
	coa_t coa = NULL;
	int ret = -ENXIO;
#if REISER4_GZIP_TFM
	ret = 0;
	switch (act) {
	case TFM_WRITE:	/* compress */
		coa = vmalloc(zlib_deflate_workspacesize());
		if (!coa) {
			ret = -ENOMEM;
			break;
		}
		xmemset(coa, 0, zlib_deflate_workspacesize());
		break;
	case TFM_READ:	/* decompress */
		coa = vmalloc(zlib_inflate_workspacesize());
		if (!coa) {
			ret = -ENOMEM;
			break;
		}
		xmemset(coa, 0, zlib_inflate_workspacesize());
		break;
	default:
		impossible("edward-767",
			   "trying to alloc workspace for unknown tfm action");
	}
#endif
	if (ret) {
		warning("edward-768",
			"alloc workspace for gzip1 (tfm action = %d) failed\n",
			act);
		return ERR_PTR(ret);
	}
	return coa;
}

static void gzip1_free(coa_t coa, tfm_action act)
{
#if REISER4_GZIP_TFM
	assert("edward-769", coa != NULL);

	switch (act) {
	case TFM_WRITE:	/* compress */
		vfree(coa);
		break;
	case TFM_READ:	/* decompress */
		vfree(coa);
		break;
	default:
		impossible("edward-770",
			   "free workspace for unknown tfm action");
	}
#endif
	return;
}

static int
gzip1_min_tfm_size(void)
{
	return 64;
}

static void
gzip1_compress(coa_t coa, __u8 * src_first, unsigned src_len,
	       __u8 * dst_first, unsigned *dst_len)
{
#if REISER4_GZIP_TFM
	int ret = 0;
	struct z_stream_s stream;

	xmemset(&stream, 0, sizeof(stream));

	assert("edward-842", coa != NULL);
	assert("edward-875", src_len != 0);

	stream.workspace = coa;
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
	return;
 rollback:
	*dst_len = src_len;
#endif
	return;
}

static void
gzip1_decompress(coa_t coa, __u8 * src_first, unsigned src_len,
		 __u8 * dst_first, unsigned *dst_len)
{
#if REISER4_GZIP_TFM
	int ret = 0;
	struct z_stream_s stream;

	xmemset(&stream, 0, sizeof(stream));

	assert("edward-843", coa != NULL);
	assert("edward-876", src_len != 0);

	stream.workspace = coa;
	ret = zlib_inflateInit2(&stream, -GZIP1_DEF_WINBITS);
	if (ret != Z_OK) {
		warning("edward-774", "zlib_inflateInit2 returned %d\n", ret);
		return;
	}
	ret = zlib_inflateReset(&stream);
	if (ret != Z_OK) {
		warning("edward-775", "zlib_inflateReset returned %d\n", ret);
		return;
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
		return;
	}
	*dst_len = stream.total_out;
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

#define LZO_HEAP_SIZE(size) \
	sizeof(lzo_align_t) * (((size) + (sizeof(lzo_align_t) - 1)) / sizeof(lzo_align_t))

static coa_t
lzo1_alloc(tfm_action act)
{
	int ret = 0;
	coa_t coa = NULL;

	switch (act) {
	case TFM_WRITE:	/* compress */
		coa = vmalloc(LZO_HEAP_SIZE(LZO1X_1_MEM_COMPRESS));
		if (!coa) {
			ret = -ENOMEM;
			break;
		}
		xmemset(coa, 0, LZO_HEAP_SIZE(LZO1X_1_MEM_COMPRESS));
	case TFM_READ:	/* decompress */
		break;
	default:
		impossible("edward-877",
			   "trying to alloc workspace for unknown tfm action");
	}
	if (ret) {
		warning("edward-878",
			"alloc workspace for lzo1 (tfm action = %d) failed\n",
			act);
		return ERR_PTR(ret);
	}
	return coa;
}

static void
lzo1_free(coa_t coa, tfm_action act)
{
	assert("edward-879", coa != NULL);

	switch (act) {
	case TFM_WRITE:	/* compress */
		vfree(coa);
	case TFM_READ:	/* decompress */
		break;
	default:
		impossible("edward-880",
			   "trying to free workspace for unknown tfm action");
	}
	return;
}

static int
lzo1_min_tfm_size(void)
{
	return 256;
}

static void
lzo1_compress(coa_t coa, __u8 * src_first, unsigned src_len,
	      __u8 * dst_first, unsigned *dst_len)
{
	int result;

	assert("edward-846", coa != NULL);
	assert("edward-847", src_len != 0);

	result = lzo_init();

	if (result != LZO_E_OK) {
		warning("edward-848", "lzo_init() failed\n");
		goto out;
	}

	result =
		lzo1x_1_compress(src_first, src_len, dst_first, dst_len, coa);
	if (result != LZO_E_OK) {
		warning("edward-849", "lzo1x_1_compress failed\n");
		goto out;
	}
	if (*dst_len >= src_len) {
		//warning("edward-850", "lzo1x_1_compress: incompressible data\n");
		goto out;
	}
	return;
 out:
	*dst_len = src_len;
	return;
}

static void
lzo1_decompress(coa_t coa, __u8 * src_first, unsigned src_len,
		__u8 * dst_first, unsigned *dst_len)
{
	int result;

	assert("edward-851", coa == NULL);
	assert("edward-852", src_len != 0);

	result = lzo_init();

	if (result != LZO_E_OK) {
		warning("edward-888", "lzo_init() failed\n");
		return;
	}

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
				 .min_tfm_size = NULL,
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
				 .min_tfm_size = null_min_tfm_size,
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
				 .alloc = lzo1_alloc,
				 .free = lzo1_free,
				 .min_tfm_size = lzo1_min_tfm_size,
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
				  .overrun = gzip1_overrun,
				  .alloc = gzip1_alloc,
				  .free = gzip1_free,
				  .min_tfm_size = gzip1_min_tfm_size,
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
