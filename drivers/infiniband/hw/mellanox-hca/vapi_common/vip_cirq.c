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

#include "vip_cirq.h"

void VIP_cirq_stats_print(VIP_cirq_t  *cirq_p)
{
    MTL_DEBUG4("%s: cirq 0x%p stats:\n", __func__, cirq_p->queue);
    MTL_DEBUG4("%s: queue size: %d\n", __func__, cirq_p->q_size);
    MTL_DEBUG4("%s: elt size: %d\n", __func__, cirq_p->element_size);
    MTL_DEBUG4("%s: producer: %d\n", __func__, cirq_p->producer);
    MTL_DEBUG4("%s: consumer: %d\n", __func__, cirq_p->consumer);
    MTL_DEBUG4("%s: full flag: %s\n", __func__, (cirq_p->full == TRUE ? "TRUE" : "FALSE"));
}

static int VIP_cirq_empty_no_mtx(VIP_cirq_t *cirq_p)
{
    int retval = FALSE;
	if(cirq_p->consumer==cirq_p->producer && !(cirq_p->full))
		retval = TRUE;
    return retval;
}
  

int VIP_cirq_create(int q_size, int element_size, VIP_cirq_t  **cirq_p)
{
    VIP_cirq_t *new_cirq;

    MTL_DEBUG4(MT_FLFMT("VIP_cirq_create: queue size = %d, elt size = %d"),q_size,element_size);
    new_cirq = (VIP_cirq_t *)MALLOC(sizeof(VIP_cirq_t));
    if (new_cirq == NULL) {
        MTL_ERROR1(MT_FLFMT("VIP_cirq_create: MALLOC failure"));
        return -1;
    }

    new_cirq->queue = (void*)VMALLOC( element_size * q_size);
    if (new_cirq == NULL) {
        MTL_ERROR1(MT_FLFMT("VIP_cirq_create: VMALLOC failure"));
        FREE(new_cirq);
        return -1;
    }
    new_cirq->full = FALSE;
    new_cirq->producer = new_cirq->consumer = 0;
    new_cirq->q_size = q_size;
    new_cirq->element_size = element_size;
    MOSAL_mutex_init(&(new_cirq->cirq_access_mtx));
    *cirq_p = new_cirq;
    return 0;
}


int VIP_cirq_add(VIP_cirq_t  *cirq_p, void * elt)
{
    int retval = 0;
    MTL_DEBUG4(MT_FLFMT("VIP_cirq_add, cirq=0x%p"), cirq_p);
    MOSAL_mutex_acq(&(cirq_p->cirq_access_mtx), TRUE);
	
    if (cirq_p->full == TRUE || elt == NULL) {
        MTL_ERROR1(MT_FLFMT("VIP_cirq_add: queue 0x%p full or element to add (0x%p)is null"), cirq_p->queue, elt);
        retval = -1;
	} else {   
        /* insert at the producer end */
        memcpy(((u_int8_t *)(cirq_p->queue)) + (cirq_p->producer * cirq_p->element_size), elt, cirq_p->element_size);
		(cirq_p->producer)++;
        if(cirq_p->producer==cirq_p->q_size)
			cirq_p->producer=0;
		if(cirq_p->producer==cirq_p->consumer)
		{
			cirq_p->full=1;
		}
	}
    
    // VIP_cirq_stats_print(cirq_p); 
    MOSAL_mutex_rel(&(cirq_p->cirq_access_mtx));
    return (retval);
}

int VIP_cirq_peek(VIP_cirq_t  *cirq_p, void * elt)
{
    int retval = 0;
    MTL_DEBUG4(MT_FLFMT("VIP_cirq_peek"));
    MOSAL_mutex_acq(&(cirq_p->cirq_access_mtx), TRUE);
    
    /* check that elt is not null and queue not empty */
    if (VIP_cirq_empty_no_mtx(cirq_p) || elt == NULL) {
        MTL_DEBUG1(MT_FLFMT("VIP_cirq_peek: queue 0x%p is empty or element (0x%p) is NULL"), cirq_p->queue,elt);
        retval =  -1;
    } else {
        memcpy(elt, ((u_int8_t *)(cirq_p->queue)) + (cirq_p->consumer * cirq_p->element_size), cirq_p->element_size);
    }
    
    MOSAL_mutex_rel(&(cirq_p->cirq_access_mtx));
    return retval;
}

int VIP_cirq_peek_ptr(VIP_cirq_t  *cirq_p, void **elt)
{
    int retval = 0;
    MTL_DEBUG4(MT_FLFMT("VIP_cirq_peek_ptr"));
    MOSAL_mutex_acq(&(cirq_p->cirq_access_mtx), TRUE);
    
    /* check that *elt is not null and queue not empty */
    if (VIP_cirq_empty_no_mtx(cirq_p) || elt == NULL) {
        MTL_DEBUG1(MT_FLFMT("VIP_cirq_peek_ptr: queue 0x%p is empty or element (0x%p) is NULL"), cirq_p->queue, elt);
        retval =  -1;
    } else {
      *elt =  ((u_int8_t *)(cirq_p->queue)) + (cirq_p->consumer * cirq_p->element_size);
    }
    
    MOSAL_mutex_rel(&(cirq_p->cirq_access_mtx));
    return retval;
}


int VIP_cirq_remove(VIP_cirq_t  *cirq_p, void * elt)
{
    int retval = 0;
    

    MTL_DEBUG4(MT_FLFMT("VIP_cirq_remove, cirq=0x%p"), cirq_p);
    MOSAL_mutex_acq(&(cirq_p->cirq_access_mtx), TRUE);
	
    if(VIP_cirq_empty_no_mtx(cirq_p)) {
        MTL_DEBUG1(MT_FLFMT("VIP_cirq_remove: queue 0x%p is empty"), cirq_p->queue);
		retval = -1;
	} else {
        /* remove(read) at the consumer end */
      if(elt != NULL){
        memcpy(elt, ((u_int8_t *)(cirq_p->queue)) + (cirq_p->consumer * cirq_p->element_size),
               cirq_p->element_size);
      }
        (cirq_p->consumer)++;
        cirq_p->full=0;
    	if(cirq_p->consumer==cirq_p->q_size)
    		cirq_p->consumer=0;
    	if(cirq_p->consumer==cirq_p->producer)	{
            MTL_DEBUG4(MT_FLFMT("VIP_cirq_add: queue 0x%p is empty"), cirq_p->queue);
    		cirq_p->consumer=cirq_p->producer=0;
    		cirq_p->full=0;
    	}
    }
    //VIP_cirq_stats_print(cirq_p);    
    MOSAL_mutex_rel(&(cirq_p->cirq_access_mtx));
	return(retval);  
}

int VIP_cirq_empty(VIP_cirq_t *cirq_p)
{
    int retval = FALSE;
    MOSAL_mutex_acq(&(cirq_p->cirq_access_mtx), TRUE);
	if(cirq_p->consumer==cirq_p->producer && !(cirq_p->full))
		retval = TRUE;
    MOSAL_mutex_rel(&(cirq_p->cirq_access_mtx));
    return retval;
}
  

int VIP_cirq_destroy(VIP_cirq_t  *cirq_p)
{
    call_result_t mt_rc;

    MTL_DEBUG4(MT_FLFMT("VIP_cirq_destroy, cirq=0x%p"), cirq_p);
    MOSAL_mutex_acq(&(cirq_p->cirq_access_mtx), TRUE);
    VFREE(cirq_p->queue);
    MOSAL_mutex_rel(&(cirq_p->cirq_access_mtx));
    mt_rc = MOSAL_mutex_free(&(cirq_p->cirq_access_mtx));
    if (mt_rc != MT_OK) {
      MTL_ERROR2(MT_FLFMT("Failed MOSAL_mutex_free (%s)"),mtl_strerror_sym(mt_rc));
    }
    FREE(cirq_p);
    return(0);
}
