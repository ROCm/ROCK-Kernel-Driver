/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#include <linux/types.h> /* needed for linux/random.h */
#include <linux/random.h>

#include "accel_cuckoo_hash.h"
#include "accel_util.h"

static inline int cuckoo_hash_key_compare(cuckoo_hash_table *hashtab,
					  cuckoo_hash_key *key1, 
					  cuckoo_hash_key *key2)
{
	return !memcmp(key1, key2, hashtab->key_length);
}


static inline void cuckoo_hash_key_set(cuckoo_hash_key *key1, 
				       cuckoo_hash_key *key2)
{
	*key1 = *key2;
}


/*
 * Sets hash function parameters.  Chooses "a" to be odd, 0 < a < 2^w
 * where w is the length of the key
 */
static void set_hash_parameters(cuckoo_hash_table *hashtab)
{
 again:
	hashtab->a0 = hashtab->a1 = 0;

	/* Make sure random */
	get_random_bytes(&hashtab->a0, hashtab->key_length);
	get_random_bytes(&hashtab->a1, hashtab->key_length);

	/* Make sure odd */
	hashtab->a0 |= 1;
	hashtab->a1 |= 1;

	/* Being different is good */
	if (hashtab->a0 != hashtab->a1)
		return;
		       
	goto again;
}

int cuckoo_hash_init(cuckoo_hash_table *hashtab, unsigned length_bits,
		     unsigned key_length)
{
	char *table_mem;
	unsigned length = 1 << length_bits;

	BUG_ON(length_bits >= sizeof(unsigned) * 8);
	BUG_ON(key_length > sizeof(cuckoo_hash_key));

	table_mem = kmalloc(sizeof(cuckoo_hash_entry) * 2 * length, GFP_KERNEL);

	if (table_mem == NULL)
		return -ENOMEM;

	hashtab->length = length;
	hashtab->length_bits = length_bits;
	hashtab->key_length = key_length;
	hashtab->entries = 0;

	hashtab->table0 = (cuckoo_hash_entry *)table_mem;
	hashtab->table1 = (cuckoo_hash_entry *)
		(table_mem + length * sizeof(cuckoo_hash_entry));

	set_hash_parameters(hashtab);

	/* Zero the table */
	memset(hashtab->table0, 0, length * 2 * sizeof(cuckoo_hash_entry));

	return 0;
}
EXPORT_SYMBOL_GPL(cuckoo_hash_init);

void cuckoo_hash_destroy(cuckoo_hash_table *hashtab)
{
	if (hashtab->table0 != NULL)
		kfree(hashtab->table0);
}

EXPORT_SYMBOL_GPL(cuckoo_hash_destroy);

/* 
 * This computes sizeof(cuckoo_hash) bits of hash, not all will be
 * necessarily used, but the hash function throws away any that
 * aren't
 */ 
static inline void cuckoo_compute_hash_helper(cuckoo_hash_table *hashtab,
					      cuckoo_hash_key *a,
					      cuckoo_hash_key *x,
					      cuckoo_hash *result) 
{
	u64 multiply_result = 0, a_temp, x_temp;
	u32 carry = 0;
	u32 *a_words;
	u32 *x_words;
	int i;

	/*
	 * As the mod and div operations in the function effectively
	 * reduce and shift the bits of the product down to just the
	 * third word, we need only compute that and return it as a
	 * result.
	 *
	 * Do enough long multiplication to get the word we need
	 */

	/* This assumes things about the sizes of the key and hash */
	BUG_ON(hashtab->key_length % sizeof(u32) != 0);
	BUG_ON(sizeof(cuckoo_hash) != sizeof(u32));

	a_words = (u32 *)a;
	x_words = (u32 *)x;

	for (i = 0; i < hashtab->key_length / sizeof(u32); i++) {
		a_temp = a_words[i];
		x_temp = x_words[i];
		
		multiply_result = (a_temp * x_temp) + carry;
		carry = (multiply_result >> 32) & 0xffffffff;
	}
	
	*result = multiply_result & 0xffffffff;
}


/*
 * Want to implement (ax mod 2^w) div 2^(w-q) for odd a, 0 < a < 2^w;
 * w is the length of the key, q is the length of the hash, I think.
 * See http://www.it-c.dk/people/pagh/papers/cuckoo-jour.pdf 
 */
static cuckoo_hash cuckoo_compute_hash(cuckoo_hash_table *hashtab, 
				       cuckoo_hash_key *key, 
				       cuckoo_hash_key *a)
{
	unsigned q = hashtab->length_bits;
	unsigned shift = 32 - q;
	unsigned mask = ((1 << q) - 1) << shift;
	cuckoo_hash hash;

	cuckoo_compute_hash_helper(hashtab, a, key, &hash);

	/* 
	 * Take the top few bits to get the right length for this
	 * hash table 
	 */
	hash = (hash & mask) >> shift;

	BUG_ON(hash >= hashtab->length);

	return hash;
}


static int cuckoo_hash_lookup0(cuckoo_hash_table *hashtab,
			       cuckoo_hash_key *key,
			       cuckoo_hash_value *value)
{
	cuckoo_hash hash = cuckoo_compute_hash(hashtab, key, &hashtab->a0);

	if ((hashtab->table0[hash].state == CUCKOO_HASH_STATE_OCCUPIED)
	    && cuckoo_hash_key_compare(hashtab, &(hashtab->table0[hash].key),
				       key)) {
		*value = hashtab->table0[hash].value;
		return 1;
	}

	return 0;
}

static int cuckoo_hash_lookup1(cuckoo_hash_table *hashtab,
			       cuckoo_hash_key *key,
			       cuckoo_hash_value *value)
{
	cuckoo_hash hash = cuckoo_compute_hash(hashtab, key, &hashtab->a1);

	if ((hashtab->table1[hash].state == CUCKOO_HASH_STATE_OCCUPIED)
	    && cuckoo_hash_key_compare(hashtab, &(hashtab->table1[hash].key),
				       key)) {
		*value = hashtab->table1[hash].value;
		return 1;
	}

	return 0;
}


int cuckoo_hash_lookup(cuckoo_hash_table *hashtab, cuckoo_hash_key *key,
		       cuckoo_hash_value *value)
{
	return cuckoo_hash_lookup0(hashtab, key, value)
		|| cuckoo_hash_lookup1(hashtab, key, value);
}
EXPORT_SYMBOL_GPL(cuckoo_hash_lookup);


/* Transfer any active entries from "old_table" into hashtab */
static int cuckoo_hash_transfer_entries(cuckoo_hash_table *hashtab,
					cuckoo_hash_entry *old_table,
					unsigned capacity)
{
	int i, rc;
	cuckoo_hash_entry *entry;

	hashtab->entries = 0;

	for (i = 0; i < capacity; i++) {
		entry = &old_table[i];
		if (entry->state == CUCKOO_HASH_STATE_OCCUPIED) {
			rc = cuckoo_hash_add(hashtab, &(entry->key), 
					     entry->value, 0);
			if (rc != 0) {
				return rc;
			}
		}
	}
  
	return 0;
}


int cuckoo_hash_rehash(cuckoo_hash_table *hashtab)
{
	cuckoo_hash_entry *new_table;
	cuckoo_hash_table old_hashtab;
	int resize = 0, rc, rehash_count;

	/*
	 * Store old tables so we can access the existing values and
	 * copy across
	 */
	memcpy(&old_hashtab, hashtab, sizeof(cuckoo_hash_table));

	/* resize if hashtable is more than half full */
	if (old_hashtab.entries > old_hashtab.length &&
	    old_hashtab.length_bits < 32)
		resize = 1;

 resize:
	if (resize) {
		new_table = kmalloc(sizeof(cuckoo_hash_entry) * 4 * hashtab->length,
				    GFP_ATOMIC);
		if (new_table == NULL) {
			rc = -ENOMEM;
			goto err;
		}

		hashtab->length = 2 * hashtab->length;
		hashtab->length_bits++;
	} else {
		new_table = kmalloc(sizeof(cuckoo_hash_entry) * 2 * hashtab->length,
				    GFP_ATOMIC);
		if (new_table == NULL) {
			rc = -ENOMEM;
			goto err;
		}
	}
    
	/*
	 * Point hashtab to new memory region so we can try to
	 * construct new table
	 */
	hashtab->table0 = new_table;
	hashtab->table1 = (cuckoo_hash_entry *)
		((char *)new_table + hashtab->length * sizeof(cuckoo_hash_entry));
  
	rehash_count = 0;

 again:
	/* Zero the new tables */
	memset(new_table, 0, hashtab->length * 2 * sizeof(cuckoo_hash_entry));

	/* Choose new parameters for the hash functions */
	set_hash_parameters(hashtab);

	/*
	 * Multiply old_table_length by 2 as the length refers to each
	 * table, and there are two of them.  This assumes that they
	 * are arranged sequentially in memory, so assert it 
	 */
	BUG_ON(((char *)old_hashtab.table1) != 
	       ((char *)old_hashtab.table0 + old_hashtab.length
		* sizeof(cuckoo_hash_entry)));
	rc = cuckoo_hash_transfer_entries(hashtab, old_hashtab.table0, 
					  old_hashtab.length * 2);
	if (rc < 0) {
		/* Problem */
		if (rc == -ENOSPC) {
			++rehash_count;
			if (rehash_count < CUCKOO_HASH_MAX_LOOP) {
				/*
				 * Wanted to rehash, but rather than
				 * recurse we can just do it here
				 */
				goto again;
			} else {
				/*
				 * Didn't manage to rehash, so let's
				 * go up a size (if we haven't already
				 * and there's space)
				 */
				if (!resize && hashtab->length_bits < 32) {
					resize = 1;
					kfree(new_table);
					goto resize;
				}
				else
					goto err;
			}
		}
		else
			goto err;
	}

	/* Success, I think.  Free up the old table */
	kfree(old_hashtab.table0);
  
	/* We should have put all the entries from old table in the new one */
	BUG_ON(hashtab->entries != old_hashtab.entries);

	return 0;
 err:
	EPRINTK("%s: Rehash failed, giving up\n", __FUNCTION__);
	/* Some other error, give up, at least restore table to how it was */
	memcpy(hashtab, &old_hashtab, sizeof(cuckoo_hash_table));
	if (new_table)
		kfree(new_table);
	return rc;
}
EXPORT_SYMBOL_GPL(cuckoo_hash_rehash);


static int 
cuckoo_hash_insert_or_displace(cuckoo_hash_entry *table, unsigned hash,
			       cuckoo_hash_key *key, 
			       cuckoo_hash_value value,
			       cuckoo_hash_key *displaced_key, 
			       cuckoo_hash_value *displaced_value)
{
	if (table[hash].state == CUCKOO_HASH_STATE_VACANT) {
		cuckoo_hash_key_set(&(table[hash].key), key);
		table[hash].value = value;
		table[hash].state = CUCKOO_HASH_STATE_OCCUPIED;

		return 1;
	} else {
		cuckoo_hash_key_set(displaced_key, &(table[hash].key));
		*displaced_value = table[hash].value;
		cuckoo_hash_key_set(&(table[hash].key), key);
		table[hash].value = value;

		return 0;
	}
}


int cuckoo_hash_add(cuckoo_hash_table *hashtab, cuckoo_hash_key *key,
		     cuckoo_hash_value value, int can_rehash)
{
	cuckoo_hash hash0, hash1;
	int i, rc;
	cuckoo_hash_key key1, key2;

	cuckoo_hash_key_set(&key1, key);

 again:
	i = 0;
	do {
		hash0 = cuckoo_compute_hash(hashtab, &key1, &hashtab->a0);
		if (cuckoo_hash_insert_or_displace(hashtab->table0, hash0, 
						   &key1, value, &key2,
						   &value)) {
			/* Success */
			hashtab->entries++;
			return 0;
		}
	
		hash1 = cuckoo_compute_hash(hashtab, &key2, &hashtab->a1);
		if (cuckoo_hash_insert_or_displace(hashtab->table1, hash1,
						   &key2, value, &key1,
						   &value)) {
			/* Success */
			hashtab->entries++;
			return 0;
		}
	} while (++i < CUCKOO_HASH_MAX_LOOP);

	if (can_rehash) {
		if ((rc = cuckoo_hash_rehash(hashtab)) < 0) {
			/*
			 * Give up - this will drop whichever
			 * key/value pair we have currently displaced
			 * on the floor
			 */
			return rc;
		}
		goto again;
	}
  
	EPRINTK("%s: failed hash add\n", __FUNCTION__);
	/*
	 * Couldn't do it - bad as we've now removed some random thing
	 * from the table, and will just drop it on the floor.  Better
	 * would be to somehow revert the table to the state it was in
	 * at the start
	 */
	return -ENOSPC;
}
EXPORT_SYMBOL_GPL(cuckoo_hash_add);


int cuckoo_hash_add_check(cuckoo_hash_table *hashtab,
			  cuckoo_hash_key *key, cuckoo_hash_value value,
			  int can_rehash)
{
	int stored_value;

	if (cuckoo_hash_lookup(hashtab, key, &stored_value))
		return -EBUSY;

	return cuckoo_hash_add(hashtab, key, value, can_rehash);
}
EXPORT_SYMBOL_GPL(cuckoo_hash_add_check);


int cuckoo_hash_remove(cuckoo_hash_table *hashtab, cuckoo_hash_key *key)
{
	cuckoo_hash hash;

	hash = cuckoo_compute_hash(hashtab, key, &hashtab->a0);
	if ((hashtab->table0[hash].state == CUCKOO_HASH_STATE_OCCUPIED) &&
	    cuckoo_hash_key_compare(hashtab, &(hashtab->table0[hash].key),
				    key)) {
		hashtab->table0[hash].state = CUCKOO_HASH_STATE_VACANT;
		hashtab->entries--;
		return 0;
	}
  
	hash = cuckoo_compute_hash(hashtab, key, &hashtab->a1);
	if ((hashtab->table1[hash].state == CUCKOO_HASH_STATE_OCCUPIED) &&
	    cuckoo_hash_key_compare(hashtab, &(hashtab->table1[hash].key),
				    key)) {
		hashtab->table1[hash].state = CUCKOO_HASH_STATE_VACANT;
		hashtab->entries--;
		return 0;
	}
 
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(cuckoo_hash_remove);


int cuckoo_hash_update(cuckoo_hash_table *hashtab, cuckoo_hash_key *key,
		       cuckoo_hash_value value)
{
	cuckoo_hash hash;

	hash = cuckoo_compute_hash(hashtab, key, &hashtab->a0);
	if ((hashtab->table0[hash].state == CUCKOO_HASH_STATE_OCCUPIED) &&
	    cuckoo_hash_key_compare(hashtab, &(hashtab->table0[hash].key),
				    key)) {
		hashtab->table0[hash].value = value;
		return 0;
	}

	hash = cuckoo_compute_hash(hashtab, key, &hashtab->a1);
	if ((hashtab->table1[hash].state == CUCKOO_HASH_STATE_OCCUPIED) &&
	    cuckoo_hash_key_compare(hashtab, &(hashtab->table1[hash].key),
				    key)) {
		hashtab->table1[hash].value = value;
		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(cuckoo_hash_update);


void cuckoo_hash_iterate_reset(cuckoo_hash_table *hashtab)
{
	hashtab->iterate_index = 0;
}
EXPORT_SYMBOL_GPL(cuckoo_hash_iterate_reset);


int cuckoo_hash_iterate(cuckoo_hash_table *hashtab,
			cuckoo_hash_key *key, cuckoo_hash_value *value)
{
	unsigned index;

	while (hashtab->iterate_index < hashtab->length) {
		index = hashtab->iterate_index;
		++hashtab->iterate_index;
		if (hashtab->table0[index].state == CUCKOO_HASH_STATE_OCCUPIED) {
			*key = hashtab->table0[index].key;
			*value = hashtab->table0[index].value;
			return 0;
		}
	}

	while (hashtab->iterate_index >= hashtab->length &&
	       hashtab->iterate_index < hashtab->length * 2) {
		index = hashtab->iterate_index - hashtab->length;
		++hashtab->iterate_index;		
		if (hashtab->table1[index].state == CUCKOO_HASH_STATE_OCCUPIED) {
			*key = hashtab->table1[index].key;
			*value = hashtab->table1[index].value;
			return 0;
		}
	}

	return -ENOSPC;
}
EXPORT_SYMBOL_GPL(cuckoo_hash_iterate);


#if 0
void cuckoo_hash_valid(cuckoo_hash_table *hashtab)
{
	int i, entry_count = 0;

	for (i=0; i < hashtab->length; i++) {
		EPRINTK_ON(hashtab->table0[i].state != CUCKOO_HASH_STATE_VACANT &&
			   hashtab->table0[i].state != CUCKOO_HASH_STATE_OCCUPIED);
		if (hashtab->table0[i].state == CUCKOO_HASH_STATE_OCCUPIED)
			entry_count++;
		EPRINTK_ON(hashtab->table1[i].state != CUCKOO_HASH_STATE_VACANT &&
			   hashtab->table1[i].state != CUCKOO_HASH_STATE_OCCUPIED);
		if (hashtab->table1[i].state == CUCKOO_HASH_STATE_OCCUPIED)
			entry_count++;	
	}
	
	if (entry_count != hashtab->entries) {
		EPRINTK("%s: bad count\n", __FUNCTION__);
		cuckoo_hash_dump(hashtab);
		return;
	}

	for (i=0; i< hashtab->length; i++) {
		if (hashtab->table0[i].state == CUCKOO_HASH_STATE_OCCUPIED)
			if (i != cuckoo_compute_hash(hashtab, 
						     &hashtab->table0[i].key, 
						     &hashtab->a0)) {
				EPRINTK("%s: Bad key table 0 index %d\n",
					__FUNCTION__, i);
				cuckoo_hash_dump(hashtab);
				return;
			}
		if (hashtab->table1[i].state == CUCKOO_HASH_STATE_OCCUPIED)
			if (i != cuckoo_compute_hash(hashtab, 
						     &hashtab->table1[i].key, 
						     &hashtab->a1)) {
				EPRINTK("%s: Bad key table 1 index %d\n",
					__FUNCTION__, i);
				cuckoo_hash_dump(hashtab);
				return;
			}
	}

}
EXPORT_SYMBOL_GPL(cuckoo_hash_valid);


void cuckoo_hash_dump(cuckoo_hash_table *hashtab)
{
	int i, entry_count;

	entry_count = 0;
	for (i=0; i < hashtab->length; i++) {
		EPRINTK_ON(hashtab->table0[i].state != CUCKOO_HASH_STATE_VACANT &&
			   hashtab->table0[i].state != CUCKOO_HASH_STATE_OCCUPIED);
		if (hashtab->table0[i].state == CUCKOO_HASH_STATE_OCCUPIED)
			entry_count++;
		EPRINTK_ON(hashtab->table1[i].state != CUCKOO_HASH_STATE_VACANT &&
			   hashtab->table1[i].state != CUCKOO_HASH_STATE_OCCUPIED);
		if (hashtab->table1[i].state == CUCKOO_HASH_STATE_OCCUPIED)
			entry_count++;	
	}

	EPRINTK("======================\n");
	EPRINTK("Cuckoo hash table dump\n");
	EPRINTK("======================\n");
	EPRINTK("length: %d; length_bits: %d; key_length: %d\n", hashtab->length,
		hashtab->length_bits, hashtab->key_length);
	EPRINTK("Recorded entries: %d\n", hashtab->entries);
	EPRINTK("Counted entries: %d\n", entry_count);
	EPRINTK("a0: %llx; a1: %llx\n", hashtab->a0, hashtab->a1);
	EPRINTK("-----------------------------------------\n");
	EPRINTK("Index  Occupied  Key  Value Index0 Index1\n");
	EPRINTK("-----------------------------------------\n");		
	for (i=0; i< hashtab->length; i++) {
		if (hashtab->table0[i].state == CUCKOO_HASH_STATE_OCCUPIED)
		EPRINTK("%d %d %llx %d %d %d\n", i,
			hashtab->table0[i].state == CUCKOO_HASH_STATE_OCCUPIED,
			hashtab->table0[i].key, hashtab->table0[i].value,
			cuckoo_compute_hash(hashtab, &hashtab->table0[i].key, 
					    &hashtab->a0),
			cuckoo_compute_hash(hashtab, &hashtab->table0[i].key, 
					    &hashtab->a1));
		else
		EPRINTK("%d %d - - - -\n", i,
			hashtab->table0[i].state == CUCKOO_HASH_STATE_OCCUPIED);
			
	}
	EPRINTK("-----------------------------------------\n");
	EPRINTK("Index  Occupied  Key  Value Index0 Index1\n");
	EPRINTK("-----------------------------------------\n");
	for (i=0; i< hashtab->length; i++) {
		if (hashtab->table1[i].state == CUCKOO_HASH_STATE_OCCUPIED)
		EPRINTK("%d %d %llx %d %d %d\n", i,
			hashtab->table1[i].state == CUCKOO_HASH_STATE_OCCUPIED,
			hashtab->table1[i].key, hashtab->table1[i].value,
			cuckoo_compute_hash(hashtab, &hashtab->table1[i].key, 
					    &hashtab->a0),
			cuckoo_compute_hash(hashtab, &hashtab->table1[i].key, 
					    &hashtab->a1));
		else
		EPRINTK("%d %d - - - -\n", i,
			hashtab->table1[i].state == CUCKOO_HASH_STATE_OCCUPIED);
	} 
	EPRINTK("======================\n");
}
EXPORT_SYMBOL_GPL(cuckoo_hash_dump);
#endif
