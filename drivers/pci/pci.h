/* Functions internal to the PCI core code */

extern int pci_hotplug (struct device *dev, char **envp, int num_envp,
			 char *buffer, int buffer_size);
extern void pci_create_sysfs_dev_files(struct pci_dev *pdev);
extern int pci_register_dynids(struct pci_driver *drv);
