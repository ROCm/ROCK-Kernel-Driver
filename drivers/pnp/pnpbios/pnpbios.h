/*
 * pnpbios.h - contains definitions for functions used only locally.
 */

extern int pnpbios_parse_data_stream(struct pnp_dev *dev, struct pnp_bios_node * node);
extern int pnpbios_read_resources_from_node(struct pnp_resource_table *res, struct pnp_bios_node * node);
extern int pnpbios_write_resources_to_node(struct pnp_resource_table *res, struct pnp_bios_node * node);
extern void pnpid32_to_pnpid(u32 id, char *str);

extern void pnpbios_print_status(const char * module, u16 status);
extern int pnpbios_probe_installation(void);
