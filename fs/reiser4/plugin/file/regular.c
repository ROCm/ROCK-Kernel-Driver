/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Contains Reiser4 regular plugins which:
   . specify a set of reiser4 regular object plugins,
   . used by directory plugin to create entries powered by specified
     regular plugins */

#include "../plugin.h"

regular_plugin regular_plugins[LAST_REGULAR_ID] = {
	[UF_REGULAR_ID] = {
		.h = {
			.type_id = REISER4_REGULAR_PLUGIN_TYPE,
			.id = UF_REGULAR_ID,
			.pops = NULL,
			.label = "unixfile",
			.desc = "Unix file regular plugin",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.id = UNIX_FILE_PLUGIN_ID
	},
	[CRC_REGULAR_ID] = {
		.h = {
			.type_id = REISER4_REGULAR_PLUGIN_TYPE,
			.id = CRC_REGULAR_ID,
			.pops = NULL,
			.label = "cryptcompress",
			.desc = "Cryptcompress regular plugin",
			.linkage = TYPE_SAFE_LIST_LINK_ZERO
		},
		.id = CRC_FILE_PLUGIN_ID
	}
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
