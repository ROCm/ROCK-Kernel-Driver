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

  $Id: khash.c 35 2004-04-09 05:34:32Z roland $
*/

#include <linux/config.h>

#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>


#include "ib_legacy_types.h"
#include "ts_kernel_trace.h"
#include "khash.h"

#define DAPL_EXPECT(expr)                                         \
{                                                                 \
  if (!(expr)) {                                                  \
    TS_REPORT_WARN(MOD_UDAPL,                                     \
             "Internal error check <%s> failed.", #expr);         \
  } /* if */                                                      \
} /* DAPL_EXPECT */
#define DAPL_CHECK_NULL(value, result) \
        if (NULL == (value)) return (result);
#define DAPL_CHECK_LT(value, bound, result) \
        if ((bound) > (value)) return(result);

static inline tUINT32 _DaplKeyHash(DAPL_HASH_TABLE table, char *key)
{
    tUINT32 i;
    tUINT32 s = 0;

    for (i = 0; i < HASH_KEY_SIZE; i++)
	s += key[i];

    return (s & table->mask);
}

/* ------------------------------------------------------------------------- */
/* Static functions for simple hash table bucket managment                   */
/* ------------------------------------------------------------------------- */
static kmem_cache_t *_bucket_cache = NULL;
static tINT32        _use_count    = 0;

/* ========================================================================= */
/*.._DaplBucketCacheCreate -- create, if necessary, a bucket cache           */
static tINT32 _DaplBucketCacheCreate
(
 void
 )
{
    if (NULL == _bucket_cache) {
	DAPL_EXPECT((0 == _use_count));

	_bucket_cache = kmem_cache_create("DaplBucket",
					  sizeof(DAPL_HASH_BUCKET_STRUCT),
					  0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (NULL == _bucket_cache) {
	    return -ENOMEM;
	} /* if */
    } /* if */

    _use_count++;

    return 0;
} /* _DaplBucketCacheCreate */

/* ========================================================================= */
/*.._DaplBucketCacheDestroy -- destroy, if necessary, the bucket cache       */
static tINT32 _DaplBucketCacheDestroy
(
 void
 )
{
    DAPL_CHECK_NULL(_bucket_cache, -EINVAL);
    DAPL_CHECK_LT(_use_count, 1, -EINVAL);

    _use_count--;

    if (0 == _use_count) {
	kmem_cache_destroy(_bucket_cache);
	_bucket_cache = NULL;
    } /* if */

    return 0;
} /* _DaplBucketCacheDestroy */

/* ========================================================================= */
/*.._DaplBucketCacheGet -- get the bucket cache                              */
static kmem_cache_t * _DaplBucketCacheGet
(
 void
 )
{
    return _bucket_cache;
} /* _DaplBucketCacheGet */

/* --------------------------------------------------------------------- */
/* Simple hash table API. (insert, remove, lookup, create, destroy)      */
/* --------------------------------------------------------------------- */

/* ========================================================================= */
/*..DaplHashTableCreate -- create the simple hash table                      */
DAPL_HASH_TABLE DaplHashTableCreate
(
 tINT32 size
 )
{
    DAPL_HASH_TABLE table;
    tINT32 result;
    /*
     * round size down to a multiple of a bucket pointer.
     */
    size -= size % sizeof(DAPL_HASH_BUCKET);
    DAPL_CHECK_LT(size, sizeof(DAPL_HASH_BUCKET), NULL);

    table = kmalloc(sizeof(DAPL_HASH_TABLE_STRUCT), GFP_KERNEL);
    if (NULL == table) {
	TS_REPORT_FATAL(MOD_UDAPL, "Cannot allocate hash table memory");
	return NULL;
    } /* if */
    /*
     * kmalloc since the size should remain pretty small, for such a special
     * purpose table.
     */
    table->buckets = kmalloc(size, GFP_KERNEL);
    if (NULL == table->buckets) {
	TS_REPORT_FATAL(MOD_UDAPL,
			"Cannot allocate memory for hash table bucket array");
	kfree(table);
	return NULL;
    } /* if */
    /*
     * create the bucket cache this table will be using.
     */
    result = _DaplBucketCacheCreate();
    if (0 > result) {
	TS_REPORT_FATAL(MOD_UDAPL,
		 "Cannot allocate memory for hash table bucket cache");
	kfree(table->buckets);
	kfree(table);
	return NULL;
    } /* if */

    memset(table->buckets, 0, size);
    table->num_entries = 0;
    table->num_collisions = 0;
    table->size = size/sizeof(DAPL_HASH_BUCKET);
    table->mask = table->size - 1;

    return table;
} /* DaplHashTableCreate */

/* ========================================================================= */
/*..DaplHashTableDestroy -- destroy the simple hash table                    */
tINT32 DaplHashTableDestroy
(
 DAPL_HASH_TABLE table
 )
{
    DAPL_HASH_BUCKET trav_bucket;
    tINT32 counter;

    DAPL_CHECK_NULL(table, -EINVAL);
    /*
     * traverse and empty
     */
    for (counter = 0; counter < table->size; counter++) {
	while (NULL != (trav_bucket = table->buckets[counter])) {
	    table->buckets[counter] = trav_bucket->next;
	    kmem_cache_free(_DaplBucketCacheGet(), trav_bucket);
	} /* while */
    } /* for */
    /*
     * free table
     */
    (void)_DaplBucketCacheDestroy();
    kfree(table->buckets);
    kfree(table);

    return 0;
} /* DaplHashTableDestroy */

/* ========================================================================= */
/*..DaplHashTableInsert -- insert a value into the hash table                */
tINT32 DaplHashTableInsert
(
 DAPL_HASH_TABLE table,
 char            *key,
 unsigned long         value
)
{
    DAPL_HASH_BUCKET bucket;
    tUINT32 offset;

    DAPL_CHECK_NULL(_DaplBucketCacheGet(), -EINVAL);
    DAPL_CHECK_NULL(table, -EINVAL);
    /*
     * lookup the bucket chain
     */
    offset = _DaplKeyHash(table, key);
    bucket = table->buckets[offset];
    /*
     * search for correct bucket
     */
    while (NULL != bucket &&
	   memcmp(key, bucket->key, HASH_KEY_SIZE)) {
	bucket = bucket->next;
    } /* while */

    if (NULL != bucket) {
	return -EEXIST;
    } /* if */
    else {
	bucket = kmem_cache_alloc(_DaplBucketCacheGet(), SLAB_KERNEL);
	memcpy(bucket->key, key, HASH_KEY_SIZE);
	bucket->value = value;

	bucket->next = table->buckets[offset];
	table->buckets[offset] = bucket;
	bucket->p_next = &table->buckets[offset];

	if (NULL != bucket->next) {
	    bucket->next->p_next = &bucket->next;
	} /* if */

	table->num_entries++;

	if (NULL != bucket->next) {
	    table->num_collisions++;
	} /* if */
    } /* if */

    return 0;
} /* DaplHashTableInsert */

/* ========================================================================= */
/*..DaplHashTableLookup -- lookup a value using it's key in the table        */
tINT32 DaplHashTableLookup
(
 DAPL_HASH_TABLE table,
 char            *key,
 unsigned long   *value
)
{
    DAPL_HASH_BUCKET bucket;
    tUINT32 offset;

    DAPL_CHECK_NULL(table, -EINVAL);
    /*
     * lookup the bucket chain
     */
    offset = _DaplKeyHash(table, key);
    bucket = table->buckets[offset];
    /*
     * search for correct bucket
     */
    while (NULL != bucket &&
	   memcmp(key, bucket->key, HASH_KEY_SIZE)) {
	bucket = bucket->next;
    } /* while */

    if (NULL == bucket) {
	*value = 0;
	return -ENOENT;
    } /* if */

    *value = bucket->value;
    return 0;
} /* DaplHashTableLookup */

/* ========================================================================= */
/*..DaplHashTableRemove -- remove a src indexed entry from the table         */
tINT32 DaplHashTableRemove
(
 DAPL_HASH_TABLE table,
 char            *key,
 unsigned long   *value
)
{
    DAPL_HASH_BUCKET bucket;
    tUINT32 offset;

    DAPL_CHECK_NULL(_DaplBucketCacheGet(), -EINVAL);
    DAPL_CHECK_NULL(table, -EINVAL);
    /*
     * lookup the bucket chain
     */
    offset = _DaplKeyHash(table, key);
    bucket = table->buckets[offset];
    /*
     * search for correct bucket
     */
    while (NULL != bucket &&
	   memcmp(key, bucket->key, HASH_KEY_SIZE)) {
	bucket = bucket->next;
    } /* while */

    if (NULL == bucket) {

	return -ENOENT;
    } /* if */

    if (NULL != bucket->next) {
	bucket->next->p_next = bucket->p_next;
    } /* if */

    *(bucket->p_next) = bucket->next;

    if (NULL != value) {
	*value = bucket->value;
    } /* if */

    kmem_cache_free(_DaplBucketCacheGet(), bucket);

    return 0;
} /* DaplHashTableRemove */

/* ========================================================================= */
/*..DaplHashTableRemoveValue -- remove all entries with a given value        */
tINT32 DaplHashTableRemoveValue
(
 DAPL_HASH_TABLE table,
 unsigned long   value
)
{
    DAPL_HASH_BUCKET next;
    DAPL_HASH_BUCKET bucket;
    tINT32 offset;

    DAPL_CHECK_NULL(_DaplBucketCacheGet(), -EINVAL);
    DAPL_CHECK_NULL(table, -EINVAL);
#if 0
    /*
     * first remove the value where it's used as an index.
     */
    (void)DaplHashTableRemove(table, value, NULL);
#endif
    /*
     * next remove the valuewhere ever it's a value. This is slow,
     * the hash table isn't built for it, so we traverse the whole table.
     * It's OK since this request is infrequent.
     */
    for (offset = 0; offset < table->size; offset++) {
	bucket = table->buckets[offset];
	while (NULL != bucket) {
	    if (value == bucket->value) {
		next = bucket->next;
		if (NULL != next) {
		    next->p_next = bucket->p_next;
	} /* if */
		*(bucket->p_next) = next;
		kmem_cache_free(_DaplBucketCacheGet(), bucket);
		bucket = next;
	    } /* if */
	    else {
		bucket = bucket->next;
	    } /* else */
	} /* while */
    } /* for */

    return 0;
} /* DaplHashTableRemoveId */

/* ========================================================================= */
/*..DaplHashTableDump -- dump the contents of the hash table                 */
tINT32 DaplHashTableDump
(
 DAPL_HASH_TABLE table,
 DAPL_HASH_DUMP_FUNC dfunc,
 tSTR    buffer,
 tINT32  max_size,
 tINT32  start,
 tINT32 *end
)
{
    DAPL_HASH_BUCKET bucket;
    tINT32 offset = 0;
    tINT32 elements;
    tINT32 counter;
    tINT32 result;

    DAPL_CHECK_NULL(table, -EINVAL);
    DAPL_CHECK_NULL(buffer, -EINVAL);
    DAPL_CHECK_NULL(dfunc, -EINVAL);

    *end = 0;
    /*
     * traverse
     */
    for (counter = 0, elements = 0; counter < table->size; counter++) {
	for (bucket = table->buckets[counter];
	     NULL != bucket;
	     bucket = bucket->next) {
	    if (elements < start) {
		continue;
	    } /* if */
	    result = dfunc((buffer + offset), (max_size - offset),
			   bucket->key, bucket->value);
	    if (0 > result) {
		*end = elements;
		break;
	    } /* if */
	    else {
		offset += result;
		elements++;
	    } /* else */
	} /* for */
    } /* for */

    return offset;
}
