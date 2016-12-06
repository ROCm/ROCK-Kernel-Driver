/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <uapi/linux/kfd_ioctl.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <uapi/asm-generic/mman-common.h>
#include <asm/processor.h>
#include <linux/ptrace.h>

#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_dbgmgr.h"
#include "kfd_ipc.h"
#include "cik_regs.h"

static long kfd_ioctl(struct file *, unsigned int, unsigned long);
static int kfd_open(struct inode *, struct file *);
static int kfd_mmap(struct file *, struct vm_area_struct *);
static bool kfd_is_large_bar(struct kfd_dev *dev);

static const char kfd_dev_name[] = "kfd";

static const struct file_operations kfd_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = kfd_ioctl,
	.compat_ioctl = kfd_ioctl,
	.open = kfd_open,
	.mmap = kfd_mmap,
};

static int kfd_char_dev_major = -1;
static struct class *kfd_class;
struct device *kfd_device;

static char *kfd_devnode(struct device *dev, umode_t *mode)
{
	if (mode && dev->devt == MKDEV(kfd_char_dev_major, 0))
		*mode = 0666;

	return NULL;
}

int kfd_chardev_init(void)
{
	int err = 0;

	kfd_char_dev_major = register_chrdev(0, kfd_dev_name, &kfd_fops);
	err = kfd_char_dev_major;
	if (err < 0)
		goto err_register_chrdev;

	kfd_class = class_create(THIS_MODULE, kfd_dev_name);
	err = PTR_ERR(kfd_class);
	if (IS_ERR(kfd_class))
		goto err_class_create;

	kfd_class->devnode = kfd_devnode;

	kfd_device = device_create(kfd_class, NULL,
					MKDEV(kfd_char_dev_major, 0),
					NULL, kfd_dev_name);
	err = PTR_ERR(kfd_device);
	if (IS_ERR(kfd_device))
		goto err_device_create;

	return 0;

err_device_create:
	class_destroy(kfd_class);
err_class_create:
	unregister_chrdev(kfd_char_dev_major, kfd_dev_name);
err_register_chrdev:
	return err;
}

void kfd_chardev_exit(void)
{
	device_destroy(kfd_class, MKDEV(kfd_char_dev_major, 0));
	class_destroy(kfd_class);
	unregister_chrdev(kfd_char_dev_major, kfd_dev_name);
}

struct device *kfd_chardev(void)
{
	return kfd_device;
}


static int kfd_open(struct inode *inode, struct file *filep)
{
	struct kfd_process *process;
	bool is_32bit_user_mode;

	if (iminor(inode) != 0)
		return -ENODEV;

	is_32bit_user_mode = in_compat_syscall();

	if (is_32bit_user_mode) {
		dev_warn(kfd_device,
			"Process %d (32-bit) failed to open /dev/kfd\n"
			"32-bit processes are not supported by amdkfd\n",
			current->pid);
		return -EPERM;
	}

	process = kfd_create_process(filep);
	if (IS_ERR(process))
		return PTR_ERR(process);

	dev_dbg(kfd_device, "process %d opened, compat mode (32 bit) - %d\n",
		process->pasid, process->is_32bit_user_mode);

	return 0;
}

static int kfd_ioctl_get_version(struct file *filep, struct kfd_process *p,
					void *data)
{
	struct kfd_ioctl_get_version_args *args = data;

	args->major_version = KFD_IOCTL_MAJOR_VERSION;
	args->minor_version = KFD_IOCTL_MINOR_VERSION;

	return 0;
}

static int set_queue_properties_from_user(struct queue_properties *q_properties,
				struct kfd_ioctl_create_queue_args *args)
{
	if (args->queue_percentage > KFD_MAX_QUEUE_PERCENTAGE) {
		pr_err("kfd: queue percentage must be between 0 to KFD_MAX_QUEUE_PERCENTAGE\n");
		return -EINVAL;
	}

	if (args->queue_priority > KFD_MAX_QUEUE_PRIORITY) {
		pr_err("kfd: queue priority must be between 0 to KFD_MAX_QUEUE_PRIORITY\n");
		return -EINVAL;
	}

	if ((args->ring_base_address) &&
		(!access_ok(VERIFY_WRITE,
			(const void __user *) args->ring_base_address,
			sizeof(uint64_t)))) {
		pr_err("kfd: can't access ring base address\n");
		return -EFAULT;
	}

	if (!is_power_of_2(args->ring_size) && (args->ring_size != 0)) {
		pr_err("kfd: ring size must be a power of 2 or 0\n");
		return -EINVAL;
	}

	if (!access_ok(VERIFY_WRITE,
			(const void __user *) args->read_pointer_address,
			sizeof(uint32_t))) {
		pr_err("kfd: can't access read pointer\n");
		return -EFAULT;
	}

	if (!access_ok(VERIFY_WRITE,
			(const void __user *) args->write_pointer_address,
			sizeof(uint32_t))) {
		pr_err("kfd: can't access write pointer\n");
		return -EFAULT;
	}

	if (args->eop_buffer_address &&
		!access_ok(VERIFY_WRITE,
			(const void __user *) args->eop_buffer_address,
			sizeof(uint32_t))) {
		pr_debug("kfd: can't access eop buffer");
		return -EFAULT;
	}

	if (args->ctx_save_restore_address &&
		!access_ok(VERIFY_WRITE,
			(const void __user *) args->ctx_save_restore_address,
			sizeof(uint32_t))) {
		pr_debug("kfd: can't access ctx save restore buffer");
		return -EFAULT;
	}

	q_properties->is_interop = false;
	q_properties->queue_percent = args->queue_percentage;
	q_properties->priority = args->queue_priority;
	q_properties->queue_address = args->ring_base_address;
	q_properties->queue_size = args->ring_size;
	q_properties->read_ptr = (uint32_t *) args->read_pointer_address;
	q_properties->write_ptr = (uint32_t *) args->write_pointer_address;
	q_properties->eop_ring_buffer_address = args->eop_buffer_address;
	q_properties->eop_ring_buffer_size = args->eop_buffer_size;
	q_properties->ctx_save_restore_area_address =
			args->ctx_save_restore_address;
	q_properties->ctx_save_restore_area_size = args->ctx_save_restore_size;
	q_properties->ctl_stack_size = args->ctl_stack_size;
	if (args->queue_type == KFD_IOC_QUEUE_TYPE_COMPUTE ||
		args->queue_type == KFD_IOC_QUEUE_TYPE_COMPUTE_AQL)
		q_properties->type = KFD_QUEUE_TYPE_COMPUTE;
	else if (args->queue_type == KFD_IOC_QUEUE_TYPE_SDMA)
		q_properties->type = KFD_QUEUE_TYPE_SDMA;
	else
		return -ENOTSUPP;

	if (args->queue_type == KFD_IOC_QUEUE_TYPE_COMPUTE_AQL)
		q_properties->format = KFD_QUEUE_FORMAT_AQL;
	else
		q_properties->format = KFD_QUEUE_FORMAT_PM4;

	pr_debug("Queue Percentage (%d, %d)\n",
			q_properties->queue_percent, args->queue_percentage);

	pr_debug("Queue Priority (%d, %d)\n",
			q_properties->priority, args->queue_priority);

	pr_debug("Queue Address (0x%llX, 0x%llX)\n",
			q_properties->queue_address, args->ring_base_address);

	pr_debug("Queue Size (0x%llX, %u)\n",
			q_properties->queue_size, args->ring_size);

	pr_debug("Queue r/w Pointers (0x%llX, 0x%llX)\n",
			(uint64_t) q_properties->read_ptr,
			(uint64_t) q_properties->write_ptr);

	pr_debug("Queue Format (%d)\n", q_properties->format);

	pr_debug("Queue EOP (0x%llX)\n", q_properties->eop_ring_buffer_address);

	pr_debug("Queue CTX save arex (0x%llX)\n",
			q_properties->ctx_save_restore_area_address);

	return 0;
}

static int kfd_ioctl_create_queue(struct file *filep, struct kfd_process *p,
					void *data)
{
	struct kfd_ioctl_create_queue_args *args = data;
	struct kfd_dev *dev;
	int err = 0;
	unsigned int queue_id;
	struct kfd_process_device *pdd;
	struct queue_properties q_properties;

	memset(&q_properties, 0, sizeof(struct queue_properties));

	pr_debug("kfd: creating queue ioctl\n");

	err = set_queue_properties_from_user(&q_properties, args);
	if (err)
		return err;

	pr_debug("kfd: looking for gpu id 0x%x\n", args->gpu_id);
	dev = kfd_device_by_id(args->gpu_id);
	if (dev == NULL) {
		pr_debug("kfd: gpu id 0x%x was not found\n", args->gpu_id);
		return -EINVAL;
	}

	down_write(&p->lock);

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto err_bind_process;
	}

	pr_debug("kfd: creating queue for PASID %d on GPU 0x%x\n",
			p->pasid,
			dev->id);

	err = pqm_create_queue(&p->pqm, dev, filep, &q_properties, &queue_id);
	if (err != 0)
		goto err_create_queue;

	args->queue_id = queue_id;


	/* Return gpu_id as doorbell offset for mmap usage */
	args->doorbell_offset = KFD_MMAP_TYPE_DOORBELL;
	args->doorbell_offset |= KFD_MMAP_GPU_ID(args->gpu_id);
	args->doorbell_offset <<= PAGE_SHIFT;
	if (KFD_IS_SOC15(dev->device_info->asic_family))
		/* On SOC15 ASICs, doorbell allocation must be
		 * per-device, and independent from the per-process
		 * queue_id. Return the doorbell offset within the
		 * doorbell aperture to user mode.
		 */
		args->doorbell_offset |= q_properties.doorbell_off;

	up_write(&p->lock);

	pr_debug("kfd: queue id %d was created successfully\n", args->queue_id);

	pr_debug("ring buffer address == 0x%016llX\n",
			args->ring_base_address);

	pr_debug("read ptr address    == 0x%016llX\n",
			args->read_pointer_address);

	pr_debug("write ptr address   == 0x%016llX\n",
			args->write_pointer_address);

	return 0;

err_create_queue:
err_bind_process:
	up_write(&p->lock);
	return err;
}

static int kfd_ioctl_destroy_queue(struct file *filp, struct kfd_process *p,
					void *data)
{
	int retval;
	struct kfd_ioctl_destroy_queue_args *args = data;

	pr_debug("kfd: destroying queue id %d for PASID %d\n",
				args->queue_id,
				p->pasid);

	down_write(&p->lock);

	retval = pqm_destroy_queue(&p->pqm, args->queue_id);

	up_write(&p->lock);
	return retval;
}

static int kfd_ioctl_update_queue(struct file *filp, struct kfd_process *p,
					void *data)
{
	int retval;
	struct kfd_ioctl_update_queue_args *args = data;
	struct queue_properties properties;

	if (args->queue_percentage > KFD_MAX_QUEUE_PERCENTAGE) {
		pr_err("kfd: queue percentage must be between 0 to KFD_MAX_QUEUE_PERCENTAGE\n");
		return -EINVAL;
	}

	if (args->queue_priority > KFD_MAX_QUEUE_PRIORITY) {
		pr_err("kfd: queue priority must be between 0 to KFD_MAX_QUEUE_PRIORITY\n");
		return -EINVAL;
	}

	if ((args->ring_base_address) &&
		(!access_ok(VERIFY_WRITE,
			(const void __user *) args->ring_base_address,
			sizeof(uint64_t)))) {
		pr_err("kfd: can't access ring base address\n");
		return -EFAULT;
	}

	if (!is_power_of_2(args->ring_size) && (args->ring_size != 0)) {
		pr_err("kfd: ring size must be a power of 2 or 0\n");
		return -EINVAL;
	}

	properties.queue_address = args->ring_base_address;
	properties.queue_size = args->ring_size;
	properties.queue_percent = args->queue_percentage;
	properties.priority = args->queue_priority;

	pr_debug("kfd: updating queue id %d for PASID %d\n",
			args->queue_id, p->pasid);

	down_write(&p->lock);

	retval = pqm_update_queue(&p->pqm, args->queue_id, &properties);

	up_write(&p->lock);

	return retval;
}

static int kfd_ioctl_set_cu_mask(struct file *filp, struct kfd_process *p,
					void *data)
{
	int retval;
	const int max_num_cus = 1024;
	struct kfd_ioctl_set_cu_mask_args *args = data;
	struct queue_properties properties;
	uint32_t __user *cu_mask_ptr = (uint32_t __user *)args->cu_mask_ptr;
	size_t cu_mask_size = sizeof(uint32_t) * (args->num_cu_mask / 32);

	if ((args->num_cu_mask % 32) != 0) {
		pr_debug("kfd: num_cu_mask (0x%x) must be a multiple of 32",
				args->num_cu_mask);
		return -EINVAL;
	}

	properties.cu_mask_count = args->num_cu_mask;
	if (properties.cu_mask_count == 0) {
		pr_debug("kfd: CU Mask cannot be 0");
		return -EINVAL;
	}

	/* To prevent an unreasonably large CU mask size, set an arbitrary
	 * limit of max_num_cus bits.  We can then just drop any CU mask bits
	 * past max_num_cus bits and just use the first max_num_cus bits.
	 */
	if (properties.cu_mask_count > max_num_cus) {
		pr_debug("kfd: CU mask cannot be greater than 1024 bits");
		properties.cu_mask_count = max_num_cus;
		cu_mask_size = sizeof(uint32_t) * (max_num_cus/32);
	}

	properties.cu_mask = kzalloc(cu_mask_size, GFP_KERNEL);
	if (!properties.cu_mask)
		return -ENOMEM;

	retval = copy_from_user(properties.cu_mask, cu_mask_ptr, cu_mask_size);
	if (retval) {
		pr_debug("kfd: Could not copy cu mask from userspace");
		kfree(properties.cu_mask);
		return -EFAULT;
	}

	down_write(&p->lock);

	retval = pqm_set_cu_mask(&p->pqm, args->queue_id, &properties);

	up_write(&p->lock);

	return retval;
}

static int kfd_ioctl_set_memory_policy(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_memory_policy_args *args = data;
	struct kfd_dev *dev;
	int err = 0;
	struct kfd_process_device *pdd;
	enum cache_policy default_policy, alternate_policy;

	if (args->default_policy != KFD_IOC_CACHE_POLICY_COHERENT
	    && args->default_policy != KFD_IOC_CACHE_POLICY_NONCOHERENT) {
		return -EINVAL;
	}

	if (args->alternate_policy != KFD_IOC_CACHE_POLICY_COHERENT
	    && args->alternate_policy != KFD_IOC_CACHE_POLICY_NONCOHERENT) {
		return -EINVAL;
	}

	dev = kfd_device_by_id(args->gpu_id);
	if (dev == NULL)
		return -EINVAL;

	down_write(&p->lock);

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto out;
	}

	default_policy = (args->default_policy == KFD_IOC_CACHE_POLICY_COHERENT)
			 ? cache_policy_coherent : cache_policy_noncoherent;

	alternate_policy =
		(args->alternate_policy == KFD_IOC_CACHE_POLICY_COHERENT)
		   ? cache_policy_coherent : cache_policy_noncoherent;

	if (!dev->dqm->ops.set_cache_memory_policy(dev->dqm,
				&pdd->qpd,
				default_policy,
				alternate_policy,
				(void __user *)args->alternate_aperture_base,
				args->alternate_aperture_size))
		err = -EINVAL;

out:
	up_write(&p->lock);

	return err;
}

static int kfd_ioctl_set_trap_handler(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_trap_handler_args *args = data;
	struct kfd_dev *dev;
	int err = 0;
	struct kfd_process_device *pdd;

	dev = kfd_device_by_id(args->gpu_id);
	if (dev == NULL)
		return -EINVAL;

	down_write(&p->lock);

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto out;
	}

	if (dev->dqm->ops.set_trap_handler(dev->dqm,
					&pdd->qpd,
					args->tba_addr,
					args->tma_addr))
		err = -EINVAL;

out:
	up_write(&p->lock);

	return err;
}

static int
kfd_ioctl_dbg_register(struct file *filep, struct kfd_process *p, void *data)
{
	long status = -EFAULT;
	struct kfd_ioctl_dbg_register_args *args = data;
	struct kfd_dev *dev;
	struct kfd_dbgmgr *dbgmgr_ptr;
	struct kfd_process_device *pdd;
	bool create_ok = false;

	pr_debug("kfd:dbg: %s\n", __func__);

	dev = kfd_device_by_id(args->gpu_id);
	if (!dev) {
		dev_info(NULL, "Error! kfd: In func %s >> getting device by id failed\n",
				__func__);
		return status;
	}

	down_write(&p->lock);
	mutex_lock(get_dbgmgr_mutex());

	/* make sure that we have pdd, if this the first queue created for
	 * this process
	 */
	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		mutex_unlock(get_dbgmgr_mutex());
		up_write(&p->lock);
		return PTR_ERR(pdd);
	}

	if (dev->dbgmgr == NULL) {
		/* In case of a legal call, we have no dbgmgr yet */

		create_ok = kfd_dbgmgr_create(&dbgmgr_ptr, dev);
		if (create_ok) {
			status = kfd_dbgmgr_register(dbgmgr_ptr, p);
			if (status != 0)
				kfd_dbgmgr_destroy(dbgmgr_ptr);
			else
				dev->dbgmgr = dbgmgr_ptr;
		}
	}

	mutex_unlock(get_dbgmgr_mutex());
	up_write(&p->lock);

	return status;
}

static int kfd_ioctl_dbg_unregister(struct file *filep,
				struct kfd_process *p, void *data)
{
	long status = -EFAULT;
	struct kfd_ioctl_dbg_unregister_args *args = data;
	struct kfd_dev *dev;

	dev = kfd_device_by_id(args->gpu_id);
	if (!dev) {
		dev_info(NULL, "Error! kfd: In func %s >> getting device by id failed\n",
				__func__);
		return -EINVAL;
	}

	if (dev->device_info->asic_family == CHIP_CARRIZO) {
		pr_debug("kfd_ioctl_dbg_unregister not supported on CZ\n");
		return -EINVAL;
	}

	mutex_lock(get_dbgmgr_mutex());

	status = kfd_dbgmgr_unregister(dev->dbgmgr, p);
	if (status == 0) {
		kfd_dbgmgr_destroy(dev->dbgmgr);
		dev->dbgmgr = NULL;
	}

	mutex_unlock(get_dbgmgr_mutex());

	return status;
}

/*
 * Parse and generate variable size data structure for address watch.
 * Total size of the buffer and # watch points is limited in order
 * to prevent kernel abuse. (no bearing to the much smaller HW limitation
 * which is enforced by dbgdev module.
 * please also note that the watch address itself are not "copied from user",
 * since it be set into the HW in user mode values.
 *
 */

static int
kfd_ioctl_dbg_address_watch(struct file *filep,
		struct kfd_process *p,
		void *data)
{
	long status = -EFAULT;
	struct kfd_ioctl_dbg_address_watch_args *args = data;
	struct kfd_dev *dev;
	struct dbg_address_watch_info aw_info;
	unsigned char *args_buff = NULL;
	unsigned int args_idx = 0;
	void __user *cmd_from_user;
	uint64_t watch_mask_value = 0;

	memset((void *) &aw_info, 0, sizeof(struct dbg_address_watch_info));

	dev = kfd_device_by_id(args->gpu_id);
	if (!dev) {
		dev_info(NULL,
		"Error! kfd: In func %s >> get device by id failed\n",
		__func__);
		return -EFAULT;
	}

	cmd_from_user = (void __user *) args->content_ptr;

	if (args->buf_size_in_bytes > MAX_ALLOWED_AW_BUFF_SIZE ||
	   (args->buf_size_in_bytes <= sizeof(*args)))
		return -EINVAL;

	/* this is the actual buffer to work with */
	args_buff = memdup_user(cmd_from_user,
			args->buf_size_in_bytes - sizeof(*args));

	if (IS_ERR(args_buff))
		return PTR_ERR(args_buff);

	aw_info.process = p;

	aw_info.num_watch_points =
		*((uint32_t *)(&args_buff[args_idx]));
	args_idx += sizeof(aw_info.num_watch_points);

	aw_info.watch_mode =
		(enum HSA_DBG_WATCH_MODE *) &args_buff[args_idx];
	args_idx += sizeof(enum HSA_DBG_WATCH_MODE) *
					aw_info.num_watch_points;

	/* set watch address base pointer to point on the array base
	 * within args_buff
	 */

	aw_info.watch_address = (uint64_t *) &args_buff[args_idx];

	/* skip over the addresses buffer */
	args_idx += sizeof(aw_info.watch_address) *
					aw_info.num_watch_points;

	if (args_idx >= args->buf_size_in_bytes) {
		status = -EINVAL;
		goto out;
	}

	watch_mask_value = (uint64_t) args_buff[args_idx];

	if (watch_mask_value > 0) {
		/* there is an array of masks */

		/* set watch mask base pointer to point on the array
		 * base within args_buff
		 */
		aw_info.watch_mask = (uint64_t *) &args_buff[args_idx];

		/* skip over the masks buffer */
		args_idx += sizeof(aw_info.watch_mask) *
					aw_info.num_watch_points;
	}

	else
		/* just the NULL mask, set to NULL and skip over it */
	{
		aw_info.watch_mask = NULL;
		args_idx += sizeof(aw_info.watch_mask);
	}

	if (args_idx > args->buf_size_in_bytes) {
		status = -EINVAL;
		goto out;
	}

	/* Currently HSA Event is not supported for DBG */
	aw_info.watch_event = NULL;

	mutex_lock(get_dbgmgr_mutex());

	status = kfd_dbgmgr_address_watch(dev->dbgmgr, &aw_info);

	mutex_unlock(get_dbgmgr_mutex());

out:
	kfree(args_buff);

	return status;
}

/*
 * Parse and generate fixed size data structure for wave control.
 * Buffer is generated in a "packed" form, for avoiding structure
 * packing/pending dependencies.
 */

static int
kfd_ioctl_dbg_wave_control(struct file *filep, struct kfd_process *p,
		void *data)
{
	long status = -EFAULT;
	struct kfd_ioctl_dbg_wave_control_args *args = data;
	struct kfd_dev *dev;
	struct dbg_wave_control_info wac_info;
	unsigned char *args_buff = NULL;
	unsigned int args_idx = 0;
	void __user *cmd_from_user;
	uint32_t computed_buff_size;

	memset((void *) &wac_info, 0, sizeof(struct dbg_wave_control_info));

	/* we use compact form, independent of the packing attribute value */

	computed_buff_size = sizeof(*args) +
				sizeof(wac_info.mode) +
				sizeof(wac_info.operand) +
				sizeof(wac_info.dbgWave_msg.DbgWaveMsg) +
				sizeof(wac_info.dbgWave_msg.MemoryVA) +
				sizeof(wac_info.trapId);


	dev_info(NULL, "kfd: In func %s - start\n", __func__);

	dev = kfd_device_by_id(args->gpu_id);
	if (!dev) {
		dev_info(NULL, "Error! kfd: In func %s >> getting device by id failed\n",
				__func__);
		return -EFAULT;
	}

	/* input size must match the computed "compact" size */

	if (args->buf_size_in_bytes != computed_buff_size) {
		dev_info(NULL,
			 "Error! kfd: In func %s >> size mismatch, computed : actual %u : %u\n",
			__func__, args->buf_size_in_bytes,
			computed_buff_size);
		return -EINVAL;
	}

	cmd_from_user = (void __user *) args->content_ptr;

	/* copy the entire buffer from user */

	args_buff = memdup_user(cmd_from_user,
			args->buf_size_in_bytes - sizeof(*args));
	if (IS_ERR(args_buff))
		return PTR_ERR(args_buff);

	if (copy_from_user(args_buff,
			(void __user *) args->content_ptr,
			args->buf_size_in_bytes - sizeof(*args))) {
		dev_info(NULL,
		"Error! kfd: In func %s >> copy_from_user failed\n",
		 __func__);
		goto out;
	}

	/* move ptr to the start of the "pay-load" area */

	wac_info.process = p;

	wac_info.operand =
		*((enum HSA_DBG_WAVEOP *)(&args_buff[args_idx]));
	args_idx += sizeof(wac_info.operand);

	wac_info.mode =
		*((enum HSA_DBG_WAVEMODE *)(&args_buff[args_idx]));
	args_idx += sizeof(wac_info.mode);

	wac_info.trapId = *((uint32_t *)(&args_buff[args_idx]));
	args_idx += sizeof(wac_info.trapId);

	wac_info.dbgWave_msg.DbgWaveMsg.WaveMsgInfoGen2.Value =
		*((uint32_t *)(&args_buff[args_idx]));
	wac_info.dbgWave_msg.MemoryVA = NULL;

	mutex_lock(get_dbgmgr_mutex());

	dev_info(NULL,
		"kfd: In func %s >> calling dbg manager process %p, operand %u, mode %u, trapId %u, message %u\n",
		__func__, wac_info.process, wac_info.operand,
		wac_info.mode, wac_info.trapId,
		wac_info.dbgWave_msg.DbgWaveMsg.WaveMsgInfoGen2.Value);

	status = kfd_dbgmgr_wave_control(dev->dbgmgr, &wac_info);

	dev_info(NULL, "kfd: In func %s >> returned status of dbg manager is %ld\n",
			__func__, status);

	mutex_unlock(get_dbgmgr_mutex());

out:
	kfree(args_buff);

	return status;
}

static int kfd_ioctl_get_clock_counters(struct file *filep,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_clock_counters_args *args = data;
	struct kfd_dev *dev;
	struct timespec64 time;

	dev = kfd_device_by_id(args->gpu_id);
	if (dev)
		/* Reading GPU clock counter from KGD */
		args->gpu_clock_counter =
			dev->kfd2kgd->get_gpu_clock_counter(dev->kgd);
	else
		/* Node without GPU resource */
		args->gpu_clock_counter = 0;

	/* No access to rdtsc. Using raw monotonic time */
	getrawmonotonic64(&time);
	args->cpu_clock_counter = (uint64_t)timespec64_to_ns(&time);

	get_monotonic_boottime64(&time);
	args->system_clock_counter = (uint64_t)timespec64_to_ns(&time);

	/* Since the counter is in nano-seconds we use 1GHz frequency */
	args->system_clock_freq = 1000000000;

	return 0;
}


static int kfd_ioctl_get_process_apertures(struct file *filp,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_process_apertures_args *args = data;
	struct kfd_process_device_apertures *pAperture;
	struct kfd_process_device *pdd;

	dev_dbg(kfd_device, "get apertures for PASID %d", p->pasid);

	args->num_of_nodes = 0;

	down_write(&p->lock);

	/*if the process-device list isn't empty*/
	if (kfd_has_process_device_data(p)) {
		/* Run over all pdd of the process */
		pdd = kfd_get_first_process_device_data(p);
		do {
			pAperture =
				&args->process_apertures[args->num_of_nodes];
			pAperture->gpu_id = pdd->dev->id;
			pAperture->lds_base = pdd->lds_base;
			pAperture->lds_limit = pdd->lds_limit;
			pAperture->gpuvm_base = pdd->gpuvm_base;
			pAperture->gpuvm_limit = pdd->gpuvm_limit;
			pAperture->scratch_base = pdd->scratch_base;
			pAperture->scratch_limit = pdd->scratch_limit;

			dev_dbg(kfd_device,
				"node id %u\n", args->num_of_nodes);
			dev_dbg(kfd_device,
				"gpu id %u\n", pdd->dev->id);
			dev_dbg(kfd_device,
				"lds_base %llX\n", pdd->lds_base);
			dev_dbg(kfd_device,
				"lds_limit %llX\n", pdd->lds_limit);
			dev_dbg(kfd_device,
				"gpuvm_base %llX\n", pdd->gpuvm_base);
			dev_dbg(kfd_device,
				"gpuvm_limit %llX\n", pdd->gpuvm_limit);
			dev_dbg(kfd_device,
				"scratch_base %llX\n", pdd->scratch_base);
			dev_dbg(kfd_device,
				"scratch_limit %llX\n", pdd->scratch_limit);

			args->num_of_nodes++;
		} while ((pdd = kfd_get_next_process_device_data(p, pdd)) !=
				NULL &&
				(args->num_of_nodes < NUM_OF_SUPPORTED_GPUS));
	}

	up_write(&p->lock);

	return 0;
}

static int kfd_ioctl_get_process_apertures_new(struct file *filp,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_process_apertures_new_args *args = data;
	struct kfd_process_device_apertures *pa;
	struct kfd_process_device *pdd;
	uint32_t nodes = 0;
	int ret;

	dev_dbg(kfd_device, "get apertures for PASID %d", p->pasid);

	if (args->num_of_nodes == 0) {
		/* Return number of nodes, so that user space can alloacate
		 * sufficient memory
		 */
		down_write(&p->lock);

		if (!kfd_has_process_device_data(p)) {
			up_write(&p->lock);
			return 0;
		}

		/* Run over all pdd of the process */
		pdd = kfd_get_first_process_device_data(p);
		do {
			args->num_of_nodes++;
		} while ((pdd =
			kfd_get_next_process_device_data(p, pdd)) != NULL);

		up_write(&p->lock);
		return 0;
	}

	/* Fill in process-aperture information for all available
	 * nodes, but not more than args->num_of_nodes as that is
	 * the amount of memory allocated by user
	 */
	pa = kzalloc((sizeof(struct kfd_process_device_apertures) *
				args->num_of_nodes), GFP_KERNEL);
	if (!pa)
		return -ENOMEM;

	down_write(&p->lock);

	if (!kfd_has_process_device_data(p)) {
		up_write(&p->lock);
		args->num_of_nodes = 0;
		kfree(pa);
		return 0;
	}

	/* Run over all pdd of the process */
	pdd = kfd_get_first_process_device_data(p);
	do {
		pa[nodes].gpu_id = pdd->dev->id;
		pa[nodes].lds_base = pdd->lds_base;
		pa[nodes].lds_limit = pdd->lds_limit;
		pa[nodes].gpuvm_base = pdd->gpuvm_base;
		pa[nodes].gpuvm_limit = pdd->gpuvm_limit;
		pa[nodes].scratch_base = pdd->scratch_base;
		pa[nodes].scratch_limit = pdd->scratch_limit;

		dev_dbg(kfd_device,
			"gpu id %u\n", pdd->dev->id);
		dev_dbg(kfd_device,
			"lds_base %llX\n", pdd->lds_base);
		dev_dbg(kfd_device,
			"lds_limit %llX\n", pdd->lds_limit);
		dev_dbg(kfd_device,
			"gpuvm_base %llX\n", pdd->gpuvm_base);
		dev_dbg(kfd_device,
			"gpuvm_limit %llX\n", pdd->gpuvm_limit);
		dev_dbg(kfd_device,
			"scratch_base %llX\n", pdd->scratch_base);
		dev_dbg(kfd_device,
			"scratch_limit %llX\n", pdd->scratch_limit);
		nodes++;
	} while (
		(pdd = kfd_get_next_process_device_data(p, pdd)) != NULL &&
		(nodes < args->num_of_nodes));
	up_write(&p->lock);

	args->num_of_nodes = nodes;
	ret = copy_to_user(
			(void __user *)args->kfd_process_device_apertures_ptr,
			pa,
			(nodes * sizeof(struct kfd_process_device_apertures)));
	kfree(pa);
	return ret ? -EFAULT : 0;
}

static int
kfd_ioctl_create_event(struct file *filp, struct kfd_process *p, void *data)
{
	struct kfd_ioctl_create_event_args *args = data;
	struct kfd_dev *kfd;
	struct kfd_process_device *pdd;
	int err = -EINVAL;
	void *mem, *kern_addr = NULL;

	pr_debug("amdkfd: Event page offset 0x%llx\n", args->event_page_offset);

	if (args->event_page_offset) {
		kfd = kfd_device_by_id(GET_GPU_ID(args->event_page_offset));
		if (!kfd) {
			pr_err("amdkfd: can't find kfd device\n");
			return -EFAULT;
		}
		if (KFD_IS_DGPU(kfd->device_info->asic_family)) {
			down_write(&p->lock);
			pdd = kfd_bind_process_to_device(kfd, p);
			if (IS_ERR(pdd)) {
				err = PTR_ERR(pdd);
				up_write(&p->lock);
				return -EFAULT;
			}
			mem = kfd_process_device_translate_handle(pdd,
				GET_IDR_HANDLE(args->event_page_offset));
			if (!mem) {
				pr_err("amdkfd: can't find BO offset is 0x%llx\n",
						args->event_page_offset);
				up_write(&p->lock);
				return -EFAULT;
			}
			up_write(&p->lock);

			/* Map dGPU gtt BO to kernel */
			kfd->kfd2kgd->map_gtt_bo_to_kernel(kfd->kgd,
					mem, &kern_addr);
		}
	}

	err = kfd_event_create(filp, p,
			args->event_type,
			args->auto_reset != 0,
			args->node_id,
			&args->event_id,
			&args->event_trigger_data,
			&args->event_page_offset,
			&args->event_slot_index,
			kern_addr);

	return err;
}

static int
kfd_ioctl_destroy_event(struct file *filp, struct kfd_process *p, void *data)
{
	struct kfd_ioctl_destroy_event_args *args = data;

	return kfd_event_destroy(p, args->event_id);
}

static int
kfd_ioctl_set_event(struct file *filp, struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_event_args *args = data;

	return kfd_set_event(p, args->event_id);
}

static int
kfd_ioctl_reset_event(struct file *filp, struct kfd_process *p, void *data)
{
	struct kfd_ioctl_reset_event_args *args = data;

	return kfd_reset_event(p, args->event_id);
}

static int
kfd_ioctl_wait_events(struct file *filp, struct kfd_process *p, void *data)
{
	struct kfd_ioctl_wait_events_args *args = data;
	enum kfd_event_wait_result wait_result;
	int err;

	err = kfd_wait_on_events(p, args->num_events,
			(void __user *)args->events_ptr,
			(args->wait_for_all != 0),
			args->timeout, &wait_result);

	args->wait_result = wait_result;

	return err;
}
static int kfd_ioctl_alloc_scratch_memory(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_alloc_memory_of_scratch_args *args = data;
	struct kfd_process_device *pdd;
	struct kfd_dev *dev;
	long err;

	if (args->size == 0)
		return -EINVAL;

	dev = kfd_device_by_id(args->gpu_id);
	if (dev == NULL)
		return -EINVAL;

	down_write(&p->lock);

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto bind_process_to_device_fail;
	}

	pdd->sh_hidden_private_base_vmid = args->va_addr;
	pdd->qpd.sh_hidden_private_base = args->va_addr;

	up_write(&p->lock);

	if (dev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS &&
	    pdd->qpd.vmid != 0) {
		err = dev->kfd2kgd->alloc_memory_of_scratch(
			dev->kgd, args->va_addr, pdd->qpd.vmid);
		if (err != 0)
			goto alloc_memory_of_scratch_failed;
	}

	return 0;

bind_process_to_device_fail:
	up_write(&p->lock);
alloc_memory_of_scratch_failed:
	return -EFAULT;
}

bool kfd_is_large_bar(struct kfd_dev *dev)
{
	struct kfd_local_mem_info mem_info;

	if (debug_largebar) {
		pr_debug("amdkfd: simulate large-bar allocation on non large-bar machine\n");
		return true;
	}

	if (!KFD_IS_DGPU(dev->device_info->asic_family))
		return false;

	dev->kfd2kgd->get_local_mem_info(dev->kgd, &mem_info);
	if (mem_info.local_mem_size_private == 0 &&
			mem_info.local_mem_size_public > 0)
		return true;
	return false;
}

static int kfd_ioctl_alloc_memory_of_gpu(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_alloc_memory_of_gpu_args *args = data;
	struct kfd_process_device *pdd;
	void *mem;
	struct kfd_dev *dev;
	int idr_handle;
	long err;
	uint64_t offset;

	if (args->size == 0)
		return -EINVAL;

	dev = kfd_device_by_id(args->gpu_id);
	if (dev == NULL)
		return -EINVAL;

	down_write(&p->lock);
	pdd = kfd_bind_process_to_device(dev, p);
	up_write(&p->lock);
	if (IS_ERR(pdd))
		return PTR_ERR(pdd);

	if (args->flags & KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL) {
		if (args->size != kfd_doorbell_process_slice(dev))
			return -EINVAL;
		offset = kfd_get_process_doorbells(dev, p);
	} else
		offset = args->mmap_offset;

	err = dev->kfd2kgd->alloc_memory_of_gpu(
		dev->kgd, args->va_addr, args->size,
		pdd->vm, (struct kgd_mem **) &mem, &offset,
		NULL, args->flags);

	if (err != 0)
		return err;

	down_write(&p->lock);
	idr_handle = kfd_process_device_create_obj_handle(pdd, mem,
			args->va_addr, args->size, NULL);
	up_write(&p->lock);
	if (idr_handle < 0) {
		dev->kfd2kgd->free_memory_of_gpu(dev->kgd,
						 (struct kgd_mem *) mem,
						 pdd->vm);
		return -EFAULT;
	}

	args->handle = MAKE_HANDLE(args->gpu_id, idr_handle);
	if ((args->flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) != 0 &&
			!kfd_is_large_bar(dev)) {
		args->mmap_offset = 0;
	} else {
		args->mmap_offset = KFD_MMAP_TYPE_MAP_BO;
		args->mmap_offset |= KFD_MMAP_GPU_ID(args->gpu_id);
		args->mmap_offset <<= PAGE_SHIFT;
		args->mmap_offset |= offset;
	}

	return 0;
}

static int kfd_ioctl_free_memory_of_gpu(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_free_memory_of_gpu_args *args = data;
	struct kfd_process_device *pdd;
	struct kfd_bo *buf_obj;
	struct kfd_dev *dev;
	int ret;

	dev = kfd_device_by_id(GET_GPU_ID(args->handle));
	if (dev == NULL)
		return -EINVAL;

	down_write(&p->lock);

	pdd = kfd_get_process_device_data(dev, p);
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		ret = -EINVAL;
		goto err_unlock;
	}

	buf_obj = kfd_process_device_find_bo(pdd,
					GET_IDR_HANDLE(args->handle));
	if (buf_obj == NULL) {
		ret = -EINVAL;
		goto err_unlock;
	}
	run_rdma_free_callback(buf_obj);

	up_write(&p->lock);

	ret = dev->kfd2kgd->free_memory_of_gpu(dev->kgd, buf_obj->mem,
					       pdd->vm);

	/* If freeing the buffer failed, leave the handle in place for
	 * clean-up during process tear-down.
	 */
	if (ret == 0) {
		down_write(&p->lock);
		kfd_process_device_remove_obj_handle(
			pdd, GET_IDR_HANDLE(args->handle));
		up_write(&p->lock);
	}

	return ret;

err_unlock:
	up_write(&p->lock);
	return ret;
}

int kfd_map_memory_to_gpu(void *mem, struct kfd_process_device *pdd)
{
	int err;
	struct kfd_dev *dev = pdd->dev;

	err = dev->kfd2kgd->map_memory_to_gpu(
		dev->kgd, (struct kgd_mem *) mem, pdd->vm);

	/* Theoretically we don't need this flush. However, as there are
	 * some bugs in our PTE handling for mapping and unmapping, we
	 * need this flush to pass all the tests.
	 */
	kfd_flush_tlb(dev, pdd->process->pasid);

	return err;
}

static int kfd_ioctl_map_memory_to_gpu(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_map_memory_to_gpu_args *args = data;
	struct kfd_process_device *pdd, *peer_pdd;
	void *mem;
	struct kfd_dev *dev, *peer;
	long err = 0;
	int i, num_dev;
	uint32_t *devices_arr = NULL;

	dev = kfd_device_by_id(GET_GPU_ID(args->handle));
	if (dev == NULL)
		return -EINVAL;

	if (args->device_ids_array_size > 0 &&
			(args->device_ids_array_size < sizeof(uint32_t))) {
		pr_err("amdkfd: err node IDs array size %u\n",
				args->device_ids_array_size);
		return -EFAULT;
	}

	if (args->device_ids_array_size > 0) {
		devices_arr = kmalloc(args->device_ids_array_size, GFP_KERNEL);
		if (!devices_arr)
			return -ENOMEM;

		err = copy_from_user(devices_arr,
				(void __user *)args->device_ids_array_ptr,
				args->device_ids_array_size);
		if (err != 0) {
			err = -EFAULT;
			goto copy_from_user_failed;
		}
	}

	down_write(&p->lock);

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto bind_process_to_device_failed;
	}

	mem = kfd_process_device_translate_handle(pdd,
						GET_IDR_HANDLE(args->handle));
	up_write(&p->lock);

	if (mem == NULL) {
		err = PTR_ERR(mem);
		goto get_mem_obj_from_handle_failed;
	}

	if (args->device_ids_array_size > 0) {
		num_dev = args->device_ids_array_size / sizeof(uint32_t);
		for (i = 0 ; i < num_dev; i++) {
			peer = kfd_device_by_id(devices_arr[i]);
			if (!peer) {
				pr_err("amdkfd: didn't found kfd-dev for 0x%x\n",
						devices_arr[i]);
				err = -EFAULT;
				goto get_mem_obj_from_handle_failed;
			}
			down_write(&p->lock);
			peer_pdd = kfd_bind_process_to_device(peer, p);
			up_write(&p->lock);
			if (!peer_pdd) {
				err = -EFAULT;
				goto get_mem_obj_from_handle_failed;
			}
			err = kfd_map_memory_to_gpu(mem, peer_pdd);
			if (err != 0)
				pr_err("amdkfd: failed to map\n");
		}
	} else {
		err = kfd_map_memory_to_gpu(mem, pdd);
		if (err != 0)
			pr_err("amdkfd: failed to map\n");
	}

	if (args->device_ids_array_size > 0 && devices_arr)
		kfree(devices_arr);

	return err;

bind_process_to_device_failed:
	up_write(&p->lock);
get_mem_obj_from_handle_failed:
copy_from_user_failed:
	kfree(devices_arr);
	return err;
}

int kfd_unmap_memory_from_gpu(void *mem, struct kfd_process_device *pdd)
{
	int err;
	struct kfd_dev *dev = pdd->dev;

	err = dev->kfd2kgd->unmap_memory_to_gpu(
		dev->kgd, (struct kgd_mem *) mem, pdd->vm);

	if (err != 0)
		return err;

	kfd_flush_tlb(dev, pdd->process->pasid);

	return 0;
}

static int kfd_ioctl_unmap_memory_from_gpu(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_unmap_memory_from_gpu_args *args = data;
	struct kfd_process_device *pdd, *peer_pdd;
	void *mem;
	struct kfd_dev *dev, *peer;
	long err = 0;
	uint32_t *devices_arr = NULL, num_dev, i;

	dev = kfd_device_by_id(GET_GPU_ID(args->handle));
	if (dev == NULL)
		return -EINVAL;

	if (args->device_ids_array_size > 0 &&
			(args->device_ids_array_size < sizeof(uint32_t))) {
		pr_err("amdkfd: err node IDs array size %u\n",
				args->device_ids_array_size);
		return -EFAULT;
	}

	if (args->device_ids_array_size > 0) {
		devices_arr = kmalloc(args->device_ids_array_size, GFP_KERNEL);
		if (!devices_arr)
			return -ENOMEM;

		err = copy_from_user(devices_arr,
				(void __user *)args->device_ids_array_ptr,
				args->device_ids_array_size);
		if (err != 0) {
			err = -EFAULT;
			goto copy_from_user_failed;
		}
	}

	down_write(&p->lock);

	pdd = kfd_get_process_device_data(dev, p);
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		err = PTR_ERR(pdd);
		goto bind_process_to_device_failed;
	}

	mem = kfd_process_device_translate_handle(pdd,
						GET_IDR_HANDLE(args->handle));
	up_write(&p->lock);

	if (mem == NULL) {
		err = PTR_ERR(mem);
		goto get_mem_obj_from_handle_failed;
	}

	if (args->device_ids_array_size > 0) {
		num_dev = args->device_ids_array_size / sizeof(uint32_t);
		for (i = 0 ; i < num_dev; i++) {
			peer = kfd_device_by_id(devices_arr[i]);
			if (!peer) {
				err = -EFAULT;
				goto get_mem_obj_from_handle_failed;
			}
			down_write(&p->lock);
			peer_pdd = kfd_get_process_device_data(peer, p);
			up_write(&p->lock);
			if (!peer_pdd) {
				err = -EFAULT;
				goto get_mem_obj_from_handle_failed;
			}
			kfd_unmap_memory_from_gpu(mem, peer_pdd);
		}
		kfree(devices_arr);
	} else
		kfd_unmap_memory_from_gpu(mem, pdd);

	return 0;

bind_process_to_device_failed:
	up_write(&p->lock);
get_mem_obj_from_handle_failed:
copy_from_user_failed:
	kfree(devices_arr);
	return err;
}

static int kfd_ioctl_open_graphic_handle(struct file *filep,
					struct kfd_process *p,
					void *data)
{
	struct kfd_ioctl_open_graphic_handle_args *args = data;
	struct kfd_dev *dev;
	struct kfd_process_device *pdd;
	void *mem;
	int idr_handle;
	long err;

	dev = kfd_device_by_id(args->gpu_id);
	if (dev == NULL)
		return -EINVAL;

	if (dev->device_info->asic_family != CHIP_KAVERI) {
		pr_debug("kfd_ioctl_open_graphic_handle only supported on KV\n");
		return -EINVAL;
	}

	down_write(&p->lock);
	pdd = kfd_bind_process_to_device(dev, p);
	up_write(&p->lock);
	if (IS_ERR(pdd))
		return PTR_ERR(pdd);

	err = dev->kfd2kgd->open_graphic_handle(dev->kgd,
			args->va_addr,
			(struct kgd_vm *) pdd->vm,
			args->graphic_device_fd,
			args->graphic_handle,
			(struct kgd_mem **) &mem);

	if (err != 0)
		return err;

	down_write(&p->lock);
	/*TODO: When open_graphic_handle is implemented, we need to create
	 * the corresponding interval tree. We need to know the size of
	 * the buffer through open_graphic_handle(). We use 1 for now.
	 */
	idr_handle = kfd_process_device_create_obj_handle(pdd, mem,
			args->va_addr, 1, NULL);
	up_write(&p->lock);
	if (idr_handle < 0) {
		/* FIXME: destroy_process_gpumem doesn't seem to be
		 * implemented anywhere
		 */
		dev->kfd2kgd->destroy_process_gpumem(dev->kgd, mem);
		return -EFAULT;
	}

	args->handle = MAKE_HANDLE(args->gpu_id, idr_handle);

	return 0;
}

static int kfd_ioctl_set_process_dgpu_aperture(struct file *filep,
		struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_process_dgpu_aperture_args *args = data;
	struct kfd_dev *dev;
	struct kfd_process_device *pdd;
	long err;

	dev = kfd_device_by_id(args->gpu_id);
	if (dev == NULL)
		return -EINVAL;

	down_write(&p->lock);

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto exit;
	}

	err = kfd_set_process_dgpu_aperture(pdd, args->dgpu_base,
			args->dgpu_limit);

exit:
	up_write(&p->lock);
	return err;
}

static int kfd_ioctl_get_dmabuf_info(struct file *filep,
		struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_dmabuf_info_args *args = data;
	struct kfd_dev *dev = NULL;
	struct kgd_dev *dma_buf_kgd;
	void *metadata_buffer = NULL;
	uint32_t flags;
	unsigned int i;
	int r;

	/* Find a KFD GPU device that supports the get_dmabuf_info query */
	for (i = 0; kfd_topology_enum_kfd_devices(i, &dev) == 0; i++)
		if (dev && dev->kfd2kgd->get_dmabuf_info)
			break;
	if (!dev)
		return -EINVAL;

	if (args->metadata_ptr) {
		metadata_buffer = kzalloc(args->metadata_size, GFP_KERNEL);
		if (!metadata_buffer)
			return -ENOMEM;
	}

	/* Get dmabuf info from KGD */
	r = dev->kfd2kgd->get_dmabuf_info(dev->kgd, args->dmabuf_fd,
					  &dma_buf_kgd, &args->size,
					  metadata_buffer, args->metadata_size,
					  &args->metadata_size, &flags);
	if (r)
		goto exit;

	/* Reverse-lookup gpu_id from kgd pointer */
	dev = kfd_device_by_kgd(dma_buf_kgd);
	if (!dev) {
		r = -EINVAL;
		goto exit;
	}
	args->gpu_id = kfd_get_gpu_id(dev);
	args->flags = flags;

	/* Copy metadata buffer to user mode */
	if (metadata_buffer) {
		r = copy_to_user((void __user *)args->metadata_ptr,
				 metadata_buffer, args->metadata_size);
		if (r != 0)
			r = -EFAULT;
	}

exit:
	kfree(metadata_buffer);

	return r;
}

static int kfd_ioctl_import_dmabuf(struct file *filep,
				   struct kfd_process *p, void *data)
{
	struct kfd_ioctl_import_dmabuf_args *args = data;
	struct kfd_dev *dev;
	int r;

	dev = kfd_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	r = kfd_ipc_import_dmabuf(dev, p, args->gpu_id, args->dmabuf_fd,
				  args->va_addr, &args->handle, NULL);
	if (r)
		dev_err(kfd_device, "Failed to import dmabuf\n");

	return r;
}

static int kfd_ioctl_ipc_export_handle(struct file *filep,
				       struct kfd_process *p,
				       void *data)
{
	struct kfd_ioctl_ipc_export_handle_args *args = data;
	struct kfd_dev *dev;
	int r;

	dev = kfd_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	r = kfd_ipc_export_as_handle(dev, p, args->handle, args->share_handle);
	if (r)
		dev_err(kfd_device, "Failed to export IPC handle\n");

	return r;
}

static int kfd_ioctl_ipc_import_handle(struct file *filep,
				       struct kfd_process *p,
				       void *data)
{
	struct kfd_ioctl_ipc_import_handle_args *args = data;
	struct kfd_dev *dev = NULL;
	int r;

	dev = kfd_device_by_id(args->gpu_id);
	if (!dev)
		return -EINVAL;

	r = kfd_ipc_import_handle(dev, p, args->gpu_id, args->share_handle,
				  args->va_addr, &args->handle,
				  &args->mmap_offset);
	if (r)
		dev_err(kfd_device, "Failed to import IPC handle\n");

	return r;
}

static int kfd_ioctl_get_tile_config(struct file *filep,
		struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_tile_config_args *args = data;
	struct kfd_dev *dev;
	struct tile_config config;
	int err = 0;

	dev = kfd_device_by_id(args->gpu_id);

	dev->kfd2kgd->get_tile_config(dev->kgd, &config);

	args->gb_addr_config = config.gb_addr_config;
	args->num_banks = config.num_banks;
	args->num_ranks = config.num_ranks;

	if (args->num_tile_configs > config.num_tile_configs)
		args->num_tile_configs = config.num_tile_configs;
	err = copy_to_user((void __user *)args->tile_config_ptr,
			config.tile_config_ptr,
			args->num_tile_configs * sizeof(uint32_t));
	if (err) {
		args->num_tile_configs = 0;
		return -EFAULT;
	}

	if (args->num_macro_tile_configs > config.num_macro_tile_configs)
		args->num_macro_tile_configs =
				config.num_macro_tile_configs;
	err = copy_to_user((void __user *)args->macro_tile_config_ptr,
			config.macro_tile_config_ptr,
			args->num_macro_tile_configs * sizeof(uint32_t));
	if (err) {
		args->num_macro_tile_configs = 0;
		return -EFAULT;
	}

	return 0;
}

static int kfd_ioctl_cross_memory_copy(struct file *filep,
				       struct kfd_process *local_p, void *data)
{
	struct kfd_ioctl_cross_memory_copy_args *args = data;
	struct kfd_memory_range *src_array, *dst_array;
	struct kfd_bo *src_bo, *dst_bo;
	struct kfd_process *remote_p, *src_p, *dst_p;
	struct task_struct *remote_task;
	struct mm_struct *remote_mm;
	struct pid *remote_pid;
	struct dma_fence *fence = NULL, *lfence = NULL;
	uint64_t dst_va_addr;
	uint64_t copied, total_copied = 0;
	uint64_t src_offset, dst_offset;
	int i, j = 0, err = 0;

	/* Check parameters */
	if (args->src_mem_range_array == 0 || args->dst_mem_range_array == 0 ||
		args->src_mem_array_size == 0 || args->dst_mem_array_size == 0)
		return -EINVAL;
	args->bytes_copied = 0;

	/* Allocate space for source and destination arrays */
	src_array = kmalloc_array((args->src_mem_array_size +
				  args->dst_mem_array_size),
				  sizeof(struct kfd_memory_range),
				  GFP_KERNEL);
	if (src_array == NULL)
		return -ENOMEM;
	dst_array = &src_array[args->src_mem_array_size];

	if (copy_from_user(src_array, (void __user *)args->src_mem_range_array,
			   args->src_mem_array_size *
			   sizeof(struct kfd_memory_range))) {
		err = -EFAULT;
		goto copy_from_user_fail;
	}
	if (copy_from_user(dst_array, (void __user *)args->dst_mem_range_array,
			   args->dst_mem_array_size *
			   sizeof(struct kfd_memory_range))) {
		err = -EFAULT;
		goto copy_from_user_fail;
	}

	/* Get remote process */
	remote_pid = find_get_pid(args->pid);
	if (remote_pid == NULL) {
		pr_err("Cross mem copy failed. Invalid PID %d\n", args->pid);
		err = -ESRCH;
		goto copy_from_user_fail;
	}

	remote_task = get_pid_task(remote_pid, PIDTYPE_PID);
	if (remote_pid == NULL) {
		pr_err("Cross mem copy failed. Invalid PID or task died %d\n",
			args->pid);
		err = -ESRCH;
		goto get_pid_task_fail;
	}

	/* Check access permission */
	remote_mm = mm_access(remote_task, PTRACE_MODE_ATTACH_REALCREDS);
	if (!remote_mm || IS_ERR(remote_mm)) {
		err = IS_ERR(remote_mm) ? PTR_ERR(remote_mm) : -ESRCH;
		if (err == -EACCES) {
			pr_err("Cross mem copy failed. Permission error\n");
			err = -EPERM;
		} else
			pr_err("Cross mem copy failed. Invalid task (%d)\n",
			       err);
		goto mm_access_fail;
	}

	remote_p = kfd_get_process(remote_task);
	if (remote_p == NULL) {
		pr_err("Cross mem copy failed. Invalid kfd process %d\n",
		       args->pid);
		err = -EINVAL;
		goto kfd_process_fail;
	}

	if (KFD_IS_CROSS_MEMORY_WRITE(args->flags)) {
		src_p = local_p;
		dst_p = remote_p;
		pr_debug("CMA WRITE: local -> remote\n");
	} else {
		src_p = remote_p;
		dst_p = local_p;
		pr_debug("CMA READ: remote -> local\n");
	}


	/* For each source kfd_range:
	 * - Find the BO. Each range has to be within the same BO.
	 * - Copy this range to single or multiple destination BOs.
	 * - dst_va_addr - will point to next va address into which data will
	 *                 be copied.
	 * - dst_bo & src_bo - the current destination and source BOs
	 * - src_offset & dst_offset - offset into the respective BOs from
	 *                             data will be sourced or copied
	 */
	dst_va_addr = dst_array[0].va_addr;
	down_read(&dst_p->lock);
	dst_bo = kfd_process_find_bo_from_interval(dst_p,
			dst_va_addr,
			dst_va_addr + dst_array[0].size - 1);
	up_read(&dst_p->lock);
	if (dst_bo == NULL) {
		err = -EFAULT;
		goto kfd_process_fail;
	}
	dst_offset = dst_va_addr - dst_bo->it.start;

	for (i = 0; i < args->src_mem_array_size; i++) {
		uint64_t src_va_addr_end = src_array[i].va_addr +
					   src_array[i].size - 1;
		uint64_t src_size_to_copy = src_array[i].size;

		down_read(&src_p->lock);
		src_bo = kfd_process_find_bo_from_interval(src_p,
				src_array[i].va_addr,
				src_va_addr_end);
		up_read(&src_p->lock);
		if (src_bo == NULL || src_va_addr_end > src_bo->it.last) {
			pr_err("Cross mem copy failed. Invalid range\n");
			err = -EFAULT;
			break;
		}

		src_offset = src_array[i].va_addr - src_bo->it.start;

		/* Copy src_bo to one or multiple dst_bo(s) based on size and
		 * and current copy location.
		 */
		while (j < args->dst_mem_array_size) {
			uint64_t copy_size;
			int64_t space_left;

			/* Find the current copy_size. This will be smaller of
			 * the following
			 * - space left in the current dest memory range
			 * - data left to copy from source range
			 */
			space_left = (dst_array[j].va_addr + dst_array[j].size)
					- dst_va_addr;
			copy_size = (src_size_to_copy < space_left) ?
					src_size_to_copy : space_left;

			/* Check both BOs belong to same device */
			if (src_bo->dev->kgd != dst_bo->dev->kgd) {
				pr_err("Cross Memory failed. Not same device\n");
				err = -EINVAL;
				break;
			}

			/* Store prev fence. Release it when a later fence is
			 * created
			 */
			lfence = fence;
			fence = NULL;

			err = dst_bo->dev->kfd2kgd->copy_mem_to_mem(
				src_bo->dev->kgd,
				src_bo->mem, src_offset,
				dst_bo->mem, dst_offset,
				copy_size,
				&fence, &copied);

			if (err) {
				pr_err("GPU Cross mem copy failed\n");
				err = -EFAULT;
				break;
			}

			/* Later fence available. Release old fence */
			if (fence && lfence) {
				dma_fence_put(lfence);
				lfence = NULL;
			}

			total_copied += copied;
			src_size_to_copy -= copied;
			space_left -= copied;
			dst_va_addr += copied;
			dst_offset += copied;
			src_offset += copied;
			if (dst_va_addr > dst_bo->it.last + 1) {
				pr_err("Cross mem copy failed. Memory overflow\n");
				err = -EFAULT;
				break;
			}

			/* If the cur dest range is full move to next one */
			if (space_left <= 0) {
				if (++j >= args->dst_mem_array_size)
					break;

				dst_va_addr = dst_array[j].va_addr;
				dst_bo = kfd_process_find_bo_from_interval(
						dst_p,
						dst_va_addr,
						dst_va_addr +
						dst_array[j].size - 1);
				dst_offset = dst_va_addr - dst_bo->it.start;
			}

			/* If the cur src range is done, move to next one */
			if (src_size_to_copy <= 0)
				break;
		}
		if (err)
			break;
	}

	/* Wait for the last fence irrespective of error condition */
	if (fence) {
		if (dma_fence_wait_timeout(fence, false, msecs_to_jiffies(1000))
			< 0)
			pr_err("Cross mem copy failed. BO timed out\n");
		dma_fence_put(fence);
	} else if (lfence) {
		pr_debug("GPU copy fail. But wait for prev DMA to finish\n");
		dma_fence_wait_timeout(lfence, true, msecs_to_jiffies(1000));
		dma_fence_put(lfence);
	}

kfd_process_fail:
	mmput(remote_mm);
mm_access_fail:
	put_task_struct(remote_task);
get_pid_task_fail:
	put_pid(remote_pid);
copy_from_user_fail:
	kfree(src_array);

	/* An error could happen after partial copy. In that case this will
	 * reflect partial amount of bytes copied
	 */
	args->bytes_copied = total_copied;
	return err;
}

#define AMDKFD_IOCTL_DEF(ioctl, _func, _flags) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func, .flags = _flags, \
			    .cmd_drv = 0, .name = #ioctl}

/** Ioctl table */
static const struct amdkfd_ioctl_desc amdkfd_ioctls[] = {
	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_VERSION,
			kfd_ioctl_get_version, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_CREATE_QUEUE,
			kfd_ioctl_create_queue, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DESTROY_QUEUE,
			kfd_ioctl_destroy_queue, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_MEMORY_POLICY,
			kfd_ioctl_set_memory_policy, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_CLOCK_COUNTERS,
			kfd_ioctl_get_clock_counters, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_PROCESS_APERTURES,
			kfd_ioctl_get_process_apertures, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_UPDATE_QUEUE,
			kfd_ioctl_update_queue, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_CREATE_EVENT,
			kfd_ioctl_create_event, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DESTROY_EVENT,
			kfd_ioctl_destroy_event, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_EVENT,
			kfd_ioctl_set_event, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_RESET_EVENT,
			kfd_ioctl_reset_event, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_WAIT_EVENTS,
			kfd_ioctl_wait_events, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DBG_REGISTER,
			kfd_ioctl_dbg_register, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DBG_UNREGISTER,
			kfd_ioctl_dbg_unregister, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DBG_ADDRESS_WATCH,
			kfd_ioctl_dbg_address_watch, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_DBG_WAVE_CONTROL,
			kfd_ioctl_dbg_wave_control, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_ALLOC_MEMORY_OF_GPU,
			kfd_ioctl_alloc_memory_of_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_FREE_MEMORY_OF_GPU,
			kfd_ioctl_free_memory_of_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_MAP_MEMORY_TO_GPU,
			kfd_ioctl_map_memory_to_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_UNMAP_MEMORY_FROM_GPU,
			kfd_ioctl_unmap_memory_from_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_OPEN_GRAPHIC_HANDLE,
			kfd_ioctl_open_graphic_handle, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_ALLOC_MEMORY_OF_SCRATCH,
			kfd_ioctl_alloc_scratch_memory, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_CU_MASK,
			kfd_ioctl_set_cu_mask, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_PROCESS_DGPU_APERTURE,
			kfd_ioctl_set_process_dgpu_aperture, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_TRAP_HANDLER,
			kfd_ioctl_set_trap_handler, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_PROCESS_APERTURES_NEW,
				kfd_ioctl_get_process_apertures_new, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_DMABUF_INFO,
				kfd_ioctl_get_dmabuf_info, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_IMPORT_DMABUF,
				kfd_ioctl_import_dmabuf, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_TILE_CONFIG,
				kfd_ioctl_get_tile_config, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_IPC_IMPORT_HANDLE,
				kfd_ioctl_ipc_import_handle, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_IPC_EXPORT_HANDLE,
				kfd_ioctl_ipc_export_handle, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_CROSS_MEMORY_COPY,
				kfd_ioctl_cross_memory_copy, 0)

};

#define AMDKFD_CORE_IOCTL_COUNT	ARRAY_SIZE(amdkfd_ioctls)

static long kfd_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct kfd_process *process;
	amdkfd_ioctl_t *func;
	const struct amdkfd_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128];
	char *kdata = NULL;
	unsigned int usize, asize;
	int retcode = -EINVAL;

	if (nr >= AMDKFD_CORE_IOCTL_COUNT)
		goto err_i1;

	if ((nr >= AMDKFD_COMMAND_START) && (nr < AMDKFD_COMMAND_END)) {
		u32 amdkfd_size;

		ioctl = &amdkfd_ioctls[nr];

		amdkfd_size = _IOC_SIZE(ioctl->cmd);
		usize = asize = _IOC_SIZE(cmd);
		if (amdkfd_size > asize)
			asize = amdkfd_size;

		cmd = ioctl->cmd;
	} else
		goto err_i1;

	dev_dbg(kfd_device, "ioctl cmd (#0x%x), arg 0x%lx\n",
				nr, arg);

	process = kfd_get_process(current);
	if (IS_ERR(process)) {
		dev_dbg(kfd_device, "no process\n");
		goto err_i1;
	}

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		dev_dbg(kfd_device, "no function\n");
		retcode = -EINVAL;
		goto err_i1;
	}

	if (cmd & (IOC_IN | IOC_OUT)) {
		if (asize <= sizeof(stack_kdata)) {
			kdata = stack_kdata;
		} else {
			kdata = kmalloc(asize, GFP_KERNEL);
			if (!kdata) {
				retcode = -ENOMEM;
				goto err_i1;
			}
		}
		if (asize > usize)
			memset(kdata + usize, 0, asize - usize);
	}

	if (cmd & IOC_IN) {
		if (copy_from_user(kdata, (void __user *)arg, usize) != 0) {
			retcode = -EFAULT;
			goto err_i1;
		}
	} else if (cmd & IOC_OUT) {
		memset(kdata, 0, usize);
	}

	retcode = func(filep, process, kdata);

	if (cmd & IOC_OUT)
		if (copy_to_user((void __user *)arg, kdata, usize) != 0)
			retcode = -EFAULT;

err_i1:
	if (!ioctl)
		dev_dbg(kfd_device, "invalid ioctl: pid=%d, cmd=0x%02x, nr=0x%02x\n",
			  task_pid_nr(current), cmd, nr);

	if (kdata != stack_kdata)
		kfree(kdata);

	if (retcode)
		dev_dbg(kfd_device, "ioctl cmd (#0x%x), arg 0x%lx, failed %d\n",
				nr, arg, retcode);

	return retcode;
}

static int kfd_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct kfd_process *process;
	struct kfd_dev *kfd;
	unsigned long vm_pgoff;
	int retval;

	process = kfd_get_process(current);
	if (IS_ERR(process))
		return PTR_ERR(process);

	vm_pgoff = vma->vm_pgoff;
	vma->vm_pgoff = KFD_MMAP_OFFSET_VALUE_GET(vma->vm_pgoff);

	switch (vm_pgoff & KFD_MMAP_TYPE_MASK) {
	case KFD_MMAP_TYPE_DOORBELL:
		kfd = kfd_device_by_id(KFD_MMAP_GPU_ID_GET(vm_pgoff));
		if (!kfd)
			return -EFAULT;
		return kfd_doorbell_mmap(kfd, process, vma);

	case KFD_MMAP_TYPE_EVENTS:
		return kfd_event_mmap(process, vma);

	case KFD_MMAP_TYPE_MAP_BO:
		kfd = kfd_device_by_id(KFD_MMAP_GPU_ID_GET(vm_pgoff));
		if (!kfd)
			return -EFAULT;
		retval = kfd->kfd2kgd->mmap_bo(kfd->kgd, vma);
		return retval;

	case KFD_MMAP_TYPE_RESERVED_MEM:
		return kfd_reserved_mem_mmap(process, vma);

	}

	return -EFAULT;
}


