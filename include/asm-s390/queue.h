/*
 *  include/asm-s390/queue.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  A little set of queue utilies.
 */
#include <linux/stddef.h>
#include <asm/types.h>

typedef struct queue
{
	struct queue *next;	
} queue;

typedef queue list;

typedef struct
{
	queue *head;
	queue *tail;
} qheader;

static __inline__ void init_queue(qheader *qhead)
{
	memset(qhead,0,sizeof(*qhead));
}

static __inline__ void enqueue_tail(qheader *qhead,queue *member)
{
	queue *tail=qhead->tail;
	member->next=NULL;
	
	if(member)
	{
		if(tail)
			tail->next=member;
		else
			
			qhead->head=member;
		qhead->tail=member;
		member->next=NULL;
	}
} 

static __inline__ queue *dequeue_head(qheader *qhead)
{
	queue *head=qhead->head,*next_head;

	if(head)
	{
		next_head=head->next;
		qhead->head=next_head;
	        if(!next_head)
			qhead->tail=NULL;
	}
	return(head);
}

static __inline__ void init_list(list **lhead)
{
	*lhead=NULL;
}

static __inline__ void add_to_list(list **lhead,list *member)
{
	member->next=*lhead;
	*lhead=member;
}

static __inline__ int is_in_list(list *lhead,list *member)
{
	list *curr;

	for(curr=lhead;curr!=NULL;curr=curr->next)
		if(curr==member)
			return(TRUE);
	return(FALSE);
}

static __inline__ int get_prev(list *lhead,list *member,list **prev)
{
	list *curr;

	*prev=NULL;
	for(curr=lhead;curr!=NULL;curr=curr->next)
	{
		if(curr==member)
			return(TRUE);
		*prev=curr;
	}
	*prev=NULL;
	return(FALSE);
}


static __inline__ int remove_from_list(list **lhead,list *member)
{
	list *prev;

	if(get_prev(*lhead,member,&prev))
	{

		if(prev)
			prev->next=member->next;
		else
			*lhead=member->next;
		return(TRUE);
	}
	return(FALSE);
}




