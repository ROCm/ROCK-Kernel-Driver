/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Contains cluster operations for cryptcompress object plugin (see
   http://www.namesys.com/cryptcompress_design.txt for details). */

#include "plugin/plugin_header.h"
#include "plugin/plugin.h"
#include "inode.h"

/*         Concepts of clustering. Definition of cluster size.
	   Data clusters, page clusters, disk clusters.


   In order to compress plain text we first should split it into chunks.
   Then we process each chunk independently by the following function:

   void alg(char *input_ptr, int input_length, char *output_ptr, int *output_length);

   where:
   input_ptr is a pointer to the first byte of input chunk (that contains plain text),
   input_len is a length of input chunk,
   output_ptr is a pointer to the first byte of output chunk (that contains processed text),
   *output_len is a length of output chunk.

   the length of output chunk depends both on input_len and on the content of
   input chunk.  input_len (which can be assigned an arbitrary value) affects the
   compression quality (the more input_len the better the compression quality).
   For each cryptcompress file we assign special attribute - cluster size:

   Cluster size is a file attribute, which determines the maximal size
   of input chunk that we use for compression.

   So if we wanna compress a 10K-file with a cluster size of 4K, we split this file
   into three chunks (first and second - 4K, third - 2K). Those chunks are
   clusters in the space of file offsets (data clusters).

   Cluster sizes are represented as (PAGE_CACHE_SIZE << shift), where
   shift (= 0, 1, 2,... ).  You'll note that this representation
   affects the allowed values for cluster size.  This is stored in
   disk stat-data (CLUSTER_STAT, layout is in reiser4_cluster_stat (see
   (plugin/item/static_stat.h) for details).
   Note that working with
   cluster_size > PAGE_SIZE (when cluster_shift > 0, and cluster contains more
   then one page) is suboptimal because before compression we should assemble
   all cluster pages into one flow (this means superfluous memcpy during
   read/write). So the better way to increase cluster size (and therefore
   compression quality) is making PAGE_SIZE larger (for instance by page
   clustering stuff of William Lee). But if you need PAGE_SIZE < cluster_size,
   then use the page clustering offered by reiser4.

   The inode mapping of a cryptcompress file contains pages filled by plain text.
   Cluster size also defines clustering in address space. For example,
   101K-file with cluster size 16K (cluster shift = 2), which can be mapped
   into 26 pages, has 7 "page clusters": first six clusters contains 4 pages
   and one cluster contains 2 pages (for the file tail).

   We split each output (compressed) chunk into special items to provide
   tight packing of data on disk (currently only ctails hold compressed data).
   This set of items we call a "disk cluster".

   Each cluster is defined (like pages are) by its index (e.g. offset,
   but the unit is cluster size instead of PAGE_SIZE). Key offset of
   the first unit of the first item of each disk cluster (we call this a
   "key of disk cluster") is a multiple of the cluster index.

   All read/write/truncate operations are performed upon clusters.
   For example, if we wanna read 40K of a cryptcompress file with cluster size 16K
   from offset = 20K, we first need to read two clusters (of indexes 1, 2). This
   means that all main methods of cryptcompress object plugin call appropriate
   cluster operation.

   For the same index we use one structure (type reiser4_cluster_t) to
   represent all data/page/disk clusters.  (EDWARD-FIXME-HANS: are you
   sure that is good style? and where is the code that goes with this comment....;-) )
*/

static int
change_cluster(struct inode * inode, reiser4_plugin * plugin)
{
	int result = 0;

	assert("edward-1324", inode != NULL);
	assert("edward-1325", plugin != NULL);
	assert("edward-1326", is_reiser4_inode(inode));
	assert("edward-1327", plugin->h.type_id == REISER4_CLUSTER_PLUGIN_TYPE);

	if (inode_file_plugin(inode)->h.id == DIRECTORY_FILE_PLUGIN_ID)
		result = plugin_set_cluster(&reiser4_inode_data(inode)->pset,
					    &plugin->clust);
	else
		result = RETERR(-EINVAL);
	return result;
}

static reiser4_plugin_ops cluster_plugin_ops = {
	.init     = NULL,
	.load     = NULL,
	.save_len = NULL,
	.save     = NULL,
	.change   = &change_cluster
};

#define SUPPORT_CLUSTER(SHIFT, ID, LABEL, DESC)                    \
	[CLUSTER_ ## ID ## _ID] = {                                \
		.h = {                                             \
			.type_id = REISER4_CLUSTER_PLUGIN_TYPE,    \
			.id = CLUSTER_ ## ID ## _ID,               \
			.pops = &cluster_plugin_ops,               \
			.label = LABEL,                            \
			.desc = DESC,                              \
			.linkage = TYPE_SAFE_LIST_LINK_ZERO        \
		},                                                 \
		.shift = SHIFT                                     \
	}

cluster_plugin cluster_plugins[LAST_CLUSTER_ID] = {
	SUPPORT_CLUSTER(12, 4K, "4K", "Minimal"),
	SUPPORT_CLUSTER(13, 8K, "8K", "Small"),
	SUPPORT_CLUSTER(14, 16K, "16K", "Average"),
	SUPPORT_CLUSTER(15, 32K, "32K", "Big"),
	SUPPORT_CLUSTER(16, 64K, "64K", "Large")
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
