#ifndef _PARISC_BOOTDATA_H
#define _PARISC_BOOTDATA_H

/* structure given from bootloader... */
typedef struct {
    unsigned 	data_valid_signature,
		initrd_start,
		initrd_end;
    char	commandline[1024];
} bootdata_t;

#define BOOTDATA_DATA_VALID_SIGNATURE 0xC0400000

#define BOOTDATA_PTR ((bootdata_t*) 0xC0400000)

#endif 
