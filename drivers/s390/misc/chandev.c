/*
 *  drivers/s390/misc/chandev.c
 *
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 * 
 *  Generic channel device initialisation support. 
 */
#define __KERNEL_SYSCALLS__
#include <linux/config.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <asm/irq.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <asm/chandev.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <asm/s390dyn.h>
#include <asm/queue.h>

typedef struct chandev_model_info chandev_model_info;
struct chandev_model_info
{
	struct chandev_model_info *next;
	chandev_type chan_type;
	s32 cu_type;      /* control unit type  -1 = don't care */
	s16 cu_model;     /* control unit model -1 = don't care */
	s32 dev_type;     /* device type -1 = don't care */
	s16 dev_model;    /* device model -1 = don't care */
	u8  max_port_no;
	int      auto_msck_recovery;
	devreg_t drinfo;
};

typedef struct chandev chandev;
struct chandev
{
	struct chandev *next;
	chandev_model_info *model_info;
	u16 cu_type;      /* control unit type */
	u8  cu_model;     /* control unit model */
	u16 dev_type;     /* device type */
	u8  dev_model;    /* device model */
	u16 devno;
	unsigned int irq;
	int owned;
};

typedef struct chandev_noauto_range chandev_noauto_range;
struct chandev_noauto_range
{
	struct chandev_noauto_range *next;
	u16     lo_devno;
	u16     hi_devno;
};

typedef struct chandev_force chandev_force;
struct chandev_force
{
	struct chandev_force *next;
	chandev_type chan_type;
	s32     devif_num; /* -1 don't care e.g. tr0 implies 0 */
        u16     read_devno;
	u16     write_devno;
        s16     port_protocol_no; /* where available e.g. lcs,-1 don't care */
	u8      checksum_received_ip_pkts;
	u8      use_hw_stats; /* where available e.g. lcs */
};

typedef struct chandev_probelist chandev_probelist;
struct chandev_probelist
{
	struct chandev_probelist *next;
	chandev_probefunc       probefunc;
	chandev_shutdownfunc    shutdownfunc;
	chandev_reoperfunc      reoperfunc;
	chandev_type            chan_type;
	int                     devices_found;
};



#define default_msck_bits ((1<<(not_oper-1))|(1<<(no_path-1))|(1<<(revalidate-1))|(1<<(gone-1)))


static char *msck_status_strs[]=
{
	"good",
	"not_operational",
	"no_path",
	"revalidate",
	"device_gone"
};

typedef struct chandev_msck_range chandev_msck_range;
struct chandev_msck_range
{
	struct chandev_msck_range *next;
	u16     lo_devno;
	u16     hi_devno;
	int      auto_msck_recovery;
};

static chandev_msck_range *chandev_msck_range_head=NULL;

typedef struct chandev_irqinfo chandev_irqinfo;
struct chandev_irqinfo
{
	chandev_irqinfo      *next;
	chandev_msck_status  msck_status;
	u16                  devno;
	unsigned int         irq;
	void                 (*handler)(int, void *, struct pt_regs *);
	unsigned long        irqflags;
	void                 *dev_id;
	char                 devname[0];
};


chandev_irqinfo *chandev_irqinfo_head=NULL;

typedef struct chandev_parms chandev_parms;
struct chandev_parms
{
	chandev_parms      *next;
	chandev_type       chan_type;
	char               parmstr[0];
};

chandev_parms *chandev_parms_head=NULL;


typedef struct chandev_activelist chandev_activelist;
struct chandev_activelist
{
	struct chandev_activelist *next;
	chandev_irqinfo         *read_irqinfo;
	chandev_irqinfo         *write_irqinfo;
	u16                     cu_type;      /* control unit type */
	u8                      cu_model;     /* control unit model */
	u16                     dev_type;     /* device type */
	u8                      dev_model;    /* device model */
	chandev_probefunc       probefunc;
	chandev_shutdownfunc    shutdownfunc;
	chandev_reoperfunc      reoperfunc;
	chandev_unregfunc       unreg_dev;
	chandev_type            chan_type;
	u8                      port_no;
	chandev_category        category;
	int                     saved_busy_flag;
	

	void                    *dev_ptr;
	char                    devname[0];
};



static chandev_model_info *chandev_models_head=NULL;
/* The only reason chandev_head is a queue is so that net devices */
/* will be by default named in the order of their irqs */
static qheader chandev_head={NULL,NULL};
static chandev_noauto_range *chandev_noauto_head=NULL;
static chandev_force *chandev_force_head=NULL;
static chandev_probelist *chandev_probelist_head=NULL;
static chandev_activelist *chandev_activelist_head=NULL;
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
static int use_devno_names=FALSE;
#endif
static int chandev_conf_read=FALSE;
static int chandev_initialised=FALSE;


static unsigned long chandev_last_machine_check;


static struct tq_struct chandev_msck_task_tq;
static atomic_t chandev_msck_thread_lock;
static atomic_t chandev_new_msck;
static unsigned long chandev_last_startmsck_list_update;

typedef struct chandev_startmsck_list chandev_startmsck_list;
struct chandev_startmsck_list
{
	chandev_startmsck_list    *next;
	chandev_msck_status       pre_recovery_action_status;
	chandev_msck_status       post_recovery_action_status;
	char                      devname[0];
};


static chandev_startmsck_list *startlist_head=NULL;
static chandev_startmsck_list *mscklist_head=NULL;




static void chandev_read_conf(void);

#if LINUX_VERSION_CODE >=KERNEL_VERSION(2,3,0)
typedef struct net_device  net_device;
#else
typedef struct device  net_device;

static inline void init_waitqueue_head(wait_queue_head_t *q)
{
	*q=NULL;
}
#endif

#if LINUX_VERSION_CODE<KERNEL_VERSION(2,3,45)
static __inline__ void netif_stop_queue(net_device *dev)
{
	dev->tbusy=1;
}

static __inline__ void netif_start_queue(net_device *dev)
{
	dev->tbusy=0;
}
#endif



#define CHANDEV_INVALID_LOCK_OWNER            -1
static long                 chandev_lock_owner;
static int                  chandev_lock_cnt; 
static spinlock_t           chandev_spinlock;

typedef struct chandev_not_oper_struct chandev_not_oper_struct;

struct  chandev_not_oper_struct
{
	chandev_not_oper_struct *next;
	int irq;
	int status;
};


/* May as well try to keep machine checks in the order they happen so
 * we use qheader for chandev_not_oper_head instead of list.
 */
static qheader chandev_not_oper_head={NULL,NULL};
static spinlock_t           chandev_not_oper_spinlock;
static char           exec_script[]="/bin/chandev";

#define chandev_interrupt_check() \
if(in_interrupt())                \
     printk(KERN_WARNING __FUNCTION__ " called under interrupt this shouldn't happen\n")


#define for_each(variable,head) \
for((variable)=(head);(variable)!=NULL;(variable)=(variable)->next)


#define CHANDEV_USE_KERNEL_THREADS (LINUX_VERSION_CODE<KERNEL_VERSION(2,4,0))

#if CHANDEV_USE_KERNEL_THREADS
static void chandev_start_msck_thread(void *unused);
#if LINUX_VERSION_CODE<KERNEL_VERSION(2,3,0)
#define chandev_daemonize(name,mask,use_init_fs) s390_daemonize(name,mask)
#else
#define chandev_daemonize(args...) s390_daemonize(args)
#endif
typedef int chandev_task_retval;
#define chandev_task_return(val) return(val)       
#else
#define chandev_daemonize(noargs...)
#define chandev_task_retval void
#define chandev_task_return(val) return
#endif


static void chandev_lock(void)
{
	chandev_interrupt_check();
	eieio();
	if(chandev_lock_owner!=(long)current)
	{
		spin_lock(&chandev_spinlock);
		chandev_lock_cnt=1;
		chandev_lock_owner=(long)current;
	}
	else
		chandev_lock_cnt++;
	if(chandev_lock_cnt<0||chandev_lock_cnt>100)
			panic("odd lock_cnt in lcs %d lcs_chan_lock",chandev_lock_cnt);
}


static void chandev_unlock(void)
{
	if(chandev_lock_owner!=(long)current)
		panic("chandev_unlock: current=%lx"
		      " chandev_lock_owner=%lx chandev_lock_cnt=%d\n",
		      (long)current,
		      chandev_lock_owner,
		      chandev_lock_cnt);
	if(--chandev_lock_cnt==0)
	{
		chandev_lock_owner=CHANDEV_INVALID_LOCK_OWNER;
		spin_unlock(&chandev_spinlock);
	}
	if(chandev_lock_cnt<0)
		panic("odd lock_cnt in lcs %d lcs_chan_unlock",chandev_lock_cnt);
}

int chandev_full_unlock(void)
{
	int ret_lock_cnt=chandev_lock_cnt;
	chandev_lock_cnt=0;
	chandev_lock_owner=CHANDEV_INVALID_LOCK_OWNER;
	spin_unlock(&chandev_spinlock);
	return(ret_lock_cnt);
}




void *chandev_alloc(size_t size)
{
	void *mem=kmalloc(size,GFP_ATOMIC);
	if(mem)
		memset(mem,0,size);
	return(mem);
}

static void chandev_add_to_list(list **listhead,void *member)
{
	chandev_lock();
	add_to_list(listhead,member);
	chandev_unlock();
}

static void chandev_queuemember(qheader *qhead,void *member)
{
	chandev_lock();
	enqueue_tail(qhead,(queue *)member);
	chandev_unlock();
}

static int chandev_remove_from_list(list **listhead,list *member)
{
	int retval;

	chandev_lock();
	retval=remove_from_list(listhead,member);
	chandev_unlock();
	return(retval);
}

static int chandev_remove_from_queue(qheader *qhead,queue *member)
{
	int retval;
	
	chandev_lock();
	retval=remove_from_queue(qhead,member);
	chandev_unlock();
	return(retval);
}



void chandev_free_listmember(list **listhead,list *member)
{
	if(member)
	{
		if(chandev_remove_from_list(listhead,member))
			kfree(member);
		else
			printk(KERN_CRIT"chandev_free_listmember detected nonexistant"
			       "listmember listhead=%p member %p\n",listhead,member);
	}
}

void chandev_free_queuemember(qheader *qhead,queue *member)
{
	if(member)
	{
		if(chandev_remove_from_queue(qhead,member))
			kfree(member);
		else
			printk(KERN_CRIT"chandev_free_listmember detected nonexistant"
			       "listmember qhead=%p member %p\n",qhead,member);
	}
}



void chandev_free_all_list(list **listhead)
{
	list *head;

	while((head=remove_listhead(listhead)))
		kfree(head);
}

void chandev_free_all_queue(qheader *qhead)
{
	while(qhead->head)
		chandev_free_queuemember(qhead,qhead->head);
}


struct files_struct *chandev_new_files_struct(void)
{
        struct files_struct *newf = kmem_cache_alloc(files_cachep, SLAB_KERNEL);
	if (!newf) 
		return(NULL);
	memset(newf,0,sizeof(struct files_struct));
	atomic_set(&newf->count, 1);
	newf->file_lock	    = RW_LOCK_UNLOCKED;
	newf->next_fd	    = 0;
	newf->max_fds	    = NR_OPEN_DEFAULT;
	newf->max_fdset	    = __FD_SETSIZE;
	newf->close_on_exec = &newf->close_on_exec_init;
	newf->open_fds	    = &newf->open_fds_init;
	newf->fd	    = &newf->fd_array[0];
	return(newf);
}
/*
 * Mostly robbed from kmod.c
 */

static inline void
use_init_fs_context(void)
{
	struct fs_struct *our_fs, *init_fs;
	struct dentry *root, *pwd;
	struct vfsmount *rootmnt, *pwdmnt;

	/*
	 * Make modprobe's fs context be a copy of init's.
	 *
	 * We cannot use the user's fs context, because it
	 * may have a different root than init.
	 * Since init was created with CLONE_FS, we can grab
	 * its fs context from "init_task".
	 *
	 * The fs context has to be a copy. If it is shared
	 * with init, then any chdir() call in modprobe will
	 * also affect init and the other threads sharing
	 * init_task's fs context.
	 *
	 * We created the exec_modprobe thread without CLONE_FS,
	 * so we can update the fields in our fs context freely.
	 */

	init_fs = init_task.fs;
	read_lock(&init_fs->lock);
	rootmnt = mntget(init_fs->rootmnt);
	root = dget(init_fs->root);
	pwdmnt = mntget(init_fs->pwdmnt);
	pwd = dget(init_fs->pwd);
	read_unlock(&init_fs->lock);

	/* FIXME - unsafe ->fs access */
	our_fs = current->fs;
	our_fs->umask = init_fs->umask;
	set_fs_root(our_fs, rootmnt, root);
	set_fs_pwd(our_fs, pwdmnt, pwd);
	write_lock(&our_fs->lock);
	if (our_fs->altroot) {
		struct vfsmount *mnt = our_fs->altrootmnt;
		struct dentry *dentry = our_fs->altroot;
		our_fs->altrootmnt = NULL;
		our_fs->altroot = NULL;
		write_unlock(&our_fs->lock);
		dput(dentry);
		mntput(mnt);
	} else 
		write_unlock(&our_fs->lock);
	dput(root);
	mntput(rootmnt);
	dput(pwd);
	mntput(pwdmnt);
}


static int exec_usermodehelper(char *program_path, char *argv[], char *envp[])
{
	int err;
	wait_queue_head_t    wait;


	current->session = 1;
	current->pgrp = 1;

	/* We copy this off init & can't go until this is set up */
	init_waitqueue_head(&wait);
	while(init_task.fs->root==NULL)
	{
		sleep_on_timeout(&wait,HZ);
	}
	use_init_fs_context();

	/* Prevent parent user process from sending signals to child.
	   Otherwise, if the modprobe program does not exist, it might
	   be possible to get a user defined signal handler to execute
	   as the super user right after the execve fails if you time
	   the signal just right.
	*/
	spin_lock_irq(&current->sigmask_lock);
	flush_signals(current);
	flush_signal_handlers(current);
	spin_unlock_irq(&current->sigmask_lock);
	 /* current->files sometimes was null this means we need */
	 /* to build our own */
	 exit_files(current);
	 if((current->files=chandev_new_files_struct())==NULL)
	 {
		 printk("chandev_new_files_struct allocation failed\n");
		 return(0);
	 }
	 
	/* Drop the "current user" thing */
	{
		struct user_struct *user = current->user;
		current->user = INIT_USER;
		atomic_inc(&INIT_USER->__count);
		atomic_inc(&INIT_USER->processes);
		atomic_dec(&user->processes);
		free_uid(user);
	}

	/* Take all effective privileges.. */
	current->uid = current->euid = current->fsuid = 0;
	cap_set_full(current->cap_effective);
	/* Allow execve & open args to be in kernel space. */
	set_fs(KERNEL_DS);
	/* We need stdin out & err for scripts */
	if (open("/dev/console", O_RDWR, 0)< 0)
		printk("chandev exec_usermode_helper unable to open an initial console.\n");
	(void) dup(0);
	(void) dup(0);

	
        /* Go, go, go... */
        err=execve(program_path, argv, envp);
	return err;
}

static int exec_start_script(void *unused)
{
	
	char **argv,*tempname;
	int retval=-ENOMEM;
	int loopcnt,argc;
	size_t allocsize;
	chandev_startmsck_list *member;
	wait_queue_head_t      wait;
	static char * envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
	
	init_waitqueue_head(&wait);
	s390_daemonize("chandev_script",0,FALSE);
	for(loopcnt=0;loopcnt<10&&(jiffies-chandev_last_startmsck_list_update)<HZ;loopcnt++)
	{
		sleep_on_timeout(&wait,HZ);
	}
	chandev_lock();
	argc=1;
	if(startlist_head)
		argc++;
	for_each(member,startlist_head)
		argc++;
	if(mscklist_head)
		argc+=3;
	for_each(member,mscklist_head)
		argc++;

	allocsize=(argc+1)*sizeof(char *);
        /* Warning possible stack overflow */
	/* We can't kmalloc the parameters here as execve will */
	/* not return if successful */
	argv=alloca(allocsize);
	if(argv)
	{
		memset(argv,0,allocsize);
		argc=0;
		argv[argc++]=exec_script;
		if(startlist_head)
			argv[argc++]="start";
		for_each(member,startlist_head)
		{
			tempname=alloca(strlen(member->devname)+1);
			if(tempname)
			{
				strcpy(tempname,member->devname);
				argv[argc++]=tempname;
			}
			else
				goto Fail;
		}
		if(mscklist_head)
			argv[argc++]="machine_check";
		for_each(member,mscklist_head)
		{
			tempname=alloca(strlen(member->devname)+1);
			if(tempname)
			{
				strcpy(tempname,member->devname);
				argv[argc++]=tempname;
			}
			else
				goto Fail;
			argv[loopcnt++]=msck_status_strs[member->pre_recovery_action_status];
			argv[loopcnt++]=msck_status_strs[member->post_recovery_action_status];
		}
		chandev_free_all_list((list **)&startlist_head);
		chandev_free_all_list((list **)&mscklist_head);
		chandev_unlock();
		/* We are basically execve'ing here there normally is no */
		/* return */
		retval=exec_usermodehelper(exec_script, argv, envp);
		goto Fail2;
	}
 Fail:
	
	chandev_unlock();
 Fail2:
	/* We don't really need to report /bin/chandev not existing */
	if(retval!=-ENOENT)
	   printk("exec_start_script failed retval=%d\n",retval);
	return(0);
}


void *chandev_allocstr(const char *str,size_t offset)
{
	char *member;

	if((member=chandev_alloc(offset+strlen(str)+1)))
	{
		strcpy(&member[offset],str);
	}
	return((void *)member);
}


static int chandev_add_to_startmsck_list(chandev_startmsck_list **listhead,char *devname,
chandev_msck_status pre_recovery_action_status,chandev_msck_status post_recovery_action_status)
{
	int retval;
	chandev_startmsck_list *member;
	int pid;
	
	chandev_lock();
	/* remove operations still outstanding for this device */
	for_each(member,startlist_head)
		if(strcmp(member->devname,devname)==0)
			chandev_remove_from_list((list **)&startlist_head,(list *)member);
	for_each(member,mscklist_head)
		if(strcmp(member->devname,devname)==0)
			chandev_remove_from_list((list **)&mscklist_head,(list *)member);
	

	if((member=chandev_allocstr(devname,offsetof(chandev_startmsck_list,devname))))
	{
		member->pre_recovery_action_status=pre_recovery_action_status;
		member->post_recovery_action_status=post_recovery_action_status;
		add_to_list((list **)listhead,(list *)member);
		chandev_last_startmsck_list_update=jiffies;
		chandev_unlock();
		/* We do CLONE_FILES so we can exit_files to get rid of it */
                /* cheaply & allocate a new one we need current->files &  */
                /* some tasks have current->files==NULL */
		pid = kernel_thread(exec_start_script,NULL,CLONE_FILES|SIGCHLD);
		if(pid<0)
		{
			printk("error making kernel thread for exec_start_script\n");
			retval=pid;
		}
		else
			return(0);

	}
	else
	{
		printk("chandev_add_to_startmscklist memory allocation failed devname=%s\n",devname);
		retval=-ENOMEM;
	}
	chandev_unlock();
	return(retval);
}





int chandev_oper_func(int irq,devreg_t *dreg)
{
	chandev_last_machine_check=jiffies;
	if(atomic_dec_and_test(&chandev_msck_thread_lock))
	{
#if CHANDEV_USE_KERNEL_THREADS
		queue_task(&chandev_msck_task_tq,&tq_scheduler);
#else
		schedule_task(&chandev_msck_task_tq);
#endif
	}
	atomic_set(&chandev_new_msck,TRUE);
	return(0);
}

static void chandev_not_oper_handler(int irq,int status )
{
	chandev_not_oper_struct *new_not_oper;

	chandev_last_machine_check=jiffies;
	if((new_not_oper=kmalloc(sizeof(chandev_not_oper_struct),GFP_ATOMIC)))
	{
		new_not_oper->irq=irq;
		new_not_oper->status=status;
		spin_lock(&chandev_not_oper_spinlock);
		enqueue_tail(&chandev_not_oper_head,(queue *)new_not_oper);
		spin_unlock(&chandev_not_oper_spinlock);
		if(atomic_dec_and_test(&chandev_msck_thread_lock))
		{
#if CHANDEV_USE_KERNEL_THREADS
			queue_task(&chandev_msck_task_tq,&tq_scheduler);
#else
			schedule_task(&chandev_msck_task_tq);
#endif
		}
	}
	else
		printk("chandev_not_oper_handler failed to allocate memory & "
		       "lost a not operational interrupt %d %x",
		       irq,status);
}

chandev_irqinfo *chandev_get_irqinfo_by_irq(int irq)
{
	chandev_irqinfo *curr_irqinfo;
	for_each(curr_irqinfo,chandev_irqinfo_head)
		if(irq==curr_irqinfo->irq)
			return(curr_irqinfo);
	return(NULL);
}

chandev *chandev_get_by_irq(int irq)
{
	chandev *curr_chandev;

	for_each(curr_chandev,(chandev *)chandev_head.head)
		if(curr_chandev->irq==irq)
		{
			return(curr_chandev);
		}
	return(NULL);
}

chandev_activelist *chandev_get_activelist_by_irq(int irq)
{
	chandev_activelist *curr_device;

	for_each(curr_device,chandev_activelist_head)
	{
			if(curr_device->read_irqinfo->irq==irq||
			   curr_device->write_irqinfo->irq==irq)
				return(curr_device);
	}
	return(NULL);
}



int chandev_request_irq(unsigned int   irq,
                      void           (*handler)(int, void *, struct pt_regs *),
                      unsigned long  irqflags,
                      const char    *devname,
                      void          *dev_id)
{
	chandev_irqinfo *new_irqinfo;
	chandev_activelist *curr_device;
	s390_dev_info_t         devinfo;
	int          retval;
	

	chandev_lock();
	if((curr_device=chandev_get_activelist_by_irq(irq)))
	{
		printk("chandev_request_irq failed devname=%s irq=%d "
		       "it already belongs to %s shutdown this device first.\n",
		       devname,irq,curr_device->devname);
		chandev_unlock();
		return(-EPERM);
	}
	/* remove any orphan irqinfo left lying around. */
        if((new_irqinfo=chandev_get_irqinfo_by_irq(irq)))
		chandev_remove_from_list((list **)chandev_irqinfo_head,
					 (list *)new_irqinfo);
	chandev_unlock();
	if((new_irqinfo=chandev_allocstr(devname,offsetof(chandev_irqinfo,devname))))
	{
		
		if((retval=get_dev_info_by_irq(irq,&devinfo))||
		   (retval=s390_request_irq_special(irq,handler,
						chandev_not_oper_handler,
						irqflags,devname,dev_id)))
			kfree(new_irqinfo);
		else
		{
			new_irqinfo->irq=irq;
			new_irqinfo->handler=handler;
			new_irqinfo->dev_id=dev_id;
			new_irqinfo->devno=devinfo.devno;
			chandev_add_to_list((list **)&chandev_irqinfo_head,new_irqinfo);
		}
	}
	else
	{
		printk("chandev_request_irq memory allocation failed devname=%s irq=%d\n",devname,irq);
		retval=-ENOMEM;
	}
	return(retval);
}



void chandev_sprint_type_model(char *buff,s32 type,s16 model)
{
	if(type==-1)
		strcpy(buff,"    *    ");
	else
		sprintf(buff," 0x%04x  ",(int)type);
	buff+=strlen(buff);
	if(model==-1)
		strcpy(buff,"    *   ");
	else
		sprintf(buff," 0x%02x  ",(int)model);
}

void chandev_sprint_devinfo(char *buff,s32 cu_type,s16 cu_model,s32 dev_type,s16 dev_model)
{
	chandev_sprint_type_model(buff,cu_type,cu_model);
	chandev_sprint_type_model(&buff[strlen(buff)],dev_type,dev_model);
}

void chandev_remove_parms(chandev_type chan_type,int exact_match)
{
	chandev_parms      *curr_parms;

	chandev_lock();
	for_each(curr_parms,chandev_parms_head)
	{
		if((chan_type&(curr_parms->chan_type)&&!exact_match)||
		    (chan_type==(curr_parms->chan_type)&&exact_match))
		    chandev_free_listmember((list **)&chandev_parms_head,(list *)curr_parms);
	}
	chandev_unlock();
}

void chandev_add_parms(chandev_type chan_type,char *parmstr)
{
	chandev_parms      *new_parms;
	
	if((new_parms=chandev_allocstr(parmstr,offsetof(chandev_parms,parmstr))))
	{
		chandev_remove_parms(chan_type,TRUE);
		new_parms->chan_type=chan_type;
		chandev_add_to_list((list **)&chandev_parms_head,(void *)new_parms);
	}
	else
		printk("chandev_add_parmstr memory request failed\n");
}


void chandev_add_model(chandev_type chan_type,s32 cu_type,s16 cu_model,
		       s32 dev_type,s16 dev_model,u8 max_port_no,int auto_msck_recovery)
{
	chandev_model_info *newmodel;
	int                err;
	char buff[40];

	if((newmodel=chandev_alloc(sizeof(chandev_model_info))))
	{
		devreg_t *drinfo=&newmodel->drinfo;
		newmodel->chan_type=chan_type;
		newmodel->cu_type=cu_type;
		newmodel->cu_model=cu_model;
		newmodel->dev_type=dev_type;
		newmodel->dev_model=dev_model;
		newmodel->max_port_no=max_port_no;
		newmodel->auto_msck_recovery=auto_msck_recovery;
		
		if(cu_type==-1&&dev_type==-1)
		{
			chandev_sprint_devinfo(buff,newmodel->cu_type,newmodel->cu_model,
					       newmodel->dev_type,newmodel->dev_model);
			printk(KERN_INFO"can't call s390_device_register for this device chan_type/chan_model/dev_type/dev_model %s\n",buff);
			kfree(newmodel);
			return;
		}
		/* We ignore errors as they are likely to
		   occur owing to incompatibilities with
		   Ingos layer 
		*/
		drinfo->flag=DEVREG_TYPE_DEVCHARS;
		if(dev_model==-1)
			drinfo->flag|=(dev_type==-1 ? DEVREG_NO_DEV_INFO:DEVREG_MATCH_DEV_TYPE);
		if(cu_model==-1)
			drinfo->flag|=(cu_type==-1 ? DEVREG_NO_CU_INFO:DEVREG_MATCH_CU_TYPE);
		else if(dev_model!=-1&&cu_type!=-1)
			drinfo->flag|=DEVREG_EXACT_MATCH;
		drinfo->ci.hc.ctype=cu_type;
		drinfo->ci.hc.cmode=cu_model;
		drinfo->ci.hc.dtype=dev_type;
		drinfo->ci.hc.dmode=dev_model;
		drinfo->oper_func=chandev_oper_func;
		if((err=s390_device_register(&newmodel->drinfo)))
		{
			chandev_sprint_devinfo(buff,newmodel->cu_type,newmodel->cu_model,
					       newmodel->dev_type,newmodel->dev_model);
			printk("s390_device_register failed in chandev_add_model"
			       " this is nothing to worry about chan_type/chan_model/dev_type/dev_model %s\n",buff);
			drinfo->oper_func=NULL;
		}
		chandev_add_to_list((list **)&chandev_models_head,newmodel);
	}
}


void chandev_remove(chandev *member)
{
	chandev_free_queuemember(&chandev_head,(queue *)member);
}


void chandev_remove_all(void)
{
	chandev_free_all_queue(&chandev_head);
}

void chandev_remove_model(chandev_model_info *model)
{
	chandev *curr_chandev;

	chandev_lock();
	for_each(curr_chandev,(chandev *)chandev_head.head)
		if(curr_chandev->model_info==model)
			chandev_remove(curr_chandev);
	if(model->drinfo.oper_func)
		s390_device_unregister(&model->drinfo);
	chandev_free_listmember((list **)&chandev_models_head,(list *)model);
	chandev_unlock();
}

void chandev_remove_all_models(void)
{
	chandev_lock();
	while(chandev_models_head)
		chandev_remove_model(chandev_models_head);
	chandev_unlock();
}

void chandev_del_model(s32 cu_type,s16 cu_model,s32 dev_type,s16 dev_model)
{
	chandev_model_info *curr_model;
	
	chandev_lock();
	for_each(curr_model,chandev_models_head)
		if((curr_model->cu_type==cu_type||cu_type==-1)&&
		   (curr_model->cu_model==cu_model||cu_model==-1)&&
		   (curr_model->dev_type==dev_type||dev_type==-1)&&
		   (curr_model->dev_model==dev_model||dev_model==-1))
			chandev_remove_model(curr_model);			
	chandev_unlock();
}

static void chandev_init_default_models(void)
{
	/* P390/Planter 3172 emulation assume maximum 16 to be safe. */
	chandev_add_model(lcs,0x3088,0x1,-1,-1,15,default_msck_bits);	

	/* 3172/2216 Paralell the 2216 allows 16 ports per card the */
	/* the original 3172 only allows 4 we will assume the max of 16 */
	chandev_add_model(lcs|ctc,0x3088,0x8,-1,-1,15,default_msck_bits);

	/* 3172/2216 Escon serial the 2216 allows 16 ports per card the */
	/* the original 3172 only allows 4 we will assume the max of 16 */
	chandev_add_model(lcs|escon,0x3088,0x1F,-1,-1,15,default_msck_bits);

	/* Only 2 ports allowed on OSA2 cards model 0x60 */
	chandev_add_model(lcs,0x3088,0x60,-1,-1,1,default_msck_bits);
	/* qeth has relative adapter concept so we give it 16 */
	chandev_add_model(qeth,0x1731,0x1,0x1732,0x1,15,default_msck_bits);
	/* Osa-D we currently aren't too emotionally involved with this */
	chandev_add_model(osad,0x3088,0x62,-1,-1,0,default_msck_bits);
}


void chandev_del_noauto(u16 devno)
{
	chandev_noauto_range *curr_noauto;
	chandev_lock();
	for_each(curr_noauto,chandev_noauto_head)
		if(curr_noauto->lo_devno<=devno&&curr_noauto->hi_devno>=devno)
			chandev_free_listmember((list **)&chandev_noauto_head,(list *)curr_noauto); 
	chandev_unlock();
}

void chandev_del_msck(u16 devno)
{
	chandev_msck_range *curr_msck_range;
	chandev_lock();
	for_each(curr_msck_range,chandev_msck_range_head)
		if(curr_msck_range->lo_devno<=devno&&curr_msck_range->hi_devno>=devno)
			chandev_free_listmember((list **)&chandev_msck_range_head,(list *)curr_msck_range); 
	chandev_unlock();
}


void chandev_add(s390_dev_info_t  *newdevinfo,chandev_model_info *newmodelinfo)
{
	chandev *new_chandev=NULL;

	if((new_chandev=chandev_alloc(sizeof(chandev))))
	{
		new_chandev->model_info=newmodelinfo;
		new_chandev->cu_type=newdevinfo->sid_data.cu_type; /* control unit type */
		new_chandev->cu_model=newdevinfo->sid_data.cu_model; /* control unit model */
		new_chandev->dev_type=newdevinfo->sid_data.dev_type; /* device type */
		new_chandev->dev_model=newdevinfo->sid_data.dev_model; /* device model */
		new_chandev->devno=newdevinfo->devno;
		new_chandev->irq=newdevinfo->irq;
		new_chandev->owned=(newdevinfo->status&DEVSTAT_DEVICE_OWNED ? TRUE:FALSE);
		chandev_queuemember(&chandev_head,new_chandev);
	}
}

void chandev_unregister_probe(chandev_probefunc probefunc)
{
	chandev_probelist *curr_probe;

	chandev_lock();
	for_each(curr_probe,chandev_probelist_head)
		if(curr_probe->probefunc==probefunc)
			chandev_free_listmember((list **)&chandev_probelist_head,
						(list *)curr_probe);
	chandev_unlock();
}


void chandev_reset(void)
{
	chandev_lock();
	chandev_remove_all_models();
	chandev_free_all_list((list **)&chandev_noauto_head);
	chandev_free_all_list((list **)&chandev_msck_range_head);
	chandev_free_all_list((list **)&chandev_force_head);
	chandev_remove_parms(-1,FALSE);
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	use_devno_names=FALSE;
#endif
	chandev_conf_read=FALSE;
	chandev_unlock();
}


chandev_model_info *chandev_is_chandev(int irq,s390_dev_info_t *devinfo)
{
	chandev_model_info *curr_model=NULL;
	int err;
	if((err=get_dev_info_by_irq(irq,devinfo)))
	{
		printk("chandev_is_chandev get_dev_info_by_irq reported err=%X on irq %d\n"
		       "should not happen\n",err,irq);
			return(NULL);
	}
	chandev_lock();
	for_each(curr_model,chandev_models_head)
	{
		if(((curr_model->cu_type==devinfo->sid_data.cu_type)||(curr_model->cu_type==-1))&&
		   ((curr_model->cu_model==devinfo->sid_data.cu_model)||(curr_model->cu_model==-1))&&
		   ((curr_model->dev_type==devinfo->sid_data.dev_type)||(curr_model->dev_type==-1))&&
		   ((curr_model->dev_model==devinfo->sid_data.dev_model)||(curr_model->dev_model==-1)))
			break;
	}
	chandev_unlock();
	return(curr_model);
}

void chandev_collect_devices(void)
{
	int curr_irq,loopcnt=0;
	s390_dev_info_t   curr_devinfo;
	chandev_model_info *curr_model;
     

	for(curr_irq=get_irq_first();curr_irq>=0; curr_irq=get_irq_next(curr_irq))
	{
		/* check read chandev
		 * we had to do the cu_model check also because ctc devices
		 * have the same cutype & after asking some people
		 * the model numbers are given out pseudo randomly so
		 * we can't just take a range of them also the dev_type & models are 0
		 */
		loopcnt++;
		if(loopcnt>0x10000)
		{
			printk(KERN_ERR"chandev_collect_devices detected infinite loop bug in get_irq_next\n");
			break;
		}
		chandev_lock();
		if((curr_model=chandev_is_chandev(curr_irq,&curr_devinfo)))
			chandev_add(&curr_devinfo,curr_model);
		chandev_unlock();
	}
}

void chandev_add_force(chandev_type chan_type,s32 devif_num,u16 read_devno,
u16 write_devno,s16 port_protocol_no,u8 checksum_received_ip_pkts,u8 use_hw_stats)

{
	chandev_force *new_chandev_force;

	if((new_chandev_force=chandev_alloc(sizeof(chandev_force))))
	{
		new_chandev_force->chan_type=chan_type;
		new_chandev_force->devif_num=devif_num;
		new_chandev_force->read_devno=read_devno;
		new_chandev_force->write_devno=write_devno;
		new_chandev_force->port_protocol_no=port_protocol_no;
		new_chandev_force->checksum_received_ip_pkts=checksum_received_ip_pkts;
		new_chandev_force->use_hw_stats=use_hw_stats;
		chandev_add_to_list((list **)&chandev_force_head,new_chandev_force);
	}
}

void chandev_del_force(u16 read_devno)
{
	chandev_force *curr_force;
	
	chandev_lock();
	for_each(curr_force,chandev_force_head)
	{
		if(curr_force->read_devno==read_devno)
			chandev_free_listmember((list **)&chandev_force_head,
						(list *)curr_force);
	}
	chandev_unlock();
}


void chandev_shutdown(chandev_activelist *curr_device)
{
	chandev_lock();

	if(curr_device->category==network_device)
	{
		/* unregister_netdev calls the dev->close so we shouldn't do this */
		/* this otherwise we crash */
		if(curr_device->unreg_dev)
			curr_device->unreg_dev(curr_device->dev_ptr); 
	}
	curr_device->shutdownfunc(curr_device->dev_ptr);
	kfree(curr_device->dev_ptr);
	chandev_free_listmember((list **)&chandev_irqinfo_head,(list *)curr_device->read_irqinfo);
	chandev_free_listmember((list **)&chandev_irqinfo_head,(list *)curr_device->write_irqinfo);
	chandev_free_listmember((list **)&chandev_activelist_head,
				(list *)curr_device);
	chandev_unlock();
}

void chandev_shutdown_all(void)
{
	while(chandev_activelist_head)
		chandev_shutdown(chandev_activelist_head);
}
void chandev_shutdown_by_name(char *devname)
{
	chandev_activelist *curr_device;

	chandev_lock();
	for_each(curr_device,chandev_activelist_head)
		if(strcmp(devname,curr_device->devname)==0)
		{
			chandev_shutdown(curr_device);
			break;
		}
	chandev_unlock();
}

static chandev_activelist *chandev_active(u16 devno)
{
	chandev_activelist *curr_device;

	for_each(curr_device,chandev_activelist_head)
		if(curr_device->read_irqinfo->devno==devno||
		   curr_device->write_irqinfo->devno==devno)
		{
			return(curr_device);
		}
	return(NULL);
}

void chandev_shutdown_by_devno(u16 devno)
{
	chandev_activelist *curr_device;

	chandev_lock();
	curr_device=chandev_active(devno);
	if(curr_device)
		chandev_shutdown(curr_device);
	chandev_unlock();
}


int chandev_pack_args(char *str)
{
	char *newstr=str,*next;
	int strcnt=1;

	while(*str)
	{
		next=str+1;
		/*remove dead spaces */
		if(isspace(*str)&&isspace(*next))
		{
			str++;
			continue;
		}
		if(isspace(*str)||((*str)=='-'))
		{
			*str=',';
			goto pack_dn;
		}
		if(((*str)==';')&&(*next))
		{
			strcnt++;
			*str=0;
		}
	pack_dn:
		*newstr++=*str++;
		
	}
	*newstr=0;
	return(strcnt);
}

typedef enum
{ 
	isnull=0,
	isstr=1,
	isnum=2,
	iscomma=4,
} chandev_strval;

chandev_strval chandev_strcmp(char *teststr,char **str,long *endlong)
{
	char *cur;
	chandev_strval  retval=isnull;

	int len=strlen(teststr);
	if(strncmp(teststr,*str,len)==0)
	{
		*str+=len;
		retval=isstr;
		cur=*str;
		*endlong=simple_strtol(cur,str,0);
		if(cur!=*str)
			retval|=isnum;
		if(**str==',')
		{
			retval|=iscomma;
			*str+=1;
		}
		else if(**str!=0)
			retval=isnull;
	}
	return(retval);
}


int chandev_initdevice(chandev_probeinfo *probeinfo,void *dev_ptr,u8 port_no,char *devname,chandev_category category,chandev_unregfunc unreg_dev)
{
	chandev_activelist *newdevice;

	chandev_interrupt_check();
	if(probeinfo->newdevice!=NULL)
	{
		printk("probeinfo->newdevice!=NULL in chandev_initdevice for %s",devname);
		return(-EPERM);
	}
	

	if((newdevice=chandev_allocstr(devname,offsetof(chandev_activelist,devname))))
	{
		probeinfo->newdevice=newdevice;
		chandev_lock();
		newdevice->read_irqinfo=chandev_get_irqinfo_by_irq(probeinfo->read_irq);
		newdevice->write_irqinfo=chandev_get_irqinfo_by_irq(probeinfo->write_irq);
		chandev_unlock();
		if(newdevice->read_irqinfo==NULL||newdevice->write_irqinfo==NULL)
		{
			printk("chandev_initdevice, it appears that chandev_request_irq was not "
			       "called for devname=%s read_irq=%d write_irq=%d\n",devname,probeinfo->read_irq,probeinfo->write_irq);
			kfree(newdevice);
			return(-EPERM);
		}
		newdevice->cu_type=probeinfo->cu_type;
		newdevice->cu_model=probeinfo->cu_model;
		newdevice->dev_type=probeinfo->dev_type;
		newdevice->dev_model=probeinfo->dev_model;
		newdevice->chan_type=probeinfo->chan_type;		
		newdevice->dev_ptr=dev_ptr;
		newdevice->port_no=port_no;
		newdevice->category=category;
		newdevice->unreg_dev=unreg_dev;
		return(0);
	}
	return(-ENOMEM);
}

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
struct net_device *chandev_initnetdevice(chandev_probeinfo *probeinfo,u8 port_no,
struct net_device *dev, int sizeof_priv, char *basename, 
struct net_device *(*init_netdevfunc)(struct net_device *dev, int sizeof_priv),
void (*unreg_netdevfunc)(struct net_device *dev))
#else
struct device *chandev_initnetdevice(chandev_probeinfo *probeinfo,u8 port_no,
struct device *dev, int sizeof_priv, char *basename,
struct device *(*init_netdevfunc)(struct device *dev, int sizeof_priv),
void (*unreg_netdevfunc)(struct device *dev))
#endif
{
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	struct net_device *retdevice=NULL;
	int new_device = 0;
#else
	struct device *retdevice=NULL;
#endif

	if (!init_netdevfunc) 
	{
		printk("init_netdevfunc=NULL in chandev_initnetdevice, it should not be valid.\n");
		return NULL;
	}
	if (!unreg_netdevfunc) 
	{
		printk("unreg_netdevfunc=NULL in chandev_initnetdevice, it should not be valid.\n");
		return NULL;
	}

	chandev_interrupt_check();

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
        /* Allocate a device if one is not provided. */
        if (dev == NULL) 
	{
		/* ensure 32-byte alignment of the private area */
		int alloc_size = sizeof (*dev) + sizeof_priv + 31;

		dev = (struct net_device *) kmalloc (alloc_size, GFP_KERNEL);
		if (dev == NULL) 
		{
			printk(KERN_ERR "chandev_initnetdevice: Unable to allocate device memory.\n");
			return NULL;
		}

		memset(dev, 0, alloc_size);

		if (sizeof_priv)
			dev->priv = (void *) (((long)(dev + 1) + 31) & ~31);

		if (probeinfo->devif_num != -1) 
			sprintf(dev->name,"%s%d",basename,(int)probeinfo->devif_num);
		else if (use_devno_names) 
			sprintf(dev->name,"%s0x%04x",basename,(int)probeinfo->read_devno);

		new_device = 1;
	}
#endif

	retdevice=init_netdevfunc(dev,sizeof_priv);

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	/* Register device if necessary */
	if (retdevice && new_device)
		register_netdev(retdevice);
#endif

	if (retdevice) 
	{
		if (chandev_initdevice(probeinfo,retdevice,port_no,retdevice->name,
				      network_device,(chandev_unregfunc)unreg_netdevfunc)) 
		{
			unreg_netdevfunc(retdevice);
			retdevice = NULL;
		}
	}

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	/* We allocated it, so we should free it on error */
	if (!retdevice && new_device) 
		kfree(dev);
#endif

	return retdevice;
}




int chandev_doprobe(chandev_force *force,chandev *read_chandev,
chandev *write_chandev)
{
	chandev_probelist *probe;
	chandev_model_info *model_info;
	chandev_probeinfo probeinfo;
	int               rc=-1,hint=-1;
	chandev_activelist *newdevice;
	chandev_probefunc  probefunc;
	int                saved_lock_cnt;
	chandev_parms      *curr_parms;

	memset(&probeinfo,0,sizeof(probeinfo));
	model_info=read_chandev->model_info;
	if(read_chandev->model_info!=write_chandev->model_info||
	   (force&&((force->chan_type&model_info->chan_type)==0))||
	   (!force&&((read_chandev->cu_type!=write_chandev->cu_type)||
		    (read_chandev->cu_model!=write_chandev->cu_model)||
		    (read_chandev->dev_type!=write_chandev->dev_type)||
		   (read_chandev->dev_model!=write_chandev->dev_model))))
		return(-1); /* inconsistent */
	for_each(probe,chandev_probelist_head)
	{
		probeinfo.chan_type=(probe->chan_type&model_info->chan_type);
		if(probeinfo.chan_type)
		{
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
			if(use_devno_names)
				probeinfo.devif_num=read_chandev->devno;
			else
#endif
				probeinfo.devif_num=-1;
			probeinfo.read_irq=read_chandev->irq;
			probeinfo.write_irq=write_chandev->irq;
			probeinfo.read_devno=read_chandev->devno;
			probeinfo.write_devno=write_chandev->devno;
			probeinfo.max_port_no=model_info->max_port_no;
			probeinfo.cu_type=read_chandev->cu_type;
			probeinfo.cu_model=read_chandev->cu_model;
			probeinfo.dev_type=read_chandev->dev_type;
			probeinfo.dev_model=read_chandev->dev_model;
			for_each(curr_parms,chandev_parms_head)
			{
				if(probe->chan_type==curr_parms->chan_type)
				{
					probeinfo.parmstr=curr_parms->parmstr;
					break;
				}
			}
			if(force)
			{
				probeinfo.port_protocol_no=force->port_protocol_no;
				if(force->devif_num!=-1)
					probeinfo.devif_num=force->devif_num;
				probeinfo.checksum_received_ip_pkts=force->checksum_received_ip_pkts;
				probeinfo.use_hw_stats=force->use_hw_stats;
				
			}
			else
			{
				probeinfo.port_protocol_no=-1;
				probeinfo.checksum_received_ip_pkts=FALSE;
				probeinfo.use_hw_stats=FALSE;
				if(probe->chan_type&lcs)
				{
					if((probeinfo.read_devno&1)||
					   ((probeinfo.read_devno|1)!=
					    (probeinfo.write_devno)))
						return(-1);
					hint=(read_chandev->devno&0xFF)>>1;
					if(hint>model_info->max_port_no)
					{
				/* The card is possibly emulated e.g P/390 */
				/* or possibly configured to use a shared */
				/* port configured by osa-sf. */
						hint=0;
					}
				}
			}
			probeinfo.hint_port_no=hint;
			probefunc=probe->probefunc;
			saved_lock_cnt=chandev_full_unlock();
			/* We have to leave the lock go here */
			/* as probefunctions can call schedule & */
                        /* reenter to do a kernel thread & we may deadlock */
			rc=probefunc(&probeinfo);
			chandev_lock();
			chandev_lock_cnt=saved_lock_cnt;
			if(rc==0)
			{
				newdevice=probeinfo.newdevice;
				if(newdevice)
				{
					newdevice->probefunc=probe->probefunc;
					newdevice->shutdownfunc=probe->shutdownfunc;
					newdevice->reoperfunc=probe->reoperfunc;
					probe->devices_found++;
					chandev_add_to_list((list **)&chandev_activelist_head,
							    newdevice);
					chandev_add_to_startmsck_list(&startlist_head,
								      newdevice->devname,good,good);
					
				}
				else
				{
					printk("chandev_initdevice either failed or wasn't called for device read_irq=0x%04x\n",probeinfo.read_irq);
				}
				break;
			}
		}
	}
	return(rc);
}


int chandev_request_irq_from_irqinfo(chandev_irqinfo *irqinfo,chandev *this_chandev)
{
	int retval=s390_request_irq_special(irqinfo->irq,
				   irqinfo->handler,
				   chandev_not_oper_handler,
				   irqinfo->irqflags,
				   irqinfo->devname,
				   irqinfo->dev_id);
	if(retval==0)
		this_chandev->owned=TRUE;
	return(retval);
}

void chandev_irqallocerr(chandev_irqinfo *irqinfo,int err)
{
	printk("chandev_probe failed to realloc irq=%d for %s err=%d\n",irqinfo->irq,irqinfo->devname,err);
}

void chandev_probe(void)
{
	chandev *read_chandev,*write_chandev,*curr_chandev;
	chandev_force *curr_force;
	chandev_noauto_range *curr_noauto;
	chandev_activelist *curr_device;
	chandev_irqinfo *curr_irqinfo;
	s390_dev_info_t curr_devinfo;
	int  err;
	int auto_msck_recovery;
	chandev_msck_status prevstatus;
	chandev_msck_range *curr_msck_range;


	chandev_interrupt_check();
	chandev_collect_devices();
	chandev_lock();
	for_each(curr_irqinfo,chandev_irqinfo_head)
	{
		if((curr_device=chandev_get_activelist_by_irq(curr_irqinfo->irq)))
		{
			prevstatus=curr_irqinfo->msck_status;
			if(curr_irqinfo->msck_status!=good)
			{
				curr_chandev=chandev_get_by_irq(curr_irqinfo->irq);
				if(curr_chandev)
				{
					auto_msck_recovery=curr_chandev->model_info->
						auto_msck_recovery;
				}
				else
					goto remove;
				for_each(curr_msck_range,chandev_msck_range_head)
				{
					if(curr_msck_range->lo_devno<=
					   curr_irqinfo->devno&&
					   curr_msck_range->hi_devno>=
					   curr_irqinfo->devno)
					{
						auto_msck_recovery=
							curr_msck_range->
							auto_msck_recovery;
						break;
					}
				}
				if((1<<(curr_irqinfo->msck_status-1))&auto_msck_recovery)
				{
					if(curr_irqinfo->msck_status==revalidate)
					{
						if((get_dev_info_by_irq(curr_irqinfo->irq,&curr_devinfo)==0))
						{
							curr_irqinfo->devno=curr_devinfo.devno;
							curr_irqinfo->msck_status=good;
							goto remove;
						}
					}
					else
					{
						if((curr_chandev=chandev_get_by_irq(curr_irqinfo->irq)))
						{
							/* Has the device reappeared */
							if(curr_chandev->cu_type==curr_device->cu_type&&
							   curr_chandev->cu_model==curr_device->cu_model&&
							   curr_chandev->dev_type==curr_device->dev_type&&
							   curr_chandev->dev_model==curr_device->dev_model&&
							   curr_chandev->devno==curr_irqinfo->devno)
							{
								if((err=chandev_request_irq_from_irqinfo(curr_irqinfo,curr_chandev))==0)
									curr_irqinfo->msck_status=good;
								else
									chandev_irqallocerr(curr_irqinfo,err);
							}
					
						}
					}
				}
			}
			if(curr_irqinfo->msck_status==good&&prevstatus!=good)
			{
				if(curr_device->reoperfunc)
					curr_device->reoperfunc(curr_device->dev_ptr,
								(curr_device->read_irqinfo==curr_irqinfo),
								prevstatus);
				if(curr_device->category==network_device&&
				   curr_device->write_irqinfo==curr_irqinfo)
				{
					net_device *dev=(net_device *)curr_device->dev_ptr;
					if(dev->flags&IFF_UP)
						netif_start_queue(dev);
				}
				chandev_add_to_startmsck_list(&mscklist_head,curr_device->devname,
							      prevstatus,curr_irqinfo->msck_status);
			}
		}
		/* This is required because the device can go & come back */
                /* even before we realize it is gone owing to the waits in our kernel threads */
		/* & the device will be marked as not owned but its status will be good */
                /* & an attempt to accidently reprobe it may be done. */ 
		remove:
		chandev_remove(chandev_get_by_irq(curr_irqinfo->irq));
		
	}
	/* extra sanity */
	for_each(curr_chandev,(chandev *)chandev_head.head)
		if(curr_chandev->owned)
			chandev_remove(curr_chandev);
	for_each(curr_force,chandev_force_head)
	{
		for_each(read_chandev,(chandev *)chandev_head.head)
			if(read_chandev->devno==curr_force->read_devno&&
				!chandev_active(curr_force->read_devno))
			{
				for_each(write_chandev,(chandev *)chandev_head.head)
					if(write_chandev->devno==
					   curr_force->write_devno&&
					   !chandev_active(curr_force->write_devno))
					{
						if(chandev_doprobe(curr_force,
								read_chandev,
								write_chandev)==0)
						{
							chandev_remove(read_chandev);
							chandev_remove(write_chandev);
							goto chandev_probe_skip;
						}
					}
			}
	chandev_probe_skip:
	}
	for_each(curr_chandev,(chandev *)chandev_head.head)
	{
		for_each(curr_noauto,chandev_noauto_head)
		{
			if(curr_chandev->devno>=curr_noauto->lo_devno&&
			   curr_chandev->devno<=curr_noauto->hi_devno)
			{
				chandev_remove(curr_chandev);
				break;
			}
		}
	}
	for_each(curr_chandev,(chandev *)chandev_head.head)
	{
		if(curr_chandev->next&&curr_chandev->model_info==
		   curr_chandev->next->model_info)
		{
			
			chandev_doprobe(NULL,curr_chandev,curr_chandev->next);
			curr_chandev=curr_chandev->next;
		}
	}
	chandev_unlock();
	chandev_remove_all();
}

static void chandev_not_oper_func(int irq,int status)
{
	chandev_irqinfo *curr_irqinfo;
	chandev_activelist *curr_device;
	
	chandev_lock();
	for_each(curr_irqinfo,chandev_irqinfo_head)
		if(curr_irqinfo->irq==irq)
		{
			switch(status)
			{
				/* Currently defined but not used in kernel */
				/* Despite being in specs */
			case DEVSTAT_NOT_OPER:
				curr_irqinfo->msck_status=not_oper;
				break;
#ifdef DEVSTAT_NO_PATH
				/* Kernel hasn't this defined currently. */
				/* Despite being in specs */
			case DEVSTAT_NO_PATH:
				curr_irqinfo->msck_status=no_path;
				break;
#endif
			case DEVSTAT_REVALIDATE:
				curr_irqinfo->msck_status=revalidate;
				break;
			case DEVSTAT_DEVICE_GONE:
				curr_irqinfo->msck_status=gone;
				break;
			}
			for_each(curr_device,chandev_activelist_head)
			{
				if(curr_device->write_irqinfo==curr_irqinfo)
				{
					if(curr_device->category==network_device)
					{
						net_device *dev=(net_device *)curr_device->dev_ptr;
						if(dev->flags&IFF_UP)
							netif_stop_queue(dev);
					}
				}
				break;
			}
			break;
		}
	chandev_unlock();
}


static chandev_task_retval chandev_msck_task(void *unused)
{
	int loopcnt,not_oper_probe_required=FALSE;
	wait_queue_head_t    wait;
	chandev_not_oper_struct *new_not_oper;

	chandev_daemonize("chandev_msck_kernel_thread",0,TRUE);
	/* This loop exists because machine checks tend to come in groups & we have
           to wait for the other devnos to appear also */
	init_waitqueue_head(&wait);
	for(loopcnt=0;loopcnt<10||(jiffies-chandev_last_machine_check)<HZ;loopcnt++)
	{
		sleep_on_timeout(&wait,HZ);
	}
	atomic_set(&chandev_msck_thread_lock,1);
	while(!atomic_compare_and_swap(TRUE,FALSE,&chandev_new_msck));
	{
		chandev_probe();
	}
	while(TRUE)
	{
		
		unsigned long        flags; 
		spin_lock_irqsave(&chandev_not_oper_spinlock,flags);
		new_not_oper=(chandev_not_oper_struct *)dequeue_head(&chandev_not_oper_head);
		spin_unlock_irqrestore(&chandev_not_oper_spinlock,flags);
		if(new_not_oper)
		{
			chandev_not_oper_func(new_not_oper->irq,new_not_oper->status);
			not_oper_probe_required=TRUE;
			kfree(new_not_oper);
		}
		else
			break;
	}
	if(not_oper_probe_required)
		chandev_probe();
	chandev_task_return(0);
}



#if CHANDEV_USE_KERNEL_THREADS
static void chandev_start_msck_thread(void *unused)
{
	/* tq_scheduler sometimes leaves interrupts disabled from do bottom half */
	__sti();
	kernel_thread((int (*)(void *))chandev_msck_task,
		      (void*)NULL,0);
}
#endif



static char *argstrs[]=
{
	"noauto",
	"del_noauto",
	"ctc",
	"escon",
	"lcs",
	"osad",
	"qeth",
	"claw",
	"add_parms",
	"del_parms",
	"del_force",
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	"use_devno_names",
	"dont_use_devno_names",
#endif
	"add_model",
	"del_model",
	"auto_msck",
	"del_auto_msck",
	"del_all_models",
	"reset_conf_clean",
	"reset_conf",
	"shutdown",
	"reprobe",
	"unregister_probe",
	"read_conf",
	"dont_read_conf",
};

typedef enum
{
	stridx_mult=256,
	first_stridx=0,
	noauto_stridx=first_stridx,
	del_noauto_stridx,
	ctc_stridx,
	escon_stridx,
	lcs_stridx,
	osad_stridx,
        qeth_stridx,
	claw_stridx,
	add_parms_stridx,
	del_parms_stridx,
	del_force_stridx,
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	use_devno_names_stridx,
	dont_use_devno_names_stridx,
#endif
	add_model_stridx,
	del_model_stridx,
	auto_msck_stridx,
	del_auto_msck_stridx,
	del_all_models_stridx,
	reset_conf_clean_stridx,
	reset_conf_stridx,
	shutdown_stridx,
	reprobe_stridx,
	unregister_probe_stridx,
	read_conf_stridx,
	dont_read_conf_stridx,
	last_stridx,
} chandev_str_enum;

void chandev_add_noauto(u16 lo_devno,u16 hi_devno)
{
	chandev_noauto_range *new_range;

	if((new_range=chandev_alloc(sizeof(chandev_noauto_range))))
	{
		new_range->lo_devno=lo_devno;
		new_range->hi_devno=hi_devno;
		chandev_add_to_list((list **)&chandev_noauto_head,new_range);
	}
}


void chandev_add_msck_range(u16 lo_devno,u16 hi_devno,int auto_msck_recovery)
{
	chandev_msck_range *new_range;

	if((new_range=chandev_alloc(sizeof(chandev_msck_range))))
	{
		new_range->lo_devno=lo_devno;
		new_range->hi_devno=hi_devno;
		new_range->auto_msck_recovery=auto_msck_recovery;
		chandev_add_to_list((list **)&chandev_msck_range_head,new_range);
	}
}



static char chandev_keydescript[]=
"\nchan_type key bitfield ctc=0x1,escon=0x2,lcs=0x4,osad=0x8,qeth=0x10,claw=0x20\n";


#if  CONFIG_ARCH_S390X
/* We need this as we sometimes use this to evaluate pointers */
typedef long chandev_int; 
#else
typedef int chandev_int;
#endif


#if (LINUX_VERSION_CODE<KERNEL_VERSION(2,3,0)) || (CONFIG_ARCH_S390X)
/*
 * Read an int from an option string; if available accept a subsequent
 * comma as well.
 *
 * Return values:
 * 0 : no int in string
 * 1 : int found, no subsequent comma
 * 2 : int found including a subsequent comma
 */
static chandev_int chandev_get_option(char **str,chandev_int *pint)
{
    char *cur = *str;

    if (!cur || !(*cur)) return 0;
    *pint = simple_strtol(cur,str,0);
    if (cur==*str) return 0;
    if (**str==',') {
        (*str)++;
        return 2;
    }

    return 1;
}

static char *chandev_get_options(char *str, int nints, chandev_int *ints)
{
	int res,i=1;

    while (i<nints) {
        res = chandev_get_option(&str, ints+i);
        if (res==0) break;
        i++;
        if (res==1) break;
    }
	ints[0] = i-1;
	return(str);
}
#else
#define chandev_get_option get_option
#define chandev_get_options get_options
#endif

static int chandev_setup(char *instr,char *errstr,int lineno)
{
	chandev_strval   val=isnull;
	chandev_str_enum stridx;
	long             endlong;
	chandev_type     chan_type;
	char             *str,*currstr,*interpretstr=NULL;
	int              cnt,strcnt;
#define CHANDEV_MAX_EXTRA_INTS 8
	chandev_int ints[CHANDEV_MAX_EXTRA_INTS+1];
	memset(ints,0,sizeof(ints));
	currstr=alloca(strlen(instr)+1);
	strcpy(currstr,instr);
	strcnt=chandev_pack_args(currstr);
	for(cnt=1;cnt<=strcnt;cnt++)
	{
		interpretstr=currstr;
		for(stridx=first_stridx;stridx<last_stridx;stridx++)
		{
			str=currstr;
			if((val=chandev_strcmp(argstrs[stridx],&str,&endlong)))
				break;
		}
		currstr=str;
		if(val)
		{
			if(val&iscomma)
			{
				if(stridx==add_parms_stridx&&(val==(isstr|iscomma)))
				{
					str=currstr;
					if(chandev_get_option(&str,&ints[0])==2)
					{
						chandev_add_parms(ints[0],str);
						currstr=str+strlen(str)+1;
						continue;
					}
					else
						goto BadArgs;
				}
				else
					currstr=chandev_get_options(str,CHANDEV_MAX_EXTRA_INTS,ints)+1;
			}
			else
			{
				ints[0]=0;
				currstr++;
			}
			val=(((chandev_strval)stridx)*stridx_mult)+(val&~isstr);
			switch(val)
			{
			case noauto_stridx*stridx_mult:
			case (noauto_stridx*stridx_mult)|iscomma:
				switch(ints[0])
				{
				case 0: 
					chandev_free_all_list((list **)&chandev_noauto_head);
					chandev_add_noauto(0,0xffff);
					break;
				case 1:
					ints[2]=ints[1];
				case 2:
					chandev_add_noauto(ints[1],ints[2]);
					break;
				default:
					goto BadArgs;
				}
				break;
			case (auto_msck_stridx*stridx_mult)|iscomma:
				switch(ints[0])
				{
				case 1:
					chandev_free_all_list((list **)&chandev_msck_range_head);
					chandev_add_msck_range(0,0xffff,ints[1]);
					break;
				case 2:
					chandev_add_msck_range(ints[1],ints[1],ints[2]);
					break;
				case 3:
					chandev_add_msck_range(ints[1],ints[2],ints[3]);
					break;
				default:
					goto BadArgs;
					
				}
			case del_auto_msck_stridx*stridx_mult:
			case (del_auto_msck_stridx*stridx_mult)|iscomma:
				switch(ints[0])
				{
				case 0:
					chandev_free_all_list((list **)&chandev_msck_range_head);
					break;
				case 1:
					chandev_del_msck(ints[1]);
				default:
					goto BadArgs;
				}
			case del_noauto_stridx*stridx_mult:
				chandev_free_all_list((list **)&chandev_noauto_head);
				break;
			case (del_noauto_stridx*stridx_mult)|iscomma:
				if(ints[0]==1)
					chandev_del_noauto(ints[1]);
				else
					goto BadArgs;
				break;
			case (ctc_stridx*stridx_mult)|isnum|iscomma:
			case (escon_stridx*stridx_mult)|isnum|iscomma:
			case (lcs_stridx*stridx_mult)|isnum|iscomma:
			case (osad_stridx*stridx_mult)|isnum|iscomma:
			case (qeth_stridx*stridx_mult)|isnum|iscomma:
			case (claw_stridx*stridx_mult)|isnum|iscomma:
				switch(val)
				{
				case (ctc_stridx*stridx_mult)|isnum|iscomma:
					chan_type=ctc;
					break;
				case (escon_stridx*stridx_mult)|isnum|iscomma:
					chan_type=escon;
					break;
				case (lcs_stridx*stridx_mult)|isnum|iscomma:
					chan_type=lcs;
					break;
				case (osad_stridx*stridx_mult)|isnum|iscomma:
					chan_type=osad;
					break;
				case (qeth_stridx*stridx_mult)|isnum|iscomma:
					chan_type=qeth;
					break;
				case (claw_stridx*stridx_mult)|isnum|iscomma:
					chan_type=claw;
					break;
				default:
					goto BadArgs;
				}
				chandev_add_force(chan_type,endlong,ints[1],ints[2],
						  ints[3],ints[4],ints[5]);
				break;
			case (del_parms_stridx*stridx_mult):
				ints[1]=-1;
			case (del_parms_stridx*stridx_mult)|iscomma:
				if(ints[0]==1)
					ints[2]=FALSE;
				if(ints[0]>2)
					goto BadArgs;
				chandev_remove_parms(ints[1],ints[2]);
				break;
			case (del_force_stridx*stridx_mult)|iscomma:
				if(ints[0]!=1)
					goto BadArgs;
				chandev_del_force(ints[1]);
				break;
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
			case (use_devno_names_stridx*stridx_mult):
				use_devno_names=1;
				break;
			case (dont_use_devno_names_stridx*stridx_mult):
				use_devno_names=0;
#endif
			case (add_model_stridx*stridx_mult)|iscomma:
				if(ints[0]<3)
					goto BadArgs;
				if(ints[0]==3)
					ints[4]=-1;
				if(ints[0]<=4)
					ints[5]=-1;
				if(ints[0]<=5)
					ints[6]=-1;
				if(ints[0]<=6)
					ints[7]=default_msck_bits;
				ints[0]=7;
				chandev_add_model(ints[1],ints[2],ints[3],
						  ints[4],ints[5],ints[6],ints[7]);
				break;
			case (del_model_stridx*stridx_mult)|iscomma:
				if(ints[0]<2||ints[0]>4)
					goto BadArgs;
				if(ints[0]<3)
					ints[3]=-2;
				if(ints[0]<4)
					ints[4]=-2;
				ints[0]=4;
				chandev_del_model(ints[1],ints[2],ints[3],ints[4]);
				break;
			case del_all_models_stridx*stridx_mult:
				chandev_remove_all_models();
				break;
			case reset_conf_stridx*stridx_mult:
				chandev_reset();
				chandev_init_default_models();
				break;
			case reset_conf_clean_stridx*stridx_mult:
				chandev_reset();
				break;
			case shutdown_stridx*stridx_mult:
				chandev_shutdown_all();
				break;
			case (shutdown_stridx*stridx_mult)|iscomma:
				switch(ints[0])
				{
				case 0:
					if(strlen(str))
						chandev_shutdown_by_name(str);
					else
						goto BadArgs;
					break;
				case 1:
					chandev_shutdown_by_devno(ints[1]);
					break;
				default:
					goto BadArgs;
				}
				break;
			case reprobe_stridx*stridx_mult:
				chandev_probe();
				break;
			case unregister_probe_stridx*stridx_mult:
				chandev_free_all_list((list **)&chandev_probelist_head);
				break;
			case (unregister_probe_stridx*stridx_mult)|iscomma:
				if(ints[0]!=1)
					goto BadArgs;
				chandev_unregister_probe((chandev_probefunc)ints[1]);
				break;
			case read_conf_stridx*stridx_mult:
				chandev_read_conf();
				break;
			case dont_read_conf_stridx*stridx_mult:
				chandev_conf_read=TRUE;
				break;
			default:
				goto BadArgs;
			}		
		}
		else
			goto BadArgs;
	}
	return(1);
 BadArgs:
	printk("chandev_setup bad argument %s",instr);
	if(errstr)
	{
                printk("%s %d interpreted as %s",errstr,lineno,interpretstr);
		if(strcnt>1)
			printk(" before semicolon no %d",cnt);
	}
	printk(".\n Type man chandev for more info.\n\n");
	return(0);
}
#define CHANDEV_KEYWORD "chandev="
static int chandev_setup_bootargs(char *str,int paramno)
{
	int len;

	char *copystr;
	for(len=0;str[len]!=0&&!isspace(str[len]);len++);
	copystr=alloca(len+1);
	strncpy(copystr,str,len);
	copystr[len]=0;
	if(chandev_setup(copystr,"at "CHANDEV_KEYWORD" bootparam no",paramno)==0)
		return(0);
	return(len);

}

/*
  We can't parse using a __setup function as kmalloc isn't available
  at this time.
 */
static void __init chandev_parse_args(void)
{
#define CHANDEV_KEYWORD "chandev="
	extern char saved_command_line[];
	int cnt,len,paramno=1;

	len=strlen(saved_command_line)-sizeof(CHANDEV_KEYWORD);
	for(cnt=0;cnt<len;cnt++)
	{
		if(strncmp(&saved_command_line[cnt],CHANDEV_KEYWORD,
			   sizeof(CHANDEV_KEYWORD)-1)==0)
		{
			cnt+=(sizeof(CHANDEV_KEYWORD)-1);	
			cnt+=chandev_setup_bootargs(&saved_command_line[cnt],paramno);
			paramno++;
		}
	}
}

int chandev_do_setup(char *buff,int size)
{
	int curr,comment=FALSE,newline=FALSE,oldnewline=TRUE;
	char *startline=NULL,*endbuff=&buff[size];

	int lineno=0;

	*endbuff=0;
	for(;buff<=endbuff;curr++,buff++)
	{
		if(*buff==0xa||*buff==0xc||*buff==0)
		{
			if(*buff==0xa||*buff==0)
				lineno++;
			*buff=0;
			newline=TRUE;
		}
		else
		{ 
			newline=FALSE;
			if(*buff=='#')
				comment=TRUE;
		}
		if(comment==TRUE)
			*buff=0;
		if(startline==NULL&&isalpha(*buff))
			startline=buff;
		if(startline&&(buff>startline)&&(oldnewline==FALSE)&&(newline==TRUE))
		{
			if((chandev_setup(startline," on line no",lineno))==0)
				return(-EINVAL);
			startline=NULL;
		}
		if(newline)
			comment=FALSE;
	        oldnewline=newline;
	}
	return(0);
}


static void chandev_read_conf(void)
{
#define CHANDEV_FILE "/etc/chandev.conf"
	struct stat statbuf;
	char        *buff;
	int         curr,left,len,fd;

	if(in_interrupt()||current->fs->root==NULL)
		return;
	chandev_conf_read=TRUE;
	set_fs(KERNEL_DS);
	if(stat(CHANDEV_FILE,&statbuf)==0)
	{
		set_fs(USER_DS);
		buff=vmalloc(statbuf.st_size+1);
		if(buff)
		{
			set_fs(KERNEL_DS);
			if((fd=open(CHANDEV_FILE,O_RDONLY,0))!=-1)
			{
				curr=0;
				left=statbuf.st_size;
				while((len=read(fd,&buff[curr],left))>0)
				{
					curr+=len;
					left-=len;
				}
				close(fd);
			}
			set_fs(USER_DS);
			chandev_do_setup(buff,statbuf.st_size);
			vfree(buff);
		}
	}
	set_fs(USER_DS);
}

static void chandev_read_conf_if_necessary(void)
{
	if(!chandev_conf_read)
		chandev_read_conf();
}

#ifdef CONFIG_PROC_FS
#define chandev_printf(exitchan,args...)     \
splen=sprintf(spbuff,##args);                \
spoffset+=splen;                             \
if(spoffset>offset) {                        \
       spbuff+=splen;                        \
       currlen+=splen;                       \
}                                            \
if(currlen>=length)                          \
       goto exitchan;

void sprintf_msck(char *buff,int auto_msck_recovery)
{
	chandev_msck_status idx;
	int first_time=TRUE;
	buff[0]=0;
	for(idx=first_msck;idx<last_msck;idx++)
	{
		if((1<<(idx-1))&auto_msck_recovery)
		{
			buff+=sprintf(buff,"%s%s",(first_time ? "":","),
				      msck_status_strs[idx]);
			first_time=FALSE;
		}
	}
}

static int chandev_read_proc(char *page, char **start, off_t offset,
			  int length, int *eof, void *data)
{
	char *spbuff=*start=page;
	int    currlen=0,splen=0;
	off_t  spoffset=0;
	chandev_model_info *curr_model;
	chandev_noauto_range *curr_noauto;
	chandev_force *curr_force;
	chandev_activelist *curr_device;
	chandev_probelist  *curr_probe;
	chandev_msck_range *curr_msck_range;
	s390_dev_info_t   curr_devinfo;
	int pass,chandevs_detected,curr_irq,loopcnt;
	chandev_irqinfo *read_irqinfo,*write_irqinfo;
	char buff[40],buff2[80];    

	chandev_lock();
	chandev_read_conf_if_necessary();
	chandev_printf(chan_exit,"\n%s\n"
		       "*'s for cu/dev type/models indicate don't cares\n",chandev_keydescript);
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
	chandev_printf(chan_exit,"\nuse_devno_names: %s\n\n",use_devno_names ? "on":"off");
#endif
	if(chandev_models_head)
	{
		chandev_printf(chan_exit,"Channels enabled for detection\n");      
		chandev_printf(chan_exit,"  chan     cu      cu     dev   dev    max     auto recovery\n");
		chandev_printf(chan_exit,"  type    type    model  type  model  port_no.      type    \n");
		chandev_printf(chan_exit,"============================================================\n");
		for_each(curr_model,chandev_models_head)
		{
			
			
			chandev_sprint_devinfo(buff,curr_model->cu_type,
					       curr_model->cu_model,
					       curr_model->dev_type,
					       curr_model->dev_model);
			sprintf_msck(buff2,curr_model->auto_msck_recovery);
			chandev_printf(chan_exit,"  0x%02x  %s%3d %s\n",
				       curr_model->chan_type,buff,
				       (int)curr_model->max_port_no,buff2);         
		}
	}
        
	if(chandev_noauto_head)
	{
		chandev_printf(chan_exit,"\nNo auto devno ranges\n");
		chandev_printf(chan_exit,"   From        To   \n");
		chandev_printf(chan_exit,"====================\n");
		for_each(curr_noauto,chandev_noauto_head)
		{
			chandev_printf(chan_exit,"  0x%04x     0x%04x\n",
				       curr_noauto->lo_devno,
				       curr_noauto->hi_devno);
		}
	}
	if(chandev_msck_range_head)
	{
		
		chandev_printf(chan_exit,"\nAutomatic machine check recovery devno ranges\n");
		chandev_printf(chan_exit,"   From        To   automatic recovery type\n");
		chandev_printf(chan_exit,"===========================================\n");
		for_each(curr_msck_range,chandev_msck_range_head)
		{
			sprintf_msck(buff2,curr_msck_range->auto_msck_recovery);
			chandev_printf(chan_exit,"  0x%04x     0x%04x %s\n",
				       curr_msck_range->lo_devno,
				       curr_msck_range->hi_devno,buff2)
		}
	}
	if(chandev_force_head)
	{
		chandev_printf(chan_exit,"\nForced devices\n");
		chandev_printf(chan_exit,"  chan defif read   write     port         ip    hw\n");
		chandev_printf(chan_exit,"  type  num  devno  devno  protocol no.  chksum stats\n");
		chandev_printf(chan_exit,"======================================================\n");
		for_each(curr_force,chandev_force_head)
		{
			chandev_printf(chan_exit,"  0x%02x  %3d  0x%04x 0x%04x       %3d       %1d    %1d\n",
				       curr_force->chan_type,curr_force->devif_num,
				       curr_force->read_devno,curr_force->write_devno,
				       curr_force->port_protocol_no,curr_force->checksum_received_ip_pkts,
				       curr_force->use_hw_stats);
		}
	}
	if(chandev_probelist_head)
	{
#if CONFIG_ARCH_S390X
		chandev_printf(chan_exit,"\nRegistered probe functions\n"
			       		 "probefunc            shutdownfunc         reoperfunc         chan  devices\n"
                                         "                                                             type   found\n"
			                 "==========================================================================\n");
#else
		chandev_printf(chan_exit,"\nRegistered probe functions\n"
			                 "probefunc   shutdownfunc  reoperfunc chan  devices\n"
                                         "                                     type   found\n"
			                 "==================================================\n");
#endif
		for_each(curr_probe,chandev_probelist_head)
		{
			chandev_printf(chan_exit,"0x%p   0x%p   0x%p   0x%02x      %d\n",
				       curr_probe->probefunc,
				       curr_probe->shutdownfunc,
				       curr_probe->reoperfunc,
				       curr_probe->chan_type,
				       curr_probe->devices_found);
		}
	}
	if(chandev_activelist_head)
	{
#if CONFIG_ARCH_S390X
		chandev_printf(chan_exit,
			       "\nInitialised Devices\n"
			       " read   write  read   write chan port  dev             dev        read msck   write msck\n"
			       " irq    irq    devno  devno type no.   ptr             name        status      status   \n"
			       "========================================================================================\n");
#else
		chandev_printf(chan_exit,
			       "\nInitialised Devices\n"
			       " read   write  read   write chan port  dev     dev        read msck   write msck\n"
			       " irq    irq    devno  devno type no.   ptr     name        status      status   \n"
			       "================================================================================\n");
#endif
		/* We print this list backwards for cosmetic reasons */
		for(curr_device=chandev_activelist_head;
		    curr_device->next!=NULL;curr_device=curr_device->next);
		while(curr_device)
		{
			read_irqinfo=curr_device->read_irqinfo;
			write_irqinfo=curr_device->write_irqinfo;
			chandev_printf(chan_exit,
				       "0x%04x 0x%04x 0x%04x 0x%04x 0x%02x %2d 0x%p %-10s   %-12s %-12s\n",
				       curr_device->read_irqinfo->irq,curr_device->write_irqinfo->irq,
				       (int)read_irqinfo->devno,
				       (int)write_irqinfo->devno,
				       curr_device->chan_type,(int)curr_device->port_no,
				       curr_device->dev_ptr,curr_device->devname,
				       msck_status_strs[read_irqinfo->msck_status],
				       msck_status_strs[write_irqinfo->msck_status]);
			get_prev((list *)chandev_activelist_head,
				 (list *)curr_device,
				 (list **)&curr_device);
		}
	}
	chandevs_detected=FALSE;
	for(pass=FALSE;pass<=TRUE;pass++)
	{
		if(pass&&chandevs_detected)
		{
			chandev_printf(chan_exit,"\nchannels detected\n");
			chandev_printf(chan_exit,"              chan    cu    cu   dev    dev   in   chandev\n");
			chandev_printf(chan_exit,"  irq  devno  type   type  model type  model  use    reg.\n");
			chandev_printf(chan_exit,"==========================================================\n");
		}
		for(curr_irq=get_irq_first(),loopcnt=0;curr_irq>=0; curr_irq=get_irq_next(curr_irq),loopcnt++)
		{
			if(loopcnt>0x10000)
			{
				printk(KERN_ERR"chandev_read_proc detected infinite loop bug in get_irq_next\n");
				goto chan_error;
			}
			if((curr_model=chandev_is_chandev(curr_irq,&curr_devinfo)))
			{
				chandevs_detected=TRUE;
				if(pass)
				{
					chandev_printf(chan_exit,"0x%04x 0x%04x 0x%02x  0x%04x 0x%02x  0x%04x 0x%02x  %-5s %-5s\n",
						       curr_irq,curr_devinfo.devno,
						       curr_model->chan_type,
						       (int)curr_devinfo.sid_data.cu_type,
						       (int)curr_devinfo.sid_data.cu_model,
						       (int)curr_devinfo.sid_data.dev_type,
						       (int)curr_devinfo.sid_data.dev_model,
						       (curr_devinfo.status&DEVSTAT_DEVICE_OWNED) ? "yes":"no",
						       (chandev_get_irqinfo_by_irq(curr_irq) ? "yes":"no"));
						       
						       
				}
					
			}

		}
	}
	if(chandev_parms_head)
	{
		chandev_parms      *curr_parms;

		chandev_printf(chan_exit,"\n driver specific parameters\n");
		chandev_printf(chan_exit,"chan    driver\n");
		chandev_printf(chan_exit,"type  parameters\n");
		chandev_printf(chan_exit,"=============================================================================\n");
		for_each(curr_parms,chandev_parms_head)
		{
			chandev_printf(chan_exit,"0x%02x    %s\n",
				       curr_parms->chan_type,
				       curr_parms->parmstr);
		}
	}
 chan_error:
	*eof=TRUE;
 chan_exit:
	if(currlen>length) {
		/* rewind to previous printf so that we are correctly
		 * aligned if we get called to print another page.
                 */
		currlen-=splen;
	}
	chandev_unlock();
	return(currlen);
}


static int chandev_write_proc(struct file *file, const char *buffer,
			   unsigned long count, void *data)
{
	int         rc;
	char        *buff;
	
	chandev_read_conf_if_necessary();
	buff=vmalloc(count+1);
	if(buff)
	{
		rc = copy_from_user(buff,buffer,count);
		if (rc)
			goto chandev_write_exit;
		chandev_do_setup(buff,count);
		rc=count;
	chandev_write_exit:
		vfree(buff);
		return rc;
	}
	else
		return -ENOMEM;
	return(0);
}

static void __init chandev_create_proc(void)
{
	struct proc_dir_entry *dir_entry=
		create_proc_entry("chandev",0644,
				  &proc_root);
	if(dir_entry)
	{
		dir_entry->read_proc=&chandev_read_proc;
		dir_entry->write_proc=&chandev_write_proc;
	}
}


#endif
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
static  
#endif
int __init chandev_init(void)
{
	if(!chandev_initialised)
	{
		chandev_parse_args();
		chandev_init_default_models();
#if CONFIG_PROC_FS
		chandev_create_proc();
#endif
		chandev_msck_task_tq.routine=
#if CHANDEV_USE_KERNEL_THREADS
		chandev_start_msck_thread;
#else
		chandev_msck_task;
#endif
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
		INIT_LIST_HEAD(&chandev_msck_task_tq.list);
		chandev_msck_task_tq.sync=0;
#endif
		chandev_msck_task_tq.data=NULL;
		chandev_last_startmsck_list_update=chandev_last_machine_check=jiffies-HZ;
		atomic_set(&chandev_msck_thread_lock,1);
		chandev_lock_owner=CHANDEV_INVALID_LOCK_OWNER;
		chandev_lock_cnt=0;
		spin_lock_init(&chandev_spinlock);
		spin_lock_init(&chandev_not_oper_spinlock);
		chandev_initialised=TRUE;
		atomic_set(&chandev_new_msck,FALSE);
	}
	return(0);
}
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,3,0)
__initcall(chandev_init);
#endif

int chandev_register_and_probe(chandev_probefunc probefunc,
			       chandev_shutdownfunc shutdownfunc,
			       chandev_reoperfunc reoperfunc,
			       chandev_type chan_type)
{
	chandev_probelist *new_probe;
	/* Avoid chicked & egg situations where we may be called before we */
	/* are initialised. */

	chandev_interrupt_check();
	if(!chandev_initialised)
		chandev_init();
	chandev_read_conf_if_necessary();
	if((new_probe=chandev_alloc(sizeof(chandev_probelist))))
	{
		new_probe->probefunc=probefunc;
		new_probe->shutdownfunc=shutdownfunc;
		new_probe->reoperfunc=reoperfunc;
		new_probe->chan_type=chan_type;
		new_probe->devices_found=0;
		chandev_add_to_list((list **)&chandev_probelist_head,new_probe);
		chandev_probe();
	}
	return(new_probe ? new_probe->devices_found:0);
}

void chandev_unregister(chandev_probefunc probefunc,int call_shutdown)
{
	chandev_probelist *curr_probe=NULL;
	chandev_activelist *curr_device;
	
	chandev_interrupt_check();
	chandev_lock();
	for_each(curr_probe,chandev_probelist_head)
	{
		if(curr_probe->probefunc==probefunc)
		{
			for_each(curr_device,chandev_activelist_head)
				if(curr_device->probefunc==probefunc)
				{
					if(call_shutdown)
					{
						chandev_shutdown(curr_device);
					}
				}
			chandev_free_listmember((list **)&chandev_probelist_head,
						(list *)curr_probe);
		}
	}
	chandev_unlock();
}

