/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* Crypto-plugins for reiser4 cryptcompress objects */

#include "debug.h"
#include "plugin/plugin.h"
#include "plugin/cryptcompress.h"
#include <linux/types.h>
#include <linux/random.h>

#define MAX_CRYPTO_BLOCKSIZE 128
#define NONE_EXPKEY_WORDS 8
#define NONE_BLOCKSIZE 8

/*
  Default align() method of the crypto-plugin (look for description of this method
  in plugin/plugin.h)

1) creates the aligning armored format of the input flow before encryption.
   "armored" means that padding is filled by private data (for example,
   pseudo-random sequence of bytes is not private data).
2) returns length of appended padding

   [ flow | aligning_padding ]
            ^
            |
	  @pad
*/
UNUSED_ARG static int
align_cluster_common(__u8 *pad /* pointer to the first byte of aligning format */,
		     int flow_size /* size of non-aligned flow */,
				int blocksize /* crypto-block size */)
{
	int pad_size;

	assert("edward-01", pad != NULL);
	assert("edward-02", flow_size != 0);
	assert("edward-03", blocksize != 0 || blocksize <= MAX_CRYPTO_BLOCKSIZE);

	pad_size = blocksize - (flow_size % blocksize);
	get_random_bytes (pad, pad_size);
	return pad_size;
}

/* common scale method (look for description of this method in plugin/plugin.h)
   for all symmetric algorithms which doesn't scale anything
*/
static loff_t scale_common(struct inode * inode UNUSED_ARG,
			   size_t blocksize UNUSED_ARG /* crypto block size, which is returned
							  by blocksize method of crypto plugin */,
			   loff_t src_off /* offset to scale */)
{
	return src_off;
}

REGISTER_NONE_ALG(crypt, CRYPTO)

/* EDWARD-FIXME-HANS: why is this not in the plugin directory? */

/* crypto plugins */
crypto_plugin crypto_plugins[LAST_CRYPTO_ID] = {
	[NONE_CRYPTO_ID] = {
		.h = {
			.type_id = REISER4_CRYPTO_PLUGIN_TYPE,
			.id = NONE_CRYPTO_ID,
			.pops = NULL,
			/* If you wanna your files to not be crypto
			   transformed, specify this crypto pluigin */
			.label = "none",
			.desc = "absence of crypto transform",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.alloc = alloc_none_crypt,
		.free = free_none_crypt,
		.nr_keywords = NONE_EXPKEY_WORDS,
		.scale = scale_common,
	        .align_cluster = NULL,
	        .setkey = NULL,
	        .encrypt = NULL,
	        .decrypt = NULL
	}
};

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
