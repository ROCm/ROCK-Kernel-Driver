#include <linux/config.h>
#ifdef	CONFIG_ACPI_KERNEL_CONFIG
/*
 *  acpikcfg.h - ACPI based Kernel Configuration Manager External Interfaces
 *
 *  Copyright (C) 2000 Intel Corp.
 *  Copyright (C) 2000 J.I. Lee  <jung-ik.lee@intel.com>
 */


u32	__init acpi_cf_init (void * rsdp);
u32	__init acpi_cf_terminate (void );

u32	__init
acpi_cf_get_pci_vectors (
	struct pci_vector_struct	**vectors,
	int				*num_pci_vectors
	);


#ifdef	CONFIG_ACPI_KERNEL_CONFIG_DEBUG
void	__init
acpi_cf_print_pci_vectors (
	struct pci_vector_struct	*vectors,
	int				num_pci_vectors
	);
#endif
#endif	/* CONFIG_ACPI_KERNEL_CONFIG */
