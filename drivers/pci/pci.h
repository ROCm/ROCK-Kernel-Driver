/* Functions internal to the PCI core code */

extern int pci_hotplug (struct device *dev, char **envp, int num_envp,
			 char *buffer, int buffer_size);
extern void pci_create_sysfs_dev_files(struct pci_dev *pdev);
extern int pci_bus_alloc_resource(struct pci_bus *bus, struct resource *res,
				  unsigned long size, unsigned long align,
				  unsigned long min, unsigned int type_mask,
				  void (*alignf)(void *, struct resource *,
					  	 unsigned long, unsigned long),
				  void *alignf_data);
extern int pci_proc_attach_device(struct pci_dev *dev);
extern int pci_proc_detach_device(struct pci_dev *dev);
extern int pci_proc_attach_bus(struct pci_bus *bus);
extern int pci_proc_detach_bus(struct pci_bus *bus);

/* Functions for PCI Hotplug drivers to use */
extern struct pci_bus * pci_add_new_bus(struct pci_bus *parent, struct pci_dev *dev, int busnr);
extern unsigned int pci_do_scan_bus(struct pci_bus *bus);
extern void pci_remove_bus_device(struct pci_dev *dev);
extern int pci_remove_device_safe(struct pci_dev *dev);

struct pci_dev_wrapped {
	struct pci_dev	*dev;
	void		*data;
};

struct pci_bus_wrapped {
	struct pci_bus	*bus;
	void		*data;
};

struct pci_visit {
	int (* pre_visit_pci_bus)	(struct pci_bus_wrapped *,
					 struct pci_dev_wrapped *);
	int (* post_visit_pci_bus)	(struct pci_bus_wrapped *,
					 struct pci_dev_wrapped *);

	int (* pre_visit_pci_dev)	(struct pci_dev_wrapped *,
					 struct pci_bus_wrapped *);
	int (* visit_pci_dev)		(struct pci_dev_wrapped *,
					 struct pci_bus_wrapped *);
	int (* post_visit_pci_dev)	(struct pci_dev_wrapped *,
					 struct pci_bus_wrapped *);
};

extern int pci_visit_dev(struct pci_visit *fn,
			 struct pci_dev_wrapped *wrapped_dev,
			 struct pci_bus_wrapped *wrapped_parent);
