
#define INITRD_MINOR 250 /* shouldn't collide with /dev/ram* too soon ... */

/* 1 = load ramdisk, 0 = don't load */
extern int rd_doload;

/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_prompt;

/* starting block # of image */
extern int rd_image_start;

/* 1 if it is not an error if initrd_start < memory_start */
extern int initrd_below_start_ok;

extern unsigned long initrd_start, initrd_end;
