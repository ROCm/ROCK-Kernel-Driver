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

#include <mosal_wrap.h>
/* #include "vipkl_hpool.c" - obsolete */
#include <linux/errno.h>
#include <mosal_priv.h>
#include <mosal_k2u_cbk_priv.h>

/* 
	Wrapper functions' implementation
	-----------------------------

1. Main ideas
	- user-level wrapper serializes all input parameters into an INPUT buffer; output ones -into an OUPUT buffer;
	- pointers are never relayed: if a parameter is a pointer to a structure/buffer, 
	   the structure/buffer is copied to the INPUT buffer;
	- kernel function return code is always relayed as the first field of the OUTPUT buffer;
	- the INPUT/OUTPUT buffers are relayed to/from kernel by an OS-dependent function;

2. Implementation detailes
	- user-level wrapper use the following
	  a) conventions:
	  	-- for function XXX, 
	  		INPUT buffer has 'struct i_XXX_ops_t' type,
	  		OUTPUT buffer has 'struct i_XXX_ops_t' type;
	  	-- if a function parameter is pointer to a structure or array, it must be named YYY_p and 
	  	    the appropriate field of INPUT or OUTPUT buffer must be named YYY;
	  	-- if a function parameter is a value of an integral type, its name must be the same, as the 
	  	    name of the appropriate INPUT buffer field;
	  	    
	  b) local variables:
	  	-- pi 	- pointer to the INPUT buffer of 'struct i_<func_name>_ops_t *' type
	  	-- pi_sz	- size of the INPUT buffer;
	  	-- po 	- pointer to the OUTPUT buffer of 'struct o_<func_name>_ops_t *' type
	  	-- pi_sz	- size of the OUTPUT buffer;

	- in accord to above conventions, kernel wrapper functions have the prototype (say, for function XXX):
		static call_result_t XXX_stat(				// function name; has suffuix '_stat', shown in prints					
			IN	struct i_XXX_ops_t * pi, 			// typed pointer to the INPUT buffer
			IN	struct o_XXX_ops_t * po, 			// typed pointer to the OUTPUT buffer
			OUT	u_int32_t* ret_po_sz_p				// returned size the OUTPUT buffer (see more below)
			);

	- an OS-dependent kernel function is responsible for relaying the INPUT and OUTPUT buffers, prepared by
	   by the user-level wrapper, into the kernel. It may do that by zero-copying technology, just mapping them 
	   into kernel. But for generality we assume, that the kernel function allocates buffers in kernel space and 
	   copies buffers to/from user space. Because of this assumption comes the last parameter of the wrapper
	   functions, that tells what part of the OUTPUT buffer (from the beginning) is to be copied back to user space.

	- the wrapper functions are written with the help of macros, shortly documented below;   
*/

/* return code of the kernel function */
#define W_RET					po->ret

/* Generic input parameters size check (FM #18007) */                                                   
#define W_IPARAM_SZ_CHECK_DEFAULT(name)                                                           \
  if (sizeof(struct i_##name##_ops_t) > pi_sz) {                                                  \
     MTL_ERROR1(MT_FLFMT("%s: Input parameters buffer is too small "                              \
      "(sizeof(i_"#name"_ops_t)=%u > pi_sz=%u)"),                                                 \
      __func__, (unsigned int)sizeof(struct i_##name##_ops_t), pi_sz);  \
     return MT_EINVAL;                                                                     \
  }

/* Generic output parameters size check (FM #18007) */                                                   
#define W_OPARAM_SZ_CHECK_DEFAULT(name)                                                           \
  if (sizeof(struct o_##name##_ops_t) > po_sz) {                                                  \
     MTL_ERROR1(MT_FLFMT("%s: Output parameters buffer is too small "                             \
      "(sizeof(o_"#name"_ops_t)=%u > po_sz=%u)"),       \
      __func__, (unsigned int)sizeof(struct o_##name##_ops_t), po_sz);  \
     return MT_EINVAL;                                                                     \
  }

/* Size check for input buffer which includes variable size field (array[1]) */
#define W_IPARAM_SZ_CHECK_VARSIZE_BASE(name,vardata,var_size)                                              \
  if (FIELD_OFFSET(struct i_##name##_ops_t, vardata) + pi-> var_size > pi_sz) {                                   \
     MTL_ERROR1(MT_FLFMT("%s: Input parameters buffer is too small "                              \
      "(FIELD_OFFSET(struct i_"#name"_ops_t, "#vardata")=%lu, "#var_size"=%lu > pi_sz=%u)"),                       \
      __func__, FIELD_OFFSET(struct i_##name##_ops_t, vardata), (long unsigned int)pi-> var_size, pi_sz);                           \
     return MT_EINVAL;                                                                     \
  }

/* Size check for output buffer which includes variable size field (array[1]) */
#define W_OPARAM_SZ_CHECK_VARSIZE_BASE(name,vardata,var_size)                                              \
  if (FIELD_OFFSET(struct o_##name##_ops_t, vardata) + pi-> var_size > po_sz) {                                   \
     MTL_ERROR1(MT_FLFMT("%s: Output parameters buffer is too small "                             \
      "(FIELD_OFFSET(struct o_"#name"_ops_t, "#vardata")=%lu, "#var_size"=%lu > pi_sz=%u)"),                       \
      __func__, FIELD_OFFSET(struct o_##name##_ops_t, vardata), (long unsigned int)pi-> var_size, pi_sz);                           \
     return MT_EINVAL;                                                                     \
  }
  

#define W_DEFAULT_PARAM_SZ_CHECK(name)      \
  W_IPARAM_SZ_CHECK_DEFAULT(name)           \
  W_OPARAM_SZ_CHECK_DEFAULT(name)

/* create function prototype */
#define W_FUNC_PROTOTYPE(name)        \
static int name##_stat(               \
  struct i_##name##_ops_t * pi,       \
  u_int32_t pi_sz,                    \
  struct o_##name##_ops_t * po,       \
  u_int32_t po_sz,                    \
  u_int32_t* ret_po_sz_p)             \
{


/* create function prototype and print it name by MTL_DEBUG3 */
#define W_FUNC_START(name)                  \
  W_FUNC_PROTOTYPE(name)                    \
  MTL_DEBUG3("CALLING " #name "_stat \n");  \
  W_DEFAULT_PARAM_SZ_CHECK(name)
  
/* Used when one needs to use local variables in the wrapper function */
/* Use W_DEFAULT_PARAM_SZ_CHECK after local variables are declared    */
#define W_FUNC_START_WO_PRINT(name)   W_FUNC_PROTOTYPE(name)              

#define W_FUNC_START_WO_PARAM_SZ_CHECK(name)  \
  W_FUNC_PROTOTYPE(name)                      \
  MTL_DEBUG3("CALLING " #name "_stat \n");  
  
#define W_FUNC_START_W_IPARAM_SZ_CHECK(name)  \
  W_FUNC_PROTOTYPE(name)                      \
  MTL_DEBUG3("CALLING " #name "_stat \n");  \
  W_IPARAM_SZ_CHECK_DEFAULT(name);           
  
#define W_FUNC_START_W_OPARAM_SZ_CHECK(name)  \
  W_FUNC_PROTOTYPE(name)                      \
  MTL_DEBUG3("CALLING " #name "_stat \n");  \
  W_OPARAM_SZ_CHECK_DEFAULT(name);           
  


/* print function name by MTL_DEBUG3. Used when one uses more local variables in the wrapper function */
#define W_FUNC_NAME(name)  {MTL_DEBUG3("CALLING " #name "_stat \n");}

/* This return code is the wrapper admin. return code - 
 *  not W_RET, which is the result of the MTKL call.
 * If we reached here, the wrapper operation was OK (parameters were passed successfully) */
#define W_FUNC_END  return MT_OK; }

/* return call_result and end the function */
#define W_FUNC_END_RC_SZ(name)                                                               \
	if ((W_RET != MT_OK)	&& (po_sz >= sizeof(call_result_t)))  *ret_po_sz_p = sizeof(call_result_t);  \
	if (W_RET) {	\
	    	MTL_DEBUG4(MT_FLFMT(#name ": func_rc=" U64_FMT ", pi_sz=%u, po_sz=%u"), (u_int64_t)W_RET, pi_sz, po_sz); \
	} \
	return MT_OK; }



/* These inline static functions decode parameters
 * and delegate each to the appropriate MOSAL KL API */

/*****************************************
  struct i_MOSAL_MPC860_present_ops_t {
    IN int dummy;
  };
  struct o_MOSAL_MPC860_present_ops_t {
    OUT bool ret;
  };
******************************************/
/* bool MOSAL_MPC860_present( void ) */
W_FUNC_START_W_OPARAM_SZ_CHECK(MOSAL_MPC860_present)
  	W_RET = MOSAL_MPC860_present();
W_FUNC_END

/*****************************************
  struct i_MOSAL_MPC860_read_ops_t {
    IN u_int32_t addr;
    IN u_int32_t size;
  };
  struct o_MOSAL_MPC860_read_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t data[1];
  };
******************************************/
/* call_result_t MOSAL_MPC860_read( u_int32_t addr, u_int32_t size, void * data_p ) */
W_FUNC_START_W_IPARAM_SZ_CHECK(MOSAL_MPC860_read)
	W_OPARAM_SZ_CHECK_VARSIZE_BASE(MOSAL_MPC860_read,data,size)
  	W_RET = MOSAL_MPC860_read(pi->addr, pi->size, &po->data[0]);
W_FUNC_END_RC_SZ(MOSAL_MPC860_read)

/*****************************************
  struct i_MOSAL_MPC860_write_ops_t {
    IN u_int32_t addr;
    IN u_int32_t size;
    IN u_int8_t data[1];
  };
  struct o_MOSAL_MPC860_write_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_MPC860_write( u_int32_t addr, u_int32_t size, void * data_p ) */
W_FUNC_START_W_OPARAM_SZ_CHECK(MOSAL_MPC860_write)
	W_IPARAM_SZ_CHECK_VARSIZE_BASE(MOSAL_MPC860_write,data,size)
  	W_RET = MOSAL_MPC860_write(pi->addr, pi->size, &pi->data[0]);
W_FUNC_END_RC_SZ(MOSAL_MPC860_write)

/*****************************************
  struct i_MOSAL_PCI_find_class_ops_t {
    IN u_int32_t class_code;
    IN u_int16_t index;
  };
  struct o_MOSAL_PCI_find_class_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t bus;
    OUT u_int8_t dev_func;
  };
******************************************/
/* call_result_t MOSAL_PCI_find_class( u_int32_t class_code, u_int16_t index, u_int8_t * bus_p, u_int8_t * dev_func_p ) */
W_FUNC_START(MOSAL_PCI_find_class)
  	W_RET = MOSAL_PCI_find_class(pi->class_code, pi->index, &po->bus, &po->dev_func);
W_FUNC_END_RC_SZ(MOSAL_PCI_find_class)

/*****************************************
  struct i_MOSAL_PCI_find_device_ops_t {
    IN u_int16_t vendor_id;
    IN u_int16_t dev_id;
    IN u_int16_t index;
  };
  struct o_MOSAL_PCI_find_device_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t bus;
    OUT u_int8_t dev_func;
  };
******************************************/
/* call_result_t MOSAL_PCI_find_device( u_int16_t vendor_id, u_int16_t dev_id, u_int16_t index, u_int8_t * bus_p, u_int8_t * dev_func_p ) */
W_FUNC_START(MOSAL_PCI_find_device)
  	W_RET = MOSAL_PCI_find_device(pi->vendor_id, pi->dev_id, pi->index, &po->bus, &po->dev_func);
W_FUNC_END_RC_SZ(MOSAL_PCI_find_device)

/*****************************************
  struct i_MOSAL_PCI_get_cfg_hdr_ops_t {
    IN u_int8_t bus;
    IN u_int8_t dev_func;
  };
  struct o_MOSAL_PCI_get_cfg_hdr_ops_t {
    OUT call_result_t ret;
    OUT MOSAL_PCI_cfg_hdr_t cfg_hdr;
  };
******************************************/
/* call_result_t MOSAL_PCI_get_cfg_hdr( u_int8_t bus, u_int8_t dev_func, MOSAL_PCI_cfg_hdr_t * cfg_hdr_p ) */
W_FUNC_START(MOSAL_PCI_get_cfg_hdr)
  	W_RET = MOSAL_PCI_get_cfg_hdr(pi->pci_dev, &po->cfg_hdr);
W_FUNC_END_RC_SZ(MOSAL_PCI_get_cfg_hdr)

/*****************************************
  struct i_MOSAL_PCI_present_ops_t {
    IN int dummy;
  };
  struct o_MOSAL_PCI_present_ops_t {
    OUT bool ret;
  };
******************************************/
/* bool MOSAL_PCI_present( void ) */
W_FUNC_START_W_OPARAM_SZ_CHECK(MOSAL_PCI_present)
  	W_RET = MOSAL_PCI_present();
W_FUNC_END

/*****************************************
  struct i_MOSAL_PCI_read_config_byte_ops_t {
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_read_config_byte_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t data;
  };
******************************************/
/* call_result_t MOSAL_PCI_read_config_byte( u_int8_t bus, u_int8_t dev_func, u_int8_t offset, u_int8_t * data_p ) */
W_FUNC_START(MOSAL_PCI_read_config_byte)
  	W_RET = MOSAL_PCI_read_config_byte(pi->bus, pi->dev_func, pi->offset, &po->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_read_config_byte)

/*****************************************
  struct i_MOSAL_PCI_read_config_word_ops_t {
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_read_config_word_ops_t {
    OUT call_result_t ret;
    OUT u_int16_t data;
  };
******************************************/
/* call_result_t MOSAL_PCI_read_config_word( u_int8_t bus, u_int8_t dev_func, u_int8_t offset, u_int16_t * data_p ) */
W_FUNC_START(MOSAL_PCI_read_config_word)
  	W_RET = MOSAL_PCI_read_config_word(pi->bus, pi->dev_func, pi->offset, &po->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_read_config_word)

/*****************************************
  struct i_MOSAL_PCI_read_config_dword_ops_t {
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_read_config_dword_ops_t {
    OUT call_result_t ret;
    OUT u_int32_t data;
  };
******************************************/
/* call_result_t MOSAL_PCI_read_config_dword( u_int8_t bus, u_int8_t dev_func, u_int8_t offset, u_int32_t * data_p ) */
W_FUNC_START(MOSAL_PCI_read_config_dword)
  	W_RET = MOSAL_PCI_read_config_dword(pi->bus, pi->dev_func, pi->offset, &po->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_read_config_dword)

/*****************************************
  struct i_MOSAL_PCI_read_io_byte_ops_t {
    IN u_int32_t addr;
  };
  struct o_MOSAL_PCI_read_io_byte_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t data;
  };
******************************************/
/* call_result_t MOSAL_PCI_read_io_byte( u_int32_t addr, u_int8_t * data_p ) */
W_FUNC_START(MOSAL_PCI_read_io_byte)
  	W_RET = MOSAL_PCI_read_io_byte(pi->addr, &po->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_read_io_byte)

/*****************************************
  struct i_MOSAL_PCI_read_io_word_ops_t {
    IN u_int32_t addr;
  };
  struct o_MOSAL_PCI_read_io_word_ops_t {
    OUT call_result_t ret;
    OUT u_int16_t data;
  };
******************************************/
/* call_result_t MOSAL_PCI_read_io_word( u_int32_t addr, u_int16_t * data_p ) */
W_FUNC_START(MOSAL_PCI_read_io_word)
  	W_RET = MOSAL_PCI_read_io_word(pi->addr, &po->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_read_io_word)

/*****************************************
  struct i_MOSAL_PCI_read_io_dword_ops_t {
    IN u_int32_t addr;
  };
  struct o_MOSAL_PCI_read_io_dword_ops_t {
    OUT call_result_t ret;
    OUT u_int32_t data;
  };
******************************************/
/* call_result_t MOSAL_PCI_read_io_dword( u_int32_t addr, u_int32_t * data_p ) */
W_FUNC_START(MOSAL_PCI_read_io_dword)
  	W_RET = MOSAL_PCI_read_io_dword(pi->addr, &po->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_read_io_dword)

/*****************************************
  struct i_MOSAL_PCI_write_config_byte_ops_t {
    IN u_int8_t data;
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_write_config_byte_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_PCI_write_config_byte( u_int8_t bus, u_int8_t dev_func, u_int8_t offset, u_int8_t data ) */
W_FUNC_START(MOSAL_PCI_write_config_byte)
  	W_RET = MOSAL_PCI_write_config_byte(pi->bus, pi->dev_func, pi->offset, pi->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_write_config_byte)

/*****************************************
  struct i_MOSAL_PCI_write_config_word_ops_t {
    IN u_int16_t data;
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_write_config_word_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_PCI_write_config_word( u_int8_t bus, u_int8_t dev_func, u_int8_t offset, u_int16_t data ) */
W_FUNC_START(MOSAL_PCI_write_config_word)
  	W_RET = MOSAL_PCI_write_config_word(pi->bus, pi->dev_func, pi->offset, pi->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_write_config_word)

/*****************************************
  struct i_MOSAL_PCI_write_config_dword_ops_t {
    IN u_int32_t data;
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_write_config_dword_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_PCI_write_config_dword( u_int8_t bus, u_int8_t dev_func, u_int8_t offset, u_int32_t data ) */
W_FUNC_START(MOSAL_PCI_write_config_dword)
  	W_RET = MOSAL_PCI_write_config_dword(pi->bus, pi->dev_func, pi->offset, pi->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_write_config_dword)

/*****************************************
  struct i_MOSAL_PCI_write_io_byte_ops_t {
    IN u_int32_t addr;
    IN u_int8_t data;
  };
  struct o_MOSAL_PCI_write_io_byte_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_PCI_write_io_byte( u_int32_t addr, u_int8_t data ) */
W_FUNC_START(MOSAL_PCI_write_io_byte)
  	W_RET = MOSAL_PCI_write_io_byte(pi->addr, pi->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_write_io_byte)

/*****************************************
  struct i_MOSAL_PCI_write_io_word_ops_t {
    IN u_int32_t addr;
    IN u_int16_t data;
  };
  struct o_MOSAL_PCI_write_io_word_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_PCI_write_io_word( u_int32_t addr, u_int16_t data ) */
W_FUNC_START(MOSAL_PCI_write_io_word)
  	W_RET = MOSAL_PCI_write_io_word(pi->addr, pi->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_write_io_word)

/*****************************************
  struct i_MOSAL_PCI_write_io_dword_ops_t {
    IN u_int32_t addr;
    IN u_int32_t data;
  };
  struct o_MOSAL_PCI_write_io_dword_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_PCI_write_io_dword( u_int32_t addr, u_int32_t data ) */
W_FUNC_START(MOSAL_PCI_write_io_dword)
  	W_RET = MOSAL_PCI_write_io_dword(pi->addr, pi->data);
W_FUNC_END_RC_SZ(MOSAL_PCI_write_io_dword)

/*****************************************
  struct i_MOSAL_get_counts_per_sec_ops_t {
    IN int dummy;
  };
  struct o_MOSAL_get_counts_per_sec_ops_t {
    OUT u_int64_t ret;
  };
******************************************/
/* u_int64_t MOSAL_get_counts_per_sec( void ) */
W_FUNC_START_W_OPARAM_SZ_CHECK(MOSAL_get_counts_per_sec)
  	W_RET = MOSAL_get_counts_per_sec();
W_FUNC_END

/*****************************************
  struct i_MOSAL_get_page_shift_ops_t {
    IN MT_virt_addr_t va;
    IN MOSAL_prot_ctx_t prot_ctx;
  };
  struct o_MOSAL_get_page_shift_ops_t {
    OUT call_result_t ret;
    OUT unsigned int page_shift;
  };
******************************************/
/* call_result_t MOSAL_get_page_shift( MOSAL_prot_ctx_t prot_ctx, MT_virt_addr_t va, unsigned * page_shift_p ) */
W_FUNC_START(MOSAL_get_page_shift)
  	W_RET = MOSAL_get_page_shift(pi->prot_ctx, pi->va, &po->page_shift);
W_FUNC_END_RC_SZ(MOSAL_get_page_shift)

/*****************************************
  struct i_MOSAL_is_privileged_ops_t {
    IN int dummy;
  };
  struct o_MOSAL_is_privileged_ops_t {
    OUT MT_bool ret;
  };
******************************************/
/* MT_bool MOSAL_is_privileged( void ) */
W_FUNC_START_W_OPARAM_SZ_CHECK(MOSAL_is_privileged)
  	W_RET = MOSAL_is_privileged();
W_FUNC_END
	
/*****************************************
  struct i_MOSAL_map_phys_addr_ops_t {
    IN MT_phys_addr_t pa;
    IN MT_size_t size;
    IN MOSAL_prot_ctx_t prot_ctx;
    IN MOSAL_mem_flags_t flags;
  };
  struct o_MOSAL_map_phys_addr_ops_t {
    OUT MT_virt_addr_t ret;
  };
******************************************/
/* MT_virt_addr_t MOSAL_map_phys_addr( MT_phys_addr_t pa, MT_size_t size, MOSAL_mem_flags_t flags, MOSAL_prot_ctx_t prot_ctx ) */
W_FUNC_START(MOSAL_map_phys_addr)
  	W_RET = MOSAL_map_phys_addr(pi->pa, pi->size, pi->flags, pi->prot_ctx);
W_FUNC_END

/*****************************************
  struct i_MOSAL_unmap_phys_addr_ops_t {
    IN MT_virt_addr_t va;
    IN MT_size_t size;
    IN MOSAL_prot_ctx_t prot_ctx;
  };
  struct o_MOSAL_unmap_phys_addr_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_unmap_phys_addr( MOSAL_prot_ctx_t prot_ctx, MT_virt_addr_t va, MT_size_t size ) */
W_FUNC_START(MOSAL_unmap_phys_addr)
  	W_RET = MOSAL_unmap_phys_addr(pi->prot_ctx, pi->va, pi->size);
W_FUNC_END_RC_SZ(MOSAL_unmap_phys_addr)

/*****************************************
  struct i_MOSAL_mlock_ops_t {
    IN MT_virt_addr_t va;
    IN MT_size_t size;
  };
  struct o_MOSAL_mlock_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_mlock( MT_virt_addr_t va, MT_size_t size ) */
W_FUNC_START(MOSAL_mlock)
  	W_RET = MOSAL_mlock(pi->va, pi->size);
W_FUNC_END_RC_SZ(MOSAL_mlock)

/*****************************************
  struct i_MOSAL_munlock_ops_t {
    IN MT_virt_addr_t va;
    IN MT_size_t size;
  };
  struct o_MOSAL_munlock_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_munlock( MT_virt_addr_t va, MT_size_t size ) */
W_FUNC_START(MOSAL_munlock)
  	W_RET = MOSAL_munlock(pi->va, pi->size);
W_FUNC_END_RC_SZ(MOSAL_munlock)

/*****************************************
  struct i_MOSAL_virt_to_phys_ops_t {
    IN MT_virt_addr_t va;
    IN MOSAL_prot_ctx_t prot_ctx;
  };
  struct o_MOSAL_virt_to_phys_ops_t {
    OUT MT_phys_addr_t pa;
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_virt_to_phys( MOSAL_prot_ctx_t prot_ctx, const MT_virt_addr_t va, MT_phys_addr_t * pa_p ) */
W_FUNC_START(MOSAL_virt_to_phys)
  	W_RET = MOSAL_virt_to_phys(pi->prot_ctx, pi->va, &po->pa);
W_FUNC_END

/*****************************************
  struct i_k2u_cbk_pollq_ops_t {
    IN k2u_cbk_hndl_t k2u_cbk_h;
  };
  struct o_k2u_cbk_pollq_ops_t {
    OUT MT_size_t size;
    OUT call_result_t ret;
    OUT k2u_cbk_id_t cbk_id;
    OUT u_int8_t data[MAX_CBK_DATA_SZ];
  };
******************************************/
/* call_result_t k2u_cbk_pollq( k2u_cbk_hndl_t k2u_cbk_h, k2u_cbk_id_t * cbk_id_p, void * data_p, MT_size_t * size_p ) */
W_FUNC_START(k2u_cbk_pollq)
  	W_RET = k2u_cbk_pollq(pi->k2u_cbk_h, &po->cbk_id, &po->data[0], &po->size);
W_FUNC_END

/*****************************************
  struct i_mtl_log_set_ops_t {
    IN char layer[MAX_MTL_LOG_LAYER];
    IN char info[MAX_MTL_LOG_INFO];
  };
  struct o_mtl_log_set_ops_t {
    IN int dummy;
  };
******************************************/
/* void mtl_log_set( char * layer, char * info ) */
W_FUNC_START_W_IPARAM_SZ_CHECK(mtl_log_set)
  	mtl_log_set(&pi->layer[0], &pi->info[0]);
W_FUNC_END
	
#ifdef __WIN__  

/*****************************************
  struct i_MOSAL_PCI_read_config_data_ops_t {
    IN u_int32_t size;
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_read_config_data_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t data[1];
  };
******************************************/
/* call_result_t MOSAL_PCI_read_config_data( u_int8_t bus, u_int8_t dev_func, u_int8_t offset, u_int32_t size, u_int8_t * data_p ) */
W_FUNC_START_W_IPARAM_SZ_CHECK(MOSAL_PCI_read_config_data)
	W_OPARAM_SZ_CHECK_VARSIZE_BASE(MOSAL_PCI_read_config_data,data,size)
  	W_RET = MOSAL_PCI_read_config_data(pi->bus, pi->dev_func, pi->offset, pi->size, &po->data[0]);
W_FUNC_END_RC_SZ(MOSAL_PCI_read_config_data)

/*****************************************
  struct i_MOSAL_PCI_write_config_data_ops_t {
    IN u_int32_t size;
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
    IN u_int8_t data[1];
  };
  struct o_MOSAL_PCI_write_config_data_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_PCI_write_config_data( u_int8_t bus, u_int8_t dev_func, u_int8_t offset, u_int32_t size, u_int8_t * data_p ) */
W_FUNC_START_W_OPARAM_SZ_CHECK(MOSAL_PCI_write_config_data)
	W_IPARAM_SZ_CHECK_VARSIZE_BASE(MOSAL_PCI_write_config_data,data,size)
  	W_RET = MOSAL_PCI_write_config_data(pi->bus, pi->dev_func, pi->offset, pi->size, &pi->data[0]);
W_FUNC_END_RC_SZ(MOSAL_PCI_write_config_data)

/*****************************************
  struct i_MOSAL_PCI_read_mem_ops_t {
    IN u_int64_t addr;
    IN u_int64_t size;
  };
  struct o_MOSAL_PCI_read_mem_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t data[1];
  };
******************************************/
/* call_result_t MOSAL_PCI_read_mem( u_int64_t addr, u_int64_t size, void * data_p ) */
W_FUNC_START_W_IPARAM_SZ_CHECK(MOSAL_PCI_read_mem)
	W_OPARAM_SZ_CHECK_VARSIZE_BASE(MOSAL_PCI_read_mem,data,size)
  	W_RET = MOSAL_PCI_read_mem(pi->addr, pi->size, &po->data[0]);
W_FUNC_END_RC_SZ(MOSAL_PCI_read_mem)

/*****************************************
  struct i_MOSAL_PCI_write_mem_ops_t {
    IN u_int64_t addr;
    IN u_int64_t size;
    IN u_int8_t data[1];
  };
  struct o_MOSAL_PCI_write_mem_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_PCI_write_mem( u_int64_t addr, u_int64_t size, void * data_p ) */
W_FUNC_START_W_OPARAM_SZ_CHECK(MOSAL_PCI_write_mem)
	W_IPARAM_SZ_CHECK_VARSIZE_BASE(MOSAL_PCI_write_mem,data,size)
  	W_RET = MOSAL_PCI_write_mem(pi->addr, pi->size, &pi->data[0]);
W_FUNC_END_RC_SZ(MOSAL_PCI_write_mem)

/*****************************************
  struct i_MOSAL_io_remap_for_user_ops_t {
    IN MT_phys_addr_t pa;
    IN MT_size_t size;
  };
  struct o_MOSAL_io_remap_for_user_ops_t {
    OUT MT_virt_addr_t ret;
  };
******************************************/
/* MT_virt_addr_t MOSAL_io_remap_for_user( MT_phys_addr_t pa, MT_size_t size ) */
W_FUNC_START(MOSAL_io_remap_for_user)
  	W_RET = MOSAL_io_remap_for_user(pi->pa, pi->size);
W_FUNC_END

/*****************************************
  struct o_MOSAL_io_release_for_user_ops_t {
    IN int dummy;
  };
  struct i_MOSAL_io_release_for_user_ops_t {
    IN MT_phys_addr_t pa;
  };
******************************************/
/* void MOSAL_io_release_for_user( MT_phys_addr_t pa ) */
W_FUNC_START_W_IPARAM_SZ_CHECK(MOSAL_io_release_for_user)
  	MOSAL_io_release_for_user(pi->pa);
W_FUNC_END

/*****************************************
  struct i_MOSAL_phys_ctg_get_for_user_ops_t {
    IN MT_size_t size;
  };
  struct o_MOSAL_phys_ctg_get_for_user_ops_t {
    OUT MT_virt_addr_t ret;
  };
******************************************/
/* MT_virt_addr_t MOSAL_phys_ctg_get_for_user( MT_size_t size ) */
W_FUNC_START(MOSAL_phys_ctg_get_for_user)
  	W_RET = MOSAL_phys_ctg_get_for_user(pi->size);
W_FUNC_END

/*****************************************
  struct i_MOSAL_phys_ctg_free_for_user_ops_t {
    IN MT_virt_addr_t va;
  };
  struct o_MOSAL_phys_ctg_free_for_user_ops_t {
    IN int dummy;
  };
******************************************/
/* void MOSAL_phys_ctg_free_for_user( MT_virt_addr_t va ) */
W_FUNC_START_W_IPARAM_SZ_CHECK(MOSAL_phys_ctg_free_for_user)
  	MOSAL_phys_ctg_free_for_user(pi->va);
W_FUNC_END

/*****************************************
  struct i_MOSAL_MOSAL_nsecs_ops_t {
    IN int dummy;
  };
  struct o_MOSAL_nsecs_ops_t {
    OUT u_int64_t ret;
  };
******************************************/
/* u_int64_t MOSAL_nsecs( void ) */
W_FUNC_START_W_OPARAM_SZ_CHECK(MOSAL_nsecs)
  	W_RET = MOSAL_nsecs();
W_FUNC_END

#endif

#ifdef __LINUX__


/*****************************************
  struct i_MOSAL_I2C_open_ops_t {
    IN char name[I2C_MAX_DEV_NAME];
  };
  struct o_MOSAL_I2C_open_ops_t {
    OUT call_result_t ret;
    OUT MOSAL_i2c_devh_t dev_h;
  };
******************************************/
/* call_result_t MOSAL_I2C_open( char * name, MOSAL_i2c_devh_t * dev_h_p ) */
W_FUNC_START(MOSAL_I2C_open)
  	W_RET = MOSAL_I2C_open(&pi->name[0], &po->dev_h);
W_FUNC_END_RC_SZ(MOSAL_I2C_open)

/*****************************************
  struct i_MOSAL_I2C_close_ops_t {
    IN MOSAL_i2c_devh_t dev_h;
  };
  struct o_MOSAL_I2C_close_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_I2C_close( MOSAL_i2c_devh_t dev_h ) */
W_FUNC_START(MOSAL_I2C_close)
  	W_RET = MOSAL_I2C_close(pi->dev_h);
W_FUNC_END_RC_SZ(MOSAL_I2C_close)

/*****************************************
  struct i_MOSAL_I2C_read_ops_t {
    IN MOSAL_i2c_devh_t dev_h;
    IN u_int32_t addr;
    IN u_int32_t size;
    IN u_int16_t i2c_addr;
  };
  struct o_MOSAL_I2C_read_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t data[1];
  };
******************************************/
/* call_result_t MOSAL_I2C_read( MOSAL_i2c_devh_t dev_h, u_int16_t i2c_addr, u_int32_t addr, u_int8_t * data_p, u_int32_t size ) */
W_FUNC_START_W_IPARAM_SZ_CHECK(MOSAL_I2C_read)
	W_OPARAM_SZ_CHECK_VARSIZE_BASE(MOSAL_I2C_read,data,size)
  	W_RET = MOSAL_I2C_read(pi->dev_h, pi->i2c_addr, pi->addr, &po->data[0], pi->size);
W_FUNC_END_RC_SZ(MOSAL_I2C_read)

/*****************************************
  struct i_MOSAL_I2C_write_ops_t {
    IN MOSAL_i2c_devh_t dev_h;
    IN u_int32_t addr;
    IN u_int32_t size;
    IN u_int16_t i2c_addr;
    IN u_int8_t data[1];
  };
  struct o_MOSAL_I2C_write_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_I2C_write( MOSAL_i2c_devh_t dev_h, u_int16_t i2c_addr, u_int32_t addr, u_int8_t * data_p, u_int32_t size ) */
W_FUNC_START_W_OPARAM_SZ_CHECK(MOSAL_I2C_write)
	W_IPARAM_SZ_CHECK_VARSIZE_BASE(MOSAL_I2C_write,data,size)
  	W_RET = MOSAL_I2C_write(pi->dev_h, pi->i2c_addr, pi->addr, &pi->data[0], pi->size);
W_FUNC_END_RC_SZ(MOSAL_I2C_write)

/*****************************************
  struct i_MOSAL_I2C_master_receive_ops_t {
    IN MOSAL_i2c_devh_t dev_h;
    IN u_int32_t key;
    IN u_int16_t size;
    IN MT_bool sendSTOP;
    IN u_int8_t slv_addr;
  };
  struct o_MOSAL_I2C_master_receive_ops_t {
    OUT call_result_t ret;
    OUT u_int32_t bytes_received;
    OUT u_int8_t data[1];
  };
******************************************/
/* call_result_t MOSAL_I2C_master_receive( MOSAL_i2c_devh_t dev_h, u_int8_t slv_addr, void * data_p, u_int16_t size, u_int32_t * bytes_received_p, MT_bool sendSTOP, u_int32_t key ) */
W_FUNC_START_W_IPARAM_SZ_CHECK(MOSAL_I2C_master_receive)
	W_OPARAM_SZ_CHECK_VARSIZE_BASE(MOSAL_I2C_master_receive,data,size)
  	W_RET = MOSAL_I2C_master_receive(pi->dev_h, pi->slv_addr, &po->data[0], pi->size, &po->bytes_received, pi->sendSTOP, pi->key);
W_FUNC_END_RC_SZ(MOSAL_I2C_master_receive)

/*****************************************
  struct i_MOSAL_I2C_send_stop_ops_t {
    IN MOSAL_i2c_devh_t dev_h;
    IN u_int32_t key;
  };
  struct o_MOSAL_I2C_send_stop_ops_t {
    OUT call_result_t ret;
  };
******************************************/
/* call_result_t MOSAL_I2C_send_stop( MOSAL_i2c_devh_t dev_h, const u_int32_t key ) */
W_FUNC_START(MOSAL_I2C_send_stop)
  	W_RET = MOSAL_I2C_send_stop(pi->dev_h, pi->key);
W_FUNC_END_RC_SZ(MOSAL_I2C_send_stop)

#endif


/*************************************************************************
 * Implement system call by calling appropriate static function 
 *************************************************************************/
#define MOSAL_INVOKE_STAT_FUNC(func) ret= func##_stat((struct i_##func##_ops_t*)pi,pi_sz,(struct o_##func##_ops_t*)po,po_sz,ret_po_sz_p)

call_result_t MOSAL_ioctl(MOSAL_ops_t ops, void *pi, u_int32_t pi_sz, void *po, u_int32_t po_sz, u_int32_t* ret_po_sz_p )
{
  call_result_t ret = MT_OK;
  MTL_TRACE1("CALLED MOSAL_ioctl (ops %d, pi_sz %d, po_sz %d)\n", ops,  pi_sz, po_sz);
  *ret_po_sz_p = po_sz;
  switch (ops) {
    case MOSAL_MPC860_PRESENT:  MOSAL_INVOKE_STAT_FUNC(MOSAL_MPC860_present); break;
    case MOSAL_MPC860_READ:  MOSAL_INVOKE_STAT_FUNC(MOSAL_MPC860_read); break;
    case MOSAL_MPC860_WRITE:  MOSAL_INVOKE_STAT_FUNC(MOSAL_MPC860_write); break;
    
    case MOSAL_PCI_FIND_CLASS:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_find_class); break;
    case MOSAL_PCI_FIND_DEVICE:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_find_device); break;
    case MOSAL_PCI_GET_CFG_HDR:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_get_cfg_hdr); break;
    case MOSAL_PCI_PRESENT:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_present); break;
    
    case MOSAL_PCI_READ_CFG_BYTE:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_read_config_byte); break;
    case MOSAL_PCI_READ_CFG_WORD:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_read_config_word); break;
    case MOSAL_PCI_READ_CFG_DWORD:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_read_config_dword); break;
    
    case MOSAL_PCI_WRITE_CFG_BYTE:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_write_config_byte); break;
    case MOSAL_PCI_WRITE_CFG_WORD:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_write_config_word); break;
    case MOSAL_PCI_WRITE_CFG_DWORD:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_write_config_dword); break;
    
    case MOSAL_PCI_READ_IO_BYTE:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_read_io_byte); break;
    case MOSAL_PCI_READ_IO_WORD:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_read_io_word); break;
    case MOSAL_PCI_READ_IO_DWORD:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_read_io_dword); break;

    case MOSAL_PCI_WRITE_IO_BYTE:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_write_io_byte); break;
    case MOSAL_PCI_WRITE_IO_WORD:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_write_io_word); break;
    case MOSAL_PCI_WRITE_IO_DWORD:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_write_io_dword); break;
    
    case MOSAL_GET_COUNTS_PER_SEC:  MOSAL_INVOKE_STAT_FUNC(MOSAL_get_counts_per_sec); break;
    case MOSAL_GET_PAGE_SHIFT:  MOSAL_INVOKE_STAT_FUNC(MOSAL_get_page_shift); break;
    case MOSAL_IS_PRIVILIGED:  MOSAL_INVOKE_STAT_FUNC(MOSAL_is_privileged); break;
    
    case MOSAL_MAP_PHYS_ADDR:  MOSAL_INVOKE_STAT_FUNC(MOSAL_map_phys_addr); break;
    case MOSAL_UNMAP_PHYS_ADDR:     MOSAL_INVOKE_STAT_FUNC(MOSAL_unmap_phys_addr) ; break;
    
    case MOSAL_MLOCK:    MOSAL_INVOKE_STAT_FUNC(MOSAL_mlock); break;
    case MOSAL_MUNLOCK:      MOSAL_INVOKE_STAT_FUNC(MOSAL_munlock)  ; break;
    
    case MOSAL_VIRT_TO_PHYS:  MOSAL_INVOKE_STAT_FUNC(MOSAL_virt_to_phys); break;
    case MOSAL_K2U_CBK_POLLQ:  MOSAL_INVOKE_STAT_FUNC(k2u_cbk_pollq); break;
    case MOSAL_MTL_LOG_SET:  MOSAL_INVOKE_STAT_FUNC(mtl_log_set); break;

#ifdef __WIN__  
    case MOSAL_PCI_READ_CFG_DATA:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_read_config_data); break;
    case MOSAL_PCI_WRITE_CFG_DATA:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_write_config_data); break;
    case MOSAL_PCI_READ_MEM:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_read_mem); break;
    case MOSAL_PCI_WRITE_MEM:  MOSAL_INVOKE_STAT_FUNC(MOSAL_PCI_write_mem); break;
    case MOSAL_IO_REMAP_FOR_USER:  MOSAL_INVOKE_STAT_FUNC(MOSAL_io_remap_for_user); break;
    case MOSAL_IO_RELEASE_FOR_USER:  MOSAL_INVOKE_STAT_FUNC(MOSAL_io_release_for_user); break;
    case MOSAL_PHYS_CTG_GET_FOR_USER:  MOSAL_INVOKE_STAT_FUNC(MOSAL_phys_ctg_get_for_user); break;
    case MOSAL_PHYS_CTG_FREE_FOR_USER:  MOSAL_INVOKE_STAT_FUNC(MOSAL_phys_ctg_free_for_user); break;
    case MOSAL_NSECS:  MOSAL_INVOKE_STAT_FUNC(MOSAL_nsecs); break;
#endif  

#ifdef __LINUX__  
    case MOSAL_I2C_OPEN:  MOSAL_INVOKE_STAT_FUNC(MOSAL_I2C_open); break;
    case MOSAL_I2C_CLOSE:  MOSAL_INVOKE_STAT_FUNC(MOSAL_I2C_close); break;
    case MOSAL_I2C_READ:  MOSAL_INVOKE_STAT_FUNC(MOSAL_I2C_read); break;
    case MOSAL_I2C_WRITE:  MOSAL_INVOKE_STAT_FUNC(MOSAL_I2C_write); break;
    case MOSAL_I2C_MASTER_RCV:  MOSAL_INVOKE_STAT_FUNC(MOSAL_I2C_master_receive); break;
    case MOSAL_I2C_SEND_STOP:  MOSAL_INVOKE_STAT_FUNC(MOSAL_I2C_send_stop); break;
#endif  


    default: ret = MT_EINVAL; break;
  }

  return ret;
}


