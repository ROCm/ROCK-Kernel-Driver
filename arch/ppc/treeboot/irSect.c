/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: irSect.c
 *
 *    Description:
 *      Defines variables to hold the absolute starting address and size
 *      of the Linux kernel "image" and the initial RAM disk "initrd"
 *      sections within the boot loader.
 *
 */

#include "irSect.h"


/*
 * The order of globals below must not change. If more globals are added,
 * you must change the script 'mkirimg' accordingly.
 *
 */

/* 
 * irSectStart must be at beginning of file 
 */
unsigned int irSectStart = 0xdeadbeaf;

unsigned int imageSect_start	= 0;
unsigned int imageSect_size	= 0;
unsigned int initrdSect_start	= 0;
unsigned int initrdSect_size	= 0;

/* 
 * irSectEnd must be at end of file 
 */
unsigned int irSectEnd   = 0xdeadbeaf;
