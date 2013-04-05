int machine_kexec_setup_resource(struct resource *hypervisor,
				 struct resource *phys_cpu)
{
	/* The per-cpu crash note resources belong to the hypervisor resource */
	insert_resource(hypervisor, phys_cpu);
	if (!phys_cpu->parent) /* outside of hypervisor range */
		insert_resource(&iomem_resource, phys_cpu);

	return 0;
}

int __init machine_kexec_setup_resources(struct resource *hypervisor,
					 struct resource *phys_cpus,
					 int nr_phys_cpus)
{
	unsigned int k;

	insert_resource(&iomem_resource, hypervisor);
	if (crashk_res.end > crashk_res.start)
		insert_resource(&iomem_resource, &crashk_res);

	for (k = 0; k < nr_phys_cpus; k++)
		machine_kexec_setup_resource(hypervisor, phys_cpus + k);

	return xen_create_contiguous_region((unsigned long)&vmcoreinfo_note,
					    get_order(sizeof(vmcoreinfo_note)),
					    BITS_PER_LONG);

}
