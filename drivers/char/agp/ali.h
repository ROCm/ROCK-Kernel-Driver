struct agp_device_ids ali_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_AL_M1541,
		.chipset	= ALI_M1541,
		.chipset_name	= "M1541",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1621,
		.chipset	= ALI_M1621,
		.chipset_name	= "M1621",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1631,
		.chipset	= ALI_M1631,
		.chipset_name	= "M1631",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1632,
		.chipset	= ALI_M1632,
		.chipset_name	= "M1632",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1641,
		.chipset	= ALI_M1641,
		.chipset_name	= "M1641",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1644,
		.chipset	= ALI_M1644,
		.chipset_name	= "M1644",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1647,
		.chipset	= ALI_M1647,
		.chipset_name	= "M1647",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1651,
		.chipset	= ALI_M1651,
		.chipset_name	= "M1651",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1671,
		.chipset	= ALI_M1671,
		.chipset_name	= "M1671",
	},
	{
		.device_id	= 0,
		.chipset	= ALI_GENERIC,
		.chipset_name	= "Generic",
	},

	{ }, /* dummy final entry, always present */
};

struct agp_bridge_info ali_agp_bridge_info __initdata =
{
	.vendor_id	= PCI_VENDOR_ID_AL,
	.vendor_name	= "Ali",
	.chipset_setup	= ali_generic_setup,
	.ids			= ali_agp_device_ids,
};

