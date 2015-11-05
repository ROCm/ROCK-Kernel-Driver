#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/mm.h>
#include <linux/amd-iommu.h>
#include "kfd_crat.h"
#include "kfd_priv.h"
#include "kfd_topology.h"

static uint32_t gpu_processor_id_low = 0x80000000; /*GPU Processor ID base*/


static void kfd_populated_cu_info_cpu(struct kfd_topology_device *dev,
		struct crat_subtype_computeunit *cu)
{
	BUG_ON(!dev);
	BUG_ON(!cu);

	dev->node_props.cpu_cores_count = cu->num_cpu_cores;
	dev->node_props.cpu_core_id_base = cu->processor_id_low;
	if (cu->hsa_capability & CRAT_CU_FLAGS_IOMMU_PRESENT)
		dev->node_props.capability |= HSA_CAP_ATS_PRESENT;

	pr_debug("CU CPU: cores=%d id_base=%d\n", cu->num_cpu_cores,
			cu->processor_id_low);
}

static void kfd_populated_cu_info_gpu(struct kfd_topology_device *dev,
		struct crat_subtype_computeunit *cu)
{
	BUG_ON(!dev);
	BUG_ON(!cu);

	dev->node_props.simd_id_base = cu->processor_id_low;
	dev->node_props.simd_count = cu->num_simd_cores;
	dev->node_props.lds_size_in_kb = cu->lds_size_in_kb;
	dev->node_props.max_waves_per_simd = cu->max_waves_simd;
	dev->node_props.wave_front_size = cu->wave_front_size;
	dev->node_props.array_count = cu->array_count;
	dev->node_props.cu_per_simd_array = cu->num_cu_per_array;
	dev->node_props.simd_per_cu = cu->num_simd_per_cu;
	dev->node_props.max_slots_scratch_cu = cu->max_slots_scatch_cu;
	if (cu->hsa_capability & CRAT_CU_FLAGS_HOT_PLUGGABLE)
		dev->node_props.capability |= HSA_CAP_HOT_PLUGGABLE;
	pr_debug("CU GPU: id_base=%d\n", cu->processor_id_low);
}

/* kfd_parse_subtype_cu - parse compute unit subtypes and attach it to correct
 *		topology device present in the device_list
 */
static int kfd_parse_subtype_cu(struct crat_subtype_computeunit *cu,
				struct list_head *device_list)
{
	struct kfd_topology_device *dev;
	int i = 0;

	BUG_ON(!cu);

	pr_debug("Found CU entry in CRAT table with proximity_domain=%d caps=%x\n",
			cu->proximity_domain, cu->hsa_capability);
	list_for_each_entry(dev, device_list, list) {
		if (cu->proximity_domain == i) {
			if (cu->flags & CRAT_CU_FLAGS_CPU_PRESENT)
				kfd_populated_cu_info_cpu(dev, cu);

			if (cu->flags & CRAT_CU_FLAGS_GPU_PRESENT)
				kfd_populated_cu_info_gpu(dev, cu);
			break;
		}
		i++;
	}

	return 0;
}

/* kfd_parse_subtype_mem - parse memory subtypes and attach it to correct
 *		topology device present in the device_list
 */
static int kfd_parse_subtype_mem(struct crat_subtype_memory *mem,
				struct list_head *device_list)
{
	struct kfd_mem_properties *props;
	struct kfd_topology_device *dev;
	int i = 0;

	BUG_ON(!mem);

	pr_debug("Found memory entry in CRAT table with proximity_domain=%d\n",
			mem->promixity_domain);
	list_for_each_entry(dev, device_list, list) {
		if (mem->promixity_domain == i) {
			props = kfd_alloc_struct(props);
			if (props == NULL)
				return -ENOMEM;

			if (dev->node_props.cpu_cores_count == 0)
				props->heap_type = HSA_MEM_HEAP_TYPE_FB_PRIVATE;
			else
				props->heap_type = HSA_MEM_HEAP_TYPE_SYSTEM;

			if (mem->flags & CRAT_MEM_FLAGS_HOT_PLUGGABLE)
				props->flags |= HSA_MEM_FLAGS_HOT_PLUGGABLE;
			if (mem->flags & CRAT_MEM_FLAGS_NON_VOLATILE)
				props->flags |= HSA_MEM_FLAGS_NON_VOLATILE;

			props->size_in_bytes =
				((uint64_t)mem->length_high << 32) +
							mem->length_low;
			props->width = mem->width;

			dev->node_props.mem_banks_count++;
			list_add_tail(&props->list, &dev->mem_props);

			break;
		}
		i++;
	}

	return 0;
}

/* kfd_parse_subtype_cache - parse cache subtypes and attach it to correct
 *		topology device present in the device_list
 */
static int kfd_parse_subtype_cache(struct crat_subtype_cache *cache,
			struct list_head *device_list)
{
	struct kfd_cache_properties *props;
	struct kfd_topology_device *dev;
	uint32_t id;

	BUG_ON(!cache);

	id = cache->processor_id_low;

	pr_debug("Found cache entry in CRAT table with processor_id=%d\n", id);
	list_for_each_entry(dev, device_list, list)
		/* CPU caches can be shared by multiple cores, so include all the caches */
		if ((id >= dev->node_props.cpu_core_id_base &&
			id <= dev->node_props.cpu_core_id_base + dev->node_props.cpu_cores_count) ||
				(id == dev->node_props.simd_id_base)) {
			props = kfd_alloc_struct(props);
			if (props == NULL)
				return -ENOMEM;

			props->processor_id_low = id;
			props->cache_level = cache->cache_level;
			props->cache_size = cache->cache_size;
			props->cacheline_size = cache->cache_line_size;
			props->cachelines_per_tag = cache->lines_per_tag;
			props->cache_assoc = cache->associativity;
			props->cache_latency = cache->cache_latency;

			if (cache->flags & CRAT_CACHE_FLAGS_DATA_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_DATA;
			if (cache->flags & CRAT_CACHE_FLAGS_INST_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_INSTRUCTION;
			if (cache->flags & CRAT_CACHE_FLAGS_CPU_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_CPU;
			if (cache->flags & CRAT_CACHE_FLAGS_SIMD_CACHE)
				props->cache_type |= HSA_CACHE_TYPE_HSACU;

			dev->cache_count++;
			dev->node_props.caches_count++;
			list_add_tail(&props->list, &dev->cache_props);

			break;
		}

	return 0;
}

/* kfd_parse_subtype_iolink - parse iolink subtypes and attach it to correct
 *		topology device present in the device_list
 */
static int kfd_parse_subtype_iolink(struct crat_subtype_iolink *iolink,
					struct list_head *device_list)
{
	struct kfd_iolink_properties *props;
	struct kfd_topology_device *dev;
	uint32_t i = 0;
	uint32_t id_from;
	uint32_t id_to;

	BUG_ON(!iolink);

	id_from = iolink->proximity_domain_from;
	id_to = iolink->proximity_domain_to;

	pr_debug("Found IO link entry in CRAT table with id_from=%d\n", id_from);
	list_for_each_entry(dev, device_list, list) {
		if (id_from == i) {
			props = kfd_alloc_struct(props);
			if (props == NULL)
				return -ENOMEM;

			props->node_from = id_from;
			props->node_to = id_to;
			props->ver_maj = iolink->version_major;
			props->ver_min = iolink->version_minor;

			/*
			 * weight factor (derived from CDIR), currently always 1
			 */
			props->weight = 1;

			props->min_latency = iolink->minimum_latency;
			props->max_latency = iolink->maximum_latency;
			props->min_bandwidth = iolink->minimum_bandwidth_mbs;
			props->max_bandwidth = iolink->maximum_bandwidth_mbs;
			props->rec_transfer_size =
					iolink->recommended_transfer_size;

			dev->io_link_count++;
			dev->node_props.io_links_count++;
			list_add_tail(&props->list, &dev->io_link_props);

			break;
		}
		i++;
	}

	return 0;
}

/* kfd_parse_subtype - parse subtypes and attach it to correct topology device
 *		present in the device_list
 *	@sub_type_hdr - subtype section of crat_image
 *	@device_list - list of topology devices present in this crat_image
 */
static int kfd_parse_subtype(struct crat_subtype_generic *sub_type_hdr,
				struct list_head *device_list)
{
	struct crat_subtype_computeunit *cu;
	struct crat_subtype_memory *mem;
	struct crat_subtype_cache *cache;
	struct crat_subtype_iolink *iolink;
	int ret = 0;

	BUG_ON(!sub_type_hdr);

	switch (sub_type_hdr->type) {
	case CRAT_SUBTYPE_COMPUTEUNIT_AFFINITY:
		cu = (struct crat_subtype_computeunit *)sub_type_hdr;
		ret = kfd_parse_subtype_cu(cu, device_list);
		break;
	case CRAT_SUBTYPE_MEMORY_AFFINITY:
		mem = (struct crat_subtype_memory *)sub_type_hdr;
		ret = kfd_parse_subtype_mem(mem, device_list);
		break;
	case CRAT_SUBTYPE_CACHE_AFFINITY:
		cache = (struct crat_subtype_cache *)sub_type_hdr;
		ret = kfd_parse_subtype_cache(cache, device_list);
		break;
	case CRAT_SUBTYPE_TLB_AFFINITY:
		/*
		 * For now, nothing to do here
		 */
		pr_debug("Found TLB entry in CRAT table (not processing)\n");
		break;
	case CRAT_SUBTYPE_CCOMPUTE_AFFINITY:
		/*
		 * For now, nothing to do here
		 */
		pr_debug("Found CCOMPUTE entry in CRAT table (not processing)\n");
		break;
	case CRAT_SUBTYPE_IOLINK_AFFINITY:
		iolink = (struct crat_subtype_iolink *)sub_type_hdr;
		ret = kfd_parse_subtype_iolink(iolink, device_list);
		break;
	default:
		pr_warn("Unknown subtype (%d) in CRAT\n",
				sub_type_hdr->type);
	}

	return ret;
}

/* kfd_parse_crat_table - parse CRAT table. For each node present in CRAT
 *		create a kfd_topology_device and add in to device_list. Also parse
 *		CRAT subtypes and attach it to appropriate kfd_topology_device
 *	@crat_image - input image containing CRAT
 *	@device_list - [OUT] list of kfd_topology_device generated after parsing
 *				   crat_image
 *	Return - 0 if successful else -ve value
 */
int kfd_parse_crat_table(void *crat_image,
						struct list_head *device_list)
{
	struct kfd_topology_device *top_dev = NULL;
	struct crat_subtype_generic *sub_type_hdr;
	uint16_t node_id;
	int ret;
	struct crat_header *crat_table = (struct crat_header *)crat_image;
	uint16_t num_nodes;
	uint32_t image_len;

	if (!crat_image)
		return -EINVAL;

	if (!list_empty(device_list)) {
		pr_warn("Error device list should be empty\n");
	}

	num_nodes = crat_table->num_domains;
	image_len = crat_table->length;

	pr_info("Parsing CRAT table with %d nodes\n", num_nodes);

	for (node_id = 0; node_id < num_nodes; node_id++) {
		top_dev = kfd_create_topology_device(device_list);
		if (!top_dev)
			break;
	}

	if (!top_dev)
		return -ENOMEM;

	memcpy(top_dev->oem_id, crat_table->oem_id, CRAT_OEMID_LENGTH);
	memcpy(top_dev->oem_table_id, crat_table->oem_table_id, CRAT_OEMTABLEID_LENGTH);
	top_dev->oem_revision = crat_table->oem_revision;

	sub_type_hdr = (struct crat_subtype_generic *)(crat_table+1);
	while ((char *)sub_type_hdr + sizeof(struct crat_subtype_generic) <
			((char *)crat_image) + image_len) {
		if (sub_type_hdr->flags & CRAT_SUBTYPE_FLAGS_ENABLED) {
			ret = kfd_parse_subtype(sub_type_hdr, device_list);
			if (ret != 0)
				return ret;
		}

		sub_type_hdr = (typeof(sub_type_hdr))((char *)sub_type_hdr +
				sub_type_hdr->length);
	}

	return 0;
}

/*
 * kfd_create_crat_image_acpi - Allocates memory for CRAT image and
 *		copies CRAT from ACPI (if available).
 *
 * NOTE: Call kfd_destroy_crat_image to free CRAT image memory
 *
 * @crat_image: CRAT read from ACPI. If no CRAT in ACPI then
 *				*crat_image will be NULL
 * @size: [OUT] size of crat_image
 *
 * Return 0 if successful else return -ve value
 */
int kfd_create_crat_image_acpi(void **crat_image, size_t *size)
{
	struct acpi_table_header *crat_table;
	acpi_status status;
	void *pcrat_image;

	if (!crat_image)
		return -EINVAL;

	*crat_image = NULL;

	/*
	 * Fetch the CRAT table from ACPI
	 */
	status = acpi_get_table(CRAT_SIGNATURE, 0, &crat_table);
	if (status == AE_NOT_FOUND) {
		pr_warn("CRAT table not found\n");
		return -ENODATA;
	} else if (ACPI_FAILURE(status)) {
		const char *err = acpi_format_exception(status);
		pr_err("CRAT table error: %s\n", err);
		return -EINVAL;
	}

	pcrat_image = kmalloc(crat_table->length, GFP_KERNEL);
	if (!pcrat_image) {
		pr_err("No memory for allocating CRAT image\n");
		return -ENOMEM;
	}

	memcpy(pcrat_image, crat_table, crat_table->length);

	*crat_image = pcrat_image;
	*size = crat_table->length;

	return 0;
}

/* kfd_create_vcrat_image_cpu - Create Virtual CRAT for CPU
 *
 *	@pcrat_image: Fill in VCRAT for CPU
 *	@size:	[IN] allocated size of crat_image.
 *			[OUT] actual size of data filled in crat_image
 */
static int kfd_create_vcrat_image_cpu(void *pcrat_image, size_t *size)
{
	struct crat_header *crat_table = (struct crat_header *)pcrat_image;
	struct acpi_table_header *acpi_table;
	acpi_status status;
	struct crat_subtype_generic *sub_type_hdr;
	struct crat_subtype_computeunit *cu;
	struct crat_subtype_memory *mem;
	struct sysinfo meminfo;
	uint64_t mem_in_bytes;
	struct cpuinfo_x86 *cpuinfo = &cpu_data(0);
	int avail_size = *size;

	/* Since there is no easy way to predict the amount of memory required,
	 * atleast 1 PAGE_SIZE of memory is expected. This should cover almost
	 * all the cases. But to be safe additional check is put in the code to
	 * ensure we don't overwrite.
	 */
	if (pcrat_image == NULL || avail_size < PAGE_SIZE)
		return -EINVAL;

	/* Fill in CRAT Header.
	 * Modify length and total_entries as subunits are added.
	 */
	avail_size -= sizeof(struct crat_header);
	if (avail_size < 0)
		return -ENOMEM;

	memset(crat_table, 0, sizeof(struct crat_header));
	memcpy(&crat_table->signature, CRAT_SIGNATURE, sizeof(crat_table->signature));
	crat_table->length = sizeof(struct crat_header);

	status = acpi_get_table("DSDT", 0, &acpi_table);
	if (status == AE_NOT_FOUND)
		pr_warn("DSDT table not found for OEM information\n");
	else {
		crat_table->oem_revision = acpi_table->revision;
		memcpy(crat_table->oem_id, acpi_table->oem_id, CRAT_OEMID_LENGTH);
		memcpy(crat_table->oem_table_id, acpi_table->oem_table_id, CRAT_OEMTABLEID_LENGTH);
	}
	crat_table->total_entries = 0;
	crat_table->num_domains = 1;

	/* Fill in Subtype: Compute Unit
	 * First fill in the sub type header and then sub type data
	 */
	avail_size -= sizeof(struct crat_subtype_computeunit);
	if (avail_size < 0)
		return -ENOMEM;

	sub_type_hdr = (struct crat_subtype_generic *)(crat_table+1);
	memset(sub_type_hdr, 0, sizeof(struct crat_subtype_computeunit));

	sub_type_hdr->type = CRAT_SUBTYPE_COMPUTEUNIT_AFFINITY;
	sub_type_hdr->length = sizeof(struct crat_subtype_computeunit);
	sub_type_hdr->flags = CRAT_SUBTYPE_FLAGS_ENABLED;

	/* Fill in CU data */
	cu = (struct crat_subtype_computeunit *)sub_type_hdr;
	cu->flags |= CRAT_CU_FLAGS_CPU_PRESENT;
	cu->proximity_domain = 0;
	cu->processor_id_low = cpuinfo->apicid;
	cu->num_cpu_cores = num_online_cpus();
	crat_table->length += sub_type_hdr->length;
	crat_table->total_entries++;

	/* Fill in Subtype: Memory
	 * First fill in the sub type header and then sub type data
	 */
	avail_size -= sizeof(struct crat_subtype_memory);
	if (avail_size < 0)
		return -ENOMEM;

	sub_type_hdr = (typeof(sub_type_hdr))((char *)sub_type_hdr +
						sub_type_hdr->length);
	memset(sub_type_hdr, 0, sizeof(struct crat_subtype_memory));

	sub_type_hdr->type = CRAT_SUBTYPE_MEMORY_AFFINITY;
	sub_type_hdr->length = sizeof(struct crat_subtype_memory);
	sub_type_hdr->flags = CRAT_SUBTYPE_FLAGS_ENABLED;

	/* Fill in Memory Subunit data */
	mem = (struct crat_subtype_memory *)sub_type_hdr;
	si_meminfo(&meminfo);

	mem_in_bytes = (meminfo.totalram * meminfo.mem_unit);
	mem->length_low = lower_32_bits(mem_in_bytes);
	mem->length_high = upper_32_bits(mem_in_bytes);
	mem->promixity_domain = 0;
	crat_table->length += sub_type_hdr->length;
	crat_table->total_entries++;

	/* TODO: Add cache Subtype for CPU.
	 * Currently, CPU cache information is available in function
	 * detect_cache_attributes(cpu) defined in the file
	 * ./arch/x86/kernel/cpu/intel_cacheinfo.c. This function is not exported
	 * and to get the same information the code needs to be duplicated.
	 */

	*size = crat_table->length;
	pr_info("Virtual CRAT table created for CPU\n");

	return 0;
}

/* kfd_create_vcrat_image_gpu - Create Virtual CRAT for CPU
 *
 *	@pcrat_image: Fill in VCRAT for GPU
 *	@size:	[IN] allocated size of crat_image.
 *		[OUT] actual size of data filled in crat_image
 */
static int kfd_create_vcrat_image_gpu(void *pcrat_image,
			size_t *size, struct kfd_dev *kdev)
{
	struct crat_header *crat_table = (struct crat_header *)pcrat_image;
	struct crat_subtype_generic *sub_type_hdr;
	struct crat_subtype_computeunit *cu;
	struct crat_subtype_memory *mem;
	struct kfd_cu_info cu_info;
	struct kfd_local_mem_info local_mem_info;
	struct amd_iommu_device_info iommu_info;
	int avail_size = *size;
	const u32 required_iommu_flags = AMD_IOMMU_DEVICE_FLAG_ATS_SUP |
								AMD_IOMMU_DEVICE_FLAG_PRI_SUP |
								AMD_IOMMU_DEVICE_FLAG_PASID_SUP;

	/* Since there is no easy way to predict the amount of memory required,
	 * at least 1 PAGE_SIZE of memory is expected. This should cover almost
	 * all the cases. But to be safe additional check is put in the code to
	 * ensure we don't overwrite.
	 */
	if (pcrat_image == NULL || avail_size < PAGE_SIZE)
		return -EINVAL;

	/* Fill the CRAT Header.
	 * Modify length and total_entries as subunits are added.
	 */
	avail_size -= sizeof(struct crat_header);
	if (avail_size < 0)
		return -ENOMEM;

	memset(crat_table, 0, sizeof(struct crat_header));

	memcpy(&crat_table->signature, CRAT_SIGNATURE, sizeof(crat_table->signature));
	crat_table->length = sizeof(struct crat_header); /* Change length as we add more subtypes*/
	crat_table->num_domains = 1;
	crat_table->total_entries = 0;

	/* Fill in Subtype: Compute Unit
	 * First fill in the sub type header and then sub type data
	 */
	avail_size -= sizeof(struct crat_subtype_computeunit);
	if (avail_size < 0)
		return -ENOMEM;

	sub_type_hdr = (struct crat_subtype_generic *)(crat_table + 1);
	memset(sub_type_hdr, 0, sizeof(struct crat_subtype_computeunit));

	sub_type_hdr->type = CRAT_SUBTYPE_COMPUTEUNIT_AFFINITY;
	sub_type_hdr->length = sizeof(struct crat_subtype_computeunit);
	sub_type_hdr->flags = CRAT_SUBTYPE_FLAGS_ENABLED;

	/* Fill CU subtype data */
	cu = (struct crat_subtype_computeunit *)sub_type_hdr;
	cu->flags |= CRAT_CU_FLAGS_GPU_PRESENT;
	cu->proximity_domain = 0;
	cu->processor_id_low = gpu_processor_id_low++;

	kdev->kfd2kgd->get_cu_info(kdev->kgd, &cu_info);
	cu->num_simd_per_cu = cu_info.simd_per_cu;
	cu->num_simd_cores = cu_info.simd_per_cu * cu_info.cu_active_number;
	cu->max_waves_simd = cu_info.max_waves_per_simd;

	cu->wave_front_size = cu_info.wave_front_size;
	cu->array_count = cu_info.num_shader_arrays_per_engine *
		cu_info.num_shader_engines;
	cu->num_cu_per_array = cu_info.num_cu_per_sh;
	cu->max_slots_scatch_cu = cu_info.max_scratch_slots_per_cu;
	cu->num_banks = cu_info.num_shader_engines;
	cu->lds_size_in_kb = cu_info.lds_size;

	cu->hsa_capability = 0;

	/* Check if this node supports IOMMU. During parsing this flag will
	 * translate to HSA_CAP_ATS_PRESENT */
	iommu_info.flags = 0;
	if (0 == amd_iommu_device_info(kdev->pdev, &iommu_info)) {
		if ((iommu_info.flags & required_iommu_flags) == required_iommu_flags)
			cu->hsa_capability |= CRAT_CU_FLAGS_IOMMU_PRESENT;
	}

	crat_table->length += sub_type_hdr->length;
	crat_table->total_entries++;

	/* Fill in Subtype: Memory
	 * First fill in the sub type header and then sub type data
	 */
	avail_size -= sizeof(struct crat_subtype_memory);
	if (avail_size < 0)
		return -ENOMEM;

	sub_type_hdr = (typeof(sub_type_hdr))((char *)sub_type_hdr +
		sub_type_hdr->length);
	memset(sub_type_hdr, 0, sizeof(struct crat_subtype_memory));

	sub_type_hdr->type = CRAT_SUBTYPE_MEMORY_AFFINITY;
	sub_type_hdr->length = sizeof(struct crat_subtype_memory);
	sub_type_hdr->flags = CRAT_SUBTYPE_FLAGS_ENABLED;

	/* Fill Memory subtype data */
	mem = (struct crat_subtype_memory *)sub_type_hdr;
	mem->promixity_domain = 0;
	kdev->kfd2kgd->get_local_mem_info(kdev->kgd, &local_mem_info);

	mem->length_low = lower_32_bits(local_mem_info.local_mem_size);
	mem->length_high = upper_32_bits(local_mem_info.local_mem_size);
	mem->width = local_mem_info.vram_width;

	crat_table->length += sub_type_hdr->length;
	crat_table->total_entries++;

	/* TODO: Fill in cache information. This information is NOT readily
	 * available in KGD */

	*size = crat_table->length;
	pr_info("Virtual CRAT table created for GPU\n");

	return 0;
}

/* kfd_create_crat_image_virtual - Allocates memory for CRAT image and
 *		creates a Virtual CRAT (VCRAT) image
 *
 * NOTE: Call kfd_destroy_crat_image to free CRAT image memory
 *
 *	@crat_image: VCRAT image created because ACPI does not have a
 *		CRAT for this device
 *	@size: [OUT] size of virtual crat_image
 *	@flags:	COMPUTE_UNIT_CPU - Create VCRAT for CPU device
 *			COMPUTE_UNIT_GPU - Create VCRAT for GPU
 *			(COMPUTE_UNIT_CPU | COMPUTE_UNIT_GPU) - Create VCRAT for APU
 *				-- this option is not currently implemented. The assumption
 *				   is that all AMD APUs will have CRAT
 *	@kdev: Valid kfd_device required if flags contain COMPUTE_UNIT_GPU
 *
 * Return 0 if successful else return -ve value
*/
int kfd_create_crat_image_virtual(void **crat_image, size_t *size,
				int flags, struct kfd_dev *kdev)
{
	void *pcrat_image;
	int ret = 0;

	if (!crat_image)
		return -EINVAL;

	*crat_image = NULL;

	/* Allocate one PAGE_SIZE for virtual crat image. This should cover
	 * all the current conditions. A check is put not to overwrite beyond
	 * one PAGE_SIZE
	*/
	pcrat_image = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!pcrat_image) {
		pr_err("No memory for allocating CRAT image\n");
		return -ENOMEM;
	}
	*size = PAGE_SIZE;

	switch (flags) {
	case COMPUTE_UNIT_CPU:
		ret = kfd_create_vcrat_image_cpu(pcrat_image, size);
		break;
	case COMPUTE_UNIT_GPU:
		if (!kdev)
			return -EINVAL;
		ret = kfd_create_vcrat_image_gpu(pcrat_image, size, kdev);
		break;
	case (COMPUTE_UNIT_CPU | COMPUTE_UNIT_GPU) :
		/*TODO:*/
		ret = -EINVAL;
		pr_err("VCRAT not implemented for APU\n");
		break;
	default:
		ret = -EINVAL;
	}

	if (ret == 0)
		*crat_image = pcrat_image;

	return ret;
}


/* kfd_destroy_crat_image
 *
 * @crat_image: [IN] - crat_image from kfd_create_crat_image_xxx(..)
 *
 */
void kfd_destroy_crat_image(void *crat_image)
{
	if (crat_image)
		kfree(crat_image);
	return;
}
