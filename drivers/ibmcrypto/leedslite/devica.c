/* drivers/char/devica.c: An IBM Crypto Adapter Work Distributor for Linux. */
/* Copyright (c) International Business Machines Corp., 2001 */
/*
	Written 2000-2001 by Jon Grimm

	Version history:
	YYYY Mon DD First Lastname <email@host>
		Change Description
    2003 Nov 18 Serge Hallyn <sergeh@us.ibm.com>
	    Separate 2.4 and 2.6 driver code.
    2003 Nov 10 Serge Hallyn <sergeh@us.ibm.com>
    	    Update for 2.6 kernel (with chardev ripped out of devfs)
    2001 Mar 05 Jon Grimm <jgrimm@us.ibm.com>
	    Fix multi-thread bug (devica worker interface changed)
*/


#if !defined(__OPTIMIZE__)  ||  !defined(__KERNEL__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error You must compile this driver with "-O".
#endif

#include <linux/config.h>
#include <linux/version.h>

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/kmod.h>
#include <linux/list.h>

#include <asm/bitops.h>
#include <asm/io.h>
#include <linux/delay.h>

#include <linux/icaioctl.h>

/* Includes neccessary for PowerPC64 that provide functionality to 
   call system ioctls from the device driver
*/
#ifdef CONFIG_PPC64
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/file.h>
#include <asm/ioctls.h>
#endif

typedef struct devfs_entry *devfs_handle_t;

/* Dynamic ioctl32 compatability, necessary for PPC64 and possibly
   other 64 bit platforms
*/
#ifdef CONFIG_PPC64
extern int register_ioctl32_conversion(unsigned int cmd,
                                       int (*handler)(unsigned int,
                                                      unsigned int,
                                                      unsigned long,
                                                      struct file *));
#define A(__x) ((unsigned long)(__x))
extern int unregister_ioctl32_conversion(unsigned int cmd);
#endif

typedef struct ica_worker_wrapper {
	struct list_head workers;
	ica_worker_t *worker;
} ica_worker_wrapper_t;

typedef struct ica_dev {
	struct list_head workers;
	unsigned int count;
} ica_dev_t;

static const char *version =
"devica.c:v0.23 11/10/03 Jon Grimm (c) IBM Corp.";

static const char *modname_template =
"ica-slot-%d";

static int driver_major;
static int maxdevices = 1;
static int maxmodules = 1;

static ica_dev_t **devices;

/* This function is called on PPC 64 systems after conversions are 
   made to the structure passed in as arg.  This function calls
   the device driver ioctl with the new converted arg
*/
#ifdef CONFIG_PPC64
int do_ica_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct file * filp;
	int error = -EBADF;
	mm_segment_t old_fs = get_fs();

	filp = fget(fd);
	if (!filp)
		goto out;
	error = 0;
	lock_kernel();

	set_fs(KERNEL_DS);
	if (filp->f_op && filp->f_op->ioctl) {
		error = filp->f_op->ioctl(filp->f_dentry->d_inode, filp, cmd, arg);
	}

	set_fs(old_fs);
	unlock_kernel();
	fput(filp);
out:
	return error;
}
#endif

int ica_register_worker(int part, ica_worker_t *worker)
{
	ica_dev_t *dev;
	ica_worker_wrapper_t *wrapper;

	assertk(worker);

	if((part < 0)||(part >= maxdevices)){
		return -ENODEV;
	}

	dev = devices[part];

	if(dev == NULL){
		dev = kmalloc(sizeof(ica_dev_t), GFP_KERNEL);
      if ( dev == NULL ){
         assertk(dev);
         return -ENOMEM;
      }
		memset(dev, 0, sizeof(ica_dev_t));
		INIT_LIST_HEAD(&dev->workers);
	}

	wrapper = kmalloc(sizeof(ica_worker_wrapper_t), GFP_KERNEL);
   if (wrapper == NULL) {
      assertk(wrapper);
      return -ENOMEM;
   }
	memset(wrapper, 0, sizeof(ica_worker_wrapper_t));

	INIT_LIST_HEAD(&wrapper->workers);

	wrapper->worker = worker;

	list_add(&wrapper->workers, &dev->workers);

	dev->count++;
	devices[part] = dev;

	return 0;
}

int ica_unregister_worker(int part, ica_worker_t *worker)
{
	ica_dev_t *dev;
	ica_worker_wrapper_t *wrapper;
	int rc = -1;

	if(part >= maxdevices){
		return -1;
	}

	dev = devices[part];

	
	if(dev){
		struct list_head *pos;		
		
		list_for_each(pos, &dev->workers){
			wrapper = list_entry(pos, ica_worker_wrapper_t, workers);
		
			if(wrapper->worker == worker){
		        
				list_del(pos);
				kfree(pos);
				dev->count--;
				rc=0;
				break;
			}
		}

		if(list_empty(&dev->workers)){
			devices[part] = NULL;
			kfree(dev);
		}
	}

	return rc;
}



static ssize_t ica_rng_read(struct file * filp, char * buf, size_t nbytes, loff_t *ppos)
{
	ssize_t count = -ENODEV;
	ica_worker_t *worker = NULL;
	ica_worker_wrapper_t *wrapper = NULL;
	ica_dev_t **save = filp->private_data;
	ica_dev_t *dev = *save;
	
	if(dev){
		if(!list_empty(&dev->workers)){
			struct list_head *entry;
			struct ica_operations *fops;

			entry = dev->workers.next;

			// put at end of list
			list_del(entry);
			INIT_LIST_HEAD(entry);
			
			list_add_tail(entry, &dev->workers);
			wrapper = list_entry(entry, ica_worker_wrapper_t, workers);
			
			worker = wrapper->worker;
			assertk(worker);

			fops = worker->icaops;
			
			assertk(fops);
			
			count = fops->read(filp, buf, nbytes, ppos, worker->private_data);
			

			// put at front of list
			// note: worker may have unregistered already, so we
			// might not find it on the list.   On the slim chance that
			// someone re-registers with the same partition and with the same
			// pointer, the worst that happens is that we move it to the 
			// front of the list.

			dev = *save;
			if(dev){
				struct list_head *pos;

				list_for_each(pos, &dev->workers){
					wrapper = list_entry(pos, ica_worker_wrapper_t, workers);
					if(wrapper->worker == worker){
						list_del(pos);
						INIT_LIST_HEAD(pos);
						list_add(pos, &dev->workers);
						break;
					}
				}
			}			
		}
	}


	return count;
}


static int ica_open(struct inode *inode, struct file *filp)
{
	int num;
	int rc = 0;

	num = MINOR(inode->i_rdev);

	if(num >= maxdevices){
		printk(KERN_ERR "devica: node %d does not exist\n", num);
		return -ENODEV;
	}
	
	filp->private_data = &devices[num];


	return rc;
}


static int ica_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int ica_ioctl_getcount(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	return -ENOTSUPP;
}

static int ica_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	ssize_t count = -ENODEV;
	ica_worker_t *worker = NULL;
	ica_worker_wrapper_t *wrapper = NULL;
	ica_dev_t **save = filp->private_data;
	ica_dev_t *dev = *save;
	

	switch(cmd){
	case ICASETBIND:
	case ICAGETBIND:	
	case ICAGETID:
	case ICAGETVPD:
		return -EOPNOTSUPP;
	case ICAGETCOUNT:
		return ica_ioctl_getcount(inode, filp, cmd, arg);
	}
 
   
	if(dev){

		if(!list_empty(&dev->workers)){
			struct list_head *entry;
			struct ica_operations *fops;

			entry = dev->workers.next;

			assertk(entry);

			// put at end of list
			list_del(entry);
			INIT_LIST_HEAD(entry);

			list_add_tail(entry, &dev->workers);
			wrapper = list_entry(entry, ica_worker_wrapper_t, workers);
			
			worker = wrapper->worker;
			assertk(worker);

			fops = worker->icaops;
			
			assertk(fops);
						
			count = fops->ioctl(inode, filp, cmd, arg, worker->private_data);
						
#if 0
			// put at front of list
			// note: worker may have unregistered already, so we
			// might not find it on the list.   On the slim chance that
			// someone re-registers with the same partition and with the same
			// pointer, the worst that happens is that we move it to the 
			// front of the list.

			dev = *save;
			
			if(dev){
				struct list_head *pos;

				list_for_each(pos, &dev->workers){
					
					wrapper = list_entry(pos, ica_worker_wrapper_t, workers);
					if(wrapper->worker == worker){
						list_del(pos);
						INIT_LIST_HEAD(pos);

						list_add(pos, &dev->workers);
						break;
					}
				}
			}
#endif
		}
	}


	return count;
}


struct file_operations ica_fops = {
	owner: THIS_MODULE,
	open: ica_open,
	release: ica_release,
	ioctl: ica_ioctl,
	read: ica_rng_read,
};


/*
 * Create [/devfs]/ica
 *  and
 * [/devfs]/devica/ica{0,1,...}
 */
void  __devinit ica_register_devfs(unsigned int major)
{
	int i;

	devfs_mk_cdev(MKDEV(major,0), S_IRUGO | S_IWUGO | S_IFCHR,
				"ica");

	devfs_mk_dir("devica");
	for (i=0; i<maxdevices; i++)
		devfs_mk_cdev(MKDEV(major,i), S_IRUGO | S_IWUGO | S_IFCHR,
					"devica/ica%d", i);
}


/* On PPC 64 bit machines, currently there is a 64 bit kernel space and
 * a 32 bit user space.  When a user calls an ioctl that is in the
 * kernel, any pointers or longs that are passed into the ioctl must 
 * be converted so they remain 32 bit rather than expanding to 64 bit
 * values.  This function performs the necessary conversions for each of
 * commands the LeedsLite card supports.
*/
#ifdef CONFIG_PPC64
static int ica_ioctl32_conversion(unsigned int fd, unsigned int cmd,
                                unsigned long arg, struct file *filp)
{
 
        int rc;
	void *karg;

/* SAB
 the unions are to allow us to allocate a single location on the stack
 at the beginning of the function and reference it as the individual
 ioctls.
  For every ioctl that is converted, there needs to be a corresponding
  entery into the ioctls union.

  There is a one to one mapping between the _32 type and the regular
  type in icaioctl.h.   
*/
union {
           ica_rng_t_32 rng;
           ica_sha1_t_32 sha1;
           ica_des_t_32 des;
           ica_rsa_modexpo_crt_t_32 crt;
           ica_rsa_modexpo_t_32 mex;
           ica_desmac_t_32 dmac;
} parms32;

union {
           ica_rng_t rng;
           ica_sha1_t sha1;
           ica_des_t des;
           ica_rsa_modexpo_crt_t crt;
           ica_rsa_modexpo_t mex;
           ica_desmac_t dmac;
} parms;

   switch(cmd) {
        case ICASETBIND:
        case ICAGETBIND:
        case ICAGETCOUNT:
        case ICAGETID:
	   rc = do_ica_ioctl(fd, cmd, arg);
	   break;
        case ICARNG:
           if (copy_from_user(&parms32.rng, (struct ica_rng_t_32 *)arg,
sizeof(ica_rng_t_32)))
                return -EFAULT;
           parms.rng.buf = (char *)(A(parms32.rng.buf));
           parms.rng.nbytes = parms32.rng.nbytes;
           break;
        case ICATDESMAC:
        case ICADESMAC:
           if (copy_from_user(&parms32.dmac, (struct ica_desmac_t_32 *)arg,
sizeof(ica_desmac_t_32)))
                return -EFAULT;
           parms.dmac.inputdata = (unsigned char *)(A(parms32.dmac.inputdata));
           parms.dmac.inputdatalength = parms32.dmac.inputdatalength;
           parms.dmac.outputdata = (unsigned char *)(A(parms32.dmac.outputdata));
           parms.dmac.outputdatalength = parms32.dmac.outputdatalength;
           parms.dmac.iv = (ica_des_vector_t *)(A(parms32.dmac.iv));
           parms.dmac.keys = (ica_des_key_t *)(A(parms32.dmac.keys));

	//printk("DEVICA Before do_ica_ioctl: output: %p\n", parms.dmac.outputdata);

	   break;
        case ICARSAMODMULT:
        case ICARSAMODEXPO:
           if (copy_from_user(&parms32.mex, (struct ica_rsa_modexpo_t_32 *)arg, sizeof(ica_rsa_modexpo_t_32)))
                return -EFAULT;
           parms.mex.inputdata = (unsigned char *)(A(parms32.mex.inputdata));
           parms.mex.inputdatalength = parms32.mex.inputdatalength;
           parms.mex.outputdata = (unsigned char *)(A(parms32.mex.outputdata));
           parms.mex.outputdatalength = parms32.mex.outputdatalength;
           parms.mex.b_key = (unsigned char *)(A(parms32.mex.b_key));
           parms.mex.n_modulus = (unsigned char *)(A(parms32.mex.n_modulus));
           break;
        case ICARSACRT:
           if (copy_from_user(&parms32.crt, (struct ica_rsa_modexpo_crt_t_32 *)arg, sizeof(ica_rsa_modexpo_crt_t_32)))
                return -EFAULT;

           parms.crt.inputdata = (unsigned char *)(A(parms32.crt.inputdata));
           parms.crt.inputdatalength = parms32.crt.inputdatalength;
           parms.crt.outputdata = (unsigned char *)(A(parms32.crt.outputdata));
           parms.crt.outputdatalength = parms32.crt.outputdatalength;
           parms.crt.bp_key = (unsigned char *)(A(parms32.crt.bp_key));
           parms.crt.bq_key = (unsigned char *)(A(parms32.crt.bq_key));
           parms.crt.np_prime = (unsigned char *)(A(parms32.crt.np_prime));
           parms.crt.nq_prime = (unsigned char *)(A(parms32.crt.nq_prime));
           parms.crt.u_mult_inv = (unsigned char *)(A(parms32.crt.u_mult_inv));
           break;
        case ICATDES:
        case ICADES:
           if (copy_from_user(&parms32.des, (struct ica_des_t_32 *)arg, sizeof(ica_des_t_32)))
                return -EFAULT;

           parms.des.mode = parms32.des.mode;
           parms.des.direction = parms32.des.direction;
           parms.des.inputdatalength = parms32.des.inputdatalength;
           parms.des.outputdatalength = parms32.des.outputdatalength;
           parms.des.iv = (ica_des_vector_t *)(A(parms32.des.iv));
           parms.des.keys = (ica_des_key_t *)(A(parms32.des.keys));
           parms.des.outputdata = (unsigned char *)(A(parms32.des.outputdata));
           parms.des.inputdata = (unsigned char *)(A(parms32.des.inputdata));
           break;
        case ICASHA1:
        case ICATDESSHA:
           if (copy_from_user(&parms32.sha1, (struct ica_sha1_t_32 *)arg,
sizeof(ica_sha1_t_32)))
                return -EFAULT;

           parms.sha1.inputdatalength = parms32.sha1.inputdatalength;
           parms.sha1.outputdata = (ica_sha1_result_t *)(A(parms32.sha1.outputdata));
           parms.sha1.initialh = (ica_sha1_result_t *)(A(parms32.sha1.initialh));
           parms.sha1.inputdata = (unsigned char *)(A(parms32.sha1.inputdata));
           break;
        default: 
           rc = -EOPNOTSUPP;
                break;
        }
/* issue the ioctl in one place for those
 ioctl's which have argument mapping. */
      karg = &parms.rng; /* need to use an element of the union.  any one will do
                          since they all occupy the same space in memory*/
      rc = do_ica_ioctl(fd, cmd, (unsigned long)karg);

     return rc;
}

/* Register all of the handlers needed for conversion */
int register_conversion_handlers(void)
{
        int err;

        err |= register_ioctl32_conversion(ICASETBIND, ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICAGETBIND, ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICAGETCOUNT,
ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICAGETID, ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICARSAMODEXPO,
ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICARSACRT, ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICARSAMODMULT,
ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICADES, ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICATDES, ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICADESMAC, ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICATDESMAC, ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICATDESSHA, ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICASHA1, ica_ioctl32_conversion);
        err |= register_ioctl32_conversion(ICARNG, ica_ioctl32_conversion);

        return err;
}

/* Unregister the conversion handlers.  There is no need to check
   return codes, all unregister_ioctl32_conversion does it try to 
   remove the command from a linked list, if is it not there, the
   error is non-fatal
*/
void unregister_conversion_handlers(void)
{
        unregister_ioctl32_conversion(ICASETBIND);
        unregister_ioctl32_conversion(ICAGETBIND);
        unregister_ioctl32_conversion(ICAGETCOUNT);
        unregister_ioctl32_conversion(ICAGETID);
        unregister_ioctl32_conversion(ICARSAMODEXPO);
        unregister_ioctl32_conversion(ICARSACRT);
        unregister_ioctl32_conversion(ICARSAMODMULT);
        unregister_ioctl32_conversion(ICADES);
        unregister_ioctl32_conversion(ICATDES);
        unregister_ioctl32_conversion(ICADESMAC);
        unregister_ioctl32_conversion(ICATDESMAC);
        unregister_ioctl32_conversion(ICATDESSHA);
        unregister_ioctl32_conversion(ICASHA1);
        unregister_ioctl32_conversion(ICARNG);

        return;
}
#endif

int __init ica_driver_init(void)
{
	int rc;	
	int i;
	char modname[100];

	if(maxdevices < 1){
		printk(KERN_ERR "devica: maxdevices<1\n");
		maxdevices = 1;
	}

	if(maxmodules < 1){
		printk(KERN_ERR "devica: maxmodules<1\n");
		maxmodules = 1;
	}

	devices = kmalloc(maxdevices * sizeof(ica_dev_t *), GFP_KERNEL);

	if(!devices){
		assertk(devices);
		rc = -ENOMEM;
		goto err_ica_init;
	}

	memset(devices, 0, maxdevices * sizeof(ica_dev_t *));

	rc = register_chrdev(driver_major, "ica", &ica_fops);

	if(rc < 0){
		printk("ica_register_chrdev(ica) returned %d\n", rc);
		assertk(rc >= 0);
		goto err_ica_init_register_chrdev;
	}

	if(driver_major == 0)
		driver_major = rc;

	ica_register_devfs(driver_major);

	printk(KERN_INFO "%s: Init Success\n", version);

	/* Try to load dependent drivers, if not already loaded */
	
	for(i=0; i<maxmodules; i++){		
		sprintf(modname, modname_template, i);
		request_module(modname);
	}  
// On 64 bit kernels, we need function handlers to convert from 32 bit user
// space to 64 bit kernel space
#ifdef CONFIG_PPC64
        rc = register_conversion_handlers();
        if (rc != 0){
                printk(KERN_ERR "Error Registering conversion handlers(%d)\n",
			rc);
		goto err_reg_handlers;
        }
#endif

	return 0;
#ifdef CONFIG_PPC64
 err_reg_handlers:
	unregister_conversion_handlers();
#endif
	unregister_chrdev(driver_major, "ica");
 err_ica_init_register_chrdev:
	kfree(devices);
 err_ica_init:
	return rc;
}

static int __init ica_init_module(void)
{
	return ica_driver_init();
}

static void __exit ica_cleanup_module(void)
{
	/* Unregister all of the handlers for 32->64 bit conversion */
#ifdef CONFIG_PPC64
	unregister_conversion_handlers();
#endif
	devfs_remove("ica");
	devfs_remove("devica");

	unregister_chrdev(driver_major, "ica");
	kfree(devices);
}


#ifdef MODULE

MODULE_AUTHOR("Jon Grimm <jgrimm@us.ibm.com>");
MODULE_DESCRIPTION("IBM Crypto Adapter Work Distributor");
MODULE_PARM(maxdevices, "i");
MODULE_PARM(maxmodules, "i");
MODULE_PARM(driver_major, "i");

#endif /* MODULE */

EXPORT_SYMBOL(ica_register_worker);
EXPORT_SYMBOL(ica_unregister_worker);

module_init(ica_init_module);
module_exit(ica_cleanup_module);


/*
 * Local variables:
 *  compile-command: "gcc -g -DMODULE -D__KERNEL__  -Wall -Wstrict-prototypes -O6 -c devica.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
