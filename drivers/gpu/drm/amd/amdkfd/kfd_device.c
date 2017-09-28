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

#if defined(CONFIG_AMD_IOMMU_V2_MODULE) || defined(CONFIG_AMD_IOMMU_V2)
#include <linux/amd-iommu.h>
#endif
#include <linux/bsearch.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/dma-fence.h>
#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_pm4_headers_vi.h"
#include "cwsr_trap_handler_carrizo.h"
#include "cwsr_trap_handler_gfx9.asm"

#define MQD_SIZE_ALIGNED 768

#if defined(CONFIG_AMD_IOMMU_V2_MODULE) || defined(CONFIG_AMD_IOMMU_V2)
static const struct kfd_device_info kaveri_device_info = {
	.asic_family = CHIP_KAVERI,
	.max_pasid_bits = 16,
	/* max num of queues for KV.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.is_need_iommu_device = true,
	.supports_cwsr = false,
	.needs_pci_atomics = false,
};
#endif

static const struct kfd_device_info hawaii_device_info = {
	.asic_family = CHIP_HAWAII,
	.max_pasid_bits = 16,
	/* max num of queues for KV.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.is_need_iommu_device = false,
	.supports_cwsr = false,
	.needs_pci_atomics = false,
};

#if defined(CONFIG_AMD_IOMMU_V2_MODULE) || defined(CONFIG_AMD_IOMMU_V2)
static const struct kfd_device_info carrizo_device_info = {
	.asic_family = CHIP_CARRIZO,
	.max_pasid_bits = 16,
	/* max num of queues for CZ.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.is_need_iommu_device = true,
	.supports_cwsr = true,
	.needs_pci_atomics = false,
};
#endif

static const struct kfd_device_info tonga_device_info = {
	.asic_family = CHIP_TONGA,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.is_need_iommu_device = false,
	.supports_cwsr = false,
	.needs_pci_atomics = true,
};

static const struct kfd_device_info fiji_device_info = {
	.asic_family = CHIP_FIJI,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.is_need_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
};

static const struct kfd_device_info polaris10_device_info = {
	.asic_family = CHIP_POLARIS10,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.is_need_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
};

static const struct kfd_device_info polaris11_device_info = {
	.asic_family = CHIP_POLARIS11,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.is_need_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
};

static const struct kfd_device_info polaris12_device_info = {
	.asic_family = CHIP_POLARIS12,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 4,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.is_need_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
};

static const struct kfd_device_info vega10_device_info = {
	.asic_family = CHIP_VEGA10,
	.max_pasid_bits = 16,
	.max_no_of_hqd  = 24,
	.doorbell_size  = 8,
	.ih_ring_entry_size = 8 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_v9,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED,
	.is_need_iommu_device = false,
	.supports_cwsr = true,
	.needs_pci_atomics = true,
};

struct kfd_deviceid {
	unsigned short did;
	const struct kfd_device_info *device_info;
};

/*
 * //
// TONGA/AMETHYST device IDs (performance segment)
//
#define DEVICE_ID_VI_TONGA_P_6920               0x6920  // unfused
#define DEVICE_ID_VI_TONGA_P_6921               0x6921  // Amethyst XT
#define DEVICE_ID_VI_TONGA_P_6928               0x6928  // Tonga GL XT
#define DEVICE_ID_VI_TONGA_P_692B               0x692B  // Tonga GL PRO
#define DEVICE_ID_VI_TONGA_P_692F               0x692F  // Tonga GL PRO VF
#define DEVICE_ID_VI_TONGA_P_6938               0x6938  // Tonga XT
#define DEVICE_ID_VI_TONGA_P_6939               0x6939  // Tonga PRO
 *
 */
/* Please keep this sorted by increasing device id. */
static const struct kfd_deviceid supported_devices[] = {
#if defined(CONFIG_AMD_IOMMU_V2_MODULE) || defined(CONFIG_AMD_IOMMU_V2)
	{ 0x1304, &kaveri_device_info },	/* Kaveri */
	{ 0x1305, &kaveri_device_info },	/* Kaveri */
	{ 0x1306, &kaveri_device_info },	/* Kaveri */
	{ 0x1307, &kaveri_device_info },	/* Kaveri */
	{ 0x1309, &kaveri_device_info },	/* Kaveri */
	{ 0x130A, &kaveri_device_info },	/* Kaveri */
	{ 0x130B, &kaveri_device_info },	/* Kaveri */
	{ 0x130C, &kaveri_device_info },	/* Kaveri */
	{ 0x130D, &kaveri_device_info },	/* Kaveri */
	{ 0x130E, &kaveri_device_info },	/* Kaveri */
	{ 0x130F, &kaveri_device_info },	/* Kaveri */
	{ 0x1310, &kaveri_device_info },	/* Kaveri */
	{ 0x1311, &kaveri_device_info },	/* Kaveri */
	{ 0x1312, &kaveri_device_info },	/* Kaveri */
	{ 0x1313, &kaveri_device_info },	/* Kaveri */
	{ 0x1315, &kaveri_device_info },	/* Kaveri */
	{ 0x1316, &kaveri_device_info },	/* Kaveri */
	{ 0x1317, &kaveri_device_info },	/* Kaveri */
	{ 0x1318, &kaveri_device_info },	/* Kaveri */
	{ 0x131B, &kaveri_device_info },	/* Kaveri */
	{ 0x131C, &kaveri_device_info },	/* Kaveri */
	{ 0x131D, &kaveri_device_info },	/* Kaveri */
#endif
	{ 0x67A0, &hawaii_device_info },	/* Hawaii */
	{ 0x67A1, &hawaii_device_info },	/* Hawaii */
	{ 0x67A2, &hawaii_device_info },	/* Hawaii */
	{ 0x67A8, &hawaii_device_info },	/* Hawaii */
	{ 0x67A9, &hawaii_device_info },	/* Hawaii */
	{ 0x67AA, &hawaii_device_info },	/* Hawaii */
	{ 0x67B0, &hawaii_device_info },	/* Hawaii */
	{ 0x67B1, &hawaii_device_info },	/* Hawaii */
	{ 0x67B8, &hawaii_device_info },	/* Hawaii */
	{ 0x67B9, &hawaii_device_info },	/* Hawaii */
	{ 0x67BA, &hawaii_device_info },	/* Hawaii */
	{ 0x67BE, &hawaii_device_info },	/* Hawaii */
#if defined(CONFIG_AMD_IOMMU_V2_MODULE) || defined(CONFIG_AMD_IOMMU_V2)
	{ 0x9870, &carrizo_device_info },	/* Carrizo */
	{ 0x9874, &carrizo_device_info },	/* Carrizo */
	{ 0x9875, &carrizo_device_info },	/* Carrizo */
	{ 0x9876, &carrizo_device_info },	/* Carrizo */
	{ 0x9877, &carrizo_device_info },	/* Carrizo */
#endif
	{ 0x6920, &tonga_device_info   },	/* Tonga */
	{ 0x6921, &tonga_device_info   },	/* Tonga */
	{ 0x6928, &tonga_device_info   },	/* Tonga */
	{ 0x692B, &tonga_device_info   },	/* Tonga */
	{ 0x692F, &tonga_device_info   },	/* Tonga */
	{ 0x6938, &tonga_device_info   },	/* Tonga */
	{ 0x6939, &tonga_device_info   },	/* Tonga */
	{ 0x7300, &fiji_device_info    },	/* Fiji */
	{ 0x67C0, &polaris10_device_info },     /* Polaris10 */
	{ 0x67C1, &polaris10_device_info },     /* Polaris10 */
	{ 0x67C2, &polaris10_device_info },     /* Polaris10 */
	{ 0x67C4, &polaris10_device_info },	/* Polaris10 */
	{ 0x67C7, &polaris10_device_info },	/* Polaris10 */
	{ 0x67C8, &polaris10_device_info },     /* Polaris10 */
	{ 0x67C9, &polaris10_device_info },     /* Polaris10 */
	{ 0x67CA, &polaris10_device_info },     /* Polaris10 */
	{ 0x67CC, &polaris10_device_info },     /* Polaris10 */
	{ 0x67CF, &polaris10_device_info },     /* Polaris10 */
	{ 0x67D0, &polaris10_device_info },     /* Polaris10 */
	{ 0x67DF, &polaris10_device_info },	/* Polaris10 */
	{ 0x67E0, &polaris11_device_info },     /* Polaris11 */
	{ 0x67E1, &polaris11_device_info },     /* Polaris11 */
	{ 0x67E3, &polaris11_device_info },	/* Polaris11 */
	{ 0x67E7, &polaris11_device_info },     /* Polaris11 */
	{ 0x67E8, &polaris11_device_info },     /* Polaris11 */
	{ 0x67E9, &polaris11_device_info },     /* Polaris11 */
	{ 0x67EB, &polaris11_device_info },     /* Polaris11 */
	{ 0x67EF, &polaris11_device_info },	/* Polaris11 */
	{ 0x67FF, &polaris11_device_info },	/* Polaris11 */
	{ 0x6860, &vega10_device_info },	/* Vega10 */
	{ 0x6861, &vega10_device_info },	/* Vega10 */
	{ 0x6862, &vega10_device_info },	/* Vega10 */
	{ 0x6863, &vega10_device_info },	/* Vega10 */
	{ 0x6864, &vega10_device_info },	/* Vega10 */
	{ 0x6867, &vega10_device_info },	/* Vega10 */
	{ 0x6868, &vega10_device_info },	/* Vega10 */
	{ 0x686C, &vega10_device_info },	/* Vega10 */
	{ 0x687F, &vega10_device_info },	/* Vega10 */
	{ 0x6980, &polaris12_device_info },	/* Polaris12 */
	{ 0x6981, &polaris12_device_info },	/* Polaris12 */
	{ 0x6985, &polaris12_device_info },	/* Polaris12 */
	{ 0x6986, &polaris12_device_info },	/* Polaris12 */
	{ 0x6987, &polaris12_device_info },	/* Polaris12 */
	{ 0x6995, &polaris12_device_info },	/* Polaris12 */
	{ 0x6997, &polaris12_device_info },	/* Polaris12 */
	{ 0x699F, &polaris12_device_info }	/* Polaris12 */
};

static int kfd_gtt_sa_init(struct kfd_dev *kfd, unsigned int buf_size,
				unsigned int chunk_size);
static void kfd_gtt_sa_fini(struct kfd_dev *kfd);

static int kfd_resume(struct kfd_dev *kfd);

static const struct kfd_device_info *lookup_device_info(unsigned short did)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(supported_devices); i++) {
		if (supported_devices[i].did == did) {
			WARN(!supported_devices[i].device_info,
				"Cannot look up device info, Device Info is NULL");
			return supported_devices[i].device_info;
		}
	}

	return NULL;
}

struct kfd_dev *kgd2kfd_probe(struct kgd_dev *kgd,
	struct pci_dev *pdev, const struct kfd2kgd_calls *f2g)
{
	struct kfd_dev *kfd;

	const struct kfd_device_info *device_info =
					lookup_device_info(pdev->device);

	if (!device_info)
		return NULL;

	if (device_info->needs_pci_atomics) {
		/* Allow BIF to recode atomics to PCIe 3.0 AtomicOps.
		 */
		if (pci_enable_atomic_ops_to_root(pdev) < 0) {
			dev_info(kfd_device,
				"skipped device %x:%x, PCI rejects atomics",
				 pdev->vendor, pdev->device);
			return NULL;
		}
	}

	kfd = kzalloc(sizeof(*kfd), GFP_KERNEL);
	if (!kfd)
		return NULL;

	kfd->kgd = kgd;
	kfd->device_info = device_info;
	kfd->pdev = pdev;
	kfd->init_complete = false;
	kfd->kfd2kgd = f2g;

	mutex_init(&kfd->doorbell_mutex);
	memset(&kfd->doorbell_available_index, 0,
		sizeof(kfd->doorbell_available_index));

	return kfd;
}

#if defined(CONFIG_AMD_IOMMU_V2_MODULE) || defined(CONFIG_AMD_IOMMU_V2)
static bool device_iommu_pasid_init(struct kfd_dev *kfd)
{
	const u32 required_iommu_flags = AMD_IOMMU_DEVICE_FLAG_ATS_SUP |
					AMD_IOMMU_DEVICE_FLAG_PRI_SUP |
					AMD_IOMMU_DEVICE_FLAG_PASID_SUP;

	struct amd_iommu_device_info iommu_info;
	unsigned int pasid_limit;
	int err;

	err = amd_iommu_device_info(kfd->pdev, &iommu_info);
	if (err < 0) {
		dev_err(kfd_device,
			"error getting iommu info. is the iommu enabled?\n");
		return false;
	}

	if ((iommu_info.flags & required_iommu_flags) != required_iommu_flags) {
		dev_err(kfd_device, "error required iommu flags ats %i, pri %i, pasid %i\n",
		       (iommu_info.flags & AMD_IOMMU_DEVICE_FLAG_ATS_SUP) != 0,
		       (iommu_info.flags & AMD_IOMMU_DEVICE_FLAG_PRI_SUP) != 0,
		       (iommu_info.flags & AMD_IOMMU_DEVICE_FLAG_PASID_SUP)
									!= 0);
		return false;
	}

	pasid_limit = min_t(unsigned int,
			(unsigned int)(1 << kfd->device_info->max_pasid_bits),
			iommu_info.max_pasids);
	/*
	 * last pasid is used for kernel queues doorbells
	 * in the future the last pasid might be used for a kernel thread.
	 */
	pasid_limit = min_t(unsigned int,
				pasid_limit,
				kfd->doorbell_process_limit - 1);

	if (!kfd_set_pasid_limit(pasid_limit)) {
		dev_err(kfd_device, "error setting pasid limit\n");
		return false;
	}

	return true;
}

static void iommu_pasid_shutdown_callback(struct pci_dev *pdev, int pasid)
{
	struct kfd_dev *dev = kfd_device_by_pci_dev(pdev);

	if (dev)
		kfd_process_iommu_unbind_callback(dev, pasid);
}

/*
 * This function called by IOMMU driver on PPR failure
 */
static int iommu_invalid_ppr_cb(struct pci_dev *pdev, int pasid,
		unsigned long address, u16 flags)
{
	struct kfd_dev *dev;

	dev_warn(kfd_device,
			"Invalid PPR device %x:%x.%x pasid %d address 0x%lX flags 0x%X",
			PCI_BUS_NUM(pdev->devfn),
			PCI_SLOT(pdev->devfn),
			PCI_FUNC(pdev->devfn),
			pasid,
			address,
			flags);

	dev = kfd_device_by_pci_dev(pdev);
	if (WARN_ON(!dev))
		return -ENODEV;

	kfd_signal_iommu_event(dev, pasid, address,
			flags & PPR_FAULT_WRITE, flags & PPR_FAULT_EXEC);

	return AMD_IOMMU_INV_PRI_RSP_INVALID;
}
#endif /* CONFIG_AMD_IOMMU_V2 */

static int kfd_cwsr_init(struct kfd_dev *kfd)
{
	/*
	 * Initialize the CWSR required memory for TBA and TMA
	 */
	if (cwsr_enable && kfd->device_info->supports_cwsr) {
		const uint32_t *cwsr_hex;
		void *cwsr_addr = NULL;
		unsigned int size;

		if (kfd->device_info->asic_family < CHIP_VEGA10) {
			cwsr_hex = cwsr_trap_carrizo_hex;
			size = sizeof(cwsr_trap_carrizo_hex);
		} else {
			cwsr_hex = cwsr_trap_gfx9_hex;
			size = sizeof(cwsr_trap_gfx9_hex);
		}

		if (size > PAGE_SIZE) {
			pr_err("Wrong CWSR ISA size.\n");
			return -EINVAL;
		}
		kfd->cwsr_size =
			ALIGN(size, PAGE_SIZE) + PAGE_SIZE;
		kfd->cwsr_pages = alloc_pages(GFP_KERNEL | __GFP_HIGHMEM,
					get_order(kfd->cwsr_size));
		if (!kfd->cwsr_pages) {
			pr_err("Failed to allocate CWSR isa memory.\n");
			return -ENOMEM;
		}
		/*Only first page used for cwsr ISA code */
		cwsr_addr = kmap(kfd->cwsr_pages);
		memset(cwsr_addr, 0, PAGE_SIZE);
		memcpy(cwsr_addr, cwsr_hex, size);
		kunmap(kfd->cwsr_pages);
		kfd->tma_offset = ALIGN(size, PAGE_SIZE);
		kfd->cwsr_enabled = true;
		dev_info(kfd_device,
			"Reserved %d pages for cwsr.\n",
			(kfd->cwsr_size >> PAGE_SHIFT));
	}

	return 0;
}

static void kfd_cwsr_fini(struct kfd_dev *kfd)
{
	if (kfd->cwsr_pages)
		__free_pages(kfd->cwsr_pages, get_order(kfd->cwsr_size));
}

static void kfd_ib_mem_init(struct kfd_dev *kdev)
{
	/* In certain cases we need to send IB from kernel using the GPU address
	 * space created by user applications.
	 * For example, on GFX v7, we need to flush TC associated to the VMID
	 * before tearing down the VMID. In order to do so, we need an address
	 * valid to the VMID to place the IB while this space was created on
	 * the user's side, not the kernel.
	 * Since kfd_set_process_dgpu_aperture reserves "cwsr_base + cwsr_size"
	 * but CWSR only uses pages above cwsr_base, we'll use one page memory
	 * under cwsr_base for IB submissions
	 */
	kdev->ib_size = PAGE_SIZE;
}

bool kgd2kfd_device_init(struct kfd_dev *kfd,
			 const struct kgd2kfd_shared_resources *gpu_resources)
{
	unsigned int size;
	unsigned int vmid_bitmap_kfd, vmid_num_kfd;

	kfd->mec_fw_version = kfd->kfd2kgd->get_fw_version(kfd->kgd,
			KGD_ENGINE_MEC1);

	kfd->shared_resources = *gpu_resources;

	vmid_bitmap_kfd = kfd->shared_resources.compute_vmid_bitmap;
	kfd->vm_info.first_vmid_kfd = ffs(vmid_bitmap_kfd) - 1;
	kfd->vm_info.last_vmid_kfd = fls(vmid_bitmap_kfd) - 1;
	vmid_num_kfd = kfd->vm_info.last_vmid_kfd
			- kfd->vm_info.first_vmid_kfd + 1;
	kfd->vm_info.vmid_num_kfd = vmid_num_kfd;

	/* Verify module parameters regarding mapped process number*/
	if ((hws_max_conc_proc < 0)
			|| (hws_max_conc_proc > vmid_num_kfd)) {
		dev_err(kfd_device,
			"hws_max_conc_proc %d must be between 0 and %d, use %d instead\n",
			hws_max_conc_proc, vmid_num_kfd, vmid_num_kfd);
		kfd->max_proc_per_quantum = vmid_num_kfd;
	} else
		kfd->max_proc_per_quantum = hws_max_conc_proc;

	/* calculate max size of mqds needed for queues */
	size = max_num_of_queues_per_device *
			kfd->device_info->mqd_size_aligned;

	/*
	 * calculate max size of runlist packet.
	 * There can be only 2 packets at once
	 */
	size += (KFD_MAX_NUM_OF_PROCESSES * sizeof(struct pm4_mes_map_process) +
		max_num_of_queues_per_device * sizeof(struct pm4_mes_map_queues)
		+ sizeof(struct pm4_mes_runlist)) * 2;

	/* Add size of HIQ & DIQ */
	size += KFD_KERNEL_QUEUE_SIZE * 2;

	/* add another 512KB for all other allocations on gart (HPD, fences) */
	size += 512 * 1024;

	if (kfd->kfd2kgd->init_gtt_mem_allocation(
			kfd->kgd, size, &kfd->gtt_mem,
			&kfd->gtt_start_gpu_addr, &kfd->gtt_start_cpu_ptr)){
		dev_err(kfd_device,
			"Could not allocate %d bytes for device %x:%x\n",
			size, kfd->pdev->vendor, kfd->pdev->device);
		goto out;
	}

	dev_info(kfd_device,
		"Allocated %d bytes on gart for device %x:%x\n",
		size, kfd->pdev->vendor, kfd->pdev->device);

	/* Initialize GTT sa with 512 byte chunk size */
	if (kfd_gtt_sa_init(kfd, size, 512) != 0) {
		dev_err(kfd_device,
			"Error initializing gtt sub-allocator\n");
		goto kfd_gtt_sa_init_error;
	}

	kfd_doorbell_init(kfd);

	if (kfd_topology_add_device(kfd) != 0) {
		dev_err(kfd_device,
			"Error adding device %x:%x to topology\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto kfd_topology_add_device_error;
	}

	if (kfd_interrupt_init(kfd)) {
		dev_err(kfd_device,
			"Error initializing interrupts for device %x:%x\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto kfd_interrupt_error;
	}

	kfd->dqm = device_queue_manager_init(kfd);
	if (!kfd->dqm) {
		dev_err(kfd_device,
			"Error initializing queue manager for device %x:%x\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto device_queue_manager_error;
	}

#if defined(CONFIG_AMD_IOMMU_V2_MODULE) || defined(CONFIG_AMD_IOMMU_V2)
	if (kfd->device_info->is_need_iommu_device) {
		if (!device_iommu_pasid_init(kfd)) {
			dev_err(kfd_device,
				"Error initializing iommuv2 for device %x:%x\n",
				kfd->pdev->vendor, kfd->pdev->device);
			goto device_iommu_pasid_error;
		}
	}
#endif

	if (kfd_cwsr_init(kfd))
		goto device_iommu_pasid_error;

	kfd_ib_mem_init(kfd);

	if (kfd_resume(kfd))
		goto kfd_resume_error;

	kfd->dbgmgr = NULL;

	kfd->init_complete = true;
	dev_info(kfd_device, "added device %x:%x\n", kfd->pdev->vendor,
		 kfd->pdev->device);

	pr_debug("Starting kfd with the following scheduling policy %d\n",
		kfd->dqm->sched_policy);

	goto out;

kfd_resume_error:
	kfd_cwsr_fini(kfd);
device_iommu_pasid_error:
	device_queue_manager_uninit(kfd->dqm);
device_queue_manager_error:
	kfd_interrupt_exit(kfd);
kfd_interrupt_error:
	kfd_topology_remove_device(kfd);
kfd_topology_add_device_error:
	kfd_gtt_sa_fini(kfd);
kfd_gtt_sa_init_error:
	kfd->kfd2kgd->free_gtt_mem(kfd->kgd, kfd->gtt_mem);
	dev_err(kfd_device,
		"device %x:%x NOT added due to errors\n",
		kfd->pdev->vendor, kfd->pdev->device);
out:
	return kfd->init_complete;
}

void kgd2kfd_device_exit(struct kfd_dev *kfd)
{
	if (kfd->init_complete) {
		kgd2kfd_suspend(kfd);
		kfd_cwsr_fini(kfd);
		device_queue_manager_uninit(kfd->dqm);
		kfd_interrupt_exit(kfd);
		kfd_topology_remove_device(kfd);
		kfd_gtt_sa_fini(kfd);
		kfd->kfd2kgd->free_gtt_mem(kfd->kgd, kfd->gtt_mem);
	}

	kfree(kfd);
}

void kgd2kfd_suspend(struct kfd_dev *kfd)
{
	if (!kfd->init_complete)
		return;

	kfd->dqm->ops.stop(kfd->dqm);

#if defined(CONFIG_AMD_IOMMU_V2_MODULE) || defined(CONFIG_AMD_IOMMU_V2)
	if (!kfd->device_info->is_need_iommu_device)
		return;

	kfd_unbind_processes_from_device(kfd);

	amd_iommu_set_invalidate_ctx_cb(kfd->pdev, NULL);
	amd_iommu_set_invalid_ppr_cb(kfd->pdev, NULL);
	amd_iommu_free_device(kfd->pdev);
#endif
}

int kgd2kfd_resume(struct kfd_dev *kfd)
{
	if (!kfd->init_complete)
		return 0;

	return kfd_resume(kfd);

}

static int kfd_resume(struct kfd_dev *kfd)
{
	int err = 0;

#if defined(CONFIG_AMD_IOMMU_V2_MODULE) || defined(CONFIG_AMD_IOMMU_V2)
	if (kfd->device_info->is_need_iommu_device) {
		unsigned int pasid_limit = kfd_get_pasid_limit();

		err = amd_iommu_init_device(kfd->pdev, pasid_limit);
		if (err)
			return -ENXIO;
		amd_iommu_set_invalidate_ctx_cb(kfd->pdev,
				iommu_pasid_shutdown_callback);
		amd_iommu_set_invalid_ppr_cb(kfd->pdev,
				iommu_invalid_ppr_cb);

		err = kfd_bind_processes_to_device(kfd);
		if (err)
			return -ENXIO;
	}
#endif

	err = kfd->dqm->ops.start(kfd->dqm);
	if (err) {
		dev_err(kfd_device,
			"Error starting queue manager for device %x:%x\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto dqm_start_error;
	}

	kfd->kfd2kgd->write_config_static_mem(kfd->kgd, true, 1, 3, 0);

	return err;

dqm_start_error:
#if defined(CONFIG_AMD_IOMMU_V2_MODULE) || defined(CONFIG_AMD_IOMMU_V2)
	if (kfd->device_info->is_need_iommu_device)
		amd_iommu_free_device(kfd->pdev);
#endif
	return err;
}

/* This is called directly from KGD at ISR. */
void kgd2kfd_interrupt(struct kfd_dev *kfd, const void *ih_ring_entry)
{
	uint32_t patched_ihre[DIV_ROUND_UP(
				kfd->device_info->ih_ring_entry_size,
				sizeof(uint32_t))];
	bool is_patched = false;

	if (!kfd->init_complete)
		return;

	spin_lock(&kfd->interrupt_lock);

	if (kfd->interrupts_active && interrupt_is_wanted(kfd, ih_ring_entry,
						patched_ihre, &is_patched)
	    && enqueue_ih_ring_entry(kfd,
				is_patched ? patched_ihre : ih_ring_entry))
		queue_work(kfd->ih_wq, &kfd->interrupt_work);

	spin_unlock(&kfd->interrupt_lock);
}

/* quiesce_process_mm -
 *  Quiesce all user queues that belongs to given process p
 */
static int quiesce_process_mm(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	int r = 0;
	unsigned int n_evicted = 0;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		r = process_evict_queues(pdd->dev->dqm, &pdd->qpd, false);
		if (r != 0) {
			pr_err("Failed to evict process queues\n");
			goto fail;
		}
		n_evicted++;
	}

	return r;

fail:
	/* To keep state consistent, roll back partial eviction by
	 * restoring queues
	 */
	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		if (n_evicted == 0)
			break;
		if (process_restore_queues(pdd->dev->dqm, &pdd->qpd))
			pr_err("Failed to restore queues\n");

		n_evicted--;
	}

	return r;
}

/* resume_process_mm -
 *  Resume all user queues that belongs to given process p. The caller must
 *  ensure that process p context is valid.
 */
static int resume_process_mm(struct kfd_process *p)
{
	struct kfd_process_device *pdd;
	struct mm_struct *mm = (struct mm_struct *)p->mm;
	int r, ret = 0;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list) {
		if (pdd->dev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS)
			down_read(&mm->mmap_sem);

		r = process_restore_queues(pdd->dev->dqm, &pdd->qpd);
		if (r != 0) {
			pr_err("Failed to restore process queues\n");
			if (ret == 0)
				ret = r;
		}

		if (pdd->dev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS)
			up_read(&mm->mmap_sem);
	}

	return ret;
}

int kgd2kfd_quiesce_mm(struct kfd_dev *kfd, struct mm_struct *mm)
{
	struct kfd_process *p;
	struct kfd_process_device *pdd;
	int r;

	/* Because we are called from arbitrary context (workqueue) as opposed
	 * to process context, kfd_process could attempt to exit while we are
	 * running so the lookup function increments the process ref count.
	 */
	p = kfd_lookup_process_by_mm(mm);
	if (!p)
		return -ENODEV;

	if (kfd) {
		r = -ENODEV;
		pdd = kfd_get_process_device_data(kfd, p);
		if (pdd)
			r = process_evict_queues(kfd->dqm, &pdd->qpd, false);
	} else {
		r = quiesce_process_mm(p);
	}

	kfd_unref_process(p);
	return r;
}

int kgd2kfd_resume_mm(struct kfd_dev *kfd, struct mm_struct *mm)
{
	struct kfd_process *p;
	struct kfd_process_device *pdd;
	int r;

	/* Because we are called from arbitrary context (workqueue) as opposed
	 * to process context, kfd_process could attempt to exit while we are
	 * running so the lookup function increments the process ref count.
	 */
	p = kfd_lookup_process_by_mm(mm);
	if (!p)
		return -ENODEV;

	if (kfd) {
		r = -ENODEV;
		pdd = kfd_get_process_device_data(kfd, p);
		if (pdd)
			r = process_restore_queues(kfd->dqm, &pdd->qpd);
	} else {
		r = resume_process_mm(p);
	}

	kfd_unref_process(p);
	return r;
}


void kfd_restore_bo_worker(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct kfd_process *p;
	struct kfd_process_device *pdd;
	int ret = 0;

	dwork = to_delayed_work(work);

	/* Process termination destroys this worker thread. So during the
	 * lifetime of this thread, kfd_process p will be valid
	 */
	p = container_of(dwork, struct kfd_process, restore_work);

	/* Call restore_process_bos on the first KGD device. This function
	 * takes care of restoring the whole process including other devices.
	 * Restore can fail if enough memory is not available. If so,
	 * reschedule again.
	 */
	pdd = list_first_entry(&p->per_device_data,
			       struct kfd_process_device,
			       per_device_list);

	pr_info("Started restoring process of pasid %d\n", p->pasid);

	/* Setting last_restore_timestamp before successful restoration.
	 * Otherwise this would have to be set by KGD (restore_process_bos)
	 * before KFD BOs are unreserved. If not, the process can be evicted
	 * again before the timestamp is set.
	 * If restore fails, the timestamp will be set again in the next
	 * attempt. This would mean that the minimum GPU quanta would be
	 * PROCESS_ACTIVE_TIME_MS - (time to execute the following two
	 * functions)
	 */

	p->last_restore_timestamp = get_jiffies_64();
	ret = pdd->dev->kfd2kgd->restore_process_bos(p->process_info);
	if (ret) {
		pr_info("Restore failed, try again after %d ms\n",
			PROCESS_BACK_OFF_TIME_MS);
		ret = schedule_delayed_work(&p->restore_work,
				PROCESS_BACK_OFF_TIME_MS);
		WARN(!ret, "reschedule restore work failed\n");
		return;
	}

	ret = resume_process_mm(p);
	if (ret)
		pr_err("Failed to resume user queues\n");

	pr_info("Finished restoring process of pasid %d\n", p->pasid);
}

/** kgd2kfd_schedule_evict_and_restore_process - Schedules work queue that will
 *   prepare for safe eviction of KFD BOs that belong to the specified
 *   process.
 *
 * @mm: mm_struct that identifies the specified KFD process
 * @fence: eviction fence attached to KFD process BOs
 *
 */
int kgd2kfd_schedule_evict_and_restore_process(struct mm_struct *mm,
					       struct dma_fence *fence)
{
	struct kfd_process *p;
	unsigned long active_time;
	unsigned long delay_jiffies = msecs_to_jiffies(PROCESS_ACTIVE_TIME_MS);

	if (!fence)
		return -EINVAL;

	if (dma_fence_is_signaled(fence))
		return 0;

	p = kfd_lookup_process_by_mm(mm);
	if (!p)
		return -ENODEV;

	if (delayed_work_pending(&p->eviction_work.dwork)) {
		/* It is possible has TTM has lined up couple of BOs of the same
		 * process to be evicted. Check if the fence is same which
		 * indicates that previous work item scheduled is not completed
		 */
		if (p->eviction_work.quiesce_fence == fence)
			goto out;
		else {
			WARN(1, "Starting new evict with previous evict is not completed\n");
			if (cancel_delayed_work_sync(&p->eviction_work.dwork))
				dma_fence_put(p->eviction_work.quiesce_fence);
		}
	}

	p->eviction_work.quiesce_fence = dma_fence_get(fence);

	/* Avoid KFD process starvation. Wait for at least
	 * PROCESS_ACTIVE_TIME_MS before evicting the process again
	 */
	active_time = get_jiffies_64() - p->last_restore_timestamp;
	if (delay_jiffies > active_time)
		delay_jiffies -= active_time;
	else
		delay_jiffies = 0;

	/* During process initialization eviction_work.dwork is initialized
	 * to kfd_evict_bo_worker
	 */
	schedule_delayed_work(&p->eviction_work.dwork, delay_jiffies);
out:
	kfd_unref_process(p);
	return 0;
}

void kfd_evict_bo_worker(struct work_struct *work)
{
	int ret;
	struct kfd_process *p;
	struct kfd_eviction_work *eviction_work;
	struct delayed_work *dwork;

	dwork = to_delayed_work(work);
	eviction_work = container_of(dwork, struct kfd_eviction_work,
				     dwork);

	/* Process termination destroys this worker thread. So during the
	 * lifetime of this thread, kfd_process p will be valid
	 */
	p = container_of(eviction_work, struct kfd_process, eviction_work);

	/* Narrow window of overlap between restore and evict work item is
	 * possible. Once amdgpu_amdkfd_gpuvm_restore_process_bos unreserves
	 * KFD BOs, it is possible to evicted again. But restore has few more
	 * steps of finish. So lets wait for the restore work to complete
	 */
	if (delayed_work_pending(&p->restore_work))
		flush_delayed_work(&p->restore_work);

	pr_info("Started evicting process of pasid %d\n", p->pasid);
	ret = quiesce_process_mm(p);
	if (!ret) {
		dma_fence_signal(eviction_work->quiesce_fence);
		schedule_delayed_work(&p->restore_work,
					PROCESS_RESTORE_TIME_MS);
	} else
		pr_err("Failed to quiesce user queues. Cannot evict BOs\n");

	dma_fence_put(eviction_work->quiesce_fence);

	pr_info("Finished evicting process of pasid %d\n", p->pasid);

}

static int kfd_gtt_sa_init(struct kfd_dev *kfd, unsigned int buf_size,
				unsigned int chunk_size)
{
	unsigned int num_of_bits;

	kfd->gtt_sa_chunk_size = chunk_size;
	kfd->gtt_sa_num_of_chunks = buf_size / chunk_size;

	num_of_bits = kfd->gtt_sa_num_of_chunks / BITS_PER_BYTE;
	if (num_of_bits == 0) {
		pr_err("Number of bits is 0 in %s", __func__);
		return -EINVAL;
	}

	kfd->gtt_sa_bitmap = kzalloc(num_of_bits, GFP_KERNEL);

	if (!kfd->gtt_sa_bitmap)
		return -ENOMEM;

	pr_debug("gtt_sa_num_of_chunks = %d, gtt_sa_bitmap = %p\n",
			kfd->gtt_sa_num_of_chunks, kfd->gtt_sa_bitmap);

	mutex_init(&kfd->gtt_sa_lock);

	return 0;

}

static void kfd_gtt_sa_fini(struct kfd_dev *kfd)
{
	mutex_destroy(&kfd->gtt_sa_lock);
	kfree(kfd->gtt_sa_bitmap);
}

static inline uint64_t kfd_gtt_sa_calc_gpu_addr(uint64_t start_addr,
						unsigned int bit_num,
						unsigned int chunk_size)
{
	return start_addr + bit_num * chunk_size;
}

static inline uint32_t *kfd_gtt_sa_calc_cpu_addr(void *start_addr,
						unsigned int bit_num,
						unsigned int chunk_size)
{
	return (uint32_t *) ((uint64_t) start_addr + bit_num * chunk_size);
}

int kfd_gtt_sa_allocate(struct kfd_dev *kfd, unsigned int size,
			struct kfd_mem_obj **mem_obj)
{
	unsigned int found, start_search, cur_size;

	if (size == 0)
		return -EINVAL;

	if (size > kfd->gtt_sa_num_of_chunks * kfd->gtt_sa_chunk_size)
		return -ENOMEM;

	*mem_obj = kzalloc(sizeof(struct kfd_mem_obj), GFP_NOIO);
	if (!(*mem_obj))
		return -ENOMEM;

	pr_debug("Allocated mem_obj = %p for size = %d\n", *mem_obj, size);

	start_search = 0;

	mutex_lock(&kfd->gtt_sa_lock);

kfd_gtt_restart_search:
	/* Find the first chunk that is free */
	found = find_next_zero_bit(kfd->gtt_sa_bitmap,
					kfd->gtt_sa_num_of_chunks,
					start_search);

	pr_debug("Found = %d\n", found);

	/* If there wasn't any free chunk, bail out */
	if (found == kfd->gtt_sa_num_of_chunks)
		goto kfd_gtt_no_free_chunk;

	/* Update fields of mem_obj */
	(*mem_obj)->range_start = found;
	(*mem_obj)->range_end = found;
	(*mem_obj)->gpu_addr = kfd_gtt_sa_calc_gpu_addr(
					kfd->gtt_start_gpu_addr,
					found,
					kfd->gtt_sa_chunk_size);
	(*mem_obj)->cpu_ptr = kfd_gtt_sa_calc_cpu_addr(
					kfd->gtt_start_cpu_ptr,
					found,
					kfd->gtt_sa_chunk_size);

	pr_debug("gpu_addr = %p, cpu_addr = %p\n",
			(uint64_t *) (*mem_obj)->gpu_addr, (*mem_obj)->cpu_ptr);

	/* If we need only one chunk, mark it as allocated and get out */
	if (size <= kfd->gtt_sa_chunk_size) {
		pr_debug("Single bit\n");
		set_bit(found, kfd->gtt_sa_bitmap);
		goto kfd_gtt_out;
	}

	/* Otherwise, try to see if we have enough contiguous chunks */
	cur_size = size - kfd->gtt_sa_chunk_size;
	do {
		(*mem_obj)->range_end =
			find_next_zero_bit(kfd->gtt_sa_bitmap,
					kfd->gtt_sa_num_of_chunks, ++found);
		/*
		 * If next free chunk is not contiguous than we need to
		 * restart our search from the last free chunk we found (which
		 * wasn't contiguous to the previous ones
		 */
		if ((*mem_obj)->range_end != found) {
			start_search = found;
			goto kfd_gtt_restart_search;
		}

		/*
		 * If we reached end of buffer, bail out with error
		 */
		if (found == kfd->gtt_sa_num_of_chunks)
			goto kfd_gtt_no_free_chunk;

		/* Check if we don't need another chunk */
		if (cur_size <= kfd->gtt_sa_chunk_size)
			cur_size = 0;
		else
			cur_size -= kfd->gtt_sa_chunk_size;

	} while (cur_size > 0);

	pr_debug("range_start = %d, range_end = %d\n",
		(*mem_obj)->range_start, (*mem_obj)->range_end);

	/* Mark the chunks as allocated */
	for (found = (*mem_obj)->range_start;
		found <= (*mem_obj)->range_end;
		found++)
		set_bit(found, kfd->gtt_sa_bitmap);

kfd_gtt_out:
	mutex_unlock(&kfd->gtt_sa_lock);
	return 0;

kfd_gtt_no_free_chunk:
	pr_debug("Allocation failed with mem_obj = %p\n", mem_obj);
	mutex_unlock(&kfd->gtt_sa_lock);
	kfree(mem_obj);
	return -ENOMEM;
}

int kfd_gtt_sa_free(struct kfd_dev *kfd, struct kfd_mem_obj *mem_obj)
{
	unsigned int bit;

	/* Act like kfree when trying to free a NULL object */
	if (!mem_obj)
		return 0;

	pr_debug("Free mem_obj = %p, range_start = %d, range_end = %d\n",
			mem_obj, mem_obj->range_start, mem_obj->range_end);

	mutex_lock(&kfd->gtt_sa_lock);

	/* Mark the chunks as free */
	for (bit = mem_obj->range_start;
		bit <= mem_obj->range_end;
		bit++)
		clear_bit(bit, kfd->gtt_sa_bitmap);

	mutex_unlock(&kfd->gtt_sa_lock);

	kfree(mem_obj);
	return 0;
}
