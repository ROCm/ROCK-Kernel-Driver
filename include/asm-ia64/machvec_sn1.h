#ifndef _ASM_IA64_MACHVEC_SN1_h
#define _ASM_IA64_MACHVEC_SN1_h

extern ia64_mv_setup_t sn1_setup;
extern ia64_mv_irq_init_t sn1_irq_init;
extern ia64_mv_map_nr_t sn1_map_nr;
extern ia64_mv_send_ipi_t sn1_send_IPI;
extern ia64_mv_pci_fixup_t sn1_pci_fixup;
extern ia64_mv_inb_t sn1_inb;
extern ia64_mv_inw_t sn1_inw;
extern ia64_mv_inl_t sn1_inl;
extern ia64_mv_outb_t sn1_outb;
extern ia64_mv_outw_t sn1_outw;
extern ia64_mv_outl_t sn1_outl;
extern ia64_mv_pci_alloc_consistent	sn1_pci_alloc_consistent;
extern ia64_mv_pci_free_consistent	sn1_pci_free_consistent;
extern ia64_mv_pci_map_single		sn1_pci_map_single;
extern ia64_mv_pci_unmap_single		sn1_pci_unmap_single;
extern ia64_mv_pci_map_sg		sn1_pci_map_sg;
extern ia64_mv_pci_unmap_sg		sn1_pci_unmap_sg;
extern ia64_mv_pci_dma_sync_single	sn1_pci_dma_sync_single;
extern ia64_mv_pci_dma_sync_sg		sn1_pci_dma_sync_sg;
extern ia64_mv_pci_dma_address		sn1_dma_address;

/*
 * This stuff has dual use!
 *
 * For a generic kernel, the macros are used to initialize the
 * platform's machvec structure.  When compiling a non-generic kernel,
 * the macros are used directly.
 */
#define platform_name		"sn1"
#define platform_setup		sn1_setup
#define platform_irq_init	sn1_irq_init
#define platform_map_nr		sn1_map_nr
#define platform_send_ipi	sn1_send_IPI
#define platform_pci_fixup	sn1_pci_fixup
#define platform_inb		sn1_inb
#define platform_inw		sn1_inw
#define platform_inl		sn1_inl
#define platform_outb		sn1_outb
#define platform_outw		sn1_outw
#define platform_outl		sn1_outl
#define platform_pci_alloc_consistent	sn1_pci_alloc_consistent
#define platform_pci_free_consistent	sn1_pci_free_consistent
#define platform_pci_map_single		sn1_pci_map_single
#define platform_pci_unmap_single	sn1_pci_unmap_single
#define platform_pci_map_sg		sn1_pci_map_sg
#define platform_pci_unmap_sg		sn1_pci_unmap_sg
#define platform_pci_dma_sync_single	sn1_pci_dma_sync_single
#define platform_pci_dma_sync_sg	sn1_pci_dma_sync_sg
#define platform_pci_dma_address	sn1_dma_address

#endif /* _ASM_IA64_MACHVEC_SN1_h */
