/*
    i2c-dev.c - i2c-bus driver, char device interface  

    Copyright (C) 1995-97 Simon G. Vogl
    Copyright (C) 1998-99 Frodo Looijaard <frodol@dds.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* Note that this is a complete rewrite of Simon Vogl's i2c-dev module.
   But I have used so much of his original code and ideas that it seems
   only fair to recognize him as co-author -- Frodo */

/* The I2C_RDWR ioctl code is written by Kolja Waschk <waschk@telos.de> */

/* The devfs code is contributed by Philipp Matthias Hahn 
   <pmhahn@titan.lahn.de> */

/* $Id: i2c-dev.c,v 1.53 2003/01/21 08:08:16 kmalkki Exp $ */

/* If you want debugging uncomment: */
/* #define DEBUG 1 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <asm/uaccess.h>

/* struct file_operations changed too often in the 2.1 series for nice code */

static ssize_t i2cdev_read (struct file *file, char *buf, size_t count, 
                            loff_t *offset);
static ssize_t i2cdev_write (struct file *file, const char *buf, size_t count, 
                             loff_t *offset);

static int i2cdev_ioctl (struct inode *inode, struct file *file, 
                         unsigned int cmd, unsigned long arg);
static int i2cdev_open (struct inode *inode, struct file *file);

static int i2cdev_release (struct inode *inode, struct file *file);

static int i2cdev_attach_adapter(struct i2c_adapter *adap);
static int i2cdev_detach_client(struct i2c_client *client);
static int i2cdev_command(struct i2c_client *client, unsigned int cmd,
                           void *arg);

static struct file_operations i2cdev_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= i2cdev_read,
	.write		= i2cdev_write,
	.ioctl		= i2cdev_ioctl,
	.open		= i2cdev_open,
	.release	= i2cdev_release,
};

#define I2CDEV_ADAPS_MAX I2C_ADAP_MAX
static struct i2c_adapter *i2cdev_adaps[I2CDEV_ADAPS_MAX];

static struct i2c_driver i2cdev_driver = {
	.owner		= THIS_MODULE,
	.name		= "dev driver",
	.id		= I2C_DRIVERID_I2CDEV,
	.flags		= I2C_DF_DUMMY,
	.attach_adapter	= i2cdev_attach_adapter,
	.detach_client	= i2cdev_detach_client,
	.command	= i2cdev_command,
};

static struct i2c_client i2cdev_client_template = {
	.dev		= {
		.name	= "I2C /dev entry",
	},
	.id		= 1,
	.addr		= -1,
	.driver		= &i2cdev_driver,
};

static ssize_t i2cdev_read (struct file *file, char *buf, size_t count,
                            loff_t *offset)
{
	char *tmp;
	int ret;

	struct i2c_client *client = (struct i2c_client *)file->private_data;

	if (count > 8192)
		count = 8192;

	/* copy user space data to kernel space. */
	tmp = kmalloc(count,GFP_KERNEL);
	if (tmp==NULL)
		return -ENOMEM;

	pr_debug("i2c-dev.o: i2c-%d reading %d bytes.\n",
		minor(file->f_dentry->d_inode->i_rdev), count);

	ret = i2c_master_recv(client,tmp,count);
	if (ret >= 0)
		ret = copy_to_user(buf,tmp,count)?-EFAULT:ret;
	kfree(tmp);
	return ret;
}

static ssize_t i2cdev_write (struct file *file, const char *buf, size_t count,
                             loff_t *offset)
{
	int ret;
	char *tmp;
	struct i2c_client *client = (struct i2c_client *)file->private_data;

	if (count > 8192)
		count = 8192;

	/* copy user space data to kernel space. */
	tmp = kmalloc(count,GFP_KERNEL);
	if (tmp==NULL)
		return -ENOMEM;
	if (copy_from_user(tmp,buf,count)) {
		kfree(tmp);
		return -EFAULT;
	}

	pr_debug("i2c-dev.o: i2c-%d writing %d bytes.\n",
		minor(file->f_dentry->d_inode->i_rdev), count);

	ret = i2c_master_send(client,tmp,count);
	kfree(tmp);
	return ret;
}

int i2cdev_ioctl (struct inode *inode, struct file *file, unsigned int cmd, 
                  unsigned long arg)
{
	struct i2c_client *client = (struct i2c_client *)file->private_data;
	struct i2c_rdwr_ioctl_data rdwr_arg;
	struct i2c_smbus_ioctl_data data_arg;
	union i2c_smbus_data temp;
	struct i2c_msg *rdwr_pa;
	int i,datasize,res;
	unsigned long funcs;

	pr_debug("i2c-dev.o: i2c-%d ioctl, cmd: 0x%x, arg: %lx.\n",
		minor(inode->i_rdev),cmd, arg);

	switch ( cmd ) {
	case I2C_SLAVE:
	case I2C_SLAVE_FORCE:
		if ((arg > 0x3ff) || 
		    (((client->flags & I2C_M_TEN) == 0) && arg > 0x7f))
			return -EINVAL;
		if ((cmd == I2C_SLAVE) && i2c_check_addr(client->adapter,arg))
			return -EBUSY;
		client->addr = arg;
		return 0;
	case I2C_TENBIT:
		if (arg)
			client->flags |= I2C_M_TEN;
		else
			client->flags &= ~I2C_M_TEN;
		return 0;
	case I2C_PEC:
		if (arg)
			client->flags |= I2C_CLIENT_PEC;
		else
			client->flags &= ~I2C_CLIENT_PEC;
		return 0;
	case I2C_FUNCS:
		funcs = i2c_get_functionality(client->adapter);
		return (copy_to_user((unsigned long *)arg,&funcs,
		                     sizeof(unsigned long)))?-EFAULT:0;

        case I2C_RDWR:
		if (copy_from_user(&rdwr_arg, 
				   (struct i2c_rdwr_ioctl_data *)arg, 
				   sizeof(rdwr_arg)))
			return -EFAULT;

		/* Put an arbritrary limit on the number of messages that can
		 * be sent at once */
		if (rdwr_arg.nmsgs > 42)
			return -EINVAL;
		
		rdwr_pa = (struct i2c_msg *)
			kmalloc(rdwr_arg.nmsgs * sizeof(struct i2c_msg), 
			GFP_KERNEL);

		if (rdwr_pa == NULL) return -ENOMEM;

		res = 0;
		for( i=0; i<rdwr_arg.nmsgs; i++ ) {
		    	if(copy_from_user(&(rdwr_pa[i]),
					&(rdwr_arg.msgs[i]),
					sizeof(rdwr_pa[i]))) {
			        res = -EFAULT;
				break;
			}
			/* Limit the size of the message to a sane amount */
			if (rdwr_pa[i].len > 8192) {
				res = -EINVAL;
				break;
			}
			rdwr_pa[i].buf = kmalloc(rdwr_pa[i].len, GFP_KERNEL);
			if(rdwr_pa[i].buf == NULL) {
				res = -ENOMEM;
				break;
			}
			if(copy_from_user(rdwr_pa[i].buf,
				rdwr_arg.msgs[i].buf,
				rdwr_pa[i].len)) {
			    	res = -EFAULT;
				break;
			}
		}
		if (res < 0) {
			int j;
			for (j = 0; j < i; ++j)
				kfree(rdwr_pa[j].buf);
			kfree(rdwr_pa);
			return res;
		}
		if (!res) {
			res = i2c_transfer(client->adapter,
				rdwr_pa,
				rdwr_arg.nmsgs);
		}
		while(i-- > 0) {
			if( res>=0 && (rdwr_pa[i].flags & I2C_M_RD))
			{
				if(copy_to_user(
					rdwr_arg.msgs[i].buf,
					rdwr_pa[i].buf,
					rdwr_pa[i].len))
				{
					res = -EFAULT;
				}
			}
			kfree(rdwr_pa[i].buf);
		}
		kfree(rdwr_pa);
		return res;

	case I2C_SMBUS:
		if (copy_from_user(&data_arg,
		                   (struct i2c_smbus_ioctl_data *) arg,
		                   sizeof(struct i2c_smbus_ioctl_data)))
			return -EFAULT;
		if ((data_arg.size != I2C_SMBUS_BYTE) && 
		    (data_arg.size != I2C_SMBUS_QUICK) &&
		    (data_arg.size != I2C_SMBUS_BYTE_DATA) && 
		    (data_arg.size != I2C_SMBUS_WORD_DATA) &&
		    (data_arg.size != I2C_SMBUS_PROC_CALL) &&
		    (data_arg.size != I2C_SMBUS_BLOCK_DATA) &&
		    (data_arg.size != I2C_SMBUS_I2C_BLOCK_DATA) &&
		    (data_arg.size != I2C_SMBUS_BLOCK_PROC_CALL)) {
			dev_dbg(&client->dev,
				"size out of range (%x) in ioctl I2C_SMBUS.\n",
				data_arg.size);
			return -EINVAL;
		}
		/* Note that I2C_SMBUS_READ and I2C_SMBUS_WRITE are 0 and 1, 
		   so the check is valid if size==I2C_SMBUS_QUICK too. */
		if ((data_arg.read_write != I2C_SMBUS_READ) && 
		    (data_arg.read_write != I2C_SMBUS_WRITE)) {
			dev_dbg(&client->dev, 
				"read_write out of range (%x) in ioctl I2C_SMBUS.\n",
				data_arg.read_write);
			return -EINVAL;
		}

		/* Note that command values are always valid! */

		if ((data_arg.size == I2C_SMBUS_QUICK) ||
		    ((data_arg.size == I2C_SMBUS_BYTE) && 
		    (data_arg.read_write == I2C_SMBUS_WRITE)))
			/* These are special: we do not use data */
			return i2c_smbus_xfer(client->adapter, client->addr,
					      client->flags,
					      data_arg.read_write,
					      data_arg.command,
					      data_arg.size, NULL);

		if (data_arg.data == NULL) {
			dev_dbg(&client->dev,
				"data is NULL pointer in ioctl I2C_SMBUS.\n");
			return -EINVAL;
		}

		if ((data_arg.size == I2C_SMBUS_BYTE_DATA) ||
		    (data_arg.size == I2C_SMBUS_BYTE))
			datasize = sizeof(data_arg.data->byte);
		else if ((data_arg.size == I2C_SMBUS_WORD_DATA) || 
		         (data_arg.size == I2C_SMBUS_PROC_CALL))
			datasize = sizeof(data_arg.data->word);
		else /* size == smbus block, i2c block, or block proc. call */
			datasize = sizeof(data_arg.data->block);

		if ((data_arg.size == I2C_SMBUS_PROC_CALL) || 
		    (data_arg.size == I2C_SMBUS_BLOCK_PROC_CALL) || 
		    (data_arg.read_write == I2C_SMBUS_WRITE)) {
			if (copy_from_user(&temp, data_arg.data, datasize))
				return -EFAULT;
		}
		res = i2c_smbus_xfer(client->adapter,client->addr,client->flags,
		      data_arg.read_write,
		      data_arg.command,data_arg.size,&temp);
		if (! res && ((data_arg.size == I2C_SMBUS_PROC_CALL) || 
		              (data_arg.size == I2C_SMBUS_BLOCK_PROC_CALL) || 
			      (data_arg.read_write == I2C_SMBUS_READ))) {
			if (copy_to_user(data_arg.data, &temp, datasize))
				return -EFAULT;
		}
		return res;

	default:
		return i2c_control(client,cmd,arg);
	}
	return 0;
}

static int i2cdev_open(struct inode *inode, struct file *file)
{
	unsigned int minor = minor(inode->i_rdev);
	struct i2c_client *client;

	if ((minor >= I2CDEV_ADAPS_MAX) || !(i2cdev_adaps[minor]))
		return -ENODEV;

	client = kmalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	memcpy(client, &i2cdev_client_template, sizeof(*client));

	/* registered with adapter, passed as client to user */
	client->adapter = i2cdev_adaps[minor];
	file->private_data = client;

	/* use adapter module, i2c-dev handled with fops */
	if (!try_module_get(client->adapter->owner))
		goto out_kfree;

	return 0;

out_kfree:
	kfree(client);
	return -ENODEV;
}

static int i2cdev_release(struct inode *inode, struct file *file)
{
	struct i2c_client *client = file->private_data;

	module_put(client->adapter->owner);
	kfree(client);
	file->private_data = NULL;

	return 0;
}

int i2cdev_attach_adapter(struct i2c_adapter *adap)
{
	int i;
	char name[12];

	if ((i = i2c_adapter_id(adap)) < 0) {
		dev_dbg(&adap->dev, "Unknown adapter ?!?\n");
		return -ENODEV;
	}
	if (i >= I2CDEV_ADAPS_MAX) {
		dev_dbg(&adap->dev, "Adapter number too large?!? (%d)\n",i);
		return -ENODEV;
	}

	sprintf (name, "i2c/%d", i);
	if (! i2cdev_adaps[i]) {
		i2cdev_adaps[i] = adap;
		devfs_register (NULL, name,
			DEVFS_FL_DEFAULT, I2C_MAJOR, i,
			S_IFCHR | S_IRUSR | S_IWUSR,
			&i2cdev_fops, NULL);
		dev_dbg(&adap->dev, "Registered as minor %d\n", i);
	} else {
		/* This is actually a detach_adapter call! */
		devfs_remove("i2c/%d", i);
		i2cdev_adaps[i] = NULL;
		dev_dbg(&adap->dev, "Adapter unregistered\n");
	}

	return 0;
}

int i2cdev_detach_client(struct i2c_client *client)
{
	return 0;
}

static int i2cdev_command(struct i2c_client *client, unsigned int cmd,
                           void *arg)
{
	return -1;
}

int __init i2c_dev_init(void)
{
	int res;

	printk(KERN_INFO "i2c /dev entries driver module version %s (%s)\n",
		I2C_VERSION, I2C_DATE);

	if (register_chrdev(I2C_MAJOR,"i2c",&i2cdev_fops)) {
		printk(KERN_ERR "i2c-dev.o: unable to get major %d for i2c bus\n",
		       I2C_MAJOR);
		return -EIO;
	}
	devfs_mk_dir("i2c");
	if ((res = i2c_add_driver(&i2cdev_driver))) {
		printk(KERN_ERR "i2c-dev.o: Driver registration failed, module not inserted.\n");
		devfs_remove("i2c");
		unregister_chrdev(I2C_MAJOR,"i2c");
		return res;
	}
	return 0;
}

static void __exit i2c_dev_exit(void)
{
	i2c_del_driver(&i2cdev_driver);
	devfs_remove("i2c");
	unregister_chrdev(I2C_MAJOR,"i2c");
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl> and Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C /dev entries driver");
MODULE_LICENSE("GPL");

module_init(i2c_dev_init);
module_exit(i2c_dev_exit);
