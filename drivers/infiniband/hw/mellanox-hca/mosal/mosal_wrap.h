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

 #ifndef MOSAL_WRAP_H
#define MOSAL_WRAP_H

#include <mosal.h>
/* OS-dependent stuff, if any */
#include <mosal_wrap_imp.h>		

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

#ifndef FIELD_OFFSET
#define FIELD_OFFSET(type, field)    ((long)(int *)&(((type *)0)->field))
#endif


typedef enum {  /* Function selector for VIPKL system call */
  /* For each selector there is a function in VIP(KL) which the wrapper calls */
                        /* The called function */
                        /***********************/
  MOSAL_MPC860_PRESENT=MOSAL_FUNC_BASE,       /* MOSAL_MPC860_present */
  MOSAL_MPC860_READ,      /* MOSAL_MPC860_read */
  MOSAL_MPC860_WRITE,      /* MOSAL_MPC860_write */
  MOSAL_PCI_FIND_CLASS,      /* MOSAL_PCI_find_class */
  MOSAL_PCI_FIND_DEVICE,      /* MOSAL_PCI_find_device */
  MOSAL_PCI_GET_CFG_HDR,      /* MOSAL_PCI_get_cfg_hdr */
  MOSAL_PCI_PRESENT,      /* MOSAL_PCI_present */
  MOSAL_PCI_READ_CFG_BYTE,      /* MOSAL_PCI_read_config_byte */
  MOSAL_PCI_READ_CFG_WORD,      /* MOSAL_PCI_read_config_word */
  MOSAL_PCI_READ_CFG_DWORD,      /* MOSAL_PCI_read_config_dword */
  MOSAL_PCI_WRITE_CFG_BYTE,      /* MOSAL_PCI_write_config_byte */
  MOSAL_PCI_WRITE_CFG_WORD,      /* MOSAL_PCI_write_config_word */
  MOSAL_PCI_WRITE_CFG_DWORD,      /* MOSAL_PCI_write_config_dword */
  MOSAL_PCI_READ_IO_BYTE,      /* MOSAL_PCI_read_io_byte */
  MOSAL_PCI_READ_IO_WORD,      /* MOSAL_PCI_read_io_word */
  MOSAL_PCI_READ_IO_DWORD,      /* MOSAL_PCI_read_io_dword */
  MOSAL_PCI_WRITE_IO_BYTE,      /* MOSAL_PCI_write_io_byte */
  MOSAL_PCI_WRITE_IO_WORD,      /* MOSAL_PCI_write_io_word */
  MOSAL_PCI_WRITE_IO_DWORD,      /* MOSAL_PCI_write_io_dword */
  MOSAL_GET_COUNTS_PER_SEC,      /* MOSAL_get_counts_per_sec */
  MOSAL_GET_PAGE_SHIFT,      /* MOSAL_get_page_shift */
  MOSAL_IS_PRIVILIGED,      /* MOSAL_is_privileged */
  MOSAL_MAP_PHYS_ADDR,      /* MOSAL_map_phys_addr */
  MOSAL_UNMAP_PHYS_ADDR,      /* MOSAL_unmap_phys_addr */
  MOSAL_MLOCK,      /* MOSAL_mlock */
  MOSAL_MUNLOCK,      /* MOSAL_munlock */
  MOSAL_VIRT_TO_PHYS,      /* MOSAL_virt_to_phys */
  MOSAL_K2U_CBK_POLLQ,      /* k2u_cbk_pollq */
  MOSAL_MTL_LOG_SET,      /* mtl_log_set */

  
#ifdef __WIN__  
  MOSAL_PCI_READ_CFG_DATA,      /* MOSAL_PCI_read_config_data */
  MOSAL_PCI_WRITE_CFG_DATA,      /* MOSAL_PCI_write_config_data */
  MOSAL_PCI_READ_MEM,      /* MOSAL_PCI_read_mem */
  MOSAL_PCI_WRITE_MEM,      /* MOSAL_PCI_write_mem */
  MOSAL_NSECS,      /* MOSAL_nsecs */
  MOSAL_IO_REMAP_FOR_USER,      /* MOSAL_io_remap_for_user */
  MOSAL_IO_RELEASE_FOR_USER,      /* MOSAL_io_release_for_user */
  MOSAL_PHYS_CTG_GET_FOR_USER,      /* MOSAL_phys_ctg_get_for_user */
  MOSAL_PHYS_CTG_FREE_FOR_USER,      /* MOSAL_phys_ctg_free_for_user */
#endif  

#ifdef __LINUX__  
  MOSAL_I2C_OPEN,      /* MOSAL_I2C_open */
  MOSAL_I2C_CLOSE,      /* MOSAL_I2C_close */
  MOSAL_I2C_READ,      /* MOSAL_I2C_read */
  MOSAL_I2C_WRITE,      /* MOSAL_I2C_write */
  MOSAL_I2C_MASTER_RCV,      /* MOSAL_I2C_master_receive */
  MOSAL_I2C_SEND_STOP,      /* MOSAL_I2C_send_stop */
#endif  
  
  /* The following code must always be the last */
  MOSAL_MAX_OP
} MOSAL_ops_t;


/*************************************************************************
 * Function: MOSAL_MPC860_present
 *************************************************************************/ 
struct i_MOSAL_MPC860_present_ops_t {
    IN int dummy;
  };
struct o_MOSAL_MPC860_present_ops_t {
    OUT bool ret;
  };

/*************************************************************************
 * Function: MOSAL_MPC860_read
 *************************************************************************/ 
   struct i_MOSAL_MPC860_read_ops_t {
    IN u_int32_t addr;
    IN u_int32_t size;
  };
  struct o_MOSAL_MPC860_read_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t data[1];
  };

/*************************************************************************
 * Function: MOSAL_MPC860_write
 *************************************************************************/ 
  struct i_MOSAL_MPC860_write_ops_t {
    IN u_int32_t addr;
    IN u_int32_t size;
    IN u_int8_t data[1];
  };
  struct o_MOSAL_MPC860_write_ops_t {
    OUT call_result_t ret;
  };

/*************************************************************************
 * Function: MOSAL_PCI_find_class
 *************************************************************************/ 
   struct i_MOSAL_PCI_find_class_ops_t {
    IN u_int32_t class_code;
    IN u_int16_t index;
  };
  struct o_MOSAL_PCI_find_class_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t bus;
    OUT u_int8_t dev_func;
  };


/*************************************************************************
 * Function: MOSAL_PCI_find_device
 *************************************************************************/ 
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

/*************************************************************************
 * Function: MOSAL_PCI_get_cfg_hdr
 *************************************************************************/ 
 struct i_MOSAL_PCI_get_cfg_hdr_ops_t {
    IN MOSAL_pci_dev_t pci_dev;
  };
  struct o_MOSAL_PCI_get_cfg_hdr_ops_t {
    OUT call_result_t ret;
    OUT MOSAL_PCI_cfg_hdr_t cfg_hdr;
  };
 
/*************************************************************************
 * Function: MOSAL_PCI_present
 *************************************************************************/ 
  struct i_MOSAL_PCI_present_ops_t {
    IN int dummy;
  };
  struct o_MOSAL_PCI_present_ops_t {
    OUT bool ret;
  };

/*************************************************************************
 * Function: MOSAL_PCI_read_config_byte
 *************************************************************************/ 
  struct i_MOSAL_PCI_read_config_byte_ops_t {
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_read_config_byte_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t data;
  };

/*************************************************************************
 * Function: MOSAL_PCI_read_config_word
 *************************************************************************/ 
  struct i_MOSAL_PCI_read_config_word_ops_t {
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_read_config_word_ops_t {
    OUT call_result_t ret;
    OUT u_int16_t data;
  };

/*************************************************************************
 * Function: MOSAL_PCI_read_config_dword
 *************************************************************************/ 
  struct i_MOSAL_PCI_read_config_dword_ops_t {
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_read_config_dword_ops_t {
    OUT call_result_t ret;
    OUT u_int32_t data;
  };

/*************************************************************************
 * Function: MOSAL_PCI_read_io_byte
 *************************************************************************/ 
  struct i_MOSAL_PCI_read_io_byte_ops_t {
    IN u_int32_t addr;
  };
  struct o_MOSAL_PCI_read_io_byte_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t data;
  };

/*************************************************************************
 * Function: MOSAL_PCI_read_io_word
 *************************************************************************/ 
  struct i_MOSAL_PCI_read_io_word_ops_t {
    IN u_int32_t addr;
  };
  struct o_MOSAL_PCI_read_io_word_ops_t {
    OUT call_result_t ret;
    OUT u_int16_t data;
  };

/*************************************************************************
 * Function: MOSAL_PCI_read_io_dword
 *************************************************************************/ 
  struct i_MOSAL_PCI_read_io_dword_ops_t {
    IN u_int32_t addr;
  };
  struct o_MOSAL_PCI_read_io_dword_ops_t {
    OUT call_result_t ret;
    OUT u_int32_t data;
  };

/*************************************************************************
 * Function: MOSAL_PCI_write_config_byte
 *************************************************************************/ 
  struct i_MOSAL_PCI_write_config_byte_ops_t {
    IN u_int8_t data;
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_write_config_byte_ops_t {
    OUT call_result_t ret;
  };

/*************************************************************************
 * Function: MOSAL_PCI_write_config_word
 *************************************************************************/ 
  struct i_MOSAL_PCI_write_config_word_ops_t {
    IN u_int16_t data;
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_write_config_word_ops_t {
    OUT call_result_t ret;
  };

/*************************************************************************
 * Function: MOSAL_PCI_write_config_dword
 *************************************************************************/ 
  struct i_MOSAL_PCI_write_config_dword_ops_t {
    IN u_int32_t data;
    IN u_int8_t bus;
    IN u_int8_t dev_func;
    IN u_int8_t offset;
  };
  struct o_MOSAL_PCI_write_config_dword_ops_t {
    OUT call_result_t ret;
  };

/*************************************************************************
 * Function: MOSAL_PCI_write_io_byte
 *************************************************************************/ 
  struct i_MOSAL_PCI_write_io_byte_ops_t {
    IN u_int32_t addr;
    IN u_int8_t data;
  };
  struct o_MOSAL_PCI_write_io_byte_ops_t {
    OUT call_result_t ret;
  };

  /*************************************************************************
 * Function: MOSAL_PCI_write_io_word
 *************************************************************************/ 
struct i_MOSAL_PCI_write_io_word_ops_t {
    IN u_int32_t addr;
    IN u_int16_t data;
  };
  struct o_MOSAL_PCI_write_io_word_ops_t {
    OUT call_result_t ret;
  };

/*************************************************************************
 * Function: MOSAL_PCI_write_io_dword
 *************************************************************************/ 
struct i_MOSAL_PCI_write_io_dword_ops_t {
    IN u_int32_t addr;
    IN u_int32_t data;
  };
  struct o_MOSAL_PCI_write_io_dword_ops_t {
    OUT call_result_t ret;
  };

/*************************************************************************
 * Function: MOSAL_get_counts_per_sec
 *************************************************************************/ 
  struct i_MOSAL_get_counts_per_sec_ops_t {
    IN int dummy;
  };
struct o_MOSAL_get_counts_per_sec_ops_t {
    OUT u_int64_t ret;
  };

/*************************************************************************
 * Function: MOSAL_get_page_shift
 *************************************************************************/ 
  struct i_MOSAL_get_page_shift_ops_t {
    IN MT_virt_addr_t va;
    IN MOSAL_prot_ctx_t prot_ctx;
  };
  struct o_MOSAL_get_page_shift_ops_t {
    OUT call_result_t ret;
    OUT unsigned int page_shift;
  };

/*************************************************************************
 * Function: MOSAL_is_privileged
 *************************************************************************/ 
  struct i_MOSAL_is_privileged_ops_t {
    IN int dummy;
  };
  struct o_MOSAL_is_privileged_ops_t {
    OUT MT_bool ret;
  };

/*************************************************************************
 * Function: MOSAL_map_phys_addr
 *************************************************************************/ 
  struct i_MOSAL_map_phys_addr_ops_t {
    IN MT_phys_addr_t pa;
    IN MT_size_t size;
    IN MOSAL_prot_ctx_t prot_ctx;
    IN MOSAL_mem_flags_t flags;
  };
  struct o_MOSAL_map_phys_addr_ops_t {
    OUT MT_virt_addr_t ret;
  };

/*************************************************************************
 * Function: MOSAL_unmap_phys_addr
 *************************************************************************/ 
  struct i_MOSAL_unmap_phys_addr_ops_t {
    IN MT_virt_addr_t va;
    IN MT_size_t size;
    IN MOSAL_prot_ctx_t prot_ctx;
  };
  struct o_MOSAL_unmap_phys_addr_ops_t {
    OUT call_result_t ret;
  };


/*************************************************************************
 * Function: MOSAL_mlock
 *************************************************************************/ 
struct i_MOSAL_mlock_ops_t {
    IN MT_virt_addr_t va;
    IN MT_size_t size;
  };
  struct o_MOSAL_mlock_ops_t {
    OUT call_result_t ret;
  };

/*************************************************************************
 * Function: MOSAL_munlock
 *************************************************************************/ 
struct i_MOSAL_munlock_ops_t {
    IN MT_virt_addr_t va;
    IN MT_size_t size;
  };
  struct o_MOSAL_munlock_ops_t {
    OUT call_result_t ret;
  };

/*************************************************************************
 * Function: MOSAL_virt_to_phys
 *************************************************************************/ 
  struct i_MOSAL_virt_to_phys_ops_t {
    IN MT_virt_addr_t va;
    IN MOSAL_prot_ctx_t prot_ctx;
  };
  struct o_MOSAL_virt_to_phys_ops_t {
    OUT call_result_t ret;
    OUT MT_phys_addr_t pa;
  };

/*************************************************************************
 * Function: k2u_cbk_pollq
 *************************************************************************/ 
  struct i_k2u_cbk_pollq_ops_t {
    IN k2u_cbk_hndl_t k2u_cbk_h;
  };
  struct o_k2u_cbk_pollq_ops_t {
    OUT MT_size_t size;
    OUT call_result_t ret;
    OUT k2u_cbk_id_t cbk_id;
    OUT u_int8_t data[MAX_CBK_DATA_SZ];
  };

/*************************************************************************
 * Function: mtl_log_set
 *************************************************************************/ 
  struct o_mtl_log_set_ops_t {
    IN int dummy;
  };
  struct i_mtl_log_set_ops_t {
    IN char layer[MAX_MTL_LOG_LAYER];
    IN char info[MAX_MTL_LOG_INFO];
  };


#ifdef __WIN__

/*************************************************************************
 * Function: MOSAL_PCI_read_config_data
 *************************************************************************/ 
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

/*************************************************************************
 * Function: MOSAL_PCI_write_config_data
 *************************************************************************/ 
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



/*************************************************************************
 * Function: MOSAL_PCI_read_mem
 *************************************************************************/ 
  struct i_MOSAL_PCI_read_mem_ops_t {
    IN u_int64_t addr;
    IN u_int64_t size;
  };
  struct o_MOSAL_PCI_read_mem_ops_t {
    OUT call_result_t ret;
    OUT u_int8_t data[1];
  };

/*************************************************************************
 * Function: MOSAL_PCI_write_mem
 *************************************************************************/ 
  struct i_MOSAL_PCI_write_mem_ops_t {
    IN u_int64_t addr;
    IN u_int64_t size;
    IN u_int8_t data[1];
  };
  struct o_MOSAL_PCI_write_mem_ops_t {
    OUT call_result_t ret;
  };

/*************************************************************************
 * Function: MOSAL_io_remap_for_user
 *************************************************************************/ 
  struct i_MOSAL_io_remap_for_user_ops_t {
    IN MT_phys_addr_t pa;
    IN MT_size_t size;
  };
  struct o_MOSAL_io_remap_for_user_ops_t {
    OUT MT_virt_addr_t ret;
  };

/*************************************************************************
 * Function: MOSAL_io_release_for_user
 *************************************************************************/ 
  struct i_MOSAL_io_release_for_user_ops_t {
    IN MT_phys_addr_t pa;
  };
  struct o_MOSAL_io_release_for_user_ops_t {
    IN int dummy;
  };

/*************************************************************************
 * Function: MOSAL_phys_ctg_get_for_user
 *************************************************************************/ 
  struct i_MOSAL_phys_ctg_get_for_user_ops_t {
    IN MT_size_t size;
  };
  struct o_MOSAL_phys_ctg_get_for_user_ops_t {
    OUT MT_virt_addr_t ret;
  };
  
/*************************************************************************
 * Function: MOSAL_phys_ctg_free_for_user
 *************************************************************************/ 
  struct i_MOSAL_phys_ctg_free_for_user_ops_t {
    IN MT_virt_addr_t va;
  };
  struct o_MOSAL_phys_ctg_free_for_user_ops_t {
    IN int dummy;
  };

/*************************************************************************
 * Function: MOSAL_nsecs
 *************************************************************************/ 
  struct i_MOSAL_MOSAL_nsecs_ops_t {
    IN int dummy;
  };
   struct o_MOSAL_nsecs_ops_t {
    OUT u_int64_t ret;
  };


#endif


#ifdef __LINUX__
/*************************************************************************
 * Function: MOSAL_I2C_open
 *************************************************************************/ 
  struct i_MOSAL_I2C_open_ops_t {
    IN char name[I2C_MAX_DEV_NAME];
  };
  struct o_MOSAL_I2C_open_ops_t {
    OUT call_result_t ret;
    OUT MOSAL_i2c_devh_t dev_h;
  };

/*************************************************************************
 * Function: MOSAL_I2C_close
 *************************************************************************/ 
  struct i_MOSAL_I2C_close_ops_t {
    IN MOSAL_i2c_devh_t dev_h;
  };
  struct o_MOSAL_I2C_close_ops_t {
    OUT call_result_t ret;
  };

/*************************************************************************
 * Function: MOSAL_I2C_read
 *************************************************************************/ 
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

/*************************************************************************
 * Function: MOSAL_I2C_write
 *************************************************************************/ 
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


/*************************************************************************
 * Function: MOSAL_I2C_master_receive
 *************************************************************************/ 
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

/*************************************************************************
 * Function: MOSAL_I2C_send_stop
 *************************************************************************/ 
   struct i_MOSAL_I2C_send_stop_ops_t {
    IN MOSAL_i2c_devh_t dev_h;
    IN u_int32_t key;
  };
  struct o_MOSAL_I2C_send_stop_ops_t {
    OUT call_result_t ret;
  };

#endif


#endif
