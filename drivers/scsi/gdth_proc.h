#ifndef _GDTH_PROC_H
#define _GDTH_PROC_H

/* gdth_proc.h 
 * $Id: gdth_proc.h,v 1.14 2003/08/27 11:37:35 achim Exp $
 */

static int gdth_set_info(char *buffer,int length,int hanum,int busnum);
static int gdth_get_info(char *buffer,char **start,off_t offset,
                         int length,int hanum,int busnum);

#if LINUX_VERSION_CODE >= 0x020503
static void gdth_do_req(Scsi_Request *srp, gdth_cmd_str *cmd, 
                        char *cmnd, int timeout);
static int gdth_set_asc_info(char *buffer,int length,int hanum,Scsi_Request *scp);
#ifdef GDTH_IOCTL_PROC
static int gdth_set_bin_info(char *buffer,int length,int hanum,Scsi_Request *scp);
#endif
#elif LINUX_VERSION_CODE >= 0x020322
static void gdth_do_cmd(Scsi_Cmnd *scp, gdth_cmd_str *cmd, 
                        char *cmnd, int timeout);
static int gdth_set_asc_info(char *buffer,int length,int hanum,Scsi_Cmnd *scp);
#ifdef GDTH_IOCTL_PROC
static int gdth_set_bin_info(char *buffer,int length,int hanum,Scsi_Cmnd *scp);
#endif
#else 
static void gdth_do_cmd(Scsi_Cmnd *scp, gdth_cmd_str *cmd, 
                        char *cmnd, int timeout);
static int gdth_set_asc_info(char *buffer,int length,int hanum,Scsi_Cmnd scp);
#ifdef GDTH_IOCTL_PROC
static int gdth_set_bin_info(char *buffer,int length,int hanum,Scsi_Cmnd scp);
#endif
#endif

static char *gdth_ioctl_alloc(int hanum, int size, int scratch,
                              ulong32 *paddr);  
static void gdth_ioctl_free(int hanum, int size, char *buf, ulong32 paddr);
#ifdef GDTH_IOCTL_PROC
static int gdth_ioctl_check_bin(int hanum, ushort size);
#endif
static void gdth_wait_completion(int hanum, int busnum, int id);
static void gdth_stop_timeout(int hanum, int busnum, int id);
static void gdth_start_timeout(int hanum, int busnum, int id);
static int gdth_update_timeout(int hanum, Scsi_Cmnd *scp, int timeout);

void gdth_scsi_done(Scsi_Cmnd *scp);

#endif

