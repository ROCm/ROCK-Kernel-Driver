/*
 * budget-patch.c: driver for Budget Patch,
 * hardware modification of DVB-S cards enabling full TS
 *
 * Written by Emard <emard@softhome.net>
 *
 * Original idea by Roberto Deza <rdeza@unav.es>
 *
 * Special thanks to Holger Waechtler, Michael Hunold, Marian Durkovic
 * and Metzlerbros
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 * 
 *
 * the project's page is at http://www.linuxtv.org/dvb/
 */

#include "budget.h"
#include "av7110.h"
#include "av7110_hw.h"

#define budget_patch budget

static struct saa7146_extension budget_extension;

MAKE_BUDGET_INFO(fs_1_3,"Siemens/Technotrend/Hauppauge PCI rev1.3+Budget_Patch", BUDGET_PATCH);

static struct pci_device_id pci_tbl[] = {
        MAKE_EXTENSION_PCI(fs_1_3,0x13c2, 0x0000),
        {
                .vendor    = 0,
        }
};

static int budget_wdebi(struct budget_patch *budget, u32 config, int addr, u32 val, int count)
{
        struct saa7146_dev *dev=budget->dev;

        DEB_EE(("budget: %p\n", budget));

        if (count <= 0 || count > 4)
                return -1;

        saa7146_write(dev, DEBI_CONFIG, config);

        saa7146_write(dev, DEBI_AD, val );
        saa7146_write(dev, DEBI_COMMAND, (count << 17) | (addr & 0xffff));
        saa7146_write(dev, MC2, (2 << 16) | 2);
        mdelay(5);

        return 0;
}


static int budget_av7110_send_fw_cmd(struct budget_patch *budget, u16* buf, int length)
{
        int i;

        DEB_EE(("budget: %p\n", budget));

        for (i = 2; i < length; i++)
                budget_wdebi(budget, DEBINOSWAP, COMMAND + 2*i, (u32) buf[i], 2);

        if (length)
                budget_wdebi(budget, DEBINOSWAP, COMMAND + 2, (u32) buf[1], 2);
        else
                budget_wdebi(budget, DEBINOSWAP, COMMAND + 2, 0, 2);

        budget_wdebi(budget, DEBINOSWAP, COMMAND, (u32) buf[0], 2);
        return 0;
}


static void av7110_set22k(struct budget_patch *budget, int state)
{
        u16 buf[2] = {( COMTYPE_AUDIODAC << 8) | (state ? ON22K : OFF22K), 0};
        
        DEB_EE(("budget: %p\n", budget));
        budget_av7110_send_fw_cmd(budget, buf, 2);
}


static int av7110_send_diseqc_msg(struct budget_patch *budget, int len, u8 *msg, int burst)
{
        int i;
        u16 buf[18] = { ((COMTYPE_AUDIODAC << 8) | SendDiSEqC),
                16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

        DEB_EE(("budget: %p\n", budget));

        if (len>10)
                len=10;

        buf[1] = len+2;
        buf[2] = len;

        if (burst != -1)
                buf[3]=burst ? 0x01 : 0x00;
        else
                buf[3]=0xffff;
                
        for (i=0; i<len; i++)
                buf[i+4]=msg[i];

        budget_av7110_send_fw_cmd(budget, buf, 18);
        return 0;
}


int budget_patch_diseqc_ioctl (struct dvb_frontend *fe, unsigned int cmd, void *arg)
{
        struct budget_patch *budget = fe->before_after_data;

        DEB_EE(("budget: %p\n", budget));

        switch (cmd) {
        case FE_SET_TONE:
                switch ((fe_sec_tone_mode_t) arg) {
                case SEC_TONE_ON:
                        av7110_set22k (budget, 1);
                        break;
                case SEC_TONE_OFF:
                        av7110_set22k (budget, 0);
                        break;
                default:
                        return -EINVAL;
                }
                break;

        case FE_DISEQC_SEND_MASTER_CMD:
        {
                struct dvb_diseqc_master_cmd *cmd = arg;

                av7110_send_diseqc_msg (budget, cmd->msg_len, cmd->msg, 0);
                break;
        }

        case FE_DISEQC_SEND_BURST:
                av7110_send_diseqc_msg (budget, 0, NULL, (int) (long) arg);
                break;

        default:
                return -EOPNOTSUPP;
        }

        return 0;
}


static int budget_patch_attach (struct saa7146_dev* dev, struct saa7146_pci_extension_data *info)
{
        struct budget_patch *budget;
        int err;
	int count = 0;

        if (!(budget = kmalloc (sizeof(struct budget_patch), GFP_KERNEL)))
                return -ENOMEM;

        DEB_EE(("budget: %p\n",budget));

        if ((err = ttpci_budget_init (budget, dev, info))) {
                kfree (budget);
                return err;
        }

/*
**      This code will setup the SAA7146_RPS1 to generate a square 
**      wave on GPIO3, changing when a field (TS_HEIGHT/2 "lines" of 
**      TS_WIDTH packets) has been acquired on SAA7146_D1B video port; 
**      then, this GPIO3 output which is connected to the D1B_VSYNC 
**      input, will trigger the acquisition of the alternate field 
**      and so on.
**      Currently, the TT_budget / WinTV_Nova cards have two ICs 
**      (74HCT4040, LVC74) for the generation of this VSYNC signal, 
**      which seems that can be done perfectly without this :-)).
*/                                                      

	// Setup RPS1 "program" (p35)

        // Wait reset Source Line Counter Threshold                     (p36)
        WRITE_RPS1(cpu_to_le32(CMD_PAUSE | RPS_INV | EVT_HS));
        // Wait Source Line Counter Threshold                           (p36)
        WRITE_RPS1(cpu_to_le32(CMD_PAUSE | EVT_HS));
        // Set GPIO3=1                                                  (p42)
        WRITE_RPS1(cpu_to_le32(CMD_WR_REG_MASK | (GPIO_CTRL>>2)));
        WRITE_RPS1(cpu_to_le32(GPIO3_MSK));
        WRITE_RPS1(cpu_to_le32(SAA7146_GPIO_OUTHI<<24));
        // Wait reset Source Line Counter Threshold                     (p36)
        WRITE_RPS1(cpu_to_le32(CMD_PAUSE | RPS_INV | EVT_HS));
        // Wait Source Line Counter Threshold
        WRITE_RPS1(cpu_to_le32(CMD_PAUSE | EVT_HS));
        // Set GPIO3=0                                                  (p42)
        WRITE_RPS1(cpu_to_le32(CMD_WR_REG_MASK | (GPIO_CTRL>>2)));
        WRITE_RPS1(cpu_to_le32(GPIO3_MSK));
        WRITE_RPS1(cpu_to_le32(SAA7146_GPIO_OUTLO<<24));
        // Jump to begin of RPS program                                 (p37)
        WRITE_RPS1(cpu_to_le32(CMD_JUMP));
        WRITE_RPS1(cpu_to_le32(dev->d_rps1.dma_handle));

        // Fix VSYNC level
        saa7146_setgpio(dev, 3, SAA7146_GPIO_OUTLO);
        // Set RPS1 Address register to point to RPS code               (r108 p42)
        saa7146_write(dev, RPS_ADDR1, dev->d_rps1.dma_handle);
        // Set Source Line Counter Threshold, using BRS                 (rCC p43)
        saa7146_write(dev, RPS_THRESH1, ((TS_HEIGHT/2) | MASK_12));
        // Enable RPS1                                                  (rFC p33)
        saa7146_write(dev, MC1, (MASK_13 | MASK_29));

        dvb_add_frontend_ioctls (budget->dvb_adapter,
                budget_patch_diseqc_ioctl, NULL, budget);

        dev->ext_priv = budget;

        return 0;
}


static int budget_patch_detach (struct saa7146_dev* dev)
{
        struct budget_patch *budget = (struct budget_patch*) dev->ext_priv;
        int err;

        dvb_remove_frontend_ioctls (budget->dvb_adapter,
                budget_patch_diseqc_ioctl, NULL);

        err = ttpci_budget_deinit (budget);

        kfree (budget);

        return err;
}


static int __init budget_patch_init(void) 
{
	return saa7146_register_extension(&budget_extension);
}


static void __exit budget_patch_exit(void)
{
        DEB_EE((".\n"));
        saa7146_unregister_extension(&budget_extension); 
}


static struct saa7146_extension budget_extension = {
        .name           = "budget_patch dvb\0",
        .flags          = 0,
        
        .module         = THIS_MODULE,
        .pci_tbl        = pci_tbl,
        .attach         = budget_patch_attach,
        .detach         = budget_patch_detach,

        .irq_mask       = MASK_10,
        .irq_func       = ttpci_budget_irq10_handler,
};


module_init(budget_patch_init);
module_exit(budget_patch_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emard, Roberto Deza, Holger Waechtler, Michael Hunold, others");
MODULE_DESCRIPTION("Driver for full TS modified DVB-S SAA7146+AV7110 "
                   "based so-called Budget Patch cards");

