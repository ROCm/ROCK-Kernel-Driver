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

#ifndef VIP_COMMON_VIP_DELAY_UNLOCK_H
#define VIP_COMMON_VIP_DELAY_UNLOCK_H


#include <vapi_types.h>
#include <mosal.h>
 

typedef struct VIP_delay_unlock_st * VIP_delay_unlock_t;


int VIP_delay_unlock_create(VIP_delay_unlock_t * delay_unlock_obj_p);

int VIP_delay_unlock_insert(VIP_delay_unlock_t delay_unlock_obj,MOSAL_iobuf_t iobuf);
int VIP_delay_unlock_destroy(VIP_delay_unlock_t delay_unlock_obj);


#endif


