/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: khash.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _KHASH_H
#define _KHASH_H

#include <ib_legacy_types.h>

#define HASH_KEY_SIZE 40

/*
 * table types
 */
typedef struct DAPL_HASH_TABLE_STRUCT DAPL_HASH_TABLE_STRUCT,
    *DAPL_HASH_TABLE;
typedef struct DAPL_HASH_BUCKET_STRUCT DAPL_HASH_BUCKET_STRUCT,
    *DAPL_HASH_BUCKET;
/*
 * traversal function for dumping table. Return value is the number of
 * bytes written into buffer, a negative return means that data will not
 * fit into max_size bytes.
 */
typedef tINT32 (* DAPL_HASH_DUMP_FUNC) (tSTR    buffer,
					tINT32  max_size,
					char    *key,
					tUINT32 value);
/*
 * A simple hash table.
 */
struct DAPL_HASH_BUCKET_STRUCT {
    DAPL_HASH_BUCKET  next;               /* next bucket in chain */
    DAPL_HASH_BUCKET  *p_next;            /* previous next pointer in the chain. */
    char              key[HASH_KEY_SIZE]; /* hash key */
    unsigned long     value;              /* hash value */
};

struct DAPL_HASH_TABLE_STRUCT {
    tINT32 size;           /* size of hash table */
    tINT32 num_entries;    /* number of entries in hash table */
    tUINT64 mask;          /* mask used for computing the hash */
    tINT32 num_collisions; /* number of collisions (useful for stats) */
    DAPL_HASH_BUCKET *buckets; /* room for pointers to entries */
};

DAPL_HASH_TABLE DaplHashTableCreate(
				    tINT32 size
				    );

tINT32 DaplHashTableDestroy(
			    DAPL_HASH_TABLE table
			    );

tINT32 DaplHashTableInsert(
			   DAPL_HASH_TABLE table,
			   char    *key,
			   unsigned long value
			   );

tINT32 DaplHashTableLookup(
			   DAPL_HASH_TABLE table,
			   char    *key,
			   unsigned long *value
			   );

tINT32 DaplHashTableRemove(
			   DAPL_HASH_TABLE table,
			   char    *key,
			   unsigned long *value
			   );

tINT32 DaplHashTableRemoveValue(
				DAPL_HASH_TABLE table,
				unsigned long value
				);

tINT32 DaplHashTableDump(
			 DAPL_HASH_TABLE table,
			 DAPL_HASH_DUMP_FUNC dfunc,
			 tSTR    buffer,
			 tINT32  max_size,
			 tINT32  start,
			 tINT32 *end
			 );


#endif /* _KHASH_H */
