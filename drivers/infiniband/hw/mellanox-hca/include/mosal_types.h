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

#ifndef H_MOSAL_TYPES_H
#define H_MOSAL_TYPES_H

#include <linux/ptrace.h>
#include <asm/ptrace.h>	

/* structure for double-linked lists */
typedef struct _LIST_ENTRY {
   struct _LIST_ENTRY *Flink;
   struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;


struct Entry_node_st {
    MT_phys_addr_t  phys_addr;
    MT_virt_addr_t  virt_addr;
};

struct Table_entry_st {
    struct Entry_node_st * pval;
    struct Table_entry_st* next;
};

/*
 * MOSAL physical addresses access
 * -------------------------------
 */

/* Calculates the number of entry in Page_table for the given physical address */
/* Returnes u_int32_t */
#define MOSAL_PAGE_ENTRY(phys_addr) ( ((phys_addr) & MAKE_ULONGLONG(0x000FF000)) >> 12 )

/* Returns the base address (physical) of the page for a given address */
/* Returnes MT_phys_addr_t */
#define MOSAL_PAGE_BASE(phys_addr) ( (MT_phys_addr_t)((phys_addr) & MAKE_ULONGLONG(0xFFFFFFFFFFFFF000)) )

#define MOSAL_PAGE_SIZE 4096
#define MOSAL_TABLE_SIZE 256


typedef int MOSAL_shmid_t;






/*****************************************************************************************
* Name:
*              MOSAL_free_node (macro)
* Description:
*              Frees the memory allocated for the node
*
* Parameters:
*              node(IN): the node to be freed
* Return value:
*
******************************************************************************************/
#ifdef __KERNEL__
#define MOSAL_free_node(node)  {iounmap((u_int8_t*)((node)->pval->virt_addr)); FREE((node)->pval); FREE(node);}
#endif


#endif

