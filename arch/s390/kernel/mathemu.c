/*
 *  arch/s390/kernel/mathemu.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *
 * 'mathemu.c' handles IEEE instructions on a S390 processor
 * that does not have the IEEE fpu
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ptrace.h>

#include <asm/uaccess.h>
#include <asm/mathemu.h>

#ifdef CONFIG_SYSCTL
int sysctl_ieee_emulation_warnings=1;
#endif

#define mathemu_put_user(x, ptr) \
{                                \
	if(put_user((x),(ptr)))  \
		return 1;        \
}

#define mathemu_get_user(x, ptr) \
{                                \
	if(get_user((x),(ptr)))  \
		return 1;        \
}


#define mathemu_copy_from_user(to,from,n)                 \
{                                                         \
	if(copy_from_user((to),(from),(n))==-EFAULT)      \
		return 1;                                 \
}


#define mathemu_copy_to_user(to, from, n)                 \
{                                                         \
	if(copy_to_user((to),(from),(n))==-EFAULT)      \
		return 1;                                 \
}



static void display_emulation_not_implemented(char *instr)
{
	struct pt_regs *regs;
	__u16 *location;
	
#if CONFIG_SYSCTL
	if(sysctl_ieee_emulation_warnings)
#endif
	{
		regs=current->thread.regs;
		location = (__u16 *)(regs->psw.addr-S390_lowcore.pgm_ilc);
		printk("%s ieee fpu instruction not emulated process name: %s pid: %d \n",
		       instr,
		       current->comm, current->pid);
		printk("%s's PSW:    %08lx %08lx\n",instr,
		       (unsigned long) regs->psw.mask,
		       (unsigned long) location);
	}
}

static int set_CC_df(__u64 val1,__u64 val2) {
        int rc;
        rc = __cmpdf2(val1,val2);
        current->thread.regs->psw.mask &= 0xFFFFCFFF;
        switch (rc) {
                case -1:
                        current->thread.regs->psw.mask |= 0x00001000;
                        break;
                case 1:
                        current->thread.regs->psw.mask |= 0x00002000;
                        break;
        }
	return 0;
}

static int set_CC_sf(__u32 val1,__u32 val2) {
        int rc;
        rc = __cmpsf2(val1,val2);
        current->thread.regs->psw.mask &= 0xFFFFCFFF;
        switch (rc) {
                case -1:
                        current->thread.regs->psw.mask |= 0x00001000;
                        break;
                case 1:
                        current->thread.regs->psw.mask |= 0x00002000;
                        break;
        }
	return 0;
}


static int emu_adb (int rx, __u64 val) {
        current->thread.fp_regs.fprs[rx].d = __adddf3(current->thread.fp_regs.fprs[rx].d,val);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
	return 0;
}

static int emu_adbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = __adddf3(current->thread.fp_regs.fprs[rx].d,
                                         current->thread.fp_regs.fprs[ry].d);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
	return 0;
}

static int emu_aeb (int rx, __u32 val) {
        current->thread.fp_regs.fprs[rx].f = __addsf3(current->thread.fp_regs.fprs[rx].f,val);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_aebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __addsf3(current->thread.fp_regs.fprs[rx].f,
                                        current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_axbr (int rx, int ry) {
        display_emulation_not_implemented("axbr");
	return 0;
}

static int emu_cdb (int rx, __u64 val) {
        set_CC_df(current->thread.fp_regs.fprs[rx].d,val);
	return 0;
}

static int emu_cdbr (int rx, int ry) {
        set_CC_df(current->thread.fp_regs.fprs[rx].d,current->thread.fp_regs.fprs[ry].d);
	return 0;
}

static int emu_cdfbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d =
                     __floatsidf(current->thread.regs->gprs[ry]);
	return 0;
}

static int emu_ceb (int rx, __u32 val) {
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,val);
	return 0;
}

static int emu_cebr (int rx, int ry) {
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,current->thread.fp_regs.fprs[ry].f);
	return 0;
}

static int emu_cefbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f =
                     __floatsisf(current->thread.regs->gprs[ry]);
	return 0;
}

static int emu_cfdbr (int rx, int ry, int mask) {
        current->thread.regs->gprs[rx] =
                     __fixdfsi(current->thread.fp_regs.fprs[ry].d);
	return 0;
}

static int emu_cfebr (int rx, int ry, int mask) {
        current->thread.regs->gprs[rx] =
                     __fixsfsi(current->thread.fp_regs.fprs[ry].f);
	return 0;
}

static int emu_cfxbr (int rx, int ry, int mask) {
        display_emulation_not_implemented("cfxbr");
	return 0;
}

static int emu_cxbr (int rx, int ry) {
        display_emulation_not_implemented("cxbr");
	return 0;
}

static int emu_cxfbr (int rx, int ry) {
        display_emulation_not_implemented("cxfbr");
	return 0;
}

static int emu_ddb (int rx, __u64 val) {
        current->thread.fp_regs.fprs[rx].d = __divdf3(current->thread.fp_regs.fprs[rx].d,val);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
	return 0;
}

static int emu_ddbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = __divdf3(current->thread.fp_regs.fprs[rx].d,
                                         current->thread.fp_regs.fprs[ry].d);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
	return 0;
}

static int emu_deb (int rx, __u32 val) {
        current->thread.fp_regs.fprs[rx].f = __divsf3(current->thread.fp_regs.fprs[rx].f,val);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_debr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __divsf3(current->thread.fp_regs.fprs[rx].f,
                                         current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_didbr (int rx, int ry, int mask) {
        display_emulation_not_implemented("didbr");
	return 0;
}

static int emu_diebr (int rx, int ry, int mask) {
        display_emulation_not_implemented("diebr");
	return 0;
}

static int emu_dxbr (int rx, int ry) {
        display_emulation_not_implemented("dxbr");
	return 0;
}

static int emu_efpc (int rx, int ry) {
	current->thread.regs->gprs[rx]=current->thread.fp_regs.fpc;
	return 0;
}

static int emu_fidbr (int rx, int ry, int mask) {
        display_emulation_not_implemented("fidbr");
	return 0;
}

static int emu_fiebr (int rx, int ry, int mask) {
        display_emulation_not_implemented("fiebr");
	return 0;
}

static int emu_fixbr (int rx, int ry, int mask) {
        display_emulation_not_implemented("fixbr");
	return 0;
}

static int emu_kdb (int rx, __u64 val) {
        display_emulation_not_implemented("kdb");
	return 0;
}

static int emu_kdbr (int rx, int ry) {
        display_emulation_not_implemented("kdbr");
	return 0;
}

static int emu_keb (int rx, __u32 val) {
        display_emulation_not_implemented("keb");
	return 0;
}

static int emu_kebr (int rx, int ry) {
        display_emulation_not_implemented("kebr");
	return 0;
}

static int emu_kxbr (int rx, int ry) {
        display_emulation_not_implemented("kxbr");
	return 0;
}

static int emu_lcdbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d =
        __negdf2(current->thread.fp_regs.fprs[ry].d);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
	return 0;
}

static int emu_lcebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f =
        __negsf2(current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_lcxbr (int rx, int ry) {
        display_emulation_not_implemented("lcxbr");
	return 0;
}

static int emu_ldeb (int rx, __u32 val) {
        current->thread.fp_regs.fprs[rx].d = __extendsfdf2(val);
	return 0;
}

static int emu_ldebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d =
        __extendsfdf2(current->thread.fp_regs.fprs[ry].f);
	return 0;
}

static int emu_ldxbr (int rx, int ry) {
        display_emulation_not_implemented("ldxbr");
	return 0;
}

static int emu_ledbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __truncdfsf2(current->thread.fp_regs.fprs[ry].d);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_lexbr (int rx, int ry) {
        display_emulation_not_implemented("lexbr");
	return 0;
}

static int emu_lndbr (int rx, int ry) {
        display_emulation_not_implemented("lndbr");
	return 0;
}

static int emu_lnebr (int rx, int ry) {
        display_emulation_not_implemented("lnebr");
	return 0;
}

static int emu_lnxbr (int rx, int ry) {
        display_emulation_not_implemented("lnxbr");
	return 0;
}

static int emu_lpdbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = __absdf2(current->thread.fp_regs.fprs[ry].d);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0);
	return 0;
}

static int emu_lpebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __abssf2(current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_lpxbr (int rx, int ry) {
        display_emulation_not_implemented("lpxbr");
	return 0;
}

static int emu_ltdbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = current->thread.fp_regs.fprs[ry].d;
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
	return 0;
}

static int emu_ltebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = current->thread.fp_regs.fprs[ry].f;
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_ltxbr (int rx, int ry) {
        display_emulation_not_implemented("ltxbr");
	return 0;
}

static int emu_lxdb (int rx, __u64 val) {
        display_emulation_not_implemented("lxdb");
	return 0;
}

static int emu_lxdbr (int rx, int ry) {
        display_emulation_not_implemented("lxdbr");
	return 0;
}

static int emu_lxeb (int rx, __u32 val) {
        display_emulation_not_implemented("lxeb");
	return 0;
}

static int emu_lxebr (int rx, int ry) {
        display_emulation_not_implemented("lxebr");
	return 0;
}

static int emu_madb (int rx, __u64 val, int mask) {
        display_emulation_not_implemented("madb");
	return 0;
}

static int emu_madbr (int rx, int ry, int mask) {
        display_emulation_not_implemented("madbr");
	return 0;
}

static int emu_maeb (int rx, __u32 val, int mask) {
        display_emulation_not_implemented("maeb");
	return 0;
}

static int emu_maebr (int rx, int ry, int mask) {
        display_emulation_not_implemented("maebr");
	return 0;
}

static int emu_mdb (int rx, __u64 val) {
        current->thread.fp_regs.fprs[rx].d = __muldf3(current->thread.fp_regs.fprs[rx].d,val);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
	return 0;
}

static int emu_mdbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = __muldf3(current->thread.fp_regs.fprs[rx].d,
                                         current->thread.fp_regs.fprs[ry].d);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
	return 0;
}

static int emu_mdeb (int rx, __u32 val) {
        display_emulation_not_implemented("mdeb");
	return 0;
}

static int emu_mdebr (int rx, int ry) {
        display_emulation_not_implemented("mdebr");
	return 0;
}

static int emu_meeb (int rx, __u32 val) {
        current->thread.fp_regs.fprs[rx].f = __mulsf3(current->thread.fp_regs.fprs[rx].f,
                                         val);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_meebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __mulsf3(current->thread.fp_regs.fprs[rx].f,
                                         current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_msdb (int rx, __u64 val, int mask) {
        display_emulation_not_implemented("msdb");
	return 0;
}

static int emu_msdbr (int rx, int ry, int mask) {
        display_emulation_not_implemented("msdbr");
	return 0;
}

static int emu_mseb (int rx, __u32 val, int mask) {
        display_emulation_not_implemented("mseb");
	return 0;
}

static int emu_msebr (int rx, int ry, int mask) {
        display_emulation_not_implemented("msebr");
	return 0;
}

static int emu_mxbr (int rx, int ry) {
        display_emulation_not_implemented("mxbr");
	return 0;
}

static int emu_mxdb (int rx, __u64 val) {
        display_emulation_not_implemented("mxdb");
	return 0;
}

static int emu_mxdbr (int rx, int ry) {
        display_emulation_not_implemented("mxdbr");
	return 0;
}

static int emu_sdb (int rx, __u64 val) {
        current->thread.fp_regs.fprs[rx].d = __subdf3(current->thread.fp_regs.fprs[rx].d,
                                         val);
        set_CC_sf(current->thread.fp_regs.fprs[rx].d,0ULL);
	return 0;
}

static int emu_sdbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = __subdf3(current->thread.fp_regs.fprs[rx].d,
                                         current->thread.fp_regs.fprs[ry].d);
        set_CC_sf(current->thread.fp_regs.fprs[rx].d,0ULL);
	return 0;
}

static int emu_seb (int rx, __u32 val) {
        current->thread.fp_regs.fprs[rx].f = __subsf3(current->thread.fp_regs.fprs[rx].f,
                                         val);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_sebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __subsf3(current->thread.fp_regs.fprs[rx].f,
                                         current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
	return 0;
}

static int emu_sfpc (int rx, int ry) {
	__u32 val=current->thread.regs->gprs[rx];
	if(val==0)
		current->thread.fp_regs.fpc=val;
	else
		display_emulation_not_implemented("sfpc");
	return 0;
}

static int emu_sqdb (int rx, __u64 val) {
        display_emulation_not_implemented("sqdb");
	return 0;
}

static int emu_sqdbr (int rx, int ry) {
        display_emulation_not_implemented("sqdbr");
	return 0;
}

static int emu_sqeb (int rx, __u32 val) {
        display_emulation_not_implemented("sqeb");
	return 0;
}

static int emu_sqebr (int rx, int ry) {
        display_emulation_not_implemented("sqebr");
	return 0;
}

static int emu_sqxbr (int rx, int ry) {
        display_emulation_not_implemented("sqxbr");
	return 0;
}

static int emu_sxbr (int rx, int ry) {
        display_emulation_not_implemented("sxbr");
	return 0;
}

static int emu_tcdb (int rx, __u64 val) {
        display_emulation_not_implemented("tcdb");
	return 0;
}

static int emu_tceb (int rx, __u32 val) {
        display_emulation_not_implemented("tceb");
	return 0;
}

static int emu_tcxb (int rx, __u64 val) {
        display_emulation_not_implemented("tcxb");
	return 0;
}


static inline void emu_load_regd(int reg) {
        if ((reg&9) == 0) {                /* test if reg in {0,2,4,6} */
                __asm__ __volatile (       /* load reg from fp_regs.fprs[reg] */
                        "     bras  1,0f\n"
                        "     ld    0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" (reg<<4), "a" (&current->thread.fp_regs.fprs[reg].d)
                        : "1" );
        }
}

static inline void emu_load_rege(int reg) {
        if ((reg&9) == 0) {                /* test if reg in {0,2,4,6} */
                __asm__ __volatile (       /* load reg from fp_regs.fprs[reg] */
                        "     bras  1,0f\n"
                        "     le    0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" (reg<<4), "a" (&current->thread.fp_regs.fprs[reg].f)
                        : "1" );
        }
}

static inline void emu_store_regd(int reg) {
        if ((reg&9) == 0) {                /* test if reg in {0,2,4,6} */
                __asm__ __volatile (       /* store reg to fp_regs.fprs[reg] */
                        "     bras  1,0f\n"
                        "     std   0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" (reg<<4), "a" (&current->thread.fp_regs.fprs[reg].d)
                        : "1" );
        }
}


static inline void emu_store_rege(int reg) {
        if ((reg&9) == 0) {                /* test if reg in {0,2,4,6} */
                __asm__ __volatile (       /* store reg to fp_regs.fprs[reg] */
                        "     bras  1,0f\n"
                        "     ste   0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" (reg<<4), "a" (&current->thread.fp_regs.fprs[reg].f)
                        : "1" );
        }
}

int math_emu_b3(__u8 *opcode, struct pt_regs * regs) {
	int rc=0;
        static const __u8 format_table[] = {
                2, 2, 2, 2, 9, 1, 2, 1, 2, 2, 2, 2, 9, 2, 4, 4,
                1, 1, 1, 1, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 3, 3,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                1, 1, 1, 1,10, 1, 1, 3, 1, 1, 1, 1, 1, 1, 0, 0,
                0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 3,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0,
                0, 0, 0, 0, 5, 6, 6, 0, 7, 8, 8, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        static const void *jump_table[]= {
                emu_lpebr, emu_lnebr, emu_ltebr, emu_lcebr,
                emu_ldebr, emu_lxdbr, emu_lxebr, emu_mxdbr,
                emu_kebr,  emu_cebr,  emu_aebr,  emu_sebr,
                emu_mdebr, emu_debr,  emu_maebr, emu_msebr,
                emu_lpdbr, emu_lndbr, emu_ltdbr, emu_lcdbr,
                emu_sqebr, emu_sqdbr, emu_sqxbr, emu_meebr,
                emu_kdbr,  emu_cdbr,  emu_adbr,  emu_sdbr,
                emu_mdbr,  emu_ddbr,  emu_madbr, emu_msdbr,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                emu_lpxbr, emu_lnxbr, emu_ltxbr, emu_lcxbr,
                emu_ledbr, emu_ldxbr, emu_lexbr, emu_fixbr,
                emu_kxbr,  emu_cxbr,  emu_axbr,  emu_sxbr,
                emu_mxbr,  emu_dxbr,  NULL,      NULL,
                NULL,      NULL,      NULL,      emu_diebr,
                NULL,      NULL,      NULL,      emu_fiebr,
                NULL,      NULL,      NULL,      emu_didbr,
                NULL,      NULL,      NULL,      emu_fidbr,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                emu_sfpc,  NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                emu_efpc,  NULL,      NULL,      NULL,
                NULL,      NULL,      NULL,      NULL,
                emu_cefbr, emu_cdfbr, emu_cxfbr, NULL,
                emu_cfebr, emu_cfdbr, emu_cfxbr
        };

        switch (format_table[opcode[1]]) {
        case 1: /* RRE format, double operation */
                emu_store_regd((opcode[3]>>4)&15);
                emu_store_regd(opcode[3]&15);
                /* call the emulation function */
                rc=((int (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
		emu_load_regd((opcode[3]>>4)&15);
		emu_load_regd(opcode[3]&15);
                return rc;
        case 2: /* RRE format, float operation */
                emu_store_rege((opcode[3]>>4)&15);
                emu_store_rege(opcode[3]&15);
                /* call the emulation function */
                rc=((int (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
		emu_load_rege((opcode[3]>>4)&15);
		emu_load_rege(opcode[3]&15);
                return rc;
        case 3: /* RRF format, double operation */
                emu_store_regd((opcode[3]>>4)&15);
                emu_store_regd(opcode[3]&15);
                /* call the emulation function */
                rc=((int (*)(int, int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15,opcode[2]>>4);
		emu_load_regd((opcode[3]>>4)&15);
		emu_load_regd(opcode[3]&15);
		return rc;
        case 4: /* RRF format, float operation */
                emu_store_rege((opcode[3]>>4)&15);
                emu_store_rege(opcode[3]&15);
                /* call the emulation function */
                rc=((int (*)(int, int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15,opcode[2]>>4);
		emu_load_rege((opcode[3]>>4)&15);
		emu_load_rege(opcode[3]&15);
                return rc;
        case 5: /* RRE format, cefbr instruction */
                emu_store_rege((opcode[3]>>4)&15);
                /* call the emulation function */
                rc=((int (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
		emu_load_rege((opcode[3]>>4)&15);
                return rc;
        case 6: /* RRE format, cdfbr & cxfbr instruction */
                emu_store_regd((opcode[3]>>4)&15);
                /* call the emulation function */
                rc=((int (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
		emu_load_regd((opcode[3]>>4)&15);
                return rc;
	case 7: /* RRF format, cfebr instruction */
                emu_store_rege(opcode[3]&15);
                /* call the emulation function */
                rc=((int (*)(int, int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15,opcode[2]>>4);
                return rc;
        case 8: /* RRF format, cfdbr & cfxbr instruction */
                emu_store_regd(opcode[3]&15);
                /* call the emulation function */
                rc=((int (*)(int, int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15,opcode[2]>>4);
                return rc;
	case 9: /* RRE format, ldebr & mdebr instruction */
		/* float store but double load */
                emu_store_rege((opcode[3]>>4)&15);
                emu_store_rege(opcode[3]&15);
                /* call the emulation function */
                rc=((int (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
		emu_load_regd((opcode[3]>>4)&15);
                return rc;
        case 10: /* RRE format, ledbr instruction */
		/* double store but float load */
                emu_store_regd((opcode[3]>>4)&15);
                emu_store_regd(opcode[3]&15);
                /* call the emulation function */
                rc=((int (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
		emu_load_rege((opcode[3]>>4)&15);
                return rc;
        default:
                return 1;
        }
}

static void* calc_addr(struct pt_regs *regs,int rx,int rb,int disp)
{
  rx &= 0xf;
  rb &= 0xf;
  disp &= 0xfff;
  return (void*) ((rx != 0 ? regs->gprs[rx] : 0)  + /* index */   
         (rb != 0 ? regs->gprs[rb] : 0)  + /* base */
         disp);
}
    
int math_emu_ed(__u8 *opcode, struct pt_regs * regs) {
	int rc=0;

        static const __u8 format_table[] = {
                0, 0, 0, 0, 5, 1, 2, 1, 2, 2, 2, 2, 5, 2, 4, 4,
                2, 1, 1, 0, 2, 1, 0, 2, 1, 1, 1, 1, 1, 1, 3, 3,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        static const void *jump_table[]= {
                NULL,     NULL,     NULL,     NULL,
                emu_ldeb, emu_lxdb, emu_lxeb, emu_mxdb,
                emu_keb,  emu_ceb,  emu_aeb,  emu_seb,
                emu_mdeb, emu_deb,  emu_maeb, emu_mseb,
                emu_tceb, emu_tcdb, emu_tcxb, NULL,
                emu_sqeb, emu_sqdb, NULL,     emu_meeb,
                emu_kdb,  emu_cdb,  emu_adb,  emu_sdb,
                emu_mdb,  emu_ddb,  emu_madb, emu_msdb
        };

        switch (format_table[opcode[5]]) {
        case 1: /* RXE format, __u64 constant */ {
                __u64 *dxb, temp;
                __u32 opc;

                emu_store_regd((opcode[1]>>4)&15);
                opc = *((__u32 *) opcode);
                dxb = (__u64 *) calc_addr(regs,opc>>16,opc>>12,opc);
                mathemu_copy_from_user(&temp, dxb, 8);
                /* call the emulation function */
                rc=((int (*)(int, __u64))jump_table[opcode[5]])
                        (opcode[1]>>4,temp);
		emu_load_regd((opcode[1]>>4)&15);
                return rc;
        }
        case 2: /* RXE format, __u32 constant */ {
                __u32 *dxb, temp;
                __u32 opc;

                emu_store_rege((opcode[1]>>4)&15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
                mathemu_get_user(temp, dxb);
                /* call the emulation function */
                rc=((int (*)(int, __u32))jump_table[opcode[5]])
                        (opcode[1]>>4,temp);
		emu_load_rege((opcode[1]>>4)&15);
                return rc;
        }
        case 3: /* RXF format, __u64 constant */ {
                __u32 *dxb, temp;
                __u32 opc;

                emu_store_regd((opcode[1]>>4)&15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
                 mathemu_copy_from_user(&temp, dxb, 8);
                /* call the emulation function */
                rc=((int (*)(int, __u32, int))jump_table[opcode[5]])
                        (opcode[1]>>4,temp,opcode[4]>>4);
		emu_load_regd((opcode[1]>>4)&15);
                return rc;
        }
        case 4: /* RXF format, __u32 constant */ {
                __u32 *dxb, temp;
                __u32 opc;

                emu_store_rege((opcode[1]>>4)&15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
                mathemu_get_user(temp, dxb);
                /* call the emulation function */
                rc=((int (*)(int, __u32, int))jump_table[opcode[5]])
                        (opcode[1]>>4,temp,opcode[4]>>4);
                emu_load_rege((opcode[1]>>4)&15);
                return rc;
        }
	case 5: /* RXE format, __u32 constant */
                /* store_rege and load_regd */ 
	{
                __u32 *dxb, temp;
                __u32 opc;
                emu_store_rege((opcode[1]>>4)&15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
                mathemu_get_user(temp, dxb);
                /* call the emulation function */
                rc=((int (*)(int, __u32))jump_table[opcode[5]])
                        (opcode[1]>>4,temp);
                emu_load_regd((opcode[1]>>4)&15);
                return rc;
        }
        default:
                return 1;
        }
}

/*
 * Emulate LDR Rx,Ry with Rx or Ry not in {0, 2, 4, 6}
 */
int math_emu_ldr(__u8 *opcode) {
        __u16 opc = *((__u16 *) opcode);

        if ((opc & 0x0090) == 0) {         /* test if rx in {0,2,4,6} */
                /* we got an exception therfore ry can't be in {0,2,4,6} */
                __asm__ __volatile (       /* load rx from fp_regs.fprs[ry] */
                        "     bras  1,0f\n"
                        "     ld    0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" (opc&0x00f0),
                          "a" (&current->thread.fp_regs.fprs[opc&0x000f].d)
                        : "1" );
        } else if ((opc & 0x0009) == 0) {  /* test if ry in {0,2,4,6} */
                __asm__ __volatile (       /* store ry to fp_regs.fprs[rx] */
                        "     bras  1,0f\n"
                        "     std   0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" ((opc&0x000f)<<4),
                          "a" (&current->thread.fp_regs.fprs[(opc&0x00f0)>>4].d)
                        : "1" );
        } else {                          /* move fp_regs.fprs[ry] to fp_regs.fprs[rx] */
                current->thread.fp_regs.fprs[(opc&0x00f0)>>4] =
                        current->thread.fp_regs.fprs[opc&0x000f];
        }
	return 0;
}

/*
 * Emulate LER Rx,Ry with Rx or Ry not in {0, 2, 4, 6}
 */
int math_emu_ler(__u8 *opcode) {
        __u16 opc = *((__u16 *) opcode);

        if ((opc & 0x0090) == 0) {         /* test if rx in {0,2,4,6} */
                /* we got an exception therfore ry can't be in {0,2,4,6} */
                __asm__ __volatile (       /* load rx from fp_regs.fprs[ry] */
                        "     bras  1,0f\n"
                        "     le    0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" (opc&0x00f0),
                          "a" (&current->thread.fp_regs.fprs[opc&0x000f].f)
                        : "1" );
        } else if ((opc & 0x0009) == 0) {  /* test if ry in {0,2,4,6} */
                __asm__ __volatile (       /* store ry to fp_regs.fprs[rx] */
                        "     bras  1,0f\n"
                        "     ste   0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" ((opc&0x000f)<<4),
                          "a" (&current->thread.fp_regs.fprs[(opc&0x00f0)>>4].f)
                        : "1" );
        } else {                          /* move fp_regs.fprs[ry] to fp_regs.fprs[rx] */
                current->thread.fp_regs.fprs[(opc&0x00f0)>>4] =
                        current->thread.fp_regs.fprs[opc&0x000f];
        }
	return 0;
}

/*
 * Emulate LD R,D(X,B) with R not in {0, 2, 4, 6}
 */
int math_emu_ld(__u8 *opcode, struct pt_regs * regs) {
        __u32 opc = *((__u32 *) opcode);
        __u64 *dxb;

        dxb = (__u64 *) calc_addr(regs,opc>>16,opc>>12,opc);
        mathemu_copy_from_user(&current->thread.fp_regs.fprs[(opc>>20)&15].d, dxb, 8);
	return 0;
}

/*
 * Emulate LE R,D(X,B) with R not in {0, 2, 4, 6}
 */
int math_emu_le(__u8 *opcode, struct pt_regs * regs) {
        __u32 opc = *((__u32 *) opcode);
        __u32 *mem, *dxb;

        dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
        mem = (__u32 *) (&current->thread.fp_regs.fprs[(opc>>20)&15].f);
        mathemu_get_user(mem[0], dxb);
	return 0;
}

/*
 * Emulate STD R,D(X,B) with R not in {0, 2, 4, 6}
 */
int math_emu_std(__u8 *opcode, struct pt_regs * regs) {
        __u32 opc = *((__u32 *) opcode);
        __u64 *dxb;
        dxb = (__u64 *) calc_addr(regs,opc>>16,opc>>12,opc);
        mathemu_copy_to_user(dxb, &current->thread.fp_regs.fprs[(opc>>20)&15].d, 8);
	return 0;
}

/*
 * Emulate STE R,D(X,B) with R not in {0, 2, 4, 6}
 */
int math_emu_ste(__u8 *opcode, struct pt_regs * regs) {
        __u32 opc = *((__u32 *) opcode);
        __u32 *mem, *dxb;
        dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
        /* FIXME: how to react if mathemu_put_user fails ? */
        mem = (__u32 *) (&current->thread.fp_regs.fprs[(opc>>20)&15].f);
        mathemu_put_user(mem[0], dxb);
	return 0;
}

/*
 * Emulate LFPC D(B)
 */
int math_emu_lfpc(__u8 *opcode, struct pt_regs *regs) {
	__u32 *dxb,temp;
	__u32 opc = *((__u32 *) opcode);
	dxb= (__u32 *) calc_addr(regs,0,opc>>12,opc);
	mathemu_get_user(temp, dxb);
	if(temp!=0)
		display_emulation_not_implemented("lfpc");
        return 0;
}

/*
 * Emulate STFPC D(B)
 */
int math_emu_stfpc(__u8 *opcode, struct pt_regs *regs) {
	__u32 *dxb;
	__u32 opc = *((__u32 *) opcode);
	dxb= (__u32 *) calc_addr(regs,0,opc>>12,opc);
	mathemu_put_user(current->thread.fp_regs.fpc, dxb);
        return 0;
}

/*
 * Emulate SRNM D(B)
 */
int math_emu_srnm(__u8 *opcode, struct pt_regs *regs) {
        /* FIXME: how to do that ?!? */
	display_emulation_not_implemented("srnm");
        return 0;
}
















