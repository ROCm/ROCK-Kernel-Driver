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

#ifndef H_MTL_TYPES_H
#define H_MTL_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#if (defined(__KERNEL__) || defined(KERNEL)) && ! defined(MT_KERNEL)
#define MT_KERNEL 1
#endif


  
#include <sys/mtl_sys_types.h>
#include <mtl_errno.h>

typedef unsigned char MT_bool;
typedef unsigned int MT_u_int_t;
#ifndef __cplusplus
/* avoid collision with curses.h */
#ifndef bool
#define bool MT_bool
#endif
#endif

#ifndef MT_KERNEL
#ifndef FALSE
#define FALSE 0
#undef TRUE
#define TRUE  (!FALSE)
#endif
#endif

#define IS_FALSE(b) ((b) == FALSE)
#define IS_TRUE(b)  ((b) != FALSE)

typedef enum{LOGIC_LOW = 0, LOGIC_HIGH = 1} logic_t;

typedef u_int32_t MT_dev_id_t;

#define EMPTY

#define MT_BUS_LIST \
MT_BUS_ELEM(MEM, 	=0, 	"Memory") \
MT_BUS_ELEM(PCI,   	EMPTY, 	"PCI")    \
MT_BUS_ELEM(I2C,   	EMPTY, 	"I2C")    \
MT_BUS_ELEM(MPC860, EMPTY,	"MPC860") \
MT_BUS_ELEM(SIM,   	EMPTY, 	"SIM")




typedef enum {
#define MT_BUS_ELEM(x,y,z) x y,
	MT_BUS_LIST
#undef  MT_BUS_ELEM
	MT_DUMMY_BUS
} MT_bus_t;


static inline const char* MT_strbus( MT_bus_t bustype)
{
		switch (bustype) {
#define MT_BUS_ELEM(A, B, C) case A: return C;
		MT_BUS_LIST
#undef  MT_BUS_ELEM
		default: return "Unknown bus";
		}
}




typedef void (*void_func_t)(void);
typedef void (*rx_func_t)(MT_virt_addr_t data, u_int32_t size, void *priv);

#ifndef NULL
#define NULL 0
#endif /*NULL*/

#ifdef __cplusplus
}
#endif

#endif /* H_MTL_TYPES_H */
