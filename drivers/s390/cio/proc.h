#ifndef S390_PROC_H
#define S390_PROC_H

extern int cio_procfs_device_purge (void);
extern int cio_procfs_device_create (int devno);
extern int cio_procfs_device_remove (int devno);

//FIXME: shouldn`t this be 'struct{unsigned int len; char data[0]};' ?
/*
 * Display info on subchannels in /proc/subchannels.
 * Adapted from procfs stuff in dasd.c by Cornelia Huck, 02/28/01.
 */
typedef struct {
        char *data;
        int len;
} tempinfo_t;


#endif
