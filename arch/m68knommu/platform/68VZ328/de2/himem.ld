MEMORY 
	{
        romvec : ORIGIN = 0x01e00000, LENGTH = 1k
        flash  : ORIGIN = 0x01e00400, LENGTH = 2M - 1k
        eflash : ORIGIN = 0x02000000, LENGTH = 0
        ramvec : ORIGIN = 0x00000000, LENGTH = 1k
        ram    : ORIGIN = 0x00010000, LENGTH = 30M - 64k
        eram   : ORIGIN = 0x01e00000, LENGTH = 0
	}

INCLUDE arch/m68knommu/platform/68VZ328/de2/fixed.ld
