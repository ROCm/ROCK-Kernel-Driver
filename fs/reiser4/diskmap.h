#if !defined (__REISER4_DISKMAP_H__)
#define __REISER4_DISKMAP_H__

/*
 * Disk map.
 *
 * Disk map is a special data structure used by reiser4 as an optional
 * "anchor" of other meta-data. That is, disk map (if present) may contain
 * disk addresses of the rest of meta-data for this file system: master
 * super-block, bitmaps, journal header and footer, etc. Disk map is used to
 * avoid dependency on fixed disk addresses, with the following goals:
 *
 *     1. allow users to experiment with tuning their file system layout, and,
 *     more importantly,
 *
 *     2. allow reiser4 to be survive bad blocks in critical disk locations.
 *
 * That is, disk map allows to "relocate" meta-data structures if their
 * default disk addresses is not accessible.
 *
 * More generally, disk map can be used as a generic table used to store
 * persistent parameters.
 *
 * Currently disk map is read-only for the kernel. It can only be
 * constructed/modified by user-level utilities.
 *
 */

#include "dformat.h"

#define REISER4_FIXMAP_MAGIC "R4FiXMaPv1.0"

#define REISER4_FIXMAP_END_LABEL -2
#define REISER4_FIXMAP_NEXT_LABEL -1

/* This is diskmap table, it's entries must be sorted ascending first in label
   order, then in parameter order.  End of table is marked with label
   REISER4_FIXMAP_END_LABEL label REISER4_FIXMAP_NEXT_LABEL means that value
   in this row contains disk block of next diskmap in diskmaps chain */
struct reiser4_diskmap {
	char magic[16];
	struct {
		d32 label;
		d32 parameter;
		d64 value;
	} table[0];
};

int reiser4_get_diskmap_value(u32, u32, u64 *);


#endif
