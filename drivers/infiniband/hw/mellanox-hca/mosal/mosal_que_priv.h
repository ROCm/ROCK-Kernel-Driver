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

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef H_MOSAL_QUE_PRIV_H
#define H_MOSAL_QUE_PRIV_H

#ifdef __KERNEL__

#if LINUX_KERNEL_2_2
#define GET_HANDLER(qh) (&qhandles[(qh)]->q)
#define WAIT_QUEUE struct wait_queue
#define GET_HANDLER_Q(qh) qhandles[(qh)]->q
#elif LINUX_KERNEL_2_4 || LINUX_KERNEL_2_6
#define GET_HANDLER(qh) (qhandles[(qh)]->q)
#define WAIT_QUEUE wait_queue_head_t
#define GET_HANDLER_Q(qh) *(qhandles[(qh)]->q)
#endif

/* Mosal queue defs. */
struct mosalq_el
{
    int              size;
    void             *data;
    struct mosalq_el *next;
};

struct mosalq_st
{
    int               is_waiting;
    int               is_wakedup;
    int               is_destroyed;
    WAIT_QUEUE        *q;
    volatile struct mosalq_el  *head;
    volatile struct mosalq_el  *tail;
    MOSAL_data_free_t qdestroy_free;  /* Use to free of data on qdestroy */
};

extern struct mosalq_st* qhandles[MOSAL_MAX_QHANDLES];

#endif

#endif

