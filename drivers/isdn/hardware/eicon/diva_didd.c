/* $Id: diva_didd.c,v 1.1.2.6 2001/05/01 15:48:05 armin Exp $
 *
 * DIDD Interface module for Eicon active cards.
 * 
 * Functions are in dadapter.c 
 * 
 * Copyright 2002 by Armin Schindler (mac@melware.de) 
 * Copyright 2002 Cytronics & Melware (info@melware.de)
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>

#include "platform.h"
#include "di_defs.h"
#include "dadapter.h"
#include "divasync.h"
#include "did_vers.h"

static char *main_revision = "$Revision: 1.1.2.6 $";

static char *DRIVERNAME =
    "Eicon DIVA - DIDD table (http://www.melware.net)";
static char *DRIVERLNAME = "divadidd";
char *DRIVERRELEASE = "2.0";

static char *dir_in_proc_net = "isdn";
static char *main_proc_dir = "eicon";

MODULE_DESCRIPTION("DIDD table driver for diva drivers");
MODULE_AUTHOR("Cytronics & Melware, Eicon Networks");
MODULE_SUPPORTED_DEVICE("Eicon diva drivers");
MODULE_LICENSE("GPL");

#define MAX_DESCRIPTORS  32

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)

extern int diddfunc_init(void);
extern void diddfunc_finit(void);

extern void DIVA_DIDD_Read(void *, int);

static struct proc_dir_entry *proc_net_isdn;
static struct proc_dir_entry *proc_didd;
struct proc_dir_entry *proc_net_isdn_eicon = NULL;

EXPORT_SYMBOL_NOVERS(DIVA_DIDD_Read);
EXPORT_SYMBOL_NOVERS(proc_net_isdn_eicon);

static char *getrev(const char *revision)
{
	char *rev;
	char *p;
	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "1.0";
	return rev;
}

static int
proc_read(char *page, char **start, off_t off, int count, int *eof,
	  void *data)
{
	int len = 0;
	char tmprev[32];

	strcpy(tmprev, main_revision);
	len += sprintf(page + len, "%s\n", DRIVERNAME);
	len += sprintf(page + len, "name     : %s\n", DRIVERLNAME);
	len += sprintf(page + len, "release  : %s\n", DRIVERRELEASE);
	len += sprintf(page + len, "build    : %s(%s)\n",
		       diva_didd_common_code_build, DIVA_BUILD);
	len += sprintf(page + len, "revision : %s\n", getrev(tmprev));

	if (off + count >= len)
		*eof = 1;
	if (len < off)
		return 0;
	*start = page + off;
	return ((count < len - off) ? count : len - off);
}

static int DIVA_INIT_FUNCTION create_proc(void)
{
	struct proc_dir_entry *pe;

	for (pe = proc_net->subdir; pe; pe = pe->next) {
		if (!memcmp(dir_in_proc_net, pe->name, pe->namelen)) {
			proc_net_isdn = pe;
			break;
		}
	}
	if (!proc_net_isdn) {
		proc_net_isdn =
		    create_proc_entry(dir_in_proc_net, S_IFDIR, proc_net);
	}
	proc_net_isdn_eicon =
	    create_proc_entry(main_proc_dir, S_IFDIR, proc_net_isdn);

	if (proc_net_isdn_eicon) {
		if (
		    (proc_didd =
		     create_proc_entry(DRIVERLNAME, S_IFREG | S_IRUGO,
				       proc_net_isdn_eicon))) {
			proc_didd->read_proc = proc_read;
		}
		return (1);
	}
	return (0);
}

static void remove_proc(void)
{
	remove_proc_entry(DRIVERLNAME, proc_net_isdn_eicon);
	remove_proc_entry(main_proc_dir, proc_net_isdn);

	if ((proc_net_isdn) && (!proc_net_isdn->subdir)) {
		remove_proc_entry(dir_in_proc_net, proc_net);
	}
}

static int DIVA_INIT_FUNCTION divadidd_init(void)
{
	char tmprev[32];
	int ret = 0;

	MOD_INC_USE_COUNT;

	printk(KERN_INFO "%s\n", DRIVERNAME);
	printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE);
	strcpy(tmprev, main_revision);
	printk("%s  Build:%s(%s)\n", getrev(tmprev),
	       diva_didd_common_code_build, DIVA_BUILD);

	if (!create_proc()) {
		printk(KERN_ERR "%s: could not create proc entry\n",
		       DRIVERLNAME);
		ret = -EIO;
		goto out;
	}

	if (!diddfunc_init()) {
		printk(KERN_ERR "%s: failed to connect to DIDD.\n",
		       DRIVERLNAME);
		remove_proc();
		ret = -EIO;
		goto out;
	}

      out:
	MOD_DEC_USE_COUNT;
	return (ret);
}

void DIVA_EXIT_FUNCTION divadidd_exit(void)
{
	diddfunc_finit();
	remove_proc();
	printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(divadidd_init);
module_exit(divadidd_exit);
