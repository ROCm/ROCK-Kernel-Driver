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

#include <linux/module.h>

#include <mosal.h>
#include "mosal_driver.h"
#include <mosal_k2u_cbk.h>
#include <mosal_mem.h>
#include <mosal_mlock.h>
#include <mosal_sync.h>
#include <mosal_i2c.h>
#include <mosal_thread.h>

extern void*  mtl_log_vmalloc(const char* fn, int ln, int bsize);
extern void   mtl_log_vfree(const char* fn, int ln, void* ptr);
extern void*  mtl_log_kmalloc(const char* fn, int ln, int bsize, unsigned g);
extern void   mtl_log_kfree(const char* fn, int ln, void* ptr);

EXPORT_SYMBOL(k2u_cbk_invoke);
EXPORT_SYMBOL(mosal_chrdev_register);
EXPORT_SYMBOL(mosal_chrdev_unregister);
EXPORT_SYMBOL(MOSAL_delay_execution);
EXPORT_SYMBOL(MOSAL_usleep);
EXPORT_SYMBOL(MOSAL_usleep_ui);
EXPORT_SYMBOL(MOSAL_DPC_init);
EXPORT_SYMBOL(MOSAL_DPC_schedule);
EXPORT_SYMBOL(MOSAL_get_counts_per_sec);
EXPORT_SYMBOL(MOSAL_get_exec_ctx);
EXPORT_SYMBOL(MOSAL_getpid);
#ifndef MTL_TRACK_ALLOC
EXPORT_SYMBOL(MOSAL_io_remap);
EXPORT_SYMBOL(MOSAL_io_unmap);
#else
EXPORT_SYMBOL(MOSAL_io_remap_memtrack);
EXPORT_SYMBOL(MOSAL_io_unmap_memtrack);
#endif
EXPORT_SYMBOL(MOSAL_ISR_set);
EXPORT_SYMBOL(MOSAL_ISR_unset);
EXPORT_SYMBOL(MOSAL_map_phys_addr);
EXPORT_SYMBOL(MOSAL_mlock);
EXPORT_SYMBOL(MOSAL_munlock);
#ifndef MTL_TRACK_ALLOC
EXPORT_SYMBOL(MOSAL_iobuf_register);
EXPORT_SYMBOL(MOSAL_iobuf_deregister);
#else
EXPORT_SYMBOL(MOSAL_iobuf_register_memtrack);
EXPORT_SYMBOL(MOSAL_iobuf_deregister_memtrack);
#endif
EXPORT_SYMBOL(MOSAL_iobuf_get_props);
EXPORT_SYMBOL(MOSAL_iobuf_get_tpt);
EXPORT_SYMBOL(MOSAL_iobuf_cmp_tpt);
EXPORT_SYMBOL(MOSAL_iobuf_iter_init);
EXPORT_SYMBOL(MOSAL_iobuf_get_tpt_seg);
EXPORT_SYMBOL(MOSAL_mutex_acq);
EXPORT_SYMBOL(MOSAL_mutex_acq_ui);
EXPORT_SYMBOL(MOSAL_mutex_init);
EXPORT_SYMBOL(MOSAL_mutex_rel);
EXPORT_SYMBOL(MOSAL_phys_ctg_free);
EXPORT_SYMBOL(MOSAL_phys_ctg_get);
EXPORT_SYMBOL(MOSAL_sem_acq);
EXPORT_SYMBOL(MOSAL_sem_acq_ui);
EXPORT_SYMBOL(MOSAL_sem_init);
EXPORT_SYMBOL(MOSAL_sem_rel);
EXPORT_SYMBOL(MOSAL_spinlock_init);
EXPORT_SYMBOL(MOSAL_syncobj_init);
EXPORT_SYMBOL(MOSAL_syncobj_clear);
EXPORT_SYMBOL(MOSAL_syncobj_signal);
EXPORT_SYMBOL(MOSAL_syncobj_waiton);
EXPORT_SYMBOL(MOSAL_syncobj_waiton_ui);
EXPORT_SYMBOL(MOSAL_unmap_phys_addr);
EXPORT_SYMBOL(MOSAL_virt_to_phys);
EXPORT_SYMBOL(MOSAL_thread_start);
EXPORT_SYMBOL(MOSAL_thread_set_name);
EXPORT_SYMBOL(MOSAL_thread_wait_for_exit);
EXPORT_SYMBOL(MOSAL_time_get_clock);
EXPORT_SYMBOL(MOSAL_time_compare);
EXPORT_SYMBOL(mtl_basename);
EXPORT_SYMBOL(mtl_log);
EXPORT_SYMBOL(mtl_log_kmalloc);
EXPORT_SYMBOL(mtl_log_kfree);
EXPORT_SYMBOL(mtl_log_vmalloc);
EXPORT_SYMBOL(mtl_log_vfree);
EXPORT_SYMBOL(mtl_strerror_sym);
EXPORT_SYMBOL(MOSAL_get_page_shift);
EXPORT_SYMBOL(MOSAL_MPC860_present);
EXPORT_SYMBOL(MOSAL_qdestroy);
EXPORT_SYMBOL(MOSAL_qget);
EXPORT_SYMBOL(MOSAL_PCI_read_config_dword);
EXPORT_SYMBOL(MOSAL_PCI_read_config_byte);
EXPORT_SYMBOL(MOSAL_PCI_write_config_byte);
EXPORT_SYMBOL(mtl_strerror);
EXPORT_SYMBOL(MOSAL_set_intr_handler);
EXPORT_SYMBOL(MOSAL_unset_intr_handler);
EXPORT_SYMBOL(MOSAL_qcreate);
EXPORT_SYMBOL(MOSAL_timer_del);
EXPORT_SYMBOL(MOSAL_timer_add);
EXPORT_SYMBOL(MOSAL_timer_init);
EXPORT_SYMBOL(MOSAL_qput);
EXPORT_SYMBOL(MOSAL_PCI_write_config_dword);
EXPORT_SYMBOL(MOSAL_PCI_find_device);
EXPORT_SYMBOL(MOSAL_PCI_get_cfg_hdr);
EXPORT_SYMBOL(MOSAL_PCI_present);
EXPORT_SYMBOL(MOSAL_I2C_master_receive);
EXPORT_SYMBOL(MOSAL_I2C_master_transmit);
EXPORT_SYMBOL(MOSAL_I2C_send_stop);
EXPORT_SYMBOL(MOSAL_I2C_read);
EXPORT_SYMBOL(MOSAL_I2C_write);
EXPORT_SYMBOL(MOSAL_I2C_open);
EXPORT_SYMBOL(MOSAL_I2C_close);
EXPORT_SYMBOL(MOSAL_I2C_add_dev);
EXPORT_SYMBOL(MOSAL_I2C_del_dev);
EXPORT_SYMBOL(MOSAL_PCI_read_config_word);
EXPORT_SYMBOL(MOSAL_PCI_write_config_word);
EXPORT_SYMBOL(MOSAL_PCI_read_config);
EXPORT_SYMBOL(MOSAL_PCI_write_config);
EXPORT_SYMBOL(MOSAL_PCI_find_dev);
