#ifndef _ACI_H_
#define _ACI_H_

extern int aci_port;
extern int aci_idcode[2];	/* manufacturer and product ID */
extern int aci_version;		/* ACI firmware version	*/
extern int aci_rw_cmd(int write1, int write2, int write3);

extern char * aci_radio_name;
extern int aci_rds_cmd(unsigned char cmd, unsigned char databuffer[], int datasize);

#define aci_indexed_cmd(a, b) aci_rw_cmd(a, b, -1)
#define aci_write_cmd(a, b)   aci_rw_cmd(a, b, -1)
#define aci_read_cmd(a)       aci_rw_cmd(a,-1, -1)

#define COMMAND_REGISTER    (aci_port)      /* write register */
#define STATUS_REGISTER     (aci_port + 1)  /* read register */
#define BUSY_REGISTER       (aci_port + 2)  /* also used for rds */

#define RDS_REGISTER        BUSY_REGISTER

#define RDS_STATUS      0x01
#define RDS_STATIONNAME 0x02
#define RDS_TEXT        0x03
#define RDS_ALTFREQ     0x04
#define RDS_TIMEDATE    0x05
#define RDS_PI_CODE     0x06
#define RDS_PTYTATP     0x07
#define RDS_RESET       0x08
#define RDS_RXVALUE     0x09

/*
 * The following macro SCALE can be used to scale one integer volume
 * value into another one using only integer arithmetic. If the input
 * value x is in the range 0 <= x <= xmax, then the result will be in
 * the range 0 <= SCALE(xmax,ymax,x) <= ymax.
 *
 * This macro has for all xmax, ymax > 0 and all 0 <= x <= xmax the
 * following nice properties:
 *
 * - SCALE(xmax,ymax,xmax) = ymax
 * - SCALE(xmax,ymax,0) = 0
 * - SCALE(xmax,ymax,SCALE(ymax,xmax,SCALE(xmax,ymax,x))) = SCALE(xmax,ymax,x)
 *
 * In addition, the rounding error is minimal and nicely distributed.
 * The proofs are left as an exercise to the reader.
 */

#define SCALE(xmax,ymax,x) (((x)*(ymax)+(xmax)/2)/(xmax))

extern void __exit unload_aci_rds(void);
extern int __init attach_aci_rds(void);


#endif  /* _ACI_H_ */
