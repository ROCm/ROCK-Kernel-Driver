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

 #include <mosal.h>
 #include <vip_delay_unlock_priv.h>
 
 int VIP_delay_unlock_create(VIP_delay_unlock_t * delay_unlock_obj_p)
 {
     VIP_delay_unlock_t  new_obj = NULL;

     new_obj = (VIP_delay_unlock_t )MALLOC(sizeof(struct VIP_delay_unlock_st));
     if (new_obj == NULL) {
         * delay_unlock_obj_p = NULL;
         /* malloc failure */
         return -3;
     }
     new_obj->list_start = NULL;
     new_obj->is_valid = TRUE;       /* sets list to valid */
     MOSAL_spinlock_init(&new_obj->spl);
     *delay_unlock_obj_p = new_obj;
     return 0;
 }

 int VIP_delay_unlock_insert(VIP_delay_unlock_t delay_unlock_obj,
                              MOSAL_iobuf_t iobuf) 
 {

     VIP_delay_unlock_elem_t * new_elt = NULL;
     VIP_delay_unlock_elem_t * temp_next = NULL;

     if (delay_unlock_obj == NULL) {
         return -1; /* invalid argument */
     }

     /* allocate a new stack element */
     new_elt = TMALLOC(VIP_delay_unlock_elem_t);
     if (new_elt == NULL) {
         /* malloc failure */
         MTL_ERROR1("%s:  MALLOC failure. cannot defer delete of iobuf=0x%p\n",
                 __func__, (void *) iobuf);
         return -3;
     }
     new_elt->iobuf = iobuf;
     MTL_DEBUG1("%s: DEFERRING unlock of iobuf=0x%p \n",
             __func__, (void *) iobuf);
     MOSAL_spinlock_irq_lock(&delay_unlock_obj->spl);
     if (delay_unlock_obj->is_valid == FALSE) {
         /* list has been deleted */
         MOSAL_spinlock_unlock(&delay_unlock_obj->spl);
         MTL_ERROR1("%s: DEFERRED LIST has been deleted. FAIL deferring delete of iobuf=0x%p\n",
                 __func__, (void *) iobuf);
         FREE(new_elt);
         return -2;
     }
     temp_next = delay_unlock_obj->list_start;
     delay_unlock_obj->list_start = new_elt;
     new_elt->next = temp_next;
     MOSAL_spinlock_unlock(&delay_unlock_obj->spl);
     
     return 0;
 }

 int VIP_delay_unlock_destroy(VIP_delay_unlock_t delay_unlock_obj)
 {
     VIP_delay_unlock_elem_t * found_elt = NULL;
     VIP_delay_unlock_elem_t * next_elt = NULL;

     if (delay_unlock_obj == NULL) {
         return -1; /* invalid argument */
     }
     /* make list invalid */
     MOSAL_spinlock_irq_lock(&delay_unlock_obj->spl);
     if (delay_unlock_obj->is_valid == FALSE) {
         /* list has been deleted */
         MOSAL_spinlock_unlock(&delay_unlock_obj->spl);
         return -2;
     }
     delay_unlock_obj->is_valid = FALSE;
     found_elt = delay_unlock_obj->list_start;
     MOSAL_spinlock_unlock(&delay_unlock_obj->spl);

     /* need no more spinlocks */
     while(found_elt != NULL) {
         next_elt  = found_elt->next;
         MOSAL_iobuf_deregister(found_elt->iobuf);
         MTL_DEBUG1("%s: DEFERRED unlock iobuf=0x%p\n", 
                  __func__, found_elt->iobuf );
         FREE(found_elt);
         found_elt = next_elt;
     }
     FREE(delay_unlock_obj);

     return 0;
 }






