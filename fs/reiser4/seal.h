/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Declaration of seals: "weak" tree pointers. See seal.c for comments. */

#ifndef __SEAL_H__
#define __SEAL_H__

#include "forward.h"
#include "debug.h"
#include "dformat.h"
#include "key.h"
#include "coord.h"

/* for __u?? types */
#include <linux/types.h>

/* seal. See comment at the top of seal.c */
typedef struct seal_s {
	/* version of znode recorder at the time of seal creation */
	__u64 version;
	/* block number of znode attached to this seal */
	reiser4_block_nr block;
#if REISER4_DEBUG
	/* coord this seal is attached to. For debugging. */
	coord_t coord;
	/* key this seal is attached to. For debugging. */
	reiser4_key key;
#endif
} seal_t;

extern void seal_init(seal_t * seal, const coord_t * coord, const reiser4_key * key);
extern void seal_done(seal_t * seal);

extern int seal_is_set(const seal_t * seal);

extern int seal_validate(seal_t * seal,
			 coord_t * coord,
			 const reiser4_key * key,
			 tree_level level,
			 lock_handle * lh, lookup_bias bias, znode_lock_mode mode, znode_lock_request request);

#if REISER4_DEBUG_OUTPUT
extern void print_seal(const char *prefix, const seal_t * seal);
#else
#define print_seal( prefix, seal ) noop
#endif

/* __SEAL_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
