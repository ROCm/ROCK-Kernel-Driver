extern struct bus_type pnp_bus_type;
extern spinlock_t pnp_lock;
void *pnp_alloc(long size);
int pnp_interface_attach_device(struct pnp_dev *dev);
void pnp_name_device(struct pnp_dev *dev);
void pnp_fixup_device(struct pnp_dev *dev);
void pnp_free_resources(struct pnp_resources *resources);
int __pnp_add_device(struct pnp_dev *dev);
void __pnp_remove_device(struct pnp_dev *dev);

/* resource conflict types */
#define CONFLICT_TYPE_NONE	0x0000	/* there are no conflicts, other than those in the link */
#define CONFLICT_TYPE_RESERVED	0x0001	/* the resource requested was reserved */
#define CONFLICT_TYPE_IN_USE	0x0002	/* there is a conflict because the resource is in use */
#define CONFLICT_TYPE_PCI	0x0004	/* there is a conflict with a pci device */
#define CONFLICT_TYPE_INVALID	0x0008	/* the resource requested is invalid */
#define CONFLICT_TYPE_INTERNAL	0x0010	/* resources within the device conflict with each ohter */
#define CONFLICT_TYPE_PNP_WARM	0x0020	/* there is a conflict with a pnp device that is active */
#define CONFLICT_TYPE_PNP_COLD	0x0040	/* there is a conflict with a pnp device that is disabled */

/* conflict search modes */
#define SEARCH_WARM 1	/* check for conflicts with active devices */
#define SEARCH_COLD 0	/* check for conflicts with disabled devices */

struct pnp_dev * pnp_check_port_conflicts(struct pnp_dev * dev, int idx, int mode);
int pnp_check_port(struct pnp_dev * dev, int idx);
struct pnp_dev * pnp_check_mem_conflicts(struct pnp_dev * dev, int idx, int mode);
int pnp_check_mem(struct pnp_dev * dev, int idx);
struct pnp_dev * pnp_check_irq_conflicts(struct pnp_dev * dev, int idx, int mode);
int pnp_check_irq(struct pnp_dev * dev, int idx);
struct pnp_dev * pnp_check_dma_conflicts(struct pnp_dev * dev, int idx, int mode);
int pnp_check_dma(struct pnp_dev * dev, int idx);
