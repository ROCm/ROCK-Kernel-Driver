/*
    i2c-proc.c - Part of lm_sensors, Linux kernel modules for hardware
                monitoring
    Copyright (c) 1998 - 2001 Frodo Looijaard <frodol@dds.nl> and
    Mark D. Studebaker <mdsxyz123@yahoo.com>

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

/*
    This driver puts entries in /proc/sys/dev/sensors for each I2C device
*/

/* #define DEBUG 1 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/i2c.h>
#include <linux/i2c-proc.h>
#include <asm/uaccess.h>

static int i2c_parse_reals(int *nrels, void *buffer, int bufsize,
			       long *results, int magnitude);
static int i2c_write_reals(int nrels, void *buffer, size_t *bufsize,
			       long *results, int magnitude);
static int i2c_proc_chips(ctl_table * ctl, int write,
			      struct file *filp, void *buffer,
			      size_t * lenp);
static int i2c_sysctl_chips(ctl_table * table, int *name, int nlen,
				void *oldval, size_t * oldlenp,
				void *newval, size_t newlen,
				void **context);

#define SENSORS_ENTRY_MAX 20
static struct ctl_table_header *i2c_entries[SENSORS_ENTRY_MAX];

static struct i2c_client *i2c_clients[SENSORS_ENTRY_MAX];

static ctl_table i2c_proc_dev_sensors[] = {
	{SENSORS_CHIPS, "chips", NULL, 0, 0644, NULL, &i2c_proc_chips,
	 &i2c_sysctl_chips},
	{0}
};

static ctl_table i2c_proc_dev[] = {
	{DEV_SENSORS, "sensors", NULL, 0, 0555, i2c_proc_dev_sensors},
	{0},
};


static ctl_table i2c_proc[] = {
	{CTL_DEV, "dev", NULL, 0, 0555, i2c_proc_dev},
	{0}
};


static struct ctl_table_header *i2c_proc_header;

/* This returns a nice name for a new directory; for example lm78-isa-0310
   (for a LM78 chip on the ISA bus at port 0x310), or lm75-i2c-3-4e (for
   a LM75 chip on the third i2c bus at address 0x4e).  
   name is allocated first. */
static char *generate_name(struct i2c_client *client, const char *prefix)
{
	struct i2c_adapter *adapter = client->adapter;
	int addr = client->addr;
	char name_buffer[50], *name;

	if (i2c_is_isa_adapter(adapter)) {
		sprintf(name_buffer, "%s-isa-%04x", prefix, addr);
	} else if (adapter->algo->smbus_xfer || adapter->algo->master_xfer) {
		int id = i2c_adapter_id(adapter);
		if (id < 0)
			return ERR_PTR(-ENOENT);
		sprintf(name_buffer, "%s-i2c-%d-%02x", prefix, id, addr);
	} else {	/* dummy adapter, generate prefix */
		int end, i;

		sprintf(name_buffer, "%s-", prefix);
		end = strlen(name_buffer);

		for (i = 0; i < 32; i++) {
			if (adapter->algo->name[i] == ' ')
				break;
			name_buffer[end++] = tolower(adapter->algo->name[i]);
		}

		name_buffer[end] = 0;
		sprintf(name_buffer + end, "-%04x", addr);
	}

	name = kmalloc(strlen(name_buffer) + 1, GFP_KERNEL);
	if (unlikely(!name))
		return ERR_PTR(-ENOMEM);
	strcpy(name, name_buffer);
	return name;
}

/* This rather complex function must be called when you want to add an entry
   to /proc/sys/dev/sensors/chips. It also creates a new directory within 
   /proc/sys/dev/sensors/.
   ctl_template should be a template of the newly created directory. It is
   copied in memory. The extra2 field of each file is set to point to client.
   If any driver wants subdirectories within the newly created directory,
   this function must be updated!  */
int i2c_register_entry(struct i2c_client *client, const char *prefix,
		       struct ctl_table *leaf)
{
	struct { struct ctl_table root[2], dev[2], sensors[2]; } *tbl;
	struct ctl_table_header *hdr;
	struct ctl_table *tmp;
	const char *name;
	int id;

	name = generate_name(client, prefix);
	if (IS_ERR(name))
		return PTR_ERR(name);

	for (id = 0; id < SENSORS_ENTRY_MAX; id++) {
		if (!i2c_entries[id])
			goto free_slot;
	}

	goto out_free_name;

 free_slot:
	tbl = kmalloc(sizeof(*tbl), GFP_KERNEL);
	if (unlikely(!tbl))
		goto out_free_name;
	memset(tbl, 0, sizeof(*tbl));

	for (tmp = leaf; tmp->ctl_name; tmp++)
		tmp->extra2 = client;

	tbl->sensors->ctl_name = id+256;
	tbl->sensors->procname = name;
	tbl->sensors->mode = 0555;
	tbl->sensors->child = leaf;

	tbl->dev->ctl_name = DEV_SENSORS;
	tbl->dev->procname = "sensors";
	tbl->dev->mode = 0555;
	tbl->dev->child = tbl->sensors;

	tbl->root->ctl_name = CTL_DEV;
	tbl->root->procname = "dev";
	tbl->root->mode = 0555;
	tbl->root->child = tbl->dev;

	hdr = register_sysctl_table(tbl->root, 0);
	if (unlikely(!hdr))
		goto out_free_tbl;

	i2c_entries[id] = hdr;
	i2c_clients[id] = client;

	return (id + 256);	/* XXX(hch) why?? */

 out_free_tbl:
	kfree(tbl);
 out_free_name:
	kfree(name);
	return -ENOMEM;
}

void i2c_deregister_entry(int id)
{
	id -= 256;

	if (i2c_entries[id]) {
		struct ctl_table_header *hdr = i2c_entries[id];
		struct ctl_table *tbl = hdr->ctl_table;

		unregister_sysctl_table(hdr);
		kfree(tbl->child->child->procname);
		kfree(tbl); /* actually the whole anonymous struct */
	}

	i2c_entries[id] = NULL;
	i2c_clients[id] = NULL;
}

static int i2c_proc_chips(ctl_table * ctl, int write, struct file *filp,
		       void *buffer, size_t * lenp)
{
	char BUF[SENSORS_PREFIX_MAX + 30];
	int buflen, curbufsize, i;
	struct ctl_table *client_tbl;

	if (write)
		return 0;

	/* If buffer is size 0, or we try to read when not at the start, we
	   return nothing. Note that I think writing when not at the start
	   does not work either, but anyway, this is straight from the kernel
	   sources. */
	if (!*lenp || (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}
	curbufsize = 0;
	for (i = 0; i < SENSORS_ENTRY_MAX; i++)
		if (i2c_entries[i]) {
			client_tbl =
			    i2c_entries[i]->ctl_table->child->child;
			buflen =
			    sprintf(BUF, "%d\t%s\n", client_tbl->ctl_name,
				    client_tbl->procname);
			if (buflen + curbufsize > *lenp)
				buflen = *lenp - curbufsize;
			if(copy_to_user(buffer, BUF, buflen))
				return -EFAULT;
			curbufsize += buflen;
			(char *) buffer += buflen;
		}
	*lenp = curbufsize;
	filp->f_pos += curbufsize;
	return 0;
}

static int i2c_sysctl_chips(ctl_table * table, int *name, int nlen,
			 void *oldval, size_t * oldlenp, void *newval,
			 size_t newlen, void **context)
{
	struct i2c_chips_data data;
	int i, oldlen, nrels, maxels,ret=0;
	struct ctl_table *client_tbl;

	if (oldval && oldlenp && !((ret = get_user(oldlen, oldlenp))) && 
	    oldlen) {
		maxels = oldlen / sizeof(struct i2c_chips_data);
		nrels = 0;
		for (i = 0; (i < SENSORS_ENTRY_MAX) && (nrels < maxels);
		     i++)
			if (i2c_entries[i]) {
				client_tbl =
				    i2c_entries[i]->ctl_table->child->
				    child;
				data.sysctl_id = client_tbl->ctl_name;
				strcpy(data.name, client_tbl->procname);
				if(copy_to_user(oldval, &data,
					     sizeof(struct
						    i2c_chips_data)))
					return -EFAULT;
				(char *) oldval +=
				    sizeof(struct i2c_chips_data);
				nrels++;
			}
		oldlen = nrels * sizeof(struct i2c_chips_data);
		if(put_user(oldlen, oldlenp))
			return -EFAULT;
	}
	return ret;
}


/* This function reads or writes a 'real' value (encoded by the combination
   of an integer and a magnitude, the last is the power of ten the value
   should be divided with) to a /proc/sys directory. To use this function,
   you must (before registering the ctl_table) set the extra2 field to the
   client, and the extra1 field to a function of the form:
      void func(struct i2c_client *client, int operation, int ctl_name,
                int *nrels_mag, long *results)
   This function can be called for three values of operation. If operation
   equals SENSORS_PROC_REAL_INFO, the magnitude should be returned in 
   nrels_mag. If operation equals SENSORS_PROC_REAL_READ, values should
   be read into results. nrels_mag should return the number of elements
   read; the maximum number is put in it on entry. Finally, if operation
   equals SENSORS_PROC_REAL_WRITE, the values in results should be
   written to the chip. nrels_mag contains on entry the number of elements
   found.
   In all cases, client points to the client we wish to interact with,
   and ctl_name is the SYSCTL id of the file we are accessing. */
int i2c_proc_real(ctl_table * ctl, int write, struct file *filp,
		      void *buffer, size_t * lenp)
{
#define MAX_RESULTS 32
	int mag, nrels = MAX_RESULTS;
	long results[MAX_RESULTS];
	i2c_real_callback callback = ctl->extra1;
	struct i2c_client *client = ctl->extra2;
	int res;

	/* If buffer is size 0, or we try to read when not at the start, we
	   return nothing. Note that I think writing when not at the start
	   does not work either, but anyway, this is straight from the kernel
	   sources. */
	if (!*lenp || (filp->f_pos && !write)) {
		*lenp = 0;
		return 0;
	}

	/* Get the magnitude */
	callback(client, SENSORS_PROC_REAL_INFO, ctl->ctl_name, &mag,
		 NULL);

	if (write) {
		/* Read the complete input into results, converting to longs */
		res = i2c_parse_reals(&nrels, buffer, *lenp, results, mag);
		if (res)
			return res;

		if (!nrels)
			return 0;

		/* Now feed this information back to the client */
		callback(client, SENSORS_PROC_REAL_WRITE, ctl->ctl_name,
			 &nrels, results);

		filp->f_pos += *lenp;
		return 0;
	} else {		/* read */
		/* Get the information from the client into results */
		callback(client, SENSORS_PROC_REAL_READ, ctl->ctl_name,
			 &nrels, results);

		/* And write them to buffer, converting to reals */
		res = i2c_write_reals(nrels, buffer, lenp, results, mag);
		if (res)
			return res;
		filp->f_pos += *lenp;
		return 0;
	}
}

/* This function is equivalent to i2c_proc_real, only it interacts with
   the sysctl(2) syscall, and returns no reals, but integers */
int i2c_sysctl_real(ctl_table * table, int *name, int nlen,
			void *oldval, size_t * oldlenp, void *newval,
			size_t newlen, void **context)
{
	long results[MAX_RESULTS];
	int oldlen, nrels = MAX_RESULTS,ret=0;
	i2c_real_callback callback = table->extra1;
	struct i2c_client *client = table->extra2;

	/* Check if we need to output the old values */
	if (oldval && oldlenp && !((ret=get_user(oldlen, oldlenp))) && oldlen) {
		callback(client, SENSORS_PROC_REAL_READ, table->ctl_name,
			 &nrels, results);

		/* Note the rounding factor! */
		if (nrels * sizeof(long) < oldlen)
			oldlen = nrels * sizeof(long);
		oldlen = (oldlen / sizeof(long)) * sizeof(long);
		if(copy_to_user(oldval, results, oldlen))
			return -EFAULT;
		if(put_user(oldlen, oldlenp))
			return -EFAULT;
	}

	if (newval && newlen) {
		/* Note the rounding factor! */
		newlen -= newlen % sizeof(long);
		nrels = newlen / sizeof(long);
		if(copy_from_user(results, newval, newlen))
			return -EFAULT;

		/* Get the new values back to the client */
		callback(client, SENSORS_PROC_REAL_WRITE, table->ctl_name,
			 &nrels, results);
	}
	return ret;
}


/* nrels contains initially the maximum number of elements which can be
   put in results, and finally the number of elements actually put there.
   A magnitude of 1 will multiply everything with 10; etc.
   buffer, bufsize is the character buffer we read from and its length.
   results will finally contain the parsed integers. 

   Buffer should contain several reals, separated by whitespace. A real
   has the following syntax:
     [ Minus ] Digit* [ Dot Digit* ] 
   (everything between [] is optional; * means zero or more).
   When the next character is unparsable, everything is skipped until the
   next whitespace.

   WARNING! This is tricky code. I have tested it, but there may still be
            hidden bugs in it, even leading to crashes and things!
*/
static int i2c_parse_reals(int *nrels, void *buffer, int bufsize,
			 long *results, int magnitude)
{
	int maxels, min, mag;
	long res,ret=0;
	char nextchar = 0;

	maxels = *nrels;
	*nrels = 0;

	while (bufsize && (*nrels < maxels)) {

		/* Skip spaces at the start */
		while (bufsize && 
		       !((ret=get_user(nextchar, (char *) buffer))) &&
		       isspace((int) nextchar)) {
			bufsize--;
			((char *) buffer)++;
		}

		if (ret)
			return -EFAULT;	
		/* Well, we may be done now */
		if (!bufsize)
			return 0;

		/* New defaults for our result */
		min = 0;
		res = 0;
		mag = magnitude;

		/* Check for a minus */
		if (!((ret=get_user(nextchar, (char *) buffer)))
		    && (nextchar == '-')) {
			min = 1;
			bufsize--;
			((char *) buffer)++;
		}
		if (ret)
			return -EFAULT;

		/* Digits before a decimal dot */
		while (bufsize && 
		       !((ret=get_user(nextchar, (char *) buffer))) &&
		       isdigit((int) nextchar)) {
			res = res * 10 + nextchar - '0';
			bufsize--;
			((char *) buffer)++;
		}
		if (ret)
			return -EFAULT;

		/* If mag < 0, we must actually divide here! */
		while (mag < 0) {
			res = res / 10;
			mag++;
		}

		if (bufsize && (nextchar == '.')) {
			/* Skip the dot */
			bufsize--;
			((char *) buffer)++;

			/* Read digits while they are significant */
			while (bufsize && (mag > 0) &&
			       !((ret=get_user(nextchar, (char *) buffer))) &&
			       isdigit((int) nextchar)) {
				res = res * 10 + nextchar - '0';
				mag--;
				bufsize--;
				((char *) buffer)++;
			}
			if (ret)
				return -EFAULT;
		}
		/* If we are out of data, but mag > 0, we need to scale here */
		while (mag > 0) {
			res = res * 10;
			mag--;
		}

		/* Skip everything until we hit whitespace */
		while (bufsize && 
		       !((ret=get_user(nextchar, (char *) buffer))) &&
		       isspace((int) nextchar)) {
			bufsize--;
			((char *) buffer)++;
		}
		if (ret)
			return -EFAULT;

		/* Put res in results */
		results[*nrels] = (min ? -1 : 1) * res;
		(*nrels)++;
	}

	/* Well, there may be more in the buffer, but we need no more data. 
	   Ignore anything that is left. */
	return 0;
}

static int i2c_write_reals(int nrels, void *buffer, size_t *bufsize,
			 long *results, int magnitude)
{
#define BUFLEN 20
	char BUF[BUFLEN + 1];	/* An individual representation should fit! */
	char printfstr[10];
	int nr = 0;
	int buflen, mag, times;
	int curbufsize = 0;

	while ((nr < nrels) && (curbufsize < *bufsize)) {
		mag = magnitude;

		if (nr != 0) {
			if(put_user(' ', (char *) buffer))
				return -EFAULT;
			curbufsize++;
			((char *) buffer)++;
		}

		/* Fill BUF with the representation of the next string */
		if (mag <= 0) {
			buflen = sprintf(BUF, "%ld", results[nr]);
			if (buflen < 0) {	/* Oops, a sprintf error! */
				*bufsize = 0;
				return -EINVAL;
			}
			while ((mag < 0) && (buflen < BUFLEN)) {
				BUF[buflen++] = '0';
				mag++;
			}
			BUF[buflen] = 0;
		} else {
			times = 1;
			for (times = 1; mag-- > 0; times *= 10);
			if (results[nr] < 0) {
				BUF[0] = '-';
				buflen = 1;
			} else
				buflen = 0;
			strcpy(printfstr, "%ld.%0Xld");
			printfstr[6] = magnitude + '0';
			buflen +=
			    sprintf(BUF + buflen, printfstr,
				    abs(results[nr]) / times,
				    abs(results[nr]) % times);
			if (buflen < 0) {	/* Oops, a sprintf error! */
				*bufsize = 0;
				return -EINVAL;
			}
		}

		/* Now copy it to the user-space buffer */
		if (buflen + curbufsize > *bufsize)
			buflen = *bufsize - curbufsize;
		if(copy_to_user(buffer, BUF, buflen))
			return -EFAULT;
		curbufsize += buflen;
		(char *) buffer += buflen;

		nr++;
	}
	if (curbufsize < *bufsize) {
		if(put_user('\n', (char *) buffer))
			return -EFAULT;
		curbufsize++;
	}
	*bufsize = curbufsize;
	return 0;
}


/* Very inefficient for ISA detects, and won't work for 10-bit addresses! */
int i2c_detect(struct i2c_adapter *adapter,
		   struct i2c_address_data *address_data,
		   i2c_found_addr_proc * found_proc)
{
	int addr, i, found, j, err;
	struct i2c_force_data *this_force;
	int is_isa = i2c_is_isa_adapter(adapter);
	int adapter_id =
	    is_isa ? SENSORS_ISA_BUS : i2c_adapter_id(adapter);

	/* Forget it if we can't probe using SMBUS_QUICK */
	if ((!is_isa) &&
	    !i2c_check_functionality(adapter, I2C_FUNC_SMBUS_QUICK))
		return -1;

	for (addr = 0x00; addr <= (is_isa ? 0xffff : 0x7f); addr++) {
		/* XXX: WTF is going on here??? */
		if ((is_isa && check_region(addr, 1)) ||
		    (!is_isa && i2c_check_addr(adapter, addr)))
			continue;

		/* If it is in one of the force entries, we don't do any
		   detection at all */
		found = 0;
		for (i = 0; !found && (this_force = address_data->forces + i, this_force->force); i++) {
			for (j = 0; !found && (this_force->force[j] != SENSORS_I2C_END); j += 2) {
				if ( ((adapter_id == this_force->force[j]) ||
				      ((this_force->force[j] == SENSORS_ANY_I2C_BUS) && !is_isa)) &&
				      (addr == this_force->force[j + 1]) ) {
					dev_dbg(&adapter->dev, "found force parameter for adapter %d, addr %04x\n", adapter_id, addr);
					if ((err = found_proc(adapter, addr, 0, this_force->kind)))
						return err;
					found = 1;
				}
			}
		}
		if (found)
			continue;

		/* If this address is in one of the ignores, we can forget about it
		   right now */
		for (i = 0; !found && (address_data->ignore[i] != SENSORS_I2C_END); i += 2) {
			if ( ((adapter_id == address_data->ignore[i]) ||
			      ((address_data->ignore[i] == SENSORS_ANY_I2C_BUS) &&
			       !is_isa)) &&
			      (addr == address_data->ignore[i + 1])) {
				dev_dbg(&adapter->dev, "found ignore parameter for adapter %d, addr %04x\n", adapter_id, addr);
				found = 1;
			}
		}
		for (i = 0; !found && (address_data->ignore_range[i] != SENSORS_I2C_END); i += 3) {
			if ( ((adapter_id == address_data->ignore_range[i]) ||
			      ((address_data-> ignore_range[i] == SENSORS_ANY_I2C_BUS) & 
			       !is_isa)) &&
			     (addr >= address_data->ignore_range[i + 1]) &&
			     (addr <= address_data->ignore_range[i + 2])) {
				dev_dbg(&adapter->dev,  "found ignore_range parameter for adapter %d, addr %04x\n", adapter_id, addr);
				found = 1;
			}
		}
		if (found)
			continue;

		/* Now, we will do a detection, but only if it is in the normal or 
		   probe entries */
		if (is_isa) {
			for (i = 0; !found && (address_data->normal_isa[i] != SENSORS_ISA_END); i += 1) {
				if (addr == address_data->normal_isa[i]) {
					dev_dbg(&adapter->dev, "found normal isa entry for adapter %d, addr %04x\n", adapter_id, addr);
					found = 1;
				}
			}
			for (i = 0; !found && (address_data->normal_isa_range[i] != SENSORS_ISA_END); i += 3) {
				if ((addr >= address_data->normal_isa_range[i]) &&
				    (addr <= address_data->normal_isa_range[i + 1]) &&
				    ((addr - address_data->normal_isa_range[i]) % address_data->normal_isa_range[i + 2] == 0)) {
					dev_dbg(&adapter->dev, "found normal isa_range entry for adapter %d, addr %04x", adapter_id, addr);
					found = 1;
				}
			}
		} else {
			for (i = 0; !found && (address_data->normal_i2c[i] != SENSORS_I2C_END); i += 1) {
				if (addr == address_data->normal_i2c[i]) {
					found = 1;
					dev_dbg(&adapter->dev, "found normal i2c entry for adapter %d, addr %02x", adapter_id, addr);
				}
			}
			for (i = 0; !found && (address_data->normal_i2c_range[i] != SENSORS_I2C_END); i += 2) {
				if ((addr >= address_data->normal_i2c_range[i]) &&
				    (addr <= address_data->normal_i2c_range[i + 1])) {
					dev_dbg(&adapter->dev, "found normal i2c_range entry for adapter %d, addr %04x\n", adapter_id, addr);
					found = 1;
				}
			}
		}

		for (i = 0;
		     !found && (address_data->probe[i] != SENSORS_I2C_END);
		     i += 2) {
			if (((adapter_id == address_data->probe[i]) ||
			     ((address_data->
			       probe[i] == SENSORS_ANY_I2C_BUS) & !is_isa))
			    && (addr == address_data->probe[i + 1])) {
				dev_dbg(&adapter->dev, "found probe parameter for adapter %d, addr %04x\n", adapter_id, addr);
				found = 1;
			}
		}
		for (i = 0; !found && (address_data->probe_range[i] != SENSORS_I2C_END); i += 3) {
			if ( ((adapter_id == address_data->probe_range[i]) ||
			      ((address_data->probe_range[i] == SENSORS_ANY_I2C_BUS) & !is_isa)) &&
			     (addr >= address_data->probe_range[i + 1]) &&
			     (addr <= address_data->probe_range[i + 2])) {
				found = 1;
				dev_dbg(&adapter->dev, "found probe_range parameter for adapter %d, addr %04x\n", adapter_id, addr);
			}
		}
		if (!found)
			continue;

		/* OK, so we really should examine this address. First check
		   whether there is some client here at all! */
		if (is_isa ||
		    (i2c_smbus_xfer (adapter, addr, 0, 0, 0, I2C_SMBUS_QUICK, NULL) >= 0))
			if ((err = found_proc(adapter, addr, 0, -1)))
				return err;
	}
	return 0;
}

static int __init i2c_proc_init(void)
{
	printk(KERN_INFO "i2c-proc.o version %s (%s)\n", I2C_VERSION, I2C_DATE);
	if (!
	    (i2c_proc_header =
	     register_sysctl_table(i2c_proc, 0))) {
		printk(KERN_ERR "i2c-proc.o: error: sysctl interface not supported by kernel!\n");
		return -EPERM;
	}
	i2c_proc_header->ctl_table->child->de->owner = THIS_MODULE;
	return 0;
}

static void __exit i2c_proc_exit(void)
{
	unregister_sysctl_table(i2c_proc_header);
}

EXPORT_SYMBOL(i2c_register_entry);
EXPORT_SYMBOL(i2c_deregister_entry);
EXPORT_SYMBOL(i2c_proc_real);
EXPORT_SYMBOL(i2c_sysctl_real);
EXPORT_SYMBOL(i2c_detect);

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>");
MODULE_DESCRIPTION("i2c-proc driver");
MODULE_LICENSE("GPL");

module_init(i2c_proc_init);
module_exit(i2c_proc_exit);
