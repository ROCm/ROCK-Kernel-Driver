/**
 * Sorted list - linked list with sortkey.
 *
 * Authors:
 * Jaakko Laine <medved@iki.fi>
 *
 * $Id: s.sortedlist.c 1.11 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <net/ipv6.h>
#include "config.h"

struct mipv6_sorted_list_entry {
	struct list_head list;
	void *data;
	int datalen;
	unsigned long sortkey;
};

/**
 * compare - compares two arbitrary data items
 * @data1: first data item
 * @data2: second data item
 * @datalen: length of data items in bits
 *
 * datalen is in bits!
 */
int mipv6_bitwise_compare(const void *data1, const void *data2, int datalen)
{
	int n = datalen;
	__u8 * ptr1 = (__u8 *)data1;
	__u8 * ptr2 = (__u8 *)data2;
	
	for (; n>=0; n-=8, ptr1++, ptr2++) {
		if (n >= 8) {
			if (*ptr1 != *ptr2)
				return 0;
		} else {
			if ((*ptr1 ^ *ptr2) & ((~0) << (8 - n)))
				return 0;
		}
	}

	return 1;
}

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
		    unsigned long sortkey)
{
	struct list_head *pos;
	struct mipv6_sorted_list_entry *entry, *tmp, *next;

	entry = kmalloc(sizeof(struct mipv6_sorted_list_entry), GFP_ATOMIC);

	if (!entry)
		return -1;

	entry->data = kmalloc(datalen, GFP_ATOMIC);

	if (!entry->data) {
		kfree(entry);
		return -1;
	}

	memcpy(entry->data, data, datalen);
	entry->datalen = datalen;
	entry->sortkey = sortkey;

	if ((pos = head->next) == head) {
		list_add(&entry->list, head);
		return 0;
	}

	tmp = list_entry(pos, struct mipv6_sorted_list_entry, list);
	if (entry->sortkey < tmp->sortkey) {
		list_add(&entry->list, head);
		return 0;
	}

	for (; pos != head; pos = pos->next) {
		tmp = list_entry(pos, struct mipv6_sorted_list_entry, list);
		if (pos->next == head) {
			list_add(&entry->list, &tmp->list);
			return 0;
		}
		next = list_entry(pos->next, struct mipv6_sorted_list_entry, list);
		if (entry->sortkey >= tmp->sortkey && entry->sortkey < next->sortkey) {
			list_add(&entry->list, &tmp->list);
			return 0;
		}
	}

	/* never reached */
	return -1;
}

/**
 * mipv6_slist_get_first - get the first data item in the list
 * @head: list_head of the sorted list
 *
 * Returns the actual data item, not copy, so don't kfree it
 */
void *mipv6_slist_get_first(struct list_head *head)
{
	struct mipv6_sorted_list_entry *entry;

	if (list_empty(head))
		return NULL;

	entry = list_entry(head->next, struct mipv6_sorted_list_entry, list);
	return entry->data;
}

/**
 * mipv6_slist_del_first - delete (and get) the first item in list
 * @head: list_head of the sorted list
 *
 * Remember to kfree the item
 */
void *mipv6_slist_del_first(struct list_head *head)
{
	void *tmp;
	struct mipv6_sorted_list_entry *entry;

	if (list_empty(head))
		return NULL;

	entry = list_entry(head->next, struct mipv6_sorted_list_entry, list);
	tmp = entry->data;

	list_del(head->next);
	kfree(entry);

	return tmp;
}

/**
 * mipv6_slist_del_item - delete entry
 * @head: list_head of the sorted list
 * @data: item to delete
 * @compare: function used for comparing the data items
 *
 * compare function needs to have prototype
 * int (*compare)(const void *data1, const void *data2, int datalen)
 */
int mipv6_slist_del_item(struct list_head *head, void *data,
			 int (*compare)(const void *data1, const void *data2,
					int datalen))
{
	struct list_head *pos;
	struct mipv6_sorted_list_entry *entry;

	for(pos = head->next; pos != head; pos = pos->next) {
		entry = list_entry(pos, struct mipv6_sorted_list_entry, list);
		if (compare(data, entry->data, entry->datalen)) {
			list_del(pos);
			kfree(entry->data);
			kfree(entry);
			return 0;
		}
	}

	return -1;
}

/**
 * mipv6_slist_get_first_key - get sortkey of the first item
 * @head: list_head of the sorted list
 */
unsigned long mipv6_slist_get_first_key(struct list_head *head)
{
	struct mipv6_sorted_list_entry *entry;

	if (list_empty(head))
		return 0;

	entry = list_entry(head->next, struct mipv6_sorted_list_entry, list);
	return entry->sortkey;
}

/**
 * mipv6_slist_get_key - get sortkey of the data item
 * @head: list_head of the sorted list
 * @data: the item to search for
 * @compare: function used for comparing the data items
 *
 * compare function needs to have prototype
 * int (*compare)(const void *data1, const void *data2, int datalen)
 */
unsigned long mipv6_slist_get_key(struct list_head *head, void *data,
				  int (*compare)(const void *data1,
						 const void *data2,
						 int datalen))
{
	struct list_head *pos;
	struct mipv6_sorted_list_entry *entry;
	
	for(pos = head->next; pos != head; pos = pos->next) {
		entry = list_entry(pos, struct mipv6_sorted_list_entry, list);
		if (compare(data, entry->data, entry->datalen))
			return entry->sortkey;
	}
	
	return 0;
}

/**
 * mipv6_slist_get_data - get the data item identified by sortkey
 * @head: list_head of the sorted list
 * @key: sortkey of the item
 *
 * Returns the actual data item, not copy, so don't kfree it
 */
void *mipv6_slist_get_data(struct list_head *head, unsigned long sortkey)
{
	struct list_head *pos;
	struct mipv6_sorted_list_entry *entry;

	list_for_each(pos, head) {
		entry = list_entry(pos, struct mipv6_sorted_list_entry, list);
		if (entry->sortkey == sortkey) 
			return entry->data;
	}

	return NULL;
}

/**
 * reorder_entry - move an entry to a new position according to sortkey
 * @head: list_head of the sorted list
 * @entry_pos: current place of the entry
 * @key: new sortkey
 */
static void reorder_entry(struct list_head *head, struct list_head *entry_pos,
			  unsigned long sortkey)
{
	struct list_head *pos;
	struct mipv6_sorted_list_entry *entry;

	list_del(entry_pos);

	for (pos = head->next; pos != head; pos = pos->next) {
		entry = list_entry(pos, struct mipv6_sorted_list_entry, list);
		if (sortkey >= entry->sortkey) {
			list_add(entry_pos, &entry->list);
			return;
		}
	}

	list_add(entry_pos, head);
}

/**
 * mipv6_slist_modify - modify data item
 * @head: list_head of the sorted list
 * @data: item, whose sortkey is to be modified
 * @datalen: datalen in bytes
 * @new_key: new sortkey
 * @compare: function used for comparing the data items
 *
 * Compies the new data on top of the old one, if compare function returns
 * true. If there's no matching entry, new one will be created.
 * Compare function needs to have prototype
 * int (*compare)(const void *data1, const void *data2, int datalen)
 */
int mipv6_slist_modify(struct list_head *head, void *data, int datalen,
		       unsigned long new_key,
		       int (*compare)(const void *data1, const void *data2,
				      int datalen))
{
	struct list_head *pos;
	struct mipv6_sorted_list_entry *entry;

	for (pos = head->next; pos != head; pos = pos->next) {
		entry = list_entry(pos, struct mipv6_sorted_list_entry, list);
		if (compare(data, entry->data, datalen)) {
			memcpy(entry->data, data, datalen);
			entry->sortkey = new_key;
			reorder_entry(head, &entry->list, new_key);
			return 0;
		}
	}

	return mipv6_slist_add(head, data, datalen, new_key);
}

/**
 * mipv6_slist_push_first - move the first entry to place indicated by new_key
 * @head: list_head of the sorted list
 * @new_key: new sortkey
 */
int mipv6_slist_push_first(struct list_head *head, unsigned long new_key)
{
	struct mipv6_sorted_list_entry *entry;

	if (list_empty(head))
		return -1;

	entry = list_entry(head->next, struct mipv6_sorted_list_entry, list);
	entry->sortkey = new_key;

	reorder_entry(head, head->next, new_key);
	return 0;
}

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
				     unsigned long sortkey))
{
	struct list_head *pos;
	struct mipv6_sorted_list_entry *entry;

	list_for_each(pos, head) {
		entry = list_entry(pos, struct mipv6_sorted_list_entry, list);
		if (func(entry->data, args, entry->sortkey))
			break;
	}

	return 0;
}

EXPORT_SYMBOL(mipv6_slist_for_each);
EXPORT_SYMBOL(mipv6_slist_modify);
EXPORT_SYMBOL(mipv6_slist_get_first_key);
EXPORT_SYMBOL(mipv6_slist_add);

