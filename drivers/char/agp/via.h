struct agp_device_ids via_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_VIA_8501_0,
		.chipset	= VIA_MVP4,
		.chipset_name	= "MVP4",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C597_0,
		.chipset	= VIA_VP3,
		.chipset_name	= "VP3",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C598_0,
		.chipset	= VIA_MVP3,
		.chipset_name	= "MVP3",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C691,
		.chipset	= VIA_APOLLO_PRO,
		.chipset_name	= "Apollo Pro",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8371_0,
		.chipset	= VIA_APOLLO_KX133,
		.chipset_name	= "Apollo Pro KX133",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8633_0,
		.chipset	= VIA_APOLLO_PRO_266,
		.chipset_name	= "Apollo Pro 266",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8363_0,
		.chipset	= VIA_APOLLO_KT133,
		.chipset_name	= "Apollo Pro KT133",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8367_0,
		.chipset	= VIA_APOLLO_KT133,
		.chipset_name	= "Apollo Pro KT266",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8377_0,
		.chipset	= VIA_APOLLO_KT400,
		.chipset_name	= "Apollo Pro KT400",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_8653_0,
		.chipset	= VIA_APOLLO_PRO,
		.chipset_name	= "Apollo Pro266T",
	},
	{
		.device_id	= PCI_DEVICE_ID_VIA_82C694X_0,
		.chipset	= VIA_VT8605,
		.chipset_name	= "PM133"
	},
	{
		.device_id	= 0,
		.chipset	= VIA_GENERIC,
		.chipset_name	= "Generic",
	},
	{ }, /* dummy final entry, always present */
};

struct agp_bridge_info via_agp_bridge_info __initdata =
{
	.vendor_id	= PCI_VENDOR_ID_VIA,
	.vendor_name	= "Via",
	.chipset_setup	= via_generic_setup,
	.ids			= via_agp_device_ids,
};
