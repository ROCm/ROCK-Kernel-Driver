#include "linux/module.h"
#include "linux/string.h"
#include "asm/current.h"
#include "asm/delay.h"
#include "asm/processor.h"
#include "asm/unistd.h"
#include "asm/pgalloc.h"
#include "asm/page.h"
#include "asm/tlbflush.h"
#include "kern_util.h"
#include "user_util.h"
#include "os.h"
#include "helper.h"

EXPORT_SYMBOL(stop);
EXPORT_SYMBOL(uml_physmem);
EXPORT_SYMBOL(set_signals);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(__const_udelay);
EXPORT_SYMBOL(sys_waitpid);
EXPORT_SYMBOL(task_size);
EXPORT_SYMBOL(__do_copy_from_user);
EXPORT_SYMBOL(__do_strncpy_from_user);
EXPORT_SYMBOL(__do_strnlen_user); 
EXPORT_SYMBOL(flush_tlb_range);
EXPORT_SYMBOL(__do_clear_user);
EXPORT_SYMBOL(honeypot);
EXPORT_SYMBOL(host_task_size);
EXPORT_SYMBOL(arch_validate);

EXPORT_SYMBOL(region_pa);
EXPORT_SYMBOL(region_va);
EXPORT_SYMBOL(phys_mem_map);
EXPORT_SYMBOL(page_mem_map);
EXPORT_SYMBOL(get_signals);
EXPORT_SYMBOL(page_to_phys);
EXPORT_SYMBOL(phys_to_page);

EXPORT_SYMBOL(os_open_file);
EXPORT_SYMBOL(os_read_file);
EXPORT_SYMBOL(os_write_file);
EXPORT_SYMBOL(os_seek_file);
EXPORT_SYMBOL(os_pipe);
EXPORT_SYMBOL(helper_wait);
EXPORT_SYMBOL(os_shutdown_socket);
EXPORT_SYMBOL(os_connect_socket);
EXPORT_SYMBOL(run_helper);
EXPORT_SYMBOL(tracing_pid);
EXPORT_SYMBOL(start_thread);
EXPORT_SYMBOL(dump_thread);

/* This is here because UML expands open to sys_open, not to a system
 * call instruction.
 */
EXPORT_SYMBOL(sys_open);
EXPORT_SYMBOL(sys_lseek);
EXPORT_SYMBOL(sys_read);
EXPORT_SYMBOL(sys_wait4);

