MEMORY 
	{
        romvec : ORIGIN = 0x00600000, LENGTH = 0x00000400
        flash  : ORIGIN = 0x00600400, LENGTH = 0x00200000 - 0x00010400
        eflash : ORIGIN = 0x007f0000, LENGTH = 0
        ramvec : ORIGIN = 0x00000000, LENGTH = 0x00000400
        ram    : ORIGIN = 0x00020000, LENGTH = 0x00600000 - 0x00020000
        eram   : ORIGIN = 0x00600000, LENGTH = 0
	}

INCLUDE arch/m68knommu/platform/68EZ328/ucsimm/fixed.ld
