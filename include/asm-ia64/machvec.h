/*
 * Machine vector for IA-64.
 *
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Srinivasa Thirumalachar <sprasad@engr.sgi.com>
 * Copyright (C) Vijay Chander <vijay@engr.sgi.com>
 * Copyright (C) 1999-2001, 2003 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#ifndef _ASM_IA64_MACHVEC_H
#define _ASM_IA64_MACHVEC_H

#include <linux/config.h>
#include <linux/types.h>

/* forward declarations: */
struct device;
struct pt_regs;
struct scatterlist;
struct irq_desc;
struct page;

typedef void ia64_mv_setup_t (char **);
typedef void ia64_mv_cpu_init_t(void);
typedef void ia64_mv_irq_init_t (void);
typedef void ia64_mv_mca_init_t (void);
typedef void ia64_mv_mca_handler_t (void);
typedef void ia64_mv_cmci_handler_t (int, void *, struct pt_regs *);
typedef void ia64_mv_log_print_t (void);
typedef void ia64_mv_send_ipi_t (int, int, int, int);
typedef void ia64_mv_global_tlb_purge_t (unsigned long, unsigned long, unsigned long);
typedef struct irq_desc *ia64_mv_irq_desc (unsigned int);
typedef u8 ia64_mv_irq_to_vector (u8);
typedef unsigned int ia64_mv_local_vector_to_irq (u8 vector);

/* DMA-mapping interface: */
typedef void ia64_mv_dma_init (void);
typedef void *ia64_mv_dma_alloc_coherent (struct device *, size_t, dma_addr_t *, int);
typedef void ia64_mv_dma_free_coherent (struct device *, size_t, void *, dma_addr_t);
typedef dma_addr_t ia64_mv_dma_map_single (struct device *, void *, size_t, int);
typedef void ia64_mv_dma_unmap_single (struct device *, dma_addr_t, size_t, int);
typedef int ia64_mv_dma_map_sg (struct device *, struct scatterlist *, int, int);
typedef void ia64_mv_dma_unmap_sg (struct device *, struct scatterlist *, int, int);
typedef void ia64_mv_dma_sync_single (struct device *, dma_addr_t, size_t, int);
typedef void ia64_mv_dma_sync_sg (struct device *, struct scatterlist *, int, int);
typedef int ia64_mv_dma_supported (struct device *, u64);

/*
 * WARNING: The legacy I/O space is _architected_.  Platforms are
 * expected to follow this architected model (see Section 10.7 in the
 * IA-64 Architecture Software Developer's Manual).  Unfortunately,
 * some broken machines do not follow that model, which is why we have
 * to make the inX/outX operations part of the machine vector.
 * Platform designers should follow the architected model whenever
 * possible.
 */
typedef unsigned int ia64_mv_inb_t (unsigned long);
typedef unsigned int ia64_mv_inw_t (unsigned long);
typedef unsigned int ia64_mv_inl_t (unsigned long);
typedef void ia64_mv_outb_t (unsigned char, unsigned long);
typedef void ia64_mv_outw_t (unsigned short, unsigned long);
typedef void ia64_mv_outl_t (unsigned int, unsigned long);
typedef unsigned char ia64_mv_readb_t (void *);
typedef unsigned short ia64_mv_readw_t (void *);
typedef unsigned int ia64_mv_readl_t (void *);
typedef unsigned long ia64_mv_readq_t (void *);

extern void machvec_noop (void);
extern void machvec_memory_fence (void);

# if defined (CONFIG_IA64_HP_SIM)
#  include <asm/machvec_hpsim.h>
# elif defined (CONFIG_IA64_DIG)
#  include <asm/machvec_dig.h>
# elif defined (CONFIG_IA64_HP_ZX1)
#  include <asm/machvec_hpzx1.h>
# elif defined (CONFIG_IA64_SGI_SN2)
#  include <asm/machvec_sn2.h>
# elif defined (CONFIG_IA64_GENERIC)

# ifdef MACHVEC_PLATFORM_HEADER
#  include MACHVEC_PLATFORM_HEADER
# else
#  define platform_name		ia64_mv.name
#  define platform_setup	ia64_mv.setup
#  define platform_cpu_init	ia64_mv.cpu_init
#  define platform_irq_init	ia64_mv.irq_init
#  define platform_mca_init	ia64_mv.mca_init
#  define platform_mca_handler	ia64_mv.mca_handler
#  define platform_cmci_handler	ia64_mv.cmci_handler
#  define platform_log_print	ia64_mv.log_print
#  define platform_send_ipi	ia64_mv.send_ipi
#  define platform_global_tlb_purge	ia64_mv.global_tlb_purge
#  define platform_dma_init		ia64_mv.dma_init
#  define platform_dma_alloc_coherent	ia64_mv.dma_alloc_coherent
#  define platform_dma_free_coherent	ia64_mv.dma_free_coherent
#  define platform_dma_map_single	ia64_mv.dma_map_single
#  define platform_dma_unmap_single	ia64_mv.dma_unmap_single
#  define platform_dma_map_sg		ia64_mv.dma_map_sg
#  define platform_dma_unmap_sg		ia64_mv.dma_unmap_sg
#  define platform_dma_sync_single	ia64_mv.dma_sync_single
#  define platform_dma_sync_sg		ia64_mv.dma_sync_sg
#  define platform_dma_supported	ia64_mv.dma_supported
#  define platform_irq_desc		ia64_mv.irq_desc
#  define platform_irq_to_vector	ia64_mv.irq_to_vector
#  define platform_local_vector_to_irq	ia64_mv.local_vector_to_irq
#  define platform_inb		ia64_mv.inb
#  define platform_inw		ia64_mv.inw
#  define platform_inl		ia64_mv.inl
#  define platform_outb		ia64_mv.outb
#  define platform_outw		ia64_mv.outw
#  define platform_outl		ia64_mv.outl
#  define platform_readb        ia64_mv.readb
#  define platform_readw        ia64_mv.readw
#  define platform_readl        ia64_mv.readl
#  define platform_readq        ia64_mv.readq
# endif

/* __attribute__((__aligned__(16))) is required to make size of the
 * structure multiple of 16 bytes.
 * This will fillup the holes created because of section 3.3.1 in
 * Software Conventions guide.
 */
struct ia64_machine_vector {
	const char *name;
	ia64_mv_setup_t *setup;
	ia64_mv_cpu_init_t *cpu_init;
	ia64_mv_irq_init_t *irq_init;
	ia64_mv_mca_init_t *mca_init;
	ia64_mv_mca_handler_t *mca_handler;
	ia64_mv_cmci_handler_t *cmci_handler;
	ia64_mv_log_print_t *log_print;
	ia64_mv_send_ipi_t *send_ipi;
	ia64_mv_global_tlb_purge_t *global_tlb_purge;
	ia64_mv_dma_init *dma_init;
	ia64_mv_dma_alloc_coherent *dma_alloc_coherent;
	ia64_mv_dma_free_coherent *dma_free_coherent;
	ia64_mv_dma_map_single *dma_map_single;
	ia64_mv_dma_unmap_single *dma_unmap_single;
	ia64_mv_dma_map_sg *dma_map_sg;
	ia64_mv_dma_unmap_sg *dma_unmap_sg;
	ia64_mv_dma_sync_single *dma_sync_single;
	ia64_mv_dma_sync_sg *dma_sync_sg;
	ia64_mv_dma_supported *dma_supported;
	ia64_mv_irq_desc *irq_desc;
	ia64_mv_irq_to_vector *irq_to_vector;
	ia64_mv_local_vector_to_irq *local_vector_to_irq;
	ia64_mv_inb_t *inb;
	ia64_mv_inw_t *inw;
	ia64_mv_inl_t *inl;
	ia64_mv_outb_t *outb;
	ia64_mv_outw_t *outw;
	ia64_mv_outl_t *outl;
	ia64_mv_readb_t *readb;
	ia64_mv_readw_t *readw;
	ia64_mv_readl_t *readl;
	ia64_mv_readq_t *readq;
} __attribute__((__aligned__(16))); /* align attrib? see above comment */

#define MACHVEC_INIT(name)			\
{						\
	#name,					\
	platform_setup,				\
	platform_cpu_init,			\
	platform_irq_init,			\
	platform_mca_init,			\
	platform_mca_handler,			\
	platform_cmci_handler,			\
	platform_log_print,			\
	platform_send_ipi,			\
	platform_global_tlb_purge,		\
	platform_dma_init,			\
	platform_dma_alloc_coherent,		\
	platform_dma_free_coherent,		\
	platform_dma_map_single,		\
	platform_dma_unmap_single,		\
	platform_dma_map_sg,			\
	platform_dma_unmap_sg,			\
	platform_dma_sync_single,		\
	platform_dma_sync_sg,			\
	platform_dma_supported,			\
	platform_irq_desc,			\
	platform_irq_to_vector,			\
	platform_local_vector_to_irq,		\
	platform_inb,				\
	platform_inw,				\
	platform_inl,				\
	platform_outb,				\
	platform_outw,				\
	platform_outl,				\
	platform_readb,				\
	platform_readw,				\
	platform_readl,				\
	platform_readq,				\
}

extern struct ia64_machine_vector ia64_mv;
extern void machvec_init (const char *name);

# else
#  error Unknown configuration.  Update asm-ia64/machvec.h.
# endif /* CONFIG_IA64_GENERIC */

/*
 * Declare default routines which aren't declared anywhere else:
 */
extern ia64_mv_dma_init			swiotlb_init;
extern ia64_mv_dma_alloc_coherent	swiotlb_alloc_coherent;
extern ia64_mv_dma_free_coherent	swiotlb_free_coherent;
extern ia64_mv_dma_map_single		swiotlb_map_single;
extern ia64_mv_dma_unmap_single		swiotlb_unmap_single;
extern ia64_mv_dma_map_sg		swiotlb_map_sg;
extern ia64_mv_dma_unmap_sg		swiotlb_unmap_sg;
extern ia64_mv_dma_sync_single		swiotlb_sync_single;
extern ia64_mv_dma_sync_sg		swiotlb_sync_sg;
extern ia64_mv_dma_supported		swiotlb_dma_supported;

/*
 * Define default versions so we can extend machvec for new platforms without having
 * to update the machvec files for all existing platforms.
 */
#ifndef platform_setup
# define platform_setup		((ia64_mv_setup_t *) machvec_noop)
#endif
#ifndef platform_cpu_init
# define platform_cpu_init	((ia64_mv_cpu_init_t *) machvec_noop)
#endif
#ifndef platform_irq_init
# define platform_irq_init	((ia64_mv_irq_init_t *) machvec_noop)
#endif
#ifndef platform_mca_init
# define platform_mca_init	((ia64_mv_mca_init_t *) machvec_noop)
#endif
#ifndef platform_mca_handler
# define platform_mca_handler	((ia64_mv_mca_handler_t *) machvec_noop)
#endif
#ifndef platform_cmci_handler
# define platform_cmci_handler	((ia64_mv_cmci_handler_t *) machvec_noop)
#endif
#ifndef platform_log_print
# define platform_log_print	((ia64_mv_log_print_t *) machvec_noop)
#endif
#ifndef platform_send_ipi
# define platform_send_ipi	ia64_send_ipi	/* default to architected version */
#endif
#ifndef platform_global_tlb_purge
# define platform_global_tlb_purge	ia64_global_tlb_purge /* default to architected version */
#endif
#ifndef platform_dma_init
# define platform_dma_init		swiotlb_init
#endif
#ifndef platform_dma_alloc_coherent
# define platform_dma_alloc_coherent	swiotlb_alloc_coherent
#endif
#ifndef platform_dma_free_coherent
# define platform_dma_free_coherent	swiotlb_free_coherent
#endif
#ifndef platform_dma_map_single
# define platform_dma_map_single	swiotlb_map_single
#endif
#ifndef platform_dma_unmap_single
# define platform_dma_unmap_single	swiotlb_unmap_single
#endif
#ifndef platform_dma_map_sg
# define platform_dma_map_sg		swiotlb_map_sg
#endif
#ifndef platform_dma_unmap_sg
# define platform_dma_unmap_sg		swiotlb_unmap_sg
#endif
#ifndef platform_dma_sync_single
# define platform_dma_sync_single	swiotlb_sync_single
#endif
#ifndef platform_dma_sync_sg
# define platform_dma_sync_sg		swiotlb_sync_sg
#endif
#ifndef platform_dma_supported
# define  platform_dma_supported	swiotlb_dma_supported
#endif
#ifndef platform_irq_desc
# define platform_irq_desc		__ia64_irq_desc
#endif
#ifndef platform_irq_to_vector
# define platform_irq_to_vector		__ia64_irq_to_vector
#endif
#ifndef platform_local_vector_to_irq
# define platform_local_vector_to_irq	__ia64_local_vector_to_irq
#endif
#ifndef platform_inb
# define platform_inb		__ia64_inb
#endif
#ifndef platform_inw
# define platform_inw		__ia64_inw
#endif
#ifndef platform_inl
# define platform_inl		__ia64_inl
#endif
#ifndef platform_outb
# define platform_outb		__ia64_outb
#endif
#ifndef platform_outw
# define platform_outw		__ia64_outw
#endif
#ifndef platform_outl
# define platform_outl		__ia64_outl
#endif
#ifndef platform_readb
# define platform_readb		__ia64_readb
#endif
#ifndef platform_readw
# define platform_readw		__ia64_readw
#endif
#ifndef platform_readl
# define platform_readl		__ia64_readl
#endif
#ifndef platform_readq
# define platform_readq		__ia64_readq
#endif

#endif /* _ASM_IA64_MACHVEC_H */
