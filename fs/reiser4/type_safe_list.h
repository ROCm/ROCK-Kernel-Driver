/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#ifndef __REISER4_TYPE_SAFE_LIST_H__
#define __REISER4_TYPE_SAFE_LIST_H__

#include "debug.h"
/* A circular doubly linked list that differs from the previous
   <linux/list.h> implementation because it is parametrized to provide
   type safety.  This data structure is also useful as a queue or stack.

   The "list template" consists of a set of types and methods for
   implementing list operations.  All of the types and methods
   associated with a single list class are assigned unique names and
   type signatures, thus allowing the compiler to verify correct
   usage.

   The first parameter of a list class is the item type being stored
   in the list.  The list class maintains two pointers within each
   item structure for its "next" and "prev" pointers.

   There are two structures associated with the list, in addition to
   the item type itself.  The "list link" contains the two pointers
   that are embedded within the item itself.  The "list head" also
   contains two pointers which refer to the first item ("front") and
   last item ("back") of the list.

   The list maintains a "circular" invariant, in that you can always
   begin at the front and follow "next" pointers until eventually you
   reach the same point.  The "list head" is included within the
   cycle, even though it does not have the correct item type.  The
   "list head" and "list link" types are different objects from the
   user's perspective, but the core algorithms that operate on this
   style of list treat the "list head" and "list link" as identical
   types.  That is why these algorithms are so simple.

   The <linux/list.h> implementation uses the same algorithms as those
   in this file but uses only a single type "struct list_head".  There
   are two problems with this approach.  First, there are no type
   distinctions made between the two objects despite their distinct
   types, which greatly increases the possibility for mistakes.  For
   example, the <linux/list.h> list_add function takes two "struct
   list_head" arguments: the first is the item being inserted and the
   second is the "struct list_head" which should precede the new
   insertion to the list.  You can use this function to insert at any
   point in the list, but by far the most common list operations are
   to insert at the front or back of the list.  This common case
   should accept two different argument types: a "list head" and an
   "item", this allows for no confusion.

   The second problem with using a single "struct list_head" is that
   it does not distinguish between list objects of distinct list
   classes.  If a single item can belong to two separate lists, there
   is easily the possibility of a mistake being made that causes the
   item to be added to a "list head" using the wrong "list link".  By
   using a parametrized list class we can statically detect such
   mistakes, detecting mistakes as soon as they happen.

   To create a new list class takes several steps which are described
   below.  Suppose for this example that you would like to link
   together items of type "rx_event".  You should decide on
   prefix-name to be used on all list functions and structures.  For
   example, the string "rx_event" can be as a prefix for all the list
   operations, resulting in a "list head" named rx_event_list_head and
   a "list link" named rx_event_list_link.  The list operations on
   this list class would be named "rx_event_list_empty",
   "rx_event_list_init", "rx_event_list_push_front",
   "rx_event_list_push_back", and so on.
*/

#define TYPE_SAFE_LIST_LINK_INIT(name) { &(name), &(name) }
#define TYPE_SAFE_LIST_HEAD_INIT(name) { (void *)&(name), (void *)&(name) }
#define TYPE_SAFE_LIST_LINK_ZERO { NULL, NULL }
#define TYPE_SAFE_LIST_HEAD_ZERO { NULL, NULL }

#define TS_LINK_TO_ITEM(ITEM_TYPE,LINK_NAME,LINK) \
	((ITEM_TYPE *)((char *)(LINK)-(unsigned long)(&((ITEM_TYPE *)0)->LINK_NAME)))

/* Step 1: Use the TYPE_SAFE_LIST_DECLARE() macro to define the "list head"
   and "list link" objects.  This macro takes one arguments, the
   prefix-name, which is prepended to every structure and function
   name of the list class.  Following the example, this will create
   types named rx_event_list_head and rx_event_list_link.  In the
   example you would write:

   TYPE_SAFE_LIST_DECLARE(rx_event);

*/
#define TYPE_SAFE_LIST_DECLARE(PREFIX)                                                               \
                                                                                              \
typedef struct _##PREFIX##_list_head        PREFIX##_list_head;                               \
typedef struct _##PREFIX##_list_link        PREFIX##_list_link;                               \
                                                                                              \
struct _##PREFIX##_list_link                                                                  \
{                                                                                             \
  PREFIX##_list_link *_next;                                                                  \
  PREFIX##_list_link *_prev;                                                                  \
};                                                                                            \
                                                                                              \
struct _##PREFIX##_list_head                                                                  \
{                                                                                             \
  PREFIX##_list_link *_next;                                                                  \
  PREFIX##_list_link *_prev;                                                                  \
}

/* Step 2: Once you have defined the two list classes, you should
   define the item type you intend to use.  The list classes must be
   declared before the item type because the item type must contain an
   embedded "list link" object.  Following the example, you might define
   rx_event as follows:

   typedef struct _rx_event  rx_event;

   struct _rx_event
   {
     ... other members ...

     rx_event_list_link  _link;
   };

   In this case we have given the rx_event a field named "_link" of
   the appropriate type.
*/

/* Step 3: The final step will define the list-functions for a
   specific list class using the macro TYPE_SAFE_LIST_DEFINE.  There are
   three arguments to the TYPE_SAFE_LIST_DEFINE macro: the prefix-name, the
   item type name, and field name of the "list link" element within
   the item type.  In the above example you would supply "rx_event" as
   the type name and "_link" as the field name (without quotes).
   E.g.,

   TYPE_SAFE_LIST_DEFINE(rx_event,rx_event,_link)

   The list class you define is now complete with the functions:

   rx_event_list_init             Initialize a list_head
   rx_event_list_clean            Initialize a list_link
   rx_event_list_is_clean         True if list_link is not in a list
   rx_event_list_push_front       Insert to the front of the list
   rx_event_list_push_back        Insert to the back of the list
   rx_event_list_insert_before    Insert just before given item in the list
   rx_event_list_insert_after     Insert just after given item in the list
   rx_event_list_remove           Remove an item from anywhere in the list
   rx_event_list_remove_clean     Remove an item from anywhere in the list and clean link_item
   rx_event_list_remove_get_next  Remove an item from anywhere in the list and return the next element
   rx_event_list_remove_get_prev  Remove an item from anywhere in the list and return the prev element
   rx_event_list_pop_front        Remove and return the front of the list, cannot be empty
   rx_event_list_pop_back         Remove and return the back of the list, cannot be empty
   rx_event_list_front            Get the front of the list
   rx_event_list_back             Get the back of the list
   rx_event_list_next             Iterate front-to-back through the list
   rx_event_list_prev             Iterate back-to-front through the list
   rx_event_list_end              Test to end an iteration, either direction
   rx_event_list_splice           Join two lists at the head
   rx_event_list_empty            True if the list is empty
   rx_event_list_object_ok        Check that list element satisfies double
                                  list invariants. For debugging.

   To iterate over such a list use a for-loop such as:

     rx_event_list_head *head = ...;
     rx_event *item;

     for (item = rx_event_list_front (head);
               ! rx_event_list_end   (head, item);
          item = rx_event_list_next  (item))
       {...}
*/
#define TYPE_SAFE_LIST_DEFINE(PREFIX,ITEM_TYPE,LINK_NAME)                                            \
                                                                                              \
static __inline__ int                                                                         \
PREFIX##_list_link_invariant (const PREFIX##_list_link  *_link)                               \
{                                                                                             \
  return (_link != NULL) &&                                                                   \
	  (_link->_prev != NULL) && (_link->_next != NULL ) &&                                \
	  (_link->_prev->_next == _link) &&                                                   \
	  (_link->_next->_prev == _link);                                                     \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_link_ok (const PREFIX##_list_link  *_link UNUSED_ARG)                           \
{                                                                                             \
  assert ("nikita-1054", PREFIX##_list_link_invariant (_link));                               \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_object_ok (const ITEM_TYPE           *item)                                     \
{                                                                                             \
  PREFIX##_list_link_ok (&item->LINK_NAME);                                                   \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_init (PREFIX##_list_head  *head)                                                \
{                                                                                             \
  head->_next = (PREFIX##_list_link*) head;                                                   \
  head->_prev = (PREFIX##_list_link*) head;                                                   \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_clean (ITEM_TYPE           *item)                                               \
{                                                                                             \
  PREFIX##_list_link *_link = &item->LINK_NAME;                                               \
                                                                                              \
  _link->_next = _link;                                                                       \
  _link->_prev = _link;                                                                       \
}                                                                                             \
                                                                                              \
static __inline__ int                                                                         \
PREFIX##_list_is_clean (const ITEM_TYPE           *item)                                      \
{                                                                                             \
  const PREFIX##_list_link *_link = &item->LINK_NAME;                                         \
                                                                                              \
  PREFIX##_list_link_ok (_link);                                                              \
  return (_link == _link->_next) && (_link == _link->_prev);                                  \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_insert_int (PREFIX##_list_link  *next,                                          \
	 		         PREFIX##_list_link  *item)                                   \
{                                                                                             \
  PREFIX##_list_link *prev = next->_prev;                                                     \
  PREFIX##_list_link_ok (next);                                                               \
  PREFIX##_list_link_ok (prev);                                                               \
  next->_prev = item;                                                                         \
  item->_next = next;                                                                         \
  item->_prev = prev;                                                                         \
  prev->_next = item;                                                                         \
  PREFIX##_list_link_ok (next);                                                               \
  PREFIX##_list_link_ok (prev);                                                               \
  PREFIX##_list_link_ok (item);                                                               \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_push_front (PREFIX##_list_head  *head,                                          \
			  ITEM_TYPE           *item)                                          \
{                                                                                             \
  PREFIX##_list_insert_int (head->_next, & item->LINK_NAME);                                  \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_push_back (PREFIX##_list_head  *head,                                           \
			 ITEM_TYPE           *item)                                           \
{                                                                                             \
  PREFIX##_list_insert_int ((PREFIX##_list_link *) head, & item->LINK_NAME);                  \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_insert_before (ITEM_TYPE         *reference,                                    \
			   ITEM_TYPE           *item)                                         \
{                                                                                             \
  PREFIX##_list_insert_int (& reference->LINK_NAME, & item->LINK_NAME);                       \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_insert_after (ITEM_TYPE         *reference,                                     \
			  ITEM_TYPE           *item)                                          \
{                                                                                             \
  PREFIX##_list_insert_int (reference->LINK_NAME._next, & item->LINK_NAME);                   \
}                                                                                             \
                                                                                              \
static __inline__ PREFIX##_list_link*                                                         \
PREFIX##_list_remove_int (PREFIX##_list_link *list_link)                                      \
{                                                                                             \
  PREFIX##_list_link *next = list_link->_next;                                                \
  PREFIX##_list_link *prev = list_link->_prev;                                                \
  PREFIX##_list_link_ok (list_link);                                                          \
  PREFIX##_list_link_ok (next);                                                               \
  PREFIX##_list_link_ok (prev);                                                               \
  next->_prev = prev;                                                                         \
  prev->_next = next;                                                                         \
  PREFIX##_list_link_ok (next);                                                               \
  PREFIX##_list_link_ok (prev);                                                               \
  return list_link;                                                                           \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_remove (ITEM_TYPE  *item)                                                       \
{                                                                                             \
  PREFIX##_list_remove_int (& item->LINK_NAME);                                               \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_remove_clean (ITEM_TYPE  *item)                                                 \
{                                                                                             \
  PREFIX##_list_remove_int (& item->LINK_NAME);                                               \
  PREFIX##_list_clean (item);                                                                 \
}                                                                                             \
                                                                                              \
static __inline__ ITEM_TYPE*                                                                  \
PREFIX##_list_remove_get_next (ITEM_TYPE  *item)                                              \
{                                                                                             \
  PREFIX##_list_link *next = item->LINK_NAME._next;                                           \
  PREFIX##_list_remove_int (& item->LINK_NAME);                                               \
  return TS_LINK_TO_ITEM(ITEM_TYPE,LINK_NAME,next);                                           \
}                                                                                             \
                                                                                              \
static __inline__ ITEM_TYPE*                                                                  \
PREFIX##_list_remove_get_prev (ITEM_TYPE  *item)                                              \
{                                                                                             \
  PREFIX##_list_link *prev = item->LINK_NAME._prev;                                           \
  PREFIX##_list_remove_int (& item->LINK_NAME);                                               \
  return TS_LINK_TO_ITEM(ITEM_TYPE,LINK_NAME,prev);                                           \
}                                                                                             \
                                                                                              \
static __inline__ int                                                                         \
PREFIX##_list_empty (const PREFIX##_list_head  *head)                                         \
{                                                                                             \
  return head == (PREFIX##_list_head*) head->_next;                                           \
}                                                                                             \
                                                                                              \
static __inline__ ITEM_TYPE*                                                                  \
PREFIX##_list_pop_front (PREFIX##_list_head  *head)                                           \
{                                                                                             \
  assert ("nikita-1913", ! PREFIX##_list_empty (head));                                       \
  return TS_LINK_TO_ITEM(ITEM_TYPE,LINK_NAME,PREFIX##_list_remove_int (head->_next));         \
}                                                                                             \
                                                                                              \
static __inline__ ITEM_TYPE*                                                                  \
PREFIX##_list_pop_back (PREFIX##_list_head  *head)                                            \
{                                                                                             \
  assert ("nikita-1914", ! PREFIX##_list_empty (head)); /* WWI started */                     \
  return TS_LINK_TO_ITEM(ITEM_TYPE,LINK_NAME,PREFIX##_list_remove_int (head->_prev));         \
}                                                                                             \
                                                                                              \
static __inline__ ITEM_TYPE*                                                                  \
PREFIX##_list_front (const PREFIX##_list_head  *head)                                         \
{                                                                                             \
  return TS_LINK_TO_ITEM(ITEM_TYPE,LINK_NAME,head->_next);                                    \
}                                                                                             \
                                                                                              \
static __inline__ ITEM_TYPE*                                                                  \
PREFIX##_list_back (const PREFIX##_list_head  *head)                                          \
{                                                                                             \
  return TS_LINK_TO_ITEM(ITEM_TYPE,LINK_NAME,head->_prev);                                    \
}                                                                                             \
                                                                                              \
static __inline__ ITEM_TYPE*                                                                  \
PREFIX##_list_next (const ITEM_TYPE *item)                                                    \
{                                                                                             \
  return TS_LINK_TO_ITEM(ITEM_TYPE,LINK_NAME,item->LINK_NAME._next);                          \
}                                                                                             \
                                                                                              \
static __inline__ ITEM_TYPE*                                                                  \
PREFIX##_list_prev (const ITEM_TYPE *item)                                                    \
{                                                                                             \
  return TS_LINK_TO_ITEM(ITEM_TYPE,LINK_NAME,item->LINK_NAME._prev);                          \
}                                                                                             \
                                                                                              \
static __inline__ int                                                                         \
PREFIX##_list_end (const PREFIX##_list_head  *head,                                           \
 		   const ITEM_TYPE           *item)                                           \
{                                                                                             \
  return ((PREFIX##_list_link *) head) == (& item->LINK_NAME);                                \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_splice (PREFIX##_list_head  *head_join,                                         \
 		      PREFIX##_list_head  *head_empty)                                        \
{                                                                                             \
  if (PREFIX##_list_empty (head_empty)) {                                                     \
    return;                                                                                   \
  }                                                                                           \
                                                                                              \
  head_empty->_prev->_next = (PREFIX##_list_link*) head_join;                                 \
  head_empty->_next->_prev = head_join->_prev;                                                \
                                                                                              \
  head_join->_prev->_next  = head_empty->_next;                                               \
  head_join->_prev         = head_empty->_prev;                                               \
                                                                                              \
  PREFIX##_list_link_ok ((PREFIX##_list_link*) head_join);                                    \
  PREFIX##_list_link_ok (head_join->_prev);                                                   \
  PREFIX##_list_link_ok (head_join->_next);                                                   \
                                                                                              \
  PREFIX##_list_init (head_empty);                                                            \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_split(PREFIX##_list_head  *head_split,                                          \
		    PREFIX##_list_head  *head_new,                                            \
		    ITEM_TYPE  *item)                                                         \
{                                                                                             \
  assert("vs-1471", PREFIX##_list_empty(head_new));                                           \
                                                                                              \
  /* attach to new list */                                                                    \
  head_new->_next = (& item->LINK_NAME);                                                      \
  head_new->_prev = head_split->_prev;                                                        \
                                                                                              \
  /* cut from old list */                                                                     \
  item->LINK_NAME._prev->_next = (PREFIX##_list_link*)head_split;                             \
  head_split->_prev = item->LINK_NAME._prev;                                                  \
                                                                                              \
  /* link new list */                                                                         \
  head_new->_next->_prev = (PREFIX##_list_link*)head_new;                                     \
  head_new->_prev->_next = (PREFIX##_list_link*)head_new;                                     \
}                                                                                             \
                                                                                              \
static __inline__ void                                                                        \
PREFIX##_list_check (const PREFIX##_list_head  *head)                                         \
{                                                                                             \
	const PREFIX##_list_link *link;                                                       \
                                                                                              \
	for (link = head->_next ; link != ((PREFIX##_list_link *) head) ; link = link->_next) \
		PREFIX##_list_link_ok (link);                                                 \
}                                                                                             \
                                                                                              \
typedef struct { int foo; } PREFIX##_list_dummy_decl

/* The final typedef is to allow a semicolon at the end of
 * TYPE_SAFE_LIST_DEFINE(); */

#define for_all_type_safe_list(prefix, head, item)		\
	for(item = prefix ## _list_front(head),			\
                   prefetch(prefix ## _list_next(item));	\
	    !prefix ## _list_end(head, item) ;			\
	    item = prefix ## _list_next(item),			\
		    prefetch(prefix ## _list_next(item)))

#define for_all_type_safe_list_safe(prefix, head, item, next)	\
	for(item = prefix ## _list_front(head),			\
            next = prefix ## _list_next(item);			\
	    !prefix ## _list_end(head, item) ;			\
	    item = next,					\
	    next = prefix ## _list_next(item))

/* __REISER4_TYPE_SAFE_LIST_H__ */
#endif

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
