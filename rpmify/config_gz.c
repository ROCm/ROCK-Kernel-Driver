
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

/* ----------------------------------------------------------------------------- */

static char kernel_config_data[] = {
#include "config_gz.h"
};

static int config_read_proc(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
    int len = ARRAY_SIZE(kernel_config_data);
    if (len <= off+count)
	*eof = 1;
    *start = page;
    len  -= off;
    if (len > count)
	len = count;
    if (len < 0)
	len = 0;
    memcpy(page,kernel_config_data+off,len);
    return len;
}

static int __init config_init(void)
{
    create_proc_read_entry("config.gz", 0, NULL, config_read_proc, NULL);
    return 0;
}

static void __exit config_fini(void)
{
    remove_proc_entry("config.gz", NULL);
}

module_init(config_init);
module_exit(config_fini);
