struct agp_device_ids hp_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_HP_ZX1_LBA,
		.chipset	= HP_ZX1,
		.chipset_name	= "ZX1",
	},
	{ }, /* dummy final entry, always present */
};

struct agp_bridge_info hp_agp_bridge_info __initdata =
{
	.vendor_id	= PCI_VENDOR_ID_HP,
	.vendor_name	= "HP",
	.chipset_setup	= hp_zx1_setup,
	.ids		= hp_agp_device_ids,
};
