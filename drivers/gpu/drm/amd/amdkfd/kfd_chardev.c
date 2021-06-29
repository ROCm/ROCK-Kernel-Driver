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
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <uapi/linux/kfd_ioctl.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ptrace.h>
#include <linux/dma-buf.h>
#include <linux/fdtable.h>
#include <asm/processor.h>
#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_dbgmgr.h"
#include "kfd_svm.h"
#include "amdgpu_amdkfd.h"
#include "kfd_smi_events.h"
#include "amdgpu_object.h"
#include "amdgpu_dma_buf.h"

static long kfd_ioctl(struct file *, unsigned int, unsigned long);
static int kfd_open(struct inode *, struct file *);
static int kfd_release(struct inode *, struct file *);
static int kfd_mmap(struct file *, struct vm_area_struct *);

static const char kfd_dev_name[] = "kfd";

static const struct file_operations kfd_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = kfd_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.open = kfd_open,
	.release = kfd_release,
	.mmap = kfd_mmap,
};

static int kfd_char_dev_major = -1;
static struct class *kfd_class;
struct device *kfd_device;

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
	kfd_device = NULL;
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

	if (kfd_is_locked()) {
		dev_dbg(kfd_device, "kfd is locked!\n"
				"process %d unreferenced", process->pasid);
		kfd_unref_process(process);
		return -EAGAIN;
	}

	/* filep now owns the reference returned by kfd_create_process */
	filep->private_data = process;

	dev_dbg(kfd_device, "process %d opened, compat mode (32 bit) - %d\n",
		process->pasid, process->is_32bit_user_mode);

	return 0;
}

static int kfd_release(struct inode *inode, struct file *filep)
{
	struct kfd_process *process = filep->private_data;

	if (process)
		kfd_unref_process(process);

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
		pr_err("Queue percentage must be between 0 to KFD_MAX_QUEUE_PERCENTAGE\n");
		return -EINVAL;
	}

	if (args->queue_priority > KFD_MAX_QUEUE_PRIORITY) {
		pr_err("Queue priority must be between 0 to KFD_MAX_QUEUE_PRIORITY\n");
		return -EINVAL;
	}

	if ((args->ring_base_address) &&
		(!access_ok((const void __user *) args->ring_base_address,
			sizeof(uint64_t)))) {
		pr_err("Can't access ring base address\n");
		return -EFAULT;
	}

	if (!is_power_of_2(args->ring_size) && (args->ring_size != 0)) {
		pr_err("Ring size must be a power of 2 or 0\n");
		return -EINVAL;
	}

	if (!access_ok((const void __user *) args->read_pointer_address,
			sizeof(uint32_t))) {
		pr_err("Can't access read pointer\n");
		return -EFAULT;
	}

	if (!access_ok((const void __user *) args->write_pointer_address,
			sizeof(uint32_t))) {
		pr_err("Can't access write pointer\n");
		return -EFAULT;
	}

	if (args->eop_buffer_address &&
		!access_ok((const void __user *) args->eop_buffer_address,
			sizeof(uint32_t))) {
		pr_debug("Can't access eop buffer");
		return -EFAULT;
	}

	if (args->ctx_save_restore_address &&
		!access_ok((const void __user *) args->ctx_save_restore_address,
			sizeof(uint32_t))) {
		pr_debug("Can't access ctx save restore buffer");
		return -EFAULT;
	}

	q_properties->is_interop = false;
	q_properties->is_gws = false;
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
	else if (args->queue_type == KFD_IOC_QUEUE_TYPE_SDMA_XGMI)
		q_properties->type = KFD_QUEUE_TYPE_SDMA_XGMI;
	else
		return -ENOTSUPP;

	if (args->queue_type == KFD_IOC_QUEUE_TYPE_COMPUTE_AQL)
		q_properties->format = KFD_QUEUE_FORMAT_AQL;
	else
		q_properties->format = KFD_QUEUE_FORMAT_PM4;

	pr_debug("Queue Percentage: %d, %d\n",
			q_properties->queue_percent, args->queue_percentage);

	pr_debug("Queue Priority: %d, %d\n",
			q_properties->priority, args->queue_priority);

	pr_debug("Queue Address: 0x%llX, 0x%llX\n",
			q_properties->queue_address, args->ring_base_address);

	pr_debug("Queue Size: 0x%llX, %u\n",
			q_properties->queue_size, args->ring_size);

	pr_debug("Queue r/w Pointers: %px, %px\n",
			q_properties->read_ptr,
			q_properties->write_ptr);

	pr_debug("Queue Format: %d\n", q_properties->format);

	pr_debug("Queue EOP: 0x%llX\n", q_properties->eop_ring_buffer_address);

	pr_debug("Queue CTX save area: 0x%llX\n",
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
	uint32_t doorbell_offset_in_process = 0;

	memset(&q_properties, 0, sizeof(struct queue_properties));

	pr_debug("Creating queue ioctl\n");

	err = set_queue_properties_from_user(&q_properties, args);
	if (err)
		return err;

	pr_debug("Looking for gpu id 0x%x\n", args->gpu_id);

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		pr_debug("Could not find gpu id 0x%x\n", args->gpu_id);
		return -EINVAL;
	}
	dev = pdd->dev;

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto err_bind_process;
	}

	pr_debug("Creating queue for PASID 0x%x on gpu 0x%x\n",
			p->pasid,
			dev->id);

	err = pqm_create_queue(&p->pqm, dev, filep, &q_properties, &queue_id,
			NULL, &doorbell_offset_in_process);
	if (err != 0)
		goto err_create_queue;

	args->queue_id = queue_id;


	/* Return gpu_id as doorbell offset for mmap usage */
	args->doorbell_offset = KFD_MMAP_TYPE_DOORBELL;
	args->doorbell_offset |= KFD_MMAP_GPU_ID(args->gpu_id);
	if (KFD_IS_SOC15(dev->device_info->asic_family))
		/* On SOC15 ASICs, include the doorbell offset within the
		 * process doorbell frame, which is 2 pages.
		 */
		args->doorbell_offset |= doorbell_offset_in_process;

	mutex_unlock(&p->mutex);

	pr_debug("Queue id %d was created successfully\n", args->queue_id);

	pr_debug("Ring buffer address == 0x%016llX\n",
			args->ring_base_address);

	pr_debug("Read ptr address    == 0x%016llX\n",
			args->read_pointer_address);

	pr_debug("Write ptr address   == 0x%016llX\n",
			args->write_pointer_address);

	return 0;

err_create_queue:
err_bind_process:
	mutex_unlock(&p->mutex);
	return err;
}

static int kfd_ioctl_destroy_queue(struct file *filp, struct kfd_process *p,
					void *data)
{
	int retval;
	struct kfd_ioctl_destroy_queue_args *args = data;

	pr_debug("Destroying queue id %d for pasid 0x%x\n",
				args->queue_id,
				p->pasid);

	mutex_lock(&p->mutex);

	retval = pqm_destroy_queue(&p->pqm, args->queue_id);

	mutex_unlock(&p->mutex);
	return retval;
}

static int kfd_ioctl_update_queue(struct file *filp, struct kfd_process *p,
					void *data)
{
	int retval;
	struct kfd_ioctl_update_queue_args *args = data;
	struct queue_properties properties;

	if (args->queue_percentage > KFD_MAX_QUEUE_PERCENTAGE) {
		pr_err("Queue percentage must be between 0 to KFD_MAX_QUEUE_PERCENTAGE\n");
		return -EINVAL;
	}

	if (args->queue_priority > KFD_MAX_QUEUE_PRIORITY) {
		pr_err("Queue priority must be between 0 to KFD_MAX_QUEUE_PRIORITY\n");
		return -EINVAL;
	}

	if ((args->ring_base_address) &&
		(!access_ok((const void __user *) args->ring_base_address,
			sizeof(uint64_t)))) {
		pr_err("Can't access ring base address\n");
		return -EFAULT;
	}

	if (!is_power_of_2(args->ring_size) && (args->ring_size != 0)) {
		pr_err("Ring size must be a power of 2 or 0\n");
		return -EINVAL;
	}

	properties.queue_address = args->ring_base_address;
	properties.queue_size = args->ring_size;
	properties.queue_percent = args->queue_percentage;
	properties.priority = args->queue_priority;

	pr_debug("Updating queue id %d for pasid 0x%x\n",
			args->queue_id, p->pasid);

	mutex_lock(&p->mutex);

	retval = pqm_update_queue(&p->pqm, args->queue_id, &properties);

	mutex_unlock(&p->mutex);

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
		pr_debug("num_cu_mask 0x%x must be a multiple of 32",
				args->num_cu_mask);
		return -EINVAL;
	}

	properties.cu_mask_count = args->num_cu_mask;
	if (properties.cu_mask_count == 0) {
		pr_debug("CU mask cannot be 0");
		return -EINVAL;
	}

	/* To prevent an unreasonably large CU mask size, set an arbitrary
	 * limit of max_num_cus bits.  We can then just drop any CU mask bits
	 * past max_num_cus bits and just use the first max_num_cus bits.
	 */
	if (properties.cu_mask_count > max_num_cus) {
		pr_debug("CU mask cannot be greater than 1024 bits");
		properties.cu_mask_count = max_num_cus;
		cu_mask_size = sizeof(uint32_t) * (max_num_cus/32);
	}

	properties.cu_mask = kzalloc(cu_mask_size, GFP_KERNEL);
	if (!properties.cu_mask)
		return -ENOMEM;

	retval = copy_from_user(properties.cu_mask, cu_mask_ptr, cu_mask_size);
	if (retval) {
		pr_debug("Could not copy CU mask from userspace");
		kfree(properties.cu_mask);
		return -EFAULT;
	}

	mutex_lock(&p->mutex);

	retval = pqm_set_cu_mask(&p->pqm, args->queue_id, &properties);

	mutex_unlock(&p->mutex);

	if (retval)
		kfree(properties.cu_mask);

	return retval;
}

static int kfd_ioctl_get_queue_wave_state(struct file *filep,
					  struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_queue_wave_state_args *args = data;
	int r;

	mutex_lock(&p->mutex);

	r = pqm_get_wave_state(&p->pqm, args->queue_id,
			       (void __user *)args->ctl_stack_address,
			       &args->ctl_stack_used_size,
			       &args->save_area_used_size);

	mutex_unlock(&p->mutex);

	return r;
}

static int kfd_ioctl_set_memory_policy(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_memory_policy_args *args = data;
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

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		pr_debug("Could not find gpu id 0x%x\n", args->gpu_id);
		err = -EINVAL;
		goto out;
	}

	pdd = kfd_bind_process_to_device(pdd->dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto out;
	}

	default_policy = (args->default_policy == KFD_IOC_CACHE_POLICY_COHERENT)
			 ? cache_policy_coherent : cache_policy_noncoherent;

	alternate_policy =
		(args->alternate_policy == KFD_IOC_CACHE_POLICY_COHERENT)
		   ? cache_policy_coherent : cache_policy_noncoherent;

	if (!pdd->dev->dqm->ops.set_cache_memory_policy(pdd->dev->dqm,
				&pdd->qpd,
				default_policy,
				alternate_policy,
				(void __user *)args->alternate_aperture_base,
				args->alternate_aperture_size))
		err = -EINVAL;

out:
	mutex_unlock(&p->mutex);

	return err;
}

static int kfd_ioctl_set_trap_handler(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_trap_handler_args *args = data;
	int err = 0;
	struct kfd_process_device *pdd;

	mutex_lock(&p->mutex);

	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		err = -EINVAL;
		goto out;
	}

	pdd = kfd_bind_process_to_device(pdd->dev, p);
	if (IS_ERR(pdd)) {
		err = -ESRCH;
		goto out;
	}

	kfd_process_set_trap_handler(&pdd->qpd, args->tba_addr, args->tma_addr);

out:
	mutex_unlock(&p->mutex);

	return err;
}

static int kfd_ioctl_dbg_register(struct file *filep,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_dbg_register_args *args = data;
	struct kfd_dev *dev;
	struct kfd_dbgmgr *dbgmgr_ptr;
	struct kfd_process_device *pdd;
	bool create_ok;
	long status = 0;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		status = -EINVAL;
		goto out_unlock_p;
	}
	dev = pdd->dev;

	if (dev->device_info->asic_family == CHIP_CARRIZO) {
		pr_debug("kfd_ioctl_dbg_register not supported on CZ\n");
		status = -EINVAL;
		goto out_unlock_p;
	}

	mutex_lock(kfd_get_dbgmgr_mutex());

	/*
	 * make sure that we have pdd, if this the first queue created for
	 * this process
	 */
	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		status = PTR_ERR(pdd);
		goto out_unlock_dbg;
	}

	if (!dev->dbgmgr) {
		/* In case of a legal call, we have no dbgmgr yet */
		create_ok = kfd_dbgmgr_create(&dbgmgr_ptr, dev);
		if (create_ok) {
			status = kfd_dbgmgr_register(dbgmgr_ptr, p);
			if (status != 0)
				kfd_dbgmgr_destroy(dbgmgr_ptr);
			else
				dev->dbgmgr = dbgmgr_ptr;
		}
	} else {
		pr_debug("debugger already registered\n");
		status = -EINVAL;
	}

out_unlock_dbg:
	mutex_unlock(kfd_get_dbgmgr_mutex());
out_unlock_p:
	mutex_unlock(&p->mutex);

	return status;
}

static int kfd_ioctl_dbg_unregister(struct file *filep,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_dbg_unregister_args *args = data;
	struct kfd_process_device *pdd;
	struct kfd_dev *dev;
	long status;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd || !pdd->dev->dbgmgr) {
		mutex_unlock(&p->mutex);
		return -EINVAL;
	}
	dev = pdd->dev;
	mutex_unlock(&p->mutex);

	if (dev->device_info->asic_family == CHIP_CARRIZO) {
		pr_debug("kfd_ioctl_dbg_unregister not supported on CZ\n");
		return -EINVAL;
	}

	mutex_lock(kfd_get_dbgmgr_mutex());

	status = kfd_dbgmgr_unregister(dev->dbgmgr, p);
	if (!status) {
		kfd_dbgmgr_destroy(dev->dbgmgr);
		dev->dbgmgr = NULL;
	}

	mutex_unlock(kfd_get_dbgmgr_mutex());

	return status;
}

/*
 * Parse and generate variable size data structure for address watch.
 * Total size of the buffer and # watch points is limited in order
 * to prevent kernel abuse. (no bearing to the much smaller HW limitation
 * which is enforced by dbgdev module)
 * please also note that the watch address itself are not "copied from user",
 * since it be set into the HW in user mode values.
 *
 */
static int kfd_ioctl_dbg_address_watch(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_dbg_address_watch_args *args = data;
	struct kfd_dev *dev;
	struct kfd_process_device *pdd;
	struct dbg_address_watch_info aw_info;
	unsigned char *args_buff;
	long status;
	void __user *cmd_from_user;
	uint64_t watch_mask_value = 0;
	unsigned int args_idx = 0;

	memset((void *) &aw_info, 0, sizeof(struct dbg_address_watch_info));

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		mutex_unlock(&p->mutex);
		pr_debug("Could not find gpu id 0x%x\n", args->gpu_id);
		return -EINVAL;
	}
	dev = pdd->dev;
	mutex_unlock(&p->mutex);

	if (dev->device_info->asic_family == CHIP_CARRIZO) {
		pr_debug("kfd_ioctl_dbg_wave_control not supported on CZ\n");
		return -EINVAL;
	}

	cmd_from_user = (void __user *) args->content_ptr;

	/* Validate arguments */

	if ((args->buf_size_in_bytes > MAX_ALLOWED_AW_BUFF_SIZE) ||
		(args->buf_size_in_bytes <= sizeof(*args) + sizeof(int) * 2) ||
		(cmd_from_user == NULL))
		return -EINVAL;

	/* this is the actual buffer to work with */
	args_buff = memdup_user(cmd_from_user,
				args->buf_size_in_bytes - sizeof(*args));
	if (IS_ERR(args_buff))
		return PTR_ERR(args_buff);

	aw_info.process = p;

	aw_info.num_watch_points = *((uint32_t *)(&args_buff[args_idx]));
	args_idx += sizeof(aw_info.num_watch_points);

	aw_info.watch_mode = (enum HSA_DBG_WATCH_MODE *) &args_buff[args_idx];
	args_idx += sizeof(enum HSA_DBG_WATCH_MODE) * aw_info.num_watch_points;

	/*
	 * set watch address base pointer to point on the array base
	 * within args_buff
	 */
	aw_info.watch_address = (uint64_t *) &args_buff[args_idx];

	/* skip over the addresses buffer */
	args_idx += sizeof(aw_info.watch_address) * aw_info.num_watch_points;

	if (args_idx >= args->buf_size_in_bytes - sizeof(*args)) {
		status = -EINVAL;
		goto out;
	}

	watch_mask_value = (uint64_t) args_buff[args_idx];

	if (watch_mask_value > 0) {
		/*
		 * There is an array of masks.
		 * set watch mask base pointer to point on the array base
		 * within args_buff
		 */
		aw_info.watch_mask = (uint64_t *) &args_buff[args_idx];

		/* skip over the masks buffer */
		args_idx += sizeof(aw_info.watch_mask) *
				aw_info.num_watch_points;
	} else {
		/* just the NULL mask, set to NULL and skip over it */
		aw_info.watch_mask = NULL;
		args_idx += sizeof(aw_info.watch_mask);
	}

	if (args_idx >= args->buf_size_in_bytes - sizeof(args)) {
		status = -EINVAL;
		goto out;
	}

	/* Currently HSA Event is not supported for DBG */
	aw_info.watch_event = NULL;

	mutex_lock(kfd_get_dbgmgr_mutex());

	status = kfd_dbgmgr_address_watch(dev->dbgmgr, &aw_info);

	mutex_unlock(kfd_get_dbgmgr_mutex());

out:
	kfree(args_buff);

	return status;
}

/* Parse and generate fixed size data structure for wave control */
static int kfd_ioctl_dbg_wave_control(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_dbg_wave_control_args *args = data;
	struct kfd_dev *dev;
	struct kfd_process_device *pdd;
	struct dbg_wave_control_info wac_info;
	unsigned char *args_buff;
	uint32_t computed_buff_size;
	long status;
	void __user *cmd_from_user;
	unsigned int args_idx = 0;

	memset((void *) &wac_info, 0, sizeof(struct dbg_wave_control_info));

	/* we use compact form, independent of the packing attribute value */
	computed_buff_size = sizeof(*args) +
				sizeof(wac_info.mode) +
				sizeof(wac_info.operand) +
				sizeof(wac_info.dbgWave_msg.DbgWaveMsg) +
				sizeof(wac_info.dbgWave_msg.MemoryVA) +
				sizeof(wac_info.trapId);

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		mutex_unlock(&p->mutex);
		pr_debug("Could not find gpu id 0x%x\n", args->gpu_id);
		return -EINVAL;
	}
	dev = pdd->dev;
	mutex_unlock(&p->mutex);

	if (dev->device_info->asic_family == CHIP_CARRIZO) {
		pr_debug("kfd_ioctl_dbg_wave_control not supported on CZ\n");
		return -EINVAL;
	}

	/* input size must match the computed "compact" size */
	if (args->buf_size_in_bytes != computed_buff_size) {
		pr_debug("size mismatch, computed : actual %u : %u\n",
				args->buf_size_in_bytes, computed_buff_size);
		return -EINVAL;
	}

	cmd_from_user = (void __user *) args->content_ptr;

	if (cmd_from_user == NULL)
		return -EINVAL;

	/* copy the entire buffer from user */

	args_buff = memdup_user(cmd_from_user,
				args->buf_size_in_bytes - sizeof(*args));
	if (IS_ERR(args_buff))
		return PTR_ERR(args_buff);

	/* move ptr to the start of the "pay-load" area */
	wac_info.process = p;

	wac_info.operand = *((enum HSA_DBG_WAVEOP *)(&args_buff[args_idx]));
	args_idx += sizeof(wac_info.operand);

	wac_info.mode = *((enum HSA_DBG_WAVEMODE *)(&args_buff[args_idx]));
	args_idx += sizeof(wac_info.mode);

	wac_info.trapId = *((uint32_t *)(&args_buff[args_idx]));
	args_idx += sizeof(wac_info.trapId);

	wac_info.dbgWave_msg.DbgWaveMsg.WaveMsgInfoGen2.Value =
					*((uint32_t *)(&args_buff[args_idx]));
	wac_info.dbgWave_msg.MemoryVA = NULL;

	mutex_lock(kfd_get_dbgmgr_mutex());

	pr_debug("Calling dbg manager process %p, operand %u, mode %u, trapId %u, message %u\n",
			wac_info.process, wac_info.operand,
			wac_info.mode, wac_info.trapId,
			wac_info.dbgWave_msg.DbgWaveMsg.WaveMsgInfoGen2.Value);

	status = kfd_dbgmgr_wave_control(dev->dbgmgr, &wac_info);

	pr_debug("Returned status of dbg manager is %ld\n", status);

	mutex_unlock(kfd_get_dbgmgr_mutex());

	kfree(args_buff);

	return status;
}

static int kfd_ioctl_get_clock_counters(struct file *filep,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_clock_counters_args *args = data;
	struct kfd_process_device *pdd;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (pdd)
		/* Reading GPU clock counter from KGD */
		args->gpu_clock_counter = amdgpu_amdkfd_get_gpu_clock_counter(pdd->dev->kgd);
	else
		/* Node without GPU resource */
		args->gpu_clock_counter = 0;

	mutex_unlock(&p->mutex);

	/* No access to rdtsc. Using raw monotonic time */
	args->cpu_clock_counter = ktime_get_raw_ns();
	args->system_clock_counter = ktime_get_boottime_ns();

	/* Since the counter is in nano-seconds we use 1GHz frequency */
	args->system_clock_freq = 1000000000;

	return 0;
}


static int kfd_ioctl_get_process_apertures(struct file *filp,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_process_apertures_args *args = data;
	struct kfd_process_device_apertures *pAperture;
	int i;

	dev_dbg(kfd_device, "get apertures for PASID 0x%x", p->pasid);

	args->num_of_nodes = 0;

	mutex_lock(&p->mutex);
	/* Run over all pdd of the process */
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

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

		if (++args->num_of_nodes >= NUM_OF_SUPPORTED_GPUS)
			break;
	}
	mutex_unlock(&p->mutex);

	return 0;
}

static int kfd_ioctl_get_process_apertures_new(struct file *filp,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_process_apertures_new_args *args = data;
	struct kfd_process_device_apertures *pa;
	int ret;
	int i;

	dev_dbg(kfd_device, "get apertures for PASID 0x%x", p->pasid);

	if (args->num_of_nodes == 0) {
		/* Return number of nodes, so that user space can alloacate
		 * sufficient memory
		 */
		mutex_lock(&p->mutex);
		args->num_of_nodes = p->n_pdds;
		goto out_unlock;
	}

	/* Fill in process-aperture information for all available
	 * nodes, but not more than args->num_of_nodes as that is
	 * the amount of memory allocated by user
	 */
	pa = kzalloc((sizeof(struct kfd_process_device_apertures) *
				args->num_of_nodes), GFP_KERNEL);
	if (!pa)
		return -ENOMEM;

	mutex_lock(&p->mutex);

	if (!p->n_pdds) {
		args->num_of_nodes = 0;
		kfree(pa);
		goto out_unlock;
	}

	/* Run over all pdd of the process */
	for (i = 0; i < min(p->n_pdds, args->num_of_nodes); i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		pa[i].gpu_id = pdd->dev->id;
		pa[i].lds_base = pdd->lds_base;
		pa[i].lds_limit = pdd->lds_limit;
		pa[i].gpuvm_base = pdd->gpuvm_base;
		pa[i].gpuvm_limit = pdd->gpuvm_limit;
		pa[i].scratch_base = pdd->scratch_base;
		pa[i].scratch_limit = pdd->scratch_limit;

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
	}
	mutex_unlock(&p->mutex);

	args->num_of_nodes = i;
	ret = copy_to_user(
			(void __user *)args->kfd_process_device_apertures_ptr,
			pa,
			(i * sizeof(struct kfd_process_device_apertures)));
	kfree(pa);
	return ret ? -EFAULT : 0;

out_unlock:
	mutex_unlock(&p->mutex);
	return 0;
}

static int kmap_event_page(struct kfd_process *p, uint64_t event_page_offset)
{
	struct kfd_dev *kfd;
	struct kfd_process_device *pdd;
	void *mem, *kern_addr;
	uint64_t size;
	int err = 0;

	if (p->signal_page) {
		pr_err("Event page is already set\n");
		return -EINVAL;
	}

	pdd = kfd_process_device_data_by_id(p, GET_GPU_ID(event_page_offset));
	if (!pdd) {
		pr_err("Getting device by id failed in %s\n", __func__);
		return -EINVAL;
	}
	kfd = pdd->dev;

	pdd = kfd_bind_process_to_device(kfd, p);
	if (IS_ERR(pdd)) {
		return PTR_ERR(pdd);
	}

	mem = kfd_process_device_translate_handle(pdd,
			GET_IDR_HANDLE(event_page_offset));
	if (!mem) {
		pr_err("Can't find BO, offset is 0x%llx\n", event_page_offset);
		return -EINVAL;
	}

	err = amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel(kfd->kgd,
					mem, &kern_addr, &size);
	if (err) {
		pr_err("Failed to map event page to kernel\n");
		return err;
	}

	err = kfd_event_page_set(p, kern_addr, size, event_page_offset);
	if (err) {
		pr_err("Failed to set event page\n");
		return err;
	}
	return err;
}

static int kfd_ioctl_create_event(struct file *filp, struct kfd_process *p,
					void *data)
{
	struct kfd_ioctl_create_event_args *args = data;
	int err;

	/* For dGPUs the event page is allocated in user mode. The
	 * handle is passed to KFD with the first call to this IOCTL
	 * through the event_page_offset field.
	 */
	if (args->event_page_offset) {
		mutex_lock(&p->mutex);
		err = kmap_event_page(p, args->event_page_offset);
		mutex_unlock(&p->mutex);
		if (err)
			return err;
	}

	err = kfd_event_create(filp, p, args->event_type,
				args->auto_reset != 0, args->node_id,
				&args->event_id, &args->event_trigger_data,
				&args->event_page_offset,
				&args->event_slot_index);

	pr_debug("Created event (id:0x%08x) (%s)\n", args->event_id, __func__);
	return err;
}

static int kfd_ioctl_destroy_event(struct file *filp, struct kfd_process *p,
					void *data)
{
	struct kfd_ioctl_destroy_event_args *args = data;

	return kfd_event_destroy(p, args->event_id);
}

static int kfd_ioctl_set_event(struct file *filp, struct kfd_process *p,
				void *data)
{
	struct kfd_ioctl_set_event_args *args = data;

	return kfd_set_event(p, args->event_id);
}

static int kfd_ioctl_reset_event(struct file *filp, struct kfd_process *p,
				void *data)
{
	struct kfd_ioctl_reset_event_args *args = data;

	return kfd_reset_event(p, args->event_id);
}

static int kfd_ioctl_wait_events(struct file *filp, struct kfd_process *p,
				void *data)
{
	struct kfd_ioctl_wait_events_args *args = data;
	int err;

	err = kfd_wait_on_events(p, args->num_events,
			(void __user *)args->events_ptr,
			(args->wait_for_all != 0),
			args->timeout, &args->wait_result);

	return err;
}
static int kfd_ioctl_set_scratch_backing_va(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_scratch_backing_va_args *args = data;
	struct kfd_process_device *pdd;
	struct kfd_dev *dev;
	long err;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		err = -EINVAL;
		goto bind_process_to_device_fail;
	}
	dev = pdd->dev;

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto bind_process_to_device_fail;
	}

	pdd->qpd.sh_hidden_private_base = args->va_addr;

	mutex_unlock(&p->mutex);

	if (dev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS &&
	    pdd->qpd.vmid != 0 && dev->kfd2kgd->set_scratch_backing_va)
		dev->kfd2kgd->set_scratch_backing_va(
			dev->kgd, args->va_addr, pdd->qpd.vmid);

	return 0;

bind_process_to_device_fail:
	mutex_unlock(&p->mutex);
	return err;
}

static int kfd_ioctl_get_tile_config(struct file *filep,
		struct kfd_process *p, void *data)
{
	struct kfd_ioctl_get_tile_config_args *args = data;
	struct kfd_process_device *pdd;
	struct tile_config config;
	int err = 0;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		mutex_unlock(&p->mutex);
		return -EINVAL;
	}

	amdgpu_amdkfd_get_tile_config(pdd->dev->kgd, &config);

	mutex_unlock(&p->mutex);

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

static int kfd_ioctl_acquire_vm(struct file *filep, struct kfd_process *p,
				void *data)
{
	struct kfd_ioctl_acquire_vm_args *args = data;
	struct kfd_process_device *pdd;
	struct file *drm_file;
	int ret;

	drm_file = fget(args->drm_fd);
	if (!drm_file)
		return -EINVAL;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		ret = -EINVAL;
		goto err_unlock;
	}

	if (pdd->drm_file) {
		ret = pdd->drm_file == drm_file ? 0 : -EBUSY;
		goto err_unlock;
	}

	ret = kfd_process_device_init_vm(pdd, drm_file);
	if (ret)
		goto err_unlock;
	/* On success, the PDD keeps the drm_file reference */
	mutex_unlock(&p->mutex);

	return 0;

err_unlock:
	mutex_unlock(&p->mutex);
	fput(drm_file);
	return ret;
}

bool kfd_dev_is_large_bar(struct kfd_dev *dev)
{
	struct kfd_local_mem_info mem_info;

	if (debug_largebar) {
		pr_debug("Simulate large-bar allocation on non large-bar machine\n");
		return true;
	}

	if (dev->use_iommu_v2)
		return false;

	amdgpu_amdkfd_get_local_mem_info(dev->kgd, &mem_info);
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
	uint64_t offset = args->mmap_offset;
	uint32_t flags = args->flags;

	if (args->size == 0)
		return -EINVAL;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		err = -EINVAL;
		goto err_unlock;
	}

	dev = pdd->dev;

	if ((flags & KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC) &&
		(flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) &&
		!kfd_dev_is_large_bar(dev)) {
		pr_err("Alloc host visible vram on small bar is not allowed\n");
		err = -EINVAL;
		goto err_unlock;
	}

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto err_unlock;
	}

	if (flags & KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL) {
		if (args->size != kfd_doorbell_process_slice(dev)) {
			err = -EINVAL;
			goto err_unlock;
		}
		offset = kfd_get_process_doorbells(pdd);
	} else if (flags & KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP) {
		if (args->size != PAGE_SIZE) {
			err = -EINVAL;
			goto err_unlock;
		}
		offset = amdgpu_amdkfd_get_mmio_remap_phys_addr(dev->kgd);
		if (!offset) {
			err = -ENOMEM;
			goto err_unlock;
		}
	}

	err = amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(
		dev->kgd, args->va_addr, args->size,
		pdd->drm_priv, (struct kgd_mem **) &mem, &offset,
		flags, false);

	if (err)
		goto err_unlock;

	idr_handle = kfd_process_device_create_obj_handle(pdd, mem);
	if (idr_handle < 0) {
		err = -EFAULT;
		goto err_free;
	}

	/* Update the VRAM usage count */
	if (flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM)
		WRITE_ONCE(pdd->vram_usage, pdd->vram_usage + args->size);

	mutex_unlock(&p->mutex);

	args->handle = MAKE_HANDLE(args->gpu_id, idr_handle);
	args->mmap_offset = offset;

	/* MMIO is mapped through kfd device
	 * Generate a kfd mmap offset
	 */
	if (flags & KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP)
		args->mmap_offset = KFD_MMAP_TYPE_MMIO
					| KFD_MMAP_GPU_ID(args->gpu_id);

	return 0;

err_free:
	amdgpu_amdkfd_gpuvm_free_memory_of_gpu(dev->kgd, (struct kgd_mem *)mem,
					       pdd->drm_priv, NULL);
err_unlock:
	mutex_unlock(&p->mutex);
	return err;
}

static int kfd_ioctl_free_memory_of_gpu(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_free_memory_of_gpu_args *args = data;
	struct kfd_process_device *pdd;
	void *mem;
	int ret;
	uint64_t size = 0;

	mutex_lock(&p->mutex);

	pdd = kfd_process_device_data_by_id(p, GET_GPU_ID(args->handle));
	if (!pdd) {
		pr_err("Process device data doesn't exist\n");
		ret = -EINVAL;
		goto err_unlock;
	}

	mem = kfd_process_device_translate_handle(
		pdd, GET_IDR_HANDLE(args->handle));
	if (!mem) {
		ret = -EINVAL;
		goto err_unlock;
	}

	ret = amdgpu_amdkfd_gpuvm_free_memory_of_gpu(pdd->dev->kgd,
				(struct kgd_mem *)mem, pdd->drm_priv, &size);

	/* If freeing the buffer failed, leave the handle in place for
	 * clean-up during process tear-down.
	 */
	if (!ret)
		kfd_process_device_remove_obj_handle(
			pdd, GET_IDR_HANDLE(args->handle));

	WRITE_ONCE(pdd->vram_usage, pdd->vram_usage - size);

err_unlock:
	mutex_unlock(&p->mutex);
	return ret;
}

static int kfd_ioctl_map_memory_to_gpu(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_map_memory_to_gpu_args *args = data;
	struct kfd_process_device *pdd, *peer_pdd;
	void *mem;
	struct kfd_dev *dev;
	long err = 0;
	int i;
	uint32_t *devices_arr = NULL;

	if (!args->n_devices) {
		pr_debug("Device IDs array empty\n");
		return -EINVAL;
	}
	if (args->n_success > args->n_devices) {
		pr_debug("n_success exceeds n_devices\n");
		return -EINVAL;
	}

	devices_arr = kmalloc_array(args->n_devices, sizeof(*devices_arr),
				    GFP_KERNEL);
	if (!devices_arr)
		return -ENOMEM;

	err = copy_from_user(devices_arr,
			     (void __user *)args->device_ids_array_ptr,
			     args->n_devices * sizeof(*devices_arr));
	if (err != 0) {
		err = -EFAULT;
		goto copy_from_user_failed;
	}

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, GET_GPU_ID(args->handle));
	if (!pdd) {
		err = -EINVAL;
		goto get_process_device_data_failed;
	}
	dev = pdd->dev;

	pdd = kfd_bind_process_to_device(dev, p);
	if (IS_ERR(pdd)) {
		err = PTR_ERR(pdd);
		goto bind_process_to_device_failed;
	}

	mem = kfd_process_device_translate_handle(pdd,
						GET_IDR_HANDLE(args->handle));
	if (!mem) {
		err = -ENOMEM;
		goto get_mem_obj_from_handle_failed;
	}

	for (i = args->n_success; i < args->n_devices; i++) {
		peer_pdd = kfd_process_device_data_by_id(p, devices_arr[i]);
		if (!peer_pdd) {
			pr_debug("Getting device by id failed for 0x%x\n",
				 devices_arr[i]);
			err = -EINVAL;
			goto get_mem_obj_from_handle_failed;
		}

		peer_pdd = kfd_bind_process_to_device(peer_pdd->dev, p);
		if (IS_ERR(peer_pdd)) {
			err = PTR_ERR(peer_pdd);
			goto get_mem_obj_from_handle_failed;
		}
		err = amdgpu_amdkfd_gpuvm_map_memory_to_gpu(
			peer_pdd->dev->kgd, (struct kgd_mem *)mem, peer_pdd->drm_priv);
		if (err) {
			pr_err("Failed to map to gpu %d/%d\n",
			       i, args->n_devices);
			goto map_memory_to_gpu_failed;
		}
		args->n_success = i+1;
	}

	mutex_unlock(&p->mutex);

	err = amdgpu_amdkfd_gpuvm_sync_memory(dev->kgd, (struct kgd_mem *) mem, true);
	if (err) {
		pr_debug("Sync memory failed, wait interrupted by user signal\n");
		goto sync_memory_failed;
	}

	/* Flush TLBs after waiting for the page table updates to complete */
	for (i = 0; i < args->n_devices; i++) {
		peer_pdd = kfd_process_device_data_by_id(p, devices_arr[i]);
		if (WARN_ON_ONCE(!peer_pdd))
			continue;

		kfd_flush_tlb(peer_pdd);
	}

	kfree(devices_arr);

	return err;

get_process_device_data_failed:
bind_process_to_device_failed:
get_mem_obj_from_handle_failed:
map_memory_to_gpu_failed:
	mutex_unlock(&p->mutex);
copy_from_user_failed:
sync_memory_failed:
	kfree(devices_arr);

	return err;
}

static int kfd_ioctl_unmap_memory_from_gpu(struct file *filep,
					struct kfd_process *p, void *data)
{
	struct kfd_ioctl_unmap_memory_from_gpu_args *args = data;
	struct kfd_process_device *pdd, *peer_pdd;
	void *mem;
	long err = 0;
	uint32_t *devices_arr = NULL, i;

	if (!args->n_devices) {
		pr_debug("Device IDs array empty\n");
		return -EINVAL;
	}
	if (args->n_success > args->n_devices) {
		pr_debug("n_success exceeds n_devices\n");
		return -EINVAL;
	}

	devices_arr = kmalloc_array(args->n_devices, sizeof(*devices_arr),
				    GFP_KERNEL);
	if (!devices_arr)
		return -ENOMEM;

	err = copy_from_user(devices_arr,
			     (void __user *)args->device_ids_array_ptr,
			     args->n_devices * sizeof(*devices_arr));
	if (err != 0) {
		err = -EFAULT;
		goto copy_from_user_failed;
	}

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, GET_GPU_ID(args->handle));
	if (!pdd) {
		err = -EINVAL;
		goto bind_process_to_device_failed;
	}

	mem = kfd_process_device_translate_handle(pdd,
						GET_IDR_HANDLE(args->handle));
	if (!mem) {
		err = -ENOMEM;
		goto get_mem_obj_from_handle_failed;
	}

	for (i = args->n_success; i < args->n_devices; i++) {
		peer_pdd = kfd_process_device_data_by_id(p, devices_arr[i]);
		if (!peer_pdd) {
			err = -EINVAL;
			goto get_mem_obj_from_handle_failed;
		}
		err = amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
			peer_pdd->dev->kgd, (struct kgd_mem *)mem, peer_pdd->drm_priv);
		if (err) {
			pr_err("Failed to unmap from gpu %d/%d\n",
			       i, args->n_devices);
			goto unmap_memory_from_gpu_failed;
		}
		args->n_success = i+1;
	}
	kfree(devices_arr);

	mutex_unlock(&p->mutex);

	return 0;

bind_process_to_device_failed:
get_mem_obj_from_handle_failed:
unmap_memory_from_gpu_failed:
	mutex_unlock(&p->mutex);
copy_from_user_failed:
	kfree(devices_arr);
	return err;
}

static int kfd_ioctl_alloc_queue_gws(struct file *filep,
		struct kfd_process *p, void *data)
{
	int retval;
	struct kfd_ioctl_alloc_queue_gws_args *args = data;
	struct queue *q;
	struct kfd_dev *dev;

	mutex_lock(&p->mutex);
	q = pqm_get_user_queue(&p->pqm, args->queue_id);

	if (q) {
		dev = q->device;
	} else {
		retval = -EINVAL;
		goto out_unlock;
	}

	if (!dev->gws) {
		retval = -ENODEV;
		goto out_unlock;
	}

	if (dev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS) {
		retval = -ENODEV;
		goto out_unlock;
	}

	retval = pqm_set_gws(&p->pqm, args->queue_id, args->num_gws ? dev->gws : NULL);
	mutex_unlock(&p->mutex);

	args->first_gws = 0;
	return retval;

out_unlock:
	mutex_unlock(&p->mutex);
	return retval;
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
		if (dev)
			break;
	if (!dev)
		return -EINVAL;

	if (args->metadata_ptr) {
		metadata_buffer = kzalloc(args->metadata_size, GFP_KERNEL);
		if (!metadata_buffer)
			return -ENOMEM;
	}

	/* Get dmabuf info from KGD */
	r = amdgpu_amdkfd_get_dmabuf_info(dev->kgd, args->dmabuf_fd,
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
	args->gpu_id = dev->id;
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
	struct kfd_process_device *pdd;
	struct dma_buf *dmabuf;
	struct kfd_dev *dev;
	int idr_handle;
	uint64_t size;
	void *mem;
	int r;

	mutex_lock(&p->mutex);
	pdd = kfd_process_device_data_by_id(p, args->gpu_id);
	if (!pdd) {
		r = -EINVAL;
		goto err_unlock;
	}

	dmabuf = dma_buf_get(args->dmabuf_fd);
	if (IS_ERR(dmabuf)) {
		r = PTR_ERR(dmabuf);
		goto err_unlock;
	}

	pdd = kfd_bind_process_to_device(pdd->dev, p);
	if (IS_ERR(pdd)) {
		r = PTR_ERR(pdd);
		goto err_unlock;
	}

	r = amdgpu_amdkfd_gpuvm_import_dmabuf(pdd->dev->kgd, dmabuf,
					      args->va_addr, pdd->drm_priv,
					      (struct kgd_mem **)&mem, &size,
					      NULL);
	if (r)
		goto err_unlock;

	idr_handle = kfd_process_device_create_obj_handle(pdd, mem);
	if (idr_handle < 0) {
		r = -EFAULT;
		goto err_free;
	}

	mutex_unlock(&p->mutex);
	dma_buf_put(dmabuf);

	args->handle = MAKE_HANDLE(args->gpu_id, idr_handle);

	return 0;

err_free:
	amdgpu_amdkfd_gpuvm_free_memory_of_gpu(dev->kgd, (struct kgd_mem *)mem,
					       pdd->drm_priv, NULL);
err_unlock:
	mutex_unlock(&p->mutex);
	dma_buf_put(dmabuf);
	return r;
}

/* Handle requests for watching SMI events */
static int kfd_ioctl_smi_events(struct file *filep,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_smi_events_args *args = data;
	struct kfd_process_device *pdd;

	mutex_lock(&p->mutex);

	pdd = kfd_process_device_data_by_id(p, args->gpuid);
	if (!pdd) {
		mutex_unlock(&p->mutex);
		return -EINVAL;
	}

	mutex_unlock(&p->mutex);

	return kfd_smi_event_open(pdd->dev, &args->anon_fd);
}

static int kfd_ioctl_set_xnack_mode(struct file *filep,
				    struct kfd_process *p, void *data)
{
	struct kfd_ioctl_set_xnack_mode_args *args = data;
	int r = 0;

	mutex_lock(&p->mutex);
	if (args->xnack_enabled >= 0) {
		if (!list_empty(&p->pqm.queues)) {
			pr_debug("Process has user queues running\n");
			mutex_unlock(&p->mutex);
			return -EBUSY;
		}
		if (args->xnack_enabled && !kfd_process_xnack_mode(p, true))
			r = -EPERM;
		else
			p->xnack_enabled = args->xnack_enabled;
	} else {
		args->xnack_enabled = p->xnack_enabled;
	}
	mutex_unlock(&p->mutex);

	return r;
}

#if IS_ENABLED(CONFIG_HSA_AMD_SVM)
static int kfd_ioctl_svm(struct file *filep, struct kfd_process *p, void *data)
{
	struct kfd_ioctl_svm_args *args = data;
	int r = 0;

	if (p->svm_disabled)
		return -EPERM;

	pr_debug("start 0x%llx size 0x%llx op 0x%x nattr 0x%x\n",
		 args->start_addr, args->size, args->op, args->nattr);

	if ((args->start_addr & ~PAGE_MASK) || (args->size & ~PAGE_MASK))
		return -EINVAL;
	if (!args->start_addr || !args->size)
		return -EINVAL;

	mutex_lock(&p->mutex);

	r = svm_ioctl(p, args->op, args->start_addr, args->size, args->nattr,
		      args->attrs);

	mutex_unlock(&p->mutex);

	return r;
}
#else
static int kfd_ioctl_svm(struct file *filep, struct kfd_process *p, void *data)
{
	return -EPERM;
}
#endif
static int kfd_devinfo_dump(struct kfd_process *p, struct kfd_ioctl_criu_dumper_args *args)
{
	int ret = 0;
	int index;
	struct kfd_criu_devinfo_bucket *devinfos;

	if (p->n_pdds != args->num_of_devices)
		return -EINVAL;

	devinfos = kvzalloc((sizeof(struct kfd_criu_devinfo_bucket) *
			     args->num_of_devices), GFP_KERNEL);
	if (!devinfos)
		return -ENOMEM;

	for (index = 0; index < p->n_pdds; index++) {
		struct kfd_process_device *pdd = p->pdds[index];

		devinfos[index].user_gpu_id = pdd->user_gpu_id;
		devinfos[index].actual_gpu_id = pdd->dev->id;
	}

	ret = copy_to_user((void __user *)args->kfd_criu_devinfo_buckets_ptr,
			devinfos,
			(args->num_of_devices *
			sizeof(struct kfd_criu_devinfo_bucket)));
	if (ret)
		ret = -EFAULT;

	kvfree(devinfos);
	return ret;
}

static int kfd_devinfo_restore(struct kfd_process *p, struct kfd_criu_devinfo_bucket *devinfos,
			       uint32_t num_of_devices)
{
	int i;
	if (p->n_pdds != num_of_devices)
		return -EINVAL;

	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_dev *dev;
		struct kfd_process_device *pdd;
		struct file *drm_file;

		dev = kfd_device_by_id(devinfos[i].actual_gpu_id);
		if (!dev) {
			pr_err("Failed to find device with gpu_id = %x\n",
				devinfos[i].actual_gpu_id);
			return -EINVAL;
		}

		pdd = kfd_get_process_device_data(dev, p);
		if (!pdd) {
			pr_err("Failed to get pdd for gpu_id = %x\n", devinfos[i].actual_gpu_id);
			return -EINVAL;
		}
		pdd->user_gpu_id = devinfos[i].user_gpu_id;

		drm_file = fget(devinfos[i].drm_fd);
		if (!drm_file) {
			pr_err("Invalid render node file descriptor sent from plugin (%d)\n",
				devinfos[i].drm_fd);
			return -EINVAL;
		}

		if (pdd->drm_file)
			return -EINVAL;

		/* create the vm using render nodes for kfd pdd */
		if (kfd_process_device_init_vm(pdd, drm_file)) {
			pr_err("could not init vm for given pdd\n");
			/* On success, the PDD keeps the drm_file reference */
			fput(drm_file);
			return -EINVAL;
		}
		/*
		 * pdd now already has the vm bound to render node so below api won't create a new
		 * exclusive kfd mapping but use existing one with renderDXXX but is still needed
		 * for iommu v2 binding  and runtime pm.
		 */
		pdd = kfd_bind_process_to_device(dev, p);
		if (IS_ERR(pdd))
			return PTR_ERR(pdd);
	}
	return 0;
}

static int get_queue_data_sizes(struct kfd_process_device *pdd,
				struct queue *q,
				uint32_t *cu_mask_size,
				uint32_t *mqd_size,
				uint32_t *ctl_stack_size)
{
	int ret = 0;

	*cu_mask_size = sizeof(uint32_t) * (q->properties.cu_mask_count / 32);

	ret = pqm_get_queue_dump_info(&pdd->process->pqm,
			q->properties.queue_id, mqd_size, ctl_stack_size);
	if (ret) {
		pr_err("Failed to get queue dump info (%d)\n", ret);
		return ret;
	}
	return ret;
}

static int criu_dump_queue(struct kfd_process_device *pdd,
                           struct queue *q,
                           struct kfd_criu_q_bucket *q_bucket,
			   struct queue_restore_data *qrd)
{
	int ret = 0;

	q_bucket->gpu_id = pdd->dev->id;
	q_bucket->type = q->properties.type;
	q_bucket->format = q->properties.format;
	q_bucket->q_id =  q->properties.queue_id;
	q_bucket->q_address = q->properties.queue_address;
	q_bucket->q_size = q->properties.queue_size;
	q_bucket->priority = q->properties.priority;
	q_bucket->q_percent = q->properties.queue_percent;
	q_bucket->read_ptr_addr = (uint64_t)q->properties.read_ptr;
	q_bucket->write_ptr_addr = (uint64_t)q->properties.write_ptr;
	q_bucket->doorbell_id = q->doorbell_id;

	q_bucket->sdma_id = q->sdma_id;

	q_bucket->eop_ring_buffer_address =
		q->properties.eop_ring_buffer_address;

	q_bucket->eop_ring_buffer_size = q->properties.eop_ring_buffer_size;

	q_bucket->ctx_save_restore_area_address =
		q->properties.ctx_save_restore_area_address;

	q_bucket->ctx_save_restore_area_size =
		q->properties.ctx_save_restore_area_size;

	q_bucket->ctl_stack_size = q->properties.ctl_stack_size;

	/* queue_data contains data in this order cu_mask, mqd, ctl_stack */
	if (qrd->cu_mask_size)
		memcpy(qrd->cu_mask, q->properties.cu_mask, qrd->cu_mask_size);

	ret = pqm_dump_mqd(&pdd->process->pqm, q->properties.queue_id, qrd);
	if (ret) {
		pr_err("Failed dump queue_mqd (%d)\n", ret);
		return ret;
	}

	q_bucket->cu_mask_size = qrd->cu_mask_size;
	q_bucket->mqd_size = qrd->mqd_size;
	q_bucket->ctl_stack_size = qrd->ctl_stack_size;
	return ret;
}

static int criu_dump_queues_device(struct kfd_process_device *pdd,
				unsigned *q_index,
				unsigned int max_num_queues,
				struct kfd_criu_q_bucket *user_buckets,
				uint8_t *queues_data,
				uint32_t *queues_data_offset,
				uint32_t queues_data_size)
{
	struct queue *q;
	struct queue_restore_data qrd;
	struct kfd_criu_q_bucket q_bucket;
	uint8_t *data_ptr = NULL;
	unsigned int data_ptr_size = 0;
	int ret = 0;

	list_for_each_entry(q, &pdd->qpd.queues_list, list) {
		unsigned int q_data_size;
		if (q->properties.type != KFD_QUEUE_TYPE_COMPUTE &&
			q->properties.type != KFD_QUEUE_TYPE_SDMA &&
			q->properties.type != KFD_QUEUE_TYPE_SDMA_XGMI) {

			pr_err("Unsupported queue type (%d)\n", q->properties.type);
			return -ENOTSUPP;
		}

		if (*q_index >= max_num_queues) {
			pr_err("Number of queues(%d) exceed allocated(%d)\n",
				*q_index, max_num_queues);

			ret = -ENOMEM;
			break;
		}

		memset(&q_bucket, 0, sizeof(q_bucket));
		memset(&qrd, 0, sizeof(qrd));

		ret = get_queue_data_sizes(pdd, q, &qrd.cu_mask_size,
				&qrd.mqd_size, &qrd.ctl_stack_size);
		if (ret) {
			pr_err("Failed to get queue dump info (%d)\n", ret);
			ret = -EFAULT;
			break;
		}

		q_data_size = qrd.cu_mask_size + qrd.mqd_size + qrd.ctl_stack_size;

		/* Increase local buffer space if needed */
		if (data_ptr_size < q_data_size) {
			if (data_ptr)
				kfree(data_ptr);

			data_ptr = (uint8_t*)kzalloc(q_data_size, GFP_KERNEL);
			if (!data_ptr) {
				ret = -ENOMEM;
				break;
			}
			data_ptr_size = q_data_size;
		}

		/* data stored in this order: cu_mask, mqd, mqd_ctl_stack */
		qrd.cu_mask = data_ptr;
		qrd.mqd = data_ptr + qrd.cu_mask_size;
		qrd.mqd_ctl_stack = qrd.mqd + qrd.mqd_size;

		ret = criu_dump_queue(pdd, q, &q_bucket, &qrd);
		if (ret)
			break;

		if (*queues_data_offset + q_data_size > queues_data_size) {
			pr_err("Required memory exceeds user provided\n");
			ret = -ENOSPC;
			break;
		}
		ret = copy_to_user((void __user *) (queues_data + *queues_data_offset),
				data_ptr, q_data_size);
		if (ret) {
			ret = -EFAULT;
			break;
		}
		q_bucket.queues_data_offset = *queues_data_offset;
		*queues_data_offset += q_data_size;

		ret = copy_to_user((void __user *)&user_buckets[*q_index],
					&q_bucket, sizeof(q_bucket));
		if (ret) {
			pr_err("Failed to copy queue information to user\n");
			ret = -EFAULT;
			break;
		}
		*q_index = *q_index + 1;
	}

	if (data_ptr)
		kfree(data_ptr);

	return ret;
}

static int criu_get_prime_handle(struct drm_gem_object *gobj, int flags,
				      u32 *shared_fd)
{
	struct dma_buf *dmabuf;
	int ret;

	dmabuf = amdgpu_gem_prime_export(gobj, flags);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		pr_err("dmabuf export failed for the BO\n");
		return ret;
	}

	ret = dma_buf_fd(dmabuf, flags);
	if (ret < 0) {
		pr_err("dmabuf create fd failed, ret:%d\n", ret);
		goto out_free_dmabuf;
	}

	*shared_fd = ret;
	return 0;

out_free_dmabuf:
	dma_buf_put(dmabuf);
	return ret;
}

static int kfd_ioctl_criu_dumper(struct file *filep,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_criu_dumper_args *args = data;
	struct kfd_criu_ev_bucket *ev_buckets = NULL;
	struct kfd_criu_bo_buckets *bo_bucket;
	struct amdgpu_bo *dumper_bo;
	int ret, id, index, i = 0;
	struct kgd_mem *kgd_mem;
	int q_index = 0;
	void *mem;

	struct kfd_criu_q_bucket *user_buckets =
		(struct kfd_criu_q_bucket*) args->kfd_criu_q_buckets_ptr;

	uint8_t *queues_data = (uint8_t*)args->queues_data_ptr;
	uint32_t queues_data_offset = 0;

	pr_info("Inside %s\n",__func__);

	if (args->num_of_bos == 0) {
		pr_err("No BOs to be dumped\n");
		return -EINVAL;
	}

	if (p->n_pdds != args->num_of_devices) {
		pr_err("Invalid number of devices %d (expected = %d)\n",
								args->num_of_devices, p->n_pdds);
		return -EINVAL;
	}

	pr_debug("num of bos = %llu queues = %u events = %u\n", args->num_of_bos, args->num_of_queues, args->num_of_events);

	bo_bucket = kvzalloc((sizeof(struct kfd_criu_bo_buckets) *
			     args->num_of_bos), GFP_KERNEL);
	if (!bo_bucket)
		return -ENOMEM;

	if (args->num_of_events) {
		ev_buckets = kvzalloc((sizeof(struct kfd_criu_ev_bucket) *
				args->num_of_events), GFP_KERNEL);

		if (!ev_buckets) {
			ret = -ENOMEM;
			goto clean;
		}
	}

	mutex_lock(&p->mutex);

	if (!kfd_has_process_device_data(p)) {
		pr_err("No pdd for given process\n");
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = kfd_process_evict_queues(p);
	if (ret) {
		pr_err("Failed to evict queues\n");
		goto err_unlock;
	}

	ret = kfd_devinfo_dump(p, args);
	if (ret) {
		pr_err("Failed to dump devices\n");
		goto err_unlock;
	}

	/* Run over all PDDs of the process */
	for (index = 0; index < p->n_pdds; index++) {
		struct kfd_process_device *pdd = p->pdds[index];

		idr_for_each_entry(&pdd->alloc_idr, mem, id) {
			if (!mem) {
				ret = -ENOMEM;
				goto err_unlock;
			}

			kgd_mem = (struct kgd_mem *)mem;
			dumper_bo = kgd_mem->bo;

			if ((uint64_t)kgd_mem->va > pdd->gpuvm_base) {
				if (i >= args->num_of_bos) {
					pr_err("Num of BOs changed since last helper ioctl call\n");
					ret = -EINVAL;
					goto err_unlock;
				}

				bo_bucket[i].bo_addr = (uint64_t)kgd_mem->va;
				bo_bucket[i].bo_size = amdgpu_bo_size(dumper_bo);
				bo_bucket[i].gpu_id = pdd->dev->id;
				bo_bucket[i].bo_alloc_flags = (uint32_t)kgd_mem->alloc_flags;
				bo_bucket[i].idr_handle = id;
				if (bo_bucket[i].bo_alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_USERPTR) {
					ret = amdgpu_ttm_tt_get_userptr(&dumper_bo->tbo,
									&bo_bucket[i].user_addr);
					if (ret) {
						pr_err("Failed to obtain user address for user-pointer bo\n");
						goto err_unlock;
					}
				}

				if (bo_bucket[i].bo_alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) {
					ret = criu_get_prime_handle(&dumper_bo->tbo.base,
							bo_bucket[i].bo_alloc_flags &
							KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE ? DRM_RDWR : 0,
							&bo_bucket[i].dmabuf_fd);
					if (ret)
						goto err_unlock;
				}

				if (bo_bucket[i].bo_alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL)
					bo_bucket[i].bo_offset = KFD_MMAP_TYPE_DOORBELL |
						KFD_MMAP_GPU_ID(pdd->dev->id);
				else if (bo_bucket[i].bo_alloc_flags &
				    KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP)
					bo_bucket[i].bo_offset = KFD_MMAP_TYPE_MMIO |
						KFD_MMAP_GPU_ID(pdd->dev->id);
				else
					bo_bucket[i].bo_offset = amdgpu_bo_mmap_offset(dumper_bo);

				pr_debug("bo_size = 0x%llx, bo_addr = 0x%llx bo_offset = 0x%llx\n"
					 "gpu_id = 0x%x alloc_flags = 0x%x idr_handle = 0x%x",
					 bo_bucket[i].bo_size,
					 bo_bucket[i].bo_addr,
					 bo_bucket[i].bo_offset,
					 bo_bucket[i].gpu_id,
					 bo_bucket[i].bo_alloc_flags,
					 bo_bucket[i].idr_handle);
				i++;
			}
		}

		ret = criu_dump_queues_device(pdd, &q_index,
					args->num_of_queues, user_buckets,
					queues_data, &queues_data_offset,
					args->queues_data_size);

		if (ret)
			goto err_unlock;
	}

	/* Dump events */
	ret = kfd_event_dump(p, &args->event_page_offset, ev_buckets,
				args->num_of_events);
	if (ret) {
		pr_err("failed to dump events, ret=%d\n", ret);
		goto err_unlock;
	}
	ret = copy_to_user((void __user *)args->kfd_criu_ev_buckets_ptr,
			ev_buckets,
			(args->num_of_events *
			sizeof(struct kfd_criu_ev_bucket)));
	kvfree(ev_buckets);
	if (ret) {
		ret = -EFAULT;
		goto err_unlock;
	}

	ret = copy_to_user((void __user *)args->kfd_criu_bo_buckets_ptr,
			bo_bucket,
			(args->num_of_bos *
			 sizeof(struct kfd_criu_bo_buckets)));
	kvfree(bo_bucket);

	kfd_process_restore_queues(p);
	mutex_unlock(&p->mutex);
	return 0;

err_unlock:
	while (i--) {
		if (bo_bucket[i].bo_alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM)
			close_fd(bo_bucket[i].dmabuf_fd);
	}

	kfd_process_restore_queues(p);
	mutex_unlock(&p->mutex);
clean:
	kvfree(ev_buckets);
	pr_err("Dumper ioctl failed err:%d\n", ret);
	return ret;
}

static int set_queue_properties_from_criu(struct queue_properties *qp,
                                       struct kfd_criu_q_bucket *q_bucket,
				       struct queue_restore_data *qrd)
{
	qp->is_interop = false;
	qp->is_gws = q_bucket->is_gws;
	qp->queue_percent = q_bucket->q_percent;
	qp->priority = q_bucket->priority;
	qp->queue_address = q_bucket->q_address;
	qp->queue_size = q_bucket->q_size;
	qp->read_ptr = (uint32_t *) q_bucket->read_ptr_addr;
	qp->write_ptr = (uint32_t *) q_bucket->write_ptr_addr;
	qp->eop_ring_buffer_address = q_bucket->eop_ring_buffer_address;
	qp->eop_ring_buffer_size = q_bucket->eop_ring_buffer_size;
	qp->ctx_save_restore_area_address = q_bucket->ctx_save_restore_area_address;
	qp->ctx_save_restore_area_size = q_bucket->ctx_save_restore_area_size;
	qp->ctl_stack_size = q_bucket->ctl_stack_size;
	qp->type = q_bucket->type;
	qp->format = q_bucket->format;

	if (qrd->cu_mask_size) {
		qp->cu_mask = kzalloc(qrd->cu_mask_size, GFP_KERNEL);
		if (!qp->cu_mask) {
			pr_err("Failed to allocate memory for CU mask\n");
			return -ENOMEM;
		}

		memcpy(qp->cu_mask, qrd->cu_mask, qrd->cu_mask_size);
		qp->cu_mask_count = (qrd->cu_mask_size / sizeof(uint32_t)) * 32;
	}

	return 0;
}

/* criu_restore_queue runs with the process mutex locked */
int criu_restore_queue(struct kfd_process *p,
					struct kfd_dev *dev,
					struct kfd_process_device *pdd,
					struct kfd_criu_q_bucket *q_bucket,
					struct queue_restore_data *qrd)
{
	int ret = 0;
	unsigned int queue_id;
	struct queue_properties qp;

	pr_debug("Restoring Queue: gpu_id:%x type:%x format:%x queue_id:%u "
			"address:%llx size:%llx priority:%u percent:%u "
			"read_ptr:%llx write_ptr:%llx doorbell_id:%x "
			"doorbell_off:%llx queue_address:%llx\n",
			q_bucket->gpu_id,
			q_bucket->type,
			q_bucket->format,
			q_bucket->q_id,
			q_bucket->q_address,
			q_bucket->q_size,
			q_bucket->priority,
			q_bucket->q_percent,
			q_bucket->read_ptr_addr,
			q_bucket->write_ptr_addr,
			q_bucket->doorbell_id,
			q_bucket->doorbell_off,
			q_bucket->q_address);

	memset(&qp, 0, sizeof(qp));
	ret = set_queue_properties_from_criu(&qp, q_bucket, qrd);
	if (ret)
		goto err_create_queue;

	print_queue_properties(&qp);

	qrd->qid = q_bucket->q_id;
	qrd->sdma_id = q_bucket->sdma_id;
	qrd->doorbell_id = q_bucket->doorbell_id;

	ret = pqm_create_queue(&p->pqm, dev, NULL, &qp, &queue_id, qrd, NULL);
	if (ret) {
		pr_err("Failed to create new queue err:%d\n", ret);
		ret = -EINVAL;
		goto err_create_queue;
	}

	pr_debug("Queue id %d was restored successfully\n", queue_id);

	return 0;
err_create_queue:
	if (qp.cu_mask)
		kfree(qp.cu_mask);

	return ret;
}

/* criu_restore_queues runs with the process mutex locked */
static int criu_restore_queues(struct kfd_process *p,
			struct kfd_ioctl_criu_restorer_args *args)
{
	struct kfd_process_device *pdd;
	struct kfd_dev *dev;
	int i;
	int ret;
	struct kfd_criu_q_bucket *user_buckets =
		(struct kfd_criu_q_bucket*) args->kfd_criu_q_buckets_ptr;

	uint8_t *queues_data = (uint8_t*)args->queues_data_ptr;
	uint8_t *data_ptr = NULL;
	uint32_t data_ptr_size = 0;

	/*
         * This process will not have any queues at this point, but we are
         * setting all the dqm's for this process to evicted state.
         */
        kfd_process_evict_queues(p);

	for (i = 0; i < args->num_of_queues; i++) {
		struct kfd_criu_q_bucket q_bucket;
		struct queue_restore_data qrd;
		uint32_t q_data_size;

		memset(&qrd, 0, sizeof(qrd));

		ret = copy_from_user(&q_bucket, (void __user *)&user_buckets[i],
				sizeof(struct kfd_criu_q_bucket));

		if (ret) {
			ret = -EFAULT;
			pr_err("Failed to access");
			return ret;
		}

		dev = kfd_device_by_id(q_bucket.gpu_id);
		if (!dev) {
			pr_err("Could not get kfd_dev from gpu_id = 0x%x\n",
			q_bucket.gpu_id);

			ret = -EINVAL;
			return ret;
		}

		pdd = kfd_get_process_device_data(dev, p);
		if (!pdd) {
			pr_err("Failed to get pdd\n");
			ret = -EFAULT;
			return ret;
		}

		q_data_size = q_bucket.cu_mask_size + q_bucket.mqd_size
				+ q_bucket.ctl_stack_size;

		/* Increase local buffer space if needed */
		if (q_data_size > data_ptr_size) {
			if (data_ptr)
				kfree(data_ptr);

			data_ptr = (uint8_t*)kzalloc(q_data_size, GFP_KERNEL);
			if (!data_ptr) {
				ret = -ENOMEM;
				break;
			}
			data_ptr_size = q_data_size;
		}

		ret = copy_from_user(data_ptr,
				(void __user *) queues_data + q_bucket.queues_data_offset,
				q_data_size);
		if (ret) {
			ret = -EFAULT;
			break;
		}

		qrd.cu_mask_size = q_bucket.cu_mask_size;
		qrd.cu_mask = data_ptr;

		qrd.mqd_size = q_bucket.mqd_size;
		qrd.mqd = data_ptr + qrd.cu_mask_size;

		qrd.ctl_stack_size = q_bucket.ctl_stack_size;
		qrd.mqd_ctl_stack = qrd.mqd + qrd.mqd_size;

		ret = criu_restore_queue(p, dev, pdd, &q_bucket, &qrd);
		if (ret) {
			pr_err("Failed to restore queue (%d)\n", ret);
			break;
		}
	}

	if (data_ptr)
		kfree(data_ptr);
	return ret;
}

/* criu_restore_queues_events runs with the process mutex locked */
static int criu_restore_events(struct file *filp, struct kfd_process *p,
			struct kfd_ioctl_criu_restorer_args *args)
{
	int i;
	struct kfd_criu_ev_bucket *events;
	int ret = 0;

	if (args->event_page_offset) {
		ret = kmap_event_page(p, args->event_page_offset);
		if (ret)
			return ret;
	}

	if (!args->num_of_events)
		return 0;

	events = kvmalloc_array(args->num_of_events,
				sizeof(struct kfd_criu_ev_bucket),
				GFP_KERNEL);
	if (!events)
		return -ENOMEM;

	ret = copy_from_user(events, (void __user *) args->kfd_criu_ev_buckets_ptr,
			args->num_of_events * sizeof(struct kfd_criu_ev_bucket));

	if (ret) {
		ret = -EFAULT;
		goto exit;
	}

	for (i = 0; i < args->num_of_events; i++) {
		ret = kfd_event_restore(filp, p, &events[i]);
		if (ret)
			pr_err("Failed to restore event with id (%d)\n", ret);
	}
exit:
	kvfree(events);
	return ret;
}

static int kfd_ioctl_criu_restorer(struct file *filep,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_criu_restorer_args *args = data;
	struct kfd_criu_devinfo_bucket *devinfos = NULL;
	uint64_t *restored_bo_offsets_arr = NULL;
	struct kfd_criu_bo_buckets *bo_bucket = NULL;
	const bool criu_resume = true;
	long err = 0;
	int ret, i, j = 0;

	devinfos = kvzalloc((sizeof(struct kfd_criu_devinfo_bucket) *
			     args->num_of_devices), GFP_KERNEL);
	if (!devinfos) {
		err = -ENOMEM;
		goto failed;
	}

	err = copy_from_user(devinfos,
			     (void __user *)args->kfd_criu_devinfo_buckets_ptr,
			     sizeof(struct kfd_criu_devinfo_bucket) *
			     args->num_of_devices);
	if (err != 0) {
		err = -EFAULT;
		goto failed;
	}

	mutex_lock(&p->mutex);
	err = kfd_devinfo_restore(p, devinfos, args->num_of_devices);
	if (err)
		goto err_unlock;


	bo_bucket = kvzalloc((sizeof(struct kfd_criu_bo_buckets) *
			     args->num_of_bos), GFP_KERNEL);
	if (!bo_bucket) {
		err = -ENOMEM;
		goto err_unlock;
	}

	err = copy_from_user(bo_bucket,
			     (void __user *)args->kfd_criu_bo_buckets_ptr,
			     args->num_of_bos * sizeof(struct kfd_criu_bo_buckets));
	if (err != 0) {
		err = -EFAULT;
		goto err_unlock;
	}

	restored_bo_offsets_arr = kvmalloc_array(args->num_of_bos,
					sizeof(*restored_bo_offsets_arr),
					GFP_KERNEL);
	if (!restored_bo_offsets_arr) {
		err = -ENOMEM;
		goto err_unlock;
	}

	/* Prevent MMU notifications until stage-4 IOCTL is received */
	amdgpu_amdkfd_block_mmu_notifications(p->kgd_process_info);

	/* Create and map new BOs */
	for (i = 0; i < args->num_of_bos; i++) {
		struct kfd_dev *dev;
		struct kfd_process_device *pdd;
		struct kgd_mem *kgd_mem;
		void *mem;
		u64 offset;
		int idr_handle;

		dev = kfd_device_by_id(bo_bucket[i].gpu_id);
		if (!dev) {
			err = -EINVAL;
			pr_err("Failed to get pdd\n");
			goto err_unlock;
		}
		pdd = kfd_get_process_device_data(dev, p);
		if (!pdd) {
			err = -EINVAL;
			pr_err("Failed to get pdd\n");
			goto err_unlock;
		}

		pr_debug("kfd restore ioctl - bo_bucket[%d]:\n", i);
		pr_debug("bo_size = 0x%llx, bo_addr = 0x%llx bo_offset = 0x%llx\n"
			"gpu_id = 0x%x alloc_flags = 0x%x\n"
			"idr_handle = 0x%x\n",
			bo_bucket[i].bo_size,
			bo_bucket[i].bo_addr,
			bo_bucket[i].bo_offset,
			bo_bucket[i].gpu_id,
			bo_bucket[i].bo_alloc_flags,
			bo_bucket[i].idr_handle);

		if (bo_bucket[i].bo_alloc_flags &
		    KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL) {
			pr_debug("restore ioctl: KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL\n");
			if (bo_bucket[i].bo_size !=
			    kfd_doorbell_process_slice(dev)) {
				err = -EINVAL;
				goto err_unlock;
			}
			offset = kfd_get_process_doorbells(pdd);
		} else if (bo_bucket[i].bo_alloc_flags &
			   KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP) {
			/* MMIO BOs need remapped bus address */
			pr_info("restore ioctl :KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP\n");
			if (bo_bucket[i].bo_size != PAGE_SIZE) {
				pr_err("Invalid page size\n");
				err = -EINVAL;
				goto err_unlock;
			}
			offset = amdgpu_amdkfd_get_mmio_remap_phys_addr(dev->kgd);
			if (!offset) {
				pr_err("amdgpu_amdkfd_get_mmio_remap_phys_addr failed\n");
				err = -ENOMEM;
				goto err_unlock;
			}
		} else if (bo_bucket[i].bo_alloc_flags &
			   KFD_IOC_ALLOC_MEM_FLAGS_USERPTR) {
			offset = bo_bucket[i].user_addr;
		}


		/* Create the BO */
		ret = amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(dev->kgd,
						bo_bucket[i].bo_addr,
						bo_bucket[i].bo_size,
						pdd->drm_priv,
						(struct kgd_mem **) &mem,
						&offset,
						bo_bucket[i].bo_alloc_flags,
						criu_resume);
		if (ret) {
			pr_err("Could not create the BO\n");
			err = -ENOMEM;
			goto err_unlock;
		}
		pr_debug("New BO created: bo_size = 0x%llx, bo_addr = 0x%llx bo_offset = 0x%llx\n",
			bo_bucket[i].bo_size, bo_bucket[i].bo_addr, offset);

		/* Restore previuos IDR handle */
		pr_debug("Restoring old IDR handle for the BO");
		idr_handle = idr_alloc(&pdd->alloc_idr, mem,
				       bo_bucket[i].idr_handle,
				       bo_bucket[i].idr_handle + 1, GFP_KERNEL);
		if (idr_handle < 0) {
			pr_err("Could not allocate idr\n");
			amdgpu_amdkfd_gpuvm_free_memory_of_gpu(dev->kgd,
						(struct kgd_mem *)mem,
						pdd->drm_priv, NULL);
			goto err_unlock;
		}

		if (bo_bucket[i].bo_alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL)
			restored_bo_offsets_arr[i] = KFD_MMAP_TYPE_DOORBELL |
				KFD_MMAP_GPU_ID(pdd->dev->id);
		if (bo_bucket[i].bo_alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP) {
			restored_bo_offsets_arr[i] = KFD_MMAP_TYPE_MMIO |
				KFD_MMAP_GPU_ID(pdd->dev->id);
		} else if (bo_bucket[i].bo_alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_GTT) {
			restored_bo_offsets_arr[i] = offset;
			pr_debug("updating offset for GTT\n");
		} else if (bo_bucket[i].bo_alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) {
			restored_bo_offsets_arr[i] = offset;
			/* Update the VRAM usage count */
			WRITE_ONCE(pdd->vram_usage,
				   pdd->vram_usage + bo_bucket[i].bo_size);
			pr_debug("updating offset for VRAM\n");
		}

		/* now map these BOs to GPU/s */
		for (j = 0; j < args->num_of_devices; j++) {
			struct kfd_dev *peer;
			struct kfd_process_device *peer_pdd;

			peer = kfd_device_by_id(devinfos[j].actual_gpu_id);

			pr_debug("Inside mapping loop with desired gpu_id = 0x%x\n",
							devinfos[j].actual_gpu_id);
			if (!peer) {
				pr_debug("Getting device by id failed for 0x%x\n",
				devinfos[j].actual_gpu_id);
				err = -EINVAL;
				goto err_unlock;
			}

			peer_pdd = kfd_bind_process_to_device(peer, p);
			if (IS_ERR(peer_pdd)) {
				err = PTR_ERR(peer_pdd);
				goto err_unlock;
			}
			pr_debug("map mem in restore ioctl -> 0x%llx\n",
				 ((struct kgd_mem *)mem)->va);
			err = amdgpu_amdkfd_gpuvm_map_memory_to_gpu(peer->kgd,
				(struct kgd_mem *)mem, peer_pdd->drm_priv);
			if (err) {
				pr_err("Failed to map to gpu %d/%d\n",
				j, args->num_of_devices);
				goto err_unlock;
			}
		}

		err = amdgpu_amdkfd_gpuvm_sync_memory(dev->kgd,
						      (struct kgd_mem *) mem, true);
		if (err) {
			pr_debug("Sync memory failed, wait interrupted by user signal\n");
			goto err_unlock;
		}

		pr_info("map memory was successful for the BO\n");

		/* create the dmabuf object and export the bo */
		kgd_mem = (struct kgd_mem *)mem;
		if (bo_bucket[i].bo_alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) {
			ret = criu_get_prime_handle(&kgd_mem->bo->tbo.base,
						    DRM_RDWR,
						    &bo_bucket[i].dmabuf_fd);
			if (ret)
				goto err_unlock;
		}

	} /* done */

	/* Flush TLBs after waiting for the page table updates to complete */
	for (j = 0; j < args->num_of_devices; j++) {
		struct kfd_dev *peer;
		struct kfd_process_device *peer_pdd;

		peer = kfd_device_by_id(devinfos[j].actual_gpu_id);
		if (WARN_ON_ONCE(!peer))
			continue;
		peer_pdd = kfd_get_process_device_data(peer, p);
		if (WARN_ON_ONCE(!peer_pdd))
			continue;
		kfd_flush_tlb(peer_pdd);
	}

	ret = criu_restore_queues(p, args);
	if (ret) {
		err = ret;
		goto err_unlock;
	}

	ret = criu_restore_events(filep, p, args);
	if (ret) {
		pr_err("Failed to restore events (%d)", ret);
		err = ret;
		goto err_unlock;
	}

	ret = copy_to_user((void __user *)args->restored_bo_array_ptr,
			   restored_bo_offsets_arr,
			   (args->num_of_bos * sizeof(*restored_bo_offsets_arr)));
	if (ret) {
		err = -EFAULT;
		goto err_unlock;
	}

	ret = copy_to_user((void __user *)args->kfd_criu_bo_buckets_ptr,
			bo_bucket,
			(args->num_of_bos *
			 sizeof(struct kfd_criu_bo_buckets)));
	if (ret)
		err = -EFAULT;


err_unlock:
	while (i--) {
		if (bo_bucket[i].bo_alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM)
			close_fd(bo_bucket[i].dmabuf_fd);
	}

	mutex_unlock(&p->mutex);
failed:
	kvfree(bo_bucket);
	kvfree(restored_bo_offsets_arr);
	kvfree(devinfos);

	return err;
}

static int kfd_ioctl_criu_resume(struct file *filep,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_criu_resume_args *args = data;
	struct kfd_process *target = NULL;
	struct pid *pid = NULL;
	int ret = 0;

	pr_debug("Inside %s, target pid for criu restore: %d\n", __func__,
		 args->pid);

	pid = find_get_pid(args->pid);
	if (!pid) {
		pr_err("Cannot find pid info for %i\n", args->pid);
		return -ESRCH;
	}

	pr_debug("calling kfd_lookup_process_by_pid\n");
	target = kfd_lookup_process_by_pid(pid);
	if (!target) {
		pr_debug("Cannot find process info for %i\n", args->pid);
		put_pid(pid);
		return -ESRCH;
	}

	mutex_lock(&target->mutex);
	ret =  amdgpu_amdkfd_criu_resume(target->kgd_process_info);
	mutex_unlock(&target->mutex);

	put_pid(pid);
	kfd_unref_process(target);
	return ret;
}

static int kfd_ioctl_criu_helper(struct file *filep,
				struct kfd_process *p, void *data)
{
	struct kfd_ioctl_criu_helper_args *args = data;
	u32 queues_data_size = 0;
	struct kgd_mem *kgd_mem;
	struct queue *q;
	u64 num_of_bos = 0;
	int id, i = 0;
	u32 q_index = 0;
	void *mem;
	int ret = 0;

	pr_debug("Inside %s\n", __func__);
	mutex_lock(&p->mutex);

	if (!kfd_has_process_device_data(p)) {
		pr_err("No pdd for given process\n");
		ret = -ENODEV;
		goto err_unlock;
	}

	/* Run over all PDDs of the process */
	for (i = 0; i < p->n_pdds; i++) {
		struct kfd_process_device *pdd = p->pdds[i];

		idr_for_each_entry(&pdd->alloc_idr, mem, id) {
			if (!mem) {
				ret = -ENOMEM;
				goto err_unlock;
			}

			kgd_mem = (struct kgd_mem *)mem;
			if ((uint64_t)kgd_mem->va > pdd->gpuvm_base)
				num_of_bos++;
		}

		list_for_each_entry(q, &pdd->qpd.queues_list, list) {
			if (q->properties.type == KFD_QUEUE_TYPE_COMPUTE ||
				q->properties.type == KFD_QUEUE_TYPE_SDMA ||
				q->properties.type == KFD_QUEUE_TYPE_SDMA_XGMI) {

				u32 cu_mask_size = 0;
				u32 mqd_size = 0;
				u32 ctl_stack_size = 0;

				ret = get_queue_data_sizes(pdd, q,
					&cu_mask_size, &mqd_size,
					&ctl_stack_size);
				if (ret)
					goto err_unlock;

				queues_data_size += cu_mask_size + mqd_size
							+ ctl_stack_size;
				q_index++;
			} else {
				pr_err("Unsupported queue type (%d)\n", q->properties.type);
				ret = -ENOTSUPP;
				goto err_unlock;
			}
		}
	}

	args->task_pid = task_pid_nr_ns(p->lead_thread,
					task_active_pid_ns(p->lead_thread));
	args->num_of_devices = p->n_pdds;
	args->num_of_bos = num_of_bos;
	args->num_of_queues = q_index;
	args->queues_data_size = queues_data_size;
	args->num_of_events = kfd_get_num_events(p);

	dev_dbg(kfd_device, "Num of bos = %llu queues:%u events:%u\n", args->num_of_bos, args->num_of_queues, args->num_of_events);
err_unlock:
	mutex_unlock(&p->mutex);
	return ret;
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

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_SCRATCH_BACKING_VA,
			kfd_ioctl_set_scratch_backing_va, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_TILE_CONFIG,
			kfd_ioctl_get_tile_config, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_TRAP_HANDLER,
			kfd_ioctl_set_trap_handler, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_PROCESS_APERTURES_NEW,
			kfd_ioctl_get_process_apertures_new, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_ACQUIRE_VM,
			kfd_ioctl_acquire_vm, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_ALLOC_MEMORY_OF_GPU,
			kfd_ioctl_alloc_memory_of_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_FREE_MEMORY_OF_GPU,
			kfd_ioctl_free_memory_of_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_MAP_MEMORY_TO_GPU,
			kfd_ioctl_map_memory_to_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_UNMAP_MEMORY_FROM_GPU,
			kfd_ioctl_unmap_memory_from_gpu, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_CU_MASK,
			kfd_ioctl_set_cu_mask, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_QUEUE_WAVE_STATE,
			kfd_ioctl_get_queue_wave_state, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_GET_DMABUF_INFO,
				kfd_ioctl_get_dmabuf_info, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_IMPORT_DMABUF,
				kfd_ioctl_import_dmabuf, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_ALLOC_QUEUE_GWS,
			kfd_ioctl_alloc_queue_gws, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SMI_EVENTS,
			kfd_ioctl_smi_events, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SVM, kfd_ioctl_svm, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_SET_XNACK_MODE,
			kfd_ioctl_set_xnack_mode, 0),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_CRIU_DUMPER,
			 kfd_ioctl_criu_dumper, KFD_IOC_FLAG_PTRACE_ATTACHED),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_CRIU_RESTORER,
			 kfd_ioctl_criu_restorer, KFD_IOC_FLAG_ROOT_ONLY),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_CRIU_HELPER,
			 kfd_ioctl_criu_helper, KFD_IOC_FLAG_PTRACE_ATTACHED),

	AMDKFD_IOCTL_DEF(AMDKFD_IOC_CRIU_RESUME,
			 kfd_ioctl_criu_resume, KFD_IOC_FLAG_ROOT_ONLY),
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
	bool ptrace_attached = false;

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

	dev_dbg(kfd_device, "ioctl cmd 0x%x (#0x%x), arg 0x%lx\n", cmd, nr, arg);

	/* Get the process struct from the filep. Only the process
	 * that opened /dev/kfd can use the file descriptor. Child
	 * processes need to create their own KFD device context.
	 */
	process = filep->private_data;

	rcu_read_lock();
	if ((ioctl->flags & KFD_IOC_FLAG_PTRACE_ATTACHED) &&
	    ptrace_parent(process->lead_thread) == current)
		ptrace_attached = true;
	rcu_read_unlock();

	if (process->lead_thread != current->group_leader
	    && !ptrace_attached) {
		dev_dbg(kfd_device, "Using KFD FD in wrong process\n");
		retcode = -EBADF;
		goto err_i1;
	}

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		dev_dbg(kfd_device, "no function\n");
		retcode = -EINVAL;
		goto err_i1;
	}

	/* KFD_IOC_FLAG_ROOT_ONLY is only for CAP_SYS_ADMIN */
	if (unlikely((ioctl->flags & KFD_IOC_FLAG_ROOT_ONLY) &&
		     !capable(CAP_SYS_ADMIN)))
		return -EACCES;

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
		dev_dbg(kfd_device, "ioctl cmd (#0x%x), arg 0x%lx, ret = %d\n",
				nr, arg, retcode);

	return retcode;
}

static int kfd_mmio_mmap(struct kfd_dev *dev, struct kfd_process *process,
		      struct vm_area_struct *vma)
{
	phys_addr_t address;
	int ret;

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	address = amdgpu_amdkfd_get_mmio_remap_phys_addr(dev->kgd);

	vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_NORESERVE |
				VM_DONTDUMP | VM_PFNMAP;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	pr_debug("pasid 0x%x mapping mmio page\n"
		 "     target user address == 0x%08llX\n"
		 "     physical address    == 0x%08llX\n"
		 "     vm_flags            == 0x%04lX\n"
		 "     size                == 0x%04lX\n",
		 process->pasid, (unsigned long long) vma->vm_start,
		 address, vma->vm_flags, PAGE_SIZE);

	ret = io_remap_pfn_range(vma,
				vma->vm_start,
				address >> PAGE_SHIFT,
				PAGE_SIZE,
				vma->vm_page_prot);
	return ret;
}


static int kfd_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct kfd_process *process;
	struct kfd_dev *dev = NULL;
	unsigned long mmap_offset;
	unsigned int gpu_id;

	process = kfd_get_process(current);
	if (IS_ERR(process))
		return PTR_ERR(process);

	mmap_offset = vma->vm_pgoff << PAGE_SHIFT;
	gpu_id = KFD_MMAP_GET_GPU_ID(mmap_offset);
	if (gpu_id)
		dev = kfd_device_by_id(gpu_id);

	switch (mmap_offset & KFD_MMAP_TYPE_MASK) {
	case KFD_MMAP_TYPE_DOORBELL:
		if (!dev)
			return -ENODEV;
		return kfd_doorbell_mmap(dev, process, vma);

	case KFD_MMAP_TYPE_EVENTS:
		return kfd_event_mmap(process, vma);

	case KFD_MMAP_TYPE_RESERVED_MEM:
		if (!dev)
			return -ENODEV;
		return kfd_reserved_mem_mmap(dev, process, vma);
	case KFD_MMAP_TYPE_MMIO:
		if (!dev)
			return -ENODEV;
		return kfd_mmio_mmap(dev, process, vma);
	}

	return -EFAULT;
}
