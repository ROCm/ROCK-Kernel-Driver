#define QLA_MODEL_NAMES         0x21

/*
 * Adapter model names.
 */
char	*qla2x00_model_name[QLA_MODEL_NAMES] = {
	"QLA2340",	/* 0x100 */
	"QLA2342",	/* 0x101 */
	"QLA2344",	/* 0x102 */
	"QCP2342",	/* 0x103 */
	"QSB2340",	/* 0x104 */
	"QSB2342",	/* 0x105 */
	"QLA2310",	/* 0x106 */
	"QLA2332",	/* 0x107 */
	"QCP2332",	/* 0x108 */
	"QCP2340",	/* 0x109 */
	"QLA2342",	/* 0x10a */
	"QCP2342",	/* 0x10b */
	"QLA2350",	/* 0x10c */
	"QLA2352",	/* 0x10d */
	"QLA2352",	/* 0x10e */
	"HPQSVS ",	/* 0x10f */
	"HPQSVS ",	/* 0x110 */
	"QLA4010",	/* 0x111 */
	"QLA4010",	/* 0x112 */
	"QLA4010C",	/* 0x113 */
	"QLA4010C",	/* 0x114 */
	"QLA2360",	/* 0x115 */
	"QLA2362",	/* 0x116 */
	" ",		/* 0x117 */
	" ",		/* 0x118 */
	"QLA200",	/* 0x119 */
	"QLA200C"	/* 0x11A */
	"QLA200P"	/* 0x11B */
	"QLA200P"	/* 0x11C */
	"QLA4040"	/* 0x11D */
	"QLA4040"	/* 0x11E */
	"QLA4040C"	/* 0x11F */
	"QLA4040C"	/* 0x120 */
};

char	*qla2x00_model_desc[QLA_MODEL_NAMES] = {
	"133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x100 */
	"133MHz PCI-X to 2Gb FC, Dual Channel",		/* 0x101 */
	"133MHz PCI-X to 2Gb FC, Quad Channel",		/* 0x102 */
	" ",						/* 0x103 */
	" ",						/* 0x104 */
	" ",						/* 0x105 */
	" ",						/* 0x106 */
	" ",						/* 0x107 */
	" ",						/* 0x108 */
	" ",						/* 0x109 */
	" ",						/* 0x10a */
	" ",						/* 0x10b */
	"133MHz PCI-X to 2Gb FC, Single Channel",	/* 0x10c */
	"133MHz PCI-X to 2Gb FC, Dual Channel",		/* 0x10d */
	" ",						/* 0x10e */
	"HPQ SVS HBA- Initiator device",		/* 0x10f */
	"HPQ SVS HBA- Target device",			/* 0x110 */
	"Optical- 133MHz to 1Gb iSCSI- networking",	/* 0x111 */
	"Optical- 133MHz to 1Gb iSCSI- storage",	/* 0x112 */
	"Copper- 133MHz to 1Gb iSCSI- networking",	/* 0x113 */
	"Copper- 133MHz to 1Gb iSCSI- storage",		/* 0x114 */
	"133MHz PCI-X to 2Gb FC Single Channel",	/* 0x115 */
	"133MHz PCI-X to 2Gb FC Dual Channel",		/* 0x116 */
	" ",						/* 0x117 */
	" ",						/* 0x118 */
	"133MHz PCI-X to 2Gb FC Optical",		/* 0x119 */
	"133MHz PCI-X to 2Gb FC Copper"			/* 0x11A */
	"133MHz PCI-X to 2Gb FC SFP"			/* 0x11B */
	"133MHz PCI-X to 2Gb FC SFP"			/* 0x11C */
	"Optical- 133MHz to 1Gb NIC with IPSEC",	/* 0x11D */
	"Optical- 133MHz to 1Gb iSCSI with IPSEC",	/* 0x11E */
	"Copper- 133MHz to 1Gb NIC with IPSEC",		/* 0x11F */
	"Copper- 133MHz to 1Gb iSCSI with IPSEC",	/* 0x120 */
};
