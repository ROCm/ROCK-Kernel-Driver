/*
 *  include/asm-s390/setup.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 */

#ifndef _ASM_S390_SETUP_H
#define _ASM_S390_SETUP_H

#define PARMAREA 0x10400

#ifndef __ASSEMBLER__

#define ORIG_ROOT_DEV     (*(unsigned long *) (0x10400))
#define MOUNT_ROOT_RDONLY (*(unsigned short *) (0x10404))
#define MEMORY_SIZE       (*(unsigned long *)  (0x10406))
#define MACHINE_FLAGS     (*(unsigned long *)  (0x1040a))
#define INITRD_START      (*(unsigned long *)  (0x1040e))
#define INITRD_SIZE       (*(unsigned long *)  (0x10412))
#define RAMDISK_FLAGS     (*(unsigned short *) (0x10416))
#define COMMAND_LINE      ((char *)            (0x10480))

#else 

#define ORIG_ROOT_DEV     0x10400
#define MOUNT_ROOT_RDONLY 0x10404
#define MEMORY_SIZE       0x10406
#define MACHINE_FLAGS     0x1040a
#define INITRD_START      0x1040e
#define INITRD_SIZE       0x10412
#define RAMDISK_FLAGS     0x10416
#define COMMAND_LINE      0x10480

#endif

#define COMMAND_LINE_SIZE 896
/*
 * Machine features detected in head.S
 */
#define MACHINE_IS_VM    (MACHINE_FLAGS & 1)
#define MACHINE_HAS_IEEE (MACHINE_FLAGS & 2)
#define MACHINE_IS_P390  (MACHINE_FLAGS & 4)

#define RAMDISK_ORIGIN            0x800000
#define RAMDISK_BLKSIZE           0x1000
#define RAMDISK_IMAGE_START_MASK  0x07FF
#define RAMDISK_PROMPT_FLAG       0x8000
#define RAMDISK_LOAD_FLAG         0x4000


#endif
