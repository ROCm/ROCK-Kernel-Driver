/* to be used by qlogicfas and qlogic_cs */
#ifndef __QLOGICFAS_H
#define __QLOGICFAS_H
struct qlogicfas_priv {
	 int		qbase;		/* Port */
	 int		qinitid;	/* initiator ID */
	 int		qabort;		/* Flag to cause an abort */
	 int		qlirq;		/* IRQ being used */
	 char		qinfo[80];	/* description */
	 Scsi_Cmnd 	*qlcmd;		/* current command being processed */
};
typedef struct qlogicfas_priv *qlogicfas_priv_t;
#endif	/* __QLOGICFAS_H */

