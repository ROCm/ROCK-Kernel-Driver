/*
 *      Sorted list - linked list with sortkey
 *
 *      $Id: s.sortedlist.h 1.6 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/**
 * compare - compares two arbitrary data items
 * @data1: first data item
 * @data2: second data item
 * @datalen: length of data items in bits
 *
 * datalen is in bits!
 */
int mipv6_bitwise_compare(const void *data1, const void *data2, int datalen);

/**
 * mipv6_slist_add - add an entry to sorted list
 * @head: list_head of the sorted list
 * @data: item to store
 * @datalen: length of data (in bytes)
 * @key: sortkey of item
 *
 * Allocates memory for entry and data
 */
int mipv6_slist_add(struct list_head *head, void *data, int datalen,
		    unsigned long sortkey);

/**
 * mipv6_slist_get_first - get the first data item in the list
 * @head: list_head of the sorted list
 *
 * Returns the actual data item, not copy, so don't kfree it
 */
void *mipv6_slist_get_first(struct list_head *head);

/**
 * mipv6_slist_del_first - delete (and get) the first item in list
 * @head: list_head of the sorted list
 *
 * Remember to kfree the item
 */
void *mipv6_slist_del_first(struct list_head *head);

/**
 * mipv6_slist_del_item - delete entry
 * @head: list_head of the sorted list
 * @data: item to delete
 * @compare: function used for comparing the data items
 *
 * compare function needs to have prototype
 * int (*compare)(const void *data1, const void *data2, int datalen) where
 * datalen is in bits
 */
int mipv6_slist_del_item(struct list_head *head, void *data,
			 int (*compare)(const void *data1, const void *data2,
					int datalen));

/**
 * mipv6_slist_get_first_key - get sortkey of the first item
 * @head: list_head of the sorted list
 */
unsigned long mipv6_slist_get_first_key(struct list_head *head);

/**
 * mipv6_slist_get_key - get sortkey of the data item
 * @head: list_head of the sorted list
 * @data: the item to search for
 * @compare: function used for comparing the data items
 *
 * compare function needs to have prototype
 * int (*compare)(const void *data1, const void *data2, int datalen) where
 * datalen is in bits
 */
unsigned long mipv6_slist_get_key(struct list_head *head, void *data,
				  int (*compare)(const void *data1,
						 const void *data2,
						 int datalen));

/**
 * mipv6_slist_get_data - get the data item identified by sortkey
 * @head: list_head of the sorted list
 * @key: sortkey of the item
 *
 * Returns the actual data item, not copy, so don't kfree it
 */
void *mipv6_slist_get_data(struct list_head *head, unsigned long sortkey);

/**
 * mipv6_slist_modify - modify data item
 * @head: list_head of the sorted list
 * @data: item, whose sortkey is to be modified
 * @datalen: datalen in bytes
 * @new_key: new sortkey
 * @compare: function used for comparing the data items
 *
 * Compies the new data on top of the old one, if compare function returns
 * non-negative. If there's no matching entry, new one will be created.
 * Compare function needs to have prototype
 * int (*compare)(const void *data1, const void *data2, int datalen) where
 * datalen is in bits.
 */
int mipv6_slist_modify(struct list_head *head, void *data, int datalen,
		       unsigned long new_key,
		       int (*compare)(const void *data1, const void *data2,
				      int datalen));

/**
 * mipv6_slist_push_first - move the first entry to place indicated by new_key
 * @head: list_head of the sorted list
 * @new_key: new sortkey
 */
int mipv6_slist_push_first(struct list_head *head, unsigned long new_key);

/**
 * mipv6_slist_for_each - apply func to every item in list
 * @head: list_head of the sorted list
 * @args: args to pass to func
 * @func: function to use
 *
 * function must be of type
 * int (*func)(void *data, void *args, unsigned long sortkey)
 * List iteration will stop once func has been applied to every item
 * or when func returns true
 */
int mipv6_slist_for_each(struct list_head *head, void *args,
			 int (*func)(void *data, void *args,
				     unsigned long sortkey));
