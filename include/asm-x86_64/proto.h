#ifndef _ASM_X8664_PROTO_H
#define _ASM_X8664_PROTO_H 1

#include <asm/ldt.h>

/* misc architecture specific prototypes */

struct cpuinfo_x86; 
struct pt_regs;

extern void get_cpu_vendor(struct cpuinfo_x86*);
extern void start_kernel(void);
extern void pda_init(int); 

extern void mcheck_init(struct cpuinfo_x86 *c);
extern void init_memory_mapping(void);

extern void system_call(void); 
extern void ia32_cstar_target(void); 
extern void calibrate_delay(void);
extern void cpu_idle(void);
extern void sys_ni_syscall(void);
extern void config_acpi_tables(void);
extern void ia32_syscall(void);
extern void iommu_hole_init(void);

extern void do_softirq_thunk(void);

extern int numa_setup(char *opt);

extern int setup_early_printk(char *); 
extern void early_printk(const char *fmt, ...) __attribute__((format(printf,1,2)));

extern int k8_scan_nodes(unsigned long start, unsigned long end);

extern int numa_initmem_init(unsigned long start_pfn, unsigned long end_pfn);
extern unsigned long numa_free_all_bootmem(void);

extern void reserve_bootmem_generic(unsigned long phys, unsigned len);
extern void free_bootmem_generic(unsigned long phys, unsigned len);

extern unsigned long end_pfn_map; 

extern void show_stack(unsigned long * rsp);
extern void show_trace(unsigned long * rsp);
extern void show_registers(struct pt_regs *regs);

extern void exception_table_check(void);

extern void acpi_reserve_bootmem(void);

extern void swap_low_mappings(void);

extern void oops_begin(void);
extern void die(const char *,struct pt_regs *,long);
extern void __die(const char * str, struct pt_regs * regs, long err);

extern int map_syscall32(struct mm_struct *mm, unsigned long address);
extern char *syscall32_page;

void setup_node_bootmem(int nodeid, unsigned long start, unsigned long end);

extern unsigned long max_mapnr;
extern unsigned long end_pfn; 
extern unsigned long table_start, table_end;

extern void syscall_init(void);

struct pt_regs;

long do_arch_prctl(struct task_struct *task, int code, unsigned long addr);

#define round_up(x,y) (((x) + (y) - 1) & ~((y)-1))
#define round_down(x,y) ((x) & ~((y)-1))

#endif
