extern struct bus_type pnp_bus_type;
extern spinlock_t pnp_lock;
extern void *pnp_alloc(long size);
extern int pnp_interface_attach_device(struct pnp_dev *dev);
extern void pnp_name_device(struct pnp_dev *dev);
extern void pnp_fixup_device(struct pnp_dev *dev);
extern void pnp_free_ids(struct pnp_dev *dev);
extern void pnp_free_resources(struct pnp_resources *resources);
