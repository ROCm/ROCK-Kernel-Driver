/*
 *  arch/s390/kernel/cpcmd.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#ifndef __CPCMD__
#define __CPCMD__

#include <linux/wait.h>
/*
 * For cpint_cpcmd:
 */
typedef struct CPInt_Dev {
   void  *devExt;              /* Device type extension     */
   uid_t devOwner;             /* Current owner of device   */
   int   devLock;              /* Lock word for device      */
   wait_queue_head_t devWait;  /* Wait queue for device     */
} CPInt_Dev;

typedef struct CPCmd_Dev {
   char cmd[240];            /* The CP command                 */
   char *data;               /* CP command response buffer     */
   CPInt_Dev *dev;           /* Pointer to the base device     */
   unsigned long count;      /* Length of the command          */
   unsigned long size;       /* Length of the response         */
   int rc;                   /* Return code from CP command    */
   int flag;                /* Options flag                   */
#define UPCASE 0x80         /* Only command is uppercased     */
} CPCmd_Dev;

/*
 * Exported function for cpint module:
 */
extern int cpint_cpcmd(CPCmd_Dev *devExt);

extern void cpcmd(char *cmd, char *response, int rlen);

#endif
