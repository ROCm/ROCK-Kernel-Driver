/* 
 * crbce.h
 *
 * Copyright (C) Hubertus Franke, IBM Corp. 2003
 * 
 * This files contains the type definition of the record 
 * created by the CRBCE CKRM classification engine
 * 
 * Changes
 *
 * 2003-11-11   Created					by H.Franke
 * 2003-12-01   Sanitized for Delivery                  by H.Franke
 *        
 */


#ifndef CRBCE_RECORDS_H
#define CRBCE_RECORDS_H

#include <linux/autoconf.h>           // need to see whether CKRM is configured ore not
#include <linux/ckrm.h>
#include <linux/ckrm_ce.h>

#define CRBCE_UKCC_NAME   "crbce_ukcc"     
#define CRBCE_UKCC_PATH   "/mnt/relayfs"  

#define CRBCE_UKCC_PATH_NAME   CRBCE_UKCC_PATH"/"CRBCE_UKCC_NAME   

#define CRBCE_MAX_CLASS_NAME_LEN  256

/****************************************************************
 * 
 *  CRBCE EVENT SET is and extension to the standard CKRM_EVENTS
 *
 ****************************************************************/
enum {
	
	/* we use the standard CKRM_EVENT_<..> 
	 * to identify reclassification cause actions
	 * and extend by additional ones we need
         */

	/* up event flow */
	
	CRBCE_REC_EXIT                    =  CKRM_NUM_EVENTS,
	CRBCE_REC_DATA_DELIMITER,
	CRBCE_REC_SAMPLE,
	CRBCE_REC_TASKINFO,
	CRBCE_REC_SYS_INFO,
	CRBCE_REC_CLASS_INFO,
	CRBCE_REC_KERNEL_CMD_DONE,
	CRBCE_REC_UKCC_FULL,
	
	/* down command issueance */
	CRBCE_REC_KERNEL_CMD,

	CRBCE_NUM_EVENTS
};

struct task_sample_info {
	unsigned long cpu_running;
	unsigned long cpu_waiting;
	unsigned long io_delayed;
	unsigned long memio_delayed;
};

/*********************************************
 *          KERNEL -> USER  records          *
 *********************************************/

/* we have records with either a time stamp or not */
struct crbce_hdr {
	int    type;
	pid_t  pid;
};
	
struct crbce_hdr_ts {
	int             type;
	pid_t           pid; 
	unsigned long   jiffies;   
	unsigned long   cls;
};

/* individual records */

struct crbce_rec_fork {
	struct crbce_hdr_ts hdr;
	pid_t ppid;
};

struct crbce_rec_data_delim {
	struct crbce_hdr_ts hdr;
	int is_stop;                 /* 0 start, 1 stop */
};

struct crbce_rec_task_data {
	struct crbce_hdr_ts         hdr;
	struct task_sample_info     sample;
	struct task_delay_info      delay;
};

struct crbce_ukcc_full {
	struct crbce_hdr_ts hdr;
};

struct crbce_class_info {
	struct crbce_hdr_ts hdr;
	int    action;
	int    namelen;
	char   name[CRBCE_MAX_CLASS_NAME_LEN];
};


/*********************************************
 *           USER -> KERNEL records          *
 *********************************************/

enum crbce_kernel_cmd {
	CRBCE_CMD_START,
	CRBCE_CMD_STOP,
	CRBCE_CMD_SET_TIMER,
	CRBCE_CMD_SEND_DATA,
};


struct crbce_command {
	int           type;  /* we need this for the K->U reflection */
	int           cmd;
	unsigned int  len;   /* added in the kernel for reflection */
};

#define set_cmd_hdr(rec,tok) ((rec).hdr.type=CRBCE_REC_KERNEL_CMD,(rec).hdr.cmd=(tok))

struct crbce_cmd_done {
	struct crbce_command hdr;
	int                  rc;
};

struct crbce_cmd {
	struct crbce_command hdr;
};

struct crbce_cmd_send_data{
	struct crbce_command hdr;
	int                  delta_mode;  
};
	
struct crbce_cmd_settimer {
	struct crbce_command hdr;
	unsigned long        interval;    /* in msec .. 0 means stop */
};
	
#endif
