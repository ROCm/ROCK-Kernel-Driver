/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

#ifndef _H_ELX_UTIL
#define _H_ELX_UTIL

/* Structure to be used for single linked list header */
typedef struct elx_slink {
	struct elx_slink *q_first;	/* queue first element */
	struct elx_slink *q_last;	/* queue last element */
	uint16_t q_cnt;		/* current length of queue */
	uint16_t q_max;		/* max length */
} ELX_SLINK_t;

/* Structure to be used for double linked list header */
typedef struct elx_dlink {
	struct elx_dlink *q_f;	/* queue forward element */
	struct elx_dlink *q_b;	/* queue backward element */
	uint16_t q_cnt;		/* current length of queue */
	uint16_t q_max;		/* max length */
} ELX_DLINK_t;

#define elx_enque(x,p) {(((ELX_DLINK_t *)x)->q_f    = ((ELX_DLINK_t *)p)->q_f, \
      ((ELX_DLINK_t *)x)->q_b      = ((ELX_DLINK_t *)p),     \
      ((ELX_DLINK_t *)p)->q_f->q_b = ((ELX_DLINK_t *)x),     \
      ((ELX_DLINK_t *)p)->q_f      = ((ELX_DLINK_t *)x));}

#define elx_deque(x) {(((ELX_DLINK_t *)x)->q_b->q_f = ((ELX_DLINK_t *)x)->q_f, \
      ((ELX_DLINK_t *)x)->q_f->q_b = ((ELX_DLINK_t *)x)->q_b, \
      ((ELX_DLINK_t *)x)->q_b      = 0,                       \
      ((ELX_DLINK_t *)x)->q_f      = 0);}

/*    Typed Queues (Single or Double.. no casing.. multiple queues in a item */

/* Structure to be used for single linked list header */
#define ELX_TQS_LINK(structName) struct  {  \
   struct structName  *q_first;                   \
   struct structName  *q_last;                    \
   uint16_t           q_cnt;                      \
   uint16_t           q_max;                      \
}                                                 \

#define  elx_tqs_enqueue(queue,item,nextPtr) { \
   if(((queue)->q_cnt)++)                       \
      (queue)->q_last->nextPtr=item;            \
   else (queue)->q_first=item;                  \
   (queue)->q_last=item;                        \
   (item)->nextPtr=NULL;                        \
   if((queue)->q_cnt > (queue)->q_max)          \
            (queue)->q_max=(queue)->q_cnt;}              \

#define  elx_tqs_getcount(queue) (queue)->q_cnt
#define  elx_tqs_getfirst(queue) (queue)->q_first
#define  elx_tqs_getnext(item,nextPtr) (item)->nextPtr

#define  elx_tqs_putfirst(queue,newFirst,link) {      \
   (newFirst)->link=(queue)->q_first;                 \
   (queue)->q_first=newFirst;                         \
   if(!((queue)->q_cnt++)) (queue)->q_last=newFirst;}

#define  elx_tqs_dequeue(queue,item,link,previous) {  \
   if((queue)->q_cnt) {                               \
      (queue)->q_cnt--;                               \
      if(previous) (previous)->link = (item)->link;   \
      else (queue)->q_first = (item)->link;           \
      if((queue)->q_last== item)                      \
            (queue)->q_last=previous;                 \
      (item)->link=NULL; }                            \
   }                                                  \

#define  elx_tqs_dequeuefirst(queue,link)              \
      ((queue)->q_cnt) ? (queue)->q_first : NULL;      \
   {                                                   \
   if((queue)->q_cnt) {                                \
      (queue)->q_cnt--;                                \
      if((queue)->q_last == (queue)->q_first)          \
         (queue)->q_last=(queue)->q_first->link;       \
      (queue)->q_first =(queue)->q_first->link;        \
      }                                                \
  }						       \

/* Structure to be used for double linked list header */

#define ELX_TQD_LINK(structName) struct  {  \
   struct structName  *q_f;                     \
   struct structName  *q_b;                    \
}                                                 \

#define elx_tqd_onque(queue) ((queue).q_f ? 1 : 0)
#define elx_tqd_getnext(queue) ((queue).q_f)

#define elx_tqd_enque(x,p,queue) {        \
   if(p) {                                \
      ((x)->queue.q_f = (p)->queue.q_f,   \
      (x)->queue.q_b  = (p),              \
      (p)->queue.q_f->queue.q_b = (x),    \
      (p)->queue.q_f  = (x));}             \
      else (x)->queue.q_f=(x)->queue.q_b=x;}

#define  elx_tpd_first

#define elx_tqd_deque(x,queue) {((x)->queue.q_b->queue.q_f = (x)->queue.q_f, \
      (x)->queue.q_f->queue.q_b = (x)->queue.q_b, \
      (x)->queue.q_b      = 0,                       \
      (x)->queue.q_f      = 0);}

#endif				/* _H_ELX_UTIL */
