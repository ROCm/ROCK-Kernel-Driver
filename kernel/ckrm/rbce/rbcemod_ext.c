/* Data Collection Extension to Rule-based Classification Engine (RBCE) module
 *
 * Copyright (C) Hubertus Franke, IBM Corp. 2003
 *
 * Extension to be included into RBCE to collect delay and sample information
 * requires user daemon <crbcedmn> to activate.
 *
 * Latest version, more details at http://ckrm.sf.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */


/*************************************************************************************
 *
 *   UKCC Protocol and communication handling
 *
 *************************************************************************************/

#include <linux/relayfs_fs.h>


#define PSAMPLE(pdata)    (&((pdata)->ext_data.sample))
#define UKCC_N_SUB_BUFFERS     (4)
#define UKCC_SUB_BUFFER_SIZE   (1<<15)
#define UKCC_TOTAL_BUFFER_SIZE (UKCC_N_SUB_BUFFERS * UKCC_SUB_BUFFER_SIZE)

#define CHANNEL_AUTO_CONT  0  /* this is during debugging only. It allows the module
			       * to continue sending data the UKCC if space frees up
			       * vs. going into the recovery driven mode
			       */

enum ukcc_state { 
	UKCC_OK      = 0,
	UKCC_STANDBY = 1,
	UKCC_FULL    = 2
};

int ukcc_channel = -1;
static enum ukcc_state chan_state = UKCC_STANDBY;

inline static int ukcc_ok(void) { return (chan_state == UKCC_OK); }


static void ukcc_cmd_deliver(int rchan_id, char *from, u32 len);
static void client_attached(void); 
static void client_detached(void); 

static int ukcc_fileop_notify (int rchan_id,
			       struct file *filp,
			       enum relay_fileop fileop)
{
	static int readers = 0;
        if (fileop == RELAY_FILE_OPEN) {
		// printk("got fileop_notify RELAY_FILE_OPEN for file %p\n", filp);
                if (readers) {
                        printk("only one client allowed, backoff .... \n");
                        return -EPERM;
                }
		if (!try_module_get(THIS_MODULE)) 
			return -EPERM;
                readers++;
		client_attached(); 

        } else if (fileop == RELAY_FILE_CLOSE) {
                // printk("got fileop_notify RELAY_FILE_CLOSE for file %p\n", filp);
		client_detached();
                readers--;
		module_put(THIS_MODULE);
        }
        return 0;
}

static int create_ukcc_channel(void)
{
	static struct rchan_callbacks ukcc_callbacks = {
		.buffer_start  = NULL,
		.buffer_end    = NULL,
		.deliver       = NULL,
		.user_deliver  = ukcc_cmd_deliver,
		.needs_resize  = NULL,
		.fileop_notify = ukcc_fileop_notify,
	};


        u32 channel_flags = RELAY_USAGE_GLOBAL | RELAY_SCHEME_ANY | RELAY_TIMESTAMP_ANY;
	
	// notify on subbuffer full (through poll)
	channel_flags     |= RELAY_DELIVERY_BULK;
	// channel_flags     |= RELAY_DELIVERY_PACKET;
	// avoid overwrite, otherwise recovery will be nasty...
	channel_flags     |= RELAY_MODE_NO_OVERWRITE;

        ukcc_channel = relay_open(CRBCE_UKCC_NAME,
				  UKCC_SUB_BUFFER_SIZE,
				  UKCC_N_SUB_BUFFERS,
                                  channel_flags,
                                  &ukcc_callbacks,
                                  0,
                                  0,
                                  0,
                                  0,
				  0,
				  0,
				  NULL,
                                  0);
        if (ukcc_channel < 0)
                printk("crbce: ukcc creation failed, errcode: %d\n",ukcc_channel);
        else
                printk("crbce: ukcc created (%u KB)\n", UKCC_TOTAL_BUFFER_SIZE>>10);
        return ukcc_channel;
}

static inline void close_ukcc_channel(void) 
{ 
	if (ukcc_channel >= 0) {
		relay_close(ukcc_channel);
		ukcc_channel = -1;
		chan_state = UKCC_STANDBY;
	}
}



#define rec_set_hdr(r,t,p)      ((r)->hdr.type = (t), (r)->hdr.pid = (p))
#define rec_set_timehdr(r,t,p,c)  (rec_set_hdr(r,t,p), (r)->hdr.jiffies = jiffies, (r)->hdr.cls=(unsigned long)(c) )


#if CHANNEL_AUTO_CONT

/* we only provide this for debugging.. it allows us to send records
 * based on availability in the channel when the UKCC stalles rather
 * going through the UKCC recovery protocol
 */

#define rec_send_len(r,l)								\
	do {										\
		int chan_wasok = (chan_state == UKCC_OK);			        \
                int chan_isok  = (relay_write(ukcc_channel,(r),(l),-1,NULL) > 0);	\
		chan_state = chan_isok ? UKCC_OK : UKCC_STANDBY;				\
		if      (chan_wasok && !chan_isok) { printk("Channel stalled\n"); }	\
		else if (!chan_wasok && chan_isok) { printk("Channel continues\n"); }	\
	} while (0)

#define rec_send(r)	rec_send_len(r,sizeof(*(r)))

#else

/* Default UKCC channel protocol. 
 * Though a UKCC buffer overflow should not happen ever, it is possible iff the user daemon stops reading for 
 * some reason. Hence we provide a simple protocol based on 3 states 
 * There are three states
 *     UKCC_OK      := channel is active and properly working. When a channel write
 *                     fails we move to state CHAN_FULL.
 *     UKCC_FULL    := channel is active, but the last send_rec has failed. As
 *                     a result we will try to send an indication to the daemon
 *                     that this has happened. When that succeeds, we move to state
 *                     UKCC_STANDBY.
 *     UKCC_STANDBY := we are waiting to be restarted by the user daemon 
 *
 */

static void ukcc_full(void)
{
	static spinlock_t ukcc_state_lock = SPIN_LOCK_UNLOCKED;
	/* protect transition from OK -> FULL to ensure only one record is send, 
	 * rest we do not need to protect, protocol implies that. we keep the channel
	 * OK until 
	 */
	int send = 0;
	spin_lock(&ukcc_state_lock);
	if ((send = (chan_state != UKCC_STANDBY)))
		chan_state = UKCC_STANDBY;     /* assume we can send */
	spin_unlock(&ukcc_state_lock);
	    
	if (send) {
		struct crbce_ukcc_full rec;
		rec_set_timehdr(&rec,CRBCE_REC_UKCC_FULL,0,0);
		if (relay_write(ukcc_channel,&rec,sizeof(rec),-1,NULL) <= 0) {
			/* channel is remains full .. try with next one */
			chan_state = UKCC_FULL;
		}
	}
}

#define rec_send_len(r,l)							\
	do {									\
 	        switch (chan_state) {						\
		case UKCC_OK:							\
			if (relay_write(ukcc_channel,(r),(l),-1,NULL) > 0)	\
				break;						\
		case UKCC_FULL:							\
			ukcc_full();						\
			break;							\
		default:							\
			break;							\
		}								\
	} while (0)

#define rec_send(r)	rec_send_len(r,sizeof(*(r)))

#endif

/********************************************************************************************
 *
 *  Callbacks for the CKRM engine. 
 *    In each we do the necessary classification and event record generation
 *    We generate 3 kind of records in the callback 
 *    (a) FORK              send the pid, the class and the ppid
 *    (b) RECLASSIFICATION  send the pid, the class and < sample data + delay data >
 *    (b) EXIT              send the pid
 *
 ********************************************************************************************/

int delta_mode = 0;

static inline void copy_delay(struct task_delay_info *delay, struct task_struct *tsk)
{
	*delay = tsk->delays;
}

static inline void 
zero_delay(struct task_delay_info *delay)
{
	memset(delay,0,sizeof(struct task_delay_info));           /* we need to think about doing this 64-bit atomic */
}

static inline void 
zero_sample(struct task_sample_info *sample)
{
	memset(sample,0,sizeof(struct task_sample_info));         /* we need to think about doing this 64-bit atomic */
}

static inline int check_zero(void *ptr, int len)
{
	int iszero = 1;
	int i;
	unsigned long *uptr = (unsigned long*)ptr;

	for ( i = len / sizeof(unsigned long);  i-- && iszero ; uptr++) // assume its rounded 
		iszero &= (*uptr==0);
	return iszero;
}

static inline int check_not_zero(void *ptr, int len)
{
	int i;
	unsigned long *uptr = (unsigned long*)ptr;

	for ( i = len / sizeof(unsigned long);  i-- ; uptr++) // assume its rounded 
		if (*uptr) return 1;
	return 0;
}

static inline int sample_changed(struct task_sample_info* s) { return check_not_zero(s,sizeof(*s)); }
static inline int delay_changed (struct task_delay_info* d)  { return check_not_zero(d,sizeof(*d)); }

static inline int
send_task_record(struct task_struct *tsk, int event,struct ckrm_core_class *core, int send_forced)
{
	struct crbce_rec_task_data rec;
	struct rbce_private_data *pdata;
	int send = 0;

	if (!ukcc_ok()) 
		return 0;
 	pdata = RBCE_DATA(tsk);
	if (pdata == NULL) {
		// printk("send [%d]<%s>: no pdata\n",tsk->pid,tsk->comm);
		return 0;
	}
	if (send_forced || (delta_mode==0) || sample_changed(PSAMPLE(RBCE_DATA(tsk))) || delay_changed(&tsk->delays)) {
		rec_set_timehdr(&rec,event,tsk->pid,core ? core : (struct ckrm_core_class *)tsk->taskclass);
		rec.sample = *PSAMPLE(RBCE_DATA(tsk));
		copy_delay(&rec.delay,tsk);
		rec_send(&rec);
		if (delta_mode || send_forced) {
			// on reclassify or delta mode reset the counters  
			zero_sample(PSAMPLE(RBCE_DATA(tsk)));
			zero_delay(&tsk->delays);
		}
		send = 1;
	}
	return send;
}

static inline void
send_exit_notification(struct task_struct *tsk) 
{ 
	send_task_record(tsk,CRBCE_REC_EXIT,NULL,1);
}

static inline void
rbce_tc_ext_notify(int event, void *core, struct task_struct *tsk)
{
	struct crbce_rec_fork   rec;

	switch (event) {
	case CKRM_EVENT_FORK:
		if (ukcc_ok()) {
			rec.ppid = tsk->parent->pid;
			rec_set_timehdr(&rec,CKRM_EVENT_FORK,tsk->pid,core);
			rec_send(&rec);
		}
		break;
	case CKRM_EVENT_MANUAL:
		rbce_tc_manual(tsk);

	default:
		send_task_record(tsk,event,(struct ckrm_core_class *)core,1);
		break;
	}
}

/*====================== end classification engine =======================*/

static void sample_task_data(unsigned long unused); 

struct timer_list sample_timer = { .expires = 0, .function = sample_task_data };
unsigned long timer_interval_length = (250*HZ)/1000; 

inline void stop_sample_timer(void)
{
	if (sample_timer.expires > 0) {
		del_timer_sync(&sample_timer);
		sample_timer.expires = 0;
	}
}

inline void start_sample_timer(void)
{
	if (timer_interval_length > 0) {
		sample_timer.expires = jiffies + (timer_interval_length*HZ)/1000;
		add_timer(&sample_timer);
	}
}

static void send_task_data(void)
{
	struct crbce_rec_data_delim limrec;
        struct task_struct *proc, *thread;
	int sendcnt = 0;
	int taskcnt = 0;
	limrec.is_stop = 0;
	rec_set_timehdr(&limrec,CRBCE_REC_DATA_DELIMITER,0,0);
	rec_send(&limrec);
	
        read_lock(&tasklist_lock);
        do_each_thread(proc, thread) {
		taskcnt++;
		task_lock(thread);
		sendcnt += send_task_record(thread,CRBCE_REC_SAMPLE,NULL,0);
		task_unlock(thread);
        } while_each_thread(proc, thread);
        read_unlock(&tasklist_lock);

	limrec.is_stop = 1;
	rec_set_timehdr(&limrec,CRBCE_REC_DATA_DELIMITER,0,0);
	rec_send(&limrec);

	// printk("send_task_data mode=%d t#=%d s#=%d\n",delta_mode,taskcnt,sendcnt);
}

static void
notify_class_action(struct rbce_class *cls, int action)
{
	struct crbce_class_info cinfo;
	int len;

	rec_set_timehdr(&cinfo,CRBCE_REC_CLASS_INFO,0,cls->classobj);
	cinfo.action = action;
	len = strnlen(cls->obj.name,CRBCE_MAX_CLASS_NAME_LEN-1);
	memcpy(&cinfo.name,cls->obj.name,len);
	cinfo.name[len] = '\0';
	len++;
	cinfo.namelen = len;

	len += sizeof(cinfo)-CRBCE_MAX_CLASS_NAME_LEN;
	rec_send_len(&cinfo,len);
}

static void
send_classlist(void)
{
	struct rbce_class *cls;

	read_lock(&global_rwlock);
	list_for_each_entry(cls, &class_list, obj.link) {
		notify_class_action(cls,1);
	}
	read_unlock(&global_rwlock);
}

/*
 *  resend_task_info 
 * 
 *  This function resends all essential task information to the client.
 *  
 */
static void resend_task_info(void)
{
	struct crbce_rec_data_delim limrec;
	struct crbce_rec_fork   rec;
        struct task_struct *proc, *thread;

	send_classlist();     // first send available class information

	limrec.is_stop = 2;
	rec_set_timehdr(&limrec,CRBCE_REC_DATA_DELIMITER,0,0);
	rec_send(&limrec);
	
        write_lock(&tasklist_lock);  // avoid any mods during this phase 
        do_each_thread(proc, thread) {
		if (ukcc_ok()) {
			rec.ppid = thread->parent->pid;
			rec_set_timehdr(&rec,CRBCE_REC_TASKINFO,thread->pid,thread->taskclass);
			rec_send(&rec);
		}
        } while_each_thread(proc, thread);
        write_unlock(&tasklist_lock);

	limrec.is_stop = 3;
	rec_set_timehdr(&limrec,CRBCE_REC_DATA_DELIMITER,0,0);
	rec_send(&limrec);
}

extern int task_running_sys(struct task_struct*);

static void add_all_private_data(void)
{
        struct task_struct *proc, *thread;

        write_lock(&tasklist_lock);
        do_each_thread(proc, thread) {
		if (RBCE_DATA(thread) == NULL)
			RBCE_DATAP(thread) = create_private_data(NULL,0);
        } while_each_thread(proc, thread);
        write_unlock(&tasklist_lock);
}

static void sample_task_data(unsigned long unused)
{ 
        struct task_struct *proc, *thread;

	int run=0;
	int wait=0;
        read_lock(&tasklist_lock);
        do_each_thread(proc, thread) {
		struct rbce_private_data *pdata = RBCE_DATA(thread);

		if (pdata == NULL) {
			// some wierdo race condition .. simply ignore 
			continue;
		}
		if (thread->state == TASK_RUNNING) {
			if (task_running_sys(thread)) {
				atomic_inc((atomic_t*)&(PSAMPLE(pdata)->cpu_running));
				run++;
			} else {
				atomic_inc((atomic_t*)&(PSAMPLE(pdata)->cpu_waiting));
				wait++;
			}
		}
		/* update IO state */
		if (thread->flags & PF_IOWAIT) {
			if (thread->flags & PF_MEMIO) 
				atomic_inc((atomic_t*)&(PSAMPLE(pdata)->memio_delayed));
			else
				atomic_inc((atomic_t*)&(PSAMPLE(pdata)->io_delayed));
		}
        } while_each_thread(proc, thread);
        read_unlock(&tasklist_lock);
//	printk("sample_timer: run=%d wait=%d\n",run,wait);
	start_sample_timer();
}

static void ukcc_cmd_deliver(int rchan_id, char *from, u32 len)
{
	struct crbce_command  *cmdrec = (struct crbce_command*)from;
	struct crbce_cmd_done cmdret;
	int rc = 0;

//	printk("ukcc_cmd_deliver: %d %d len=%d:%d\n",cmdrec->type, cmdrec->cmd,cmdrec->len,len);

	cmdrec->len = len;  /* add this to reflection so the user doesn't accidently write 
			     * the wrong length and the protocol is getting screwed up */

	if (cmdrec->type != CRBCE_REC_KERNEL_CMD) {
		rc = EINVAL;
		goto out;
	}

	switch(cmdrec->cmd) {
	case CRBCE_CMD_SET_TIMER: 
	{
		struct crbce_cmd_settimer *cptr = (struct crbce_cmd_settimer*)cmdrec;
		if (len != sizeof(*cptr)) {
			rc = EINVAL;
			break;
		}
		stop_sample_timer();
		timer_interval_length = cptr->interval;
		if ((timer_interval_length > 0) && (timer_interval_length < 10)) 
				timer_interval_length = 10;  /* anything finer can create problems */
		printk(KERN_INFO "CRBCE set sample collect timer ... %lu\n",timer_interval_length);
		start_sample_timer();
		break;
	}
	case CRBCE_CMD_SEND_DATA: 
	{
		struct crbce_cmd_send_data *cptr = (struct crbce_cmd_send_data*)cmdrec;
		if (len != sizeof(*cptr)) {
			rc = EINVAL;
			break;
		}
		delta_mode = cptr->delta_mode;
		send_task_data();
		break;
	}
	case CRBCE_CMD_START:
		add_all_private_data();
		chan_state = UKCC_OK;
		resend_task_info();
		break;

	case CRBCE_CMD_STOP:
		chan_state = UKCC_STANDBY;
		free_all_private_data();
		break;

	default:
		rc = EINVAL;
		break;
        }

 out:	
	cmdret.hdr.type = CRBCE_REC_KERNEL_CMD_DONE;
	cmdret.hdr.cmd  = cmdrec->cmd;
	cmdret.rc       = rc;
	rec_send(&cmdret);
//	printk("ukcc_cmd_deliver ACK: %d %d rc=%d %d\n",cmdret.hdr.type,cmdret.hdr.cmd,rc,sizeof(cmdret));
}

static void client_attached(void)
{
	printk("client [%d]<%s> attached to UKCC\n",current->pid,current->comm);
	relay_reset(ukcc_channel);
}

static void client_detached(void)
{
	printk("client [%d]<%s> detached to UKCC\n",current->pid,current->comm);
	chan_state = UKCC_STANDBY;
	stop_sample_timer();
	relay_reset(ukcc_channel);
	free_all_private_data();
}

static int
init_rbce_ext_pre(void)
{
	int rc;

	rc = create_ukcc_channel();
	return (( rc < 0 ) ? rc : 0 );
}

static int
init_rbce_ext_post(void)
{
	init_timer(&sample_timer);
	return 0;
}

static void
exit_rbce_ext(void)
{
	stop_sample_timer();
	close_ukcc_channel();
}



