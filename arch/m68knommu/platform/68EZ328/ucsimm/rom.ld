MEMORY 
	{
        romvec : ORIGIN = 0x10c10000, LENGTH = 0x00000400
        flash  : ORIGIN = 0x10c10400, LENGTH = 0x001fec00
        eflash : ORIGIN = 0x10d00000, LENGTH = 0
        ramvec : ORIGIN = 0x00000000, LENGTH = 1024
        ram    : ORIGIN = 0x00020000, LENGTH = 0x00800000 - 0x00020000
        eram   : ORIGIN = 0x00800000, LENGTH = 0
	}

INCLUDE arch/m68knommu/platform/68EZ328/ucsimm/fixed.ld
