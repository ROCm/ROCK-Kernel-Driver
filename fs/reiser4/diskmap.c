/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */
/* Functions to deal with diskmap storage - read-only storage (currently can only be
   set via fs-creation process) for use by various plugins */


#include "debug.h"
#include "super.h"
#include "diskmap.h"

#include <linux/fs.h>

/* Looks through chain of diskmap blocks, looking for table entry where label and parameter
   patch passed in "label" and "parameter"
   Returns 0 on success, -1 if nothing was found or error have occurred. */
reiser4_internal int
reiser4_get_diskmap_value( u32 label, u32 parameter, u64 *value)
{
	struct super_block *sb = reiser4_get_current_sb();
	int retval = -1;

	assert("green-2006", label != REISER4_FIXMAP_END_LABEL && label != REISER4_FIXMAP_NEXT_LABEL);

	if ( get_super_private(sb)->diskmap_block ) { /* If there is diskmap table, we need to read and parse it */
		struct buffer_head *diskmap_bh;
		struct reiser4_diskmap *diskmap;
		int i = 0;

		diskmap_bh = sb_bread(sb, get_super_private(sb)->diskmap_block);
search_table:
		if ( !diskmap_bh ) {
			warning("green-2005", "Cannot read diskmap while doing bitmap checks");
			return -1;
		}

		diskmap = (struct reiser4_diskmap *) diskmap_bh->b_data;
		if ( strncmp(diskmap->magic, REISER4_FIXMAP_MAGIC, sizeof(REISER4_FIXMAP_MAGIC)-1 ) ) {
			/* Wrong magic */
			brelse(diskmap_bh);
			warning("green-2004", "diskmap is specified, but its magic is wrong");
			return -1;
		}

		/* Since entries in tables are sorted, we iterate until we hit item that we are looking for,
		   or we reach end of whole fixmap or end of current block */
		while (((d32tocpu(&diskmap->table[i].label) <= label) &&
		       (d32tocpu(&diskmap->table[i].parameter) < parameter)) &&
			/* Also check that we do not fall out of current block */
			((sb->s_blocksize - sizeof(diskmap->magic))/sizeof(diskmap->table[0]) >= i))
			i++;

		if ( i > (sb->s_blocksize - sizeof(diskmap->magic))/sizeof(diskmap->table[0]) ) {
			warning("green-2004", "diskmap block %Ld is not properly terminated", (long long)diskmap_bh->b_blocknr);
			brelse(diskmap_bh);
			return -1;
		}

		/* Is this last entry in current table that holds disk block with more data ? */
		if ( d32tocpu(&diskmap->table[i].label) == REISER4_FIXMAP_NEXT_LABEL ) { /* Need to load next diskmap block */
			sector_t next_diskmap_block = d64tocpu(&diskmap->table[i].value);
			brelse(diskmap_bh);
			diskmap_bh = sb_bread(sb, next_diskmap_block);
			i = 0;
			goto search_table;
		}

		/* See if we have found table entry we are looking for */
		if ( (d32tocpu(&diskmap->table[i].label) == label) &&
		     (d32tocpu(&diskmap->table[i].parameter) == parameter) ) {
			*value = d64tocpu(&diskmap->table[i].value);
			retval = 0;
		}
		brelse(diskmap_bh);
	}

	return retval;
}
