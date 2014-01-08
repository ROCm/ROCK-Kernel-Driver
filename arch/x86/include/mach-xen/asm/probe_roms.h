#if !defined(CONFIG_XEN_UNPRIVILEGED_GUEST)
# include_next <asm/probe_roms.h>
#elif !defined(_PROBE_ROMS_H_)
# define _PROBE_ROMS_H_
struct pci_dev;

static inline void __iomem *pci_map_biosrom(struct pci_dev *pdev) { return NULL; }
static inline void pci_unmap_biosrom(void __iomem *rom) { }
static inline size_t pci_biosrom_size(struct pci_dev *pdev) { return 0; }
#endif
