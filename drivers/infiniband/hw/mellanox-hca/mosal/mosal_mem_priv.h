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

#ifndef H_MOSAL_MEM_PRIV_H
#define H_MOSAL_MEM_PRIV_H

#ifndef	__KERNEL__
#ifdef __LINUX__
#include <sys/mman.h>
#endif
#endif

/*
 *  Memory attributes
 */
#define MEM_LOCKED    = (1 << 0)   /* Locked (non-pageable) */
#define MEM_ERROR     = (1 << 1)   /* Wrong condition */
#define MEM_DIRTY     = (1 << 2)   /* Dirty memory block */
#define MEM_DMA       = (1 << 3)   /* Supports DMA */
#define MEM_IO        = (1 << 4)   /* IO memory mapped */
#define MEM_IS_PAGE   = (1 << 5)   /* Represents memory page */
#define MEM_RESERVED  = (1 << 6)   /* Reserved (Inaccesible) */    
#define MEM_COPY      = (1 << 7)   /* Copy on write */
#define MEM_CONTG     = (1 << 8)   /* If buff buff */
#define MEM_CE        = (1 << 9)   /* Cache enable */
#define MEM_READ      = (1 << 10)  /* Readable */
#define MEM_WRITE     = (1 << 11)  /* Writable */
#define MEM_EXEC      = (1 << 12)  /* Executable */


/******************************************************************************************
* Name:
*               phys2virt
* Description:
*               Converts the physical address to virtual. If node for this page does not exists - creates it and puts
*        it at the begining of the list (of the appropriate entry in the table), otherwise , moves
*        the node in the begining of the list.
*
* Parameters:
*               phys_addr(IN)  : physical address
*               pvirt_addr(OUT): virtual address
* Return value:
*               Returns virtual address from PageTable
*               Actually it will always be MT_OK
* Remarks:
*               It's the programmer responsability to give the appropriate physical address.
*        Othervise the hardware exception may be caused.
******************************************************************************************/
call_result_t  phys2virt(const MT_phys_addr_t  phys_addr, MT_virt_addr_t * pvirt_addr);

#endif /* H_MOSAL_MEM_PRIV_H */
