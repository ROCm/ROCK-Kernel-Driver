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

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ptrace.h>

#include <asm/uaccess.h>
#include <asm/mathemu.h>

static void set_CC_df(__u64 val1,__u64 val2) {
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
}

static void set_CC_sf(__u32 val1,__u32 val2) {
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
}


static void emu_adb (int rx, __u64 val) {
        current->thread.fp_regs.fprs[rx].d = __adddf3(current->thread.fp_regs.fprs[rx].d,val);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
}

static void emu_adbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = __adddf3(current->thread.fp_regs.fprs[rx].d,
                                         current->thread.fp_regs.fprs[ry].d);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
}

static void emu_aeb (int rx, __u32 val) {
        current->thread.fp_regs.fprs[rx].f = __addsf3(current->thread.fp_regs.fprs[rx].f,val);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_aebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __addsf3(current->thread.fp_regs.fprs[rx].f,
                                        current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_axbr (int rx, int ry) {
        printk("axbr emulation not implemented!\n");
}

static void emu_cdb (int rx, __u64 val) {
        set_CC_df(current->thread.fp_regs.fprs[rx].d,val);
}

static void emu_cdbr (int rx, int ry) {
        set_CC_df(current->thread.fp_regs.fprs[rx].d,current->thread.fp_regs.fprs[ry].d);
}

static void emu_cdfbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d =
                     __floatsidf(current->thread.regs->gprs[ry]);
}

static void emu_ceb (int rx, __u32 val) {
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,val);
}

static void emu_cebr (int rx, int ry) {
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,current->thread.fp_regs.fprs[ry].f);
}

static void emu_cefbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f =
                     __floatsisf(current->thread.regs->gprs[ry]);
}

static void emu_cfdbr (int rx, int ry, int mask) {
        current->thread.regs->gprs[rx] =
                     __fixdfsi(current->thread.fp_regs.fprs[ry].d);
}

static void emu_cfebr (int rx, int ry, int mask) {
        current->thread.regs->gprs[rx] =
                     __fixsfsi(current->thread.fp_regs.fprs[ry].f);
}

static void emu_cfxbr (int rx, int ry, int mask) {
        printk("cfxbr emulation not implemented!\n");
}

static void emu_cxbr (int rx, int ry) {
        printk("cxbr emulation not implemented!\n");
}

static void emu_cxfbr (int rx, int ry) {
        printk("cxfbr emulation not implemented!\n");
}

static void emu_ddb (int rx, __u64 val) {
        current->thread.fp_regs.fprs[rx].d = __divdf3(current->thread.fp_regs.fprs[rx].d,val);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
}

static void emu_ddbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = __divdf3(current->thread.fp_regs.fprs[rx].d,
                                         current->thread.fp_regs.fprs[ry].d);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
}

static void emu_deb (int rx, __u32 val) {
        current->thread.fp_regs.fprs[rx].f = __divsf3(current->thread.fp_regs.fprs[rx].f,val);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_debr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __divsf3(current->thread.fp_regs.fprs[rx].f,
                                         current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_didbr (int rx, int ry, int mask) {
        printk("didbr emulation not implemented!\n");
}

static void emu_diebr (int rx, int ry, int mask) {
        printk("diebr emulation not implemented!\n");
}

static void emu_dxbr (int rx, int ry) {
        printk("dxbr emulation not implemented!\n");
}

static void emu_efpc (int rx, int ry) {
        printk("efpc emulation not implemented!\n");
}

static void emu_fidbr (int rx, int ry, int mask) {
        printk("fidbr emulation not implemented!\n");
}

static void emu_fiebr (int rx, int ry, int mask) {
        printk("fiebr emulation not implemented!\n");
}

static void emu_fixbr (int rx, int ry, int mask) {
        printk("fixbr emulation not implemented!\n");
}

static void emu_kdb (int rx, __u64 val) {
        printk("kdb emulation not implemented!\n");
}

static void emu_kdbr (int rx, int ry) {
        printk("kdbr emulation not implemented!\n");
}

static void emu_keb (int rx, __u32 val) {
        printk("keb emulation not implemented!\n");
}

static void emu_kebr (int rx, int ry) {
        printk("kebr emulation not implemented!\n");
}

static void emu_kxbr (int rx, int ry) {
        printk("kxbr emulation not implemented!\n");
}

static void emu_lcdbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d =
        __negdf2(current->thread.fp_regs.fprs[ry].d);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
}

static void emu_lcebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f =
        __negsf2(current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_lcxbr (int rx, int ry) {
        printk("lcxbr emulation not implemented!\n");
}

static void emu_ldeb (int rx, __u32 val) {
        current->thread.fp_regs.fprs[rx].d = __extendsfdf2(val);
}

static void emu_ldebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d =
        __extendsfdf2(current->thread.fp_regs.fprs[ry].f);
}

static void emu_ldxbr (int rx, int ry) {
        printk("ldxbr emulation not implemented!\n");
}

static void emu_ledbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __truncdfsf2(current->thread.fp_regs.fprs[ry].d);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_lexbr (int rx, int ry) {
        printk("lexbr emulation not implemented!\n");
}

static void emu_lndbr (int rx, int ry) {
        printk("lndbr emulation not implemented!\n");
}

static void emu_lnebr (int rx, int ry) {
        printk("lnebr emulation not implemented!\n");
}

static void emu_lnxbr (int rx, int ry) {
        printk("lnxbr emulation not implemented!\n");
}

static void emu_lpdbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = __absdf2(current->thread.fp_regs.fprs[ry].d);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0);
}

static void emu_lpebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __abssf2(current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_lpxbr (int rx, int ry) {
        printk("lpxbr emulation not implemented!\n");
}

static void emu_ltdbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = current->thread.fp_regs.fprs[ry].d;
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
}

static void emu_ltebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = current->thread.fp_regs.fprs[ry].f;
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_ltxbr (int rx, int ry) {
        printk("ltxbr emulation not implemented!\n");
}

static void emu_lxdb (int rx, __u64 val) {
        printk("lxdb emulation not implemented!\n");
}

static void emu_lxdbr (int rx, int ry) {
        printk("lxdbr emulation not implemented!\n");
}

static void emu_lxeb (int rx, __u32 val) {
        printk("lxeb emulation not implemented!\n");
}

static void emu_lxebr (int rx, int ry) {
        printk("lxebr emulation not implemented!\n");
}

static void emu_madb (int rx, __u64 val, int mask) {
        printk("madb emulation not implemented!\n");
}

static void emu_madbr (int rx, int ry, int mask) {
        printk(" emulation not implemented!\n");
}

static void emu_maeb (int rx, __u32 val, int mask) {
        printk("maeb emulation not implemented!\n");
}

static void emu_maebr (int rx, int ry, int mask) {
        printk("maebr emulation not implemented!\n");
}

static void emu_mdb (int rx, __u64 val) {
        current->thread.fp_regs.fprs[rx].d = __muldf3(current->thread.fp_regs.fprs[rx].d,val);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
}

static void emu_mdbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = __muldf3(current->thread.fp_regs.fprs[rx].d,
                                         current->thread.fp_regs.fprs[ry].d);
        set_CC_df(current->thread.fp_regs.fprs[rx].d,0ULL);
}

static void emu_mdeb (int rx, __u32 val) {
        printk("mdeb emulation not implemented!\n");
}

static void emu_mdebr (int rx, int ry) {
        printk("mdebr emulation not implemented!\n");
}

static void emu_meeb (int rx, __u32 val) {
        current->thread.fp_regs.fprs[rx].f = __mulsf3(current->thread.fp_regs.fprs[rx].f,
                                         val);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_meebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __mulsf3(current->thread.fp_regs.fprs[rx].f,
                                         current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_msdb (int rx, __u64 val, int mask) {
        printk("msdb emulation not implemented!\n");
}

static void emu_msdbr (int rx, int ry, int mask) {
        printk("msdbr emulation not implemented!\n");
}

static void emu_mseb (int rx, __u32 val, int mask) {
        printk("mseb emulation not implemented!\n");
}

static void emu_msebr (int rx, int ry, int mask) {
        printk("msebr emulation not implemented!\n");
}

static void emu_mxbr (int rx, int ry) {
        printk("mxbr emulation not implemented!\n");
}

static void emu_mxdb (int rx, __u64 val) {
        printk("mxdb emulation not implemented!\n");
}

static void emu_mxdbr (int rx, int ry) {
        printk("mxdbr emulation not implemented!\n");
}

static void emu_sdb (int rx, __u64 val) {
        current->thread.fp_regs.fprs[rx].d = __subdf3(current->thread.fp_regs.fprs[rx].d,
                                         val);
        set_CC_sf(current->thread.fp_regs.fprs[rx].d,0ULL);
}

static void emu_sdbr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].d = __subdf3(current->thread.fp_regs.fprs[rx].d,
                                         current->thread.fp_regs.fprs[ry].d);
        set_CC_sf(current->thread.fp_regs.fprs[rx].d,0ULL);
}

static void emu_seb (int rx, __u32 val) {
        current->thread.fp_regs.fprs[rx].f = __subsf3(current->thread.fp_regs.fprs[rx].f,
                                         val);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_sebr (int rx, int ry) {
        current->thread.fp_regs.fprs[rx].f = __subsf3(current->thread.fp_regs.fprs[rx].f,
                                         current->thread.fp_regs.fprs[ry].f);
        set_CC_sf(current->thread.fp_regs.fprs[rx].f,0);
}

static void emu_sfpc (int rx, int ry) {
        printk("sfpc emulation not implemented!\n");
}

static void emu_sqdb (int rx, __u64 val) {
        printk("sqdb emulation not implemented!\n");
}

static void emu_sqdbr (int rx, int ry) {
        printk("sqdbr emulation not implemented!\n");
}

static void emu_sqeb (int rx, __u32 val) {
        printk("sqeb emulation not implemented!\n");
}

static void emu_sqebr (int rx, int ry) {
        printk("sqebr emulation not implemented!\n");
}

static void emu_sqxbr (int rx, int ry) {
        printk("sqxbr emulation not implemented!\n");
}

static void emu_sxbr (int rx, int ry) {
        printk("sxbr emulation not implemented!\n");
}

static void emu_tcdb (int rx, __u64 val) {
        printk("tcdb emulation not implemented!\n");
}

static void emu_tceb (int rx, __u32 val) {
        printk("tceb emulation not implemented!\n");
}

static void emu_tcxb (int rx, __u64 val) {
        printk("tcxb emulation not implemented!\n");
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
                ((void (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
                emu_load_regd((opcode[3]>>4)&15);
                emu_load_regd(opcode[3]&15);
                return 0;
        case 2: /* RRE format, float operation */
                emu_store_rege((opcode[3]>>4)&15);
                emu_store_rege(opcode[3]&15);
                /* call the emulation function */
                ((void (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
                emu_load_rege((opcode[3]>>4)&15);
                emu_load_rege(opcode[3]&15);
                return 0;
        case 3: /* RRF format, double operation */
                emu_store_regd((opcode[3]>>4)&15);
                emu_store_regd(opcode[3]&15);
                /* call the emulation function */
                ((void (*)(int, int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15,opcode[2]>>4);
                emu_load_regd((opcode[3]>>4)&15);
                emu_load_regd(opcode[3]&15);
                return 0;
        case 4: /* RRF format, float operation */
                emu_store_rege((opcode[3]>>4)&15);
                emu_store_rege(opcode[3]&15);
                /* call the emulation function */
                ((void (*)(int, int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15,opcode[2]>>4);
                emu_load_rege((opcode[3]>>4)&15);
                emu_load_rege(opcode[3]&15);
                return 0;
        case 5: /* RRE format, cefbr instruction */
                emu_store_rege((opcode[3]>>4)&15);
                /* call the emulation function */
                ((void (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
                emu_load_rege((opcode[3]>>4)&15);
                return 0;
        case 6: /* RRE format, cdfbr & cxfbr instruction */
                emu_store_regd((opcode[3]>>4)&15);
                /* call the emulation function */
                ((void (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
                emu_load_regd((opcode[3]>>4)&15);
                return 0;
                /* FIXME !! */
                return 0;
        case 7: /* RRF format, cfebr instruction */
                emu_store_rege(opcode[3]&15);
                /* call the emulation function */
                ((void (*)(int, int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15,opcode[2]>>4);
                return 0;
        case 8: /* RRF format, cfdbr & cfxbr instruction */
                emu_store_regd(opcode[3]&15);
                /* call the emulation function */
                ((void (*)(int, int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15,opcode[2]>>4);
                return 0;
	case 9: /* RRE format, ldebr & mdebr instruction */
		/* float store but double load */
                emu_store_rege((opcode[3]>>4)&15);
                emu_store_rege(opcode[3]&15);
                /* call the emulation function */
                ((void (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
                emu_load_regd((opcode[3]>>4)&15);
                return 0;
        case 10: /* RRE format, ledbr instruction */
		/* double store but float load */
                emu_store_regd((opcode[3]>>4)&15);
                emu_store_regd(opcode[3]&15);
                /* call the emulation function */
                ((void (*)(int, int))jump_table[opcode[1]])
                        (opcode[3]>>4,opcode[3]&15);
                emu_load_rege((opcode[3]>>4)&15);
                return 0;
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
                /* FIXME: how to react if copy_from_user fails ? */
                copy_from_user(&temp, dxb, 8);
                /* call the emulation function */
                ((void (*)(int, __u64))jump_table[opcode[5]])
                        (opcode[1]>>4,temp);
                emu_load_regd((opcode[1]>>4)&15);
                return 0;
        }
        case 2: /* RXE format, __u32 constant */ {
                __u32 *dxb, temp;
                __u32 opc;

                emu_store_rege((opcode[1]>>4)&15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
                /* FIXME: how to react if get_user fails ? */
                get_user(temp, dxb);
                /* call the emulation function */
                ((void (*)(int, __u32))jump_table[opcode[5]])
                        (opcode[1]>>4,temp);
                emu_load_rege((opcode[1]>>4)&15);
                return 0;
        }
        case 3: /* RXF format, __u64 constant */ {
                __u32 *dxb, temp;
                __u32 opc;

                emu_store_regd((opcode[1]>>4)&15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
                /* FIXME: how to react if copy_from_user fails ? */
                copy_from_user(&temp, dxb, 8);
                /* call the emulation function */
                ((void (*)(int, __u32, int))jump_table[opcode[5]])
                        (opcode[1]>>4,temp,opcode[4]>>4);
                emu_load_regd((opcode[1]>>4)&15);
                return 0;
        }
        case 4: /* RXF format, __u32 constant */ {
                __u32 *dxb, temp;
                __u32 opc;

                emu_store_rege((opcode[1]>>4)&15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
                /* FIXME: how to react if get_user fails ? */
                get_user(temp, dxb);
                /* call the emulation function */
                ((void (*)(int, __u32, int))jump_table[opcode[5]])
                        (opcode[1]>>4,temp,opcode[4]>>4);
                emu_load_rege((opcode[1]>>4)&15);
                return 0;
        }
	case 5: /* RXE format, __u32 constant */
                /* store_rege and load_regd */ 
		{
                __u32 *dxb, temp;
                __u32 opc;
                emu_store_rege((opcode[1]>>4)&15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
                /* FIXME: how to react if get_user fails ? */
                get_user(temp, dxb);
                /* call the emulation function */
                ((void (*)(int, __u32))jump_table[opcode[5]])
                        (opcode[1]>>4,temp);
                emu_load_regd((opcode[1]>>4)&15);
                return 0;
        }
        default:
                return 1;
        }
}

/*
 * Emulate LDR Rx,Ry with Rx or Ry not in {0, 2, 4, 6}
 */
void math_emu_ldr(__u8 *opcode) {
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
}

/*
 * Emulate LER Rx,Ry with Rx or Ry not in {0, 2, 4, 6}
 */
void math_emu_ler(__u8 *opcode) {
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
}

/*
 * Emulate LD R,D(X,B) with R not in {0, 2, 4, 6}
 */
void math_emu_ld(__u8 *opcode, struct pt_regs * regs) {
        __u32 opc = *((__u32 *) opcode);
        __u64 *dxb;

        dxb = (__u64 *) calc_addr(regs,opc>>16,opc>>12,opc);
        /* FIXME: how to react if copy_from_user fails ? */
        copy_from_user(&current->thread.fp_regs.fprs[(opc>>20)&15].d, dxb, 8);
}

/*
 * Emulate LE R,D(X,B) with R not in {0, 2, 4, 6}
 */
void math_emu_le(__u8 *opcode, struct pt_regs * regs) {
        __u32 opc = *((__u32 *) opcode);
        __u32 *mem, *dxb;

        dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
        /* FIXME: how to react if get_user fails ? */
        mem = (__u32 *) (&current->thread.fp_regs.fprs[(opc>>20)&15].f);
        get_user(mem[0], dxb);
}

/*
 * Emulate STD R,D(X,B) with R not in {0, 2, 4, 6}
 */
void math_emu_std(__u8 *opcode, struct pt_regs * regs) {
        __u32 opc = *((__u32 *) opcode);
        __u64 *dxb;
        dxb = (__u64 *) calc_addr(regs,opc>>16,opc>>12,opc);
        /* FIXME: how to react if copy_to_user fails ? */
        copy_to_user(dxb, &current->thread.fp_regs.fprs[(opc>>20)&15].d, 8);
}

/*
 * Emulate STE R,D(X,B) with R not in {0, 2, 4, 6}
 */
void math_emu_ste(__u8 *opcode, struct pt_regs * regs) {
        __u32 opc = *((__u32 *) opcode);
        __u32 *mem, *dxb;
        dxb = (__u32 *) calc_addr(regs,opc>>16,opc>>12,opc);
        /* FIXME: how to react if put_user fails ? */
        mem = (__u32 *) (&current->thread.fp_regs.fprs[(opc>>20)&15].f);
        put_user(mem[0], dxb);
}

/*
 * Emulate LFPC D(B)
 */
int math_emu_lfpc(__u8 *opcode, struct pt_regs *regs) {
        /* FIXME: how to do that ?!? */
        return 0;
}

/*
 * Emulate STFPC D(B)
 */
int math_emu_stfpc(__u8 *opcode, struct pt_regs *regs) {
        /* FIXME: how to do that ?!? */
        return 0;
}

/*
 * Emulate SRNM D(B)
 */
int math_emu_srnm(__u8 *opcode, struct pt_regs *regs) {
        /* FIXME: how to do that ?!? */
        return 0;
}
















