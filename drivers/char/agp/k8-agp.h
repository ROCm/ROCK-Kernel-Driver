struct agp_device_ids amd_k8_device_ids[] __initdata =
{
	{ 
		.device_id	= PCI_DEVICE_ID_AMD_8151_0,
		.chipset    = AMD_8151,
		.chipset_name = "8151",
	},
	{ }, /* dummy final entry, always present */
};

struct agp_bridge_info amd_k8_agp_bridge_info[] __initdata =
{
	.vendor_id		= PCI_VENDOR_ID_AMD,
	.vendor_name	= "AMD",
	.chipset_setup	= amd_8151_setup,
	.ids			= amd_k8_device_ids,
};
