struct agp_device_ids sis_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_SI_740,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "740",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_650,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "650",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_645,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "645",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_735,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "735",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_745,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "745",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_730,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "730",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_630,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "630",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_540,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "540",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_620,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "620",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_530,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "530",
	},
	{
		.device_id	= PCI_DEVICE_ID_SI_550,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "550",
	},
	{
		.device_id	= 0,
		.chipset	= SIS_GENERIC,
		.chipset_name	= "Generic",
	},
	{ }, /* dummy final entry, always present */
};

struct agp_bridge_info sis_agp_bridge_info __initdata =
{
	.vendor_id	= PCI_VENDOR_ID_SI,
	.vendor_name	= "SiS",
	.chipset_setup	= sis_generic_setup,
	.ids			= sis_agp_device_ids,
};
