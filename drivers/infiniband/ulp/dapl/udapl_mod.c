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

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: udapl_mod.c 35 2004-04-09 05:34:32Z roland $
*/

#include <linux/config.h>

#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <linux/init.h>

#include "vapi_common.h"
#include "evapi.h"

#include "ib_legacy_types.h"
#include "ts_kernel_trace.h"
#include "ts_ib_core.h"
#include "ts_ib_sa_client.h"
#include "ip2pr_export.h"
#include "ipoib_proto.h"
#include "udapl_mod.h"
#include "khash.h"

MODULE_AUTHOR("Ariel Cohen");
MODULE_DESCRIPTION("IB uDAPL support driver");
MODULE_LICENSE("Dual BSD/GPL");

#define MAX_WOS                 1300
#define MAX_PRS                 500
#define MAX_ATS_ENTRIES         500
#define SMR_DB_SIZE             256  /* Keep this a power of 2. Max is 4096 */
#define MAX_SMR_PER_PROCESS     256
#define MAX_MRH_PER_SMR         512
#define MRH_INDEX_INVALID       ((tINT32) ~0)
#define MAX_WO_PER_PROCESS      256
#define TS_TIMEOUT_INFINITE     ((tINT32) ~0)
#define SMR_COOKIE_SIZE         40
#define ATS_TIMEOUT             (2*HZ)
#define ATS_RETRIES             15
#define ATS_CACHE_SIZE          64
#define ATS_CACHE_TIMEOUT       (300*HZ)
#define ATS_IPADDR              0
#define ATS_GID                 1
#define MAX_NET_DEVICES         64
#define MAX_HCAS                4
#define MAX_PORTS_PER_HCA       2

typedef struct pr_entry_s pr_entry_t;

struct pr_entry_s {
    struct semaphore          sem;
    tTS_IB_PATH_RECORD        user_path_record;
    tTS_IB_PATH_RECORD_STRUCT path_record;
    tINT32                    status;
    pr_entry_t                *next;
};

typedef struct ats_entry_s ats_entry_t;

struct ats_entry_s {
    struct semaphore          sem;
    uint8_t                   info[16];
    tINT32                    status;
    ats_entry_t               *next;
};

typedef struct ats_cache_rec_s ats_cache_rec_t;

struct ats_cache_rec_s {
    unsigned long    created;
    uint8_t          ip_addr[16];
    uint8_t          gid[16];
};

typedef struct ats_advert_s ats_advert_t;

struct ats_advert_s {
    uint8_t              ip_addr[16];
    uint8_t              gid[16];
    tUINT32              set_flag;
    tTS_IB_DEVICE_HANDLE ts_hca_handle;
};

typedef struct wo_entry_s wo_entry_t;

struct wo_entry_s {
    wait_queue_head_t  wq;
    unsigned long      waiting;
    unsigned long      signaled;
    wo_entry_t        *next;
};

typedef struct smr_rec_s smr_rec_t;

struct smr_rec_s {
    VAPI_mr_hndl_t *mrh_array;
    tINT32          ref_count;
    tINT32          initialized;
    char            cookie[SMR_COOKIE_SIZE];
    smr_rec_t      *next;
};

typedef struct smr_clean_info_s smr_clean_info_t;

struct smr_clean_info_s {
    smr_rec_t *smr_rec;
    tUINT32    mrh_index;
};

typedef struct resources_s resources_t;

struct resources_s {
    wo_entry_t       **wo_entries;
    smr_clean_info_t *smr_clean_info;
    tUINT32          shmem_sem_flag;
};

static int udapl_major_number = 245;

static tTS_IB_ASYNC_EVENT_HANDLER_HANDLE async_handle;

static struct semaphore shmem_sem;

static void       *wo_area;
static wo_entry_t *wo_free_list;
static spinlock_t  wo_free_list_lock = SPIN_LOCK_UNLOCKED;

static void        *ats_area;
static ats_entry_t *ats_free_list;
static spinlock_t   ats_free_list_lock = SPIN_LOCK_UNLOCKED;


static tTS_IB_DEVICE_HANDLE hcas[MAX_HCAS];
static ats_advert_t         ats_advert[MAX_HCAS][MAX_PORTS_PER_HCA];
static spinlock_t           ats_advert_lock = SPIN_LOCK_UNLOCKED;

static ats_cache_rec_t ats_cache[ATS_CACHE_SIZE];
static tUINT32         ats_cache_last;
static spinlock_t      ats_cache_lock = SPIN_LOCK_UNLOCKED;

static void       *pr_area;
static pr_entry_t *pr_free_list;
static spinlock_t  pr_free_list_lock = SPIN_LOCK_UNLOCKED;

static void       *smr_rec_area;
static smr_rec_t  *smr_free_list;
static spinlock_t  smr_free_list_lock = SPIN_LOCK_UNLOCKED;

static DAPL_HASH_TABLE smr_db;
static spinlock_t      smr_db_lock = SPIN_LOCK_UNLOCKED;

/* ------------------------------------------------------------------------- */
/* Wait object processing                                                    */
/* ------------------------------------------------------------------------- */

static wo_entry_t *get_free_wo_entry(void)
{
    unsigned long  flags;
    wo_entry_t    *wo_entry;


    spin_lock_irqsave(&wo_free_list_lock, flags);

    if (wo_free_list == NULL) {

        spin_unlock_irqrestore(&wo_free_list_lock, flags);
        TS_REPORT_FATAL(MOD_UDAPL, "Wait object pool exhausted!");
        return NULL;

    }

    wo_entry     = wo_free_list;
    wo_free_list = wo_free_list->next;

    spin_unlock_irqrestore(&wo_free_list_lock, flags);

    wo_entry->next = NULL;

    return wo_entry;
}


static inline void free_wo_entry(wo_entry_t *wo_entry)
{
    unsigned long flags;


    spin_lock_irqsave(&wo_free_list_lock, flags);

    wo_entry->next = wo_free_list;
    wo_free_list   = wo_entry;

    spin_unlock_irqrestore(&wo_free_list_lock, flags);

    return;
}


static int wait_obj_init(struct file *fp, unsigned long arg)
{
    wo_entry_t **wos;
    int          wo_n;
    wo_entry_t  *wo_entry;


    if (!arg)
	return -EINVAL;

    wo_entry = get_free_wo_entry();

    if (wo_entry == NULL)
	return -EFAULT;

    init_waitqueue_head(&wo_entry->wq);
    wo_entry->waiting  = 0;
    wo_entry->signaled = 0;

    wos = (wo_entry_t **) (((resources_t *) fp->private_data)->wo_entries);

    for (wo_n = 0; wo_n < MAX_WO_PER_PROCESS; wo_n++) {

	if (wos[wo_n] == NULL) {
	    wos[wo_n] = wo_entry;
	    break;
	}

    }

    if (wo_n == MAX_WO_PER_PROCESS) {

	TS_REPORT_WARN(MOD_UDAPL, "Wait object array is full");
	return -EFAULT;

    }

    copy_to_user((void **) arg, &wo_entry, sizeof(wo_entry_t *));

    return 0;

}


static int wait_obj_destroy(struct file *fp, unsigned long arg)
{
    wo_entry_t    **wos;
    wo_entry_t     *wo_entry;
    int             wo_n;


    if (!arg)
	return -EINVAL;

    wo_entry = (wo_entry_t *) arg;

    if (test_bit(0, &wo_entry->waiting)) {
	TS_REPORT_WARN(MOD_UDAPL, "Wait object in use; cannot destroy");
	return -EFAULT;
    }

    free_wo_entry((wo_entry_t *) arg);

    wos = (wo_entry_t **) (((resources_t *) fp->private_data)->wo_entries);

    for (wo_n = 0; wo_n < MAX_WO_PER_PROCESS; wo_n++) {

	if (wos[wo_n] == wo_entry) {
	    wos[wo_n] = NULL;
	    break;
	}

    }

    if (wo_n == MAX_WO_PER_PROCESS) {

	TS_REPORT_WARN(MOD_UDAPL,
		       "Cannot find wait object entry in array");
	return -EFAULT;

    }

    return 0;

}


static int wait_obj_wait(unsigned long arg)
{
    wo_entry_t            *wo_entry;
    wait_obj_wait_param_t  param;
    long                   os_ticks;
    long                   ticks_left;
    wait_queue_t           wait;
    int                    signaled;

    if (!arg)
	return -EINVAL;

    if (copy_from_user(&param, (wait_obj_wait_param_t *) arg,
		       sizeof(wait_obj_wait_param_t))) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed for parameters");
	return -EFAULT;
    }

    wo_entry = (wo_entry_t *) param.wait_obj;

    if (test_and_set_bit(0, &wo_entry->waiting)) {
	TS_REPORT_WARN(MOD_UDAPL, "Waiter already exists for wo_entry=%p",
		       wo_entry);
	return -EFAULT;
    }

    if (param.timeout == TS_TIMEOUT_INFINITE) {
        if (wait_event_interruptible(wo_entry->wq,
                                     test_bit(0, &wo_entry->signaled)) == 0) {
            clear_bit(0, &wo_entry->signaled);
            clear_bit(0, &wo_entry->waiting);
            return 0;
        } else {
            clear_bit(0, &wo_entry->waiting);
            return -EINTR;
        }
    }

    if (param.timeout == 0)

	ticks_left = 0;

    else {

	ticks_left = os_ticks =
	    (param.timeout + (1000000 / HZ) - 1) / (1000000 / HZ);

	if (!test_bit(0, &wo_entry->signaled)) {

	    init_waitqueue_entry(&wait, current);
	    add_wait_queue(&wo_entry->wq, &wait);
	    set_current_state(TASK_INTERRUPTIBLE);

	    if ((!test_bit(0, &wo_entry->signaled)) &&
		(!signal_pending(current))) {

		ticks_left = schedule_timeout(os_ticks);

	    }

	    current->state = TASK_RUNNING;
	    remove_wait_queue(&wo_entry->wq, &wait);

	}

    }

    signaled = test_and_clear_bit(0, &wo_entry->signaled);

    clear_bit(0, &wo_entry->waiting);

    if ((ticks_left == 0) && (!signaled))
	param.timedout = 1;
    else
	param.timedout = 0;

    if (copy_to_user((wait_obj_wait_param_t *) arg, &param,
		     sizeof(wait_obj_wait_param_t))) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_to_user() failed");
	return -EFAULT;
    }

    if (param.timedout || signaled || (param.timeout == 0))
	return 0;

    return -EINTR;
}


static int wait_obj_wakeup(unsigned long arg)
{
    wo_entry_t *wo_entry;


    if (!arg)
	return -EINVAL;

    wo_entry = (wo_entry_t *) arg;
    set_bit(0, &wo_entry->signaled);
    wake_up(&wo_entry->wq);

    return 0;
}

static void wo_clean(wo_entry_t **wos)
{
    int wo_n;


    TS_ENTER(MOD_UDAPL);

    if (wos == NULL) {

	TS_REPORT_WARN(MOD_UDAPL, "NULL wait object array");
	return;

    }

    for (wo_n = 0; wo_n < MAX_WO_PER_PROCESS; wo_n++)

	if (wos[wo_n])
	    free_wo_entry(wos[wo_n]);

}

/* ------------------------------------------------------------------------- */
/* End of wait object processing                                             */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* Completion-related processing                                             */
/* ------------------------------------------------------------------------- */

static void comp_handler(VAPI_hca_hndl_t hca_hndl,
			 VAPI_cq_hndl_t  cq_hndl,
			 void            *private_data)
{
    wo_entry_t *wo_entry;

    if (private_data == NULL) {
	TS_REPORT_WARN(MOD_UDAPL,
		       "Completion handler called with NULL private data");
	return;
    }

    wo_entry = (wo_entry_t *) private_data;

    set_bit(0, &wo_entry->signaled);
    wake_up(&wo_entry->wq);

    return;
}

static int get_khca_hndl(unsigned long arg)
{
    get_khca_hndl_param_t param;
    VAPI_ret_t            ret;


    if (!arg)
	return -EINVAL;

    if (copy_from_user(&param, (get_khca_hndl_param_t *) arg,
		       sizeof(get_khca_hndl_param_t))) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed for parameters");
	return -EFAULT;
    }

    param.ts_hca_handle = tsIbDeviceGetByName(param.hca_id);

    if (param.ts_hca_handle == TS_IB_HANDLE_INVALID) {
	TS_REPORT_WARN(MOD_UDAPL, "tsIbDeviceGetByName() failed");
	return -EFAULT;
    }

    ret = EVAPI_get_hca_hndl(param.hca_id, &param.k_hca_handle);

    if (ret != VAPI_OK) {
	TS_REPORT_WARN(MOD_UDAPL, "EVAPI_get_hca_hndl() failed: %s",
		       VAPI_strerror_sym(ret));
	return -EFAULT;
    }

    if (copy_to_user((get_khca_hndl_param_t *) arg, &param,
		     sizeof(get_khca_hndl_param_t))) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_to_user() failed");
	return -EFAULT;
    }

    return 0;
}

static int release_khca_hndl(unsigned long arg)
{
    VAPI_ret_t ret;


    ret = EVAPI_release_hca_hndl((VAPI_hca_hndl_t) arg);

    if (ret != VAPI_OK) {
	    TS_REPORT_WARN(MOD_UDAPL, "EVAPI_release_hca_hndl() failed: %s",
			   VAPI_strerror_sym(ret));
	    return -EFAULT;
    }

    return 0;
}

static int set_comp_eventh(unsigned long arg)
{
    set_comp_eventh_param_t param;
    VAPI_ret_t              ret;

    if (!arg)
	return -EINVAL;

    if (copy_from_user(&param, (set_comp_eventh_param_t *) arg,
		       sizeof(set_comp_eventh_param_t))) {
	return -EFAULT;
    }

    if (param.wait_obj == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "wait_obj is NULL");
	return -EFAULT;
    }

    ret = EVAPI_k_set_comp_eventh(param.k_hca_handle,
				  param.k_cq_handle,
				  comp_handler,
				  param.wait_obj,
				  &param.comp_handler_handle);

    if (ret != VAPI_OK) {
	    TS_REPORT_WARN(MOD_UDAPL,
			   "EVAPI_k_set_comp_eventh() failed: %s",
			   VAPI_strerror_sym(ret));
	    return -EFAULT;
    }

    if (copy_to_user((set_comp_eventh_param_t *) arg, &param,
		     sizeof(set_comp_eventh_param_t))) {
	return -EFAULT;
    }

    return 0;
}

static int clear_comp_eventh(unsigned long arg)
{
    clear_comp_eventh_param_t param;
    VAPI_ret_t                ret;

    if (!arg)
	return -EINVAL;

    if (copy_from_user(&param,
		       (clear_comp_eventh_param_t *) arg,
		       sizeof(clear_comp_eventh_param_t))) {
	return -EFAULT;
    }

    ret = EVAPI_k_clear_comp_eventh(param.k_hca_handle,
				    param.comp_handler_handle);

    if (ret != VAPI_OK) {
	    TS_REPORT_WARN(MOD_UDAPL,
			   "EVAPI_k_set_comp_eventh() failed: %s",
			   VAPI_strerror_sym(ret));
	    return -EFAULT;
    }

    return 0;
}

/* ------------------------------------------------------------------------- */
/* End of completion-related processing                                      */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* ATS processing                                                            */
/* ------------------------------------------------------------------------- */

static tUINT32 find_hca(tTS_IB_DEVICE_HANDLE ts_hca_handle)
{
    int i;

    for (i = 0; i < MAX_HCAS; i++) {
	if (hcas[i] == ts_hca_handle)
	    return i;
	if (!hcas[i])
	    break;
    }
    return i;
}

static void ats_cache_insert(uint8_t *ip_addr, uint8_t *gid, tINT32 what)
{
    unsigned long flags;
    tUINT32       entry_n;


    spin_lock_irqsave(&ats_cache_lock, flags);

    if (what == ATS_GID) {

	for (entry_n = 0; entry_n < ATS_CACHE_SIZE; entry_n++) {

	    if (!memcmp(ats_cache[entry_n].ip_addr, ip_addr, 16)) {
		memcpy(ats_cache[entry_n].gid, gid, 16);
		ats_cache[entry_n].created = jiffies;
		goto out;
	    }

	}

    } else {

	for (entry_n = 0; entry_n < ATS_CACHE_SIZE; entry_n++) {

	    if (!memcmp(ats_cache[entry_n].gid, gid, 16)) {
		memcpy(ats_cache[entry_n].ip_addr, ip_addr, 16);
		ats_cache[entry_n].created = jiffies;
		goto out;
	    }

	}

    }

    for (entry_n = 0; entry_n < ATS_CACHE_SIZE; entry_n++) {

	if (((jiffies - ats_cache[entry_n].created) >= ATS_CACHE_TIMEOUT) ||
	    (ats_cache[entry_n].created == 0)) {

	    memcpy(ats_cache[entry_n].ip_addr, ip_addr, 16);
	    memcpy(ats_cache[entry_n].gid, gid, 16);
	    ats_cache[entry_n].created = jiffies;
	    goto out;

	}

    }

    memcpy(ats_cache[ats_cache_last].ip_addr, ip_addr, 16);
    memcpy(ats_cache[ats_cache_last].gid, gid, 16);
    ats_cache[ats_cache_last].created = jiffies;
    ats_cache_last = (ats_cache_last + 1) % ATS_CACHE_SIZE;

    out:
       spin_unlock_irqrestore(&ats_cache_lock, flags);
       return;
}


static tINT32 ats_cache_lookup(uint8_t *ip_addr, uint8_t *gid, tINT32 what)
{
    unsigned long flags;
    tUINT32       entry_n;


    spin_lock_irqsave(&ats_cache_lock, flags);

    if (what == ATS_GID) {

	for (entry_n = 0; entry_n < ATS_CACHE_SIZE; entry_n++) {

	    if (!memcmp(ats_cache[entry_n].ip_addr, ip_addr, 16)) {

		if (jiffies - ats_cache[entry_n].created < ATS_CACHE_TIMEOUT) {

		    memcpy(gid, ats_cache[entry_n].gid, 16);
		    goto found;

		} else

		    goto not_found;

	    }

	}

	goto not_found;

    } else {

	for (entry_n = 0; entry_n < ATS_CACHE_SIZE; entry_n++) {

	    if (!memcmp(ats_cache[entry_n].gid, gid, 16)) {

		if (jiffies - ats_cache[entry_n].created < ATS_CACHE_TIMEOUT) {

		    memcpy(ip_addr, ats_cache[entry_n].ip_addr, 16);
		    goto found;

		} else

		    goto not_found;

	    }

	}

	goto not_found;

    }

 not_found:
    spin_unlock_irqrestore(&ats_cache_lock, flags);
    return 0;

 found:
    spin_unlock_irqrestore(&ats_cache_lock, flags);
    return 1;
}


static ats_entry_t *get_free_ats_entry(void)
{
    unsigned long   flags;
    ats_entry_t     *ats_entry;


    spin_lock_irqsave(&ats_free_list_lock, flags);

    if (ats_free_list == NULL) {

        spin_unlock_irqrestore(&ats_free_list_lock, flags);
        TS_REPORT_FATAL(MOD_UDAPL, "ATS entry pool exhausted!");
        return NULL;

    }

    ats_entry     = ats_free_list;
    ats_free_list = ats_free_list->next;

    spin_unlock_irqrestore(&ats_free_list_lock, flags);

    ats_entry->next = NULL;

    return ats_entry;
}

static inline void free_ats_entry(ats_entry_t *ats_entry)
{
    unsigned long   flags;


    spin_lock_irqsave(&ats_free_list_lock, flags);

    ats_entry->next = ats_free_list;
    ats_free_list   = ats_entry;

    spin_unlock_irqrestore(&ats_free_list_lock, flags);

    return;
}

static void ats_service_get_gid_comp(tTS_IB_CLIENT_QUERY_TID tid,
				     int                     status,
				     tTS_IB_GID              gid,
				     void                    *arg)
{
    ats_entry_t *ats_entry;


    if (arg == NULL) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL arg");
	return;

    }

    ats_entry = (ats_entry_t *) arg;

    ats_entry->status = status;

    if (status)
	goto out;

    if (gid == NULL) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL GID");
	goto out;

    }

    memcpy(ats_entry->info, gid, sizeof(tTS_IB_GID));

 out:

    up(&ats_entry->sem);

    return;

}

static tINT32 ats_gid_lookup(uint8_t *ip_addr, uint8_t *gid, void *ts_hca_handle, uint8_t port)
{
    tINT32                    status;
    ats_entry_t               *ats_entry;
    tTS_IB_CLIENT_QUERY_TID   tid;
    tUINT32                   count;


    TS_REPORT_STAGE(MOD_UDAPL, "Looking up GID for IP: %hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd on port %hd",
		    ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3],
		    ip_addr[4], ip_addr[5], ip_addr[6], ip_addr[7],
		    ip_addr[8], ip_addr[9], ip_addr[10], ip_addr[11],
		    ip_addr[12], ip_addr[13], ip_addr[14], ip_addr[15], port);

    if (ats_cache_lookup(ip_addr, gid, ATS_GID)) {

	TS_REPORT_STAGE(MOD_UDAPL, "Obtained from ATS cache GID: %hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx",
			gid[0], gid[1], gid[2], gid[3],
			gid[4], gid[5], gid[6], gid[7],
			gid[8], gid[9], gid[10], gid[11],
			gid[12], gid[13], gid[14], gid[15]);

	return 0;
    }

    ats_entry = get_free_ats_entry();

    if (ats_entry == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Failed to get a free ats_entry");
	status = -EFAULT;
	goto err;
    }

    ats_entry->status = 0;

    sema_init(&ats_entry->sem, 0);

    count = 0;

    do {
	status = tsIbAtsServiceGetGid(ts_hca_handle,
				      port,
				      ip_addr,
				      ATS_TIMEOUT,
				      ats_service_get_gid_comp,
				      ats_entry,
				      &tid);

	if (status < 0) {
	    TS_REPORT_WARN(MOD_UDAPL,
			   "tsIbAtsServiceGetGid() failed: %d", status);
	    free_ats_entry(ats_entry);
	    status = -EFAULT;
	    goto err;
	}

	status = down_interruptible(&ats_entry->sem);

	if (status) {
	    tsIbClientQueryCancel(tid);
	    free_ats_entry(ats_entry);
	    status = -EINTR;
	    goto err;
	}

	count++;

        if (ats_entry->status == -ETIMEDOUT)
            TS_REPORT_STAGE(MOD_UDAPL, "Timed out %d on IP: %hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd",
                           count, ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3],
                           ip_addr[4], ip_addr[5], ip_addr[6], ip_addr[7],
                           ip_addr[8], ip_addr[9], ip_addr[10], ip_addr[11],
                           ip_addr[12], ip_addr[13], ip_addr[14], ip_addr[15]);

    } while ((ats_entry->status == -ETIMEDOUT) && (count < ATS_RETRIES));

    if (ats_entry->status) {

	TS_REPORT_WARN(MOD_UDAPL, "ATS service get IP status: %d",
		       ats_entry->status);
	free_ats_entry(ats_entry);
	status = -EFAULT;
	goto err;
    }

    ats_cache_insert(ip_addr, ats_entry->info, ATS_GID);

    TS_REPORT_STAGE(MOD_UDAPL, "Obtained GID: %hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx",
		    ats_entry->info[0], ats_entry->info[1], ats_entry->info[2], ats_entry->info[3],
		    ats_entry->info[4], ats_entry->info[5], ats_entry->info[6], ats_entry->info[7],
		    ats_entry->info[8], ats_entry->info[9], ats_entry->info[10], ats_entry->info[11],
		    ats_entry->info[12], ats_entry->info[13], ats_entry->info[14], ats_entry->info[15]);

    memcpy(gid, ats_entry->info, 16);

    free_ats_entry(ats_entry);

    return 0;

 err:
    return status;
}

static void ats_service_get_ip_comp(tTS_IB_CLIENT_QUERY_TID tid,
                                    int                     status,
                                    tTS_IB_ATS_IP_ADDR      ip_addr,
                                    void                    *arg)
{
    ats_entry_t *ats_entry;


    if (arg == NULL) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL arg");
	return;

    }

    ats_entry = (ats_entry_t *) arg;

    ats_entry->status = status;

    if (status)
	goto out;

    if (ip_addr == NULL) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL IP address");
	goto out;

    }

    memcpy(ats_entry->info, ip_addr, sizeof(tTS_IB_ATS_IP_ADDR));

 out:

    up(&ats_entry->sem);

    return;

}

static tINT32 ats_ipaddr_lookup(struct file *fp, unsigned long arg)
{
    ats_ipaddr_lookup_param_t param;
    tINT32                    status;
    uint8_t                   gid[16];
    uint8_t                   ip_addr[16];
    ats_entry_t               *ats_entry;
    tTS_IB_CLIENT_QUERY_TID   tid;
    tUINT32                   count;

    if (!arg) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL argument");
	status = -EINVAL;
	goto err;

    }

    if (copy_from_user(&param, (ats_ipaddr_lookup_param_t *) arg,
		       sizeof(ats_ipaddr_lookup_param_t))) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed");
	status = -EFAULT;
	goto err;
    }

    if (param.gid == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL GID");
	status = -EFAULT;
	goto err;
    }

    if ((param.port < 1) || (param.port > MAX_PORTS_PER_HCA)) {
	TS_REPORT_WARN(MOD_UDAPL, "Called with a bad port number: %d",
		       param.port);
	status = -EFAULT;
	goto err;
    }

    if (param.ip_addr == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL IP address");
	status = -EFAULT;
	goto err;
    }

    if (copy_from_user(gid, param.gid, 16)) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed");
	status = -EFAULT;
	goto err;
    }

    TS_REPORT_STAGE(MOD_UDAPL, "Looking up GID: %hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx on port %hd",
		    gid[0], gid[1], gid[2], gid[3],
		    gid[4], gid[5], gid[6], gid[7],
		    gid[8], gid[9], gid[10], gid[11],
		    gid[12], gid[13], gid[14], gid[15], param.port);

    if (ats_cache_lookup(ip_addr, gid, ATS_IPADDR)) {
	TS_REPORT_STAGE(MOD_UDAPL, "Obtained from ATS cache IP: %hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd",
			ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3],
			ip_addr[4], ip_addr[5], ip_addr[6], ip_addr[7],
			ip_addr[8], ip_addr[9], ip_addr[10], ip_addr[11],
			ip_addr[12], ip_addr[13], ip_addr[14], ip_addr[15]);

	copy_to_user(param.ip_addr, ip_addr, 16);
	return 0;
    }

    ats_entry = get_free_ats_entry();

    if (ats_entry == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Failed to get a free ats_entry");
	status = -EFAULT;
	goto err;
    }

    ats_entry->status = 0;

    sema_init(&ats_entry->sem, 0);

    count = 0;

    do {
	status = tsIbAtsServiceGetIp(param.ts_hca_handle,
				     param.port,
				     gid,
				     ATS_TIMEOUT,
				     ats_service_get_ip_comp,
				     ats_entry,
				     &tid);

	if (status < 0) {
	    TS_REPORT_WARN(MOD_UDAPL,
			   "tsIbAtsServiceGetIp() failed: %d", status);
	    free_ats_entry(ats_entry);
	    status = -EFAULT;
	    goto err;
	}

	status = down_interruptible(&ats_entry->sem);

	if (status) {
	    tsIbClientQueryCancel(tid);
	    free_ats_entry(ats_entry);
	    status = -EINTR;
	    goto err;
	}

	count++;

        if (ats_entry->status == -ETIMEDOUT)
            TS_REPORT_STAGE(MOD_UDAPL, "Timed out %d on GID: %hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx",
                            count, gid[0], gid[1], gid[2], gid[3],
                            gid[4], gid[5], gid[6], gid[7],
                            gid[8], gid[9], gid[10], gid[11],
                            gid[12], gid[13], gid[14], gid[15]);

    } while ((ats_entry->status == -ETIMEDOUT) && (count < ATS_RETRIES));

    if (ats_entry->status) {

	TS_REPORT_WARN(MOD_UDAPL, "ATS service get IP status: %d",
		       ats_entry->status);
	free_ats_entry(ats_entry);
	status = -EFAULT;
	goto err;
    }

    ats_cache_insert(ats_entry->info, gid, ATS_IPADDR);

    TS_REPORT_STAGE(MOD_UDAPL, "Obtained IP: %hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd",
		    ats_entry->info[0], ats_entry->info[1], ats_entry->info[2], ats_entry->info[3],
		    ats_entry->info[4], ats_entry->info[5], ats_entry->info[6], ats_entry->info[7],
		    ats_entry->info[8], ats_entry->info[9], ats_entry->info[10], ats_entry->info[11],
		    ats_entry->info[12], ats_entry->info[13], ats_entry->info[14], ats_entry->info[15]);

    copy_to_user(param.ip_addr, ats_entry->info, 16);

    free_ats_entry(ats_entry);

    return 0;

 err:
    return status;
}

static void ats_service_set_comp(tTS_IB_CLIENT_QUERY_TID      tid,
				 int                          status,
				 tTS_IB_COMMON_ATTRIB_SERVICE service,
				 void                         *arg)
{
    ats_entry_t *ats_entry;


    if (arg == NULL) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL arg");
	return;

    }

    ats_entry = (ats_entry_t *) arg;

    ats_entry->status = status;

    up(&ats_entry->sem);

    return;
}

static tINT32 ats_do_set_ipaddr(uint8_t *ip_addr, uint8_t *gid, void *ts_hca_handle,
				uint8_t port)
{
    ats_entry_t             *ats_entry;
    tTS_IB_CLIENT_QUERY_TID tid;
    tUINT32                 count;
    tINT32                  status;


    ats_entry = get_free_ats_entry();

    if (ats_entry == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Failed to get a free ats_entry");
	status = -EFAULT;
	goto err;
    }

    ats_entry->status = 0;

    sema_init(&ats_entry->sem, 0);

    TS_REPORT_STAGE(MOD_UDAPL, "Setting IP: %hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd for GID: %hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx on port %d",
		    ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3],
		    ip_addr[4], ip_addr[5], ip_addr[6], ip_addr[7],
		    ip_addr[8], ip_addr[9], ip_addr[10], ip_addr[11],
		    ip_addr[12], ip_addr[13], ip_addr[14], ip_addr[15],
		    gid[0], gid[1], gid[2], gid[3],
		    gid[4], gid[5], gid[6], gid[7],
		    gid[8], gid[9], gid[10], gid[11],
		    gid[12], gid[13], gid[14], gid[15], port);

    count = 0;

    do {
	status = tsIbAtsServiceSet(ts_hca_handle,
				   port,
				   gid,
				   ip_addr,
				   ATS_TIMEOUT,
				   ats_service_set_comp,
				   ats_entry,
				   &tid);

	if (status < 0) {
	    TS_REPORT_WARN(MOD_UDAPL,
			   "tsIbAtsServiceSet() failed: %d", status);
	    free_ats_entry(ats_entry);
	    status = -EFAULT;
	    goto err;
	}

	status = down_interruptible(&ats_entry->sem);

	if (status) {
	    tsIbClientQueryCancel(tid);
	    free_ats_entry(ats_entry);
	    status = -EINTR;
	    goto err;
	}

	count++;

	if (ats_entry->status == -ETIMEDOUT)
	    TS_REPORT_STAGE(MOD_UDAPL, "Timed out %d on setting IP: %hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd.%hd for GID: %hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx",
			    count, ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3],
			    ip_addr[4], ip_addr[5], ip_addr[6], ip_addr[7],
			    ip_addr[8], ip_addr[9], ip_addr[10], ip_addr[11],
			    ip_addr[12], ip_addr[13], ip_addr[14], ip_addr[15],
			    gid[0], gid[1], gid[2], gid[3],
			    gid[4], gid[5], gid[6], gid[7],
			    gid[8], gid[9], gid[10], gid[11],
			    gid[12], gid[13], gid[14], gid[15]);

    } while ((ats_entry->status == -ETIMEDOUT) && (count < ATS_RETRIES));

    if (ats_entry->status) {

	TS_REPORT_WARN(MOD_UDAPL, "ATS service set completion status: %d",
		       ats_entry->status);
	free_ats_entry(ats_entry);
	status = -EFAULT;
	goto err;
    }

    TS_REPORT_STAGE(MOD_UDAPL, "IP/GID mapping set finished");

    free_ats_entry(ats_entry);

    return 0;

 err:
    return status;
}

static tINT32 ats_set_ipaddr(struct file *fp, unsigned long arg)
{
    ats_set_ipaddr_param_t  param;
    tINT32                  status;
    uint8_t                 gid[16];
    uint8_t                 ip_addr[16];
    unsigned long           flags;
    tUINT32                 hca_i, port_i;

    if (!arg) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL argument");
	status = -EINVAL;
	goto err;

    }

    if (copy_from_user(&param, (ats_set_ipaddr_param_t *) arg,
		       sizeof(ats_set_ipaddr_param_t))) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed");
	status = -EFAULT;
	goto err;
    }

    if (param.gid == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL GID");
	status = -EFAULT;
	goto err;
    }

    if ((param.port < 1) || (param.port > MAX_PORTS_PER_HCA)) {
	TS_REPORT_WARN(MOD_UDAPL, "Called with a bad port number: %d",
		       param.port);
	status = -EFAULT;
	goto err;
    }

    if (param.ip_addr == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL IP address");
	status = -EFAULT;
	goto err;
    }

    if (copy_from_user(gid, param.gid, 16)) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed");
	status = -EFAULT;
	goto err;
    }

    if (copy_from_user(ip_addr, param.ip_addr, 16)) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed");
	status = -EFAULT;
	goto err;
    }

    hca_i = find_hca(param.ts_hca_handle);
    if (hca_i == MAX_HCAS) {
	TS_REPORT_WARN(MOD_UDAPL, "Max HCAs (%d) exceeded", MAX_HCAS);
	status = -EFAULT;
	goto err;
    }
    if (!hcas[hca_i])
	hcas[hca_i] = param.ts_hca_handle;

    port_i = param.port - 1;

    spin_lock_irqsave(&ats_advert_lock, flags);
    if (!memcmp(ip_addr, ats_advert[hca_i][port_i].ip_addr, 16) &&
	!memcmp(gid, ats_advert[hca_i][port_i].gid, 16)) {

	spin_unlock_irqrestore(&ats_advert_lock, flags);
	TS_REPORT_STAGE(MOD_UDAPL, "IP address is already set");
	return 0;

    }

    status = ats_do_set_ipaddr(ip_addr, gid, param.ts_hca_handle, param.port);

    if (status) {
	ats_advert[hca_i][port_i].set_flag = 0;
	spin_unlock_irqrestore(&ats_advert_lock, flags);
	goto err;
    }

    memcpy(ats_advert[hca_i][port_i].ip_addr, ip_addr, 16);
    memcpy(ats_advert[hca_i][port_i].gid, gid, 16);
    ats_advert[hca_i][port_i].set_flag = 1;

    spin_unlock_irqrestore(&ats_advert_lock, flags);

    return 0;

 err:
    return status;
}


static void async_event_handler(tTS_IB_ASYNC_EVENT_RECORD event,
				void *arg)
{
    tINT32        status;
    unsigned long flags;
    tUINT32       hca_i, port_i;

    switch (event->event) {

    case TS_IB_PORT_ACTIVE:

	if ((event->modifier.port < 1) ||
	    (event->modifier.port > MAX_PORTS_PER_HCA)) {
	    TS_REPORT_WARN(MOD_UDAPL, "Async event on unexpected port: %d",
			   event->modifier.port);
	    return;
	}

	hca_i  = find_hca(event->device);
	if ((hca_i == MAX_HCAS) || (!hcas[hca_i]))
	    return;

	port_i = event->modifier.port - 1;

	TS_REPORT_STAGE(MOD_UDAPL, "Received a Port Active async event");
	spin_lock_irqsave(&ats_advert_lock, flags);
	if (!ats_advert[hca_i][port_i].set_flag) {
	    spin_unlock_irqrestore(&ats_advert_lock, flags);
	    return;
	}
	status = tsIbGidEntryGet(event->device, event->modifier.port, 0,
				 ats_advert[hca_i][port_i].gid);
	if (status) {
	    TS_REPORT_WARN(MOD_UDAPL, "tsIbGidEntryGet() failed: %d", status);
	    ats_advert[hca_i][port_i].set_flag = 0;
	} else {
	    status = ats_do_set_ipaddr(ats_advert[hca_i][port_i].ip_addr,
				       ats_advert[hca_i][port_i].gid, 
				       event->device, event->modifier.port);
	    if (status) {
		TS_REPORT_WARN(MOD_UDAPL, "ats_do_set_ipaddr() failed: %d", status);
		ats_advert[hca_i][port_i].set_flag = 0;
	    } else {
		ats_advert[hca_i][port_i].set_flag = 1;
	    }
	}
	spin_unlock_irqrestore(&ats_advert_lock, flags);
	
	spin_lock_irqsave(&ats_cache_lock, flags);
	memset(ats_cache, 0, sizeof(ats_cache));
	ats_cache_last = 0;
	spin_unlock_irqrestore(&ats_cache_lock, flags);

	break;

    default:
	TS_REPORT_WARN(MOD_UDAPL, "Received an unhandled async event: %d",
		       event->event);
	break;
    }
}

/* ------------------------------------------------------------------------- */
/* End of ATS processing                                                     */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* Path record lookup processing                                             */
/* ------------------------------------------------------------------------- */

static pr_entry_t *get_free_pr_entry(void)
{
    unsigned long   flags;
    pr_entry_t     *pr_entry;


    spin_lock_irqsave(&pr_free_list_lock, flags);

    if (pr_free_list == NULL) {

        spin_unlock_irqrestore(&pr_free_list_lock, flags);
        TS_REPORT_FATAL(MOD_UDAPL, "Path record pool exhausted!");
        return NULL;

    }

    pr_entry     = pr_free_list;
    pr_free_list = pr_free_list->next;

    spin_unlock_irqrestore(&pr_free_list_lock, flags);

    pr_entry->next = NULL;

    return pr_entry;
}


static inline void free_pr_entry(pr_entry_t *pr_entry)
{
    unsigned long   flags;


    spin_lock_irqsave(&pr_free_list_lock, flags);

    pr_entry->next = pr_free_list;
    pr_free_list   = pr_entry;

    spin_unlock_irqrestore(&pr_free_list_lock, flags);

    return;
}


static tINT32 gid_path_record_comp(tIP2PR_PATH_LOOKUP_ID plid,
				   tINT32                status,
				   tTS_IB_PORT           hw_port,
				   tTS_IB_DEVICE_HANDLE  ca,
				   tTS_IB_PATH_RECORD    path,
				   tPTR                  usr_arg)
{
    pr_entry_t *pr_entry;


    if (usr_arg == NULL) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL usr_arg");
	return -1;

    }

    pr_entry = (pr_entry_t *) usr_arg;

    pr_entry->status = status;

    if (status) {

	goto out;

    }

    if (path == NULL) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL path");
	goto err;

    }

    memcpy(&pr_entry->path_record, path, sizeof(tTS_IB_PATH_RECORD_STRUCT));

 out:

    up(&pr_entry->sem);

    return 0;

 err:

    up(&pr_entry->sem);

    return -1;
}


static tINT32 ip_path_record_comp(tIP2PR_PATH_LOOKUP_ID plid,
				  tINT32                status,
				  tUINT32               src_addr,
				  tUINT32               dst_addr,
				  tTS_IB_PORT           hw_port,
				  tTS_IB_DEVICE_HANDLE  ca,
				  tTS_IB_PATH_RECORD    path,
				  tPTR                  usr_arg)
{
    pr_entry_t *pr_entry;


    if (usr_arg == NULL) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL usr_arg");
	return -1;

    }

    pr_entry = (pr_entry_t *) usr_arg;

    pr_entry->status = status;

    if (status) {

	goto out;

    }

    if (path == NULL) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL path");
	goto err;

    }

    memcpy(&pr_entry->path_record, path, sizeof(tTS_IB_PATH_RECORD_STRUCT));

 out:

    up(&pr_entry->sem);

    return 0;

 err:

    up(&pr_entry->sem);

    return -1;
}


static tINT32 get_path_record(struct file *fp, unsigned long arg)
{
    path_record_param_t    param;
    tINT32                 status;
    pr_entry_t            *pr_entry;
    tIP2PR_PATH_LOOKUP_ID  plid;
    uint8_t                dst_ip_addr[16];
    uint8_t                src_gid[16];
    uint8_t                dst_gid[16];
    tTS_IB_PKEY            pkey;
    tUINT32                dst_addr;


    if (!arg) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL argument");
	status = -EINVAL;
	goto err;

    }

    if (copy_from_user(&param, (path_record_param_t *) arg,
		       sizeof(path_record_param_t))) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed");
	status = -EFAULT;
	goto err;
    }

    if (param.path_record == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL path_record");
	status = -EFAULT;
	goto err;
    }

    if (param.src_gid == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL source GID");
	status = -EFAULT;
	goto err;
    }

    if ((param.port < 1) || (param.port > MAX_PORTS_PER_HCA)) {
	TS_REPORT_WARN(MOD_UDAPL, "Called with a bad port number: %d",
		       param.port);
	status = -EFAULT;
	goto err;
    }

    if (param.dst_ip_addr == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL IP address");
	status = -EFAULT;
	goto err;
    }

    if (copy_from_user(src_gid, param.src_gid, 16)) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed");
	status = -EFAULT;
	goto err;
    }

    if (copy_from_user(dst_ip_addr, param.dst_ip_addr, 16)) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed");
	status = -EFAULT;
	goto err;
    }

    if (param.ats_flag) {

	status = ats_gid_lookup(dst_ip_addr, dst_gid, param.ts_hca_handle, param.port);

	if (status == -EINTR)
	    goto err;

	if (status < 0) {
	    TS_REPORT_WARN(MOD_UDAPL, "ats_gid_lookup() failed: %d", status);
	    goto err;
	}
    }

    pr_entry = get_free_pr_entry();

    if (pr_entry == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Failed to get a free pr_entry");
	status = -EFAULT;
	goto err;
    }

    pr_entry->status           = 0;
    pr_entry->user_path_record = param.path_record;

    sema_init(&pr_entry->sem, 0);

    if (param.ats_flag) {

	TS_REPORT_STAGE(MOD_UDAPL, "Looking up path record for SGID: %hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx DGID: %hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx",
			src_gid[0], src_gid[1], src_gid[2], src_gid[3],
			src_gid[4], src_gid[5], src_gid[6], src_gid[7],
			src_gid[8], src_gid[9], src_gid[10], src_gid[11],
			src_gid[12], src_gid[13], src_gid[14], src_gid[15],
			dst_gid[0], dst_gid[1], dst_gid[2], dst_gid[3],
			dst_gid[4], dst_gid[5], dst_gid[6], dst_gid[7],
			dst_gid[8], dst_gid[9], dst_gid[10], dst_gid[11],
			dst_gid[12], dst_gid[13], dst_gid[14], dst_gid[15]);
	pkey   = 0xFFFF;
	status = tsGid2prLookup(src_gid,
				dst_gid,
				pkey,
				gid_path_record_comp,
				pr_entry,
				&plid);

	if (status < 0) {
	    TS_REPORT_WARN(MOD_UDAPL,
			   "tsGid2prPathRecordLookup() lookup failed: %d", status);
	    free_pr_entry(pr_entry);
	    status = -EFAULT;
	    goto err;
	}

    } else {

	memcpy(&dst_addr, &dst_ip_addr[12], 4);
	status = tsIp2prPathRecordLookup(dst_addr, 0, 0, 0,
					 ip_path_record_comp, pr_entry, &plid);

	if (status < 0) {
	    TS_REPORT_WARN(MOD_UDAPL,
			   "tsIp2prPathRecordLookup() lookup failed: %d", status);
	    free_pr_entry(pr_entry);
	    status = -EFAULT;
	    goto err;
	}

    }

    status = down_interruptible(&pr_entry->sem);

    if (status) {
	if (param.ats_flag)
	    (void) tsGid2prCancel(plid);
	else
	    (void) tsIp2prPathRecordCancel(plid);
	free_pr_entry(pr_entry);
	status = -EINTR;
	goto err;
    }

    if (pr_entry->status) {

	TS_REPORT_WARN(MOD_UDAPL, "Path record lookup completion status: %d",
		       pr_entry->status);
	free_pr_entry(pr_entry);
	status = -EFAULT;
	goto err;
    }

    copy_to_user(pr_entry->user_path_record, &pr_entry->path_record,
		 sizeof(tTS_IB_PATH_RECORD_STRUCT));

    free_pr_entry(pr_entry);

    return 0;

 err:
    return status;
}

/* ------------------------------------------------------------------------- */
/* End of path record lookup processing                                      */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* SMR database processing                                                   */
/* ------------------------------------------------------------------------- */

static void smr_cookie_to_str(char *cookie, char *cookie_str)
{
    int i;

    for (i = 0; i < SMR_COOKIE_SIZE; i++) {

	if (cookie[i] < 10)
	    cookie_str[i] = cookie[i] + 48;
	else
	    cookie_str[i] = cookie[i] + 87;

    }

    cookie_str[i] = 0;
}


static inline void free_smr_rec(smr_rec_t *smr_rec)
{
    unsigned long flags;


    TS_ENTER(MOD_UDAPL);

    if (smr_rec->mrh_array != NULL)
	kfree(smr_rec->mrh_array);

    spin_lock_irqsave(&smr_free_list_lock, flags);

    smr_rec->next = smr_free_list;
    smr_free_list = smr_rec;

    spin_unlock_irqrestore(&smr_free_list_lock, flags);

    return;
}


static smr_rec_t *get_free_smr_rec(void)
{
    unsigned long  flags;
    smr_rec_t     *smr_rec;
    tUINT32        i;


    TS_ENTER(MOD_UDAPL);

    spin_lock_irqsave(&smr_free_list_lock, flags);

    if (smr_free_list == NULL) {

        spin_unlock_irqrestore(&smr_free_list_lock, flags);
        TS_REPORT_FATAL(MOD_UDAPL, "SMR record pool exhausted!");
        return NULL;

    }

    smr_rec       = smr_free_list;
    smr_free_list = smr_free_list->next;

    spin_unlock_irqrestore(&smr_free_list_lock, flags);

    smr_rec->next = NULL;

    smr_rec->mrh_array = (VAPI_mr_hndl_t *) kmalloc(MAX_MRH_PER_SMR *
						    sizeof(VAPI_mr_hndl_t),
						    GFP_KERNEL);

    if (smr_rec->mrh_array == NULL) {

	free_smr_rec(smr_rec);
	TS_REPORT_WARN(MOD_UDAPL, "Cannot allocate MRH array for SMR");
	return NULL;

    }

    for (i = 0; i < MAX_MRH_PER_SMR; i++)
	smr_rec->mrh_array[i] = VAPI_INVAL_HNDL;

    return smr_rec;
}


static tINT32 smr_insert(struct file *fp, unsigned long arg)
{
    smr_insert_param_t   param;
    tINT32               status;
    unsigned long        flags;
    smr_rec_t           *smr_rec;
    smr_clean_info_t    *smr_clean_info;
    tUINT32              smr_n;


    TS_ENTER(MOD_UDAPL);

    if (!arg) {

        status = -EINVAL;
        goto err;

    }

    if (copy_from_user(&param, (smr_insert_param_t *) arg,
                       sizeof(smr_insert_param_t))) {
        status = -EFAULT;
        goto err;
    }

    smr_rec = get_free_smr_rec();

    if (smr_rec == NULL) {
	status = -EFAULT;
	goto err;
    }

    smr_rec->initialized = 0;
    smr_rec->ref_count   = 1;

    if (copy_from_user(smr_rec->cookie, param.cookie, SMR_COOKIE_SIZE)) {

	status = -EFAULT;
	free_smr_rec(smr_rec);
	goto err;

    }

    spin_lock_irqsave(&smr_db_lock, flags);

    status = DaplHashTableInsert(smr_db, smr_rec->cookie,
                                 (unsigned long) smr_rec);

    spin_unlock_irqrestore(&smr_db_lock, flags);

    if (status && (status != -EEXIST)) {

	TS_REPORT_WARN(MOD_UDAPL, "Failed to insert into SMR database");
	free_smr_rec(smr_rec);
	goto err;

    }

    if (status == -EEXIST) {

	param.exists = 1;
	free_smr_rec(smr_rec);

    } else {

	smr_clean_info = ((resources_t *) fp->private_data)->smr_clean_info;

	spin_lock_irqsave(&smr_db_lock, flags);

	for (smr_n = 0; smr_n < MAX_SMR_PER_PROCESS; smr_n++) {

	    if (smr_clean_info[smr_n].smr_rec == NULL) {
		smr_clean_info[smr_n].smr_rec   = smr_rec;
		smr_clean_info[smr_n].mrh_index = MRH_INDEX_INVALID;
		break;
	    }

	}

	spin_unlock_irqrestore(&smr_db_lock, flags);

	if (smr_n == MAX_SMR_PER_PROCESS) {

	    TS_REPORT_WARN(MOD_UDAPL, "SMR array is full");
	    status = -EFAULT;
	    goto err;

	}

	param.exists = 0;

    }

    if (copy_to_user((smr_insert_param_t *) arg, &param,
		     sizeof(smr_insert_param_t))) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_to_user() failed");
	return -EFAULT;
    }

    return 0;

 err:
    return status;
}


static tINT32 smr_add_mrh(struct file *fp, unsigned long arg)
{
    smr_add_mrh_param_t  param;
    tINT32               status;
    unsigned long        flags;
    smr_rec_t           *smr_rec;
    smr_clean_info_t    *smr_clean_info;
    tUINT32              i;
    tUINT32              smr_n;
    char                 cookie[SMR_COOKIE_SIZE];
    char		 cookie_str[SMR_COOKIE_SIZE + 1];


    TS_ENTER(MOD_UDAPL);

    if (!arg) {

        status = -EINVAL;
        goto err;

    }

    if (copy_from_user(&param, (smr_add_mrh_param_t *) arg,
                       sizeof(smr_add_mrh_param_t))) {
        status = -EFAULT;
        goto err;
    }

    if (copy_from_user(cookie, param.cookie, SMR_COOKIE_SIZE)) {

	status = -EFAULT;
	goto err;

    }

    spin_lock_irqsave(&smr_db_lock, flags);

    status = DaplHashTableLookup(smr_db, cookie, (unsigned long *) &smr_rec);

    if (status) {

	spin_unlock_irqrestore(&smr_db_lock, flags);
	smr_cookie_to_str(cookie, cookie_str);
	TS_REPORT_WARN(MOD_UDAPL, "SMR record does not exist for cookie: %s",
				  cookie_str);
	status = -EFAULT;
	goto err;

    }

    if (memcmp(smr_rec->cookie, cookie, SMR_COOKIE_SIZE)) {

	spin_unlock_irqrestore(&smr_db_lock, flags);
	TS_REPORT_WARN(MOD_UDAPL, "Cookie mismatch");
	status = -EFAULT;
	goto err;

    }

    for (i = 0; i < MAX_MRH_PER_SMR; i++)
	if (smr_rec->mrh_array[i] == VAPI_INVAL_HNDL)
	    break;

    if (i == MAX_MRH_PER_SMR) {

	spin_unlock_irqrestore(&smr_db_lock, flags);
	TS_REPORT_WARN(MOD_UDAPL, "Memory region handle array is full");
	status = -EFAULT;
	goto err;

    }

    smr_rec->mrh_array[i] = param.mr_handle;

    smr_rec->initialized = 1;

    smr_clean_info = ((resources_t *) fp->private_data)->smr_clean_info;

    for (smr_n = 0; smr_n < MAX_SMR_PER_PROCESS; smr_n++) {

	if ((smr_clean_info[smr_n].smr_rec == smr_rec) &&
	    (smr_clean_info[smr_n].mrh_index == MRH_INDEX_INVALID)) {

	    smr_clean_info[smr_n].mrh_index = i;
	    break;

	}

    }

    spin_unlock_irqrestore(&smr_db_lock, flags);

    if (smr_n == MAX_SMR_PER_PROCESS) {

	smr_cookie_to_str(cookie, cookie_str);
	TS_REPORT_WARN(MOD_UDAPL, "No relevant cleanup SMR for cookie: %s",
		       cookie);
	status = -EFAULT;
	goto err;

    }

    return 0;

 err:
    return status;
}


static tINT32 smr_del_mrh(struct file *fp, unsigned long arg)
{
    smr_del_mrh_param_t  param;
    tINT32               status;
    unsigned long        flags;
    smr_rec_t           *smr_rec;
    smr_clean_info_t    *smr_clean_info;
    tUINT32              i;
    tUINT32              smr_n;
    char                 cookie[SMR_COOKIE_SIZE];
    char                 cookie_str[SMR_COOKIE_SIZE + 1];


    TS_ENTER(MOD_UDAPL);

    if (!arg) {

        status = -EINVAL;
        goto err;

    }

    if (copy_from_user(&param, (smr_del_mrh_param_t *) arg,
                       sizeof(smr_del_mrh_param_t))) {
        status = -EFAULT;
        goto err;
    }

    if (copy_from_user(cookie, param.cookie, SMR_COOKIE_SIZE)) {

	status = -EFAULT;
	goto err;

    }

    spin_lock_irqsave(&smr_db_lock, flags);

    status = DaplHashTableLookup(smr_db, cookie, (unsigned long *) &smr_rec);

    if (status) {

	spin_unlock_irqrestore(&smr_db_lock, flags);
	smr_cookie_to_str(cookie, cookie_str);
	TS_REPORT_WARN(MOD_UDAPL, "SMR record does not exist for cookie: %s",
		       cookie_str);
	status = -EFAULT;
	goto err;

    }

    if (memcmp(smr_rec->cookie, param.cookie, SMR_COOKIE_SIZE)) {

	spin_unlock_irqrestore(&smr_db_lock, flags);
	TS_REPORT_WARN(MOD_UDAPL, "Cookie mismatch");
	status = -EFAULT;
	goto err;

    }

    for (i = 0; i < MAX_MRH_PER_SMR; i++)
	if (smr_rec->mrh_array[i] == param.mr_handle)
	    break;

    if (i == MAX_MRH_PER_SMR) {

	spin_unlock_irqrestore(&smr_db_lock, flags);
	smr_cookie_to_str(cookie, cookie_str);
	TS_REPORT_WARN(MOD_UDAPL,
		       "Handle %x not found in MRH array for cookie: %s",
		       param.mr_handle, cookie_str);
	status = -EFAULT;
	goto err;

    }

    smr_rec->mrh_array[i] = VAPI_INVAL_HNDL;

    smr_clean_info = ((resources_t *) fp->private_data)->smr_clean_info;

    for (smr_n = 0; smr_n < MAX_SMR_PER_PROCESS; smr_n++) {

	if ((smr_clean_info[smr_n].smr_rec == smr_rec) &&
	    (smr_clean_info[smr_n].mrh_index == i)) {

	    smr_clean_info[smr_n].mrh_index = MRH_INDEX_INVALID;
	    break;

	}

    }

    for (i = 0; i < MAX_MRH_PER_SMR; i++)
	if (smr_rec->mrh_array[i] != VAPI_INVAL_HNDL)
	    break;

    if (i == MAX_MRH_PER_SMR)
	smr_rec->initialized = 0;

    spin_unlock_irqrestore(&smr_db_lock, flags);

    if (smr_n == MAX_SMR_PER_PROCESS) {

	smr_cookie_to_str(cookie, cookie_str);
	TS_REPORT_WARN(MOD_UDAPL, "No relevant cleanup SMR for cookie: %s",
		       cookie_str);
	status = -EFAULT;
	goto err;

    }

    return 0;

 err:
    return status;
}


static tINT32 smr_query(struct file *fp, unsigned long arg)
{
    smr_query_param_t    param;
    tINT32               status;
    unsigned long        flags;
    smr_rec_t           *smr_rec;
    smr_clean_info_t    *smr_clean_info;
    tUINT32              smr_n;
    tUINT32              i;
    char                 cookie[SMR_COOKIE_SIZE];
    char                 cookie_str[SMR_COOKIE_SIZE + 1];


    TS_ENTER(MOD_UDAPL);

    if (!arg) {

        status = -EINVAL;
        goto err;

    }

    if (copy_from_user(&param, (smr_query_param_t *) arg,
                       sizeof(smr_query_param_t))) {
        status = -EFAULT;
        goto err;
    }

    if (copy_from_user(cookie, param.cookie, SMR_COOKIE_SIZE)) {

	status = -EFAULT;
	goto err;

    }

    spin_lock_irqsave(&smr_db_lock, flags);

    status = DaplHashTableLookup(smr_db, cookie, (unsigned long *) &smr_rec);

    if (status || !smr_rec->initialized) {

	spin_unlock_irqrestore(&smr_db_lock, flags);
	param.ready = 0;
	goto out;

    }

    if (!smr_rec->ref_count || memcmp(smr_rec->cookie, cookie,
				       SMR_COOKIE_SIZE)) {

	spin_unlock_irqrestore(&smr_db_lock, flags);
	TS_REPORT_WARN(MOD_UDAPL, "SMR record contains unexpected data");
	status = -EFAULT;
	goto err;

    }

    smr_rec->ref_count++;

    for (i = 0; i < MAX_MRH_PER_SMR; i++)
	if (smr_rec->mrh_array[i] != VAPI_INVAL_HNDL)
	    break;

    if (i == MAX_MRH_PER_SMR) {

	spin_unlock_irqrestore(&smr_db_lock, flags);
	smr_cookie_to_str(cookie, cookie_str);
	TS_REPORT_WARN(MOD_UDAPL,
		       "MRH array is unexpectedly empty for cookie: %s",
		       cookie_str);
	status = -EFAULT;
	goto err;

    }

    param.mr_handle = smr_rec->mrh_array[i];

    smr_clean_info = ((resources_t *) fp->private_data)->smr_clean_info;

    for (smr_n = 0; smr_n < MAX_SMR_PER_PROCESS; smr_n++) {

	if (smr_clean_info[smr_n].smr_rec == NULL) {
	    smr_clean_info[smr_n].smr_rec   = smr_rec;
	    smr_clean_info[smr_n].mrh_index = MRH_INDEX_INVALID;
	    break;
	}

    }

    spin_unlock_irqrestore(&smr_db_lock, flags);

    if (smr_n == MAX_SMR_PER_PROCESS) {

	TS_REPORT_WARN(MOD_UDAPL, "SMR array is full");
	status = -EFAULT;
	goto err;

    }

    param.ready = 1;

 out:

    if (copy_to_user((smr_query_param_t *) arg, &param,
      		sizeof(smr_query_param_t))) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_to_user() failed");
	return -EFAULT;
    }

    return 0;

 err:
    return status;
}


static tINT32 smr_dec(struct file *fp, unsigned long arg)
{
    smr_insert_param_t   param;
    tINT32               status;
    unsigned long        flags;
    smr_rec_t           *smr_rec;
    smr_clean_info_t    *smr_clean_info;
    tUINT32              smr_n;
    char                 cookie[SMR_COOKIE_SIZE];
    char                 cookie_str[SMR_COOKIE_SIZE + 1];


    TS_ENTER(MOD_UDAPL);

    if (!arg) {

        status = -EINVAL;
        goto err;

    }

    if (copy_from_user(&param, (smr_dec_param_t *) arg,
                       sizeof(smr_dec_param_t))) {
        status = -EFAULT;
        goto err;
    }

    if (copy_from_user(cookie, param.cookie, SMR_COOKIE_SIZE)) {

	status = -EFAULT;
	goto err;

    }

    spin_lock_irqsave(&smr_db_lock, flags);

    status = DaplHashTableLookup(smr_db, cookie, (unsigned long *) &smr_rec);

    if (status) {

	spin_unlock_irqrestore(&smr_db_lock, flags);
	smr_cookie_to_str(cookie, cookie_str);
	TS_REPORT_WARN(MOD_UDAPL, "SMR record does not exist for cookie: %s",
		       cookie_str);
	status = -EFAULT;
	goto err;

    }

    if (!smr_rec->ref_count || memcmp(smr_rec->cookie, cookie,
	                               SMR_COOKIE_SIZE)) {

	spin_unlock_irqrestore(&smr_db_lock, flags);
	TS_REPORT_WARN(MOD_UDAPL, "SMR record contains unexpected data");
	status = -EFAULT;
	goto err;

    }

    smr_clean_info = ((resources_t *) fp->private_data)->smr_clean_info;

    for (smr_n = 0; smr_n < MAX_SMR_PER_PROCESS; smr_n++) {

	if ((smr_clean_info[smr_n].smr_rec == smr_rec) &&
	    (smr_clean_info[smr_n].mrh_index == MRH_INDEX_INVALID)) {

	    smr_clean_info[smr_n].smr_rec = NULL;
	    break;

	}

    }

    smr_rec->ref_count--;

    if (!smr_rec->ref_count) {

	status = DaplHashTableRemove(smr_db, cookie, NULL);

	if (status) {

	    spin_unlock_irqrestore(&smr_db_lock, flags);
	    TS_REPORT_WARN(MOD_UDAPL, "Failed to remove SMR record");
	    status = -EFAULT;
	    goto err;

	}
    }

    spin_unlock_irqrestore(&smr_db_lock, flags);

    if (smr_n == MAX_SMR_PER_PROCESS) {

	smr_cookie_to_str(cookie, cookie_str);
	TS_REPORT_WARN(MOD_UDAPL, "No relevant cleanup SMR for cookie: %s",
		       cookie_str);
	status = -EFAULT;
	goto err;

    }

    if (!smr_rec->ref_count)
	free_smr_rec(smr_rec);

    return 0;

 err:
    return status;
}


static void smr_clean(smr_clean_info_t *smr_clean_info)
{
    tUINT32        smr_n;
    unsigned long  flags;
    smr_rec_t      *cur_smr;
    tINT32         status;


    TS_ENTER(MOD_UDAPL);

    if (smr_clean_info == NULL) {

	TS_REPORT_WARN(MOD_UDAPL, "smr_clean_info is NULL");
	return;

    }

    spin_lock_irqsave(&smr_db_lock, flags);

    for (smr_n = 0; smr_n < MAX_SMR_PER_PROCESS; smr_n++) {

	cur_smr = smr_clean_info[smr_n].smr_rec;

	if (cur_smr != NULL) {

	    if (smr_clean_info[smr_n].mrh_index != MRH_INDEX_INVALID) {

		cur_smr->mrh_array[smr_clean_info[smr_n].mrh_index] =
		    VAPI_INVAL_HNDL;
		smr_clean_info[smr_n].mrh_index = MRH_INDEX_INVALID;

	    }

	    if (cur_smr->ref_count < 2) {

		status = DaplHashTableRemove(smr_db, cur_smr->cookie, NULL);

		if (status) {

		    TS_REPORT_WARN(MOD_UDAPL, "Failed to remove SMR record");
		    continue;

		}

		free_smr_rec(cur_smr);

	    } else
		cur_smr->ref_count--;

	}

    }

    spin_unlock_irqrestore(&smr_db_lock, flags);
}

static tINT32 smr_mutex_lock(struct file *fp)
{
    TS_ENTER(MOD_UDAPL);

    down(&shmem_sem);
    ((resources_t *) fp->private_data)->shmem_sem_flag = 1;

    return 0;
}

static tINT32 smr_mutex_unlock(struct file *fp)
{
    TS_ENTER(MOD_UDAPL);

    up(&shmem_sem);
    ((resources_t *) fp->private_data)->shmem_sem_flag = 0;

    return 0;
}

/* ------------------------------------------------------------------------- */
/* End of SMR database processing                                            */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* Misc. helper ioctls                                                       */
/* ------------------------------------------------------------------------- */

static tINT32 get_hca_ipaddr(struct file *fp, unsigned long arg)
{
    get_hca_ipaddr_param_t  param;
    tINT32                  status;
    uint8_t                 gid[16];
    uint8_t                 dev_gid[16];
    tINT32                  i;
    struct net_device       *dev;
    struct in_device        *inet_dev;
    tTS_IB_DEVICE_HANDLE    ca;
    tTS_IB_PORT             hw_port;
    tTS_IB_PKEY             pkey;


    if (!arg) {

	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL argument");
	status = -EINVAL;
	goto err;

    }

    if (copy_from_user(&param, (get_hca_ipaddr_param_t *) arg,
		       sizeof(get_hca_ipaddr_param_t))) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed");
	status = -EFAULT;
	goto err;
    }

    if (param.gid == NULL) {
	TS_REPORT_WARN(MOD_UDAPL, "Called with a NULL GID");
	status = -EFAULT;
	goto err;
    }

    if (copy_from_user(gid, param.gid, 16)) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_from_user() failed");
	status = -EFAULT;
	goto err;
    }

    TS_REPORT_STAGE(MOD_UDAPL, "Looking up IPoIB address for GID: %hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx",
		    gid[0], gid[1], gid[2], gid[3],
		    gid[4], gid[5], gid[6], gid[7],
		    gid[8], gid[9], gid[10], gid[11],
		    gid[12], gid[13], gid[14], gid[15]);

    param.ip_addr = 0;

    for (i = 0; i < MAX_NET_DEVICES; i++) {

	dev = dev_get_by_index(i);

	if ((dev != NULL) && (!strncmp(dev->name, "ib", 2))) {

	    inet_dev = dev->ip_ptr;

	    if (inet_dev != NULL) {

		status = tsIpoibDeviceHandle(dev, &ca, &hw_port, dev_gid, &pkey);

		if (status) {
		    TS_REPORT_WARN(MOD_UDAPL, "tsIpoibDeviceHandle() failed: %d",
				   status);
		    goto err;
		}

		if (!memcmp(gid, dev_gid, 16)) {

		    param.ip_addr = inet_dev->ifa_list->ifa_address;
		    TS_REPORT_STAGE(MOD_UDAPL,
				    "Found IPoIB address %d.%d.%d.%d for GID: %hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx%hx",
				    param.ip_addr & 0xff,
				    (param.ip_addr >> 8) & 0xff,
				    (param.ip_addr >> 16) & 0xff,
				    (param.ip_addr >> 24) & 0xff,
				    gid[0], gid[1], gid[2], gid[3],
				    gid[4], gid[5], gid[6], gid[7],
				    gid[8], gid[9], gid[10], gid[11],
				    gid[12], gid[13], gid[14], gid[15]);

		    break;

		}

	    }

	}
    }

    if (param.ip_addr == 0) {
	status = -ENOENT;
	goto err;
    }

    if (copy_to_user((get_hca_ipaddr_param_t *) arg, &param,
		     sizeof(get_hca_ipaddr_param_t))) {
	TS_REPORT_WARN(MOD_UDAPL, "copy_to_user() failed");
	return -EFAULT;
    }

    return 0;

 err:
    return status;
}

/* ------------------------------------------------------------------------- */
/* End of misc. helper ioctls                                                       */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* uDAPL device handling                                                     */
/* ------------------------------------------------------------------------- */

static int udapl_open(struct inode *inode, struct file *fp)
{
    resources_t *resources;

    TS_ENTER(MOD_UDAPL);

    if (fp->f_mode != FMODE_READ) {

        TS_REPORT_WARN(MOD_UDAPL,
		       "Attempt to open uDAPL device file in non-read mode");
        return -EINVAL;

    }

    fp->private_data = kmalloc(sizeof(resources_t), GFP_KERNEL);

    if (fp->private_data == NULL) {

	TS_REPORT_FATAL(MOD_UDAPL,
			"kmalloc for per-process resources failed");
	return -EFAULT;

    }

    resources = (resources_t *) fp->private_data;

    resources->wo_entries = kmalloc(sizeof(wo_entry_t *) *
				    MAX_WO_PER_PROCESS, GFP_KERNEL);

    if (resources->wo_entries == NULL) {

	TS_REPORT_FATAL(MOD_UDAPL,
			"kmalloc for process wait object resources failed");
	return -EFAULT;

    }

    memset(resources->wo_entries, 0,
	   sizeof(wo_entry_t *) * MAX_WO_PER_PROCESS);

    resources->smr_clean_info = kmalloc(sizeof(smr_clean_info_t) *
					MAX_SMR_PER_PROCESS, GFP_KERNEL);

    if (resources->smr_clean_info == NULL) {

	TS_REPORT_FATAL(MOD_UDAPL,
			"kmalloc for process SMR resources failed");
	return -EFAULT;

    }

    memset(resources->smr_clean_info, 0,
	   sizeof(smr_clean_info_t) * MAX_SMR_PER_PROCESS);

    resources->shmem_sem_flag = 0;

    return 0;
}


static int udapl_close(struct inode *inode, struct file *fp)
{
    resources_t *resources;

    TS_ENTER(MOD_UDAPL);

    resources = (resources_t *) fp->private_data;
    wo_clean(resources->wo_entries);
    smr_clean(resources->smr_clean_info);

    if (((resources_t *) fp->private_data)->shmem_sem_flag) {
	up(&shmem_sem);
    }

    kfree(resources->wo_entries);
    kfree(resources->smr_clean_info);
    kfree(fp->private_data);

    return 0;
}


static ssize_t udapl_read(struct file *fp, char *buf, size_t count,
			  loff_t *ppos)
{
    return 0;
}


static int udapl_ioctl(struct inode *inode, struct file *fp, unsigned int cmd,
		       unsigned long arg)
{
    tINT32 status;


    if ((_IOC_TYPE(cmd) != T_IOC_MAGIC) || (_IOC_NR(cmd) > T_IOC_MAXNR) ||
	(fp->private_data == NULL)) {

        status = -EINVAL;
        goto err;

    }

    switch (cmd) {

    case T_WAIT_OBJ_INIT:

	status = wait_obj_init(fp, arg);

	if (status < 0)
	    goto err;

	break;

    case T_WAIT_OBJ_WAIT:

	status = wait_obj_wait(arg);

	if (status < 0)
	    goto err;

	break;

    case T_WAIT_OBJ_WAKEUP:

	status = wait_obj_wakeup(arg);

	if (status < 0)
	    goto err;

	break;

    case T_WAIT_OBJ_DESTROY:

	status = wait_obj_destroy(fp, arg);

	if (status < 0)
	    goto err;

	break;

    case T_GET_KHCA_HNDL:

	status = get_khca_hndl(arg);

	if (status < 0)
	    goto err;

	break;

    case T_RELEASE_KHCA_HNDL:

	status = release_khca_hndl(arg);

	if (status < 0)
	    goto err;

	break;

    case T_SET_COMP_EVENTH:

	status = set_comp_eventh(arg);

	if (status < 0)
	    goto err;

	break;

    case T_CLEAR_COMP_EVENTH:

	status = clear_comp_eventh(arg);

	if (status < 0)
	    goto err;

	break;

    case T_SMR_MUTEX_LOCK:

	status = smr_mutex_lock(fp);

	if (status < 0)
	    goto err;

	break;

    case T_SMR_MUTEX_UNLOCK:

	status = smr_mutex_unlock(fp);

	if (status < 0)
	    goto err;

	break;

    case T_SMR_INSERT:

	status = smr_insert(fp, arg);

	if (status < 0)
	    goto err;

	break;

    case T_SMR_ADD_MRH:

	status = smr_add_mrh(fp, arg);

	if (status < 0)
	    goto err;

	break;

    case T_SMR_DEL_MRH:

	status = smr_del_mrh(fp, arg);

	if (status < 0)
	    goto err;

	break;

    case T_SMR_QUERY:

	status = smr_query(fp, arg);

	if (status < 0)
	    goto err;

	break;

    case T_SMR_DEC:

	status = smr_dec(fp, arg);

	if (status < 0)
	    goto err;

	break;

    case T_PATH_RECORD:

	status = get_path_record(fp, arg);

	if (status < 0)
	    goto err;

	break;

    case T_ATS_IPADDR_LOOKUP:

	status = ats_ipaddr_lookup(fp, arg);

	if (status < 0)
	    goto err;

	break;

    case T_ATS_SET_IPADDR:

	status = ats_set_ipaddr(fp, arg);

	if (status < 0)
	    goto err;

	break;

    case T_GET_HCA_IPADDR:

	status = get_hca_ipaddr(fp, arg);

	if (status < 0)
	    goto err;

	break;

    default:
	TS_REPORT_WARN(MOD_UDAPL, "Unrecognized ioctl: %d", cmd);
	status = -EINVAL;
	goto err;
    }

    return 0;

  err:
    return status;

}


static tINT32 reg_dev(void)
{
    static struct file_operations udapl_fops = {
        owner:THIS_MODULE,
        ioctl:udapl_ioctl,
        open:udapl_open,
        read:udapl_read,
        release:udapl_close,
    };
    tINT32 result;


    result = register_chrdev(udapl_major_number, UDAPL_DEVNAME, &udapl_fops);

    if (result < 0) {

	TS_REPORT_FATAL(MOD_UDAPL, "Device registration failed");

	TS_EXIT_FAIL(MOD_UDAPL, -1);

    }

    if (udapl_major_number == 0)
	udapl_major_number = result;

    return 0;
}

/* ------------------------------------------------------------------------- */
/* End of uDAPL device handling                                              */
/* ------------------------------------------------------------------------- */

static int __init udapl_init_module(void)
{
    tINT32         status;
    tINT32         entry_n;
    pr_entry_t    *pr_entry;
    ats_entry_t   *ats_entry;
    wo_entry_t    *wo_entry;
    smr_rec_t     *smr_rec;


    tsKernelTraceLevelSet(MOD_UDAPL, TRACE_FLOW_ALL);

    sema_init(&shmem_sem, 1);

    smr_db = DaplHashTableCreate(SMR_DB_SIZE);

    if (smr_db == NULL) {

	TS_REPORT_FATAL(MOD_UDAPL, "Failed to create SMR database");
	return -1;

    }

    wo_area = kmalloc(MAX_WOS * sizeof(wo_entry_t), GFP_KERNEL);

    if (wo_area == NULL) {

	TS_REPORT_FATAL(MOD_UDAPL,
			"kmalloc for wait object data structures failed");
	return -1;

    }

    pr_area = kmalloc(MAX_PRS * sizeof(pr_entry_t), GFP_KERNEL);

    if (pr_area == NULL) {

	TS_REPORT_FATAL(MOD_UDAPL,
			"kmalloc for path record data structures failed");
	kfree(wo_area);
	return -1;

    }

    ats_area = kmalloc(MAX_ATS_ENTRIES * sizeof(ats_entry_t), GFP_KERNEL);

    if (ats_area == NULL) {

	TS_REPORT_FATAL(MOD_UDAPL,
			"kmalloc for ATS data structures failed");
	kfree(wo_area);
	kfree(pr_area);
	return -1;

    }

    memset(ats_cache, 0, sizeof(ats_cache));
    memset(hcas, 0, sizeof(hcas));
    memset(ats_advert, 0, sizeof(ats_advert));
    ats_cache_last = 0;

    smr_rec_area = kmalloc(SMR_DB_SIZE * sizeof(smr_rec_t), GFP_KERNEL);

    if (smr_rec_area == NULL) {

	TS_REPORT_FATAL(MOD_UDAPL,
			"kmalloc for SMR records failed");
	kfree(wo_area);
	kfree(pr_area);
	kfree(ats_area);
	return -1;

    }

    status = reg_dev();

    if (status < 0) {

	kfree(wo_area);
	kfree(pr_area);
	kfree(ats_area);
	kfree(smr_rec_area);
	return -1;

    }

    pr_entry          = (pr_entry_t *) pr_area;
    pr_free_list      = pr_entry;
    ats_entry         = (ats_entry_t *) ats_area;
    ats_free_list     = ats_entry;
    wo_entry          = (wo_entry_t *) wo_area;
    wo_free_list      = wo_entry;

    for (entry_n = 0; entry_n < MAX_WOS - 1; entry_n++) {

	wo_entry->next = wo_entry + 1;
	wo_entry++;

    }

    wo_entry->next = NULL;

    for (entry_n = 0; entry_n < MAX_PRS - 1; entry_n++) {

	pr_entry->next = pr_entry + 1;
	pr_entry++;

    }

    pr_entry->next = NULL;

    for (entry_n = 0; entry_n < MAX_ATS_ENTRIES - 1; entry_n++) {

	ats_entry->next = ats_entry + 1;
	ats_entry++;

    }

    ats_entry->next = NULL;

    smr_rec       = (smr_rec_t *) smr_rec_area;
    smr_free_list = smr_rec;

    for (entry_n = 0; entry_n < SMR_DB_SIZE - 1; entry_n++) {

	smr_rec->next      = smr_rec + 1;
	smr_rec->mrh_array = NULL;
	smr_rec++;

    }

    smr_rec->next = NULL;

    {
	/* FIXME: We should do this for each device supported */
	tTS_IB_ASYNC_EVENT_RECORD_STRUCT event_record = {
	  .device = tsIbDeviceGetByIndex(0),
          .event  = TS_IB_PORT_ACTIVE,
        };

	status = tsIbAsyncEventHandlerRegister(&event_record,
					       async_event_handler,
					       NULL,
					       &async_handle);
    }

    if (status < 0) {
 	kfree(wo_area);
	kfree(pr_area);
	kfree(ats_area);
	kfree(smr_rec_area);
	return -1;
    }

    return 0;
}


static void udapl_cleanup_module(void)
{
    tINT32     status;

    kfree(wo_area);
    kfree(pr_area);
    kfree(ats_area);
    kfree(smr_rec_area);

    if (unregister_chrdev(udapl_major_number, UDAPL_DEVNAME) != 0)
        TS_REPORT_WARN(MOD_UDAPL, "Cannot unregister device");

    status = DaplHashTableDestroy(smr_db);

    if (status)
	TS_REPORT_WARN(MOD_UDAPL, "Failed to destroy SMR database");

    status = tsIbAsyncEventHandlerDeregister(async_handle);

    if (status)
	TS_REPORT_WARN(MOD_UDAPL, "Failed to deregister async handler");

    return;
}


module_init(udapl_init_module);
module_exit(udapl_cleanup_module);
