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

#ifndef VIP_COMMON_VIP_CIRQ_H
#define VIP_COMMON_VIP_CIRQ_H


#include <vapi_types.h>
#include <mosal.h>

typedef struct VIP_cirq_st {
	int  consumer;
	int  producer;
	int  q_size;
	MT_bool full;
	void*  queue;
	int   element_size;
    MOSAL_mutex_t  cirq_access_mtx;
} VIP_cirq_t;

int VIP_cirq_create(int q_size, int element_size, VIP_cirq_t **cirq);
int VIP_cirq_remove(VIP_cirq_t  *cirq_p, void * elt);
int VIP_cirq_add(VIP_cirq_t  *cirq_p, void * elt);
void VIP_cirq_stats_print(VIP_cirq_t  *cirq_p);
int VIP_cirq_peek(VIP_cirq_t  *cirq_p, void *elt);
int VIP_cirq_peek_ptr(VIP_cirq_t  *cirq_p, void **elt);
int VIP_cirq_destroy(VIP_cirq_t  *cirq_p);
int VIP_cirq_empty(VIP_cirq_t *cirq_p);

#endif
