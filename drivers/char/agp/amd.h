struct agp_device_ids amd_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_7006,
		.chipset	= AMD_IRONGATE,
		.chipset_name	= "Irongate",
	},
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_700E,
		.chipset	= AMD_761,
		.chipset_name	= "761",
	},
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_700C,
		.chipset	= AMD_762,
		.chipset_name	= "760MP",
	},
	{
		.device_id	= 0,
		.chipset	= AMD_GENERIC,
		.chipset_name	= "Generic",
	},
	{ }, /* dummy final entry, always present */
};

struct agp_bridge_info amd_agp_bridge_info __initdata = 
{
	.vendor_id	= PCI_VENDOR_ID_AMD,
	.vendor_name	= "AMD",
	.chipset_setup	= amd_irongate_setup,
	.ids		= amd_agp_device_ids,
};
