/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* reiser4 digest transform plugin (is used by cryptcompress object plugin) */

#include "../debug.h"
#include "plugin_header.h"
#include "plugin.h"
#include "cryptcompress.h"

#include <linux/types.h>

#define NONE_DIGEST_SIZE 0

REGISTER_NONE_ALG(digest, DIGEST)

/* digest plugins */
digest_plugin digest_plugins[LAST_DIGEST_ID] = {
	[NONE_DIGEST_ID] = {
		.h = {
			.type_id = REISER4_DIGEST_PLUGIN_TYPE,
			.id = NONE_DIGEST_ID,
			.pops = NULL,
			.label = "none",
			.desc = "trivial digest",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.dsize = NONE_DIGEST_SIZE,
		.alloc = alloc_none_digest,
		.free = free_none_digest,
	}
};

