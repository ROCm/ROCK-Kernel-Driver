struct agp_device_ids intel_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82443LX_0,
		.chipset	= INTEL_LX,
		.chipset_name	= "440LX",
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82443BX_0,
		.chipset	= INTEL_BX,
		.chipset_name	= "440BX",
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82443GX_0,
		.chipset	= INTEL_GX,
		.chipset_name	= "440GX",
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82815_MC,
		.chipset	= INTEL_I815,
		.chipset_name	= "i815",
		.chipset_setup	= intel_815_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82820_HB,
		.chipset	= INTEL_I820,
		.chipset_name	= "i820",
		.chipset_setup	= intel_820_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82820_UP_HB,
		.chipset	= INTEL_I820,
		.chipset_name	= "i820",
		.chipset_setup	= intel_820_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82830_HB,
		.chipset	= INTEL_I830_M,
		.chipset_name	= "i830M",
		.chipset_setup	= intel_830mp_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82845G_HB,
		.chipset	= INTEL_I845_G,
		.chipset_name	= "i845G",
		.chipset_setup	= intel_830mp_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82840_HB,
		.chipset	= INTEL_I840,
		.chipset_name	= "i840",
		.chipset_setup	= intel_840_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82845_HB,
		.chipset	= INTEL_I845,
		.chipset_name	= "i845",
		.chipset_setup	= intel_845_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82850_HB,
		.chipset	= INTEL_I850,
		.chipset_name	= "i850",
		.chipset_setup	= intel_850_setup
	},
	{
		.device_id	= PCI_DEVICE_ID_INTEL_82860_HB,
		.chipset	= INTEL_I860,
		.chipset_name	= "i860",
		.chipset_setup	= intel_860_setup
	},
	{
		.device_id	= 0,
		.chipset	= INTEL_GENERIC,
		.chipset_name	= "Generic",
	},
	{ }, /* dummy final entry, always present */
};

struct agp_bridge_info intel_agp_bridge_info __initdata =
{
	.vendor_id	= PCI_VENDOR_ID_INTEL,
	.vendor_name	= "Intel",
	.chipset_setup	= intel_generic_setup,
	.ids			= intel_agp_device_ids,			
};
