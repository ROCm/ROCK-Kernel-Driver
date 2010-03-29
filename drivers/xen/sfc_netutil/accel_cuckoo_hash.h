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

/*
 * A cuckoo hash table consists of two sub tables.  Each entry can
 * hash to a position in each table.  If, on entry, its position is
 * found to be occupied, the existing element is moved to it's other
 * location.  This recurses until success or a loop is found.  If a
 * loop is found the table is rehashed.
 *
 *  See http://www.it-c.dk/people/pagh/papers/cuckoo-jour.pdf
 */

#ifndef NET_ACCEL_CUCKOO_HASH_H
#define NET_ACCEL_CUCKOO_HASH_H

/*! Type used for hash table keys of ip pairs */
typedef struct {
	u32 local_ip;
	//u32 remote_ip;
	u16 local_port;
	//u16 remote_port;
	/* Technically only 1 bit, but use 16 to make key a round
	   number size */
	u16 proto;
} cuckoo_hash_ip_key;

/*! Type used for hash table keys of mac addresses */
typedef u64 cuckoo_hash_mac_key;

/*! This type is designed to be large enough to hold all supported key
 *  sizes to avoid having to malloc storage for them.
 */
typedef u64 cuckoo_hash_key;

/*! Type used for the values stored in the hash table */
typedef int cuckoo_hash_value;

/*! Type used for the hash used to index the table */
typedef u32 cuckoo_hash;

/*! How long to spend displacing values when adding before giving up
 *  and rehashing */
#define CUCKOO_HASH_MAX_LOOP (hashtab->length)

/*! State of hash table entry */
typedef enum {
	CUCKOO_HASH_STATE_VACANT = 0,
	CUCKOO_HASH_STATE_OCCUPIED 
} cuckoo_hash_state;

/*! An entry in the hash table */
typedef struct {
	cuckoo_hash_state state;
	cuckoo_hash_key key;
	cuckoo_hash_value value;
} cuckoo_hash_entry;

/*! A cuckoo hash table */
typedef struct {
	/*! The length of each table (NB. there are two tables of this
	 *  length) */
	unsigned length; 
	/*! The length of each table in bits */
	unsigned length_bits;
	/*! The length of the key in bytes */ 
	unsigned key_length; 
	/*! The number of entries currently stored in the table */
	unsigned entries;
	/*! Index into table used by cuckoo_hash_iterate */
	unsigned iterate_index; 

	/* parameter of hash functions */
	/*! The "a" parameter of the first hash function */
	cuckoo_hash_key a0; 
	/*! The "a" parameter of the second hash function */
	cuckoo_hash_key a1; 

	/*! The first table */
	cuckoo_hash_entry *table0; 
	/*! The second table */
	cuckoo_hash_entry *table1; 
} cuckoo_hash_table;

/*! Initialise the cuckoo has table 
 *
 * \param hashtab A pointer to an unitialised hash table structure
 * \param length_bits The number of elements in each table equals
 * 2**length_bits
 * \param key_length The length of the key in bytes
 *
 * \return 0 on success, -ENOMEM if it couldn't allocate the tables
 */
extern
int cuckoo_hash_init(cuckoo_hash_table *hashtab, unsigned length_bits,
		     unsigned key_length);


/*! Destroy a hash table
 *
 * \param hashtab A hash table that has previously been passed to a
 * successful call of cuckoo_hash_init()
 */
extern
void cuckoo_hash_destroy(cuckoo_hash_table *hashtab);


/*! Lookup an entry in the hash table 
 *
 * \param hashtab The hash table in which to look.
 * \param key Pointer to a mac address to use as the key
 * \param value On exit set to the value stored if key was present
 *
 * \return 0 if not present in the table, non-zero if it is (and value
 * is set accordingly)
 */
extern
int cuckoo_hash_lookup(cuckoo_hash_table *hashtab,
		       cuckoo_hash_key *key,
		       cuckoo_hash_value *value);

/*! Add an entry to the hash table.  Key must not be a duplicate of
 * anything already in the table.  If this is a risk, see
 * cuckoo_hash_add_check
 *
 * \param hashtab The hash table to add the entry to
 * \param key Pointer to a mac address to use as a key
 * \param value The value to store 
 * \param can_rehash Flag to allow the add function to rehash the
 * table if necessary
 *
 * \return 0 on success, non-zero on failure.  -ENOSPC means it just
 * couldn't find anywhere to put it - this is bad and probably means
 * an entry has been dropped on the floor (but the entry you just
 * tried to add may now be included)
 */
extern
int cuckoo_hash_add(cuckoo_hash_table *hashtab,
		    cuckoo_hash_key *key, 
		    cuckoo_hash_value value,
		    int can_rehash);

/*! Same as cuckoo_hash_add but first checks to ensure entry is not
 * already there
 * \return -EBUSY if already there
 */

extern
int cuckoo_hash_add_check(cuckoo_hash_table *hashtab,
			  cuckoo_hash_key *key, 
			  cuckoo_hash_value value,
			  int can_rehash);
/*! Remove an entry from the table 
 *
 * \param hashtab The hash table to remove the entry from
 * \param key The key that was used to previously add the entry
 *
 * \return 0 on success, -EINVAL if the entry couldn't be found 
 */
extern
int cuckoo_hash_remove(cuckoo_hash_table *hashtab, cuckoo_hash_key *key);


/*! Helper for those using mac addresses to convert to a key for the
 *  hash table
 */
static inline cuckoo_hash_mac_key cuckoo_mac_to_key(const u8 *mac)
{
	return (cuckoo_hash_mac_key)(mac[0])
		| (cuckoo_hash_mac_key)(mac[1]) << 8
		| (cuckoo_hash_mac_key)(mac[2]) << 16
		| (cuckoo_hash_mac_key)(mac[3]) << 24
		| (cuckoo_hash_mac_key)(mac[4]) << 32
		| (cuckoo_hash_mac_key)(mac[5]) << 40;
}


/*! Update an entry already in the hash table to take a new value 
 *
 * \param hashtab The hash table to add the entry to
 * \param key Pointer to a mac address to use as a key
 * \param value The value to store 
 *
 * \return 0 on success, non-zero on failure. 
 */
int cuckoo_hash_update(cuckoo_hash_table *hashtab, cuckoo_hash_key *key,
		       cuckoo_hash_value value);


/*! Go through the hash table and return all used entries (one per call)
 *
 * \param hashtab The hash table to iterate over 
 * \param key Pointer to a key to take the returned key
 * \param value Pointer to a value to take the returned value
 *
 * \return 0 on success (key, value set), non-zero on failure.
 */
int cuckoo_hash_iterate(cuckoo_hash_table *hashtab,
			cuckoo_hash_key *key, cuckoo_hash_value *value);
void cuckoo_hash_iterate_reset(cuckoo_hash_table *hashtab);

/* debug, not compiled by default */
void cuckoo_hash_valid(cuckoo_hash_table *hashtab);
void cuckoo_hash_dump(cuckoo_hash_table *hashtab);

#endif /* NET_ACCEL_CUCKOO_HASH_H */
