/*
 *  drivers/s390/misc/chandev.c
 *    common channel device layer
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 */

#define __KERNEL_SYSCALLS__
#include <linux/config.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <asm/queue.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <asm/irq.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <asm/chandev.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>

static chandev_model_info *chandev_models_head=NULL;
static chandev *chandev_head=NULL;
static chandev_noauto_range *chandev_noauto_head=NULL;
static chandev_force *chandev_force_head=NULL;
static chandev_probelist *chandev_probelist_head=NULL;
static int use_devno_names=FALSE;
static int chandev_conf_read=FALSE;

static void *chandev_alloc_listmember(list **listhead,size_t size)
{
	void *newmember=kmalloc(size, GFP_KERNEL);
	if(newmember)
		add_to_list(listhead,newmember);
	return(newmember);
}

void chandev_free_listmember(list **listhead,list *member)
{
	if(remove_from_list(listhead,member))
		kfree(member);
	else
		printk(KERN_CRIT"chandev_free_listmember detected nonexistant"
		       "listmember listhead=%p member %p\n",listhead,member);
}

void chandev_free_all(list **listhead)
{
	while(*listhead)
		chandev_free_listmember(listhead,*listhead);
}

void chandev_add_model(chandev_type chan_type,u16 cu_type,u8 cu_model,u8 max_port_no)
{
	chandev_model_info *newmodel;

	if((newmodel=chandev_alloc_listmember(
		(list **)&chandev_models_head,sizeof(chandev_model_info))))
	{
		newmodel->chan_type=chan_type;
		newmodel->cu_type=cu_type;
		newmodel->cu_model=cu_model;
		newmodel->max_port_no=max_port_no;
	}
}


void chandev_remove(chandev *member)
{
	chandev_free_listmember((list **)&chandev_head,(list *)member);
}


void chandev_remove_all(void)
{
	chandev_free_all((list **)&chandev_head);
}

void chandev_remove_model(chandev_model_info *model)
{
	chandev *curr_chandev;
	for(curr_chandev=chandev_head;curr_chandev!=NULL;
		    curr_chandev=curr_chandev->next)
		if(curr_chandev->model_info==model)
			chandev_remove(curr_chandev);
	chandev_free_listmember((list **)&chandev_models_head,(list *)model);
}

void chandev_remove_all_models(void)
{
	while(chandev_models_head)
		chandev_remove_model(chandev_models_head);
}

void chandev_del_model(u16 cu_type,u8 cu_model)
{
	chandev_model_info *curr_model;
	for(curr_model=chandev_models_head;curr_model!=NULL;
		    curr_model=curr_model->next)
		if(curr_model->cu_type==cu_type&&curr_model->cu_model==cu_model)
			chandev_remove_model(curr_model);			
}

static void chandev_init_default_models(void)
{
	/* P390/Planter 3172 emulation assume maximum 16 to be safe. */
	chandev_add_model(lcs,0x3088,0x1,15);	

	/* 3172/2216 Paralell the 2216 allows 16 ports per card the */
	/* the original 3172 only allows 4 we will assume the max of 16 */
	chandev_add_model(lcs|ctc,0x3088,0x8,15);

	/* 3172/2216 Escon serial the 2216 allows 16 ports per card the */
	/* the original 3172 only allows 4 we will assume the max of 16 */
	chandev_add_model(lcs|escon,0x3088,0x1F,15);

	/* Only 2 ports allowed on OSA2 cards model 0x60 */
	chandev_add_model(lcs,0x3088,0x60,1);

	/* Osa-D we currently aren't too emotionally involved with this */
	chandev_add_model(osad,0x3088,0x62,0);
}

void chandev_add(dev_info_t  *newdevinfo,chandev_model_info *newmodelinfo)
{
	chandev *new_chandev;

	if((new_chandev=chandev_alloc_listmember(
		(list **)&chandev_head,sizeof(chandev))))
	{
		new_chandev->model_info=newmodelinfo;
		new_chandev->devno=newdevinfo->devno;
		new_chandev->irq=newdevinfo->irq;
	}
}


void chandev_collect_devices(void)
{
	int curr_irq,loopcnt=0,err;
	dev_info_t   curr_devinfo;
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
		if((err=get_dev_info_by_irq(curr_irq,&curr_devinfo)))
		{
			printk("chandev_collect_devices get_dev_info_by_irq reported err=%X on irq %d\n"
			       "should not happen\n",err,curr_irq);
			continue;
		}
		for(curr_model=chandev_models_head;curr_model!=NULL;
		    curr_model=curr_model->next)
		{
			if((curr_model->cu_type==curr_devinfo.sid_data.cu_type)&&
			   (curr_model->cu_model==curr_devinfo.sid_data.cu_model)
			   &&((curr_devinfo.status&DEVSTAT_DEVICE_OWNED)==0))
				chandev_add(&curr_devinfo,curr_model);
		}
	}
}

void chandev_add_force(chandev_type chan_type,s32 devif_num,u16 read_devno,
u16 write_devno,s16 port_no,u8 do_ip_checksumming,u8 use_hw_stats)

{
	chandev_force *new_chandev_force;

	if((new_chandev_force=chandev_alloc_listmember(
		(list **)&chandev_force_head,sizeof(chandev_force))))
	{
		new_chandev_force->chan_type=chan_type;
		new_chandev_force->devif_num=devif_num;
		new_chandev_force->read_devno=read_devno;
		new_chandev_force->write_devno=write_devno;
		new_chandev_force->port_no=port_no;
		new_chandev_force->do_ip_checksumming=do_ip_checksumming;
		new_chandev_force->use_hw_stats=use_hw_stats;
	}
}

void chandev_del_force(u16 read_devno)
{
	chandev_force *curr_force;
	for(curr_force=chandev_force_head;curr_force!=NULL;
		    curr_force=curr_force->next)
	{
		if(curr_force->read_devno==read_devno)
			chandev_free_listmember((list **)&chandev_force_head,
						(list *)curr_force);
	}
}

void chandev_pack_args(char *str)
{
	char *newstr=str;
	while(*str)
	{
		if(isspace(*str))
			str++;
		else
			*newstr++=*str++;
	}
	*newstr=0;
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
	char *cur=*str;
	chandev_strval  retval=isnull;

	int len=strlen(teststr);
	if(strncmp(teststr,*str,len)==0)
	{
		*str+=len;
		retval=isstr;
		*endlong=simple_strtol(cur,str,0);
		if(cur!=*str)
			retval|=isnum;
		if(**str==',')
			retval|=iscomma;
	}
	return(retval);
}

static char *argstrs[]=
{
	"noauto",
	"lcs",
	"ctc",
	"escon",
	"del_force",
	"use_devno_names"
	"dont_use_devno_names",
	"add_model"
	"del_model"
	"del_all_models"
};

typedef enum
{
	stridx_mult=16,
	first_stridx=0,
	noauto_stridx=first_stridx,
	lcs_stridx,
	ctc_stridx,
	escon_stridx,
	del_force_stridx,
	use_devno_names_stridx,
	dont_use_devno_names_stridx,
	add_model_stridx,
	del_model_stridx,
	del_all_models_stridx,
	last_stridx,
} chandev_str_enum;

void chandev_add_noauto(u16 lo_devno,u16 hi_devno)
{
	chandev_noauto_range *new_range;

	if((new_range=chandev_alloc_listmember(
		(list **)&chandev_noauto_head,sizeof(chandev_noauto_range))))
	{
		new_range->lo_devno=lo_devno;
		new_range->hi_devno=hi_devno;
	}
}

static char chandev_keydescript[]=
"chan_type key bitfield\nctc=0x1,escon=0x2,lcs=0x4,lcs=0x4,osad=0x8,claw=0x16\n";

static void chandev_print_args(void)
{
	printk("valid chandev arguments are" 
                "<> indicate optional parameters | indicate a choice.\n");
	printk("noauto,<lo_devno>,<hi_devno>\n"
               "don't probe a range of device numbers for channel devices\n");
	printk("lcs|ctc|escon<devif_num>,read_devno,write_devno,<port_no>,"
	       "<do_ip_checksumming>,<use_hw_stats>\n");
	printk("e.g. ctc0,0x7c00,0x7c01,-1,0,0\n");
	printk("     tells the channel layer to force ctc0 if detected to use\n"
	       "     cuu's 7c00 & 7c01 port ( rel adapter no ) is invalid for\n"
	       "     ctc's so use -1 don't do checksumming on received ip\n"
               "     packets & as ctc doesn't have hardware stats ignore this\n"
               "     parameter\n\n");
	printk("del_force read_devno\n"
       "delete a forced channel device from force list.\n");
	printk("use_devno_names, tells the channel layer to assign device\n"
               "names based on the read channel cuu number\n"
	       "e.g. a token ring read channel 0x7c00 would have an interface"
	       "called tr0x7c00 this avoids name collisions on devices.");
	printk("add_model chan_type cu_model max_port no\n"
	       "tells the channel layer to probe for the device described\n");
	printk("%s use max_port_no of 0 for devices where this field "
       "is invalid.\n",chandev_keydescript);
	printk("del_model cu_type cu_model\n");
	printk("del_all_models\n");
}


static int chandev_setup(char *str)
{
	chandev_strval   val=isnull;
	chandev_str_enum stridx;
	long             endlong;
	chandev_type     chan_type;
#define CHANDEV_MAX_EXTRA_INTS 5
	int ints[CHANDEV_MAX_EXTRA_INTS+1];
	memset(ints,0,sizeof(ints));
	chandev_pack_args(str);
	for(stridx=first_stridx;stridx<last_stridx;stridx++)
		if((val=chandev_strcmp(argstrs[stridx],&str,&endlong)))
			break;
	if(val)
	{
		if(val&iscomma)
			get_options(str,CHANDEV_MAX_EXTRA_INTS,ints);
		else
			ints[0]=0;
		val=(((chandev_strval)stridx)*stridx_mult)+(val&~isstr);
		switch(val)
		{
		case noauto_stridx*stridx_mult:
		case (noauto_stridx*stridx_mult)|iscomma:
			switch(ints[0])
			{
			case 0: 
				chandev_free_all((list **)&chandev_noauto_head);
				chandev_add_noauto(0,0xffff);
				break;
			case 1:
				ints[2]=ints[1];
			case 2:
				chandev_add_noauto(ints[1],ints[2]);
			
			}
			break;
		case (ctc_stridx*stridx_mult)|isnum|iscomma:
		case (escon_stridx*stridx_mult)|isnum|iscomma:
		case (lcs_stridx*stridx_mult)|isnum|iscomma:
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
			default:
				goto BadArgs;
			}
			chandev_add_force(chan_type,endlong,ints[1],ints[2],
					  ints[3],ints[4],ints[5]);
			break;
		case (del_force_stridx*stridx_mult)|iscomma:
			if(ints[0]!=1)
				goto BadArgs;
			chandev_del_force(ints[1]);
			break;
		case (use_devno_names_stridx*stridx_mult):
			use_devno_names=1;
			break;
		case (dont_use_devno_names_stridx*stridx_mult):
			use_devno_names=0;
	        case (add_model_stridx*stridx_mult)|iscomma:
			if(ints[0]<3)
				goto BadArgs;
			if(ints[0]==3)
			{
				ints[0]=4;
				ints[4]=-1;
			}
			chandev_add_model(ints[1],ints[2],ints[3],ints[4]);
		break;
		case (del_model_stridx*stridx_mult)|iscomma:
			if(ints[0]!=2)
				goto BadArgs;
			chandev_del_model(ints[1],ints[2]);
			break;
		case del_all_models_stridx*stridx_mult:
			chandev_remove_all_models();
			break;
		default:
			goto BadArgs;
		}		
	}
	return(1);
 BadArgs:
	chandev_print_args();
	return(0);
}

__setup("chandev=",chandev_setup);

int chandev_doprobe(chandev_force *force,chandev *read_chandev,
chandev *write_chandev)
{
	chandev_probelist *probe;
	chandev_model_info *model_info;
	chandev_probeinfo probeinfo;
	int  retval=-1,hint=-1;

	model_info=read_chandev->model_info;
	if(read_chandev->model_info!=write_chandev->model_info||
	   (force&&((force->chan_type&model_info->chan_type)==0)))
		return(-1); /* inconsistent */
	for(probe=chandev_probelist_head;
	    probe!=NULL;
	    probe=probe->next)
	{
		if(probe->chan_type&model_info->chan_type)
		{
			if(use_devno_names)
				probeinfo.devif_num=read_chandev->devno;
			else
				probeinfo.devif_num=-1;
			probeinfo.read_irq=read_chandev->irq;
			probeinfo.write_irq=write_chandev->irq;
			
			probeinfo.max_port_no=model_info->max_port_no;
			if(force)
			{
				probeinfo.forced_port_no=force->port_no;
				if(force->devif_num!=-1)
					probeinfo.devif_num=force->devif_num;
				probeinfo.do_ip_checksumming=force->do_ip_checksumming;
				probeinfo.use_hw_stats=force->use_hw_stats;
				
			}
			else
			{
				probeinfo.forced_port_no=-1;
				probeinfo.do_ip_checksumming=FALSE;
				probeinfo.use_hw_stats=FALSE;
				if(probe->chan_type&lcs)
				{
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
			retval=probe->probefunc(&probeinfo);
			if(retval==0)
				break;
		}
	}
	return(retval);
}

void chandev_probe(void)
{
	chandev *read_chandev,*write_chandev,*curr_chandev;
	chandev_force *curr_force;
	chandev_noauto_range *curr_noauto;

	chandev_collect_devices();
	for(curr_force=chandev_force_head;curr_force!=NULL;
		    curr_force=curr_force->next)
	{
		for(read_chandev=chandev_head;
		    read_chandev!=NULL;
		    read_chandev=read_chandev->next)
			if(read_chandev->devno==curr_force->read_devno)
			{
				for(write_chandev=chandev_head;
				    write_chandev!=NULL;
				    write_chandev=write_chandev->next)
					if(write_chandev->devno==
					   curr_force->write_devno)
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
	for(curr_chandev=chandev_head;
		    curr_chandev!=NULL;
		    curr_chandev=curr_chandev->next)
	{
		for(curr_noauto=chandev_noauto_head;curr_noauto!=NULL;
		    curr_noauto=curr_noauto->next)
		{
			if(curr_chandev->devno>=curr_noauto->lo_devno&&
			   curr_chandev->devno<=curr_noauto->hi_devno)
			{
				chandev_remove(curr_chandev);
				break;
			}
		}
	}
	for(curr_chandev=chandev_head;curr_chandev!=NULL;
	    curr_chandev=curr_chandev->next)
	{
		if(curr_chandev->next&&curr_chandev->model_info==
		   curr_chandev->next->model_info)
		{
			
			chandev_doprobe(NULL,curr_chandev,curr_chandev->next);
			curr_chandev=curr_chandev->next;
		}
	}
	chandev_remove_all();
}

int chandev_do_setup(char *buff,int size)
{
	int curr,startline=0,comment=FALSE,newline=FALSE,oldnewline=TRUE;
	int rc=1;

	buff[size]=0;
	for(curr=0;curr<=size;curr++)
	{
		if(buff[curr]=='#')
		{
			comment=TRUE;
			newline=FALSE;
		}
		else if(buff[curr]==10||buff[curr]==13||buff[curr]==0)
		{
			buff[curr]=0;
			comment=FALSE;
			newline=TRUE;
		}
		if(comment==FALSE&&curr>startline
		   &&((oldnewline==TRUE&&newline==FALSE)||curr==size))
		{
			if((rc=chandev_setup(&buff[startline]))==0)
				break;
			startline=curr+1;
		}
	        oldnewline=newline;
	}
	return(rc);
}
void chandev_read_conf(void)
{
#define CHANDEV_FILE "/etc/chandev.conf"
	struct stat statbuf;
	char        *buff;
	int         curr,left,len,fd;

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

void chandev_register_and_probe(chandev_probefunc probefunc,chandev_type chan_type)
{
	chandev_probelist *new_probe;
	if(!chandev_conf_read)
		chandev_read_conf();
	if((new_probe=chandev_alloc_listmember((list **)&
		chandev_probelist_head,sizeof(chandev_probelist))))
	{
		new_probe->probefunc=probefunc;
		new_probe->chan_type=chan_type;
		chandev_probe();
	}
}

void chandev_unregister(chandev_probefunc probefunc)
{
	 chandev_probelist *curr_probe=NULL;
		
	for(curr_probe=chandev_probelist_head;curr_probe!=NULL;
		    curr_probe=curr_probe->next)
	{
		if(curr_probe->probefunc==probefunc)
			chandev_free_listmember((list **)&chandev_probelist_head,
						(list *)curr_probe);
	}
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



static int chandev_read_proc(char *page, char **start, off_t offset,
			  int length, int *eof, void *data)
{
	char *spbuff=*start=page;
	int    currlen=0,splen;
	off_t  spoffset=0;
	chandev_model_info *curr_model;
	chandev_noauto_range *curr_noauto;
	chandev_force *curr_force;


	chandev_printf(chan_exit,"Channels enabled for detection\n");      
	chandev_printf(chan_exit,"chan_type   cu_type       cu_model    max_port_no\n");
	chandev_printf(chan_exit,"=================================================\n");
	for(curr_model=chandev_models_head;curr_model!=NULL;
	    curr_model=curr_model->next)
	{
		chandev_printf(chan_exit,"0x%02x       0x%04x         0x%02x        %d\n",
		       curr_model->chan_type,(int)curr_model->cu_type,
		       (int)curr_model->cu_model,(int)curr_model->max_port_no);         
	}
	
        chandev_printf(chan_exit,"%s",chandev_keydescript);
	chandev_printf(chan_exit,"No auto devno ranges\n");
	chandev_printf(chan_exit,"   From        To   \n");
	chandev_printf(chan_exit,"====================\n");
	for(curr_noauto=chandev_noauto_head;curr_noauto!=NULL;
		    curr_noauto=curr_noauto->next)
	{
		chandev_printf(chan_exit,"0x%4x       0x%4x\n",
			       curr_noauto->lo_devno,
			       curr_noauto->hi_devno);
	}
	chandev_printf(chan_exit,"\nForced devices\n");
	chandev_printf(chan_exit,"chan_type defif_num read_devno write_devno port_no ip_cksum hw_stats\n");
	chandev_printf(chan_exit,"====================================================================\n");
	for(curr_force=chandev_force_head;curr_force!=NULL;
		    curr_force=curr_force->next)
	{
	chandev_printf(chan_exit,"0x%2x   %d  0x%4x 0x%4x  %4d    %1d   %1d\n",
		       curr_force->chan_type,curr_force->devif_num,
		       curr_force->read_devno,curr_force->write_devno,
		       curr_force->port_no,curr_force->do_ip_checksumming,
		       curr_force->use_hw_stats);
	}
	*eof=TRUE;
 chan_exit:
	if(currlen>length) {
		/* rewind to previous printf so that we are correctly
		 * aligned if we get called to print another page.
                 */
		currlen-=splen;
	}
	return(currlen);
}


static int chandev_write_proc(struct file *file, const char *buffer,
			   unsigned long count, void *data)
{
	int         rc;
	char        *buff;

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
static int __init chandev_init(void)
{
	chandev_init_default_models();
#if CONFIG_PROC_FS
	chandev_create_proc();
#endif
	return(0);
}
__initcall(chandev_init);







