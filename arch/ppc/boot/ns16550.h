/*
 * NS16550 Serial Port
 */

struct NS16550
 {
  unsigned char rbr;  /* 0 */
  unsigned char ier;  /* 1 */
  unsigned char fcr;  /* 2 */
  unsigned char lcr;  /* 3 */
  unsigned char mcr;  /* 4 */
  unsigned char lsr;  /* 5 */
  unsigned char msr;  /* 6 */
  unsigned char scr;  /* 7 */
 };

#define thr rbr
#define iir fcr
#define dll rbr
#define dlm ier

#define LSR_DR   0x01  /* Data ready */
#define LSR_OE   0x02  /* Overrun */
#define LSR_PE   0x04  /* Parity error */
#define LSR_FE   0x08  /* Framing error */
#define LSR_BI   0x10  /* Break */
#define LSR_THRE 0x20  /* Xmit holding register empty */
#define LSR_TEMT 0x40  /* Xmitter empty */
#define LSR_ERR  0x80  /* Error */

#define COM1 0x800003F8
#define COM2 0x800002F8
#define COM3 0x800003F8
#define COM4 0x80000388
