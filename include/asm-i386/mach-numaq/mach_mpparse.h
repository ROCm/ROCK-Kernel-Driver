#ifndef __ASM_MACH_MPPARSE_H
#define __ASM_MACH_MPPARSE_H

static void __init smp_read_mpc_oem(struct mp_config_oemtable *oemtable,
		        unsigned short oemsize);

static inline void mpc_oem_bus_info(struct mpc_config_bus *m, char *name, 
				struct mpc_config_translation *translation)
{
	int quad = translation->trans_quad;
	int local = translation->trans_local;

	mp_bus_id_to_node[m->mpc_busid] = quad;
	mp_bus_id_to_local[m->mpc_busid] = local;
	printk("Bus #%d is %s (node %d)\n", m->mpc_busid, name, quad);
}

static inline void mpc_oem_pci_bus(struct mpc_config_bus *m, 
				struct mpc_config_translation *translation)
{
	int quad = translation->trans_quad;
	int local = translation->trans_local;

	quad_local_to_mp_bus_id[quad][local] = m->mpc_busid;
}

static inline void mps_oem_check(struct mp_config_table *mpc, char *oem, 
		char *productid)
{
	if (strncmp(oem, "IBM NUMA", 8))
		printk("Warning!  May not be a NUMA-Q system!\n");
	if (mpc->mpc_oemptr)
		smp_read_mpc_oem((struct mp_config_oemtable *) mpc->mpc_oemptr, 
				mpc->mpc_oemsize);
}

/* Hook from generic ACPI tables.c */
static inline void acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
}

#endif /* __ASM_MACH_MPPARSE_H */
