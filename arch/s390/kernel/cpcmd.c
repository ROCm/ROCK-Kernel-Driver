/*
 *  arch/s390/kernel/cpcmd.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/ebcdic.h>
#include <linux/spinlock.h>
#include <asm/cpcmd.h>
#include <asm/system.h>
#include <linux/slab.h>
#include <asm/errno.h>

static spinlock_t cpcmd_lock = SPIN_LOCK_UNLOCKED;
static char cpcmd_buf[128];

void cpcmd(char *cmd, char *response, int rlen)
{
        const int mask = 0x40000000L;
	unsigned long flags;
        int cmdlen;

	spin_lock_irqsave(&cpcmd_lock, flags);
        cmdlen = strlen(cmd);
        strcpy(cpcmd_buf, cmd);
        ASCEBC(cpcmd_buf, cmdlen);

        if (response != NULL && rlen > 0) {
#ifndef CONFIG_ARCH_S390X
                asm volatile ("LRA   2,0(%0)\n\t"
                              "LR    4,%1\n\t"
                              "O     4,%4\n\t"
                              "LRA   3,0(%2)\n\t"
                              "LR    5,%3\n\t"
                              ".long 0x83240008 # Diagnose 83\n\t"
                              : /* no output */
                              : "a" (cpcmd_buf), "d" (cmdlen),
                                "a" (response), "d" (rlen), "m" (mask)
                              : "2", "3", "4", "5" );
#else /* CONFIG_ARCH_S390X */
                asm volatile ("   lrag  2,0(%0)\n"
                              "   lgr   4,%1\n"
                              "   o     4,%4\n"
                              "   lrag  3,0(%2)\n"
                              "   lgr   5,%3\n"
                              "   sam31\n"
                              "   .long 0x83240008 # Diagnose 83\n"
                              "   sam64"
                              : /* no output */
                              : "a" (cpcmd_buf), "d" (cmdlen),
                                "a" (response), "d" (rlen), "m" (mask)
                              : "2", "3", "4", "5" );
#endif /* CONFIG_ARCH_S390X */
                EBCASC(response, rlen);
        } else {
#ifndef CONFIG_ARCH_S390X
                asm volatile ("LRA   2,0(%0)\n\t"
                              "LR    3,%1\n\t"
                              ".long 0x83230008 # Diagnose 83\n\t"
                              : /* no output */
                              : "a" (cpcmd_buf), "d" (cmdlen)
                              : "2", "3"  );
#else /* CONFIG_ARCH_S390X */
                asm volatile ("   lrag  2,0(%0)\n"
                              "   lgr   3,%1\n"
                              "   sam31\n"
                              "   .long 0x83230008 # Diagnose 83\n"
                              "   sam64"
                              : /* no output */
                              : "a" (cpcmd_buf), "d" (cmdlen)
                              : "2", "3"  );
#endif /* CONFIG_ARCH_S390X */
        }
	spin_unlock_irqrestore(&cpcmd_lock, flags);
}

/* improved cpcmd for cpint module */
int cpint_cpcmd(CPCmd_Dev *devExt)
{
    const int mask = 0x60000000L;
    int rspSize = -EOPNOTSUPP;
    int cc = -1;

    if (!MACHINE_IS_VM)
	goto out;

    while (cc != 0)
    {
        asm volatile("LRA   2,0(%3)\t/* Get cmd address */\n\t"
		      "LR    4,%4\t/* Get length of command */\n\t"
		      "O     4,%7\t/* Set flags */\n\t"
		      "LRA   3,0(%5)\t/* Get response address */\n\t"
		      "LR    5,%6\t/* Get response length */\n\t"
		      "AHI   5,-1\t/* Leave room for eol */\n\t"
#ifdef __s390x__
		      "SAM31\t\t/* Get into 31 bit mode */\n\t"
#endif
		      ".long 0x83240008\t/* Issue command */\n\t"
#ifdef __s390x__
		      "SAM64\t\t/* Return to 64 bit mode */\n\t"
#endif
		      "IPM   %1\t\t/* Get CC */\n\t"
		      "SRL   %1,28\t/* Shuffle down */\n\t"
#ifdef __s390x__
		      "LGFR  %0,4\t/* Keep return code */\n\t"
#else
		      "LR    %0,4\t/* Keep return code */\n\t"
#endif
		      "LR    %2,5\t/* Get response length */\n\t"
		      : "=d" (devExt->rc), "=d" (cc), "=d" (rspSize)
		      : "a" (devExt->cmd), "d" (devExt->count),
		        "a" (devExt->data), "d" (devExt->size),
		        "m" (mask)
		      : "cc", "2", "3", "4", "5");
	if (cc != 0) {
	    if (rspSize <= 65536) {
		devExt->size += rspSize + 1;
		devExt->data = kmalloc(devExt->size, GFP_DMA);
		if (devExt->data) {
		    memset(devExt->data, 0, devExt->size);
		    continue;
		}
	    }
	    return -ENOMEM;
	}
    }
    devExt->size = rspSize;
out:
    return rspSize;
}
