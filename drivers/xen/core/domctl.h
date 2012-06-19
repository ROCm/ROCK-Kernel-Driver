int xen_guest_address_size(int domid);
int xen_guest_blkif_protocol(int domid);
int xen_set_physical_cpu_affinity(int pcpu);
int xen_get_topology_info(unsigned int cpu, u32 *core, u32 *socket, u32 *node);
