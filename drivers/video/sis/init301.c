/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/sis/init301.c,v 1.3 2002/22/04 01:16:16 dawes Exp $ */
/*
 * Mode switching code (CRT2 section) for SiS 300/540/630/730/315/550/650/740
 * (Universal module for Linux kernel framebuffer, XFree86 4.x)
 *
 * Assembler-To-C translation
 * Parts Copyright 2002 by Thomas Winischhofer <thomas@winischhofer.net>
 *
 * Based on BIOS
 *     1.10.07, 1.10a for SiS650/LVDS+CH7019
 *     1.07.1b for SiS650/301(B/LV)
 *     2.04.50 (I) and 2.04.5c (II) for SiS630/301(B)
 *     2.02.3b, 2.03.02, 2.04.2c and 2.04.5c for 630/LVDS/LVDS+CH7005
 *     1.09b for 315/301(B)
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holder not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  The copyright holder makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDER DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "init301.h"

#if 0
#define TWPANEL
#endif

#if 0	/* TW: Emulate 650/LVDS BIOS 1.10a (1) or 1.10.07 (0) */
#define TEST1400
#endif

#ifdef SIS300
#include "oem300.h"
#endif

#ifdef SIS315H
#include "oem310.h"
#endif

#define SiS_I2CDELAY 1000
#define SiS_I2CDELAYSHORT 333

BOOLEAN
SiS_SetCRT2Group301(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                    PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
   USHORT ModeIdIndex;
   USHORT RefreshRateTableIndex;

   SiS_SetFlag |= ProgrammingCRT2;

   SiS_SearchModeID(ROMAddr,&ModeNo,&ModeIdIndex);

   /* TW: Used for shifting CR33 */
   SiS_SelectCRT2Rate = 4;

   SiS_UnLockCRT2(HwDeviceExtension, BaseAddr);

   RefreshRateTableIndex = SiS_GetRatePtrCRT2(ROMAddr,ModeNo,ModeIdIndex);

   SiS_SaveCRT2Info(ModeNo);

   if(SiS_LowModeStuff(ModeNo,HwDeviceExtension)) {
      SiS_DisableBridge(HwDeviceExtension,BaseAddr);
      SiS_SetCRT2ModeRegs(BaseAddr,ModeNo,ModeIdIndex,HwDeviceExtension);
   }

   if(SiS_VBInfo & DisableCRT2Display) {
      SiS_LockCRT2(HwDeviceExtension, BaseAddr);
      SiS_DisplayOn();
      return(FALSE);
   }

   SiS_GetCRT2Data(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                   HwDeviceExtension);

   /* LVDS, 650/301LV(LCDA) and 630/301B BIOS set up Panel Link */
   if((SiS_IF_DEF_LVDS == 1) || (SiS_VBType & VB_SIS301BLV302BLV)) {
   	SiS_GetLVDSDesData(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
	                   HwDeviceExtension);
   }

   if(SiS_LowModeStuff(ModeNo,HwDeviceExtension)) {
      SiS_SetGroup1(BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
                    HwDeviceExtension,RefreshRateTableIndex);
   }

   if( (SiS_VBType & VB_SIS301BLV302BLV) && (SiS_VBInfo & SetCRT2ToLCDA) ) {

     	if(SiS_VBType & (VB_SIS301LV | VB_SIS302LV))
	      	SiS_SetReg1(SiS_Part4Port,0x24,0x0e);

   } else if((SiS_IF_DEF_LVDS == 0) && (!(SiS_VBInfo & SetCRT2ToLCDA))) {

        if(SiS_LowModeStuff(ModeNo,HwDeviceExtension)) {

	   SiS_SetGroup2(BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	              RefreshRateTableIndex,HwDeviceExtension);
      	   SiS_SetGroup3(BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	              HwDeviceExtension);
      	   SiS_SetGroup4(BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	              RefreshRateTableIndex,HwDeviceExtension);
      	   SiS_SetGroup5(BaseAddr,ROMAddr,ModeNo,ModeIdIndex);

	   /* TW: 630/301B BIOS does all this: */
	   if(HwDeviceExtension->jChipType < SIS_315H) {
	      if(SiS_VBType & VB_SIS301BLV302BLV) {
	         if(SiS_VBInfo & SetCRT2ToLCD) {
		    if(SiS_LCDResInfo != Panel640x480) {
		       SiS_ModCRT1CRTC(ROMAddr,ModeNo,ModeIdIndex,
		                       RefreshRateTableIndex,HwDeviceExtension);
                    }
		    SiS_SetCRT2ECLK(ROMAddr,ModeNo,ModeIdIndex,
		                    RefreshRateTableIndex,HwDeviceExtension);
		 }
              }
	   }

        }

   } else {

        if(SiS_LCDResInfo != Panel640x480) {
	        if (SiS_IF_DEF_TRUMPION == 0) {
    	 	        SiS_ModCRT1CRTC(ROMAddr,ModeNo,ModeIdIndex,
		                        RefreshRateTableIndex,HwDeviceExtension);
	        }
	}
        if(SiS_IF_DEF_FSTN == 0) {
     	 	SiS_SetCRT2ECLK(ROMAddr,ModeNo,ModeIdIndex,
		          RefreshRateTableIndex,HwDeviceExtension);
	}
	if(SiS_LowModeStuff(ModeNo,HwDeviceExtension)) {
     	  if(SiS_IF_DEF_CH70xx != 0) {
	     /* TW: Inserted from 650/LVDS BIOS */
	     if (SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
	        if(SiS_IF_DEF_CH70xx == 2) {
		    SiS_SetCHTVForLCD(HwDeviceExtension,BaseAddr);
		}
	     }
	     if(SiS_VBInfo & SetCRT2ToTV) {
	        /* TW: Set Chrontel registers only if CRT2 is TV */
       		SiS_SetCHTVReg(ROMAddr,ModeNo,ModeIdIndex,
		               RefreshRateTableIndex);
	     }
     	  }
	}

   }

#ifdef SIS300
   if ( (HwDeviceExtension->jChipType==SIS_540)||
        (HwDeviceExtension->jChipType==SIS_630)||
        (HwDeviceExtension->jChipType==SIS_730)||
        (HwDeviceExtension->jChipType==SIS_300) )
    {
	if(SiS_LowModeStuff(ModeNo,HwDeviceExtension)) {
       	   SiS_OEM300Setting(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo);
	}
    }
#endif

#ifdef SIS315H
   if ( (HwDeviceExtension->jChipType==SIS_315H)||
        (HwDeviceExtension->jChipType==SIS_315PRO)||
        (HwDeviceExtension->jChipType==SIS_550) ||
        (HwDeviceExtension->jChipType==SIS_640) ||
        (HwDeviceExtension->jChipType==SIS_740) ||
        (HwDeviceExtension->jChipType==SIS_650))
   {
        if(SiS_LowModeStuff(ModeNo,HwDeviceExtension)) {
	   SiS_OEMLCD(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
           SiS_OEM310Setting(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
           SiS_CRT2AutoThreshold(BaseAddr);
        }
   }
#endif

   if(SiS_LowModeStuff(ModeNo,HwDeviceExtension)) {
      SiS_DisplayOn();
      SiS_EnableBridge(HwDeviceExtension,BaseAddr);
   }

   if(SiS_IF_DEF_CH70xx == 1) {
	if(SiS_VBInfo & SetCRT2ToTV) {
	     /* TW: Disable LCD panel when using TV */
	     SiS_SetRegOR(SiS_P3c4,0x11,0x0C);
	} else {
	     /* TW: Disable TV when using LCD */
	     SiS_SetCH70xxANDOR(0x010E,0xF8);
	}
   }

   SiS_DisplayOn();

   if(SiS_LowModeStuff(ModeNo,HwDeviceExtension)) {
      SiS_LockCRT2(HwDeviceExtension, BaseAddr);
   }

   return 1;
}

/* TW: Checked with 650/LVDS (1.10.07) and 630+301B/LVDS BIOS */
BOOLEAN
SiS_LowModeStuff(USHORT ModeNo,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
    USHORT temp,temp1,temp2;

    if((ModeNo != 0x03) && (ModeNo != 0x10) && (ModeNo != 0x12))
         return(1);
    temp = SiS_GetReg1(SiS_P3d4,0x11);
    SiS_SetRegOR(SiS_P3d4,0x11,0x80);
    temp1 = SiS_GetReg1(SiS_P3d4,0x00);
    SiS_SetReg1(SiS_P3d4,0x00,0x55);
    temp2 = SiS_GetReg1(SiS_P3d4,0x00);
    SiS_SetReg1(SiS_P3d4,0x00,temp1);
    SiS_SetReg1(SiS_P3d4,0x11,temp);
    if(HwDeviceExtension->jChipType >= SIS_315H) {
       if(temp2 == 0x55) return(0);
       else return(1);
    } else {
       if(temp2 != 0x55) return(1);
       else {
          SiS_SetRegOR(SiS_P3d4,0x35,0x01);
          return(0);
       }
    }
}

/* TW: Set Part1 registers */
/* TW: Checked with 650/LVDS (1.10.07), 650/301LV (II) and 630/301B (II) BIOS */
void
SiS_SetGroup1(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
              PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex)
{
  USHORT  temp=0,tempax=0,tempbx=0,tempcx=0,tempbl=0;
  USHORT  pushbx=0,CRT1Index=0;
  USHORT  modeflag,resinfo=0;

  if(ModeNo<=0x13) {
	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
    	CRT1Index = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
    	resinfo = SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  /* TW: Removed 301B301LV.. check here; LCDA exists with LVDS as well */
  if(SiS_VBInfo & SetCRT2ToLCDA) {

	   /* TW: From 650/LVDS BIOS; 301(B+LV) version does not set Sync  */
	   if (SiS_IF_DEF_LVDS == 1) {
	       SiS_SetCRT2Sync(BaseAddr,ROMAddr,ModeNo,
                               RefreshRateTableIndex,HwDeviceExtension);
	   }

	   SiS_SetGroup1_LCDA(BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
     	                   HwDeviceExtension,RefreshRateTableIndex);

  } else {

     if( (HwDeviceExtension->jChipType >= SIS_315H) &&
         (SiS_IF_DEF_LVDS == 1) &&
	 (SiS_VBInfo & SetInSlaveMode)) {

        SiS_SetCRT2Sync(BaseAddr,ROMAddr,ModeNo,
                        RefreshRateTableIndex,HwDeviceExtension);

     } else {

        SiS_SetCRT2Offset(SiS_Part1Port,ROMAddr,ModeNo,ModeIdIndex,
      		       RefreshRateTableIndex,HwDeviceExtension);

        if (HwDeviceExtension->jChipType < SIS_315H ) {
#ifdef SIS300
    	      SiS_SetCRT2FIFO_300(ROMAddr,ModeNo,HwDeviceExtension);
#endif
        } else {
#ifdef SIS315H
              SiS_SetCRT2FIFO_310(ROMAddr,ModeNo,HwDeviceExtension);
#endif
	}

        SiS_SetCRT2Sync(BaseAddr,ROMAddr,ModeNo,
                        RefreshRateTableIndex,HwDeviceExtension);

	/* 1. Horizontal setup */

        if (HwDeviceExtension->jChipType < SIS_315H ) {

                /* ------------- 300 series --------------*/

    		temp = (SiS_VGAHT - 1) & 0x0FF;   			/* BTVGA2HT 0x08,0x09 */
    		SiS_SetReg1(SiS_Part1Port,0x08,temp);                   /* TW: CRT2 Horizontal Total */

    		temp = (((SiS_VGAHT - 1) & 0xFF00) >> 8) << 4;
    		SiS_SetRegANDOR(SiS_Part1Port,0x09,0x0f,temp);          /* TW: CRT2 Horizontal Total Overflow [7:4] */

    		temp = (SiS_VGAHDE + 12) & 0x0FF;                       /* BTVGA2HDEE 0x0A,0x0C */
    		SiS_SetReg1(SiS_Part1Port,0x0A,temp);                   /* TW: CRT2 Horizontal Display Enable End */

    		pushbx = SiS_VGAHDE + 12;                               /* bx  BTVGA@HRS 0x0B,0x0C */
    		tempcx = (SiS_VGAHT - SiS_VGAHDE) >> 2;
    		tempbx = pushbx + tempcx;
    		tempcx <<= 1;
    		tempcx += tempbx;

    		if(SiS_IF_DEF_LVDS == 0) {
      			if(SiS_VBInfo & SetCRT2ToRAMDAC){
			        CRT1Index &= 0x3F;
        			tempbx = SiS_CRT1Table[CRT1Index].CR[4];
        			tempbx |= ((SiS_CRT1Table[CRT1Index].CR[14] & 0xC0) << 2);
        			tempbx = (tempbx - 1) << 3;
        			tempcx = SiS_CRT1Table[CRT1Index].CR[5];
        			tempcx &= 0x1F;
        			temp = SiS_CRT1Table[CRT1Index].CR[15];
        			temp = (temp & 0x04) << (6-2);
        			tempcx = ((tempcx | temp) - 1) << 3;
      			}

    			if((SiS_VBInfo & SetCRT2ToTV) && (resinfo == 0x08)){
        			if(!(SiS_VBInfo & SetPALTV)){
      					tempbx = 1040;
      					tempcx = 1042;
      				}
    			}
	        }

    		temp = tempbx & 0x00FF;
    		SiS_SetReg1(SiS_Part1Port,0x0B,temp);                   /* TW: CRT2 Horizontal Retrace Start */

 	} else {

     	   /* ---------------------- 310 series ------------------*/    /* (BIOS label Gr1_301) */

     	   if (modeflag & HalfDCLK) {  /* for low resolution modes */

         	temp = ((SiS_VGAHT / 2) - 1) & 0xFF;                    /* BTVGA2HT 0x08,0x09 */
         	SiS_SetReg1(SiS_Part1Port,0x08,temp);                   /* TW: CRT2 Horizontal Total */

		temp = ((((SiS_VGAHT / 2) - 1) & 0xFF00) >> 8) << 4;
        	SiS_SetRegANDOR(SiS_Part1Port,0x09,0x0F,temp);        /* TW: CRT2 Horizontal Total Overflow [7:4] */

         	temp = ((SiS_VGAHDE / 2) + 16) & 0xFF;                  /* BTVGA2HDEE 0x0A,0x0C */
         	SiS_SetReg1(SiS_Part1Port,0x0A,temp);                   /* TW: CRT2 Horizontal Display Enable End */

         	pushbx = (SiS_VGAHDE / 2) + 16;
         	tempcx = ((SiS_VGAHT - SiS_VGAHDE) / 2) >> 2;           /* cx */
		if(SiS_IF_DEF_LVDS == 1)
		           tempcx >>= 1;    /* TW: From LVDS 1.10.07; not done on 301(LV) */
         	tempbx = pushbx + tempcx;                               /* bx  BTVGA@HRS 0x0B,0x0C */
         	tempcx += tempbx;

         	if(SiS_IF_DEF_LVDS == 0) {
                   if(SiS_VBInfo & SetCRT2ToRAMDAC){
                	tempbx = SiS_CRT1Table[CRT1Index].CR[4];
                	tempbx |= ((SiS_CRT1Table[CRT1Index].CR[14] & 0xC0) << 2);
                	tempbx = (tempbx - 3) << 3;         		/*(VGAHRS-3)*8 */
                	tempcx = SiS_CRT1Table[CRT1Index].CR[5];
               		tempcx &= 0x1F;
                	temp = SiS_CRT1Table[CRT1Index].CR[15];
                	temp = (temp & 0x04) << (5-2);      		/* VGAHRE D[5]  */
                	tempcx =((tempcx | temp) - 3) << 3;    		/* (VGAHRE-3)*8 */
             	   }
                   /* TW: The following is not done in 650/LVDS BIOS  */
         	   tempbx += 4;
         	   tempcx += 4;

         	   if (tempcx > (SiS_VGAHT / 2))
              		   tempcx = SiS_VGAHT / 2;
         	}

                temp = tempbx & 0x00FF;
         	SiS_SetReg1(SiS_Part1Port,0x0B,temp);                  /* TW: CRT2 Horizontal Retrace Start */

    	   } else {			/* for high resolution modes */

         	temp = (SiS_VGAHT - 1) & 0xFF;                       	/* BTVGA2HT 0x08,0x09 */
         	SiS_SetReg1(SiS_Part1Port,0x08,temp);                  /* TW: CRT2 Horizontal Total */

         	temp = (((SiS_VGAHT - 1) & 0xFF00) >> 8 ) << 4;
	 	SiS_SetRegANDOR(SiS_Part1Port,0x09,0x0F,temp);         /* TW: CRT2 Horizontal Total Overflow [7:4] */

         	temp = (SiS_VGAHDE + 16) & 0xFF;                       /* BTVGA2HDEE 0x0A,0x0C */
	 	SiS_SetReg1(SiS_Part1Port,0x0A,temp);                  /* TW: CRT2 Horizontal Display Enable End */

         	pushbx = SiS_VGAHDE + 16;
         	tempcx = (SiS_VGAHT - SiS_VGAHDE) >> 2;                /* cx */
		if(SiS_IF_DEF_LVDS == 1)
		           tempcx >>= 1;    /* TW: From LVDS 1.10.07; not done on 301(LV) */
         	tempbx = pushbx + tempcx;                              /* bx  BTVGA@HRS 0x0B,0x0C */
         	tempcx += tempbx;

         	if(SiS_IF_DEF_LVDS==0) {
             	   if(SiS_VBInfo & SetCRT2ToRAMDAC){
                	tempbx = SiS_CRT1Table[CRT1Index].CR[4];
                	tempbx |= ((SiS_CRT1Table[CRT1Index].CR[14] & 0xC0) << 2);
                	tempbx = (tempbx - 3) << 3;         		/*(VGAHRS-3)*8 */
                	tempcx = SiS_CRT1Table[CRT1Index].CR[5];
               		tempcx &= 0x1F;
                	temp = SiS_CRT1Table[CRT1Index].CR[15];
                	temp = (temp & 0x04) << (5-2);      		/* VGAHRE D[5] */
                	tempcx = ((tempcx | temp) - 3) << 3;    	/* (VGAHRE-3)*8 */
                	tempbx += 16;
                	tempcx += 16;
             	   }
		   /* TW: The entire following section is not done in 650/LVDS BIOS */
         	   if (tempcx > SiS_VGAHT)
        		tempcx = SiS_VGAHT;

         	   if((SiS_VBInfo & SetCRT2ToTV) && (resinfo == 0x08)){
             	      if(!(SiS_VBInfo & SetPALTV)){
      		 	 tempbx = 1040;
      		 	 tempcx = 1042;
      	     	      }
         	   }
                }

         	temp = tempbx & 0x00FF;
	 	SiS_SetReg1(SiS_Part1Port,0x0B,temp);                 /* TW: CRT2 Horizontal Retrace Start */

     	   } /* halfdclk */

     	}  /* 310 series */

  	/* TW: The following is done for all bridge/chip types/series */

  	tempax = tempbx & 0xFF00;
  	tempbx = pushbx;
  	tempbx = (tempbx & 0x00FF) | ((tempbx & 0xFF00) << 4);
  	tempax |= (tempbx & 0xFF00);
  	temp = (tempax & 0xFF00) >> 8;
  	SiS_SetReg1(SiS_Part1Port,0x0C,temp);                        /* TW: Overflow */

  	temp = tempcx & 0x00FF;
  	SiS_SetReg1(SiS_Part1Port,0x0D,temp);                        /* TW: CRT2 Horizontal Retrace End */

  	/* 2. Vertical setup */

  	tempcx = SiS_VGAVT - 1;
  	temp = tempcx & 0x00FF;

	/* TW: Matches 650/301LV, 650/LVDS, 630/LVDS(CLEVO), 630/LVDS(no-Ch7005) */
        if(SiS_IF_DEF_LVDS == 1) {
	     if(HwDeviceExtension->jChipType < SIS_315H) {
	          if(SiS_IF_DEF_CH70xx != 0) {
#ifndef TWPANEL
		       if(SiS_VBInfo & (SetCRT2ToSVIDEO|SetCRT2ToAVIDEO)) {
		           temp--;
		       }
#else
		       temp--;
#endif
                  }
	     } else {
	          if(SiS_IF_DEF_CH70xx != 0) {
 		      temp--;
                  }
             }
        }
  	SiS_SetReg1(SiS_Part1Port,0x0E,temp);                        /* TW: CRT2 Vertical Total */

  	tempbx = SiS_VGAVDE - 1;
  	temp = tempbx & 0x00FF;
  	SiS_SetReg1(SiS_Part1Port,0x0F,temp);                        /* TW: CRT2 Vertical Display Enable End */

  	temp = ((tempbx & 0xFF00) << 3) >> 8;
  	temp |= ((tempcx & 0xFF00) >> 8);
  	SiS_SetReg1(SiS_Part1Port,0x12,temp);                        /* TW: Overflow (and HWCursor Test Mode) */

	/* TW: For 650/LVDS */
	if((HwDeviceExtension->jChipType >= SIS_315H) && (SiS_IF_DEF_LVDS == 1)) {
           tempbx++;
   	   tempax = tempbx;
	   tempcx++;
	   tempcx = tempcx - tempax;
	   tempcx >>= 2;
	   tempbx = tempbx + tempcx;
	   if(tempcx < 4) tempcx = 4;
	   tempcx >>= 2;
	   tempcx = tempcx + tempbx;
	   tempcx++;
	} else {
	   /* TW: For 630/LVDS/301B and 650/301LV: */
  	   tempbx = (SiS_VGAVT + SiS_VGAVDE) >> 1;                      /*  BTVGA2VRS     0x10,0x11   */
  	   tempcx = ((SiS_VGAVT - SiS_VGAVDE) >> 4) + tempbx + 1;       /*  BTVGA2VRE     0x11        */
	}

  	if(SiS_IF_DEF_LVDS == 0) {
    	   if(SiS_VBInfo & SetCRT2ToRAMDAC){
      		tempbx = SiS_CRT1Table[CRT1Index].CR[8];
      		temp = SiS_CRT1Table[CRT1Index].CR[7];
      		if(temp & 0x04) tempbx |= 0x0100;
      		if(temp & 0x80) tempbx |= 0x0200;
      		temp = SiS_CRT1Table[CRT1Index].CR[13];
      		if(temp & 0x08) tempbx |= 0x0400;
      		temp = SiS_CRT1Table[CRT1Index].CR[9];
      		tempcx = (tempcx & 0xFF00) | (temp & 0x00FF);
    	   }
  	}
  	temp = tempbx & 0x00FF;
  	SiS_SetReg1(SiS_Part1Port,0x10,temp);                        /* TW: CRT2 Vertical Retrace Start */

  	temp = ((tempbx & 0xFF00) >> 8) << 4;
  	temp |= (tempcx & 0x000F);
  	SiS_SetReg1(SiS_Part1Port,0x11,temp);                        /* TW: CRT2 Vert. Retrace End; Overflow; "Enable CRTC Check" */

  	/* 3. Paneldelay */

  	if (HwDeviceExtension->jChipType < SIS_315H ) {

    	   /* ---------- 300 series -------------- */

	   if(SiS_IF_DEF_LVDS == 0) {
	        temp = 0x20;
		if(SiS_LCDResInfo == Panel1280x960) temp = 0x24;     /* TW: Not in 630/301B BIOS */
		if(SiS_VBInfo & SetCRT2ToTV) temp = 0x08;
		if(SiS_VBInfo & SetCRT2ToHiVisionTV) {		     /* TW: Not in 630/301B BIOS */
      		    if(SiS_VBInfo & SetInSlaveMode) temp = 0x2c;     /* TW: Not in 630/301B BIOS */
      		    else temp = 0x20;                                /* TW: Not in 630/301B BIOS */
    	        }
		if((ROMAddr) && (SiS_VBType & VB_SIS301BLV302BLV)) {
		    if(ROMAddr[0x220] & 0x80) {
		        if(SiS_VBInfo & (SetCRT2ToTV-SetCRT2ToHiVisionTV)) temp = ROMAddr[0x221];
			else if(SiS_VBInfo & SetCRT2ToHiVisionTV) temp = ROMAddr[0x222];
		        else if(SiS_LCDResInfo == Panel1280x1024) temp = ROMAddr[0x223];
			else temp = ROMAddr[0x224];
			temp &= 0x3c;
		    }
		}
		if(HwDeviceExtension->pdc) {
			temp = HwDeviceExtension->pdc & 0x3c;
		}
	   } else {
	        temp = 0x20;
		if(SiS_LCDResInfo == Panel640x480) temp = 0x04;
		if(ROMAddr) {
		    if(ROMAddr[0x220] & 0x80) {
		        temp = ROMAddr[0x220] & 0x3c;
		    }
		}
		if(HwDeviceExtension->pdc) {
			temp = HwDeviceExtension->pdc & 0x3c;
		}
	   }

    	   SiS_SetRegANDOR(SiS_Part1Port,0x13,~0x03C,temp);         /* TW: Panel Link Delay Compensation; (Software Command Reset; Power Saving) */
	   /* TW: This register will be adapted according to LCD
	    *     panel type later in the OEM setup functions.
	    *     (Various panel types require a different delay
	    *     such as Clevo 2202; however, on most panels,
	    *     0x20 does nicely.)
	    */

  	} else {

      	   /* ----------- 310/325 series ---------------*/
	   if(SiS_IF_DEF_LVDS == 0) {
                temp = 0x10;                                        /* TW: Modified (650/301 BIOS) */
                if(SiS_LCDResInfo == Panel1024x768)  temp = 0x2c;   /* TW: Modified (650/301 BIOS) */
    	        if(SiS_LCDResInfo == Panel1280x1024) temp = 0x20;
    	        if(SiS_LCDResInfo == Panel1280x960)  temp = 0x24;
		if(SiS_VBInfo & SetCRT2ToHiVisionTV) {
      		   if(SiS_VBInfo & SetInSlaveMode) temp = 0x2c;
      		   else temp = 0x20;
    	        }
		tempbl = 0xF0;
	   } else {
	        temp = 0x00;
		if(SiS_VBInfo & SetCRT2ToTV) temp = 0x0a;
		tempbl = 0xF0;
		if(!(SiS_VBInfo & SetCRT2ToTV)) tempbl = 0x0F;
	   }

           if(SiS_IF_DEF_LVDS == 0) {
                 temp >>= 2;         				    /* TW: Only in 650/301LV BIOS */
	   }

	   SiS_SetRegANDOR(SiS_Part1Port,0x2D,tempbl,temp);	    /* TW: Panel Link Delay Compensation */
           /* TW: This register will be adapted according to LCD
	    *     panel type later in the OEM setup functions.
	    *     (Various panel types require a different delay)
	    */

    	   tempax = 0;
    	   if (modeflag & DoubleScanMode) tempax |= 0x80;
    	   if (modeflag & HalfDCLK)       tempax |= 0x40;
    	   SiS_SetRegANDOR(SiS_Part1Port,0x2C,0x3f,tempax);

  	}

     }  /* Slavemode */

     if(SiS_IF_DEF_LVDS == 0) {

        /* TW: 630/301B BIOS sets up Panel Link, too! (650/LV does not) */
        if( (SiS_VBType & VB_SIS301BLV302BLV) && (SiS_VBInfo & SetCRT2ToLCD)
	                       && (HwDeviceExtension->jChipType < SIS_315H)) {

	    SiS_SetGroup1_LVDS(BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                       HwDeviceExtension,RefreshRateTableIndex);

        } else if(SiS_VBInfo & SetInSlaveMode) {                              /* Inserted (650/301 BIOS) */

    	    SiS_SetGroup1_301(BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                      HwDeviceExtension,RefreshRateTableIndex);
        }

     } else {

        if(HwDeviceExtension->jChipType < SIS_315H) {
	     SiS_SetGroup1_LVDS(BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                        HwDeviceExtension,RefreshRateTableIndex);
	} else {
	    /* TW: For 650/LVDS */
            if((!(SiS_VBInfo & SetCRT2ToTV)) || (SiS_VBInfo & SetInSlaveMode)) {
    	         SiS_SetGroup1_LVDS(BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                            HwDeviceExtension,RefreshRateTableIndex);
            }
	}

     }
   } /* LCDA */
}

/* TW: Checked against 650/301LV and 630/301B (II) BIOS */
void
SiS_SetGroup1_301(USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                  PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex)
{
  USHORT  push1,push2;
  USHORT  tempax,tempbx,tempcx,temp;
  USHORT  resinfo,modeflag;

  if(ModeNo<=0x13) {
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	resinfo = SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  /* TW: The following is only done if bridge is in slave mode: */

  tempax = 0xFFFF;
  if(!(SiS_VBInfo & SetCRT2ToTV))  tempax = SiS_GetVGAHT2();

  /* TW: 630/301B does not check this flag, assumes it is set */
  /*     650/LV BIOS does not check this either; so we set it... */
  if(SiS_VBType & VB_SIS301BLV302BLV) {
  	modeflag |= Charx8Dot;
  }

  if(modeflag & Charx8Dot) tempcx = 0x08;
  else tempcx = 0x09;

  if(tempax >= SiS_VGAHT) tempax = SiS_VGAHT;

  if(modeflag & HalfDCLK) tempax >>= 1;

  tempax = (tempax / tempcx) - 5;
  tempbx = tempax & 0xFF;

  temp = 0xFF;                                          /* set MAX HT */
  SiS_SetReg1(SiS_Part1Port,0x03,temp);

  tempax = SiS_VGAHDE;                                 	/* 0x04 Horizontal Display End */
  if(modeflag & HalfDCLK) tempax >>= 1;
  tempax = (tempax / tempcx) - 1;
  tempbx |= ((tempax & 0x00FF) << 8);
  temp = tempax & 0xFF;
  SiS_SetReg1(SiS_Part1Port,0x04,temp);

  temp = (tempbx & 0xFF00) >> 8;
  if(SiS_VBInfo & SetCRT2ToTV){
        if(!(SiS_VBType & VB_SIS301BLV302BLV)) {        /* TW: Inserted from 650/301, 630/301 BIOS */
    	    temp += 2;
        }                                               /* TW: Inserted from 650/301, 630/301 BIOS */
#ifdef oldHV
    	if(SiS_VBInfo & SetCRT2ToHiVisionTV) {
            if(resinfo == 7) temp -= 2;
    	}
#endif
  }
  SiS_SetReg1(SiS_Part1Port,0x05,temp);                 /* 0x05 Horizontal Display Start */

  SiS_SetReg1(SiS_Part1Port,0x06,0x03);                 /* 0x06 Horizontal Blank end     */

#ifdef oldHV
  if(SiS_VBInfo & SetCRT2ToHiVisionTV) {
    temp = (tempbx & 0x00FF) - 1;
    if(!(modeflag & HalfDCLK)) {
      temp -= 6;
      if(SiS_SetFlag & TVSimuMode) {
        temp -= 2;					/* Modified according to 650/301 BIOS; was 4 */
        if(ModeNo > 0x13) temp -= 10;
      }
    }
  } else {
#endif
    tempcx = tempbx & 0x00FF;
    tempbx = (tempbx & 0xFF00) >> 8;
    tempcx = (tempcx + tempbx) >> 1;
    temp = (tempcx & 0x00FF) + 2;
    if(SiS_VBInfo & SetCRT2ToTV){
       temp--;
       if(!(modeflag & HalfDCLK)){
          if((modeflag & Charx8Dot)){
             temp += 4;
             if(SiS_VGAHDE >= 800) temp -= 6;
	     /* TW: Inserted from 650/301 BIOS, 630/301B/301 don't do this */
             if(HwDeviceExtension->jChipType >= SIS_315H) {
	         if(SiS_VGAHDE == 800) temp += 2;
             }
          }
       }
    } else {
       if(!(modeflag & HalfDCLK)){
         temp -= 4;
         if(SiS_LCDResInfo != Panel1280x960) {
           if(SiS_VGAHDE >= 800){
             temp -= 7;
             if(SiS_ModeType == ModeEGA){                         /* 650/301LV does not do this */
               if(SiS_VGAVDE == 1024){                            /* 650/301LV does not do this */
                 temp += 15;                                      /* 650/301LV does not do this */
                 if(SiS_LCDResInfo != Panel1280x1024) temp += 7;  /* 650/301LV does not do this */
               }
             }
             if(SiS_VGAHDE >= 1280){
               if(SiS_LCDResInfo != Panel1280x960) {
                 if(SiS_LCDInfo & LCDNonExpanding) temp += 28;
               }
             }
           }
         }
       }
    }
#ifdef oldHV
  }
#endif
  SiS_SetReg1(SiS_Part1Port,0x07,temp);               	/* 0x07 Horizontal Retrace Start */

  SiS_SetReg1(SiS_Part1Port,0x08,0x00);                 /* 0x08 Horizontal Retrace End   */

  if(SiS_VBInfo & SetCRT2ToTV) {
        if(SiS_SetFlag & TVSimuMode) {
            if((ModeNo == 0x06) || (ModeNo == 0x10) || (ModeNo == 0x11) ||
	                           (ModeNo == 0x13) || (ModeNo == 0x0F)){
             	SiS_SetReg1(SiS_Part1Port,0x07,0x5b);
             	SiS_SetReg1(SiS_Part1Port,0x08,0x03);
            }
            if((ModeNo == 0x00) || (ModeNo == 0x01)) {
             	if(SiS_VBInfo & SetNTSCTV) {
             		SiS_SetReg1(SiS_Part1Port,0x07,0x2A);
             		SiS_SetReg1(SiS_Part1Port,0x08,0x61);
              	} else {
             		SiS_SetReg1(SiS_Part1Port,0x07,0x2A);
             		SiS_SetReg1(SiS_Part1Port,0x08,0x41);
             		SiS_SetReg1(SiS_Part1Port,0x0C,0xF0);
            	}
           }
           if((ModeNo == 0x02) || (ModeNo == 0x03) || (ModeNo == 0x07)){
            	if(SiS_VBInfo & SetNTSCTV) {
           		SiS_SetReg1(SiS_Part1Port,0x07,0x54);
           		SiS_SetReg1(SiS_Part1Port,0x08,0x00);
            	} else {
           		SiS_SetReg1(SiS_Part1Port,0x07,0x55);
           		SiS_SetReg1(SiS_Part1Port,0x08,0x00);
           		SiS_SetReg1(SiS_Part1Port,0x0C,0xF0);
           	}
           }
           if((ModeNo == 0x04) || (ModeNo == 0x05) || (ModeNo == 0x0D)
	                                           || (ModeNo == 0x50)){
            	if(SiS_VBInfo & SetNTSCTV) {
            		SiS_SetReg1(SiS_Part1Port,0x07,0x30);
            		SiS_SetReg1(SiS_Part1Port,0x08,0x03);
            	} else {
           		SiS_SetReg1(SiS_Part1Port,0x07,0x2f);
           		SiS_SetReg1(SiS_Part1Port,0x08,0x02);
                }
           }
       }
  }

  SiS_SetReg1(SiS_Part1Port,0x18,0x03);                	/* 0x18 SR08    */

  SiS_SetRegANDOR(SiS_Part1Port,0x19,0xF0,0x00);

  SiS_SetReg1(SiS_Part1Port,0x09,0xFF);                	/* 0x09 Set Max VT    */

  tempbx = SiS_VGAVT;
  push1 = tempbx;
  tempcx = 0x121;
  tempbx = SiS_VGAVDE;                               	/* 0x0E Vertical Display End */
  if(tempbx == 357) tempbx = 350;
  if(tempbx == 360) tempbx = 350;
  if(tempbx == 375) tempbx = 350;
  if(tempbx == 405) tempbx = 400;
  if(tempbx == 420) tempbx = 400;
  if(tempbx == 525) tempbx = 480;
  push2 = tempbx;
  if(SiS_VBInfo & SetCRT2ToLCD) {  /* TW: Entire if statement not in 630/301 BIOS */
    	if(SiS_LCDResInfo == Panel1024x768) {
      		if(!(SiS_SetFlag & LCDVESATiming)) {
        		if(tempbx == 350) tempbx += 5;
        		if(tempbx == 480) tempbx += 5;
      		}
    	}
  }
  tempbx--;
  temp = tempbx & 0x00FF;
  tempbx--;			/* Not in 630/301 BIOS */
  temp = tempbx & 0x00FF;	/* Not in 630/301 BIOS */
  SiS_SetReg1(SiS_Part1Port,0x10,temp);        		/* 0x10 vertical Blank Start */

  tempbx = push2;
  tempbx--;
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x0E,temp);

  if(tempbx & 0x0100) {
  	tempcx |= 0x0002;
	if(SiS_VBType & VB_SIS301) tempcx |=0x000a;
  }

  tempax = 0x000B;
  if(modeflag & DoubleScanMode) tempax |= 0x8000;

  if(tempbx & 0x0200) {
  	tempcx |= 0x0040;
	if(SiS_VBType & VB_SIS301) tempax |= 0x2000;
  }

  if(SiS_VBType & VB_SIS301) {
        if(SiS_VBInfo & SetPALTV) {
	      if(SiS_VGAVDE == 480) {
	             tempax = (tempax & 0x00ff) | 0x2000;
		     if(modeflag & DoubleScanMode)  tempax |= 0x8000;
	      }
	}
  }

  temp = (tempax & 0xFF00) >> 8;
  SiS_SetReg1(SiS_Part1Port,0x0B,temp);

  if(tempbx & 0x0400) tempcx |= 0x0600;

  SiS_SetReg1(SiS_Part1Port,0x11,0x00);                	/* 0x11 Vertical Blank End */

  tempax = push1;
  tempax -= tempbx;
  tempax >>= 2;
  push1 = tempax;

  if((resinfo != 0x09) || (SiS_VBType & VB_SIS301)) {
    	tempax <<= 1;
    	tempbx += tempax;
  }
#ifdef oldHV
  if(SiS_VBInfo & SetCRT2ToHiVisionTV) {
    	tempbx -= 10;
  } else {
#endif
    	if(SiS_SetFlag & TVSimuMode) {
      	   if(SiS_VBInfo & SetPALTV) {
	       if(!(SiS_HiVision & 0x03)) {
                    tempbx += 40;
		    if(HwDeviceExtension->jChipType >= SIS_315H) {
		       if(SiS_VGAHDE == 800) tempbx += 10;
		    }
      	       }
	   }
    	}
#ifdef oldHV
  }
#endif
  tempax = push1;
  tempax >>= 2;
  tempax++;
  tempax += tempbx;
  push1 = tempax;
  if((SiS_VBInfo & SetPALTV)) {
    	if(tempbx <= 513)  {
      		if(tempax >= 513) tempbx = 513;
    	}
  }
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x0C,temp);			/* 0x0C Vertical Retrace Start */

  if(!(SiS_VBType & VB_SIS301)) {
  	tempbx--;
  	temp = tempbx & 0x00FF;
  	SiS_SetReg1(SiS_Part1Port,0x10,temp);

	if(tempbx & 0x0100) tempcx |= 0x0008;

  	if(tempbx & 0x0200)
    		SiS_SetRegOR(SiS_Part1Port,0x0B,0x20);

  	tempbx++;
  }
  if(tempbx & 0x0100) tempcx |= 0x0004;
  if(tempbx & 0x0200) tempcx |= 0x0080;
  if(tempbx & 0x0400) {
        if(SiS_VBType & VB_SIS301) tempcx |= 0x0800;
  	else                       tempcx |= 0x0C00;
  }

  tempbx = push1;
  temp = tempbx & 0x00FF;
  temp &= 0x0F;
  SiS_SetReg1(SiS_Part1Port,0x0D,temp);        		/* 0x0D vertical Retrace End */

  if(tempbx & 0x0010) tempcx |= 0x2000;

  temp = tempcx & 0x00FF;

  if(SiS_VBType & VB_SIS301) {
	if(SiS_VBInfo & SetPALTV) {
	      if(SiS_VGAVDE == 480)  temp = 0xa3;
	}
  }
  SiS_SetReg1(SiS_Part1Port,0x0A,temp);                    		/* 0x0A CR07 */

  temp = (tempcx & 0xFF00) >> 8;
  SiS_SetReg1(SiS_Part1Port,0x17,temp);                    		/* 0x17 SR0A */

  tempax = modeflag;
  temp = (tempax & 0xFF00) >> 8;
  temp = (temp >> 1) & 0x09;
  /* TW: Inserted from 650/301 BIOS; not in 630/301B+301 BIOS */
  if(HwDeviceExtension->jChipType >= SIS_315H) {
       temp |= 0x01;
  }
  SiS_SetReg1(SiS_Part1Port,0x16,temp);                    		/* 0x16 SR01 */

  SiS_SetReg1(SiS_Part1Port,0x0F,0x00);                        		/* 0x0F CR14 */

  SiS_SetReg1(SiS_Part1Port,0x12,0x00);                        		/* 0x12 CR17 */

  if(SiS_LCDInfo & LCDRGB18Bit) temp = 0x80;
  else                          temp = 0x00;
  SiS_SetReg1(SiS_Part1Port,0x1A,temp);                         	/* 0x1A SR0E */

  return;
}

/* TW: Checked against 650/LVDS 1.10.07, 630/301B (I,II) and 630/LVDS BIOS */
void
SiS_SetGroup1_LVDS(USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
		   USHORT ModeIdIndex,
                   PSIS_HW_DEVICE_INFO HwDeviceExtension,
		   USHORT RefreshRateTableIndex)
{
  USHORT modeflag,resinfo;
  USHORT push1,push2,tempax,tempbx,tempcx,temp,pushcx;
  ULONG tempeax=0,tempebx,tempecx,tempvcfact=0;

  if(ModeNo<=0x13) {
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	resinfo = SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

#ifdef LINUX_XF86
#ifdef TWDEBUG
  xf86DrvMsg(0, X_INFO, "(init301: LCDHDES 0x%03x LCDVDES 0x%03x)\n", SiS_LCDHDES, SiS_LCDVDES);
  xf86DrvMsg(0, X_INFO, "(init301: HDE     0x%03x VDE     0x%03x)\n", SiS_HDE, SiS_VDE);
  xf86DrvMsg(0, X_INFO, "(init301: VGAHDE  0x%03x VGAVDE  0x%03x)\n", SiS_VGAHDE, SiS_VGAVDE);
  xf86DrvMsg(0, X_INFO, "(init301: HT      0x%03x VT      0x%03x)\n", SiS_HT, SiS_VT);
  xf86DrvMsg(0, X_INFO, "(init301: VGAHT   0x%03x VGAVT   0x%03x)\n", SiS_VGAHT, SiS_VGAVT);
#endif
#endif

  /* TW: Set up Panel Link */

  /* 1. Horizontal setup */

  tempax = SiS_LCDHDES;
   /* TW: Inserted (650/LVDS,630/301B/LVDS) BIOS) */
  if((SiS_LCDResInfo == Panel640x480) && (!(SiS_VBInfo & SetInSlaveMode)))
  	tempax -= 8;

  tempcx = SiS_HT;    				  /* Horiz. Total */

  tempbx = SiS_HDE;                               /* Horiz. Display End */

  if(SiS_LCDInfo & LCDNonExpanding) {
    if(!SiS_IF_DEF_DSTN) {
 	if(SiS_LCDResInfo == Panel800x600)        tempbx = 800;
    	else if(SiS_LCDResInfo == Panel1024x768)  tempbx = 1024;
/*	else if(SiS_LCDResInfo == Panel1024x600)  tempbx = 1024;  - not done in BIOS */
/*	else if(SiS_LCDResInfo == Panel1152x768)  tempbx = 1152;  - not done in BIOS */
	else if(SiS_LCDResInfo == Panel1280x1024) tempbx = 1280;  /* TW */
        else if(SiS_LCDResInfo != Panel640x480)   tempbx = 1400;  /* TW */
    }
  }
  tempcx = (tempcx - tempbx) >> 2;		 /* HT-HDE / 4 */

  push1 = tempax;

  tempax += tempbx;

  if(tempax >= SiS_HT) tempax -= SiS_HT;

  push2 = tempax;

  /* TW: Inserted this entire "if"-section from 650/LVDS, 630/301B and 630/LVDS BIOS */
  if(SiS_VBInfo & SetCRT2ToLCD) {
       if(!SiS_IF_DEF_DSTN){
     	  if(SiS_LCDResInfo == Panel800x600)        tempcx = 0x0028;
     	  else if(SiS_LCDResInfo == Panel1400x1050) tempcx = 0x0030;
     	  else if( (SiS_LCDResInfo == Panel1024x768) ||
	           (SiS_LCDResInfo == Panel1024x600) ||
		   (SiS_LCDResInfo == Panel1152x768) ) {
	  	if(HwDeviceExtension->jChipType < SIS_315H) {
		     if(SiS_IF_DEF_LVDS == 1) {
		           tempcx = 0x0017;
#ifdef TWPANEL
			   tempcx++;
#endif
		     } else {
		           tempcx = 0x0017;  /* A901; other 301B BIOS 0x0018; */
		     }
		} else {
		     tempcx = 0x0018;
		}
	  }
     	  else if(SiS_LCDResInfo != Panel640x480)   tempcx = 0x0030;
       }
  }

  tempcx += tempax;                              /* lcdhrs  */
  if(tempcx >= SiS_HT) tempcx -= SiS_HT;

  tempax = tempcx >> 3;                          /* BPLHRS */
  temp = tempax & 0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x14,temp);		 /* Part1_14h; TW: Panel Link Horizontal Retrace Start  */

  temp = (tempax & 0x00FF) + 10;

  /* TW: Inserted this entire "if"-section from 650/LVDS BIOS */
  if(SiS_VBInfo & SetCRT2ToLCD) {
      if(!SiS_IF_DEF_DSTN){
        if(SiS_LCDResInfo != Panel640x480) {
	  temp += 6;
          if(SiS_LCDResInfo != Panel800x600) {
	    temp++;
	    if(HwDeviceExtension->jChipType > SIS_315H) {
	       if(SiS_LCDResInfo != Panel1024x768) {
	          temp -= 3;
#ifdef TEST1400
		  temp = 0x0e;
#endif
	       }
	    }
	  }
        }
      }
  }

  temp &= 0x1F;
  temp |= ((tempcx & 0x0007) << 5);
  if(SiS_IF_DEF_FSTN) temp=0x20;
  SiS_SetReg1(SiS_Part1Port,0x15,temp);    	 /* Part1_15h; TW: Panel Link Horizontal Retrace End/Skew */

  tempbx = push2;
  tempcx = push1;                                /* lcdhdes  */

  temp = (tempcx & 0x0007);                      /* BPLHDESKEW  */
  SiS_SetReg1(SiS_Part1Port,0x1A,temp);   	 /* Part1_1Ah; TW: Panel Link Vertical Retrace Start (2:0) */

  tempcx >>= 3;                                  /* BPLHDES */
  temp = (tempcx & 0x00FF);
  if(ModeNo==0x5b) temp--;                       /* fix fstn mode=5b */
  SiS_SetReg1(SiS_Part1Port,0x16,temp);    	 /* Part1_16h; TW: Panel Link Horizontal Display Enable Start  */

  if(HwDeviceExtension->jChipType < SIS_315H) {  /* TW: Not done in LVDS BIOS 1.10.07 */
     if(tempbx & 0x07) tempbx += 8;              /* TW: Done in 630/301B and 630/LVDS BIOSes */
  }
  tempbx >>= 3;                                  /* BPLHDEE  */
  temp = tempbx & 0x00FF;
  if(ModeNo==0x5b) temp--;                    	 /* fix fstn mode=5b */
  SiS_SetReg1(SiS_Part1Port,0x17,temp);   	 /* Part1_17h; TW: Panel Link Horizontal Display Enable End  */

  /* 2. Vertical setup */

  if(HwDeviceExtension->jChipType < SIS_315H) {

      /* TW: This entire section from 630/301B and 630/LVDS/LVDS+CH BIOS */
      tempcx = SiS_VGAVT;
      tempbx = SiS_VGAVDE;
      if(SiS_LCDInfo & LCDNonExpanding) {
         if(SiS_LCDResInfo != Panel640x480) {
	    tempbx = 600;
	    if(SiS_LCDResInfo != Panel800x600) {
	       tempbx = 768;
	       if( (SiS_LCDResInfo != Panel1024x768) && (SiS_LCDResInfo != Panel1152x768) ) {
	 	    tempbx = 600;
	       }
	    }
         }
      }
      tempcx -= tempbx;

  } else {

      tempcx = SiS_VGAVT - SiS_VGAVDE;                  /* VGAVT-VGAVDE  */

  }

  tempbx = SiS_LCDVDES;	   		 	 	/* VGAVDES  */
  push1 = tempbx;

  tempax = SiS_VGAVDE;

  if((SiS_IF_DEF_TRUMPION == 0) && (!(SiS_LCDInfo & 0x0100))
                                && (SiS_LCDResInfo != Panel640x480)) {
    	if(SiS_VBInfo & SetCRT2ToLCD) {
	    if(!SiS_IF_DEF_DSTN){
      		if(SiS_LCDResInfo == Panel800x600)        tempax = 600;
      		else if(SiS_LCDResInfo == Panel1024x768)  tempax = 768;
		else if(SiS_LCDResInfo == Panel1024x600)  tempax = 600;   /* TW */
      		else if(SiS_LCDResInfo == Panel1152x768)  tempax = 768;   /* TW */
		else if(SiS_LCDResInfo == Panel1280x1024) tempax = 1024;  /* TW */
		else if(SiS_LCDResInfo == Panel1400x1050) tempax = 1050;  /* TW */
		else                                      tempax = 600;
            }
    	}
  }

  tempbx = tempbx + tempax;
  if(tempbx >= SiS_VT) tempbx -= SiS_VT;

  push2 = tempbx;                             	 /* push bx  temppush  */

  tempcx >>= 1;

  /* TW: Inserted this entire "if" section (650/LVDS; 630/301B; 630/LVDS) */
  if((SiS_VBInfo & SetCRT2ToLCD) && (SiS_LCDResInfo != Panel640x480)){
     if(!SiS_IF_DEF_DSTN){
     	if(SiS_LCDResInfo == Panel800x600)        tempcx = 0x0001;
     	else if( (SiS_LCDResInfo == Panel1024x768) ||
	         (SiS_LCDResInfo == Panel1152x768) ) {
		if(HwDeviceExtension->jChipType < SIS_315H) {
		      if(SiS_IF_DEF_LVDS == 1) {
			    tempcx = 0x0002;
#ifdef TWPANEL
			    tempcx++;
#endif
		      } else {
		            tempcx = 0x0002;   /* TW: A901; other 301B BIOS sets 0x0003; */
		      }
		} else tempcx = 0x0003;
        }
     	else if(SiS_LCDResInfo == Panel1280x768)  tempcx = 0x0003;
     	else if(SiS_LCDResInfo == Panel1280x1024) tempcx = 0x0001;
     	else if(SiS_LCDResInfo == Panel1400x1050) tempcx = 0x0001;
     	else 				          tempcx = 0x0057;
     }
  }

  tempbx += tempcx;			 	/* BPLVRS  */

  if(HwDeviceExtension->jChipType < SIS_315H) {
#ifdef TWPANEL
      if(SiS_IF_DEF_CH70xx == 0)
#endif
   	  tempbx++;
  }

#ifdef TEST1400  /* Not done on 650/LVDS 1.10.07, done in 650/LVDS 1.10a */
  if(HwDeviceExtension->jChipType >= SIS_315H) {
          tempbx++;
  }
#endif

  if(tempbx >= SiS_VT) tempbx -= SiS_VT;
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x18,temp);       	 /* Part1_18h; TW: Panel Link Vertical Retrace Start  */

  tempcx >>= 3;

  /* TW: Inserted this entire "if" section (650/LVDS, 630/LVDS, 630/301B) */
  if(SiS_VBInfo & SetCRT2ToLCD) {
     if( (HwDeviceExtension->jChipType < SIS_315H) &&
         (SiS_LCDResInfo == Panel640x480) )     tempcx = 0x0001;
     else if(SiS_LCDResInfo == Panel1400x1050)  tempcx = 0x0002;
     else if(SiS_LCDResInfo == Panel800x600)    tempcx = 0x0003;
     else if(SiS_LCDResInfo != Panel640x480)  {
     		if(HwDeviceExtension->jChipType < SIS_315H) {
		        if(SiS_IF_DEF_LVDS == 1) {
				tempcx = 0x0004;
#ifdef TWPANEL
				tempcx++;
#endif
		        } else {
				tempcx = 0x0004;   /* A901; Other BIOS sets 0x0005; */
			}
		} else {
			tempcx = 0x0005;
		}
     }
  }

  tempcx = tempcx + tempbx + 1;                  /* BPLVRE  */
  temp = tempcx & 0x000F;
  SiS_SetRegANDOR(SiS_Part1Port,0x19,0xf0,temp); /* Part1_19h; TW: Panel Link Vertical Retrace End (3:0); Misc.  */

  temp = ((tempbx & 0x0700) >> 8) << 3;          /* BPLDESKEW =0 */
  if(SiS_VGAVDE != SiS_VDE)	  temp |= 0x40;
  if(SiS_SetFlag & EnableLVDSDDA) temp |= 0x40;
  if(SiS_LCDInfo & LCDRGB18Bit)   {
      if(HwDeviceExtension->jChipType >= SIS_315H) {
         if(SiS_GetReg1(SiS_Part1Port,0x00) & 0x01) {	/* TW: Inserted from 650/LVDS 1.10.07 */
            temp |= 0x80;
         }
      } else {
	 if( (HwDeviceExtension->jChipType == SIS_630) ||
	     (HwDeviceExtension->jChipType == SIS_730) ) {
	    if(HwDeviceExtension->jChipRevision >= 0x30) {
	       temp |= 0x80;
	    }
	 }
      }
  }         /* TW: in follwing line, 0x87 was 0x07 (modified according to 650/LVDS BIOS) */
  SiS_SetRegANDOR(SiS_Part1Port,0x1A,0x87,temp);  /* Part1_1Ah; TW: Panel Link Control Signal (7:3); Vertical Retrace Start (2:0) */

  if (HwDeviceExtension->jChipType < SIS_315H) {

        /* 300 series */

        tempeax = SiS_VGAVDE << 6;
        temp = (USHORT)(tempeax % (ULONG)SiS_VDE);
        tempeax = tempeax / (ULONG)SiS_VDE;
        if(temp != 0) tempeax++;
        tempebx = tempeax;                         /* BPLVCFACT  */

  	if(SiS_SetFlag & EnableLVDSDDA) {
	     tempebx = 0x003F;    
	}

  	temp = (USHORT)(tempebx & 0x00FF);
  	SiS_SetReg1(SiS_Part1Port,0x1E,temp);      /* Part1_1Eh; TW: Panel Link Vertical Scaling Factor */

  } else {

        /* 310 series */

	SiS_SetReg1(SiS_Part1Port,0x1E,0x23);      /* Inserted from 650/LVDS BIOS */

	tempeax = SiS_VGAVDE << 18;
    	temp = (USHORT)(tempeax % (ULONG)SiS_VDE);
    	tempeax = tempeax / SiS_VDE;
    	if(temp != 0) tempeax++;
    	tempebx = tempeax;                         /* BPLVCFACT  */
        tempvcfact = tempeax;
    	temp = (USHORT)(tempebx & 0x00FF);
    	SiS_SetReg1(SiS_Part1Port,0x37,temp);      /* Part1_37h; TW: Panel Link Vertical Scaling Factor */
    	temp = (USHORT)((tempebx & 0x00FF00) >> 8);
    	SiS_SetReg1(SiS_Part1Port,0x36,temp);      /* Part1_36h; TW: Panel Link Vertical Scaling Factor */
    	temp = (USHORT)((tempebx & 0x00030000) >> 16);
    	if(SiS_VDE == SiS_VGAVDE) temp |= 0x04;
    	SiS_SetReg1(SiS_Part1Port,0x35,temp);      /* Part1_35h; TW: Panel Link Vertical Scaling Factor */

  }

  tempbx = push2;                                  /* p bx temppush1 BPLVDEE  */
  tempcx = push1;

  push1 = temp;					   /* TW: For 630/301B and 630/LVDS */

  if(!(SiS_VBInfo & SetInSlaveMode)) {
   	if(!SiS_IF_DEF_DSTN){
    		if(SiS_LCDResInfo == Panel800x600) {
      			if(resinfo == 7) tempcx++;
		}
		if(HwDeviceExtension->jChipType < SIS_315H) {   /* TW: Not done in 650/LVDS 1.10.07 */
	    	    if(resinfo == 8) tempcx++;			/* TW: But in 630/301B and 630/LVDS */
		}
	}
  }
  /* TW: Inserted (650/LVDS, 630/LVDS, 630/301B) */
  if(SiS_LCDResInfo == Panel640x480) {
     tempcx = SiS_VGAVDE;
     tempbx = SiS_VGAVDE - 1;
  }

  temp = ((tempbx & 0x0700) >> 8) << 3;
  temp |= ((tempcx & 0x0700) >> 8);
  SiS_SetReg1(SiS_Part1Port,0x1D,temp);     	/* Part1_1Dh; TW: Vertical Display Overflow; Control Signal */

  temp = tempbx & 0x00FF;
  if(SiS_IF_DEF_FSTN) temp++;
  SiS_SetReg1(SiS_Part1Port,0x1C,temp);      	/* Part1_1Ch; TW: Panel Link Vertical Display Enable End  */

  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x1B,temp);      	/* Part1_1Bh; TW: Panel Link Vertical Display Enable Start  */

  /* 3. Additional horizontal setup (scaling, etc) */

  tempecx = SiS_VGAHDE;
  if(HwDeviceExtension->jChipType >= SIS_315H) {
     if(modeflag & HalfDCLK)			/* TW: Added this entire if statement */
        tempecx >>= 1;
  }
  tempebx = SiS_HDE;
  if(tempecx == tempebx) tempeax = 0xFFFF;
  else {
     tempeax = tempecx;
     tempeax <<= 16;
     temp = (USHORT)(tempeax % tempebx);
     tempeax = tempeax / tempebx;
     if(HwDeviceExtension->jChipType >= SIS_315H) {
         if(temp) tempeax++;			/* TW: Not done in 630/301B or 630/LVDS, but for 650/LVDS */
     }
  }
  tempecx = tempeax;

  if (HwDeviceExtension->jChipType >= SIS_315H) {
      tempeax = SiS_VGAHDE;
      if(modeflag & HalfDCLK)			/* TW: Added this entire if statement */
          tempeax >>= 1;
      tempeax <<= 16;
      tempeax = (tempeax / tempecx) - 1;
  } else {
      tempeax = ((SiS_VGAHT << 16) / tempecx) - 1;    
  }
  tempecx <<= 16;
  tempecx |= (tempeax & 0xFFFF);
  temp = (USHORT)(tempecx & 0x00FF);
  SiS_SetReg1(SiS_Part1Port,0x1F,temp);  	 /* Part1_1Fh; TW: Panel Link DDA Operational Number in each horiz. line */

  tempbx = SiS_VDE;                              /* TW: added following if statement */
  if (HwDeviceExtension->jChipType >= SIS_315H) {
      tempeax = (SiS_VGAVDE << 18) / tempvcfact;
      tempbx = (USHORT)(tempeax & 0x0FFFF);
  } else {
      tempax = SiS_VGAVDE << 6;
      tempbx = push1;
      tempbx &= 0x3f;
      if(tempbx == 0) tempbx = 64;
      tempax = tempax / tempbx;
      tempbx = tempax;
  }
  if(SiS_LCDResInfo == Panel1024x768) tempbx--;
  if(SiS_SetFlag & EnableLVDSDDA)     tempbx = 1;

  temp = ((tempbx & 0xFF00) >> 8) << 3;
  temp |= (USHORT)((tempecx & 0x0700) >> 8);
  SiS_SetReg1(SiS_Part1Port,0x20,temp);  	/* Part1_20h; TW: Overflow register */

  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x21,temp);  	/* Part1_21h; TW: Panel Link Vertical Accumulator Register */

  tempecx >>= 16;                               /* BPLHCFACT  */
  if(HwDeviceExtension->jChipType < SIS_315H) { /* TW: Added this entire if statement from 630/301B+LVDS BIOSes */
      if(modeflag & HalfDCLK) tempecx >>= 1;
  }
  temp = (USHORT)((tempecx & 0xFF00) >> 8);
  SiS_SetReg1(SiS_Part1Port,0x22,temp);     	/* Part1_22h; TW: Panel Link Horizontal Scaling Factor High */

  temp = (USHORT)(tempecx & 0x00FF);
  SiS_SetReg1(SiS_Part1Port,0x23,temp);         /* Part1_22h; TW: Panel Link Horizontal Scaling Factor Low */

  /* 630/301B and 630/LVDS do something for 640x480 panels here */

  /* add dstn new register */
  if(SiS_IF_DEF_DSTN){
     	SiS_SetReg1(SiS_Part1Port,0x1E,0x01);
     	SiS_SetReg1(SiS_Part1Port,0x25,0x00);
     	SiS_SetReg1(SiS_Part1Port,0x26,0x00);
     	SiS_SetReg1(SiS_Part1Port,0x27,0x00);
     	SiS_SetReg1(SiS_Part1Port,0x28,0x87);
     	SiS_SetReg1(SiS_Part1Port,0x29,0x5A);
     	SiS_SetReg1(SiS_Part1Port,0x2A,0x4B);
     	SiS_SetRegANDOR(SiS_Part1Port,0x44,~0x007,0x03);
     	tempbx = SiS_HDE + 64;                       /*Blps = lcdhdee(lcdhdes+HDE) + 64*/
     	temp = tempbx & 0x00FF;
     	SiS_SetReg1(SiS_Part1Port,0x38,temp);
     	temp=((tempbx & 0xFF00) >> 8) << 3;
     	SiS_SetRegANDOR(SiS_Part1Port,0x35,~0x078,temp);
     	tempbx += 32;		                     /*Blpe=lBlps+32*/
     	temp = tempbx & 0x00FF;
     	if(SiS_IF_DEF_FSTN)  temp=0;
     	SiS_SetReg1(SiS_Part1Port,0x39,temp);
     	SiS_SetReg1(SiS_Part1Port,0x3A,0x00);        /*Bflml=0*/
     	SiS_SetRegANDOR(SiS_Part1Port,0x3C,~0x007,0x00);
     	tempbx = SiS_VDE / 2;
     	temp = tempbx & 0x00FF;
     	SiS_SetReg1(SiS_Part1Port,0x3B,temp);
     	temp = ((tempbx & 0xFF00) >> 8) << 3;
     	SiS_SetRegANDOR(SiS_Part1Port,0x3C,~0x038,temp);
     	tempeax = SiS_HDE << 2;                       /* BDxFIFOSTOP = (HDE*4)/128 */
     	tempebx = 128;
     	temp = (USHORT)(tempeax % tempebx);
     	tempeax = tempeax / tempebx;
     	if(temp != 0)  tempeax++;
     	temp = (USHORT)(tempeax & 0x003F);
     	SiS_SetRegANDOR(SiS_Part1Port,0x45,~0x0FF,temp);
     	SiS_SetReg1(SiS_Part1Port,0x3F,0x00);         /* BDxWadrst0 */
     	SiS_SetReg1(SiS_Part1Port,0x3E,0x00);
     	SiS_SetReg1(SiS_Part1Port,0x3D,0x10);
     	SiS_SetRegANDOR(SiS_Part1Port,0x3C,~0x040,0x00);
     	tempax = SiS_HDE >> 4;                        /* BDxWadroff = HDE*4/8/8 */
     	pushcx = tempax;
     	temp = tempax & 0x00FF;
     	SiS_SetReg1(SiS_Part1Port,0x43,temp);
     	temp = ((tempax & 0xFF00) >> 8) << 3;
     	SiS_SetRegANDOR(SiS_Part1Port,0x44,~0x0F8,temp);
     	tempax = SiS_VDE;                             /*BDxWadrst1 = BDxWadrst0 + BDxWadroff * VDE */
     	tempeax = (tempax * pushcx);
     	tempebx = 0x00100000 + tempeax;
     	temp = (USHORT)tempebx & 0x000000FF;
     	SiS_SetReg1(SiS_Part1Port,0x42,temp);
     	temp = (USHORT)((tempebx & 0x0000FF00)>>8);
     	SiS_SetReg1(SiS_Part1Port,0x41,temp);
     	temp = (USHORT)((tempebx & 0x00FF0000)>>16);
     	SiS_SetReg1(SiS_Part1Port,0x40,temp);
     	temp = (USHORT)(((tempebx & 0x01000000)>>24) << 7);
     	SiS_SetRegANDOR(SiS_Part1Port,0x3C,~0x080,temp);
     	SiS_SetReg1(SiS_Part1Port,0x2F,0x03);
     	SiS_SetReg1(SiS_Part1Port,0x03,0x50);
     	SiS_SetReg1(SiS_Part1Port,0x04,0x00);
     	SiS_SetReg1(SiS_Part1Port,0x2F,0x01);
     	SiS_SetReg1(SiS_Part1Port,0x13,0x00);
     	SiS_SetReg1(SiS_P3c4,0x05,0x86);        /* Unlock */
     	SiS_SetReg1(SiS_P3c4,0x1e,0x62);
     	if(SiS_IF_DEF_FSTN){
         	SiS_SetReg1(SiS_P3c4,0x2b,0x1b);
         	SiS_SetReg1(SiS_P3c4,0x2c,0xe3);
         	SiS_SetReg1(SiS_P3c4,0x1e,0x62);
         	SiS_SetReg1(SiS_P3c4,0x2e,0x04);
         	SiS_SetReg1(SiS_P3c4,0x2f,0x42);
         	SiS_SetReg1(SiS_P3c4,0x32,0x01);
         	SiS_SetReg1(SiS_Part1Port,0x2b,0x02);
         	SiS_SetReg1(SiS_Part1Port,0x2c,0x00);
         	SiS_SetReg1(SiS_Part1Port,0x2d,0x00);
     	}
     	SiS_SetRegANDOR(SiS_Part1Port,0x19,0x0f,0x30);
     	SiS_SetReg1(SiS_Part1Port,0x1e,0x7d);
     	SiS_SetReg1(SiS_Part1Port,0x2e,0xe0);
  }

  return;

}

#ifdef SIS315H
void
SiS_CRT2AutoThreshold(USHORT BaseAddr)
{
  SiS_SetRegOR(SiS_Part1Port,0x01,0x40);
}
#endif


/* TW: For LVDS / 302b/lv - LCDA (this must only be called on 310/325 series!) */
/* TW: Double-checked against 650/LVDS and 650/301 BIOS */
void
SiS_SetGroup1_LCDA(USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex)
{
  USHORT modeflag,resinfo;
  USHORT push1,push2,tempax,tempbx,tempcx,temp;
  ULONG tempeax=0,tempebx,tempecx,tempvcfact;

  if(SiS_IF_DEF_LVDS == 1)					/* TW: From 650/LVDS BIOS */
      SiS_SetRegANDOR(SiS_Part1Port,0x13,0xfb,0x04);      	/* TW: From 650/LVDS BIOS */

  if(SiS_IF_DEF_LVDS == 1)					/* TW: From 650/LVDS 1.10.07 */
     SiS_SetRegOR(SiS_Part1Port,0x2D,0x00);			/* TW: From 650/LVDS 1.10.07 */
  else
     SiS_SetRegOR(SiS_Part1Port,0x2D,0x20);

  if(ModeNo<=0x13) {
    modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    resinfo = SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
    modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    resinfo = SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  tempax = SiS_LCDHDES;
  tempbx = SiS_HDE;
  tempcx = SiS_HT;
  
  if(SiS_LCDInfo & LCDNonExpanding) {
    	if(SiS_LCDResInfo == Panel1280x1024)     tempbx = 1280;
    	else if(SiS_LCDResInfo == Panel1024x768) tempbx = 1024;
	else tempbx = 1400;                                     /* TW: From 650/LVDS BIOS; OK with 650/301 */
  }
  tempcx = tempcx - tempbx;                                    	/* HT-HDE  */
  push1 = tempax;
  tempax = tempax + tempbx;                                    	/* lcdhdee  */
  tempbx = SiS_HT;
  if(tempax >= tempbx)	tempax = tempax-tempbx;
  push2=tempax;
                                                           	/* push ax   lcdhdee  */
  tempcx >>= 2;                                        	        /* temp  */
  tempcx = tempcx + tempax;                                    	/* lcdhrs  */
  if(tempcx >= tempbx) tempcx = tempcx - tempbx;
                                                           	/* v ah,cl  */
  tempax = tempcx;
  tempax = tempax >> 3;                                        	/* BPLHRS */
  temp = tempax & 0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x14,temp);                         /* Part1_14h  */

  temp = (tempax & 0x00FF) + 10;
  temp = temp & 0x01F;
  temp = temp | (((tempcx & 0x00ff) & 0x07) << 5);
  SiS_SetReg1(SiS_Part1Port,0x15,temp);                         /* Part1_15h  */

  tempbx = push2;                                          	/* lcdhdee  */
  tempcx = push1;                                          	/* lcdhdes  */
  temp = (tempcx & 0x00FF);
  temp = temp & 0x07;                                  		/* BPLHDESKEW  */
  SiS_SetReg1(SiS_Part1Port,0x1A,temp);                         /* Part1_1Ah  */

  tempcx = tempcx >> 3;                                        	/* BPLHDES */
  temp = (tempcx & 0x00FF);
  SiS_SetReg1(SiS_Part1Port,0x16,temp);                         /* Part1_16h  */

  if(tempbx & 0x07) tempbx += 8;
  tempbx >>= 3;                                        		/* BPLHDEE  */
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x17,temp);                        	/* Part1_17h  */

  tempcx = SiS_VGAVT;
  tempbx = SiS_VGAVDE;
  tempcx = tempcx-tempbx;                                    	/* GAVT-VGAVDE  */
  tempbx = SiS_LCDVDES;                                        	/* VGAVDES  */
  push1 = tempbx;                                      		/* push bx temppush1 */
  if(SiS_IF_DEF_TRUMPION == 0){
    if(SiS_LCDResInfo == Panel1024x768)   tempax = 768;
    if(SiS_LCDResInfo == Panel1280x1024)  tempax = 1024;
    if(SiS_LCDResInfo == Panel1400x1050)  tempax = 1050;        /* TW: Inserted from 650/LVDS BIOS */
    else                                  tempax = 960;         /* TW: Inserted from 650/301 BIOS */
#if 0                                                           /* TW: Removed (650/LVDS BIOS) */
    if(SiS_IF_DEF_CH70xx == 1) {
      if(SiS_VBInfo & SetCRT2ToTV) {
        tempax = SiS_VGAVDE;
      }
    }
#endif
  } else tempax = SiS_VGAVDE;  /* Trumpion */
  tempbx = tempbx + tempax;
  tempax = SiS_VT;                                             	/* VT  */
  if(tempbx >= SiS_VT)  tempbx = tempbx - tempax;

  push2 = tempbx;                                      		/* push bx  temppush2  */
  tempcx >>= 1;
  tempbx = tempbx + tempcx;
  tempbx++;                                                	/* BPLVRS  */
  if(tempbx >= tempax)   tempbx = tempbx - tempax;
  temp = tempbx&0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x18,temp);                         /* Part1_18h  */

  tempcx >>= 3;
  tempcx = tempcx + tempbx;
  tempcx++;                                                	/* BPLVRE  */
  temp = tempcx & 0x00FF;
  temp &= 0x0F;
  if(SiS_IF_DEF_LVDS == 1) {
     SiS_SetRegANDOR(SiS_Part1Port,0x19,0xf0,temp);             /* TW: Inserted from 650/LVDS BIOS */
  } else {
     temp |= 0x30;                                              /* TW: Inserted from 650/301 BIOS */
     SiS_SetRegANDOR(SiS_Part1Port,0x19,0xC0,temp);             /* Part1_19h  (Was ~0x0f) */
  }

  temp = (tempbx & 0xFF00) >> 8;
  temp &= 0x07;
  temp <<= 3;  		                               		/* BPLDESKEW =0 */
  tempbx = SiS_VGAVDE;
  if(tempbx != SiS_VDE)              temp |= 0x40;
  if(SiS_SetFlag & EnableLVDSDDA)    temp |= 0x40;
  if(SiS_IF_DEF_LVDS == 1) {
      if(SiS_LCDInfo & LCDRGB18Bit)  temp |= 0x80;              /* TW: 650/301 BIOS does not check this! */
      SiS_SetRegANDOR(SiS_Part1Port,0x1A,0x87,temp);            /* Part1_1Ah */
  } else {
      SiS_SetRegANDOR(SiS_Part1Port,0x1A,0x07,temp);            /* Part1_1Ah */
  }

  tempbx = push2;                                      		/* p bx temppush2 BPLVDEE  */
  tempcx = push1;                                      		/* pop cx temppush1 NPLVDES */
  push1 = (USHORT)(tempeax & 0xFFFF);

  if(!(SiS_VBInfo & SetInSlaveMode)) {
    if(SiS_LCDResInfo == Panel800x600) {
      if(resinfo == 7) tempcx++;
    }
    if(SiS_IF_DEF_LVDS == 0) {	                                /* TW: Inserted from 650/LVDS BIOS */
        if(resinfo == 8) tempcx++;				/* TW: Modified according to 650/301 BIOSes */
    }
    if(SiS_LCDResInfo == Panel640x480) {                        /* TW: Inserted from 650/301+LVDS BIOSes */
        tempbx = SiS_VGAVDE;                                    /* TW: Inserted from 650/301+LVDS BIOS */
	tempcx = tempbx;					/* TW: Inserted from 650/301+LVDS BIOS */
        tempbx--;						/* TW: Inserted from 650/301+LVDS BIOS */
    }
  }

  temp = (tempbx & 0xFF00) >> 8;
  temp &= 0x07;
  temp <<= 3;
  temp = temp | (((tempcx & 0xFF00) >> 8) & 0x07);
  SiS_SetReg1(SiS_Part1Port,0x1D,temp);                          /* Part1_1Dh */

  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x1C,temp);                          /* Part1_1Ch  */

  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x1B,temp);                          /* Part1_1Bh  */ 

  tempecx = SiS_VGAVT;
  tempebx = SiS_VDE;
  tempeax = SiS_VGAVDE;
  tempecx = tempecx-tempeax;                                 	/* VGAVT-VGAVDE  */
  tempeax <<= 18;
  temp = (USHORT)(tempeax % tempebx);
  tempeax = tempeax / tempebx;
  if(temp != 0)  tempeax++;
  tempebx = tempeax;                                        	/* BPLVCFACT  */
  tempvcfact = tempeax;
  temp=(USHORT)(tempebx & 0x00FF);
  SiS_SetReg1(SiS_Part1Port,0x37,temp);

  temp=(USHORT)((tempebx & 0x00FF00) >> 8);
  SiS_SetReg1(SiS_Part1Port,0x36,temp);

  temp = (USHORT)((tempebx & 0x00030000) >> 16);
  if(SiS_VDE==SiS_VGAVDE) temp |= 0x04;
  SiS_SetReg1(SiS_Part1Port,0x35,temp);
  
  tempecx = SiS_VGAHDE;
  tempebx = SiS_HDE;
  tempeax = tempecx;
  tempeax <<= 16;
  tempeax = tempeax / tempebx;
  if(tempebx == tempecx)  tempeax = 0xFFFF;
  tempecx = tempeax;
  tempeax = SiS_VGAHDE;
  tempeax <<= 16;
  tempeax = tempeax / tempecx;
  tempecx <<= 16;
  tempeax--;
  tempecx = tempecx | (tempeax & 0xFFFF);
  temp=(USHORT)(tempecx & 0x00FF);
  SiS_SetReg1(SiS_Part1Port,0x1F,temp);                          /* Part1_1Fh  */

  tempeax = SiS_VGAVDE;
  tempeax <<= 18;
  tempeax = tempeax / tempvcfact;
  tempbx = (USHORT)(tempeax & 0x0FFFF);

  if(SiS_LCDResInfo == Panel1024x768) tempbx--;

  if(SiS_SetFlag & EnableLVDSDDA)  tempbx = 1;

  temp = ((tempbx & 0xFF00) >> 8) << 3;
  temp = temp | (USHORT)(((tempecx & 0x0000FF00) >> 8) & 0x07);
  SiS_SetReg1(SiS_Part1Port,0x20,temp);                         /* Part1_20h */

  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Part1Port,0x21,temp);                         /* Part1_21h */

  tempecx >>= 16;   	                                  	/* BPLHCFACT  */
  if(!(modeflag & HalfDCLK)) tempecx >>= 1;			/* TW: Inserted from BIOS */
  temp=(USHORT)((tempecx & 0x0000FF00) >> 8);
  SiS_SetReg1(SiS_Part1Port,0x22,temp);                         /* Part1_22h */

  temp=(USHORT)(tempecx & 0x000000FF);
  SiS_SetReg1(SiS_Part1Port,0x23,temp);

  /* TW: Only for 650/LVDS and 301LV/302LV */
  if((SiS_IF_DEF_LVDS == 1) || (SiS_VBInfo & (VB_SIS301LV|VB_SIS302LV))){
  	SiS_SetReg1(SiS_Part1Port,0x1e,0x20);
  }

  return;
}

/* TW: Double-checked against 650/LVDS (1.10.07) and 650/301 BIOS */
void SiS_SetCRT2Offset(USHORT SiS_Part1Port,UCHAR *ROMAddr,USHORT ModeNo,
                       USHORT ModeIdIndex ,USHORT RefreshRateTableIndex,
		       PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT offset;
  UCHAR temp;

  if(SiS_VBInfo & SetInSlaveMode) return;

  offset = SiS_GetOffset(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                         HwDeviceExtension);
  temp = (UCHAR)(offset & 0xFF);
  SiS_SetReg1(SiS_Part1Port,0x07,temp);
  temp = (UCHAR)((offset & 0xFF00) >> 8);
  SiS_SetReg1(SiS_Part1Port,0x09,temp);
  temp = (UCHAR)(((offset >> 3) & 0xFF) + 1);
  SiS_SetReg1(SiS_Part1Port,0x03,temp);
}

/* TW: Checked with 650/LVDS and 650/301 BIOS */
USHORT
SiS_GetOffset(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
              USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,colordepth;
  USHORT modeinfo,index,infoflag;
  USHORT mode960low, mode960high;
#if 0
  USHORT ColorDepth[] = { 0x01, 0x02, 0x04 };
#endif

  modeinfo = SiS_EModeIDTable[ModeIdIndex].Ext_ModeInfo;
  infoflag = SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag;
  if (HwDeviceExtension->jChipType < SIS_315H ) {
    	index = (modeinfo >> 4) & 0xFF;
	/* TW: Modes 1280x960 changed number, so this is redundant */
	mode960low = 0x7c;
	mode960high = 0x7e;
  } else {
    	index = (modeinfo >> 8) & 0xFF;       /* TW: In 650 BIOS (LVDS AND 301), 1280x960 modes are 7b-7d! */
	mode960low = 0x7c;                    /* TW: This is a bug in both BIOS versions ! */
	mode960high = 0x7e;		      /* TW: Corrected here in LVDS BIOS 1.10.07, but not in tables! */
  }

#if 0
  /* TW: Not doing this strange stuff makes 1280x960 at least work on CRT1 */
  if((ModeNo >= mode960low) && (ModeNo <= mode960high)) {
    	temp = ModeNo - mode960low;
    	colordepth = ColorDepth[temp];
    	temp = 0x6b;  /* TW: Why the heck? */
  } else {
#endif
        temp = SiS_ScreenOffset[index];
        colordepth = SiS_GetColorDepth(ROMAddr,ModeNo,ModeIdIndex);
#if 0
  }
#endif

  if(infoflag & InterlaceMode) temp <<= 1;

  temp *= colordepth;

  /* TW: Added this entire "if"-section from 650/LVDS BIOS */
  if((ModeNo >= 0x26) && (ModeNo <= 0x28)) {
        colordepth >>= 1;
	temp += colordepth;
  }

  return(temp);
}

/* Checked with 650/LVDS BIOS */
USHORT
SiS_GetColorDepth(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT ColorDepth[6] = { 1, 2, 4, 4, 6, 8};
  SHORT  index;
  USHORT modeflag;

  if(ModeNo <= 0x13)
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  else
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;

  index = (modeflag & ModeInfoFlag) - ModeEGA;
  if(index < 0) index = 0;
  return(ColorDepth[index]);
}

/* TW: Checked against 650/LVDS (1.10.07), 650/301 and 630/301B BIOS */
void
SiS_SetCRT2Sync(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempah=0,tempbl,infoflag,flag;

  flag = 0;
  tempbl = 0xC0;   /* TW: Severe BIOS bug in all BIOSes except 650/LVDS 1.10.07 */

  infoflag = SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag;

  if(SiS_IF_DEF_LVDS == 1) {				     /* LVDS */
    if(SiS_VBInfo & SetCRT2ToLCD) {
      tempah = SiS_LCDInfo;
      if(HwDeviceExtension->jChipType >= SIS_315H) {
              tempbl = tempah & 0xc0;
      }
      if(SiS_LCDInfo & LCDSync) {
        flag = 1;
      }
    }
  } else if ( (HwDeviceExtension->jChipType < SIS_315H) &&   /* 630/301B */
              (SiS_VBType & VB_SIS301BLV302BLV) ) {
     if(SiS_VBInfo & SetCRT2ToLCD) {
        tempah = SiS_LCDInfo;
	if(SiS_LCDInfo & LCDSync) {
           flag = 1;
        }
     }
  } else if (HwDeviceExtension->jChipType < SIS_315H) {      /* 630/301 */
     if(SiS_VBInfo & SetCRT2ToLCD) {
         tempah = SiS_LCDInfo;
	 if(SiS_LCDInfo & LCDNonExpandingShift) {
	    flag = 1;
	 }
     }
  }

  if(flag != 1) tempah = infoflag >> 8;

  tempah &= 0xC0;
  tempah |= 0x20;

  if(!(SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;

  if (SiS_LCDResInfo == Panel640x480) {
	/* TW: BIOS does something here (301, 301LV and LVDS) @@@ */
  }

  if(!(SiS_VBType & VB_SIS301)) {
  	tempah &= 0x3f;
  	tempah |= tempbl;
  }

  SiS_SetRegANDOR(SiS_Part1Port,0x19,0x3F,tempah);
}

/* TW: Set FIFO on 300 series */
/* TW: Checked against 630/301B BIOS; does not set PCI registers */
void
SiS_SetCRT2FIFO_300(UCHAR *ROMAddr,USHORT ModeNo,
                    PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,index;
  USHORT modeidindex,refreshratetableindex;
  USHORT VCLK,MCLK,colorth=0,data2;
  ULONG  data,eax;
  UCHAR  LatencyFactor[] = {
  	97, 88, 86, 79, 77, 00,       /*; 64  bit    BQ=2   */
        00, 87, 85, 78, 76, 54,       /*; 64  bit    BQ=1   */
        97, 88, 86, 79, 77, 00,       /*; 128 bit    BQ=2   */
        00, 79, 77, 70, 68, 48,       /*; 128 bit    BQ=1   */
        80, 72, 69, 63, 61, 00,       /*; 64  bit    BQ=2   */
        00, 70, 68, 61, 59, 37,       /*; 64  bit    BQ=1   */
        86, 77, 75, 68, 66, 00,       /*; 128 bit    BQ=2   */
        00, 68, 66, 59, 57, 37};      /*; 128 bit    BQ=1   */

  SiS_SearchModeID(ROMAddr,&ModeNo,&modeidindex);
  SiS_SetFlag &= (~ProgrammingCRT2);
  SiS_SelectCRT2Rate = 0;
  refreshratetableindex = SiS_GetRatePtrCRT2(ROMAddr,ModeNo,modeidindex);

  if(ModeNo >= 0x13) {
    index = SiS_RefIndex[refreshratetableindex].Ext_CRTVCLK;
    index &= 0x3F;
    VCLK = SiS_VCLKData[index].CLOCK;
    index = SiS_GetReg1(SiS_P3c4,0x1A);
    index &= 0x07;
    MCLK = SiS_MCLKData_0[index].CLOCK;
    data2 = SiS_ModeType - 0x02;
    switch (data2) {
      case 0 : 	colorth = 1; break;
      case 1 : 	colorth = 1; break;
      case 2 : 	colorth = 2; break;
      case 3 : 	colorth = 2; break;
      case 4 : 	colorth = 3; break;
      case 5 : 	colorth = 4; break;
    }
    /* data2=(data2*VCLK)/MCLK;   */  /*  bx */
    data2 = (colorth * VCLK) / MCLK;  /* TW */

    temp = SiS_GetReg1(SiS_P3c4,0x14);
    temp = ((temp&0x00FF)>>6)<<1;
    if(temp == 0) temp=1;
    temp <<= 2;

    data2 = temp - data2;

/*  if(data2%(28*16)) {		 TW: WRONG
      	data2=data2/(28*16);
      	data2++;
    } else {
      	data2=data2/(28*16);
    } */
    if((28*16) % data2) {		/* TW */
      	data2 = (28 * 16) / data2;
      	data2++;
    } else {
      	data2 = (28 * 16) / data2;
    }

    index = 0;
    temp = SiS_GetReg1(SiS_P3c4,0x14);
    if(temp & 0x0080) index += 12;

#ifndef LINUX_XF86
    SiS_SetReg4(0xcf8,0x800000A0);
    eax=SiS_GetReg3(0xcfc);
#else
  /* TW: We use pci functions X offers. We use tag 0, because
   * we want to read/write to the host bridge (which is always
   * 00:00.0 on 630, 730 and 540), not the VGA device.
   */
    eax = pciReadLong(0x00000000, 0xA0);
#endif
    temp=(USHORT)(eax>>24);
    if(!(temp&0x01)) index += 24;

#ifndef LINUX_XF86
    SiS_SetReg4(0xcf8,0x80000050);
    eax=SiS_GetReg3(0xcfc);
#else
    eax = pciReadLong(0x00000000, 0x50);
#endif
    temp=(USHORT)(eax >> 24);
    if(temp & 0x01) index += 6;

    temp = (temp & 0x0F) >> 1;
    index += temp;
    data = LatencyFactor[index];
    data += 15;
    temp = SiS_GetReg1(SiS_P3c4,0x14);
    if(!(temp & 0x80)) data += 5;

    data += data2;

    SiS_SetFlag |= ProgrammingCRT2;

    data = data * VCLK * colorth;
    if(data % (MCLK << 4)) {
      	data = data / (MCLK << 4);
      	data++;
    } else {
      	data = data / (MCLK << 4);
    }

    /* TW: Inserted this entire section */
    temp = SiS_GetReg1(SiS_Part1Port,0x01);
    if( ( (HwDeviceExtension->jChipType == SIS_630) ||
         (HwDeviceExtension->jChipType == SIS_730) ) &&
       (HwDeviceExtension->jChipRevision >= 0x30) ) /* 630s or 730(s?) */
    {
	temp = (temp & (~0x1F)) | 0x1b;
    } else {
	temp = (temp & (~0x1F)) | 0x16;
    }
    SiS_SetRegANDOR(SiS_Part1Port,0x01,0xe0,temp);

    if(data <= 6) data = 6;
    if(data > 0x14) data = 0x14;
    if( (HwDeviceExtension->jChipType == SIS_630) &&
        (HwDeviceExtension->jChipRevision >= 0x30) ) /* 630s, NOT 730 */
    {
   	if(data > 0x13) data = 0x13;
    }
    SiS_SetRegANDOR(SiS_Part1Port,0x02,~0x01F,data);
   /* TW end */
  }
}

/* TW: Set FIFO on 310 series */
#ifdef SIS315H
void
SiS_SetCRT2FIFO_310(UCHAR *ROMAddr,USHORT ModeNo,
                    PSIS_HW_DEVICE_INFO HwDeviceExtension)
{

  UCHAR CombCode[]  = { 1, 1, 1, 4, 3, 1, 3, 4,
                        4, 1, 4, 4, 5, 1, 5, 4};
  UCHAR CRT2ThLow[] = { 39, 63, 55, 79, 78,102, 90,114,
                        55, 87, 84,116,103,135,119,151};
  USHORT temp3,tempax,tempbx,tempcx;
  USHORT tempcl, tempch;
  USHORT index;
  USHORT CRT1ModeNo,CRT2ModeNo;
  USHORT ModeIdIndex;
  USHORT RefreshRateTableIndex;
  USHORT SelectRate_backup;

  SiS_SetReg1(SiS_Part1Port,0x01,0x3B);

  CRT1ModeNo = SiS_CRT1Mode;                             /* get CRT1 ModeNo */
  SiS_SearchModeID(ROMAddr,&CRT1ModeNo,&ModeIdIndex);

  SiS_SetFlag &= (~ProgrammingCRT2);
  SelectRate_backup = SiS_SelectCRT2Rate;
  SiS_SelectCRT2Rate = 0;

  /* Set REFIndex for crt1 refreshrate */
  RefreshRateTableIndex = SiS_GetRatePtrCRT2(ROMAddr,CRT1ModeNo,
                                             ModeIdIndex);

  index = SiS_GetVCLK2Ptr(ROMAddr,CRT1ModeNo,ModeIdIndex,
                          RefreshRateTableIndex,HwDeviceExtension);
  tempax = SiS_VCLKData[index].CLOCK;                         /* Get DCLK (VCLK?) */

  tempbx = SiS_GetColorDepth(ROMAddr,CRT1ModeNo,ModeIdIndex); /* Get colordepth */
  tempbx >>= 1;
  if(!tempbx) tempbx++;

  tempax *= tempbx;

  tempbx = SiS_GetMCLK(ROMAddr, HwDeviceExtension);		     /* Get MCLK */

  tempax /= tempbx;

  tempbx = tempax;

#if 0 /* TW: BIOS code is skrewed */
  if(SiS_GetReg1(SiS_P3c4,0x14) & 0x02) {
   	tempax = 16;
  } else {
    	tempax = 8;
  }
#endif
  tempax = 16;

  tempax -= tempbx;

  tempbx = tempax;    /* tempbx = 16-DRamBus - DCLK*BytePerPixel/MCLK */

  tempax = ((52 * 16) / tempbx);

  if ((52*16 % tempbx) != 0) {
    	tempax++;
  }
  tempcx = tempax;
  tempcx += 40;

  /* get DRAM latency */
  tempcl = (SiS_GetReg1(SiS_P3c4,0x17) >> 3) & 0x7;     /* SR17[5:3] DRAM Queue depth */
  tempch = (SiS_GetReg1(SiS_P3c4,0x17) >> 6) & 0x3;     /* SR17[7:6] DRAM Grant length */

  for (temp3 = 0; temp3 < 16; temp3 += 2) {
    if ((CombCode[temp3] == tempcl) && (CombCode[temp3+1] == tempch)) {
      temp3 = CRT2ThLow[temp3 >> 1];
    }
  }

  tempcx +=  temp3;                                      /* CRT1 Request Period */

  CRT2ModeNo = ModeNo;                                   /* get CRT2 ModeNo */
  SiS_SearchModeID(ROMAddr,&CRT2ModeNo,&ModeIdIndex);    /* Get ModeID Table */

  SiS_SetFlag |= ProgrammingCRT2;
  SiS_SelectCRT2Rate = SelectRate_backup;

  RefreshRateTableIndex=SiS_GetRatePtrCRT2(ROMAddr,CRT1ModeNo,
                                           ModeIdIndex);

  index = SiS_GetVCLK2Ptr(ROMAddr,CRT2ModeNo,ModeIdIndex,
                          RefreshRateTableIndex,HwDeviceExtension);
  tempax = SiS_VCLKData[index].CLOCK;                          /* Get VCLK  */

  tempbx = SiS_GetColorDepth(ROMAddr,CRT2ModeNo,ModeIdIndex);  /* Get colordepth */
  tempbx >>= 1;
  if(!tempbx) tempbx++;

  tempax *= tempbx;

  tempax *= tempcx;

  tempbx = SiS_GetMCLK(ROMAddr, HwDeviceExtension);		       /* Get MCLK */
  tempbx <<= 4;

  tempcx = tempax;
  tempax /= tempbx;
  if(tempcx % tempbx) tempax++;		/* CRT1 Request period * TCLK * BytePerPixel / (MCLK*16) */

  if (tempax > 0x37)  tempax = 0x37;

  /* TW: 650/LVDS (1.10.07, 1.10.00), 650/301LV overrule calculated value; 315 does not */
  if(HwDeviceExtension->jChipType == SIS_650) {
  	tempax = 0x04;
  }

  SiS_SetRegANDOR(SiS_Part1Port,0x02,~0x3F,tempax);
}

USHORT
SiS_GetMCLK(UCHAR *ROMAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT index;

  index = SiS_Get310DRAMType(ROMAddr,HwDeviceExtension);
  if(index >= 4) {
    index -= 4;
    return(SiS_MCLKData_1[index].CLOCK);
  } else {
    return(SiS_MCLKData_0[index].CLOCK);
  }
}
#endif

/* TW: Checked against 650/LVDS 1.10.07 BIOS */
void
SiS_GetLVDSDesData(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex,
		   PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT modeflag;
  USHORT PanelIndex,ResIndex;
  SiS_LVDSDesStruct  *PanelDesPtr=NULL;

  if((SiS_VBType & VB_SIS301BLV302BLV) && (SiS_VBInfo & SetCRT2ToLCDA) ) {

     SiS_GetLVDSDesPtrA(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                        &PanelIndex,&ResIndex);
     switch (PanelIndex)
     {
     	case  0: PanelDesPtr = LVDS1024x768Des_1;   break;  /* --- expanding --- */
     	case  1: PanelDesPtr = LVDS1280x1024Des_1;  break;
     	case  2: PanelDesPtr = LVDS1280x960Des_1;   break;
     	case  3: PanelDesPtr = LVDS1024x768Des_2;   break;  /* --- non expanding --- */
     	case  4: PanelDesPtr = LVDS1280x1024Des_2;  break;
     	case  5: PanelDesPtr = LVDS1280x960Des_2;   break;
     }

  } else {

     SiS_GetLVDSDesPtr(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                       &PanelIndex,&ResIndex,HwDeviceExtension);

     switch (PanelIndex)
     {
     	case  0: PanelDesPtr = SiS_PanelType00_1;   break; /* --- expanding --- | Gericom 1st supersonic (310) */
     	case  1: PanelDesPtr = SiS_PanelType01_1;   break;
     	case  2: PanelDesPtr = SiS_PanelType02_1;   break;
     	case  3: PanelDesPtr = SiS_PanelType03_1;   break;
     	case  4: PanelDesPtr = SiS_PanelType04_1;   break;
     	case  5: PanelDesPtr = SiS_PanelType05_1;   break;
     	case  6: PanelDesPtr = SiS_PanelType06_1;   break;
     	case  7: PanelDesPtr = SiS_PanelType07_1;   break;
     	case  8: PanelDesPtr = SiS_PanelType08_1;   break;
     	case  9: PanelDesPtr = SiS_PanelType09_1;   break;
     	case 10: PanelDesPtr = SiS_PanelType0a_1;   break;
     	case 11: PanelDesPtr = SiS_PanelType0b_1;   break;
     	case 12: PanelDesPtr = SiS_PanelType0c_1;   break;	/* TW: Clevo 2202 (300)  */
     	case 13: PanelDesPtr = SiS_PanelType0d_1;   break;
     	case 14: PanelDesPtr = SiS_PanelType0e_1;   break;	/* TW: Uniwill N271S2 (300) */
     	case 15: PanelDesPtr = SiS_PanelType0f_1;   break;
     	case 16: PanelDesPtr = SiS_PanelType00_2;   break;  /* --- non-expanding --- */
     	case 17: PanelDesPtr = SiS_PanelType01_2;   break;
     	case 18: PanelDesPtr = SiS_PanelType02_2;   break;
     	case 19: PanelDesPtr = SiS_PanelType03_2;   break;
     	case 20: PanelDesPtr = SiS_PanelType04_2;   break;
     	case 21: PanelDesPtr = SiS_PanelType05_2;   break;
     	case 22: PanelDesPtr = SiS_PanelType06_2;   break;
     	case 23: PanelDesPtr = SiS_PanelType07_2;   break;
     	case 24: PanelDesPtr = SiS_PanelType08_2;   break;
     	case 25: PanelDesPtr = SiS_PanelType09_2;   break;
     	case 26: PanelDesPtr = SiS_PanelType0a_2;   break;
     	case 27: PanelDesPtr = SiS_PanelType0b_2;   break;
     	case 28: PanelDesPtr = SiS_PanelType0c_2;   break;     /* TW: Gericom 2200C (300) */
     	case 29: PanelDesPtr = SiS_PanelType0d_2;   break;
     	case 30: PanelDesPtr = SiS_PanelType0e_2;   break;
     	case 31: PanelDesPtr = SiS_PanelType0f_2;   break;
     	case 32: PanelDesPtr = SiS_CHTVUNTSCDesData;   break;
     	case 33: PanelDesPtr = SiS_CHTVONTSCDesData;   break;
     	case 34: PanelDesPtr = SiS_CHTVUPALDesData;    break;
     	case 35: PanelDesPtr = SiS_CHTVOPALDesData;    break;
     }
  }
  SiS_LCDHDES = (PanelDesPtr+ResIndex)->LCDHDES;
  SiS_LCDVDES = (PanelDesPtr+ResIndex)->LCDVDES;

  if(SiS_LCDInfo & LCDNonExpanding){
    if(!(SiS_SetFlag & CRT2IsVGA)) {
      if((HwDeviceExtension->jChipType < SIS_315H) || (SiS_LCDResInfo != Panel1280x1024)) {  /* TW: New from 650/LVDS 1.10.07 */
        if(SiS_LCDResInfo >= Panel1024x768){
          if(ModeNo <= 0x13){
	    modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	    if(HwDeviceExtension->jChipType < SIS_315H) {
	         if(!(modeflag & HalfDCLK)) {
                     SiS_LCDHDES = 320;
		 }
	    } else {
	         /* TW: New from 650/LVDS 1.10.07 */
	         if(SiS_LCDResInfo == Panel1024x768)
	             SiS_LCDHDES = 480;
                 if(SiS_LCDResInfo == Panel1400x1050)
	             SiS_LCDHDES = 804;
                 if(!(modeflag & HalfDCLK)) {
                     SiS_LCDHDES = 320;
	             if(SiS_LCDResInfo == Panel1400x1050)
	                SiS_LCDHDES = 632;
                 }
            }
          }
        }
      }
    }
  }
  return;
}

/* TW: Checked against 630/LVDS (2.04.5c) and 650/LVDS (1.10.07) BIOS */
void
SiS_GetLVDSDesPtr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                  USHORT RefreshRateTableIndex,USHORT *PanelIndex,
		  USHORT *ResIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempbx,tempal,modeflag;

  if(ModeNo<=0x13) {
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	tempal = SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	tempal = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  tempbx = 0;
  if(SiS_IF_DEF_CH70xx != 0) {
    if(!(SiS_VBInfo & SetCRT2ToLCD)) {
      tempbx = 32;
      if(SiS_VBInfo & SetPALTV) tempbx += 2;
      if(SiS_VBInfo & SetCHTVOverScan) tempbx += 1;
    }
  }
  if(SiS_VBInfo & SetCRT2ToLCD) {
    tempbx = SiS_LCDTypeInfo;
    if(SiS_LCDInfo & LCDNonExpanding) tempbx += 16;
    /* TW: Inserted from 650/LVDS (1.10.07) BIOS */
    if(SiS_LCDInfo & 0x0100) {
       if(modeflag & HalfDCLK) tempbx += 16;
    }
  }
  /* TW: Inserted from 630/LVDS and 650/LVDS (1.10.07) BIOS */
  if(SiS_SetFlag & CRT2IsVGA) {
    if(SiS_LCDResInfo != Panel640x480)  {
       tempal = 0x07;
       if(HwDeviceExtension->jChipType < SIS_315H) {
          if(SiS_GetReg1(SiS_P3c4,0x13) & 0x80) tempal++;
       }
    }
  }

  *PanelIndex = tempbx;
  *ResIndex = tempal & 0x1F;
}

void
SiS_GetLVDSDesPtrA(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex,USHORT *PanelIndex,
		   USHORT *ResIndex)
{
  USHORT tempbx=0,tempal;

  tempbx = SiS_LCDResInfo - PanelMin301;  /* TW: *not* PanelMinLVDS! */
  if(SiS_LCDInfo & LCDNonExpanding)  tempbx += 3;

  if(ModeNo<=0x13)
    	tempal = SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
    	tempal = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  *PanelIndex = tempbx;
  *ResIndex = tempal & 0x1F;
}

/* TW: Checked against 650/LVDS (1.10.07), 650/301LV, 630/301 and 630/301B (II) BIOS */
void
SiS_SetCRT2ModeRegs(USHORT BaseAddr,USHORT ModeNo, USHORT ModeIdIndex,
                    PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT i,j,modeflag;
  USHORT tempcl,tempah,tempbl,temp;

  if(ModeNo<=0x13) {
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }
  
  /* TW: BIOS does not do this (neither 301 nor LVDS) */
  /*     (But it's harmless; see SetCRT2Offset) */
  SiS_SetReg1(SiS_Part1Port,0x03,0x00);   /* fix write part1 index 0  BTDRAM bit Bug */

  /* TW: Removed 301B302B301LV302LV check here to match 650/LVDS BIOS */
  if(SiS_VBInfo & SetCRT2ToLCDA) {

	/* TW:   1. for LVDS/302B/302LV **LCDA** */

      SiS_SetRegANDOR(SiS_Part1Port,0x00,0xAF,0x40); /* FUNCTION CONTROL */
      SiS_SetRegAND(SiS_Part1Port,0x2E,0xF7);
#if 0  /* TW: Not done in 650/301, 650/LVDS or 650/301LV BIOS*/
      SiS_SetRegANDOR(SiS_Part1Port,0x13,0xFB,0x04);
      SiS_SetRegANDOR(SiS_Part1Port,0x2c,0xCF,0x30);
      SiS_SetRegANDOR(SiS_Part4Port,0x21,0x3F,0xC0);
      SiS_SetRegANDOR(SiS_Part4Port,0x23,0x7F,0x00);
#endif

  } else {

    for(i=0,j=4;i<3;i++,j++) SiS_SetReg1(SiS_Part1Port,j,0);

    tempcl = SiS_ModeType;

    if(HwDeviceExtension->jChipType < SIS_315H) {

      /* ---- 300 series ---- */

      /* TW: Inserted entire if-section from 630/301B BIOS */
      if(SiS_VBType & VB_SIS301BLV302BLV) {
	  temp = SiS_GetReg1(SiS_P3c4,0x32);
	  temp &= 0xef;
	  temp |= 0x02;
	  if(SiS_VBInfo & SetCRT2ToTV) {
	     temp |= 0x10;
	     temp &= 0xfd;
	  }
	  SiS_SetReg1(SiS_P3c4,0x32,temp);
      }

      if(ModeNo > 0x13){
        tempcl -= ModeVGA;
        if((tempcl > 0) || (tempcl == 0)) {  /* TW: tempcl is USHORT -> always true! */
           tempah = ((0x010 >> tempcl) | 0x080);
        }
      } else  tempah = 0x080;

      if(SiS_VBInfo & SetInSlaveMode)  tempah = (tempah ^ 0x0A0);

    } else {

    /* ---- 310 series ---- */

      /* TW: Inserted from 650/301/301LV BIOS */
      if(SiS_VBType & VB_SIS301BLV302BLV) {
        if(SiS_VBInfo & CRT2DisplayFlag) {
	   SiS_SetRegOR(SiS_Part1Port,0x2e,0x08);
        }
      }

      if(ModeNo > 0x13) {
        tempcl -= ModeVGA;
        if((tempcl > 0) || (tempcl == 0)) {  /* TW: tempcl is USHORT -> always true! */
           tempah = (0x008 >> tempcl);
           if (tempah == 0) tempah = 1;
           tempah |= 0x040;
        }
      } else  tempah = 0x040;

      if(SiS_VBInfo & SetInSlaveMode)  tempah = (tempah ^ 0x050);

    }

    if(SiS_VBInfo & CRT2DisplayFlag)  tempah = 0;

    SiS_SetReg1(SiS_Part1Port,0x00,tempah);  	/* FUNCTION CONTROL */

    if(SiS_IF_DEF_LVDS == 0) {

	/* TW:   2. for 301 (301B, 302B 301LV, 302LV non-LCDA) */

    	tempah = 0x01;
    	if(!(SiS_VBInfo & SetInSlaveMode)) {
      		tempah |= 0x02;
    	}
    	if(!(SiS_VBInfo & SetCRT2ToRAMDAC)) {
      		tempah = (tempah ^ 0x05);
      		if(!(SiS_VBInfo & SetCRT2ToLCD)) {
        		tempah = (tempah ^ 0x01);
      		}
    	}

    	tempcl = tempah;

    	if(HwDeviceExtension->jChipType < SIS_315H) {

		/* --- 300 series --- */
      		tempah = (tempah << 5) & 0xFF;
      		if(SiS_VBInfo & CRT2DisplayFlag)  tempah=0;
      		SiS_SetReg1(SiS_Part1Port,0x01,tempah);

      		tempah = tempcl;

    	} else {

		/* --- 310 series --- */
      		if(SiS_VBInfo & CRT2DisplayFlag)  tempah = 0;
      		tempah = (SiS_GetReg1(SiS_Part1Port,0x2E) & 0xF8) | tempah;
      		SiS_SetReg1(SiS_Part1Port,0x2E,tempah);

      		tempah = tempcl;
    	}

    	if((SiS_ModeType == ModeVGA) && (!(SiS_VBInfo & SetInSlaveMode))) {
      		tempah |= 0x010;
	}

	/* TW: Inserted from 630/301 BIOS */
	if(SiS_VBType & VB_SIS301) {
		if(SiS_LCDResInfo == Panel1280x1024) {
			tempah |= 0x80;
		}
	} else {
		tempah |= 0x80;
	}

    	if(SiS_VBInfo & (SetCRT2ToTV - SetCRT2ToHiVisionTV)){   /* TW: Added -HiVision like in BIOS (650+630) */
      		if(SiS_VBInfo & SetInSlaveMode) {
          		tempah |= 0x20;
      		}
    	}
    	SiS_SetRegANDOR(SiS_Part4Port,0x0D,0x40,tempah);

    	tempah=0;
    	if(SiS_VBInfo & SetCRT2ToTV) {
      		if(SiS_VBInfo & SetInSlaveMode) {
       			if(SiS_VBType & VB_SIS301BLV302BLV) {
            			SiS_SetFlag |= RPLLDIV2XO;
            			tempah |= 0x40;
       			} else {
        			if(!(SiS_SetFlag & TVSimuMode)) {
          				if(!(SiS_VBInfo & SetCRT2ToHiVisionTV)) {
            					SiS_SetFlag |= RPLLDIV2XO;
            					tempah |= 0x40;
          				}
        			}
      			}
     		} else {
        		SiS_SetFlag |= RPLLDIV2XO;
        		tempah |= 0x40;
      		}
    	}

	if(SiS_LCDResInfo == Panel1280x1024 || SiS_LCDResInfo == Panel1280x960) {
		tempah |= 0x80;
	}

    	SiS_SetReg1(SiS_Part4Port,0x0C,tempah);

    } else {

    	/* TW: 3. for LVDS */

	/* TW: Inserted if-statement - Part1Port 0x2e not assigned on 300 series */
	if(HwDeviceExtension->jChipType >= SIS_315H) {

	   /* TW: Inserted this entire section (BIOS 650/LVDS); added ModeType check
	    *     (LVDS can only be slave in 8bpp modes)
	    */
	   tempah = 0x80;
	   if( (modeflag & CRT2Mode) && (SiS_ModeType > ModeVGA) ) {
	       if (SiS_VBInfo & DriverMode) {
	           tempah |= 0x02;
	       }
	   }

	   if(!(SiS_VBInfo & SetInSlaveMode)) {
               tempah |= 0x02;
    	   }

	   if(SiS_VBInfo & SetCRT2ToTV) {
	       tempah = tempah ^ 0x01;
	   }

	   if(SiS_VBInfo & DisableCRT2Display) {
	       tempah = 1;
	   }

    	   SiS_SetRegANDOR(SiS_Part1Port,0x2e,0xF0,tempah);

	} else {

	   /* TW: Inserted entire section from 630/LVDS BIOS (added ModeType check) */
	   tempah = 0;
	   if( (!(SiS_VBInfo & SetInSlaveMode)) && (SiS_ModeType > ModeVGA) ) {
               	  tempah |= 0x02;
    	   }
	   tempah <<= 5;
	   if(SiS_VBInfo & DisableCRT2Display)
	       tempah = 0;

	   SiS_SetReg1(SiS_Part1Port,0x01,tempah);

	}

    }

  }

  /* TW: Inserted the entire following section */

  if(SiS_IF_DEF_LVDS == 0) {

      if(HwDeviceExtension->jChipType >= SIS_315H) {             /* TW: From 650/301 BIOS */

#if 0    /* TW: This is not done in 650/301LV BIOS */
         tempah = 0x04;
         tempbl = 0xfb;
         if(!(SiS_VBInfo & SetCRT2ToLCDA)) {
            tempah = 0x00;
	    if(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))
	       tempbl = 0xff;
         }
         SiS_SetRegANDOR(SiS_Part1Port,0x13,tempbl,tempah);

         SiS_SetRegANDOR(SiS_Part1Port,0x2c,0xCF,0x30);
#endif
         /* This is done instead: */
         tempah = 0x30;
	 if(SiS_VBInfo & DisableCRT2Display) tempah = 0;
	 SiS_SetRegANDOR(SiS_Part1Port,0x2c,0xcf,tempah);

#if 0    /* TW: This is not done in 650/301LV BIOS */
	 SiS_SetRegANDOR(SiS_Part4Port,0x21,0x3f,0xc0);
#endif
	 /* This is done instead: */
	 tempah = 0xc0;
	 if(SiS_VBInfo & DisableCRT2Display) tempah = 0;
	 SiS_SetRegANDOR(SiS_Part4Port,0x21,0x3f,tempah);

#if 0    /* TW: This is not done in 650/301LV BIOS */
	 tempah = 0x00;
         tempbl = 0x7f;
         if(!(SiS_VBInfo & SetCRT2ToLCDA)) {
            tempbl = 0xff;
	    if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr)))
	       tempah = 0x80;
         }
         SiS_SetRegANDOR(SiS_Part4Port,0x23,tempbl,tempah);
#endif
	 /* This is done instead: */
	 tempah = 0x80;
	 if(SiS_VBInfo & DisableCRT2Display) tempah = 0;
	 SiS_SetRegANDOR(SiS_Part4Port,0x23,0x7F,tempah);

      } else if(SiS_VBType & VB_SIS301BLV302BLV) {		/* TW: From 630/301B BIOS */

         SiS_SetRegAND(SiS_Part4Port,0x21,0x3f);

         if(SiS_VBInfo & (SetCRT2ToLCD | DisableCRT2Display))
	    SiS_SetRegAND(SiS_Part4Port,0x23,0x7F);
	 else
	    SiS_SetRegOR(SiS_Part4Port,0x23,0x80);

      }

  } else {  							/* TW: From 650/LVDS BIOS */

      if(HwDeviceExtension->jChipType >= SIS_315H) {
         tempah = 0x04;
	 tempbl = 0xfb;
         if(!(SiS_VBInfo & SetCRT2ToLCDA)) {
            tempah = 0x00;
	    if(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))
	       tempbl = 0xff;
         }
	 SiS_SetRegANDOR(SiS_Part1Port,0x13,tempbl,tempah);

	 if(SiS_VBInfo & DisableCRT2Display)
	    SiS_SetRegANDOR(SiS_Part1Port,0x13,0xfb,0x00);

	 SiS_SetRegANDOR(SiS_Part1Port,0x2c,0xcf,0x30);
      }

  }

}

void
SiS_GetCRT2Data(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                USHORT RefreshRateTableIndex,
		PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  if(SiS_IF_DEF_LVDS == 0) {
     if(SiS_VBType & VB_SIS301BLV302BLV) {
        if(SiS_VBInfo & SetCRT2ToLCDA) {
          SiS_GetCRT2DataLVDS(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,HwDeviceExtension);
        } else {
	  if((HwDeviceExtension->jChipType < SIS_315H) && (SiS_VBInfo & SetCRT2ToLCD)){
              SiS_GetCRT2Data301(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,HwDeviceExtension);
	      /* TW: Need LVDS Data for LCD on 630/301B! */
	      SiS_GetCRT2DataLVDS(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,HwDeviceExtension);
	  } else {
	      SiS_GetCRT2Data301(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,HwDeviceExtension);
          }
        }
     } else
     	SiS_GetCRT2Data301(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,HwDeviceExtension);
  } else {
     SiS_GetCRT2DataLVDS(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,HwDeviceExtension);
  }
}

/* Checked with 650/LVDS 1.10.07 BIOS */
void
SiS_GetCRT2DataLVDS(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                    USHORT RefreshRateTableIndex,
		    PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
   USHORT CRT2Index, ResIndex;
   SiS_LVDSDataStruct *LVDSData = NULL;

   SiS_GetCRT2ResInfo(ROMAddr,ModeNo,ModeIdIndex,HwDeviceExtension);

   if((SiS_VBType & VB_SIS301BLV302BLV) && (SiS_VBInfo & SetCRT2ToLCDA)) {

      SiS_GetCRT2PtrA(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                      &CRT2Index,&ResIndex);

      switch (CRT2Index) {
      	case  0:  LVDSData = SiS_LVDS1024x768Data_1;    break;
      	case  1:  LVDSData = SiS_LVDS1280x1024Data_1;   break;
        case  2:  LVDSData = SiS_LVDS1280x960Data_1;    break;
      	case  3:  LVDSData = SiS_LVDS1024x768Data_2;    break;
      	case  4:  LVDSData = SiS_LVDS1280x1024Data_2;   break;
      	case  5:  LVDSData = SiS_LVDS1280x960Data_2;    break;
      }

   } else {

      /* TW: SiS630/301B needs LVDS Data! */
      if( (HwDeviceExtension->jChipType < SIS_315H) &&
          (SiS_VBType & VB_SIS301BLV302BLV) &&
	  (SiS_VBInfo & SetCRT2ToLCD) )
              SiS_IF_DEF_LVDS = 1;

      SiS_GetCRT2Ptr(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                     &CRT2Index,&ResIndex,HwDeviceExtension);

      /* TW: SiS630/301B needs LVDS Data! */
      if( (HwDeviceExtension->jChipType < SIS_315H) &&
          (SiS_VBType & VB_SIS301BLV302BLV) &&
	  (SiS_VBInfo & SetCRT2ToLCD) )
              SiS_IF_DEF_LVDS = 0;

      switch (CRT2Index) {
      	case  0:  LVDSData = SiS_LVDS800x600Data_1;    break;
      	case  1:  LVDSData = SiS_LVDS1024x768Data_1;   break;
      	case  2:  LVDSData = SiS_LVDS1280x1024Data_1;  break;
      	case  3:  LVDSData = SiS_LVDS800x600Data_2;    break;
      	case  4:  LVDSData = SiS_LVDS1024x768Data_2;   break;
      	case  5:  LVDSData = SiS_LVDS1280x1024Data_2;  break;
	case  6:  LVDSData = SiS_LVDS640x480Data_1;    break;
        case  7:  LVDSData = SiS_LVDSXXXxXXXData_1;    break;  /* TW: New */
	case  8:  LVDSData = SiS_LVDS1400x1050Data_1;  break;  /* TW: New */
	case  9:  LVDSData = SiS_LVDS1400x1050Data_2;  break;  /* TW: New */
      	case 10:  LVDSData = SiS_CHTVUNTSCData;        break;
      	case 11:  LVDSData = SiS_CHTVONTSCData;        break;
      	case 12:  LVDSData = SiS_CHTVUPALData;         break;
      	case 13:  LVDSData = SiS_CHTVOPALData;         break;
      	case 14:  LVDSData = SiS_LVDS320x480Data_1;    break;
	case 15:  LVDSData = SiS_LVDS1024x600Data_1;   break;  /* TW: New */
	case 16:  LVDSData = SiS_LVDS1152x768Data_1;   break;  /* TW: New */
	case 17:  LVDSData = SiS_LVDS1024x600Data_2;   break;  /* TW: New */
	case 18:  LVDSData = SiS_LVDS1152x768Data_2;   break;  /* TW: New */
     }
   }

   SiS_VGAHT = (LVDSData+ResIndex)->VGAHT;
   SiS_VGAVT = (LVDSData+ResIndex)->VGAVT;
   SiS_HT = (LVDSData+ResIndex)->LCDHT;
   SiS_VT = (LVDSData+ResIndex)->LCDVT;

  if( (SiS_IF_DEF_LVDS == 0) && (SiS_VBType & VB_SIS301BLV302BLV)) {

    if(!(SiS_LCDInfo & LCDNonExpanding)){
         if(SiS_LCDResInfo == Panel1024x768){
           SiS_HDE = 1024;
           SiS_VDE = 768;
         } else if(SiS_LCDResInfo == Panel1280x1024){
           SiS_HDE = 1280;
           SiS_VDE = 1024;
         } else {
	   SiS_HDE = 1280;
	   SiS_VDE = 960;
	 }
    }

  } else {

    if(SiS_IF_DEF_TRUMPION == 0) {
      if((SiS_VBInfo & SetCRT2ToLCD) && (!(SiS_LCDInfo & 0x0100))) {
        if(SiS_LCDResInfo != Panel640x480) {
          if((!(SiS_LCDInfo & LCDNonExpanding)) || (SiS_SetFlag & CRT2IsVGA)) {
            if(SiS_LCDResInfo == Panel800x600) {
              SiS_HDE = 800;
              SiS_VDE = 600;
            } else if(SiS_LCDResInfo == Panel1024x768) {
              SiS_HDE = 1024;
              SiS_VDE = 768;
            } else if(SiS_LCDResInfo == Panel1280x1024) {
              SiS_HDE = 1280;
              SiS_VDE = 1024;
            } else if(SiS_LCDResInfo == Panel1024x600){
	      SiS_HDE = 1024;
              SiS_VDE = 600;
	    } else if(SiS_LCDResInfo == Panel1400x1050){
	      SiS_HDE = 1400;
              SiS_VDE = 1050;
	    } else {
	      SiS_HDE = 1152;
	      SiS_VDE = 768;
	    }
            if(SiS_IF_DEF_FSTN) {
              SiS_HDE = 320;
              SiS_VDE = 480;
            }
          }
        }
      }
    }
  }
}

/* TW: Checked against 630/301B BIOS; does not check VDE values for LCD */
void
SiS_GetCRT2Data301(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                        USHORT RefreshRateTableIndex,
			PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempax,tempbx,modeflag;
  USHORT resinfo;
  USHORT CRT2Index,ResIndex;
  SiS_LCDDataStruct *LCDPtr=NULL;
  SiS_TVDataStruct  *TVPtr=NULL;

  if(ModeNo<=0x13) {
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	resinfo = SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }
  SiS_NewFlickerMode = 0;
  SiS_RVBHRS = 50;
  SiS_RY1COE = 0;
  SiS_RY2COE = 0;
  SiS_RY3COE = 0;
  SiS_RY4COE = 0;

  SiS_GetCRT2ResInfo(ROMAddr,ModeNo,ModeIdIndex,HwDeviceExtension);

  /* TW: For VGA2 ("RAMDAC2") */

  if(SiS_VBInfo & SetCRT2ToRAMDAC){
     SiS_GetRAMDAC2DATA(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                        HwDeviceExtension);
     return;
  }

  /* TW: For TV */

  if(SiS_VBInfo & SetCRT2ToTV) {

    SiS_GetCRT2Ptr(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                   &CRT2Index,&ResIndex,HwDeviceExtension);

    switch (CRT2Index) {
      case  2:  TVPtr = SiS_ExtHiTVData;   break;
      case  3:  TVPtr = SiS_ExtPALData;    break;
      case  4:  TVPtr = SiS_ExtNTSCData;   break;
      case  7:  TVPtr = SiS_St1HiTVData;   break;
      case  8:  TVPtr = SiS_StPALData;     break;
      case  9:  TVPtr = SiS_StNTSCData;    break;
      case 12:  TVPtr = SiS_St2HiTVData;   break;
      default:  TVPtr = SiS_StPALData;     break;  /* TW: Just to avoid a crash */
    }

    SiS_RVBHCMAX  = (TVPtr+ResIndex)->RVBHCMAX;
    SiS_RVBHCFACT = (TVPtr+ResIndex)->RVBHCFACT;
    SiS_VGAHT     = (TVPtr+ResIndex)->VGAHT;
    SiS_VGAVT     = (TVPtr+ResIndex)->VGAVT;
    SiS_HDE       = (TVPtr+ResIndex)->TVHDE;
    SiS_VDE       = (TVPtr+ResIndex)->TVVDE;
    SiS_RVBHRS    = (TVPtr+ResIndex)->RVBHRS;
    SiS_NewFlickerMode = (TVPtr+ResIndex)->FlickerMode;

    if(SiS_VBInfo & SetCRT2ToHiVisionTV) {

      	if(resinfo == 0x08) SiS_NewFlickerMode = 0x40;
      	if(resinfo == 0x09) SiS_NewFlickerMode = 0x40;
	if(resinfo == 0x12) SiS_NewFlickerMode = 0x40;   /* TW: Was resinfo == 0x10 */

        if(SiS_VGAVDE == 350) SiS_SetFlag |= TVSimuMode;

        SiS_HT = ExtHiTVHT;
        SiS_VT = ExtHiTVVT;
        if(SiS_VBInfo & SetInSlaveMode) {
          if(SiS_SetFlag & TVSimuMode) {
            SiS_HT = StHiTVHT;
            SiS_VT = StHiTVVT;
            if(!(modeflag & Charx8Dot)){
              SiS_HT = StHiTextTVHT;
              SiS_VT = StHiTextTVVT;
            }
          }
        }

    } else {

      SiS_RY1COE = (TVPtr+ResIndex)->RY1COE;
      SiS_RY2COE = (TVPtr+ResIndex)->RY2COE;
      SiS_RY3COE = (TVPtr+ResIndex)->RY3COE;
      SiS_RY4COE = (TVPtr+ResIndex)->RY4COE;

      if(modeflag & HalfDCLK) {
         SiS_RY1COE = 0x00;
         SiS_RY2COE = 0xf4;
         SiS_RY3COE = 0x10;
         SiS_RY4COE = 0x38;
      }

      if(!(SiS_VBInfo & SetPALTV)){
        SiS_HT = NTSCHT;
	if((ModeNo == 0x4a) || (ModeNo == 0x38)) SiS_HT = NTSC2HT;
        SiS_VT = NTSCVT;
      } else {
        SiS_HT = PALHT;
        SiS_VT = PALVT;
      }

    }

    return;
  }

  /* TW: For LCD */
  /* TW: Checked against 650/301LV; CRT2Index different (but does not matter) */

  if(SiS_VBInfo & SetCRT2ToLCD) {

    SiS_GetCRT2Ptr(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                   &CRT2Index,&ResIndex,HwDeviceExtension);

    switch (CRT2Index) {
      case  0: LCDPtr = SiS_ExtLCD1024x768Data;        break; /* VESA Timing */
      case  1: LCDPtr = SiS_ExtLCD1280x1024Data;       break; /* VESA Timing */
      case  5: LCDPtr = SiS_StLCD1024x768Data;         break; /* Obviously unused */
      case  6: LCDPtr = SiS_StLCD1280x1024Data;        break; /* Obviously unused */
      case 10: LCDPtr = SiS_St2LCD1024x768Data;        break; /* Non-VESA Timing */
      case 11: LCDPtr = SiS_St2LCD1280x1024Data;       break; /* Non-VESA Timing */
      case 13: LCDPtr = SiS_NoScaleData1024x768;       break; /* Non-expanding */
      case 14: LCDPtr = SiS_NoScaleData1280x1024;      break; /* Non-expanding */
      case 15: LCDPtr = SiS_LCD1280x960Data;           break; /* 1280x960 */
      default: LCDPtr = SiS_ExtLCD1024x768Data;	       break; /* Just to avoid a crash */
    }

    SiS_RVBHCMAX  = (LCDPtr+ResIndex)->RVBHCMAX;
    SiS_RVBHCFACT = (LCDPtr+ResIndex)->RVBHCFACT;
    SiS_VGAHT     = (LCDPtr+ResIndex)->VGAHT;
    SiS_VGAVT     = (LCDPtr+ResIndex)->VGAVT;
    SiS_HT        = (LCDPtr+ResIndex)->LCDHT;
    SiS_VT        = (LCDPtr+ResIndex)->LCDVT;

    tempax = 1024;
    if(SiS_SetFlag & LCDVESATiming) {
      if     (SiS_VGAVDE == 350) tempbx = 560;
      else if(SiS_VGAVDE == 400) tempbx = 640;
      else                       tempbx = 768;
    } else {
      if     (SiS_VGAVDE == 357) tempbx = 527;
      else if(SiS_VGAVDE == 420) tempbx = 620;
      else if(SiS_VGAVDE == 525) tempbx = 775;
      else if(SiS_VGAVDE == 600) tempbx = 775;
      else if(SiS_VGAVDE == 350) tempbx = 560;
      else if(SiS_VGAVDE == 400) tempbx = 640;
      else                       tempbx = 768;
    }
    if(SiS_LCDResInfo == Panel1280x1024){
      tempax = 1280;
      if     (SiS_VGAVDE == 360) tempbx = 768;
      else if(SiS_VGAVDE == 375) tempbx = 800;
      else if(SiS_VGAVDE == 405) tempbx = 864;
      else                       tempbx = 1024;
    }
    if(SiS_LCDResInfo == Panel1280x960){
      tempax = 1280;
      if     (SiS_VGAVDE == 350)  tempbx = 700;
      else if(SiS_VGAVDE == 400)  tempbx = 800;
      else if(SiS_VGAVDE == 1024) tempbx = 960;
      else                        tempbx = 960;
    }
    if(SiS_LCDInfo & LCDNonExpanding) {
       tempax = SiS_VGAHDE;
       tempbx = SiS_VGAVDE;
    }
    SiS_HDE = tempax;
    SiS_VDE = tempbx;
    return;
  }
}

USHORT
SiS_GetResInfo(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT resindex;

  if(ModeNo<=0x13)
    	resindex=SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  else
    	resindex=SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;

  return(resindex);
}

/* TW: Checked against 650/301LV, 650/LVDS, 630/LVDS, 630/301 and 630/301B BIOS */
void
SiS_GetCRT2ResInfo(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT xres,yres,modeflag=0,resindex;

  resindex = SiS_GetResInfo(ROMAddr,ModeNo,ModeIdIndex);

  if(ModeNo <= 0x13) {
    	xres = SiS_StResInfo[resindex].HTotal;
    	yres = SiS_StResInfo[resindex].VTotal;
  } else {
	xres = SiS_ModeResInfo[resindex].HTotal;
    	yres = SiS_ModeResInfo[resindex].VTotal;
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  /* TW: Inserted entire if-section from 650/LVDS BIOS 1.10.07: */
  if((HwDeviceExtension->jChipType >= SIS_315H) && (SiS_IF_DEF_LVDS == 1)) {
      if((ModeNo != 0x03) && (SiS_SetFlag & CRT2IsVGA)) {
          if(yres == 350) yres = 400;
      }
      if(SiS_GetReg1(SiS_P3d4,0x3a) & 0x01) {
 	  if(SiS_GetReg1(SiS_P3d4,0x34) == 0x12)
	      yres = 400;
      }
  }

  if(ModeNo > 0x13) {
      if(SiS_IF_DEF_FSTN == 1){
            xres *= 2;
            yres *= 2;
      } else {
  	    if(modeflag & HalfDCLK)       xres *= 2;
  	    if(modeflag & DoubleScanMode) yres *= 2;
      }
  }

  if(SiS_IF_DEF_LVDS == 0) {
        /* TW: Inserted from 650/301LV BIOS */
        if(SiS_VBInfo & SetCRT2ToLCDA) {
                if(xres == 720) xres = 640;
	} else {
	   if(xres == 720) xres = 640;
    	   if(SiS_LCDResInfo == Panel1280x1024) {
      		if(yres == 400) yres = 405;
      		if(yres == 350) yres = 360;
      		if(SiS_SetFlag & LCDVESATiming) {
        		if(yres == 360) yres = 375;
      		}
   	   }
    	   if(SiS_LCDResInfo == Panel1024x768){
      		if(!(SiS_SetFlag & LCDVESATiming)) {
        		if(!(SiS_LCDInfo & LCDNonExpanding)) {
          			if(yres == 350) yres = 357;
          			if(yres == 400) yres = 420;
            			if(yres == 480) yres = 525;
        		}
      		}
    	   }
	   /* TW: Inserted for 630/301B */
	   if(HwDeviceExtension->jChipType < SIS_315H) {
	      if(SiS_VBType & VB_SIS301BLV302BLV) {
                  if(xres == 720) xres = 640;
	      }
	   }
	}
  } else {
    	if(xres == 720) xres = 640;
	/* TW: Inserted from 650/LVDS and 630/LVDS BIOS */
	if(SiS_SetFlag & CRT2IsVGA) {
	      yres = 400;
	      if(HwDeviceExtension->jChipType >= SIS_315H) {
	          if(SiS_GetReg1(SiS_P3c4,0x17) & 0x80) yres = 480;
	      } else {
	          if(SiS_GetReg1(SiS_P3c4,0x13) & 0x80) yres = 480;
	      }
	}
  }
  SiS_VGAHDE = SiS_HDE = xres;
  SiS_VGAVDE = SiS_VDE = yres;
}

/* TW: Checked against 650/301 and 650/LVDS (1.10.07) BIOS; modified for new panel resolutions */
/* TW: Done differently in 630/301B BIOS; but same effect; checked against 630/301 */
void
SiS_GetCRT2Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
	       USHORT RefreshRateTableIndex,USHORT *CRT2Index,USHORT *ResIndex,
	       PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempbx=0,tempal=0;
  USHORT Flag,resinfo=0;

  if(SiS_IF_DEF_LVDS == 0) {
    	if(SiS_VBInfo & SetCRT2ToLCD){                            /* LCD */
	        if(SiS_LCDResInfo == Panel1280x960)  tempbx = 14;
		else if(SiS_LCDInfo & LCDNonExpanding) {
			tempbx = 13;
			if(SiS_LCDResInfo == Panel1280x1024) tempbx++;
		} else {
      		   tempbx = SiS_LCDResInfo - Panel1024x768;
      		   if(!(SiS_SetFlag & LCDVESATiming)) {
        		tempbx += 5;
                        /* GetRevisionID();  */
			/* TW: BIOS only adds 5 once */
        		tempbx += 5;
       		   }
	        }
     	} else {						/* TV */
       		if(SiS_VBInfo & SetCRT2ToHiVisionTV){
         		if(SiS_VGAVDE > 480) SiS_SetFlag &= (~TVSimuMode); /* TW: Was "(!TVSimuMode)" - WRONG */
         		tempbx = 2;
         		if(SiS_VBInfo & SetInSlaveMode) {
            			if(!(SiS_SetFlag & TVSimuMode)) tempbx = 12;  /* TW: Was 10! - WRONG */
         		}
       		} else {
         		if(SiS_VBInfo & SetPALTV) tempbx = 3;
         		else tempbx = 4;
         		if(SiS_SetFlag & TVSimuMode) tempbx += 5;
       		}
     	}

     	if(ModeNo <= 0x13)
       		tempal = SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
     	else
       		tempal = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

     	tempal &= 0x3F;

      	if((SiS_VBType & VB_SIS301BLV302BLV)
                     && (SiS_VBInfo & (SetCRT2ToTV-SetCRT2ToHiVisionTV))) {  /* TW: Added -Hivision (BIOS) */
      		if(tempal == 0x06) tempal = 0x07;
        }

        if(SiS_VBInfo & SetCRT2ToTV) {
            if((ModeNo == 0x31) || (ModeNo == 0x32)) tempal = 6;
	}

     	*CRT2Index = tempbx;
     	*ResIndex = tempal;

  } else {   /* LVDS */

    	Flag = 1;
    	tempbx = 0;
    	if(SiS_IF_DEF_CH70xx != 0) {
      		if(!(SiS_VBInfo & SetCRT2ToLCD)) {
        		Flag = 0;
        		tempbx = 10;
        		if(SiS_VBInfo & SetPALTV)        tempbx += 2;
        		if(SiS_VBInfo & SetCHTVOverScan) tempbx += 1;
      		}
    	}

    	if(Flag == 1) {
      		tempbx = SiS_LCDResInfo - PanelMinLVDS;
		if(SiS_LCDResInfo <= Panel1280x1024) {
   	      	    if(SiS_LCDInfo & LCDNonExpanding)  tempbx += 3;
		} else {
		    if(SiS_LCDResInfo == Panel1400x1050) {
			tempbx = 8;
			if(SiS_LCDInfo & LCDNonExpanding)  tempbx++;
		    }
        	    if(SiS_LCDInfo & 0x0100) {
			tempbx = 7;
        	    }

     		    if(SiS_LCDResInfo == Panel640x480)  tempbx = 6;

		    /* TW: Inserted from 630/LVDS 2.04.5c BIOS */
		    if(SiS_LCDResInfo == Panel1024x600) {
			tempbx = 15;
  		        if(SiS_LCDInfo & LCDNonExpanding)  tempbx += 2;
		    }
		    if(SiS_LCDResInfo == Panel1152x768) {
		        tempbx = 16;
			if(SiS_LCDInfo & LCDNonExpanding)  tempbx += 2;
		    }
		 }
	}

    	if(ModeNo <= 0x13)
      		tempal = SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
    	else {
      		tempal = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
		resinfo = SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
	}

	if(SiS_IF_DEF_FSTN){
       	 	if(SiS_LCDResInfo == Panel320x480){
         		tempbx = 14;
         		tempal = 6;
        	}
    	}

	/* TW: Inserted from 650/LVDS BIOS */
	if(SiS_SetFlag & CRT2IsVGA) {
	        if(SiS_LCDResInfo != Panel640x480) tempal = 7;
		if(HwDeviceExtension->jChipType < SIS_315H) {
		    /* TW: Inserted from 630/LVDS (2.04.5c) and 630/301B (II) BIOS */
		    if(SiS_GetReg1(SiS_P3c4,0x13) & 0x80) tempal++;
		}

	}

	/* TW: Inserted from 630/301B BIOS */
	if(SiS_VBType & VB_SIS301BLV302BLV) {
	    if(ModeNo > 0x13) {
	        if((resinfo == 0x0c) || (resinfo == 0x0d))
		    tempal = 6;
	    }
	}

    	*CRT2Index = tempbx;
    	*ResIndex = tempal & 0x1F;
  }
}

void
SiS_GetCRT2PtrA(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		USHORT RefreshRateTableIndex,USHORT *CRT2Index,
		USHORT *ResIndex)
{
  USHORT tempbx,tempal;

  tempbx = SiS_LCDResInfo - Panel1024x768;

  if(SiS_LCDInfo & LCDNonExpanding)  tempbx += 3;

  if(ModeNo <= 0x13)
      	tempal = SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
      	tempal = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  *CRT2Index = tempbx;
  *ResIndex = tempal & 0x1F;
}

/* TW: New from 650/301LV BIOS */
void
SiS_GetCRT2Part2Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		    USHORT RefreshRateTableIndex,USHORT *CRT2Index,
		    USHORT *ResIndex)
{
  USHORT tempbx,tempal;

  if(ModeNo <= 0x13)
      	tempal = SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
      	tempal = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  tempbx = SiS_LCDResInfo - Panel1024x768;

  if(SiS_LCDInfo & LCDNonExpanding)  tempbx += 2;
  else if(SiS_SetFlag & LCDVESATiming) tempbx += 4;

  *CRT2Index = tempbx;
  *ResIndex = tempal & 0x3F;
}

/* TW: Checked against 650/LVDS (1.10.07) and 630/301B BIOS */
USHORT
SiS_GetRatePtrCRT2(UCHAR *ROMAddr, USHORT ModeNo, USHORT ModeIdIndex)
{
  SHORT  LCDRefreshIndex[] = { 0x00, 0x00, 0x03, 0x01,
                               0x01, 0x01, 0x01, 0x01 };
  USHORT RefreshRateTableIndex,i,backup_i;
  USHORT modeflag,index,temp;

  if (ModeNo <= 0x13)
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  else
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;

  if(SiS_IF_DEF_CH70xx != 0) {
    	if(SiS_VBInfo & SetCRT2ToTV) {
      		if(modeflag & HalfDCLK) return(0);
    	}
  }

  if(ModeNo < 0x14) return(0xFFFF);

 /* TW: CR33 holds refresh rate index for CRT1 [3:0] and CRT2 [7:4].
  *     On LVDS machines, CRT2 index is always 0 and will be
  *     set to 0 by the following code; this causes the function
  *     to take the first non-interlaced mode in SiS_Ext2Struct
  */

  index = SiS_GetReg1(SiS_P3d4,0x33);
  index >>= SiS_SelectCRT2Rate;
  index &= 0x0F;

  if(index > 0) index--;

  /* TW: Added SetFlag and VBInfo checks; we don't care about index if we
   *     are setting CRT1 rate!
   */
  if( (SiS_SetFlag & ProgrammingCRT2) &&
      (SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) )
  	 index = 0;

  if(SiS_SetFlag & ProgrammingCRT2) {
    	if(SiS_IF_DEF_CH70xx != 0) {
      		if(SiS_VBInfo & SetCRT2ToTV) {
        		index = 0;
      		}
    	}
    	if(SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
      		if(SiS_IF_DEF_LVDS == 0) {
		        /* TW: This is not done in 630/301B BIOS */
           		temp = LCDRefreshIndex[SiS_LCDResInfo];
        		if(index > temp) index = temp;
      		} else {
        		index=0;
      		}
    	}
  }

  RefreshRateTableIndex = SiS_EModeIDTable[ModeIdIndex].REFindex;
  ModeNo = SiS_RefIndex[RefreshRateTableIndex].ModeID;

  /* TW: Inserted from 650/LVDS 1.10.07 */
  if(SiS_IF_DEF_LVDS == 1) {
    if(!(SiS_VBInfo & DriverMode)) {
      if( (SiS_EModeIDTable[ModeIdIndex].Ext_VESAID == 0x105) ||
          (SiS_EModeIDTable[ModeIdIndex].Ext_VESAID == 0x107) ) {
            if(SiS_LCDResInfo <= Panel800x600)
	       RefreshRateTableIndex++;
      }
    }
  }

  i = 0;
  do {
    	if (SiS_RefIndex[RefreshRateTableIndex+i].ModeID != ModeNo) break;
    	temp = SiS_RefIndex[RefreshRateTableIndex+i].Ext_InfoFlag;
    	temp &= ModeInfoFlag;
    	if(temp < SiS_ModeType) break;
    	i++;
    	index--;
  } while(index != 0xFFFF);

  if(!(SiS_VBInfo & SetCRT2ToRAMDAC)) {
    	if(SiS_VBInfo & SetInSlaveMode) {
      		temp = SiS_RefIndex[RefreshRateTableIndex + i - 1].Ext_InfoFlag;
      			if(temp & InterlaceMode) {
        			i++;
      			}
    	}
  }
  i--;

  if((SiS_SetFlag & ProgrammingCRT2) && (!(SiS_VBInfo & DisableCRT2Display))) {
    	backup_i = i;
    	if (!(SiS_AdjustCRT2Rate(ROMAddr,ModeNo,ModeIdIndex,
	                             RefreshRateTableIndex,&i))) {
		/* TW: This is for avoiding random data to be used; i is
		 *     in an undefined state if no matching CRT2 mode is
		 *     found.
		 */
		i = backup_i;
	}
  }

  return(RefreshRateTableIndex + i);
}

/* Checked against 650/LVDS (1.10.07) BIOS */
BOOLEAN
SiS_AdjustCRT2Rate(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex,USHORT *i)
{
  USHORT tempax,tempbx,resinfo;
  USHORT modeflag,infoflag;

  if (ModeNo <= 0x13)
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  else
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;

  resinfo = SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  tempbx = SiS_RefIndex[RefreshRateTableIndex + (*i)].ModeID;

  tempax = 0;
  if(SiS_IF_DEF_LVDS == 0) {
  	/* TW: For 301, 301B, 302B, 301LV, 302LV */
    	if(SiS_VBInfo & SetCRT2ToRAMDAC) {
      		tempax |= SupportRAMDAC2;
    	}
    	if(SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
      		tempax |= SupportLCD;
      		if(SiS_LCDResInfo != Panel1280x1024) {
        		if(SiS_LCDResInfo != Panel1280x960) {
           			if(SiS_LCDInfo & LCDNonExpanding) {
             				if(resinfo >= 9) {
               					tempax = 0;
               					return(0);
             				}
           			}
        		}
      		}
    	}
    	if(SiS_VBInfo & SetCRT2ToHiVisionTV) {    /* for HiTV */
      		tempax |= SupportHiVisionTV;
      		if(SiS_VBInfo & SetInSlaveMode){
        		if(resinfo == 4) return(0);
        		if(resinfo == 3) {
          			if(SiS_SetFlag & TVSimuMode) return(0);
        		}
        		if(resinfo > 7) return(0);
      		}
    	} else {
      		if(SiS_VBInfo & (SetCRT2ToAVIDEO|SetCRT2ToSVIDEO|SetCRT2ToSCART)) {
        		tempax |= SupportTV;
         		if(SiS_VBType & VB_SIS301BLV302BLV) {
             			tempax |= SupportTV1024;
         		}
        		if(!(SiS_VBInfo & SetPALTV)) {
          			if(modeflag & NoSupportSimuTV) {
            				if(SiS_VBInfo & SetInSlaveMode) {
              					if(!(SiS_VBInfo & SetNotSimuMode)) {
                					return 0;
              					}
            				}
          			}
        		}
      		}
    	}
  } else {
  	/* TW: for LVDS  */
    	if(SiS_IF_DEF_CH70xx != 0) {
      		if(SiS_VBInfo & SetCRT2ToTV) {
        		tempax |= SupportCHTV;
      		}
    	}
    	if(SiS_VBInfo & SetCRT2ToLCD) {
      		tempax |= SupportLCD;
		if(SiS_LCDResInfo == Panel1280x768) {
		     /* TW: Bios code makes no sense */
		} else if(SiS_LCDResInfo == Panel1400x1050) {
		     if((resinfo != 0x15) && (resinfo > 0x09)) return(0);
		} else if(SiS_LCDResInfo == Panel1280x1024) {
                     if(resinfo > 0x09) return(0);
                } else if(SiS_LCDResInfo == Panel1024x768) {
		     if(resinfo > 0x08) return(0);
		} else if(SiS_LCDResInfo == Panel800x600){
		     if(resinfo > 0x07) return(0);
		     if(resinfo == 0x04) return(0);
		}
    	}
  }
  /* TW: Look backwards in table for matching CRT2 mode */
  for(; SiS_RefIndex[RefreshRateTableIndex+(*i)].ModeID == tempbx; (*i)--) {
     	infoflag = SiS_RefIndex[RefreshRateTableIndex + (*i)].Ext_InfoFlag;
     	if(infoflag & tempax) {
       		return(1);
     	}
     	if ((*i) == 0) break;
  }
  /* TW: Look through the whole mode-section of the table from the beginning
   *     for a matching CRT2 mode if no mode was found yet.
   */
  for((*i) = 0; ; (*i)++) {
     	infoflag = SiS_RefIndex[RefreshRateTableIndex + (*i)].Ext_InfoFlag;
     	if(SiS_RefIndex[RefreshRateTableIndex + (*i)].ModeID != tempbx) {
       		return(0);
     	}
     	if(infoflag & tempax) {
       		return(1);
     	}
  }
  return(1);
}

/* Checked against 650/LVDS (1.10.07) and 650/301LV BIOS */
void
SiS_SaveCRT2Info(USHORT ModeNo)
{
  USHORT temp1,temp2;

  /* TW: We store CRT1 ModeNo in CR34 */
  SiS_SetReg1(SiS_P3d4,0x34,ModeNo);
  temp1 = (SiS_VBInfo & SetInSlaveMode) >> 8;
  temp2 = ~(SetInSlaveMode >> 8);
  SiS_SetRegANDOR(SiS_P3d4,0x31,temp2,temp1);
}

/* TW: Checked against 650+301, 650/LVDS (1.10.07) and 650/301LV BIOS */
void
SiS_GetVBInfo301(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                 USHORT ModeIdIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempax,tempbx,temp;
  USHORT modeflag, resinfo=0;
  UCHAR  OutputSelect = *pSiS_OutputSelect;

  if (ModeNo<=0x13)
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  else {
   	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	resinfo = SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  SiS_SetFlag = 0;

  SiS_ModeType = modeflag & ModeInfoFlag;

  tempbx = 0;
  if(SiS_BridgeIsOn(BaseAddr,HwDeviceExtension) == 0) {              /* TW: "== 0" inserted from 630/301B BIOS */
    	temp = SiS_GetReg1(SiS_P3d4,0x30);
    	if(SiS_VBType & (VB_SIS301LV | VB_SIS302LV)) {               /* TW: Not in (301B) BIOS */
       		temp &= 0xbf;   /* 301lvds disable CRT2RAMDAC */
    	}
    	if(SiS_IF_DEF_FSTN) {   /* fstn must set CR30=0x21 */
       		temp = 0x21;
       		SiS_SetReg1(SiS_P3d4,0x30,temp);
    	}
    	tempbx |= temp;
    	temp = SiS_GetReg1(SiS_P3d4,0x31);
	tempax = temp << 8;
        tempax &= (LoadDACFlag | DriverMode | SetDispDevSwitch |        /* TW: Inserted from 650/LVDS+301LV BIOS */
		         SetNotSimuMode | SetPALTV);                    /* TW: Inserted from 650/LVDS+301LV BIOS */
    	tempbx |= tempax;
    	temp = SetCHTVOverScan | SetInSlaveMode | DisableCRT2Display;
   	temp = 0xFFFF ^ temp;
    	tempbx &= temp;
#ifdef SIS315H
	if(HwDeviceExtension->jChipType >= SIS_315H) {       /* TW: Inserted this "if" */
	   temp = SiS_GetReg1(SiS_P3d4,0x38);
    	   if(SiS_VBType & (VB_SIS302B | VB_SIS302LV)) {
       		if((temp & (EnableDualEdge | SetToLCDA))
		          == (EnableDualEdge | SetToLCDA))   /* TW: BIOS only tests these bits, added "& ..." */
          		tempbx |= SetCRT2ToLCDA;
    	   }
	   /* TW: Inserted from 650/LVDS BIOS: */
	   if(SiS_IF_DEF_LVDS == 1) {
	        if(temp & SetToLCDA)
		        tempbx |= SetCRT2ToLCDA;
	        if(temp & 0x08)
		        tempbx |= SetCRT2ToHiVisionTV;
	   }
	}
#endif
    	if(SiS_IF_DEF_LVDS == 0) {
	        temp = SetCRT2ToLCDA   | SetCRT2ToSCART      | SetCRT2ToLCD |
		       SetCRT2ToRAMDAC | SetCRT2ToSVIDEO     | SetCRT2ToAVIDEO; /* = 0x807C; */
      		if(SiS_IF_DEF_HiVision == 1)
                     temp |= SetCRT2ToHiVisionTV; /* = 0x80FC; */
    	} else {
	        /* TW: Inserted entire 315-section */
                if(HwDeviceExtension->jChipType >= SIS_315H) {
                    if(SiS_IF_DEF_CH70xx != 0)
        		temp = SetCRT2ToLCDA   | SetCRT2ToSCART |
			       SetCRT2ToLCD    | SetCRT2ToHiVisionTV |
			       SetCRT2ToAVIDEO | SetCRT2ToSVIDEO;  /* 0x80bc */
      		    else
        		temp = SetCRT2ToLCDA | SetCRT2ToLCD;
		} else {
      		    if(SiS_IF_DEF_CH70xx != 0)
        		temp = SetCRT2ToTV | SetCRT2ToLCD;
      		    else
        		temp = SetCRT2ToLCD;
		}
    	}

    	if(!(tempbx & temp)) {
      		tempax = DisableCRT2Display;
      		tempbx = 0;
    	}

   	if(SiS_IF_DEF_LVDS==0) {
      		if(tempbx & SetCRT2ToLCDA) {
        		tempbx &= (0xFF00|SwitchToCRT2|SetSimuScanMode);
      		} else if(tempbx & SetCRT2ToRAMDAC) {
        		tempbx &= (0xFF00|SetCRT2ToRAMDAC|SwitchToCRT2|SetSimuScanMode);
      		} else if((tempbx & SetCRT2ToLCD) && (!(SiS_VBType & VB_NoLCD)) ){
        		tempbx &= (0xFF00|SetCRT2ToLCD|SwitchToCRT2|SetSimuScanMode);
      		} else if(tempbx & SetCRT2ToSCART){
        		tempbx &= (0xFF00|SetCRT2ToSCART|SwitchToCRT2|SetSimuScanMode);
        		tempbx |= SetPALTV;
      		} else if(tempbx & SetCRT2ToHiVisionTV){
        		tempbx &= (0xFF00|SetCRT2ToHiVisionTV|SwitchToCRT2|SetSimuScanMode);
        		tempbx |= SetPALTV;
      		}
   	} else { /* LVDS */
	        /* TW: Inserted entire 315/325 section */
	        if(HwDeviceExtension->jChipType >= SIS_315H) {
		    if(tempbx & SetCRT2ToLCDA)
		        tempbx &= (0xFF00|SwitchToCRT2|SetSimuScanMode);
		}
      		if(SiS_IF_DEF_CH70xx != 0) {
        	    if(tempbx & SetCRT2ToTV)
          		 tempbx &= (0xFF00|SetCRT2ToTV|SwitchToCRT2|SetSimuScanMode);
      		}
      		if(tempbx & SetCRT2ToLCD) {
        		tempbx &= (0xFF00|SetCRT2ToLCD|SwitchToCRT2|SetSimuScanMode);
		}
	        if(HwDeviceExtension->jChipType >= SIS_315H) {
		    if(tempbx & SetCRT2ToLCDA)
		        tempbx |= SetCRT2ToLCD;
		}
	}
    	if(tempax & DisableCRT2Display) {
      		if(!(tempbx & (SwitchToCRT2 | SetSimuScanMode))) {
        		tempbx = SetSimuScanMode | DisableCRT2Display;
      		}
    	}
    	if(!(tempbx & DriverMode)){
      		tempbx |= SetSimuScanMode;
    	}

	/* TW: LVDS (LCD/TV) and 630+301B (LCD) can only be slave in 8bpp modes */
	if( (SiS_IF_DEF_LVDS == 1) && (SiS_ModeType <= ModeVGA) ) {
		modeflag &= (~CRT2Mode);
	}
	if( (HwDeviceExtension->jChipType < SIS_315H) && (SiS_VBType & VB_SIS301BLV302BLV)) {
	        if(SiS_ModeType <= ModeVGA) {
			if(tempbx & SetCRT2ToLCD) {
		    		modeflag &= (~CRT2Mode);
			}
	        }
	}
	/* TW end */

    	if(!(tempbx & SetSimuScanMode)){
      		if(tempbx & SwitchToCRT2) {
        		if(!(modeflag & CRT2Mode)) {
			     if( (HwDeviceExtension->jChipType >= SIS_315H) &&
			         (SiS_VBType & VB_SIS301BLV302BLV) ) {
			        if(resinfo != 0x0a)
                                   tempbx |= SetSimuScanMode;
			     } else {
            			tempbx |= SetSimuScanMode;
	                     }

        		}
      		} else {
        		if(!(SiS_BridgeIsEnable(BaseAddr,HwDeviceExtension))) {
          			if(!(tempbx & DriverMode)) {
            				if(SiS_BridgeInSlave()) {
						tempbx |= SetSimuScanMode; /* TW: from BIOS 650/301/301LV/LVDS */
            				}
          			}
        		}
      		}
    	}

    	if(!(tempbx & DisableCRT2Display)) {
     		 if(tempbx & DriverMode) {
        		if(tempbx & SetSimuScanMode) {
          			if(!(modeflag & CRT2Mode)) {
				        if( (HwDeviceExtension->jChipType >= SIS_315H) &&
					    (SiS_VBType & VB_SIS301BLV302BLV) ) {
					        if(resinfo != 0x0a) {  /* TW: Inserted from 650/301 BIOS */
						    tempbx |= SetInSlaveMode;
            					    if(SiS_IF_DEF_LVDS == 0) {
              						if(tempbx & SetCRT2ToTV) {
                						if(!(tempbx & SetNotSimuMode))
									SiS_SetFlag |= TVSimuMode;
              						}
            					    }
					        }                      /* TW: Inserted from 650/301 BIOS */
					} else {
            					tempbx |= SetInSlaveMode;
            					if(SiS_IF_DEF_LVDS == 0) {
              						if(tempbx & SetCRT2ToTV) {
                						if(!(tempbx & SetNotSimuMode))
									SiS_SetFlag |= TVSimuMode;
              						}
            					}
         				}
				}
        		}
      		} else {
        		tempbx |= SetInSlaveMode;
        		if(SiS_IF_DEF_LVDS == 0) {
          			if(tempbx & SetCRT2ToTV) {
            				if(!(tempbx & SetNotSimuMode))
						SiS_SetFlag |= TVSimuMode;
          			}
        		}
      		}
    	}
    	if(SiS_IF_DEF_CH70xx == 1) {
      		temp = SiS_GetReg1(SiS_P3d4,0x35);
      		if(temp & TVOverScan) tempbx |= SetCHTVOverScan;
    	}
	if(SiS_IF_DEF_CH70xx == 2) {
      		temp = SiS_GetReg1(SiS_P3d4,0x79);
      		if(temp & 0x80) tempbx |= SetCHTVOverScan;
    	}
  }

  if(SiS_IF_DEF_LVDS==0) {
#ifdef SIS300
     	if((HwDeviceExtension->jChipType==SIS_630)||
           (HwDeviceExtension->jChipType==SIS_730)) {
           	if(!(OutputSelect & EnablePALMN))
             		SiS_SetRegAND(SiS_P3d4,0x35,0x3F);
           	if(tempbx & SetCRT2ToTV) {
              		if(tempbx & SetPALTV) {
                  		temp=SiS_GetReg1(SiS_P3d4,0x35);
                  		temp &= 0xC0;
                  		if(temp == 0x40)
                    			tempbx &= (~SetPALTV);
             		}
          	}
      	}
#endif
#ifdef SIS315H
     	if(HwDeviceExtension->jChipType >= SIS_315H) {
		if(!(OutputSelect & EnablePALMN))
        		SiS_SetRegAND(SiS_P3d4,0x38,0x3F);
   		if(tempbx & SetCRT2ToTV) {
    			if(tempbx & SetPALTV) {
               			temp = SiS_GetReg1(SiS_P3d4,0x38);
               			/* temp &= 0xC0;  */ /* TW: BIOS only tests 0x40, not 0x80 */
               			if(temp & 0x40)
               				tempbx &= (~SetPALTV);
              		}
        	}
  	}
#endif
  }

  SiS_VBInfo=tempbx;

  /* TW: DevSwitch not supported here */

#ifdef TWDEBUG
#ifdef LINUX_KERNEL
  printk(KERN_INFO "sisfb: (VBInfo = %x, SetFlag = %x)\n", SiS_VBInfo, SiS_SetFlag);
#endif
#ifdef LINUX_XF86
  xf86DrvMsg(0, X_INFO, "(init301: VBInfo = %x, SetFlag = %x)\n", SiS_VBInfo, SiS_SetFlag);
#endif
#endif

#if 0
  /* From 650/301LV BIOS: */
  if(ModeNo == 0x13) bp+4 = 0x03
  else bp+4 = ModeNo;
#endif

  /* TW: 630/301B and 650/301 (not 301LV!) BIOS do more here, but this seems for DOS mode */

}

void
SiS_GetRAMDAC2DATA(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex,
		   PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempax,tempbx,temp;
  USHORT temp1,temp2,modeflag=0,tempcx;
  USHORT StandTableIndex,CRT1Index;
  USHORT ResInfo,DisplayType;
  SiS_LVDSCRT1DataStruct *LVDSCRT1Ptr=NULL;

  SiS_RVBHCMAX=1;
  SiS_RVBHCFACT=1;

  if(ModeNo <= 0x13){

    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	StandTableIndex = SiS_GetModePtr(ROMAddr,ModeNo,ModeIdIndex);
    	tempax = SiS_StandTable[StandTableIndex].CRTC[0];
    	tempbx = SiS_StandTable[StandTableIndex].CRTC[6];
    	temp1 = SiS_StandTable[StandTableIndex].CRTC[7];

  } else {

     if( (SiS_VBType & VB_SIS301BLV302BLV) && (SiS_VBInfo&SetCRT2ToLCDA) ) {

    	temp=SiS_GetLVDSCRT1Ptr(ROMAddr,ModeNo,ModeIdIndex,
			RefreshRateTableIndex,&ResInfo,&DisplayType);

    	if(temp==0)  return;

    	switch(DisplayType) {
    		case 0 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_1;		break;
    		case 1 : LVDSCRT1Ptr = SiS_LVDSCRT11024x768_1;          break;
    		case 2 : LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_1;         break;
    		case 3 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_1_H;         break;
    		case 4 : LVDSCRT1Ptr = SiS_LVDSCRT11024x768_1_H;        break;
    		case 5 : LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_1_H;       break;
    		case 6 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_2;           break;
    		case 7 : LVDSCRT1Ptr = SiS_LVDSCRT11024x768_2;          break;
    		case 8 : LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_2;         break;
    		case 9 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_2_H;         break;
    		case 10: LVDSCRT1Ptr = SiS_LVDSCRT11024x768_2_H;        break;
    		case 11: LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_2_H;       break;
		case 12: LVDSCRT1Ptr = SiS_LVDSCRT1XXXxXXX_1;           break;
		case 13: LVDSCRT1Ptr = SiS_LVDSCRT1XXXxXXX_1_H;         break;
		case 14: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_1;         break;
		case 15: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_1_H;       break;
		case 16: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_2;         break;
		case 17: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_2_H;       break;
    		case 18: LVDSCRT1Ptr = SiS_CHTVCRT1UNTSC;               break;
    		case 19: LVDSCRT1Ptr = SiS_CHTVCRT1ONTSC;               break;
    		case 20: LVDSCRT1Ptr = SiS_CHTVCRT1UPAL;                break;
    		case 21: LVDSCRT1Ptr = SiS_CHTVCRT1OPAL;                break;
    		case 22: LVDSCRT1Ptr = SiS_LVDSCRT1320x480_1;           break;

    	}
    	temp1=(LVDSCRT1Ptr+ResInfo)->CR[0];
    	temp2=(LVDSCRT1Ptr+ResInfo)->CR[14];
    	tempax=(temp1&0xFF)|((temp2&0x03)<<8);
    	tempbx=(LVDSCRT1Ptr+ResInfo)->CR[6];
    	tempcx=(LVDSCRT1Ptr+ResInfo)->CR[13]<<8;
    	tempcx = tempcx&0x0100;
    	tempcx = tempcx << 2;
    	tempbx = tempbx | tempcx;
    	temp1=(LVDSCRT1Ptr+ResInfo)->CR[7];

    } else {

    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	CRT1Index = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
	if(HwDeviceExtension->jChipType < SIS_315H) {
    	   CRT1Index &= 0x3F;
	}
    	temp1 = (USHORT)SiS_CRT1Table[CRT1Index].CR[0];
    	temp2 = (USHORT)SiS_CRT1Table[CRT1Index].CR[14];
    	tempax=(temp1&0xFF)|((temp2&0x03)<<8);
    	tempbx = (USHORT)SiS_CRT1Table[CRT1Index].CR[6];
    	tempcx = (USHORT)SiS_CRT1Table[CRT1Index].CR[13]<<8;
    	tempcx = tempcx&0x0100;
    	tempcx = tempcx << 2;
    	tempbx = tempbx | tempcx;
    	temp1 = (USHORT)SiS_CRT1Table[CRT1Index].CR[7];

    }

  }

  if(temp1&0x01) tempbx |= 0x0100;
  if(temp1&0x20) tempbx |= 0x0200;
  tempax += 5;
  if(modeflag & Charx8Dot) tempax *= 8;
  else tempax *= 9;

  SiS_VGAHT = SiS_HT = tempax;
  tempbx++;
  SiS_VGAVT = SiS_VT = tempbx;
}

void
SiS_UnLockCRT2(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr)
{
  if(HwDeviceExtension->jChipType >= SIS_315H)
    	SiS_SetRegOR(SiS_Part1Port,0x2f,0x01);
  else
    	SiS_SetRegOR(SiS_Part1Port,0x24,0x01);
}

void
SiS_LockCRT2(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr)
{
  if(HwDeviceExtension->jChipType >= SIS_315H)
    	SiS_SetRegAND(SiS_Part1Port,0x2F,0xFE);
  else
     	SiS_SetRegAND(SiS_Part1Port,0x24,0xFE);
}

void
SiS_EnableCRT2()
{
  SiS_SetRegOR(SiS_P3c4,0x1E,0x20);
}

/* Checked against 650/LVDS(1.10.07)/301 and 630/301B BIOS */
void
SiS_DisableBridge(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr)
{
  USHORT tempah,temp;
  UCHAR *ROMAddr = HwDeviceExtension->pjVirtualRomBase;

  if (SiS_IF_DEF_LVDS == 0) {

      if(SiS_VBType & VB_SIS301BLV302BLV) {   /* ===== TW: For 30xB/LV ===== */

        if(HwDeviceExtension->jChipType < SIS_315H) {

	   /* 300 series */

	   if(!(SiS_CR36BIOSWord23b(HwDeviceExtension))) {
	      SiS_SetRegANDOR(SiS_P3c4,0x11,0xF7,0x08);
	      SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 3);
	   }
	   if(SiS_Is301B(BaseAddr)) {
	      SiS_SetRegAND(SiS_Part4Port,0x1f,0x3f);
	      SiS_ShortDelay(1);
	   }
	   SiS_SetRegAND(SiS_Part2Port,0x00,0xDF);
	   SiS_DisplayOff();
	   SiS_SetRegAND(SiS_P3c4,0x32,0xDF);
	   SiS_SetRegAND(SiS_P3c4,0x1E,0xDF);
	   SiS_UnLockCRT2(HwDeviceExtension,BaseAddr);
	   SiS_SetRegOR(SiS_Part1Port,0x01,0x80);
	   SiS_SetRegOR(SiS_Part1Port,0x02,0x40);
/*	   SiS_DoSomeThingPCI();    */  /* TW: Is this really required ? */
	   if( (!(SiS_CRT2IsLCD(BaseAddr))) || (!(SiS_CR36BIOSWord23d(HwDeviceExtension))) ) {
	      SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 2);
              SiS_SetRegANDOR(SiS_P3c4,0x11,0xFB,0x04);
	   }

        } else {

	   /* 310 series */

#if 0
           if(SiS_Is301B(BaseAddr)) {
#endif
             /* TW: Inserted from 650/301LV BIOS */
	     if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
	     	   SiS_SetRegANDOR(SiS_Part4Port,0x26,0xFE,0x00);
		   SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 3);
	     } else if (SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
		   SiS_SetRegANDOR(SiS_Part4Port,0x26,0xFE,0x00);
		   SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 3);
             }
	     /* TW end */
           /* TW: 301B dependent code starts here in 650/301LV BIOS */
	   if(SiS_Is301B(BaseAddr)) {
	     tempah = 0x3f;
#if 0        /* TW: This is not done in 650/301LV BIOS, instead 0x3f is used in any case */
             if(SiS_IsDualEdge(HwDeviceExtension, BaseAddr)) {
	        tempah = 0x7f;
	        if(!(SiS_IsVAMode(HwDeviceExtension, BaseAddr))) {
		   tempah = 0xbf;
                }
	     }
#endif
             SiS_SetRegAND(SiS_Part4Port,0x1F,tempah);
           } /* 301B dependent code ends here in 650/301V BIOS */
#if 0        /* TW: This is not done in 650/301LV BIOS */
	     if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
                SiS_SetRegAND(SiS_Part1Port,0x1E,0xDF);
	        SiS_DisplayOff();
	        SiS_SetRegAND(SiS_P3c4,0x32,0xDF);
	        return;
	     } else {
	        if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
                    SiS_SetRegAND(SiS_Part2Port,0x00,0xDF);
		    SiS_DisplayOff();
	        }
	     }
           } else {
#endif
                 SiS_SetRegAND(SiS_Part2Port,0x00,0xDF);
		 SiS_DisplayOff();
#if 0
	   }
#endif

           SiS_SetRegOR(SiS_Part1Port,0x00,0x80);

           SiS_SetRegAND(SiS_P3c4,0x32,0xDF);

	   temp = SiS_GetReg1(SiS_Part1Port,0x00);
           SiS_SetRegOR(SiS_Part1Port,0x00,0x10);
	   SiS_SetRegAND(SiS_P3c4,0x1E,0xDF);
	   SiS_SetReg1(SiS_Part1Port,0x00,temp);

	   /* TW: Inserted from 650/301LV BIOS */
	   if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
	       if(!(SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr))) {
	           if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
		          SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 2);
			  SiS_SetRegANDOR(SiS_Part4Port,0x26,0xFD,0x00);
			  SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 4);
                   } else if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
                          SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 2);
			  SiS_SetRegANDOR(SiS_Part4Port,0x26,0xFD,0x00);
			  SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 4);
		   }
	       }
	    } else if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
	       if (!(SiS_CRT2IsLCD(BaseAddr))) {
	            SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 2);
		    SiS_SetRegANDOR(SiS_Part4Port,0x26,0xFD,0x00);
		    SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 4);
               } else if(!(SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr))) {
	           if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
		          SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 2);
			  SiS_SetRegANDOR(SiS_Part4Port,0x26,0xFD,0x00);
			  SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 4);
                   } else if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
                          SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 2);
			  SiS_SetRegANDOR(SiS_Part4Port,0x26,0xFD,0x00);
			  SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 4);
		   }
	       }
	   }
	   /* TW: 650/301LV end */

	}

      } else {     /* ============ TW: For 301 ================ */

        if(HwDeviceExtension->jChipType < SIS_315H)
             SiS_SetRegANDOR(SiS_P3c4,0x11,0xF7,0x08);

        SiS_SetRegAND(SiS_Part2Port,0x00,0xDF);           /* disable VB */
        SiS_DisplayOff();

        if(HwDeviceExtension->jChipType >= SIS_315H)
            SiS_SetRegOR(SiS_Part1Port,0x00,0x80);

        SiS_SetRegAND(SiS_P3c4,0x32,0xDF);                /* disable lock mode */

        temp = SiS_GetReg1(SiS_Part1Port,0x00);
        SiS_SetRegOR(SiS_Part1Port,0x00,0x10);

        SiS_SetRegAND(SiS_P3c4,0x1E,0xDF);                /* disable CRT2 */
        SiS_SetReg1(SiS_Part1Port,0x00,temp);

	if(HwDeviceExtension->jChipType < SIS_315H)
	     SiS_SetRegANDOR(SiS_P3c4,0x11,0xFB,0x04);

      }

  } else {     /* ============ TW: For LVDS =============*/

    if(HwDeviceExtension->jChipType < SIS_315H) {

	/* 300 series */

	if(SiS_IF_DEF_CH70xx == 1) {
	    if(SiS_Backup70xx == 0xff) {
		SiS_Backup70xx = SiS_GetCH700x(0x0e);
	    }
	    SiS_SetCH700x(0x090E);
	}

	if(!(SiS_GetReg1(SiS_P3c4,0x11) & 0x08)) {

	    if(!(SiS_GetReg1(SiS_P3c4,0x13) & 0x40)) {

	        if(!(SiS_CR36BIOSWord23b(HwDeviceExtension))) {

                     SiS_WaitVBRetrace(HwDeviceExtension);

		     if(!(SiS_GetReg1(SiS_P3c4,0x06) & 0x1c)) {
		         SiS_DisplayOff();
	             }

	             SiS_SetRegANDOR(SiS_P3c4,0x11,0xF7,0x08);
	             SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 3);
                }
            }
	}

	SiS_DisplayOff();

	SiS_SetRegAND(SiS_P3c4,0x32,0xDF);

	SiS_SetRegAND(SiS_P3c4,0x1E,0xDF);
	SiS_UnLockCRT2(HwDeviceExtension,BaseAddr);
	SiS_SetRegOR(SiS_Part1Port,0x01,0x80);
	SiS_SetRegOR(SiS_Part1Port,0x02,0x40);

	if( (!(SiS_CRT2IsLCD(BaseAddr))) ||
	              (!(SiS_CR36BIOSWord23d(HwDeviceExtension))) ) {
		SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 2);
		SiS_SetRegANDOR(SiS_P3c4,0x11,0xFB,0x04);
	}

    } else {

	/* 310 series */

	if(SiS_IF_DEF_CH70xx == 2) {
		if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
			SiS_Chrontel701xOff();
			SiS_Chrontel701xOff2();
		} else if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
			SiS_Chrontel701xOff();
			SiS_Chrontel701xOff2();
		}

		if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
			SiS_SetCH701x(0x0149);
		} else if(SiS_IsTVOrSomething(HwDeviceExtension, BaseAddr))  {
			SiS_SetCH701x(0x0149);
		}
	}

	if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
		SiS_DisplayOff();
	} else if(!(SiS_IsTVOrSomething(HwDeviceExtension, BaseAddr))) {
		SiS_DisplayOff();
	}

	if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
		SiS_SetRegOR(SiS_Part1Port,0x00,0x80);
	} else if(!(SiS_IsVAMode(HwDeviceExtension, BaseAddr))) {
		SiS_SetRegOR(SiS_Part1Port,0x00,0x80);
	}

	SiS_SetRegAND(SiS_P3c4,0x32,0xDF);

	if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
		SiS_SetRegAND(SiS_P3c4,0x1E,0xDF);
	} else if(!(SiS_IsVAMode(HwDeviceExtension, BaseAddr))) {
		SiS_SetRegAND(SiS_P3c4,0x1E,0xDF);
	}

	if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
		SiS_SetRegAND(SiS_Part1Port,0x1e,0xdf);
	}

	if(SiS_IsDualEdge(HwDeviceExtension, BaseAddr)) {
		SiS_SetRegAND(SiS_Part1Port,0x13,0xff);
	} else {
		SiS_SetRegAND(SiS_Part1Port,0x13,0xfb);
	}

	SiS_UnLockCRT2(HwDeviceExtension, BaseAddr);

	if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
		SiS_SetRegAND(SiS_Part1Port,0x2e,0xf7);
	} else if(!(SiS_IsVAMode(HwDeviceExtension, BaseAddr))) {
		SiS_SetRegAND(SiS_Part1Port,0x2e,0xf7);
	}

#if 0  /* TW: BIOS code makes no sense */
       if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
           if(!(SiS_IsDualEdge(HwDeviceExtension, BaseAddr))) {
	        if(SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr)) {
		  /* Nothing there! */
		}
           }
       }
#endif

    }  /* 310 series */

  }  /* LVDS */

}

/* TW: Checked against 650/LVDS(1.10.07)/301 and 630/301B BIOS */
void
SiS_EnableBridge(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT  BaseAddr)
{
  USHORT temp=0,tempah,temp1;
  UCHAR *ROMAddr = HwDeviceExtension->pjVirtualRomBase;

  if(SiS_IF_DEF_LVDS == 0) {

    if(SiS_VBType & VB_SIS301BLV302BLV) {   /* TW: ====== For 301B ====== */

      if(HwDeviceExtension->jChipType < SIS_315H) {

         /* 300 series */

	 if(SiS_CRT2IsLCD(BaseAddr)) {
	    SiS_SetRegAND(SiS_P3c4,0x11,0xFB);
	    if(!(SiS_CR36BIOSWord23d(HwDeviceExtension))) {
	       SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 0);
	    }
	    SiS_SetRegOR(SiS_P3c4,0x1E,0x20);   /* Enable CRT2 */
/*	    DoSomeThingPCI_On() */
            SiS_DisplayOn();
	    SiS_UnLockCRT2(HwDeviceExtension, BaseAddr);
	    SiS_SetRegAND(SiS_Part1Port,0x02,0xBF);
	    if(SiS_BridgeInSlave()) {
      		SiS_SetRegAND(SiS_Part1Port,0x01,0x1F);
      	    } else {
      		SiS_SetRegANDOR(SiS_Part1Port,0x01,0x1F,0x40);
            }
	    if(!(SiS_GetReg1(SiS_P3c4,0x13) & 0x40)) {
	        if(!(SiS_GetReg1(SiS_P3c4,0x16) & 0x10)) {
		    if(!(SiS_CR36BIOSWord23b(HwDeviceExtension))) {
		        SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 1);
                    }
		    SiS_WaitVBRetrace(HwDeviceExtension);
                    SiS_SetRegANDOR(SiS_P3c4,0x11,0xF7,0x00);
                }
	    }
         } else {
	   temp = SiS_GetReg1(SiS_P3c4,0x32) & 0xDF;             /* lock mode */
           if(SiS_BridgeInSlave()) {
              tempah = SiS_GetReg1(SiS_P3d4,0x30);
              if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
           }
           SiS_SetReg1(SiS_P3c4,0x32,temp);
	   SiS_SetRegOR(SiS_P3c4,0x1E,0x20);
	   SiS_SetRegANDOR(SiS_Part2Port,0x00,0x1F,0x20);        /* enable VB processor */
	   if(SiS_Is301B(BaseAddr)) {
              SiS_SetRegOR(SiS_Part4Port,0x1F,0xC0);
	      SiS_DisplayOn();
	   } else {
	      SiS_VBLongWait();
	      SiS_DisplayOn();
	      SiS_VBLongWait();
	   }
	 }

      } else {

         /* 310 series */

	 /* TW: Inserted from 650/301LV BIOS */
	 if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
	     SiS_SetRegANDOR(SiS_Part4Port,0x26,0xfd,0x02);
	     SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 0);
	 } else if(SiS_CRT2IsLCD(BaseAddr)) {
	     SiS_SetRegANDOR(SiS_Part4Port,0x26,0xfd,0x02);
	     SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 0);
	 }
	 /* TW: --- end --- */

         if(!(SiS_IsVAMode(HwDeviceExtension, BaseAddr))) {
            temp = SiS_GetReg1(SiS_P3c4,0x32) & 0xDF;
	    if(SiS_BridgeInSlave()) {
               tempah = SiS_GetReg1(SiS_P3d4,0x30);
               if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
            }
            SiS_SetReg1(SiS_P3c4,0x32,temp);

	    SiS_SetRegOR(SiS_P3c4,0x1E,0x20);                   /* enable CRT2 */

/*          SiS_SetRegAND(SiS_Part1Port,0x2E,0x7F);   */ 	/* TW: Not done in 650/301LV BIOS */
            temp=SiS_GetReg1(SiS_Part1Port,0x2E);
            if (!(temp & 0x80))
                   SiS_SetRegOR(SiS_Part1Port,0x2E,0x80);
          }

          SiS_SetRegANDOR(SiS_Part2Port,0x00,0x1F,0x20);        /* enable VB processor */

          if(SiS_Is301B(BaseAddr)) {
#if 0	     /* TW: This is not done in 630/301LV BIOS */
	     if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
                SiS_SetRegOR(SiS_P3c4,0x1E,0x20);               /* enable CRT2 */
	     }
#endif
             /* TW: This is done instead: */
             SiS_SetRegOR(SiS_Part4Port,0x1F,0xc0);

#if 0	     /* TW: This is not done in 630/301LV BIOS */
	     temp=SiS_GetReg1(SiS_Part1Port,0x2E);
             if (!(temp & 0x80))
                SiS_SetRegOR(SiS_Part1Port,0x2E,0x80);

	     tempah = 0xC0;
	     if(SiS_IsDualEdge(HwDeviceExtension, BaseAddr)) {
	         tempah = 0x80;
	         if(!(SiS_IsVAMode(HwDeviceExtension, BaseAddr))) {
	             tempah = 0x40;
                 }
	     }
             SiS_SetRegOR(SiS_Part4Port,0x1F,tempah);
#endif
             if(!(SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr)))   /* TW: "if" new from 650/301LV BIOS */
	        SiS_SetRegAND(SiS_Part1Port,0x00,0x7F);

          } else {

             SiS_VBLongWait();
             SiS_DisplayOn();
	     if(!(SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr)))  {  /* TW: "if" new from 650/301LV BIOS */
	        SiS_SetRegAND(SiS_Part1Port,0x00,0x7F);
                SiS_VBLongWait();
	     }

          }

	  /* TW: Entire section from 650/301LV BIOS */
	  if(!(SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr))) {
	     if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
/*	        if (!(SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr))) {  */ /* TW: BIOS code makes no sense */
		   SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 1);
		   SiS_WaitVBRetrace(HwDeviceExtension);
		   SiS_SetRegANDOR(SiS_Part4Port,0x26,0xFE,0x01);
/*              }   */
             } else if(SiS_CRT2IsLCD(BaseAddr)) {
/*	        if (!(SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr))) {  */ /* TW: BIOS code makes no sense */
		   SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 1);
		   SiS_WaitVBRetrace(HwDeviceExtension);
		   SiS_SetRegANDOR(SiS_Part4Port,0x26,0xFE,0x01);
/*              }   */
	     }
	  }
	  /* TW: --- end --- */

      }

    } else {	/* ============  TW: For 301 ================ */

       if(HwDeviceExtension->jChipType < SIS_315H)
            SiS_SetRegANDOR(SiS_P3c4,0x11,0xFB,0x00);

       temp = SiS_GetReg1(SiS_P3c4,0x32) & 0xDF;             /* lock mode */
       if(SiS_BridgeInSlave()) {
         tempah = SiS_GetReg1(SiS_P3d4,0x30);
         if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
       }
       SiS_SetReg1(SiS_P3c4,0x32,temp);

       SiS_SetRegANDOR(SiS_P3c4,0x1E,0xFF,0x20);             /* enable CRT2 */

       if(HwDeviceExtension->jChipType >= SIS_315H) {        /* 310 series */
         temp=SiS_GetReg1(SiS_Part1Port,0x2E);
         if (!(temp & 0x80))
           SiS_SetRegOR(SiS_Part1Port,0x2E,0x80);            /* by alan,BVBDOENABLE=1 */
       }

       SiS_SetRegANDOR(SiS_Part2Port,0x00,0x1F,0x20);        /* enable VB processor */

       SiS_VBLongWait();
       SiS_DisplayOn();
       SiS_VBLongWait();

       if(HwDeviceExtension->jChipType < SIS_315H)
            SiS_SetRegANDOR(SiS_P3c4,0x11,0xF7,0x00);

    }

  } else {   /* =================== TW: For LVDS ================== */

    if(HwDeviceExtension->jChipType < SIS_315H) {

      /* 300 series */

      if(SiS_CRT2IsLCD(BaseAddr)) {
         SiS_SetRegAND(SiS_P3c4,0x11,0xFB);
	 if(!(SiS_CR36BIOSWord23d(HwDeviceExtension))) {
	    SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 0);
	 }
      }

      SiS_EnableCRT2();
      SiS_DisplayOn();
      SiS_UnLockCRT2(HwDeviceExtension, BaseAddr);
      SiS_SetRegAND(SiS_Part1Port,0x02,0xBF);
      if(SiS_BridgeInSlave()) {
      	SiS_SetRegAND(SiS_Part1Port,0x01,0x1F);
      } else {
      	SiS_SetRegANDOR(SiS_Part1Port,0x01,0x1F,0x40);
      }

      if(SiS_IF_DEF_CH70xx == 1) {
        if(!(SiS_CRT2IsLCD(BaseAddr))) {
           if (SiS_Backup70xx != 0xff) {
		SiS_SetCH700x(((SiS_Backup70xx<<8)|0x0E));
		SiS_Backup70xx = 0xff;
	   } else SiS_SetCH700x(0x0B0E);
        }
      }

      if(SiS_CRT2IsLCD(BaseAddr)) {
          if(!(SiS_GetReg1(SiS_P3c4,0x13) & 0x40)) {
              if(!(SiS_GetReg1(SiS_P3c4,0x16) & 0x10)) {
	          if(!(SiS_CR36BIOSWord23b(HwDeviceExtension))) {
			SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 1);
        		SiS_SetPanelDelay(ROMAddr, HwDeviceExtension, 1);
		  }
		  SiS_WaitVBRetrace(HwDeviceExtension);
                  SiS_SetRegAND(SiS_P3c4,0x11,0xF7);
              }
	  }
      }

    } else {

       /* 310 series */

#if 0  /* BIOS code makes no sense */
       if(SiS_IsVAMode()) {
          if(SiS_IsLCDOrLCDA()) {
	  }
       }
#endif

       SiS_EnableCRT2();
       SiS_UnLockCRT2(HwDeviceExtension, BaseAddr);

       SiS_SetRegAND(SiS_Part1Port,0x2e,0xf7);

       if(SiS_IF_DEF_CH70xx == 2) {
          temp = SiS_GetCH701x(0x66);
	  temp &= 0x20;
	  SiS_Chrontel701xOff();
       }

       SiS_SetRegAND(SiS_Part1Port,0x2e,0x7f);

       temp1 = SiS_GetReg1(SiS_Part1Port,0x2E);
       if (!(temp1 & 0x80))
           SiS_SetRegOR(SiS_Part1Port,0x2E,0x80);

       if(SiS_IF_DEF_CH70xx == 2) {
           if(temp) {
	       SiS_Chrontel701xOn();
	   }
       }

       if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
           SiS_SetRegOR(SiS_Part1Port,0x1E,0x20);
       }

       if(!(SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr))) {
           SiS_SetRegAND(SiS_Part1Port,0x00,0x7f);
       }

       if(SiS_IF_DEF_CH70xx == 2) {

       		if(SiS_IsTVOrSomething(HwDeviceExtension, BaseAddr)) {
           		SiS_Chrontel701xOn2(HwDeviceExtension, BaseAddr);
         	}

         	if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
           		SiS_ChrontelDoSomething1(HwDeviceExtension, BaseAddr);
         	} else if(SiS_IsLCDOrLCDA(HwDeviceExtension, BaseAddr)) {
           		SiS_ChrontelDoSomething1(HwDeviceExtension, BaseAddr);
        	}

       }

       if(SiS_IF_DEF_CH70xx == 2) {
       	 	if(!(SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr))) {
 	   		if(SiS_IsVAMode(HwDeviceExtension, BaseAddr)) {
	            		SiS_Chrontel701xOn();
	            		SiS_ChrontelDoSomething4(HwDeviceExtension, BaseAddr);
           		} else if(SiS_IsLCDOrLCDA(HwDeviceExtension, BaseAddr))  {
/*	      			if(!SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr)) {  */ /* TW: makes no sense */
            				SiS_Chrontel701xOn();
            				SiS_ChrontelDoSomething4(HwDeviceExtension, BaseAddr);
/*            			}   */
	   		}
       		}
       }

    } /* 310 series */

  }  /* LVDS */

}

BOOLEAN
SiS_CR36BIOSWord23b(PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,temp1;
  UCHAR *ROMAddr;

  if((ROMAddr = (UCHAR *)HwDeviceExtension->pjVirtualRomBase)) {
     temp = SiS_GetReg1(SiS_P3d4,0x36) & 0xff;
     temp >>= 4;
     temp = 1 << temp;
     temp1 = (ROMAddr[0x23c] << 8) | ROMAddr[0x23b];
     if(temp1 & temp) return(1);
     else return(0);
  } else {
     return(0);
  }
}

BOOLEAN
SiS_CR36BIOSWord23d(PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,temp1;
  UCHAR *ROMAddr;

  if((ROMAddr = (UCHAR *)HwDeviceExtension->pjVirtualRomBase)) {
     temp = SiS_GetReg1(SiS_P3d4,0x36) & 0xff;
     temp >>= 4;
     temp = 1 << temp;
     temp1 = (ROMAddr[0x23e] << 8) | ROMAddr[0x23d];
     if(temp1 & temp) return(1);
     else return(0);
  } else {
     return(0);
  }
}

void
SiS_SetPanelDelay(UCHAR *ROMAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
                  USHORT DelayTime)
{
  USHORT PanelID, DelayIndex, Delay, temp;

  if(HwDeviceExtension->jChipType < SIS_315H) {
     if(SiS_VBType & VB_SIS301BLV302BLV) {
         if(ROMAddr) {
	     if(!(ROMAddr[0x235] & 0x40)) return;
	 }
     }
  }

  if(HwDeviceExtension->jChipType < SIS_315H) {
      temp = SiS_GetReg1(SiS_P3c4,0x18);
  } else {
      temp = SiS_GetReg1(SiS_P3c4,0x1b);
  }

  if( (SiS_VBType & VB_SIS301BLV302BLV) && (!(temp & 0x10)) ) {
       PanelID = 0x12;
  } else {
       PanelID = SiS_GetReg1(SiS_P3d4,0x36);
  }

  DelayIndex = PanelID >> 4;

  if((DelayTime >= 2) && (PanelID & 0x0f) == 1) {
    Delay = 3;
  } else {
    if(DelayTime >= 2) DelayTime -= 2;
    if(SiS_IF_DEF_LVDS == 0) {
       if(!(DelayTime & 0x01)) {
       		Delay = SiS_PanelDelayTbl[DelayIndex].timer[0];
       } else {
       		Delay = SiS_PanelDelayTbl[DelayIndex].timer[1];
		if(HwDeviceExtension->jChipType >= SIS_315H) {
                    if(DelayTime & 0x04) Delay = 0x190;
                }
       }
    } else {
       if(!(DelayTime & 0x01)) {
       		Delay = SiS_PanelDelayTblLVDS[DelayIndex].timer[0];
       } else {
       		Delay = SiS_PanelDelayTblLVDS[DelayIndex].timer[1];
       }
    }
    if(ROMAddr) {
        if(HwDeviceExtension->jChipType < SIS_315H) {
          if(ROMAddr[0x220] & 0x40) {
            if(!(DelayTime & 0x01)) {
	    	Delay = (USHORT)ROMAddr[0x225];
            } else {
	    	Delay = (USHORT)ROMAddr[0x226];
            }
          }
        } else {
	  if(ROMAddr[0x13c] & 0x40) {
	    if(!(DelayTime & 0x01)) {
	    	Delay = (USHORT)ROMAddr[0x141];
            } else {
	    	Delay = (USHORT)ROMAddr[0x142];
		if(DelayTime & 0x04) Delay = 0x190;
            }
	  }
	}
    }
  }
  SiS_ShortDelay(Delay);
}

void
SiS_LongDelay(USHORT delay)
{
  while(delay--) {
    SiS_GenericDelay(0x19df);   /* 6623 */
  }
}

void
SiS_ShortDelay(USHORT delay)
{
  while(delay--) {
      SiS_GenericDelay(0x42);   /* 66 */
  }
}

void
SiS_GenericDelay(USHORT delay)
{
  USHORT temp,flag;

  flag = SiS_GetReg3(0x61) & 0x10;

  while(delay) {
      temp = SiS_GetReg3(0x61) & 0x10;
      if(temp == flag) continue;
      flag = temp;
      delay--;
  }
}

BOOLEAN
SiS_Is301B(USHORT BaseAddr)
{
  USHORT flag;

  flag = SiS_GetReg1(SiS_Part4Port,0x01);
  if(flag >= 0x0B0) return(1);
  else return(0);
}

BOOLEAN
SiS_CRT2IsLCD(USHORT BaseAddr)
{
  USHORT flag;

  flag = SiS_GetReg1(SiS_P3d4,0x30);
  if(flag & 0x20) return(1);
  else return(0);
}

BOOLEAN
SiS_IsDualEdge(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_P3d4,0x38);
     if(flag & EnableDualEdge)  return(1);    /* TW: Inverted result */
     else  return(0);
  } else
#endif
     return(0);
}

/* TW: Inverted result! */
BOOLEAN
SiS_IsVAMode(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_P3d4,0x38);
     if((flag & EnableDualEdge) && (flag & SetToLCDA))
        return(1);
     else if(SiS_VBType & VB_SIS301BLV302BLV) {   /* TW: Inserted from 650/301LV BIOS */
       if(flag) return(1);   			  /* TW: Inserted from 650/301LV BIOS */
       else     return(0);   			  /* TW: Inserted from 650/301LV BIOS */
     } else  return(0);
  } else
#endif
     return(0);
 }

BOOLEAN
SiS_WeHaveBacklightCtrl(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_P3d4,0x79);
     if(flag & 0x10)  return(1);
     else             return(0);
  } else
#endif
     return(0);
 }

#if 0
BOOLEAN
SiS_Is315E(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_P3d4,0x5f);
     if(flag & 0x10)  return(1);
     else      	      return(0);
  } else
#endif
     return(0);
}
#endif

BOOLEAN
SiS_IsYPbPr(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_P3d4,0x38);
     if(flag & 0x08)  return(1);
     else      	      return(0);
  } else
#endif
     return(0);
}

BOOLEAN
SiS_IsTVOrSomething(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
  USHORT flag;

#ifdef SIS315H
  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_P3d4,0x30);
     if(flag & SetCRT2ToTV) return(1);
     flag = SiS_GetReg1(SiS_P3d4,0x38);
     if(flag & 0x08)        return(1);
     else                   return(0);
  } else
#endif
  {
     flag = SiS_GetReg1(SiS_P3d4,0x30);
     if(flag & SetCRT2ToTV) return(1);
  }
  return(0);
}

BOOLEAN
SiS_IsLCDOrLCDA(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
  USHORT flag;

#ifdef SIS315H
  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_P3d4,0x30);
     if(flag & SetCRT2ToLCD) return(1);
     flag = SiS_GetReg1(SiS_P3d4,0x38);
     if(flag & SetToLCDA)    return(1);
     else                    return(0);
  } else
#endif
  {
   flag = SiS_GetReg1(SiS_P3d4,0x30);
   if(flag & SetCRT2ToLCD)   return(1);
  }
  return(0);

}

BOOLEAN
SiS_IsDisableCRT2(USHORT BaseAddr)
{
  USHORT flag;

  flag = SiS_GetReg1(SiS_P3d4,0x30);
  if(flag & 0x20) return(0);
  else            return(1);
}

BOOLEAN
SiS_BridgeIsOn(USHORT BaseAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT flag;

  if(SiS_IF_DEF_LVDS == 1) {
     return(0);   					/* TW: Changed from 1 to 0! */
  } else {
#if 0   /* TW: Commented for test on bridge-less systems */
     if(HwDeviceExtension->jChipType >= SIS_315H) {    	/* TW: New (from 630/301B BIOS - not done there) */
#endif
        flag = SiS_GetReg1(SiS_Part4Port,0x00);
        if((flag == 1) || (flag == 2)) return(0);       /* TW: Changed from 1 to 0! */
        else return(1);                                 /* TW: Changed from 0 to 1! */
#if 0
     } else  return(0);					/* TW: New (from 630/301B BIOS - always return 0) */
#endif
  }
}


BOOLEAN
SiS_BridgeIsEnable(USHORT BaseAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT flag;

  if(SiS_BridgeIsOn(BaseAddr,HwDeviceExtension) == 0) {
    flag=SiS_GetReg1(SiS_Part1Port,0x00);
    if(HwDeviceExtension->jChipType < SIS_315H) {
      /* 300 series */
      if(flag & 0x0a0) return 1;
      else	       return 0;
    } else {
      /* 310 series */
      if(flag & 0x050) return 1;
      else             return 0;
    }
  }
  return 0;
}

BOOLEAN
SiS_BridgeInSlave()
{
  USHORT flag1;

  flag1 = SiS_GetReg1(SiS_P3d4,0x31);
  if(flag1 & (SetInSlaveMode >> 8)) return 1;
  else return 0;
}

/* TW: New from 650/301LV BIOS */
void
SiS_SetHiVision(USHORT BaseAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  SiS_HiVision = 0;
  if(HwDeviceExtension->jChipType >= SIS_315H) {
     if(SiS_VBType & VB_SIS301BLV302BLV) {
        if(SiS_VBInfo & SetCRT2ToHiVisionTV) {
           SiS_HiVision = SiS_GetReg1(SiS_P3d4,0x38);
	   SiS_HiVision &= 0x38;
	   SiS_HiVision >>= 3;
        }
     }
  }
}

/* TW: Checked against 650/LVDS and 650/301LV BIOS */
BOOLEAN
SiS_GetLCDResInfo301(UCHAR *ROMAddr,USHORT SiS_P3d4,USHORT ModeNo,
               USHORT ModeIdIndex, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,modeflag,resinfo=0;

  SiS_LCDResInfo = 0;
  SiS_LCDTypeInfo = 0;
  SiS_LCDInfo = 0;

  if (ModeNo<=0x13) {
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  if(!(SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)))   return 0;

  if(!(SiS_VBInfo & (SetSimuScanMode | SwitchToCRT2))) return 0;

  temp = SiS_GetReg1(SiS_P3d4,0x36);

  /*fstn*: Fake CR36 (TypeInfo 2, ResInfo Panel320x480) */
  if(SiS_IF_DEF_FSTN){
   	temp = 0x20 | Panel320x480;
   	SiS_SetReg1(SiS_P3d4,0x36,temp);
  }

  SiS_LCDTypeInfo = temp >> 4;   /* BIOS uses entire CR36 - 1 */
  SiS_LCDResInfo = temp & 0x0F;

  if(SiS_IF_DEF_FSTN){
       	SiS_LCDResInfo = Panel320x480;
  }

  if(SiS_IF_DEF_LVDS == 0) {
    	if(SiS_LCDResInfo < PanelMin301) SiS_LCDResInfo = PanelMin301;
  } else {
    	if(SiS_LCDResInfo < PanelMinLVDS) SiS_LCDResInfo = PanelMinLVDS;
  }

  if(SiS_LCDResInfo > PanelMax) SiS_LCDResInfo = Panel1024x768;

  temp=SiS_GetReg1(SiS_P3d4,0x37);
  if(SiS_IF_DEF_FSTN){
        /* TW: Fake LVDS bridge for FSTN */
      	temp = 0x04;
      	SiS_SetReg1(SiS_P3d4,0x37,temp);
  }
  SiS_LCDInfo = temp;
  /* TW: Inserted entire 315-block from 650/LVDS BIOS */
  if(SiS_IF_DEF_LVDS == 1) {
     if (HwDeviceExtension->jChipType >= SIS_315H) {
        temp = SiS_GetReg1(SiS_P3d4,0x39);
        if(temp & 0x01) {
	   SiS_LCDInfo &= 0xFFEF;     /* TW: What is this? */
	   SiS_LCDInfo |= 0x0100;     /* TW: What is this? */
        }
     }
  }

#ifdef LINUX_KERNEL
  printk(KERN_INFO "sisfb: (LCDInfo = 0x%x LCDResInfo = 0x%x LCDTypeInfo = 0x%x)\n",
                   SiS_LCDInfo, SiS_LCDResInfo, SiS_LCDTypeInfo);
#endif
#ifdef LINUX_XF86
  xf86DrvMsg(0, X_INFO, "(init301: LCDInfo = 0x%x LCDResInfo = 0x%x LCDTypeInfo = 0x%x)\n",
			SiS_LCDInfo, SiS_LCDResInfo, SiS_LCDTypeInfo);
#endif

  /* TW: With Trumpion, always Expanding */
  if(SiS_IF_DEF_TRUMPION != 0){
       SiS_LCDInfo &= (~LCDNonExpanding);
  }

  /* TW: Removed LVDS==1 check here; done foe 301B BIOSes as well */
  if(modeflag & HalfDCLK){
        if(SiS_IF_DEF_TRUMPION == 0){
	    if((!(SiS_LCDInfo & 0x0100)) || (SiS_IF_DEF_LVDS == 0)) {  /* TW: Inserted from 650/LVDS BIOS */
               if(!(SiS_LCDInfo & LCDNonExpanding)){
	          if(!((SiS_IF_DEF_LVDS == 1) && (SiS_LCDResInfo == Panel640x480))){  /* TW: Inserted from 650/LVDS BIOS */
                     if(ModeNo > 0x13) {
                        if(SiS_LCDResInfo == Panel1024x768){
                           if(resinfo == 4){                              /* 512x384  */
                              SiS_SetFlag |= EnableLVDSDDA;
                           }
                        } else {
                           if(SiS_LCDResInfo == Panel800x600){
                              if(resinfo == 3){                           /* 400x300  */
                                 SiS_SetFlag |= EnableLVDSDDA;
                              }
                           }
                        }
                     }
		   } else {
		     SiS_SetFlag |= EnableLVDSDDA;
		   }
               } else { /* NonExpanding */
                 SiS_SetFlag |= EnableLVDSDDA;
               }
           } else {                          /* TW: Inserted from 650/LVDS BIOS */
	     SiS_SetFlag |= EnableLVDSDDA;   /* TW: Inserted from 650/LVDS BIOS */
	   }                                 /* TW: Inserted from 650/LVDS BIOS */
        } else { /* TRUMPION */
          SiS_SetFlag |= EnableLVDSDDA;
        }
  }

  /* TW: wdr: if (VBInfo & LCD) && (VBInfo & (SetSimuScanMode | SwitchToCRT2)) { */
  if(SiS_VBInfo & SetInSlaveMode){
    	if(SiS_VBInfo & SetNotSimuMode){
      		SiS_SetFlag |= LCDVESATiming;
    	}
  } else {
    	SiS_SetFlag |= LCDVESATiming;
  }

  return 1;
}

void
SiS_PresetScratchregister(USHORT SiS_P3d4,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  /*SiS_SetReg1(SiS_P3d4,0x30,0x21);  */
  /*SiS_SetReg1(SiS_P3d4,0x31,0x41);  */
  /*SiS_SetReg1(SiS_P3d4,0x32,0x28);  */
  /*SiS_SetReg1(SiS_P3d4,0x33,0x22);  */
  /*SiS_SetReg1(SiS_P3d4,0x35,0x43);  */
  /*SiS_SetReg1(SiS_P3d4,0x36,0x01);  */
  /*SiS_SetReg1(SiS_P3d4,0x37,0x00);  */
}

void
SiS_LongWait()
{
  USHORT i;

  i=SiS_GetReg1(SiS_P3c4,0x1F);
  if(!(i&0xC0)) {

    for(i=0; i<0xFFFF; i++) {
       if(!(SiS_GetReg2(SiS_P3da) & 0x08))
         break;
    }
    for(i=0; i<0xFFFF; i++) {
       if((SiS_GetReg2(SiS_P3da) & 0x08))
         break;
    }
  }
}

void
SiS_VBLongWait()
{
  if(!(SiS_VBInfo & SetCRT2ToTV)) {
    SiS_VBWait();
  } else {
    SiS_LongWait();
  }
  return;
}

void
SiS_VBWait(void)
{
  USHORT tempal,temp,i,j;

  temp=0;
  for(i=0;i<3;i++) {
    for(j=0;j<100;j++) {
       tempal=SiS_GetReg2(SiS_P3da);
       if(temp&0x01) {
          if((tempal&0x08))  continue;
          if(!(tempal&0x08)) break;
       } else {
          if(!(tempal&0x08)) continue;
          if((tempal&0x08))  break;
       }
    }
    temp=temp^0x01;
  }
}

void
SiS_WaitVBRetrace(PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  if(HwDeviceExtension->jChipType < SIS_315H) {
     if(SiS_VBType & VB_SIS301BLV302BLV) {
        if(!(SiS_GetReg1(SiS_Part1Port,0x00) & 0x20)) return;
     }
     if(!(SiS_GetReg1(SiS_Part1Port,0x00) & 0x80)) {
        SiS_WaitRetrace1(HwDeviceExtension);
     } else {
        SiS_WaitRetrace2(HwDeviceExtension);
     }
  } else {
     if(!(SiS_GetReg1(SiS_Part1Port,0x00) & 0x40)) {
        SiS_WaitRetrace1(HwDeviceExtension);
     } else {
        SiS_WaitRetrace2(HwDeviceExtension);
     }
  }
}

void
SiS_WaitRetrace1(PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT i,watchdog;
  
  if(HwDeviceExtension->jChipType >= SIS_315H) {
     if(SiS_GetReg1(SiS_P3c4,0x1f) & 0xc0) return;
     watchdog = 65535;
     while( (SiS_GetReg2(SiS_P3da) & 0x08) && --watchdog);
     watchdog = 65535;
     while( (!(SiS_GetReg2(SiS_P3da) & 0x08)) && --watchdog);
  } else {
#if 0  /* TW: Not done in A901 BIOS */
     if(SiS_VBType & VB_SIS301BLV302BLV) {
        if(SiS_GetReg1(SiS_P3c4,0x1f) & 0xc0) return;
     }
#endif
     for(i=0; i<10; i++) {
        watchdog = 65535;
        while( (SiS_GetReg2(SiS_P3da) & 0x08) && --watchdog);
	if(watchdog) break;
     }
     for(i=0; i<10; i++) {
        watchdog = 65535;
        while( (!(SiS_GetReg2(SiS_P3da) & 0x08)) && --watchdog);
	if(watchdog) break;
     }
  }
}

void
SiS_WaitRetrace2(PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT i,watchdog,temp;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     watchdog = 65535;
     while( (SiS_GetReg1(SiS_Part1Port,0x30) & 0x02) && --watchdog);
     watchdog = 65535;
     while( (!(SiS_GetReg1(SiS_Part1Port,0x30) & 0x02)) && --watchdog);
  } else {
     for(i=0; i<10; i++) {
        watchdog = 65535;
	while( (temp = SiS_GetReg1(SiS_Part1Port,0x25) & 0x02) && --watchdog);
	if(watchdog) break;
     }
     for(i=0; i<10; i++) {
        watchdog = 65535;
	while( (!(temp = SiS_GetReg1(SiS_Part1Port,0x25) & 0x02)) && --watchdog);
	if(watchdog) break;
     }
  }
}

/* =========== Set and Get register routines ========== */

void
SiS_SetRegANDOR(USHORT Port,USHORT Index,USHORT DataAND,USHORT DataOR)
{
  USHORT temp;

  temp=SiS_GetReg1(Port,Index);     /* SiS_Part1Port index 02 */
  temp=(temp&(DataAND))|DataOR;
  SiS_SetReg1(Port,Index,temp);
}

void
SiS_SetRegAND(USHORT Port,USHORT Index,USHORT DataAND)
{
  USHORT temp;

  temp=SiS_GetReg1(Port,Index);     /* SiS_Part1Port index 02 */
  temp=temp&DataAND;
  SiS_SetReg1(Port,Index,temp);
}

void SiS_SetRegOR(USHORT Port,USHORT Index,USHORT DataOR)
{
  USHORT temp;

  temp=SiS_GetReg1(Port,Index);     /* SiS_Part1Port index 02 */
  temp=temp|DataOR;
  SiS_SetReg1(Port,Index,temp);
}

/* ========================================================= */

/* TW: Set 301 TV Encoder (and some LCD relevant) registers */
/* TW: Checked against 650/301LV and 630/301B (I+II) */
void
SiS_SetGroup2(USHORT BaseAddr,UCHAR *ROMAddr, USHORT ModeNo,
                   USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
		   PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT i,j,tempax,tempbx,tempcx,temp,temp3;
  USHORT push1,push2,temp1;
  UCHAR *PhasePoint;
  UCHAR *TimingPoint;
  SiS_Part2PortTblStruct *CRT2Part2Ptr = NULL;
  USHORT  modeflag,resinfo,crt2crtc,resindex,CRT2Index;
  ULONG longtemp,tempeax,tempebx,temp2,tempecx;
  USHORT  SiS_RY1COE=0,SiS_RY2COE=0,SiS_RY3COE=0,SiS_RY4COE=0;
  UCHAR atable[] = {
           0xc3,0x9e,0xc3,0x9e,0x02,0x02,0x02,
	   0xab,0x87,0xab,0x9e,0xe7,0x02,0x02
  };

  /* TW: Inserted from 650/301LV BIOS */
  if(SiS_VBInfo & SetCRT2ToLCDA) return;

  if(ModeNo<=0x13) {
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;      /* si+St_ResInfo */
    	resinfo = SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
    	crt2crtc = SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;     /* si+Ext_ResInfo */
    	resinfo = SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
    	crt2crtc = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  tempcx = SiS_VBInfo;
  tempax = (tempcx & 0x00FF) << 8;
  tempbx = (tempcx & 0x00FF) | ((tempcx & 0x00FF) << 8);
  tempbx &= 0x0410;
  temp = (tempax & 0x0800) >> 8;
  temp >>= 1;
  temp |= (((tempbx & 0xFF00) >> 8) << 1);
  temp |= ((tempbx & 0x00FF) >> 3);
  temp = temp ^ 0x0C;

  PhasePoint = SiS_PALPhase;
#ifdef oldHV
  if(SiS_VBInfo & SetCRT2ToHiVisionTV) {          /* PALPhase */
    temp = temp ^ 0x01;
    if(SiS_VBInfo & SetInSlaveMode) {
      TimingPoint = SiS_HiTVSt2Timing;
      if(SiS_SetFlag & TVSimuMode) {
        if(modeflag & Charx8Dot) TimingPoint = SiS_HiTVSt1Timing;
        else TimingPoint = SiS_HiTVTextTiming;
      }
    } else TimingPoint = SiS_HiTVExtTiming;
  } else {
#endif
    if(SiS_VBInfo & SetPALTV){
      if( (SiS_VBType & VB_SIS301BLV302BLV) &&    /* TW: @@@ 650+301LV BIOS only tests 301B, 302B */
          ( (!(SiS_VBInfo & SetInSlaveMode)) || (SiS_SetFlag & TVSimuMode) ) )
         PhasePoint = SiS_PALPhase2;
      else
         PhasePoint = SiS_PALPhase;

         TimingPoint = SiS_PALTiming;
    } else {
        temp |= 0x10;
        if( (SiS_VBType & VB_SIS301BLV302BLV) &&  /* TW: @@@ 650+301LV BIOS only tests 301B, 302B */
	    ( (!(SiS_VBInfo & SetInSlaveMode)) || (SiS_SetFlag & TVSimuMode) ) )
        	PhasePoint = SiS_NTSCPhase2;
        else
        	PhasePoint = SiS_NTSCPhase;
      
        TimingPoint = SiS_NTSCTiming;
    }
#ifdef oldHV
  }
#endif
  SiS_SetReg1(SiS_Part2Port,0x00,temp);

#ifdef SIS300
  if((HwDeviceExtension->jChipType==SIS_630)||
     (HwDeviceExtension->jChipType==SIS_730)) {
     	if(SiS_VBInfo & SetCRT2ToTV) {
             if(SiS_GetReg1(SiS_P3d4,0x31) & 0x01) {
                  temp1 = SiS_GetReg1(SiS_P3d4,0x35);
                  if(temp1 & 0x40)
                     	PhasePoint = SiS_PALMPhase;
			if( (SiS_VBType & VB_SIS301BLV302BLV) &&  /* TW: From 650/301LV BIOS (see note above) */
			    ( (!(SiS_VBInfo & SetInSlaveMode)) || (SiS_SetFlag & TVSimuMode) ) )
		           PhasePoint = SiS_PALMPhase2;
                  if(temp1 & 0x80)
                     	PhasePoint = SiS_PALNPhase;
			if( (SiS_VBType & VB_SIS301BLV302BLV) &&  /* TW: From 650/301LV BIOS (see note above) */
			    ( (!(SiS_VBInfo & SetInSlaveMode)) || (SiS_SetFlag & TVSimuMode) ) )
		           PhasePoint = SiS_PALNPhase2;
             }
        }
  }
#endif

#ifdef SIS315H
  if(HwDeviceExtension->jChipType >= SIS_315H) {
           if(SiS_VBInfo & SetCRT2ToTV) {
              if(SiS_GetReg1(SiS_P3d4,0x31) & 0x01) {
                  temp1 = SiS_GetReg1(SiS_P3d4,0x38);
                  if(temp1 & 0x40) {
                     PhasePoint = SiS_PALMPhase;
		     if( (SiS_VBType & VB_SIS301BLV302BLV) &&  /* TW: @@@ From 650/301LV BIOS (see above) */
		         ( (!(SiS_VBInfo & SetInSlaveMode)) || (SiS_SetFlag & TVSimuMode) ) )
		        PhasePoint = SiS_PALMPhase2;           /* TW: From 650/301LV BIOS */
		  }
                  if(temp1 & 0x80) {
                     PhasePoint = SiS_PALNPhase;
		     if( (SiS_VBType & VB_SIS301BLV302BLV) && /* TW: @@@ From 650/301LV BIOS (see above) */
		         ( (!(SiS_VBInfo & SetInSlaveMode)) || (SiS_SetFlag & TVSimuMode) ) )
		        PhasePoint = SiS_PALNPhase2;          /* TW: From 650/301LV BIOS */
		  }
              }
           }
  }
#endif

  for(i=0x31, j=0; i<=0x34; i++, j++){
     SiS_SetReg1(SiS_Part2Port,i,PhasePoint[j]);
  }

  /* TW: Inserted from 650/301LV BIOS */
  if(SiS_VBType & (VB_SIS301LV | VB_SIS302LV)) {
     if(SiS_VBInfo & SetCRT2ToTV) {
        if(!(SiS_VBInfo & SetPALTV)) {
           if((ModeNo == 0x4a) || (ModeNo == 0x38)) {
               SiS_SetReg1(SiS_Part2Port,0x31,0x1e);
	       SiS_SetReg1(SiS_Part2Port,0x32,0x8c);
	       SiS_SetReg1(SiS_Part2Port,0x33,0x5c);
	       SiS_SetReg1(SiS_Part2Port,0x34,0x7a);
	   }
        }
     }
  }

  for(i=0x01, j=0; i<=0x2D; i++, j++){
     SiS_SetReg1(SiS_Part2Port,i,TimingPoint[j]);
  }
  for(i=0x39; i<=0x45; i++, j++){
     SiS_SetReg1(SiS_Part2Port,i,TimingPoint[j]);
  }
  if(SiS_VBInfo & SetCRT2ToTV) {
    if(HwDeviceExtension->jChipType >= SIS_315H) {
      if (!(SiS_ModeType & 0x07))
        SiS_SetRegAND(SiS_Part2Port,0x3A,0x1F);
    } else {
      SiS_SetRegAND(SiS_Part2Port,0x3A,0x1F);
    }
  }

  SiS_SetRegOR(SiS_Part2Port,0x0A,SiS_NewFlickerMode);

#if 0  /* TW: No BIOS does this */
     SiS_SetReg1(SiS_Part2Port,0x35,0x00); /*301b*/
     SiS_SetReg1(SiS_Part2Port,0x36,0x00);
     SiS_SetReg1(SiS_Part2Port,0x37,0x00);
     SiS_SetReg1(SiS_Part2Port,0x38,SiS_RY1COE);
     SiS_SetReg1(SiS_Part2Port,0x48,SiS_RY2COE);
     SiS_SetReg1(SiS_Part2Port,0x49,SiS_RY3COE);
     SiS_SetReg1(SiS_Part2Port,0x4a,SiS_RY4COE);

     /*add to change 630+301b filter*/
     resindex=SiS_GetResInfo(ROMAddr,ModeNo,ModeIdIndex);
     if(ModeNo<=0x13)
        xres = SiS_StResInfo[resindex].HTotal;
     else
        xres = SiS_ModeResInfo[resindex].HTotal;

     if(xres == 640) {  SiS_RY1COE=0xFF; SiS_RY2COE=0x03; SiS_RY3COE=0x02; SiS_RY4COE=0xF6;
                        SiS_RY5COE=0xFC; SiS_RY6COE=0x27; SiS_RY7COE=0x46;}
     if(xres == 800) {  SiS_RY1COE=0x01; SiS_RY2COE=0x01; SiS_RY3COE=0xFC; SiS_RY4COE=0xF8;
                        SiS_RY5COE=0x08; SiS_RY6COE=0x26; SiS_RY7COE=0x38;}
     if(xres == 1024){  SiS_RY1COE=0xFF; SiS_RY2COE=0xFF; SiS_RY3COE=0xFC; SiS_RY4COE=0x00;
                        SiS_RY5COE=0x0F; SiS_RY6COE=0x22; SiS_RY7COE=0x28;}
     if(xres == 720) {  SiS_RY1COE=0x01; SiS_RY2COE=0x02; SiS_RY3COE=0xFE; SiS_RY4COE=0xF7;
                        SiS_RY5COE=0x03; SiS_RY6COE=0x27; SiS_RY7COE=0x3c;}
     SiS_SetReg1(SiS_Part2Port,0x35,SiS_RY1COE); /*301b*/
     SiS_SetReg1(SiS_Part2Port,0x36,SiS_RY2COE);
     SiS_SetReg1(SiS_Part2Port,0x37,SiS_RY3COE);
     SiS_SetReg1(SiS_Part2Port,0x38,SiS_RY4COE);
     SiS_SetReg1(SiS_Part2Port,0x48,SiS_RY5COE);
     SiS_SetReg1(SiS_Part2Port,0x49,SiS_RY6COE);
     SiS_SetReg1(SiS_Part2Port,0x4a,SiS_RY7COE);
     /*end add*/
#endif

  /* TW: From 650/301LV and 630/301B BIOS: */
  SiS_SetReg1(SiS_Part2Port,0x35,SiS_RY1COE);
  SiS_SetReg1(SiS_Part2Port,0x36,SiS_RY2COE);
  SiS_SetReg1(SiS_Part2Port,0x37,SiS_RY3COE);
  SiS_SetReg1(SiS_Part2Port,0x38,SiS_RY4COE);

#ifdef oldHV
  if(SiS_VBInfo & SetCRT2ToHiVisionTV) tempax = 950;
  else {
#endif
    if(SiS_VBInfo & SetPALTV) tempax = 520;
    else tempax = 440;
#ifdef oldHV
  }
#endif

  if(SiS_VDE <= tempax) {
    tempax -= SiS_VDE;
    tempax >>= 2;
    tempax = (tempax & 0x00FF) | ((tempax & 0x00FF) << 8);
    push1 = tempax;
    temp = (tempax & 0xFF00) >> 8;
    temp += (USHORT)TimingPoint[0];
    SiS_SetReg1(SiS_Part2Port,0x01,temp);

    tempax = push1;
    temp = (tempax & 0xFF00) >> 8;
    temp += TimingPoint[1];
    SiS_SetReg1(SiS_Part2Port,0x02,temp);
  }

  if( (SiS_VBType & VB_SIS301BLV302BLV) &&
      (SiS_VBInfo & SetCRT2ToTV) &&
      (SiS_VGAHDE >= 1024) &&
      (SiS_HiVision != 3) ) {
      if(SiS_VBInfo & SetPALTV) {
         SiS_SetReg1(SiS_Part2Port,0x01,0x19);
         SiS_SetReg1(SiS_Part2Port,0x02,0x52);
      } else {
         if(HwDeviceExtension->jChipType >= SIS_315H) {
            SiS_SetReg1(SiS_Part2Port,0x01,0x17);
            SiS_SetReg1(SiS_Part2Port,0x02,0x1d);
	 } else {
            SiS_SetReg1(SiS_Part2Port,0x01,0x0b);
            SiS_SetReg1(SiS_Part2Port,0x02,0x11);
	 }
      } 
    }
    
  tempcx = SiS_HT - 1;
  if(SiS_VBType & VB_SIS301BLV302BLV) {
        tempcx--;
  }
  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Part2Port,0x1B,temp);
  temp = (tempcx & 0xFF00) >> 8;
  SiS_SetRegANDOR(SiS_Part2Port,0x1D,0xF0,temp);

  tempcx = SiS_HT >> 1;
  push1 = tempcx;                           /* push cx */
  tempcx += 7;
#ifdef oldHV
  if(SiS_VBInfo & SetCRT2ToHiVisionTV)  tempcx -= 4;   /* TW: @@@ not done in 301LV/630+301B BIOS */
#endif
  temp = (tempcx & 0x00FF) << 4;
  SiS_SetRegANDOR(SiS_Part2Port,0x22,0x0F,temp);

  tempbx = TimingPoint[j] | ((TimingPoint[j+1]) << 8);
  tempbx += tempcx;
  push2 = tempbx;
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Part2Port,0x24,temp);
  temp = ((tempbx & 0xFF00) >> 8) << 4;
  SiS_SetRegANDOR(SiS_Part2Port,0x25,0x0F,temp);

  tempbx = push2;

  tempbx += 8;
#ifdef oldHV
  if(SiS_VBInfo & SetCRT2ToHiVisionTV) {	/* TW: @@@ not done in 301LV/630+301B BIOS */
    tempbx -= 4;				/* TW: @@@ not done in 301LV/630+301B BIOS */
    tempcx = tempbx;				/* TW: @@@ not done in 301LV/630+301B BIOS */
  }						/* TW: @@@ not done in 301LV/630+301B BIOS */
#endif
  temp = (tempbx & 0x00FF) << 4;
  SiS_SetRegANDOR(SiS_Part2Port,0x29,0x0F,temp);

  j += 2;
  tempcx += ((TimingPoint[j] | ((TimingPoint[j+1]) << 8)));
  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Part2Port,0x27,temp);
  temp = ((tempcx & 0xFF00) >> 8) << 4;
  SiS_SetRegANDOR(SiS_Part2Port,0x28,0x0F,temp);

  tempcx += 8;
#ifdef oldHV
  if(SiS_VBInfo & SetCRT2ToHiVisionTV)  tempcx -= 4; /* TW: @@@ not done in 301LV BIOS */
#endif
  temp = (tempcx & 0xFF) << 4;
  SiS_SetRegANDOR(SiS_Part2Port,0x2A,0x0F,temp);

  tempcx = push1;
  j += 2;
  tempcx -= (TimingPoint[j] | ((TimingPoint[j+1]) << 8));
  temp = (tempcx & 0x00FF) << 4;
  SiS_SetRegANDOR(SiS_Part2Port,0x2D,0x0F,temp);

  tempcx -= 11;
  if(!(SiS_VBInfo & SetCRT2ToTV)){
    tempax = SiS_GetVGAHT2() - 1;
    tempcx = tempax;
  }
  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Part2Port,0x2E,temp);	  

  tempbx = SiS_VDE;
  if(SiS_VGAVDE == 360) tempbx = 746;
  if(SiS_VGAVDE == 375) tempbx = 746;
  if(SiS_VGAVDE == 405) tempbx = 853;
  if(HwDeviceExtension->jChipType < SIS_315H) {
  	if(SiS_VBInfo & SetCRT2ToTV) tempbx >>= 1;
  } else {
	if((SiS_VBInfo & SetCRT2ToTV) && (!(SiS_HiVision & 0x03))) {
	   tempbx >>= 1;
	   if(SiS_SetFlag & TVSimuMode) {
	      if(ModeNo <= 0x13) {
	         if(crt2crtc == 1) {
	            tempbx++;
                 }
	      }
	   } else {
              if(SiS_VBInfo & SetInSlaveMode) {
	         if(crt2crtc == 4)   /* TW: BIOS calls GetRatePtrCRT2 here - does not make sense */
                    if(SiS_ModeType <= 3) tempbx++;
	      }
	   }
        }
  }
  tempbx -= 2;
  temp = tempbx & 0x00FF;
#ifdef oldHV
  if(SiS_VBInfo & SetCRT2ToHiVisionTV) {
    if(SiS_VBInfo & SetInSlaveMode) {
      if(ModeNo == 0x2f) temp++;
    }
  }
#endif
  SiS_SetReg1(SiS_Part2Port,0x2F,temp);

  temp = (tempcx & 0xFF00) >> 8;
  temp |= (((tempbx & 0xFF00) >> 8) << 6);
#ifdef oldHV
  if(!(SiS_VBInfo & SetCRT2ToHiVisionTV)) {
#endif
    if(!(SiS_VBInfo & SetCRT2ToSCART)) {		/* TW: New from 630/301B (II) BIOS */
       temp |= 0x10;
       if(!(SiS_VBInfo & SetCRT2ToSVIDEO))  temp |= 0x20;
    }
#ifdef oldHV
  }
#endif
  SiS_SetReg1(SiS_Part2Port,0x30,temp);

  if(SiS_VBType & VB_SIS301BLV302BLV) {      /* tv gatingno */
    tempbx = SiS_VDE;
    if((SiS_VBInfo & SetCRT2ToTV) && (!(SiS_HiVision & 0x03))) {
         tempbx >>= 1;
    }
    temp = (((tempbx - 3) & 0x0300) >> 8) << 5;
    temp |= 0x18;                                    /* TW: Inserted from 650/301/301LV BIOS */
    SiS_SetReg1(SiS_Part2Port,0x46,temp);
    temp = (tempbx - 3) & 0x00FF;
    SiS_SetReg1(SiS_Part2Port,0x47,temp);
    if(SiS_HiVision & 0x03) {
        if(SiS_HiVision & 0x01) temp = 0x30;
	else temp = 0x50;
	SiS_SetReg1(SiS_Part2Port,0x4d,temp);
    }
  }

  tempbx &= 0x00FF;
  if(!(modeflag & HalfDCLK)){
    tempcx = SiS_VGAHDE;
    if(tempcx >= SiS_HDE){
      tempbx |= 0x2000;
      tempax &= 0x00FF;
    }
  }

  tempcx = 0x0101;
  if( (SiS_VBInfo & SetCRT2ToTV) && (!(SiS_HiVision & 0x03)) ) {  /*301b- TW: BIOS BUG! */
    if(SiS_VGAHDE >= 1024) {
      if(!(modeflag & HalfDCLK)) {   	/* TW: "if" inserted from 650/301LV and 630/301B BIOS */
        tempcx = 0x1920;
        if(SiS_VGAHDE >= 1280) {
          tempcx = 0x1420;
          tempbx &= 0xDFFF;
        }
      }
    }
  }

  if(!(tempbx & 0x2000)){

    if(modeflag & HalfDCLK){
      tempcx = (tempcx & 0xFF00) | (((tempcx & 0x00FF) << 1) & 0xff);
    }
    push1 = tempbx;
    tempeax = SiS_VGAHDE;
    tempebx = (tempcx & 0xFF00) >> 8;
    longtemp = tempeax * tempebx;
    tempecx = tempcx & 0x00FF;
    longtemp /= tempecx;
    longtemp <<= 0x0d;
    if(SiS_VBType & VB_SIS301BLV302BLV) {
     	longtemp <<= 3;
    }
    tempecx = SiS_HDE;
    temp2 = longtemp % tempecx;
    tempeax = longtemp / tempecx;
    if(temp2 != 0) tempeax++;
    tempax = (USHORT)tempeax;
    tempbx = push1;
    if(SiS_VBType & VB_SIS301BLV302BLV) {   /* TW: Done anyway in BIOS, but does not matter */
     	tempcx = ((tempax & 0xFF00) >> 5) >> 8;
    }
    tempbx |= (tempax & 0x1F00);
    tempax = ((tempax & 0x00FF) << 8) | (tempax & 0x00FF);
  }

  temp = (tempax & 0xFF00) >> 8;
  SiS_SetReg1(SiS_Part2Port,0x44,temp);
  temp = (tempbx & 0xFF00) >> 8;
  SiS_SetRegANDOR(SiS_Part2Port,0x45,0xC0,temp);

  if(SiS_VBType & VB_SIS301BLV302BLV) {
       if(tempbx & 0x2000)
    		tempcx=0x00;
       temp = tempcx;
       temp |= 0x18;
       SiS_SetRegANDOR(SiS_Part2Port,0x46,0xE0,temp);
       if(SiS_VBInfo & SetPALTV) {
             tempbx = 0x0382;    /* TW: BIOS; Was 0x0364; */
             tempcx = 0x007e;    /* TW: BIOS; Was 0x009c; */
       } else {
             tempbx = 0x0369;    /* TW: BIOS; Was 0x0346; */
             tempcx = 0x0061;    /* TW: BIOS; Was 0x0078; */
       }
       temp = (tempbx & 0x00FF) ;
       SiS_SetReg1(SiS_Part2Port,0x4B,temp);
       temp = (tempcx & 0x00FF) ;
       SiS_SetReg1(SiS_Part2Port,0x4C,temp);
       tempbx &= 0x0300;
       temp = (tempcx & 0xFF00) >> 8;
       temp = (temp & 0x0003) << 2;
       temp |= (tempbx >> 8);
       SiS_SetRegOR(SiS_Part2Port,0x4D,temp);   /* TW: 650/LV - was SetReg1() (not 630/301B) */

       temp = SiS_GetReg1(SiS_Part2Port,0x43);
       SiS_SetReg1(SiS_Part2Port,0x43,(USHORT)(temp - 3));
  }

#ifdef SIS300
  if((HwDeviceExtension->jChipType==SIS_630)||
     (HwDeviceExtension->jChipType==SIS_730)) {
          if(SiS_VBInfo & SetCRT2ToTV) {
             if(SiS_GetReg1(SiS_P3d4,0x31) & 0x01) {
                  if(SiS_GetReg1(SiS_P3d4,0x35) & 0x40) {
                           SiS_SetRegAND(SiS_Part2Port,0x00,0xEF);
                           temp3=SiS_GetReg1(SiS_Part2Port,0x01);
                           SiS_SetReg1(SiS_Part2Port,0x01,temp3-1);
                  }
             }
          }
  }
#endif

#ifdef SIS315H
  if (HwDeviceExtension->jChipType >= SIS_315H) {
            if(SiS_VBInfo & SetCRT2ToTV) {
               if(SiS_GetReg1(SiS_P3d4,0x31) & 0x01) {
                    if(SiS_GetReg1(SiS_P3d4,0x38) & 0x40) {
                             SiS_SetRegAND(SiS_Part2Port,0x00,0xEF);
                             temp3=SiS_GetReg1(SiS_Part2Port,0x01);
                             SiS_SetReg1(SiS_Part2Port,0x01,temp3-1);
                    }
               }
            }
  }
  /*end add*/
#endif

#ifdef oldHV
  if(SiS_VBInfo & SetCRT2ToHiVisionTV) {
    if(!(SiS_VBInfo & SetInSlaveMode)) {
      SiS_SetReg1(SiS_Part2Port,0x0B,0x00);
    }
  }
#endif

  if(HwDeviceExtension->jChipType < SIS_315H) {
     if(SiS_VBInfo & SetCRT2ToTV)  return;
  } else {
     if(SiS_VBInfo & SetCRT2ToTV) {
       if(SiS_VBType & (VB_SIS301LV | VB_SIS302LV)) {
         if(SiS_VBInfo & SetCRT2ToTV) {
           if(!(SiS_VBInfo & SetPALTV)) {
             if((ModeNo == 0x4a) || (ModeNo == 0x38)) {
               SiS_SetReg1(SiS_Part2Port,0x1c,0xa7);
	       SiS_SetReg1(SiS_Part2Port,0x1d,0x07);
	       SiS_SetReg1(SiS_Part2Port,0x1e,0xf2);
	       SiS_SetReg1(SiS_Part2Port,0x1f,0x6e);
	       SiS_SetReg1(SiS_Part2Port,0x20,0x17);
	       SiS_SetReg1(SiS_Part2Port,0x21,0x8b);
	       SiS_SetReg1(SiS_Part2Port,0x22,0x73);
	       SiS_SetReg1(SiS_Part2Port,0x23,0x53);
	       SiS_SetReg1(SiS_Part2Port,0x24,0x13);
	       SiS_SetReg1(SiS_Part2Port,0x25,0x40);
	       SiS_SetReg1(SiS_Part2Port,0x26,0x34);
	       SiS_SetReg1(SiS_Part2Port,0x27,0xf4);
	       SiS_SetReg1(SiS_Part2Port,0x28,0x63);
	       SiS_SetReg1(SiS_Part2Port,0x29,0xbb);
	       SiS_SetReg1(SiS_Part2Port,0x2a,0xcc);
	       SiS_SetReg1(SiS_Part2Port,0x2b,0x7a);
	       SiS_SetReg1(SiS_Part2Port,0x2c,0x58);
	       SiS_SetReg1(SiS_Part2Port,0x2d,0xe4);
	       SiS_SetReg1(SiS_Part2Port,0x2e,0x73);
	       SiS_SetReg1(SiS_Part2Port,0x2f,0xda);
	       SiS_SetReg1(SiS_Part2Port,0x30,0x13);
	       SiS_SetReg1(SiS_Part2Port,0x43,0x72);
	     }
           }
         }
       }
       return;
     }
  }

  /* TW: From here: LCD Part2 group */

  tempbx = SiS_HDE - 1;         /* RHACTE=HDE-1 */
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Part2Port,0x2C,temp);
  temp = (tempbx & 0xFF00) >> 8;
  temp <<= 4;
  SiS_SetRegANDOR(SiS_Part2Port,0x2B,0x0F,temp);

  temp = 0x01;
  if(SiS_LCDResInfo == Panel1280x1024) {
    if(SiS_ModeType == ModeEGA) {
      if(SiS_VGAHDE >= 1024) {
        temp = 0x02;
	if(HwDeviceExtension->jChipType >= SIS_315H) {
           if (SiS_SetFlag & LCDVESATiming) {
             temp = 0x01;
	   }
	}
      }
    }
  }
  SiS_SetReg1(SiS_Part2Port,0x0B,temp);

  tempbx = SiS_VDE;         /* RTVACTEO=(VDE-1)&0xFF */
  push1 = tempbx;
  tempbx--;
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Part2Port,0x03,temp);
  temp = ((tempbx & 0xFF00) >> 8) & 0x07;
  SiS_SetRegANDOR(SiS_Part2Port,0x0C,0xF8,temp);

  tempcx = SiS_VT;
  push2 = tempcx;
  tempcx--;
  temp = tempcx & 0x00FF;   /* RVTVT=VT-1 */
  SiS_SetReg1(SiS_Part2Port,0x19,temp);

  temp = (tempcx & 0xFF00) >> 8;
  temp <<= 5;
  if(HwDeviceExtension->jChipType < SIS_315H) {
    if(SiS_VBType & VB_SIS301BLV302BLV) temp |= 0x10;
    else {
#if 0
      if(SiS_LCDInfo & LCDRGB18Bit)   /* TW: 630/301B (II) BIOS does not check this!!! */
#endif
      if(SiS_LCDInfo & LCDSync)       /* TW: 630/301 BIOS checks this */
         temp |= 0x10;
    }
  } else temp |= 0x10;

  /* 630/301 does not do all this */
  if((SiS_VBType & VB_SIS301BLV302BLV) && (SiS_VBInfo & SetCRT2ToLCD)) {
      tempbx = (tempbx & 0xFF00) | (SiS_LCDInfo & 0x0FF);
      if(tempbx & LCDSync) {
        tempbx &= (0xFF00 | LCDSyncBit);
        tempbx = (tempbx & 0xFF00) | ((tempbx & 0x00FF) >> LCDSyncShift);
        temp |= (tempbx & 0x00FF);
      }
  }
  SiS_SetReg1(SiS_Part2Port,0x1A,temp);

  SiS_SetRegAND(SiS_Part2Port,0x09,0xF0);
  SiS_SetRegAND(SiS_Part2Port,0x0A,0xF0);

  SiS_SetRegAND(SiS_Part2Port,0x17,0xFB);
  SiS_SetRegAND(SiS_Part2Port,0x18,0xDF);

  if(HwDeviceExtension->jChipType >= SIS_315H) {   /* 310 series */

      /* TW: Inserted this entire section from 650/301LV BIOS */

      SiS_GetCRT2Part2Ptr(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                         &CRT2Index,&resindex);

      switch(CRT2Index) {
        case 0: CRT2Part2Ptr = SiS_CRT2Part2_1024x768_1;  break;  /* "Normal" */
        case 1: CRT2Part2Ptr = SiS_CRT2Part2_1280x1024_1; break;
        case 2: CRT2Part2Ptr = SiS_CRT2Part2_1024x768_2;  break;  /* Non-Expanding */
        case 3: CRT2Part2Ptr = SiS_CRT2Part2_1280x1024_2; break;
        case 4: CRT2Part2Ptr = SiS_CRT2Part2_1024x768_3;  break;  /* VESA Timing */
        case 5: CRT2Part2Ptr = SiS_CRT2Part2_1280x1024_3; break;
      }

      SiS_SetRegANDOR(SiS_Part2Port,0x01,0x80,(CRT2Part2Ptr+resindex)->CR[0]);
      SiS_SetRegANDOR(SiS_Part2Port,0x02,0x80,(CRT2Part2Ptr+resindex)->CR[1]);
      for(i = 2, j = 0x04; j <= 0x06; i++, j++ ) {
        SiS_SetReg1(SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      for(j = 0x1c; j <= 0x1d; i++, j++ ) {
        SiS_SetReg1(SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      for(j = 0x1f; j <= 0x21; i++, j++ ) {
        SiS_SetReg1(SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      SiS_SetReg1(SiS_Part2Port,0x23,(CRT2Part2Ptr+resindex)->CR[10]);
      SiS_SetRegANDOR(SiS_Part2Port,0x25,0x0f,(CRT2Part2Ptr+resindex)->CR[11]);

      if(!(SiS_SetFlag & LCDVESATiming)) {
        if(SiS_VGAVDE == 0x20d) {
	  temp = 0xc3;
	  if(SiS_ModeType <= ModeVGA) {
	     temp++;
	     if(SiS_VBType & VB_SIS301BLV302BLV) temp += 2;
	  }
	  SiS_SetReg1(SiS_Part2Port,0x2f,temp);
	  SiS_SetReg1(SiS_Part2Port,0x30,0xb3);
	}
	if(SiS_VGAVDE == 0x1a4) {
	  temp = 0x4d;
	  if(SiS_ModeType <= ModeVGA) {
	     temp++;
	     if(SiS_VBType & VB_SIS301BLV302BLV) temp++;
	  }
	  SiS_SetReg1(SiS_Part2Port,0x2f,temp);
	}
     } /* 2f5d */

  } else {   /* 300 series */

    tempcx++;
    tempbx = 768;
    if(SiS_LCDResInfo != Panel1024x768) {
      tempbx = 1024;
      if(SiS_LCDResInfo != Panel1280x1024) {
         tempbx = 1200;
         if(SiS_LCDResInfo != Panel1600x1200) {
            if(tempbx != SiS_VDE) {
               tempbx = 960;
            }
         }
      }
    }
    if(SiS_LCDInfo & LCDNonExpanding) {
      tempbx = SiS_VDE - 1;
      tempcx--;
    }
    tempax = 1;
    if(!(SiS_LCDInfo & LCDNonExpanding)) {
      if(tempbx != SiS_VDE){
        tempax = tempbx;
        if(tempax < SiS_VDE) {
          tempax = 0;
          tempcx = 0;
        } else {
          tempax -= SiS_VDE;
        }
        tempax >>= 1;
      }
      tempcx -= tempax; /* lcdvdes */
      tempbx -= tempax; /* lcdvdee */
    } else {
      tempax >>= 1;
      tempcx -= tempax; /* lcdvdes */
      tempbx -= tempax; /* lcdvdee */
    }

    temp = tempcx & 0x00FF;   /* RVEQ1EQ=lcdvdes */
    SiS_SetReg1(SiS_Part2Port,0x05,temp);
    temp = tempbx & 0x00FF;   /* RVEQ2EQ=lcdvdee */
    SiS_SetReg1(SiS_Part2Port,0x06,temp);

    temp = ((tempbx & 0xFF00) >> 8 ) << 3;
    temp |= ((tempcx & 0xFF00) >> 8);
    SiS_SetReg1(SiS_Part2Port,0x02,temp);

    tempbx = push2;
    tempax = push1;
    tempcx = tempbx;
    tempcx -= tempax;
    tempcx >>= 4;
    tempbx += tempax;
    tempbx >>= 1;
    if(SiS_LCDInfo & LCDNonExpanding)  tempbx -= 10;

    temp = tempbx & 0x00FF;   		/* RTVACTEE=lcdvrs */
    SiS_SetReg1(SiS_Part2Port,0x04,temp);

    temp = ((tempbx & 0xFF00) >> 8) << 4;
    tempbx += (tempcx + 1);
    temp |= (tempbx & 0x000F);
    SiS_SetReg1(SiS_Part2Port,0x01,temp);

    /* TW: Code from 630/301B (I+II) BIOS */

    if( ( ( (HwDeviceExtension->jChipType == SIS_630) ||
            (HwDeviceExtension->jChipType == SIS_730) ) &&
          (HwDeviceExtension->jChipRevision > 2) )  &&
        (SiS_LCDResInfo == Panel1024x768) &&
        (!(SiS_SetFlag & LCDVESATiming))  &&
        (!(SiS_LCDInfo & LCDNonExpanding)) ) {
            if(ModeNo == 0x13) {
              SiS_SetReg1(SiS_Part2Port,0x04,0xB9);
              SiS_SetReg1(SiS_Part2Port,0x05,0xCC);
              SiS_SetReg1(SiS_Part2Port,0x06,0xA6);
            } else {
              if((crt2crtc & 0x3F) == 4) {
                SiS_SetReg1(SiS_Part2Port,0x01,0x2B);
                SiS_SetReg1(SiS_Part2Port,0x02,0x13);
                SiS_SetReg1(SiS_Part2Port,0x04,0xE5);
                SiS_SetReg1(SiS_Part2Port,0x05,0x08);
                SiS_SetReg1(SiS_Part2Port,0x06,0xE2);
              }
            }
    }

    /* TW: Inserted missing code from 630/301B BIOS: (II: 3258) */

    if(SiS_LCDTypeInfo == 0x0c) {
         crt2crtc &= 0x1f;
         tempcx = 0;
         if(!(SiS_VBInfo & SetNotSimuMode)) {
           if (SiS_VBInfo & SetInSlaveMode) {
              tempcx += 7;
           }
         }
         tempcx += crt2crtc;
         if (crt2crtc >= 4) {
           SiS_SetReg1(SiS_Part2Port,0x06,0xff);
         }

         if(!(SiS_VBInfo & SetNotSimuMode)) {
           if (SiS_VBInfo & SetInSlaveMode) {
             if (crt2crtc == 4) {
                SiS_SetReg1(SiS_Part2Port,0x01,0x28);
             }
           }
         }
         SiS_SetReg1(SiS_Part2Port,0x02,0x18);
         SiS_SetReg1(SiS_Part2Port,0x04,atable[tempcx]);
    }

    tempcx = (SiS_HT - SiS_HDE) >> 2;    	/* (HT-HDE)>>2     */
    tempbx = SiS_HDE + 7;            		/* lcdhdee         */
    if(SiS_VBType & VB_SIS301BLV302BLV) {
         tempbx += 2;
    }
    push1 = tempbx;
    temp = tempbx & 0x00FF;    			/* RHEQPLE=lcdhdee */
    SiS_SetReg1(SiS_Part2Port,0x23,temp);
    temp = (tempbx & 0xFF00) >> 8;
    SiS_SetRegANDOR(SiS_Part2Port,0x25,0xF0,temp);

    temp = 7;
    if(SiS_VBType & VB_SIS301BLV302BLV) {
         temp += 2;
    }
    SiS_SetReg1(SiS_Part2Port,0x1F,temp);  	/* RHBLKE=lcdhdes */

    SiS_SetRegAND(SiS_Part2Port,0x20,0x0F);

    tempbx += tempcx;
    push2 = tempbx;
    temp = tempbx & 0xFF;            		/* RHBURSTS=lcdhrs */
    if(SiS_LCDResInfo == Panel1280x1024) {
      if(SiS_LCDInfo & LCDNonExpanding) {
        if(SiS_HDE == 1280)  temp = 0x47;
      }
    }
    SiS_SetReg1(SiS_Part2Port,0x1C,temp);
    temp = ((tempbx & 0xFF00) >> 8) << 4;
    SiS_SetRegANDOR(SiS_Part2Port,0x1D,0x0F,temp);

    tempbx = push2;
    tempcx <<= 1;
    tempbx += tempcx;
    temp = tempbx & 0x00FF;            		/* RHSYEXP2S=lcdhre */
    SiS_SetReg1(SiS_Part2Port,0x21,temp);

    if(!(SiS_SetFlag & LCDVESATiming)) {
      if(SiS_VGAVDE == 525) {
/*      if(SiS_VBType & VB_SIS301BLV302BLV)  */ /* TW: 630/301B (I+II) */
        if(SiS_ModeType <= ModeVGA)
    	   temp=0xC6;
        else
       	   temp=0xC3;   /* 650: c4 */
        SiS_SetReg1(SiS_Part2Port,0x2f,temp);
        SiS_SetReg1(SiS_Part2Port,0x30,0xB3);
      } else if(SiS_VGAVDE==420) {
/*      if(SiS_VBType & VB_SIS301BLV302BLV)  */ /* TW: 630/301B (I+II) */
        if(SiS_ModeType <= ModeVGA)
	   temp=0x4F;
        else
       	   temp=0x4D;   /* 650: 4e */
        SiS_SetReg1(SiS_Part2Port,0x2f,temp);
      }
    }

  } /* HwDeviceExtension */
}

USHORT
SiS_GetVGAHT2()
{
  ULONG tempax,tempbx;

  tempbx = ((SiS_VGAVT - SiS_VGAVDE) * SiS_RVBHCMAX) & 0xFFFF;
  tempax = (SiS_VT - SiS_VDE) * SiS_RVBHCFACT;
  tempax = (tempax * SiS_HT) / tempbx;
  return((USHORT) tempax);
}

/* TW: Set 301 Macrovision(tm) registers */
/* TW: Double-Checked against 650/301LV and 630/301B BIOS */
void
SiS_SetGroup3(USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
              USHORT ModeIdIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT i;
  UCHAR  *tempdi;
  USHORT modeflag;

  if(SiS_VBInfo & SetCRT2ToLCDA) return;   /* TW: Inserted from BIOS */

  if(ModeNo<=0x13)
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  else
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;

  SiS_SetReg1(SiS_Part3Port,0x00,0x00);

  if(SiS_VBInfo & SetPALTV) {
    SiS_SetReg1(SiS_Part3Port,0x13,0xFA);
    SiS_SetReg1(SiS_Part3Port,0x14,0xC8);
  } else {
    if(HwDeviceExtension->jChipType >= SIS_315H) {
      SiS_SetReg1(SiS_Part3Port,0x13,0xF5);
      SiS_SetReg1(SiS_Part3Port,0x14,0xB7);
    } else {
      SiS_SetReg1(SiS_Part3Port,0x13,0xF6);
      SiS_SetReg1(SiS_Part3Port,0x14,0xBf);
    }
  }

#ifdef SIS300
  if((HwDeviceExtension->jChipType==SIS_630)||
     (HwDeviceExtension->jChipType==SIS_730)) {
           if(SiS_VBInfo & SetCRT2ToTV) {
              if(SiS_GetReg1(SiS_P3d4,0x31) & 0x01) {
                  if(SiS_GetReg1(SiS_P3d4,0x35) & 0x40){
                          SiS_SetReg1(SiS_Part3Port,0x13,0xFA);
                          SiS_SetReg1(SiS_Part3Port,0x14,0xC8);
                          SiS_SetReg1(SiS_Part3Port,0x3D,0xA8);
                  }
              }
          }
  }
#endif

#ifdef SIS315H
  if(HwDeviceExtension->jChipType >= SIS_315H) {
         if(SiS_VBInfo & SetCRT2ToTV) {
             if(SiS_GetReg1(SiS_P3d4,0x31) & 0x01) {
                   if(SiS_GetReg1(SiS_P3d4,0x38) & 0x40){
                          SiS_SetReg1(SiS_Part3Port,0x13,0xFA);
                          SiS_SetReg1(SiS_Part3Port,0x14,0xC8);
                          SiS_SetReg1(SiS_Part3Port,0x3D,0xA8);
                   }
             }
         }
  }
#endif

  if(SiS_VBInfo & SetCRT2ToHiVisionTV) {
    tempdi = SiS_HiTVGroup3Data;
    if(SiS_SetFlag & TVSimuMode) {
      tempdi = SiS_HiTVGroup3Simu;
      if(!(modeflag & Charx8Dot)) {
        tempdi = SiS_HiTVGroup3Text;
      }
    }
    for(i=0; i<=0x3E; i++){
       SiS_SetReg1(SiS_Part3Port,i,tempdi[i]);
    }
  }
  return;
}

/* TW: Set 301 VGA2 registers */
/* TW: Double-Checked against 650/301LV and 630/301B BIOS */
void
SiS_SetGroup4(USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
              USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
	      PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempax,tempcx,tempbx,modeflag,temp,temp2,push1;
  ULONG tempebx,tempeax,templong;

  if(ModeNo<=0x13)
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  else
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;

  /* TW: From 650/301LV BIOS (done above as well, but does not matter) */
  if(SiS_VBType & (VB_SIS301LV | VB_SIS302LV)) {
      if(SiS_VBInfo & SetCRT2ToLCDA)
          SiS_SetReg1(SiS_Part4Port,0x24,0x0e);
  }

  /* TW: From 650/301LV BIOS */
  if(SiS_VBInfo & SetCRT2ToLCDA) return;

  temp = SiS_RVBHCFACT;
  SiS_SetReg1(SiS_Part4Port,0x13,temp);

  tempbx = SiS_RVBHCMAX;
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Part4Port,0x14,temp);

  temp2 = (((tempbx & 0xFF00) >> 8) << 7) & 0x00ff;

  tempcx = SiS_VGAHT - 1;
  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Part4Port,0x16,temp);

  temp = (((tempcx & 0xFF00) >> 8) << 3) & 0x00ff;
  temp2 |= temp;

  tempcx = SiS_VGAVT - 1;
  if(!(SiS_VBInfo & SetCRT2ToTV))  tempcx -= 5;

  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Part4Port,0x17,temp);

  temp = temp2 | ((tempcx & 0xFF00) >> 8);
  SiS_SetReg1(SiS_Part4Port,0x15,temp);

  tempcx = SiS_VBInfo;
  tempbx = SiS_VGAHDE;
  if(modeflag & HalfDCLK)  tempbx >>= 1;

  /* TW: New for 650/301LV and 630/301B */
  temp = 0xA0;
  if(SiS_VBInfo & SetCRT2ToHiVisionTV) {
       temp = 0xA0;
       if(tempbx != 1024) {
           temp = 0xC0;
           if(tempbx != 1280) temp = 0;
       }
  } else if(SiS_VBInfo & SetCRT2ToTV) {
      if(tempbx <= 800) {
         temp = 0x80;
	 if(SiS_VBInfo & SetCRT2ToLCD){
            temp = 0;
            if(tempbx > 800) temp = 0x60;
         }
      }
  } else {
      temp = 0x80;
      if(SiS_VBInfo & SetCRT2ToLCD){
            temp = 0;
            if(tempbx > 800) temp = 0x60;
      }
  }
  if(SiS_HiVision & 0x03) {
        temp = 0;
	if(SiS_VGAHDE == 1024) temp = 0x20;
  }
  SiS_SetRegANDOR(SiS_Part4Port,0x0E,0x10,temp);

  tempebx = SiS_VDE;
  if(SiS_VBInfo & SetCRT2ToHiVisionTV) {
     if(!(temp & 0xE0)) tempebx >>=1;
  }

  tempcx = SiS_RVBHRS;
  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Part4Port,0x18,temp);

  tempeax = SiS_VGAVDE;
  tempcx |= 0x4000;
  if(tempeax <= tempebx){
    tempcx = ((tempcx & 0xFF00) ^ 0x4000) | (tempcx & 0x00ff);
  } else {
    tempeax -= tempebx;
  }

  push1 = tempcx;

  templong = (tempeax * 256 * 1024) % tempebx;
  tempeax = (tempeax * 256 * 1024) / tempebx;
  tempebx = tempeax;
  if(templong != 0) tempebx++;

  tempcx = push1;

  temp = (USHORT)(tempebx & 0x000000FF);
  SiS_SetReg1(SiS_Part4Port,0x1B,temp);
  temp = (USHORT)((tempebx & 0x0000FF00) >> 8);
  SiS_SetReg1(SiS_Part4Port,0x1A,temp);

  tempbx = (USHORT)(tempebx >> 16);
  temp = tempbx & 0x00FF;
  temp <<= 4;
  temp |= ((tempcx & 0xFF00) >> 8);
  SiS_SetReg1(SiS_Part4Port,0x19,temp);

  if(SiS_VBType & VB_SIS301BLV302BLV) {
         SiS_SetReg1(SiS_Part4Port,0x1C,0x28);
	 tempbx = 0;   			/* TW: From 630/301B and 650/301LV BIOS */
         tempax = SiS_VGAHDE;
         if(modeflag & HalfDCLK) tempax >>= 1;
         if((SiS_VBInfo & SetCRT2ToLCD) || (SiS_HiVision & 0x03)) {
             if(tempax > 800) tempax -= 800;
         }

         if((SiS_VBInfo & (SetCRT2ToTV | SetCRT2ToHiVisionTV)) &&
	                                 (!(SiS_HiVision & 0x03))) {
           if(tempax > 800) {
	      tempbx = 8;  		/* TW: From 630/301B and 650/301LV BIOS */
              if(tempax == 1024)
	        tempax *= 25;
              else
	        tempax *= 20;

	      temp = tempax % 32;
	      tempax /= 32;
	      tempax--;
	      if (temp!=0) tempax++;
           }
         }
	 tempax--;
         temp = (tempax & 0xFF00) >> 8;
         temp &= 0x03;
	 SiS_SetReg1(SiS_Part4Port,0x1D,tempax & 0x00FF);
	 temp <<= 4;
	 temp |= tempbx;
	 SiS_SetReg1(SiS_Part4Port,0x1E,temp);

         temp = 0x0036;
         if((SiS_VBInfo & (SetCRT2ToTV-SetCRT2ToHiVisionTV)) &&
	                               (!(SiS_HiVision & 0x03))) {  /* TW: From 650/301LV BIOS */
		temp |= 0x01;
	        if(SiS_VBInfo & SetInSlaveMode) {                   /* TW: From 650/301LV BIOS */
	          if(!(SiS_SetFlag & TVSimuMode))                   /* TW: From 650/301LV BIOS */
  	                  temp &= 0xFE;                             /* TW: From 650/301LV BIOS */
		}
         }
         SiS_SetRegANDOR(SiS_Part4Port,0x1F,0xC0,temp);
         tempbx = (SiS_HT >> 1) - 2;
         temp = ((tempbx & 0x0700) >> 8) << 3;
         SiS_SetRegANDOR(SiS_Part4Port,0x21,0xC0,temp);
         temp = tempbx & 0x00FF;
         SiS_SetReg1(SiS_Part4Port,0x22,temp);
         if( (SiS_VBType & (VB_SIS301LV | VB_SIS302LV)) &&
	                        (SiS_VBInfo & SetCRT2ToLCD) ) {
             SiS_SetReg1(SiS_Part4Port,0x24,0x0e);
	 }
  }

  SiS_SetCRT2VCLK(BaseAddr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,HwDeviceExtension);
}

/* TW: Double-Checked against 650/301LV and 630/301B BIOS */
void
SiS_SetCRT2VCLK(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                 USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT vclkindex;
  USHORT tempah,temp1;

  vclkindex = SiS_GetVCLK2Ptr(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                              HwDeviceExtension);

  if(SiS_VBType & VB_SIS301BLV302BLV) {
   	tempah = SiS_VBVCLKData[vclkindex].Part4_A;
   	SiS_SetReg1(SiS_Part4Port,0x0A,tempah);
   	tempah = SiS_VBVCLKData[vclkindex].Part4_B;
   	SiS_SetReg1(SiS_Part4Port,0x0B,tempah);
	/* TW: New from 650/301LV BIOS */
	if(SiS_VBType & (VB_SIS301LV | VB_SIS302LV)) {
           if(SiS_VBInfo & SetCRT2ToTV) {
	      if(!(SiS_VBInfo & SetPALTV)) {
                 if((ModeNo == 0x4a) || (ModeNo == 0x38)) {
		    SiS_SetReg1(SiS_Part4Port,0x0a,0x57);
		    SiS_SetReg1(SiS_Part4Port,0x0b,0x46);
		    SiS_SetReg1(SiS_Part4Port,0x1f,0xf6);
                 }
              }
           }
	}
  } else {
   	SiS_SetReg1(SiS_Part4Port,0x0A,0x01);
   	tempah = SiS_VBVCLKData[vclkindex].Part4_B;
   	SiS_SetReg1(SiS_Part4Port,0x0B,tempah);
   	tempah = SiS_VBVCLKData[vclkindex].Part4_A;
   	SiS_SetReg1(SiS_Part4Port,0x0A,tempah);
  }
  SiS_SetReg1(SiS_Part4Port,0x12,0x00);
  tempah = 0x08;
  if(SiS_VBInfo & SetCRT2ToRAMDAC) {
    	tempah |= 0x020;
  }
  temp1 = SiS_GetReg1(SiS_Part4Port,0x12);
  tempah |= temp1;
  SiS_SetReg1(SiS_Part4Port,0x12,tempah);
}

/* TW: Double-checked against 650/LVDS (1.10.07), 630/301B/LVDS/LVDS+CH, 650/301LV BIOS */
USHORT
SiS_GetVCLK2Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
        USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempbx;
#ifdef SIS300
  USHORT LCDXlat1VCLK300[4] = {VCLK65,   VCLK65,   VCLK65,   VCLK65};
  USHORT LCDXlat2VCLK300[4] = {VCLK108_2,VCLK108_2,VCLK108_2,VCLK108_2};
  USHORT LVDSXlat2VCLK300[4]= {VCLK65,   VCLK65,   VCLK65,   VCLK65};
  USHORT LVDSXlat3VCLK300[4]= {VCLK65,   VCLK65,   VCLK65,   VCLK65};
#endif
#ifdef SIS315H
  USHORT LCDXlat1VCLK310[4] = {VCLK65+2,   VCLK65+2,   VCLK65+2,   VCLK65+2};
  USHORT LCDXlat2VCLK310[4] = {VCLK108_2+5,VCLK108_2+5,VCLK108_2+5,VCLK108_2+5};
  USHORT LVDSXlat2VCLK310[4]= {VCLK65+2,   VCLK65+2,   VCLK65+2,   VCLK65+2};
  USHORT LVDSXlat3VCLK310[4]= {VCLK108_2+5,VCLK108_2+5,VCLK108_2+5,VCLK108_2+5};
  			   /* {VCLK65+2,   VCLK65+2,   VCLK65+2,   VCLK65+2}; -  650/LVDS 1.10.07 */
#endif
  USHORT LCDXlat0VCLK[4]    = {VCLK40, VCLK40, VCLK40, VCLK40};
  USHORT LVDSXlat1VCLK[4]   = {VCLK40, VCLK40, VCLK40, VCLK40};
  USHORT CRT2Index,VCLKIndex=0;
  USHORT modeflag,resinfo;
  UCHAR *CHTVVCLKPtr=NULL;
  USHORT *LCDXlatVCLK1 = NULL;
  USHORT *LCDXlatVCLK2 = NULL;
  USHORT *LVDSXlatVCLK2 = NULL;
  USHORT *LVDSXlatVCLK3 = NULL;

#ifdef SIS315H
  if(HwDeviceExtension->jChipType >= SIS_315H) {
		LCDXlatVCLK1 = LCDXlat1VCLK310;
		LCDXlatVCLK2 = LCDXlat2VCLK310;
		LVDSXlatVCLK2 = LVDSXlat2VCLK310;
		LVDSXlatVCLK3 = LVDSXlat3VCLK310;
  } else {
#endif
#ifdef SIS300
		LCDXlatVCLK1 = LCDXlat1VCLK300;
		LCDXlatVCLK2 = LCDXlat2VCLK300;
		LVDSXlatVCLK2 = LVDSXlat2VCLK300;
		LVDSXlatVCLK3 = LVDSXlat3VCLK300;
#endif
#ifdef SIS315H
  }
#endif

  if(ModeNo<=0x13) {
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	resinfo = SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
    	CRT2Index = SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
    	CRT2Index = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  if(SiS_IF_DEF_LVDS==0) {    /* 301 */

     if (SiS_SetFlag & ProgrammingCRT2) {

        CRT2Index >>= 6;
        if(SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)){      /*  LCD */
            if(HwDeviceExtension->jChipType < SIS_315H) {
	       /* TW: Inserted from 630/301B BIOS */
	       if(SiS_LCDResInfo == Panel800x600)
	    		VCLKIndex = LCDXlat0VCLK[CRT2Index];
	       else if(SiS_LCDResInfo == Panel1024x768)
	    		VCLKIndex = LCDXlatVCLK1[CRT2Index];
	       else
	    		VCLKIndex = LCDXlatVCLK2[CRT2Index];
	    } else {
               /* TW: 650/301LV BIOS does not check expanding, 315 does  */
	       if( (HwDeviceExtension->jChipType > SIS_315PRO) ||
	           (!(SiS_LCDInfo & LCDNonExpanding)) ) {
      	          if(SiS_LCDResInfo == Panel1024x768){
		     VCLKIndex = LCDXlatVCLK1[CRT2Index];
                   } else if(SiS_LCDResInfo == Panel1280x960) {
		     VCLKIndex = 0x45;
		     if(resinfo == 0x09) VCLKIndex++;
	           } else {
		     VCLKIndex = LCDXlatVCLK2[CRT2Index];
      	           }
	       } else {
                   VCLKIndex = (UCHAR)SiS_GetReg2((USHORT)(SiS_P3ca+0x02));  /*  Port 3cch */
         	   VCLKIndex = ((VCLKIndex >> 2) & 0x03);
        	   if(ModeNo > 0x13) {
          		VCLKIndex = SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
        	   }
		   if(ModeNo <= 0x13) {  /* TW: Inserted from 315 BIOS */
		      if(SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC == 1) VCLKIndex = 0x42;
		   }
		   if(VCLKIndex == 0) VCLKIndex = 0x41;
		   if(VCLKIndex == 1) VCLKIndex = 0x43;
		   if(VCLKIndex == 4) VCLKIndex = 0x44;
	       }
	    }
        } else if(SiS_VBInfo & SetCRT2ToTV) {                 /*  TV */
        	if((SiS_IF_DEF_HiVision == 1) && (SiS_VBInfo & SetCRT2ToHiVisionTV)) {
          		if(SiS_SetFlag & RPLLDIV2XO)      VCLKIndex = HiTVVCLKDIV2;
     			else                              VCLKIndex = HiTVVCLK;
          		if(SiS_SetFlag & TVSimuMode) {
            			if(modeflag & Charx8Dot)  VCLKIndex = HiTVSimuVCLK;
            			else 			  VCLKIndex = HiTVTextVCLK;
          		}
        	} else {
       			if(SiS_SetFlag & RPLLDIV2XO)      VCLKIndex = TVVCLKDIV2;
            		else         		          VCLKIndex = TVVCLK;
          	}
		if(HwDeviceExtension->jChipType >= SIS_315H) {
              		VCLKIndex += 25;
  		}
        } else {         					/* RAMDAC2 */
        	VCLKIndex = (UCHAR)SiS_GetReg2((USHORT)(SiS_P3ca+0x02));
        	VCLKIndex = ((VCLKIndex >> 2) & 0x03);
        	if(ModeNo > 0x13) {
          		VCLKIndex = SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK; 
			if(HwDeviceExtension->jChipType < SIS_315H) {
          			VCLKIndex &= 0x3f;
				if( (HwDeviceExtension->jChipType == SIS_630) &&
				    (HwDeviceExtension->jChipRevision >= 0x30)) {
				     if(VCLKIndex == 0x14) VCLKIndex = 0x2e;
				}
			}
        	}
        }

    } else {   /* If not programming CRT2 */

        VCLKIndex = (UCHAR)SiS_GetReg2((USHORT)(SiS_P3ca+0x02));
        VCLKIndex = ((VCLKIndex >> 2) & 0x03);
        if(ModeNo > 0x13) {
             VCLKIndex = SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
	     if(HwDeviceExtension->jChipType < SIS_315H) {
                VCLKIndex &= 0x3f;
		if(HwDeviceExtension->jChipType != SIS_630) {
		   if(VCLKIndex == 0x1b) VCLKIndex = 0x35;
		}
	     }
        }
    }

  } else {       /*   LVDS  */

    	VCLKIndex = CRT2Index;

	if(SiS_SetFlag & ProgrammingCRT2) {  /* programming CRT2 */

	   if( (SiS_IF_DEF_CH70xx != 0) && (SiS_VBInfo & SetCRT2ToTV) ) {

		VCLKIndex &= 0x1f;
        	tempbx = 0;
        	if(SiS_VBInfo & SetPALTV) tempbx += 2;
        	if(SiS_VBInfo & SetCHTVOverScan) tempbx += 1;
       		switch(tempbx) {
          	   case 0: CHTVVCLKPtr = SiS_CHTVVCLKUNTSC;  break;
         	   case 1: CHTVVCLKPtr = SiS_CHTVVCLKONTSC;  break;
                   case 2: CHTVVCLKPtr = SiS_CHTVVCLKUPAL;   break;
                   case 3: CHTVVCLKPtr = SiS_CHTVVCLKOPAL;   break;
        	}
        	VCLKIndex = CHTVVCLKPtr[VCLKIndex];

	   } else if(SiS_VBInfo & SetCRT2ToLCD) {

	        VCLKIndex >>= 6;
     		if((SiS_LCDResInfo==Panel800x600) || (SiS_LCDResInfo==Panel320x480))
     			VCLKIndex = LVDSXlat1VCLK[VCLKIndex];
     		else if(SiS_LCDResInfo==Panel1024x768)
     			VCLKIndex = LVDSXlatVCLK2[VCLKIndex];
     		else 	VCLKIndex = LVDSXlatVCLK3[VCLKIndex];

	   } else {

	        VCLKIndex = (UCHAR)SiS_GetReg2((USHORT)(SiS_P3ca+0x02));
                VCLKIndex = ((VCLKIndex >> 2) & 0x03);
                if(ModeNo > 0x13) {
                     VCLKIndex = SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
		     if( (HwDeviceExtension->jChipType == SIS_630) &&
                         (HwDeviceExtension->jChipRevision >= 0x30) ) {
		         	if(VCLKIndex == 0x14) VCLKIndex = 0x2e;
		     }
	        }

	   }

	} else {  /* if not programming CRT2 */

	   VCLKIndex = (UCHAR)SiS_GetReg2((USHORT)(SiS_P3ca+0x02));
           VCLKIndex = ((VCLKIndex >> 2) & 0x03);
           if(ModeNo > 0x13) {
              VCLKIndex = SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
              if(HwDeviceExtension->jChipType < SIS_315H) {
	         if(HwDeviceExtension->jChipType != SIS_630) {
		        if(VCLKIndex == 0x1b) VCLKIndex = 0x35;
	         }
	      }
	   }

	}

  }

  if(HwDeviceExtension->jChipType < SIS_315H) {
    	VCLKIndex &= 0x3F;
  }
  return (VCLKIndex);
}

/* TW: Set 301 Palette address port registers */
/* TW: Checked against 650/301LV BIOS */
void
SiS_SetGroup5(USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
              USHORT ModeIdIndex)
{

  if((SiS_VBType & VB_SIS301BLV302BLV) && (SiS_VBInfo & SetCRT2ToLCDA))
      return;

  if(SiS_ModeType == ModeVGA){
    if(!(SiS_VBInfo & (SetInSlaveMode|LoadDACFlag))){
      SiS_EnableCRT2();
      SiS_LoadDAC2(ROMAddr,SiS_Part5Port,ModeNo,ModeIdIndex);
    }
  }
  return;
}

/* TW: Checked against 650/301LV BIOS */
void
SiS_LoadDAC2(UCHAR *ROMAddr,USHORT SiS_Part5Port,
             USHORT ModeNo,USHORT ModeIdIndex)
{
   USHORT data,data2;
   USHORT time,i,j,k;
   USHORT m,n,o;
   USHORT si,di,bx,dl;
   USHORT al,ah,dh;
   USHORT *table=0;
   USHORT Pindex,Pdata,modeflag;

/* if(SiS_SetFlag & SetDispDevSwitchFlag) return;  - TW: Not needed */

   if(ModeNo <= 0x13)
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;      /* si+St_ResInfo */
   else
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;     /* si+Ext_ResInfo */

#if 0
   if(!(ds:489 & 0x08)) {
#endif

	Pindex = SiS_Part5Port;
	Pdata = SiS_Part5Port + 1;
	data = modeflag & DACInfoFlag;
	time = 64;
	if(data == 0x00) table = SiS_MDA_DAC;
	if(data == 0x08) table = SiS_CGA_DAC;
	if(data == 0x10) table = SiS_EGA_DAC;
	if(data == 0x18) {
	   time = 256;
	   table = SiS_VGA_DAC;
	}

	if(time == 256) j = 16;
	else j = time;

	SiS_SetReg3(Pindex,0x00);

	for(i=0; i<j; i++) {
	   data = table[i];
	   for(k=0; k<3; k++) {
		data2 = 0;
		if(data & 0x01) data2 = 0x2A;
		if(data & 0x02) data2 += 0x15;
		data2 <<= 2;   			/* TW: New from 650/301LV BIOS */
		SiS_SetReg3(Pdata,data2);
		data >>= 2;
	   }
	}

	if(time == 256) {
	   for(i=16;i<32;i++) {
		data = table[i];
		data <<= 2;   			/* TW: New from 650/301LV BIOS */
		for(k=0; k<3; k++) SiS_SetReg3(Pdata,data);
	   }
           si = 32;
           for(m=0; m<9; m++) {
             di = si;
             bx = si + 0x04;
             dl = 0;
             for(n=0; n<3; n++) {
                for(o=0; o<5; o++) {
                  dh = table[si];
                  ah = table[di];
                  al = table[bx];
                  si++;
                  SiS_WriteDAC2(Pdata,dl,ah,al,dh);
                }         /* for 5 */
                si = si - 2;
                for(o=0; o<3; o++) {
                  dh = table[bx];
                  ah = table[di];
                  al = table[si];
                  si--;
                  SiS_WriteDAC2(Pdata,dl,ah,al,dh);
                }         /* for 3 */
                dl++;
             }            /* for 3 */
             si = si + 5;
           }               /* for 9 */
        }
#if 0
    } /* ds:489 & 0x08 */
#endif
}

/* TW: Checked against 650/301LV BIOS */
void
SiS_WriteDAC2(USHORT Pdata, USHORT dl, USHORT ah, USHORT al, USHORT dh)
{
  USHORT temp;
  USHORT bh,bl;

  bh = ah;
  bl = al;
  if(dl != 0) {
    temp = bh;
    bh = dh;
    dh = temp;
    if(dl == 1) {
       temp = bl;
       bl = dh;
       dh = temp;
    } else {
       temp = bl;
       bl = bh;
       bh = temp;
    }
  }
  dh <<= 2;   				/* TW: New from 650/301LV BIOS */
  bh <<= 2;   				/* TW: New from 650/301LV BIOS */
  bl <<= 2;   				/* TW: New from 650/301LV BIOS */
  SiS_SetReg3(Pdata,(USHORT)dh);
  SiS_SetReg3(Pdata,(USHORT)bh);
  SiS_SetReg3(Pdata,(USHORT)bl);
}

/* TW: Checked against 650/LVDS and 630/301B BIOS */
void
SiS_ModCRT1CRTC(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,tempah,i,modeflag,j;
  USHORT ResInfo,DisplayType;
  SiS_LVDSCRT1DataStruct *LVDSCRT1Ptr=NULL;

  if(ModeNo <= 0x13) {
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  temp = SiS_GetLVDSCRT1Ptr(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                            &ResInfo,&DisplayType);

  if(temp == 0) return;

  /* TW: Inserted from 630/LVDS BIOS */
  if(HwDeviceExtension->jChipType < SIS_315H) {
     if(SiS_SetFlag & CRT2IsVGA) return;
  }

  switch(DisplayType) {
    case 0 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_1;           break;
    case 1 : LVDSCRT1Ptr = SiS_LVDSCRT11024x768_1;          break;
    case 2 : LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_1;         break;
    case 3 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_1_H;         break;
    case 4 : LVDSCRT1Ptr = SiS_LVDSCRT11024x768_1_H;        break;
    case 5 : LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_1_H;       break;
    case 6 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_2;           break;
    case 7 : LVDSCRT1Ptr = SiS_LVDSCRT11024x768_2;          break;
    case 8 : LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_2;         break;
    case 9 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_2_H;         break;
    case 10: LVDSCRT1Ptr = SiS_LVDSCRT11024x768_2_H;        break;
    case 11: LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_2_H;       break;
    case 12: LVDSCRT1Ptr = SiS_LVDSCRT1XXXxXXX_1;           break;
    case 13: LVDSCRT1Ptr = SiS_LVDSCRT1XXXxXXX_1_H;         break;
    case 14: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_1;         break;
    case 15: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_1_H;       break;
    case 16: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_2;         break;
    case 17: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_2_H;       break;
    case 18: LVDSCRT1Ptr = SiS_CHTVCRT1UNTSC;               break;
    case 19: LVDSCRT1Ptr = SiS_CHTVCRT1ONTSC;               break;
    case 20: LVDSCRT1Ptr = SiS_CHTVCRT1UPAL;                break;
    case 21: LVDSCRT1Ptr = SiS_CHTVCRT1OPAL;                break;
    case 22: LVDSCRT1Ptr = SiS_LVDSCRT1320x480_1;           break; /* FSTN */
    case 23: LVDSCRT1Ptr = SiS_LVDSCRT11024x600_1;          break;
    case 24: LVDSCRT1Ptr = SiS_LVDSCRT11024x600_1_H;        break;
    case 25: LVDSCRT1Ptr = SiS_LVDSCRT11024x600_2;          break;
    case 26: LVDSCRT1Ptr = SiS_LVDSCRT11024x600_2_H;        break;
    case 27: LVDSCRT1Ptr = SiS_LVDSCRT11152x768_1;          break;
    case 28: LVDSCRT1Ptr = SiS_LVDSCRT11152x768_1_H;        break;
    case 29: LVDSCRT1Ptr = SiS_LVDSCRT11152x768_2;          break;
    case 30: LVDSCRT1Ptr = SiS_LVDSCRT11152x768_2_H;        break;
  }

  SiS_SetRegAND(SiS_P3d4,0x11,0x7f);                        /*unlock cr0-7  */

  tempah = (LVDSCRT1Ptr+ResInfo)->CR[0];
  SiS_SetReg1(SiS_P3d4,0x00,tempah);

  for(i=0x02,j=1;i<=0x05;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_P3d4,i,tempah);
  }
  for(i=0x06,j=5;i<=0x07;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_P3d4,i,tempah);
  }
  for(i=0x10,j=7;i<=0x11;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_P3d4,i,tempah);
  }
  for(i=0x15,j=9;i<=0x16;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_P3d4,i,tempah);
  }
  for(i=0x0A,j=11;i<=0x0C;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_P3c4,i,tempah);
  }

  tempah = (LVDSCRT1Ptr+ResInfo)->CR[14];
  tempah &= 0xE0;
  SiS_SetRegANDOR(SiS_P3c4,0x0E,0x1f,tempah);     	/* TW: Modfied (650/LVDS); Was SetReg(tempah) */

  tempah = (LVDSCRT1Ptr+ResInfo)->CR[14];
  tempah &= 0x01;
  tempah <<= 5;
  if(modeflag & DoubleScanMode){
    	tempah |= 0x080;
  }
  SiS_SetRegANDOR(SiS_P3d4,0x09,~0x020,tempah);

  /* TW: Inserted from 650/LVDS BIOS */
  if(SiS_VBInfo & SetCRT2ToTV) {
     if(modeflag & HalfDCLK)
        SiS_SetRegAND(SiS_P3d4,0x11,0x7f);
  }

  return;
}

#if 0 /* TW: Unused */
/*301b*/
void
SiS_CHACRT1CRTC(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                USHORT RefreshRateTableIndex)
{
  USHORT temp,tempah,i,modeflag,j;
  USHORT ResInfo,DisplayType;
  SiS_LVDSCRT1DataStruct *LVDSCRT1Ptr=NULL;

  if(ModeNo<=0x13) {
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;      /* si+St_ResInfo */
  } else {
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;     /* si+Ext_ResInfo */
  }

  temp=SiS_GetLVDSCRT1Ptr(ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                       &ResInfo,&DisplayType);
  if(temp==0){
    return;
  }

  switch(DisplayType) {
    case 0 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_1;           break;
    case 1 : LVDSCRT1Ptr = SiS_LVDSCRT11024x768_1;          break;
    case 2 : LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_1;         break;
    case 3 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_1_H;         break;
    case 4 : LVDSCRT1Ptr = SiS_LVDSCRT11024x768_1_H;        break;
    case 5 : LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_1_H;       break;
    case 6 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_2;           break;
    case 7 : LVDSCRT1Ptr = SiS_LVDSCRT11024x768_2;          break;
    case 8 : LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_2;         break;
    case 9 : LVDSCRT1Ptr = SiS_LVDSCRT1800x600_2_H;         break;
    case 10: LVDSCRT1Ptr = SiS_LVDSCRT11024x768_2_H;        break;
    case 11: LVDSCRT1Ptr = SiS_LVDSCRT11280x1024_2_H;       break;
    case 12: LVDSCRT1Ptr = SiS_LVDSCRT1XXXxXXX_1;           break;
    case 13: LVDSCRT1Ptr = SiS_LVDSCRT1XXXxXXX_1_H;         break;
    case 14: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_1;         break;
    case 15: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_1_H;       break;
    case 16: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_2;         break;
    case 17: LVDSCRT1Ptr = SiS_LVDSCRT11400x1050_2_H;       break;
    case 18: LVDSCRT1Ptr = SiS_CHTVCRT1UNTSC;               break;
    case 19: LVDSCRT1Ptr = SiS_CHTVCRT1ONTSC;               break;
    case 20: LVDSCRT1Ptr = SiS_CHTVCRT1UPAL;                break;
    case 21: LVDSCRT1Ptr = SiS_CHTVCRT1OPAL;                break;
    case 22: LVDSCRT1Ptr = SiS_LVDSCRT1320x480_1;           break; /* FSTN */
  }

  tempah=(UCHAR)SiS_GetReg1(SiS_P3d4,0x11);                        /*unlock cr0-7  */
  tempah=tempah&0x7F;
  SiS_SetReg1(SiS_P3d4,0x11,tempah);
  tempah = (LVDSCRT1Ptr+ResInfo)->CR[0];
  SiS_SetReg1(SiS_P3d4,0x0,tempah);
  for(i=0x02,j=1;i<=0x05;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_P3d4,i,tempah);
  }
  for(i=0x06,j=5;i<=0x07;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_P3d4,i,tempah);
  }
  for(i=0x10,j=7;i<=0x11;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_P3d4,i,tempah);
  }
  for(i=0x15,j=9;i<=0x16;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_P3d4,i,tempah);
  }

  for(i=0x0A,j=11;i<=0x0C;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_P3c4,i,tempah);
  }

  tempah = (LVDSCRT1Ptr+ResInfo)->CR[14];
  tempah=tempah&0x0E0;
  SiS_SetReg1(SiS_P3c4,0x0E,tempah);

  tempah = (LVDSCRT1Ptr+ResInfo)->CR[14];
  tempah=tempah&0x01;
  tempah=tempah<<5;
  if(modeflag&DoubleScanMode){
    	tempah=tempah|0x080;
  }
  SiS_SetRegANDOR(SiS_P3d4,0x09,~0x020,tempah);
  return;
}
/*add for LCDA*/
#endif

BOOLEAN
SiS_GetLCDACRT1Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		   USHORT RefreshRateTableIndex,USHORT *ResInfo,
		   USHORT *DisplayType)
 {
  USHORT tempbx=0,modeflag=0;
  USHORT CRT2CRTC=0;

  if(ModeNo<=0x13) {
  	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  	CRT2CRTC = SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
  	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  	CRT2CRTC = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  tempbx = SiS_LCDResInfo - PanelMinLVDS;

  if(SiS_LCDInfo & LCDNonExpanding) tempbx += 6;

  if(modeflag & HalfDCLK) tempbx += 3;

  *ResInfo = CRT2CRTC & 0x3F;
  *DisplayType = tempbx;

  return 1;
}

/* TW: Checked against 650/LVDS BIOS: modified for new panel resolutions */
BOOLEAN
SiS_GetLVDSCRT1Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		   USHORT RefreshRateTableIndex,USHORT *ResInfo,
		   USHORT *DisplayType)
 {
  USHORT tempbx,modeflag=0;
  USHORT Flag,CRT2CRTC;

  if(!(SiS_VBInfo & SetCRT2ToLCDA)) {                    /* TW: Inserted from 650/LVDS BIOS */
      if(!(SiS_VBInfo & SetInSlaveMode)) return 0;
  }							 /* TW: Inserted from 650/LVDS BIOS */

  if(ModeNo <= 0x13) {
    	modeflag = SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	CRT2CRTC = SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
    	modeflag = SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	CRT2CRTC = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  Flag = 1;
  tempbx = 0;
  if(SiS_IF_DEF_CH70xx != 0) {
    if(!(SiS_VBInfo & SetCRT2ToLCD)) {
      Flag = 0;
      tempbx = 18;
      if(SiS_VBInfo & SetPALTV) tempbx += 2;
      if(SiS_VBInfo & SetCHTVOverScan) tempbx++;
    }
  }
  if(Flag) {
    tempbx = SiS_LCDResInfo;
    tempbx -= PanelMinLVDS;
    if(SiS_LCDResInfo <= Panel1280x1024) {
       if(SiS_LCDInfo & LCDNonExpanding) tempbx += 6;
       if(modeflag & HalfDCLK) tempbx += 3;
    } else {
       if(SiS_LCDResInfo == Panel1400x1050) {
           tempbx = 14;
	   if(SiS_LCDInfo & LCDNonExpanding) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
       } else if(SiS_LCDInfo & 0x0100) {
           tempbx = 12;
	   if(modeflag & HalfDCLK) tempbx++;
       } else if(SiS_LCDResInfo == Panel1024x600) {
           tempbx = 23;
	   if(SiS_LCDInfo & LCDNonExpanding) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
       } else if(SiS_LCDResInfo == Panel1152x768) {
           tempbx = 27;
	   if(SiS_LCDInfo & LCDNonExpanding) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
       }
    }
  }
  if(SiS_IF_DEF_FSTN){
     if(SiS_LCDResInfo==Panel320x480){
       tempbx=22;
     }
  }
  *ResInfo = CRT2CRTC & 0x3F;
  *DisplayType = tempbx;
  return 1;
}

/* TW: Checked against 650/LVDS (1.10a, 1.10.07), 630/301B (I/II) and 630/LVDS BIOS */
void
SiS_SetCRT2ECLK(UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex,
           USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempah,tempal,pushax;
  USHORT vclkindex=0;

  if(HwDeviceExtension->jChipType < SIS_315H) {
     if(SiS_VBType & VB_SIS301BLV302BLV) {
        if(!(SiS_VBInfo & SetCRT2ToLCD)) return;
     }
  }

  if((SiS_LCDResInfo == Panel640x480) || (SiS_IF_DEF_TRUMPION == 1)) {
	SiS_SetFlag &= (~ProgrammingCRT2);
        tempal = SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
    	tempal &= 0x3F;
	if(tempal == 2) RefreshRateTableIndex--;
	vclkindex = SiS_GetVCLK2Ptr(ROMAddr,ModeNo,ModeIdIndex,
                               RefreshRateTableIndex,HwDeviceExtension);
	SiS_SetFlag |= ProgrammingCRT2;
  } else {
        vclkindex = SiS_GetVCLK2Ptr(ROMAddr,ModeNo,ModeIdIndex,
                               RefreshRateTableIndex,HwDeviceExtension);
  }

  tempal = 0x02B;
  if(!(SiS_VBInfo & SetCRT2ToLCDA)) {
     if(!(SiS_VBInfo & SetInSlaveMode)) {
    	tempal += 3;
     }
  }
  SiS_SetReg1(SiS_P3c4,0x05,0x86);
  pushax = tempal;
  SiS_SetReg1(SiS_P3c4,0x31,0x20);
  tempah = SiS_VCLKData[vclkindex].SR2B;
  SiS_SetReg1(SiS_P3c4,tempal,tempah);
  tempal++;
  tempah = SiS_VCLKData[vclkindex].SR2C;
  SiS_SetReg1(SiS_P3c4,tempal,tempah);
  SiS_SetReg1(SiS_P3c4,0x31,0x10);
  tempal = pushax;
  tempah = SiS_VCLKData[vclkindex].SR2B;
  SiS_SetReg1(SiS_P3c4,tempal,tempah);
  tempal++;
  tempah = SiS_VCLKData[vclkindex].SR2C;
  SiS_SetReg1(SiS_P3c4,tempal,tempah);
  SiS_SetReg1(SiS_P3c4,0x31,0x00);
  tempal = pushax;
  tempah = SiS_VCLKData[vclkindex].SR2B;
  SiS_SetReg1(SiS_P3c4,tempal,tempah);
  tempal++;
  tempah = SiS_VCLKData[vclkindex].SR2C;
  SiS_SetReg1(SiS_P3c4,tempal,tempah);
  return;
}

#if 0  /* TW: Not used */
void
SiS_SetDefCRT2ExtRegs(USHORT BaseAddr)
{
  USHORT  temp;

  if(SiS_IF_DEF_LVDS==0) {
    SiS_SetReg1(SiS_Part1Port,0x02,0x40);
    SiS_SetReg1(SiS_Part4Port,0x10,0x80);
    temp=(UCHAR)SiS_GetReg1(SiS_P3c4,0x16);
    temp &= 0xC3;
    SiS_SetReg1(SiS_P3d4,0x35,temp);
  } else {
    SiS_SetReg1(SiS_P3d4,0x32,0x02);
    SiS_SetReg1(SiS_Part1Port,0x02,0x00);
  }
}
#endif

/* TW: Start of Chrontel 70xx functions ---------------------- */

/* Set-up the Chrontel Registers */
void
SiS_SetCHTVReg(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
               USHORT RefreshRateTableIndex)
{
  USHORT temp,tempbx,tempcl;
  USHORT TVType,resindex;
  SiS_CHTVRegDataStruct *CHTVRegData=NULL;

  if(ModeNo<=0x13)
    	tempcl = SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
    	tempcl = SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  TVType = 0;
  if(SiS_VBInfo & SetPALTV) TVType += 2;
  if(SiS_VBInfo & SetCHTVOverScan) TVType += 1;
  switch(TVType) {
    	case 0: CHTVRegData = SiS_CHTVReg_UNTSC; break;
    	case 1: CHTVRegData = SiS_CHTVReg_ONTSC; break;
    	case 2: CHTVRegData = SiS_CHTVReg_UPAL;  break;
    	case 3: CHTVRegData = SiS_CHTVReg_OPAL;  break;
  }
  resindex = tempcl & 0x3F;

  if(SiS_IF_DEF_CH70xx == 1) {

     /* Chrontel 7005 */

     /* TW: We don't support modes >800x600 */
     if (resindex > 5) return;

     if(SiS_VBInfo & SetPALTV) {
    	SiS_SetCH700x(0x4304);  /* TW: 0x40=76uA (PAL); 0x03=15bit non-multi RGB*/
    	SiS_SetCH700x(0x6909);	/* TW: Black level for PAL (105)*/
     } else {
    	SiS_SetCH700x(0x0304);  /* TW: upper nibble=71uA (NTSC), 0x03=15bit non-multi RGB*/
    	SiS_SetCH700x(0x7109);	/* TW: Black level for NTSC (113)*/
     }

     temp = CHTVRegData[resindex].Reg[0];
     tempbx=((temp&0x00FF)<<8)|0x00;	/* TW: Mode register */
     SiS_SetCH700x(tempbx);
     temp = CHTVRegData[resindex].Reg[1];
     tempbx=((temp&0x00FF)<<8)|0x07;	/* TW: Start active video register */
     SiS_SetCH700x(tempbx);
     temp = CHTVRegData[resindex].Reg[2];
     tempbx=((temp&0x00FF)<<8)|0x08;	/* TW: Position overflow register */
     SiS_SetCH700x(tempbx);
     temp = CHTVRegData[resindex].Reg[3];
     tempbx=((temp&0x00FF)<<8)|0x0A;	/* TW: Horiz Position register */
     SiS_SetCH700x(tempbx);
     temp = CHTVRegData[resindex].Reg[4];
     tempbx=((temp&0x00FF)<<8)|0x0B;	/* TW: Vertical Position register */
     SiS_SetCH700x(tempbx);

     /* TW: Set minimum flicker filter for Luma channel (SR1-0=00),
                minimum text enhancement (S3-2=10),
   	        maximum flicker filter for Chroma channel (S5-4=10)
	        =00101000=0x28 (When reading, S1-0->S3-2, and S3-2->S1-0!)
      */
     SiS_SetCH700x(0x2801);

     /* TW: Set video bandwidth
            High bandwith Luma composite video filter(S0=1)
            low bandwith Luma S-video filter (S2-1=00)
	    disable peak filter in S-video channel (S3=0)
	    high bandwidth Chroma Filter (S5-4=11)
	    =00110001=0x31
     */
     SiS_SetCH700x(0xb103);       /* old: 3103 */

     /* TW: Register 0x3D does not exist in non-macrovision register map
            (Maybe this is a macrovision register?)
      */
     /* SiS_SetCH70xx(0x003D); */

     /* TW: Register 0x10 only contains 1 writable bit (S0) for sensing,
            all other bits a read-only. Macrovision?
      */
     SiS_SetCH70xxANDOR(0x0010,0x1F);

     /* TW: Register 0x11 only contains 3 writable bits (S0-S2) for
            contrast enhancement (set to 010 -> gain 2 Yout = 9/8*(Yin-57) )
      */
     SiS_SetCH70xxANDOR(0x0211,0xF8);

     /* TW: Clear DSEN
      */
     SiS_SetCH70xxANDOR(0x001C,0xEF);

     if(!(SiS_VBInfo&SetPALTV)) {		/* ---- NTSC ---- */
       tempcl=tempcl&0x3F;
       if(SiS_VBInfo&SetCHTVOverScan) {
         if(tempcl==0x04) {   			/* 640x480 overscan: Mode 16 */
      	   SiS_SetCH70xxANDOR(0x0020,0xEF);   	/* loop filter off */
           SiS_SetCH70xxANDOR(0x0121,0xFE);       /* ACIV on, no need to set FSCI */
         } else {
           if(tempcl==0x05) {    			/* 800x600 overscan: Mode 23 */
             SiS_SetCH70xxANDOR(0x0118,0xF0);	/* 0x18-0x1f: FSCI 469,762,048 */
             SiS_SetCH70xxANDOR(0x0C19,0xF0);
             SiS_SetCH70xxANDOR(0x001A,0xF0);
             SiS_SetCH70xxANDOR(0x001B,0xF0);
             SiS_SetCH70xxANDOR(0x001C,0xF0);
             SiS_SetCH70xxANDOR(0x001D,0xF0);
             SiS_SetCH70xxANDOR(0x001E,0xF0);
             SiS_SetCH70xxANDOR(0x001F,0xF0);
             SiS_SetCH70xxANDOR(0x1020,0xEF);     /* Loop filter on for mode 23 */
             SiS_SetCH70xxANDOR(0x0021,0xFE);     /* ACIV off, need to set FSCI */
           }
         }
       } else {
         if(tempcl==0x04) {     			/* ----- 640x480 underscan; Mode 17 */
           SiS_SetCH70xxANDOR(0x0020,0xEF); 	/* loop filter off */
           SiS_SetCH70xxANDOR(0x0121,0xFE);
         } else {
           if(tempcl==0x05) {   			/* ----- 800x600 underscan: Mode 24 */
             SiS_SetCH70xxANDOR(0x0118,0xF0);   /* (FSCI was 0x1f1c71c7 - this is for mode 22) */
             SiS_SetCH70xxANDOR(0x0919,0xF0);	/* FSCI for mode 24 is 428,554,851 */
             SiS_SetCH70xxANDOR(0x081A,0xF0);
             SiS_SetCH70xxANDOR(0x0b1B,0xF0);
             SiS_SetCH70xxANDOR(0x031C,0xF0);
             SiS_SetCH70xxANDOR(0x0a1D,0xF0);
             SiS_SetCH70xxANDOR(0x061E,0xF0);
             SiS_SetCH70xxANDOR(0x031F,0xF0);
             SiS_SetCH70xxANDOR(0x0020,0xEF);   /* loop filter off for mode 24 */
             SiS_SetCH70xxANDOR(0x0021,0xFE);	/* ACIV off, need to set FSCI */
           }
         }
       }
     } else {				/* ---- PAL ---- */
           /* TW: We don't play around with FSCI in PAL mode */
         if (tempcl==0x04) {
           SiS_SetCH70xxANDOR(0x0020,0xEF); 	/* loop filter off */
           SiS_SetCH70xxANDOR(0x0121,0xFE);     /* ACIV on */
         } else {
           SiS_SetCH70xxANDOR(0x0020,0xEF); 	/* loop filter off */
           SiS_SetCH70xxANDOR(0x0121,0xFE);     /* ACIV on */
         }
     }

  } else {

     /* Chrontel 7019 */

     /* TW: We don't support modes >1024x768 */
     if (resindex > 6) return;

     temp = CHTVRegData[resindex].Reg[0];
     tempbx=((temp & 0x00FF) <<8 ) | 0x00;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[1];
     tempbx=((temp & 0x00FF) <<8 ) | 0x01;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[2];
     tempbx=((temp & 0x00FF) <<8 ) | 0x02;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[3];
     tempbx=((temp & 0x00FF) <<8 ) | 0x04;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[4];
     tempbx=((temp & 0x00FF) <<8 ) | 0x03;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[5];
     tempbx=((temp & 0x00FF) <<8 ) | 0x05;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[6];
     tempbx=((temp & 0x00FF) <<8 ) | 0x06;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[7];
     tempbx=((temp & 0x00FF) <<8 ) | 0x07;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[8];
     tempbx=((temp & 0x00FF) <<8 ) | 0x08;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[9];
     tempbx=((temp & 0x00FF) <<8 ) | 0x15;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[10];
     tempbx=((temp & 0x00FF) <<8 ) | 0x1f;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[11];
     tempbx=((temp & 0x00FF) <<8 ) | 0x0c;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[12];
     tempbx=((temp & 0x00FF) <<8 ) | 0x0d;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[13];
     tempbx=((temp & 0x00FF) <<8 ) | 0x0e;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[14];
     tempbx=((temp & 0x00FF) <<8 ) | 0x0f;
     SiS_SetCH701x(tempbx);

     temp = CHTVRegData[resindex].Reg[15];
     tempbx=((temp & 0x00FF) <<8 ) | 0x10;
     SiS_SetCH701x(tempbx);

#if 0  /* TW: Not done in BIOS 1.10.07 */
     SiS_SetCH701x(0x3848);
     SiS_DDC2Delay(SiS_I2CDELAYSHORT * 2);
     SiS_SetCH701x(0x1848);
#endif
  }
}

void
SiS_SetCHTVForLCD(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr)
{
  UCHAR regtable[]  = { 0x1c, 0x5f, 0x64, 0x6f, 0x70, 0x71,
                        0x72, 0x73, 0x74, 0x76, 0x78, 0x7d };
  UCHAR table28b4[] = { 0x60, 0x02, 0x00, 0x07, 0x40, 0xed,
                        0xa3, 0xc8, 0xc7, 0xac, 0x60, 0x02 };
  UCHAR table28c0[] = { 0x60, 0x03, 0x11, 0x00, 0x40, 0xef,
                        0xad, 0xdb, 0xf6, 0xac, 0x60, 0x02 };
  UCHAR *tableptr = NULL;
  USHORT tempbh;
  int i;

  if(SiS_LCDResInfo == Panel1400x1050) {
      tableptr = table28c0;
  } else {
      tableptr = table28b4;
  }
  tempbh = SiS_GetCH701x(0x74);
  if((tempbh == 0xf6) || (tempbh == 0xc7)) {
     tempbh = SiS_GetCH701x(0x73);
     if(tempbh == 0xc8) {
        if(SiS_LCDResInfo != Panel1400x1050) return;
     } else if(tempbh == 0xdb) {
        if(SiS_LCDResInfo == Panel1400x1050) return;
     }
  }
  for(i=0; i<0x0c; i++) {
     SiS_SetCH701x((tableptr[i] << 8) | regtable[i]);
  }
  SiS_Chrontel19f2();
  tempbh = SiS_GetCH701x(0x1e);			/* TW: NEW in BIOS 1.10.07 */
  tempbh |= 0xc0;				/* TW: NEW in BIOS 1.10.07 */
  SiS_SetCH701x((tempbh << 8) | 0x1e);		/* TW: NEW in BIOS 1.10.07 */
}

/* TW: Chrontel 701x functions ================================= */

void
SiS_Chrontel19f2(void)
{
  UCHAR regtable[]  = { 0x67, 0x68, 0x69, 0x6a, 0x6b };
  UCHAR table19e8[] = { 0x01, 0x02, 0x01, 0x01, 0x02 };
  UCHAR table19ed[] = { 0x01, 0x02, 0x01, 0x01, 0x02 };
  UCHAR *tableptr = NULL;
  int i;

  if(SiS_LCDResInfo == Panel1400x1050) {
      tableptr = table19ed;
  } else {
      tableptr = table19e8;
  }

  for(i=0; i<5; i++) {
     SiS_SetCH701x((tableptr[i] << 8) | regtable[i]);
  }
}

void
SiS_Chrontel701xOn()
{
  USHORT temp;

  if(SiS_IF_DEF_CH70xx == 2) {
        temp = SiS_GetCH701x(0x66);
        temp |= 0x20;
	SiS_SetCH701x((temp << 8) | 0x66);
  }
}

void
SiS_Chrontel701xOn2(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr)
{
  USHORT temp;

  if(SiS_IF_DEF_CH70xx == 2) {
     if(SiS_IsYPbPr(HwDeviceExtension, BaseAddr)) {
        temp = SiS_GetCH701x(0x01);
	temp &= 0x3f;
	temp |= 0x80;
	SiS_SetCH701x((temp << 8) | 0x01);
     }
     SiS_SetCH701x(0x2049);
     temp = SiS_GetCH701x(0x49);
     if(SiS_IsYPbPr(HwDeviceExtension, BaseAddr)) {
        temp = SiS_GetCH701x(0x73);
	temp |= 0x60;
	SiS_SetCH701x((temp << 8) | 0x73);
     }
     /* TW: New from BIOS 1.10.07: */
     temp = SiS_GetCH701x(0x47);
     temp &= 0x7f;
     SiS_SetCH701x((temp << 8) | 0x47);
     SiS_LongDelay(2);
     temp = SiS_GetCH701x(0x47);
     temp |= 0x80;
     SiS_SetCH701x((temp << 8) | 0x47);
  }
}

void
SiS_Chrontel701xOff()
{
  USHORT temp;

  if(SiS_IF_DEF_CH70xx == 2) {
        temp = SiS_GetCH701x(0x66);
        temp &= 0xDF;
	SiS_SetCH701x((temp << 8) | 0x66);
  }
}

void
SiS_Chrontel701xOff2()
{
  USHORT temp;

  if(SiS_IF_DEF_CH70xx == 2) {
        SiS_LongDelay(2);
	temp = SiS_GetCH701x(0x76);
	temp &= 0xfc;
	SiS_SetCH701x((temp << 8) | 0x76);
	SiS_SetCH701x(0x0066);
  }
}

void
SiS_ChrontelFlip0x48()
{
     SiS_SetCH701x(0x1048);
     SiS_LongDelay(1);
     SiS_SetCH701x(0x1848);
}

void
SiS_ChrontelDoSomething4(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
     USHORT temp;

     SiS_SetCH701x(0xaf76);
     temp = SiS_GetCH701x(0x49);
     temp &= 1;
     if(temp != 1) {
	temp = SiS_GetCH701x(0x47);
	temp &= 0x70;
	SiS_SetCH701x((temp << 8) | 0x47);
	SiS_LongDelay(3);
	temp = SiS_GetCH701x(0x47);
	temp |= 0x80;
	SiS_SetCH701x((temp << 8) | 0x47);
     }
}

void
SiS_ChrontelDoSomething3(USHORT ModeNo,PSIS_HW_DEVICE_INFO HwDeviceExtension,
                         USHORT BaseAddr)
{
     USHORT temp,temp1;

     temp1 = 0;
     temp = SiS_GetCH701x(0x61);
     if(temp < 2) {
          temp++;
	  SiS_SetCH701x((temp << 8) | 0x61);
	  temp1 = 1;
     }
     SiS_SetCH701x(0xac76);
     temp = SiS_GetCH701x(0x66);
     temp |= 0x5f;
     SiS_SetCH701x((temp << 8) | 0x66);
     if(ModeNo > 0x13) {
         if(SiS_WeHaveBacklightCtrl(HwDeviceExtension, BaseAddr)) {
	    SiS_GenericDelay(0x3ff);
	 } else {
	    SiS_GenericDelay(0x2ff);
	 }
     } else {
         if(!temp1)
	    SiS_GenericDelay(0x2ff);
     }
     temp = SiS_GetCH701x(0x76);
     temp |= 0x03;
     SiS_SetCH701x((temp << 8) | 0x76);
     temp = SiS_GetCH701x(0x66);
     temp &= 0x7f;
     SiS_SetCH701x((temp << 8) | 0x66);
     SiS_LongDelay(1);
}

void
SiS_ChrontelDoSomething2(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
     USHORT temp,tempcl,tempch;

     SiS_LongDelay(1);
     tempcl = 3;
     tempch = 0;

     do {
       temp = SiS_GetCH701x(0x66);
       temp &= 0x04;
       if(temp == 0x04) break;

       SiS_SetCHTVForLCD(HwDeviceExtension, BaseAddr);

       if(tempcl == 0) {
           if(tempch == 3) break;
	   SiS_ChrontelFlip0x48();
	   tempcl = 3;
	   tempch++;
       }
       tempcl--;
       temp = SiS_GetCH701x(0x76);
       temp &= 0xfb;
       SiS_SetCH701x((temp << 8) | 0x76);
       SiS_LongDelay(2);
       temp = SiS_GetCH701x(0x76);
       temp |= 0x04;
       SiS_SetCH701x((temp << 8) | 0x76);
       SiS_SetCH701x(0x6078);
       SiS_LongDelay(2);
    } while(0);

    SiS_SetCH701x(0x0077);
}

void
SiS_ChrontelDoSomething1(PSIS_HW_DEVICE_INFO HwDeviceExtension,
                         USHORT BaseAddr)
{
     USHORT temp;

     temp = SiS_GetCH701x(0x03);
     temp |= 0x80;
     temp &= 0xbf;
     SiS_SetCH701x((temp << 8) | 0x03);

     SiS_ChrontelFlip0x48();

     SiS_ChrontelDoSomething2(HwDeviceExtension, BaseAddr);
     
     temp = SiS_GetReg1(SiS_P3d4,0x34);
     SiS_ChrontelDoSomething3(temp, HwDeviceExtension, BaseAddr);

     SiS_SetCH701x(0xaf76);
}

/* TW: End of Chrontel 701x functions ==================================== */

/* TW: Generic Read/write routines for Chrontel ========================== */

/* The Chrontel seems to be connected to the 630/730 via
 * the 630/730's DDC port (which is used as a I2C port here).
 *
 * On 630(S)T chipset, the port changed from 0x11 to 0x0a
 */

void
SiS_SetCH70xx(USHORT tempbx)
{
   if (SiS_IF_DEF_CH70xx == 1)
      SiS_SetCH700x(tempbx);
   else
      SiS_SetCH701x(tempbx);
}

/* TW: Write to Chrontel 700x */
/* Parameter is [Data (S15-S8) | Register no (S7-S0)] */
void
SiS_SetCH700x(USHORT tempbx)
{
  USHORT tempah,temp,i;

  if(!(SiS_ChrontelInit)) {
     SiS_DDC_Index = 0x11;		   /* TW: Bit 0 = SC;  Bit 1 = SD */
     SiS_DDC_Data  = 0x02;                 /* Bitmask in IndexReg for Data */
     SiS_DDC_Clk   = 0x01;                 /* Bitmask in IndexReg for Clk */
     SiS_DDC_DataShift = 0x00;
     SiS_DDC_DeviceAddr = 0xEA;  	   /* TW: DAB (Device Address Byte) */
  }

  for(i=0;i<10;i++) {	/* TW: Do only 10 attempts to write */
    SiS_SetSwitchDDC2();
    if (SiS_SetStart()) continue;	/* TW: Set start condition */
    tempah=SiS_DDC_DeviceAddr;
    temp=SiS_WriteDDC2Data(tempah);	/* TW: Write DAB (S0=0=write) */
    if(temp) continue;			/* TW:    (ERROR: no ack) */
    tempah=tempbx&0x00FF;
    temp=SiS_WriteDDC2Data(tempah);	/* TW: Write RAB */
    if(temp) continue;			/* TW:    (ERROR: no ack) */
    tempah=(tempbx&0xFF00)>>8;
    temp=SiS_WriteDDC2Data(tempah);	/* TW: Write data */
    if(temp) continue;			/* TW:    (ERROR: no ack) */
    if (SiS_SetStop()) continue;	/* TW: Set stop condition */
    SiS_ChrontelInit = 1;
    return;
  }

  if(!(SiS_ChrontelInit)) {
     SiS_DDC_Index = 0x0a;			/* TW: Bit 0 = SC;  Bit 1 = SD */
     SiS_DDC_Data  = 0x80;                 /* Bitmask in IndexReg for Data */
     SiS_DDC_Clk   = 0x40;                 /* Bitmask in IndexReg for Clk */
     SiS_DDC_DataShift = 0x00;
     SiS_DDC_DeviceAddr = 0xEA;  		/* TW: DAB (Device Address Byte) */

     for(i=0;i<10;i++) {	/* TW: Do only 10 attempts to write */
       SiS_SetSwitchDDC2();
       if (SiS_SetStart()) continue;		/* TW: Set start condition */
       tempah=SiS_DDC_DeviceAddr;
       temp=SiS_WriteDDC2Data(tempah);		/* TW: Write DAB (S0=0=write) */
       if(temp) continue;			/* TW:    (ERROR: no ack) */
       tempah=tempbx&0x00FF;
       temp=SiS_WriteDDC2Data(tempah);		/* TW: Write RAB */
       if(temp) continue;			/* TW:    (ERROR: no ack) */
       tempah=(tempbx&0xFF00)>>8;
       temp=SiS_WriteDDC2Data(tempah);		/* TW: Write data */
       if(temp) continue;			/* TW:    (ERROR: no ack) */
       if (SiS_SetStop()) continue;		/* TW: Set stop condition */
       SiS_ChrontelInit = 1;
       return;
    }
  }
}

/* TW: Write to Chrontel 701x */
/* Parameter is [Data (S15-S8) | Register no (S7-S0)] */
void
SiS_SetCH701x(USHORT tempbx)
{
  USHORT tempah,temp,i;

  /* TW: Toggle to DDC port */
  SiS_SetRegOR(SiS_P3c4,0x38,0x20);

  SiS_DDC_Index = 0x11;			/* TW: Bit 0 = SC;  Bit 1 = SD */
  SiS_DDC_Data  = 0x08;                 /* Bitmask in IndexReg for Data */
  SiS_DDC_Clk   = 0x04;                 /* Bitmask in IndexReg for Clk */
  SiS_DDC_DataShift = 0x00;
  SiS_DDC_DeviceAddr = 0xEA;  		/* TW: DAB (Device Address Byte) */

  for(i=0;i<10;i++) {	/* TW: Do only 10 attempts to write */
    if (SiS_SetStart()) continue;	/* TW: Set start condition */
    tempah=SiS_DDC_DeviceAddr;
    temp=SiS_WriteDDC2Data(tempah);	/* TW: Write DAB (S0=0=write) */
    if(temp) continue;			/* TW:    (ERROR: no ack) */
    tempah=tempbx&0x00FF;
    temp=SiS_WriteDDC2Data(tempah);	/* TW: Write RAB */
    if(temp) continue;			/* TW:    (ERROR: no ack) */
    tempah=(tempbx&0xFF00)>>8;
    temp=SiS_WriteDDC2Data(tempah);	/* TW: Write data */
    if(temp) continue;			/* TW:    (ERROR: no ack) */
    if (SiS_SetStop()) continue;	/* TW: Set stop condition */
    return;
  }
}

/* TW: Read from Chrontel 70xx */
/* Parameter is [Register no (S7-S0)] */
USHORT
SiS_GetCH70xx(USHORT tempbx)
{
   if (SiS_IF_DEF_CH70xx == 1)
      return(SiS_GetCH700x(tempbx));
   else
      return(SiS_GetCH701x(tempbx));
}

/* TW: Read from Chrontel 700x */
/* Parameter is [Register no (S7-S0)] */
USHORT
SiS_GetCH700x(USHORT tempbx)
{
  USHORT tempah,temp,i;

  if(!(SiS_ChrontelInit)) {
     SiS_DDC_Index = 0x11;		/* TW: Bit 0 = SC;  Bit 1 = SD */
     SiS_DDC_Data  = 0x02;              /* Bitmask in IndexReg for Data */
     SiS_DDC_Clk   = 0x01;              /* Bitmask in IndexReg for Clk */
     SiS_DDC_DataShift = 0x00;
     SiS_DDC_DeviceAddr = 0xEA;		/* TW: DAB */
  }

  SiS_DDC_ReadAddr = tempbx;

  for(i=0;i<20;i++) {	/* TW: Do only 20 attempts to read */
    SiS_SetSwitchDDC2();
    if(SiS_SetStart()) continue;	/* TW: Set start condition */
    tempah = SiS_DDC_DeviceAddr;
    temp = SiS_WriteDDC2Data(tempah);	/* TW: Write DAB (S0=0=write) */
    if(temp) continue;			/* TW:        (ERROR: no ack) */
    tempah = SiS_DDC_ReadAddr;		/* TW: Write RAB */
    temp = SiS_WriteDDC2Data(tempah);
    if(temp) continue;			/* TW:        (ERROR: no ack) */
    if (SiS_SetStart()) continue;	/* TW: Re-start */
    tempah = SiS_DDC_DeviceAddr | 0x01; /* DAB | 0x01 = Read */
    temp = SiS_WriteDDC2Data(tempah);	/* TW: DAB (S0=1=read) */
    if(temp) continue;			/* TW:        (ERROR: no ack) */
    tempah = SiS_ReadDDC2Data(tempah);	/* TW: Read byte */
    if (SiS_SetStop()) continue;	/* TW: Stop condition */
    SiS_ChrontelInit = 1;
    return(tempah);
  }

  if(!SiS_ChrontelInit) {
     SiS_DDC_Index = 0x0a;		/* TW: Bit 0 = SC;  Bit 1 = SD */
     SiS_DDC_Data  = 0x80;              /* Bitmask in IndexReg for Data */
     SiS_DDC_Clk   = 0x40;              /* Bitmask in IndexReg for Clk */
     SiS_DDC_DataShift = 0x00;
     SiS_DDC_DeviceAddr = 0xEA;  	/* TW: DAB (Device Address Byte) */

     for(i=0;i<20;i++) {	/* TW: Do only 20 attempts to read */
       SiS_SetSwitchDDC2();
       if(SiS_SetStart()) continue;		/* TW: Set start condition */
       tempah = SiS_DDC_DeviceAddr;
       temp = SiS_WriteDDC2Data(tempah);	/* TW: Write DAB (S0=0=write) */
       if(temp) continue;			/* TW:        (ERROR: no ack) */
       tempah = SiS_DDC_ReadAddr;		/* TW: Write RAB */
       temp = SiS_WriteDDC2Data(tempah);
       if(temp) continue;			/* TW:        (ERROR: no ack) */
       if (SiS_SetStart()) continue;		/* TW: Re-start */
       tempah = SiS_DDC_DeviceAddr | 0x01; 	/* DAB | 0x01 = Read */
       temp = SiS_WriteDDC2Data(tempah);	/* TW: DAB (S0=1=read) */
       if(temp) continue;			/* TW:        (ERROR: no ack) */
       tempah = SiS_ReadDDC2Data(tempah);	/* TW: Read byte */
       if (SiS_SetStop()) continue;		/* TW: Stop condition */
       SiS_ChrontelInit = 1;
       return(tempah);
     }
  }
  return(0xFFFF);
}

/* TW: Read from Chrontel 701x */
/* Parameter is [Register no (S7-S0)] */
USHORT
SiS_GetCH701x(USHORT tempbx)
{
  USHORT tempah,temp,i;

  /* TW: Toggle to DDC port */
  SiS_SetRegOR(SiS_P3c4,0x38,0x20);

  SiS_DDC_Index = 0x11;			/* TW: Bit 0 = SC;  Bit 1 = SD */
  SiS_DDC_Data  = 0x08;                 /* Bitmask in IndexReg for Data */
  SiS_DDC_Clk   = 0x04;                 /* Bitmask in IndexReg for Clk */
  SiS_DDC_DataShift = 0x00;
  SiS_DDC_DeviceAddr = 0xEA;		/* TW: DAB */
  SiS_DDC_ReadAddr = tempbx;

   for(i=0;i<20;i++) {	/* TW: Do only 20 attempts to read */
    if(SiS_SetStart()) continue;	/* TW: Set start condition */
    tempah = SiS_DDC_DeviceAddr;
    temp = SiS_WriteDDC2Data(tempah);	/* TW: Write DAB (S0=0=write) */
    if(temp) continue;			/* TW:        (ERROR: no ack) */
    tempah = SiS_DDC_ReadAddr;		/* TW: Write RAB */
    temp = SiS_WriteDDC2Data(tempah);
    if(temp) continue;			/* TW:        (ERROR: no ack) */
    if (SiS_SetStart()) continue;	/* TW: Re-start */
    tempah = SiS_DDC_DeviceAddr | 0x01; /* DAB | 0x01 = Read */
    temp = SiS_WriteDDC2Data(tempah);	/* TW: DAB (S0=1=read) */
    if(temp) continue;			/* TW:        (ERROR: no ack) */
    tempah = SiS_ReadDDC2Data(tempah);	/* TW: Read byte */
    SiS_SetStop();			/* TW: Stop condition */
    return(tempah);
   }
  return 0xFFFF;
}

void
SiS_SetCH70xxANDOR(USHORT tempax,USHORT tempbh)
{
  USHORT tempal,tempah,tempbl;

  tempal = tempax & 0x00FF;
  tempah =(tempax >> 8) & 0x00FF;
  tempbl = SiS_GetCH70xx(tempal);
  tempbl = (((tempbl & tempbh) | tempah) << 8 | tempal);
  SiS_SetCH70xx(tempbl);
}

/* TW: Generic I2C functions for Chrontel --------- */

/* I2C functions CHECKED FOR TV BUG */
void
SiS_SetSwitchDDC2(void)
{
  SiS_SetSCLKHigh();
  SiS_DDC2Delay(SiS_I2CDELAY);

  SiS_SetSCLKLow();
  SiS_DDC2Delay(SiS_I2CDELAY);
}

/* TW: Set I2C start condition */
/* TW: This is done by a SD high-to-low transition while SC is high */
USHORT
SiS_SetStart(void)
{
  if (SiS_SetSCLKLow()) return 0xFFFF;			                   /* TW: (SC->low)  */
  SiS_SetRegANDOR(SiS_DDC_Port,SiS_DDC_Index,~SiS_DDC_Data,SiS_DDC_Data);  /* TW: SD->high */
  if (SiS_SetSCLKHigh()) return 0xFFFF;			                   /* TW: SC->high */
  SiS_SetRegANDOR(SiS_DDC_Port,SiS_DDC_Index,~SiS_DDC_Data,0x00);          /* TW: SD->low = start condition */
  if (SiS_SetSCLKHigh()) return 0xFFFF;			                   /* TW: (SC->low) */
  return 0;
}

/* TW: Set I2C stop condition */
/* TW: This is done by a SD low-to-high transition while SC is high */
USHORT
SiS_SetStop(void)
{
  if (SiS_SetSCLKLow()) return 0xFFFF;			                   /* TW: (SC->low) */
  SiS_SetRegANDOR(SiS_DDC_Port,SiS_DDC_Index,~SiS_DDC_Data,0x00);          /* TW: SD->low   */
  if (SiS_SetSCLKHigh()) return 0xFFFF;			                   /* TW: SC->high  */
  SiS_SetRegANDOR(SiS_DDC_Port,SiS_DDC_Index,~SiS_DDC_Data,SiS_DDC_Data);  /* TW: SD->high = stop condition */
  if (SiS_SetSCLKHigh()) return 0xFFFF;			                   /* TW: (SC->high) */
  return 0;
}

/* TW: Write 8 bits of data */
USHORT
SiS_WriteDDC2Data(USHORT tempax)
{
  USHORT i,flag,temp;

  flag=0x80;
  for(i=0;i<8;i++) {
    SiS_SetSCLKLow();					                      /* TW: SC->low */
    if(tempax & flag) {
      SiS_SetRegANDOR(SiS_DDC_Port,SiS_DDC_Index,~SiS_DDC_Data,SiS_DDC_Data); /* TW: Write bit (1) to SD */
    } else {
      SiS_SetRegANDOR(SiS_DDC_Port,SiS_DDC_Index,~SiS_DDC_Data,0x00);         /* TW: Write bit (0) to SD */
    }
    SiS_SetSCLKHigh();					                      /* TW: SC->high */
    flag >>= 1;
  }
  temp=SiS_CheckACK();					                      /* TW: Check acknowledge */
  return(temp);
}

USHORT
SiS_ReadDDC2Data(USHORT tempax)
{
  USHORT i,temp,getdata;

  getdata=0;
  for(i=0; i<8; i++) {
    getdata <<= 1;
    SiS_SetSCLKLow();
    SiS_SetRegANDOR(SiS_DDC_Port,SiS_DDC_Index,~SiS_DDC_Data,SiS_DDC_Data);
    SiS_SetSCLKHigh();
    temp = SiS_GetReg1(SiS_DDC_Port,SiS_DDC_Index);
    if(temp & SiS_DDC_Data) getdata |= 0x01;
  }
  return(getdata);
}

USHORT
SiS_SetSCLKLow(void)
{
  USHORT temp, watchdog=50000;

  SiS_SetRegANDOR(SiS_DDC_Port,SiS_DDC_Index,~SiS_DDC_Clk,0x00);      /* SetSCLKLow()  */
  do {
    temp = SiS_GetReg1(SiS_DDC_Port,SiS_DDC_Index);
  } while((temp & SiS_DDC_Clk) && --watchdog);
  if (!watchdog) return 0xFFFF;
  SiS_DDC2Delay(SiS_I2CDELAYSHORT);
  return 0;
}

USHORT
SiS_SetSCLKHigh(void)
{
  USHORT temp,watchdog=50000;

  SiS_SetRegANDOR(SiS_DDC_Port,SiS_DDC_Index,~SiS_DDC_Clk,SiS_DDC_Clk);      /* SetSCLKHigh()  */
  do {
    temp = SiS_GetReg1(SiS_DDC_Port,SiS_DDC_Index);
  } while((!(temp & SiS_DDC_Clk)) && --watchdog);
  if (!watchdog) return 0xFFFF;
  SiS_DDC2Delay(SiS_I2CDELAYSHORT);
  return 0;
}

void
SiS_DDC2Delay(USHORT delaytime)
{
  USHORT i;

  for(i=0; i<delaytime; i++) {
    SiS_GetReg1(SiS_P3c4,0x05);
  }
}

/* TW: Check I2C acknowledge */
/* Returns 0 if ack ok, non-0 if ack not ok */
USHORT
SiS_CheckACK(void)
{
  USHORT tempah;

  SiS_SetSCLKLow();					                   /* TW: (SC->low) */
  SiS_SetRegANDOR(SiS_DDC_Port,SiS_DDC_Index,~SiS_DDC_Data,SiS_DDC_Data);  /* TW: (SD->high) */
  SiS_SetSCLKHigh();					                   /* TW: SC->high = clock impulse for ack */
  tempah = SiS_GetReg1(SiS_DDC_Port,SiS_DDC_Index);	                   /* TW: Read SD */
  SiS_SetSCLKLow();					                   /* TW: SC->low = end of clock impulse */
  if(tempah & SiS_DDC_Data) return(1);			                   /* TW: Ack OK if bit = 0 */
  else return(0);
}

/* TW: End of I2C functions ----------------------- */


/* =============== SiS 310 O.E.M. ================= */

#ifdef SIS315H

/*
---------------------------------------------------------
   LCDResInfo 1 : 800x600          TW: Table wrong for LVDS!
              2 : 1024x768
              3 : 1280x1024
              4 : 1280x960         TW: 1400x1050
              5 : 640x480          TW: 1600x1200
              6 : 1600x1200
              7 : 1920x1440
   VESA
   non-VESA
   non-Expanding
---------------------------------------------------------
*/
USHORT
GetLCDPtrIndex (void)
{
   USHORT index;

   if(SiS_IF_DEF_LVDS == 1) {   /* TW: Inserted entire if statement */

     index = SiS_LCDResInfo & 0x0F;
     if(SiS_LCDResInfo == Panel1400x1050)  index -= 5;
     if(SiS_LCDResInfo == Panel1600x1200)  index -= 6;
     index--;
     index *= 3;
     if(SiS_LCDInfo & LCDNonExpanding) index += 2;
     else if(!(SiS_SetFlag & LCDVESATiming)) index++;

   } else {

     index = (SiS_LCDResInfo & 0x0F) - 1;
     index *= 3;
     if (SiS_LCDInfo & LCDNonExpanding)
        index += 2;
     else if (!(SiS_SetFlag & LCDVESATiming))
         index++;

   }

  return index;
 }


/*
---------------------------------------------------------
       GetTVPtrIndex()
          return       0 : NTSC Enhanced/Standard
                       1 : NTSC Standard TVSimuMode
                       2 : PAL Enhanced/Standard
                       3 : PAL Standard TVSimuMode
                       4 : HiVision Enhanced/Standard
                       5 : HiVision Standard TVSimuMode
---------------------------------------------------------
*/
USHORT
GetTVPtrIndex(void)
{
  USHORT index;

  index = 0;
  if (SiS_VBInfo & SetPALTV)
    index++;
  if (SiS_VBInfo & SetCRT2ToHiVisionTV)  /* Hivision TV use PAL */
    index++;

  index <<= 1;

  if((SiS_VBInfo & SetInSlaveMode) && (SiS_SetFlag & TVSimuMode))
    index++;

  return index;
}

/* TW: Checked against 650/LVDS (1.10.07) and 650/301LV BIOS (including data) */
void
SetDelayComp(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
             UCHAR *ROMAddr,USHORT ModeNo)
{
  USHORT Part1Port;
  USHORT delay,index;

  if (SiS_VBInfo & SetCRT2ToRAMDAC) {
     delay = SiS310_CRT2DelayCompensation1;
     if (SiS_VBType & (VB_SIS301B | VB_SIS302B))
       delay = SiS310_CRT2DelayCompensation2;
     if(SiS_IF_DEF_LVDS == 1)
       delay = SiS310_CRT2DelayCompensation3;
  } else if (SiS_VBInfo & SetCRT2ToLCD) {
       index = GetLCDPtrIndex();
       delay = SiS310_LCDDelayCompensation1[index];
       if (SiS_VBType & (VB_SIS301B|VB_SIS302B|VB_SIS301LV))
         delay = SiS310_LCDDelayCompensation2[index];
       if(SiS_IF_DEF_LVDS == 1)
         delay = SiS310_LCDDelayCompensation3[index];
  } else {
       index = GetTVPtrIndex();
       delay = SiS310_TVDelayCompensation1[index];
       if (SiS_VBType & (VB_SIS301B | VB_SIS302B))
         delay = SiS310_TVDelayCompensation2[index];
       if(SiS_IF_DEF_LVDS == 1)
         delay = SiS310_TVDelayCompensation3[index];
  }
  Part1Port=BaseAddr+SIS_CRT2_PORT_04;
  if(SiS_IF_DEF_LVDS == 1) {
    if(SiS_VBInfo & SetCRT2ToTV) {
       SiS_SetRegANDOR(Part1Port,0x2D,0xF0,delay);
    } else {
       delay <<= 4;
       SiS_SetRegANDOR(Part1Port,0x2D,0x0F,delay);
    }
  } else {
     SiS_SetRegANDOR(Part1Port,0x2D,0xF0,delay);  /* index 2D D[3:0] */
  }
}

/* TW: Checked against 650/301LV BIOS (including data) */
void
SetAntiFlicker(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
               UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT Part2Port;
  USHORT index,temp;

  Part2Port=BaseAddr+SIS_CRT2_PORT_10;

  temp = GetTVPtrIndex();
  temp >>= 1;  	  /* 0: NTSC, 1 :PAL, 2:HiTV */

  if (ModeNo<=0x13)
    index = SiS_SModeIDTable[ModeIdIndex].VB_StTVFlickerIndex;
  else
    index = SiS_EModeIDTable[ModeIdIndex].VB_ExtTVFlickerIndex;

  temp = SiS310_TVAntiFlick1[temp][index];
  temp  <<= 4;

  SiS_SetRegANDOR(Part2Port,0x0A,0x8f,temp);  /* index 0A D[6:4] */
}

/* TW: Checked against 650/301LV BIOS (including data) */
void
SetEdgeEnhance(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
               UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT Part2Port;
  USHORT index,temp;

  Part2Port = BaseAddr + SIS_CRT2_PORT_10;

  temp = GetTVPtrIndex();
  temp >>= 1;              	/* 0: NTSC, 1 :PAL, 2:HiTV */

  if (ModeNo<=0x13)
    index = SiS_SModeIDTable[ModeIdIndex].VB_StTVEdgeIndex;
  else
    index = SiS_EModeIDTable[ModeIdIndex].VB_ExtTVEdgeIndex;

  temp = SiS310_TVEdge1[temp][index];
  temp <<= 5;
  SiS_SetRegANDOR(Part2Port,0x3A,0x1F,temp);  /* index 0A D[7:5] */
}

/* TW: Checked against 650/301LV BIOS */
void
SetYFilter(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
           UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT Part2Port;
  USHORT index,temp,i,j;
  UCHAR  OutputSelect=*pSiS_OutputSelect;

  Part2Port = BaseAddr + SIS_CRT2_PORT_10;

  temp = GetTVPtrIndex();
  temp >>= 1;  			/* 0: NTSC, 1 :PAL, 2:HiTV */

  if (ModeNo<=0x13) {
    index =  SiS_SModeIDTable[ModeIdIndex].VB_StTVYFilterIndex;
  } else {
    index =  SiS_EModeIDTable[ModeIdIndex].VB_ExtTVYFilterIndex;
  }

  if (SiS_VBInfo&SetCRT2ToHiVisionTV)  /* Hivision TV uses PAL */
    temp = 0;

  if(SiS_VBType & VB_SIS301BLV302BLV) {
    for(i=0x35, j=0; i<=0x38; i++, j++) {
       SiS_SetReg1(Part2Port,i,SiS310_TVYFilter2[temp][index][j]);
    }
    for(i=0x48; i<=0x4A; i++, j++) {
       SiS_SetReg1(Part2Port,i,SiS310_TVYFilter2[temp][index][j]);
    }
  } else {
    for(i=0x35, j=0; i<=0x38; i++, j++){
       SiS_SetReg1(Part2Port,i,SiS310_TVYFilter1[temp][index][j]);
    }
  }

  if(OutputSelect & EnablePALMN) {
      if(SiS_GetReg1(SiS_P3d4,0x31) & 0x01) {
         temp = SiS_GetReg1(SiS_P3d4,0x38);
         temp &= (EnablePALMN | EnablePALN);
         if(temp == EnablePALMN) {
              if(SiS_VBType & VB_SIS301BLV302BLV) {
                 for(i=0x35, j=0; i<=0x38; i++, j++){
                      SiS_SetReg1(Part2Port,i,SiS310_PALMFilter2[index][j]);
                 }
                 for(i=0x48; i<=0x4A; i++, j++) {
                       SiS_SetReg1(Part2Port,i,SiS310_PALMFilter2[index][j]);
                 }
              } else {
                 for(i=0x35, j=0; i<=0x38; i++, j++) {
                       SiS_SetReg1(Part2Port,i,SiS310_PALMFilter[index][j]);
                 }
              }
         }
         if(temp == EnablePALN) {
              if(SiS_VBType & VB_SIS301BLV302BLV) {
                 for(i=0x35, j=0; i<=0x38; i++, j++) {
                      SiS_SetReg1(Part2Port,i,SiS310_PALNFilter2[index][j]);
                 }
                 for(i=0x48, j=0; i<=0x4A; i++, j++) {
                       SiS_SetReg1(Part2Port,i,SiS310_PALNFilter2[index][j]);
                 }
             } else {
                 for(i=0x35, j=0; i<=0x38; i++, j++)
                       SiS_SetReg1(Part2Port,i,SiS310_PALNFilter[index][j]);
             }
         }
      }
  }
}

/* TW: Checked against 650/301LV BIOS (including data) */
void
SetPhaseIncr(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
             UCHAR *ROMAddr,USHORT ModeNo)
{
  USHORT Part2Port;
  USHORT index,temp,temp1,i,j;

  if(!(SiS_VBInfo & SetCRT2ToTV)) return;

  temp1 = SiS_GetReg1(SiS_P3d4,0x38);        /* if PALM/N not set */
  temp1 &=  (EnablePALMN | EnablePALN);
  if(temp1) return;

  Part2Port=BaseAddr + SIS_CRT2_PORT_10;

  temp = GetTVPtrIndex();
  /* 0: NTSC Graphics, 1: NTSC Text,    2:PAL Graphics,
   * 3: PAL Text,      4: HiTV Graphics 5:HiTV Text
   */
  index = temp % 2;
  temp >>= 1;          /* 0:NTSC, 1:PAL, 2:HiTV */

  for(j=0, i=0x31; i<=0x34; i++, j++) {
     if(!(SiS_VBType & VB_SIS301BLV302BLV))
	  SiS_SetReg1(Part2Port,i,SiS310_TVPhaseIncr1[temp][index][j]);
     else if((!(SiS_VBInfo & SetInSlaveMode)) || (SiS_SetFlag & TVSimuMode))
          SiS_SetReg1(Part2Port,i,SiS310_TVPhaseIncr2[temp][index][j]);
     else
          SiS_SetReg1(Part2Port,i,SiS310_TVPhaseIncr1[temp][index][j]);
  }
  if(SiS_VBType & (VB_SIS301LV | VB_SIS302LV)) {
     if(!(SiS_VBInfo & SetPALTV)) {
        if((ModeNo == 0x38) || (ModeNo == 0x4a)) {
	    SiS_SetReg1(SiS_Part2Port,0x31,0x1e);
	    SiS_SetReg1(SiS_Part2Port,0x32,0x8c);
	    SiS_SetReg1(SiS_Part2Port,0x33,0x5c);
	    SiS_SetReg1(SiS_Part2Port,0x34,0x7a);
	}
     }
  }
}

void
SiS_OEM310Setting(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                  UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
   SetDelayComp(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo);
   /* TW: The TV funtions are not for LVDS */
   if( (SiS_IF_DEF_LVDS == 0) && (SiS_VBInfo & SetCRT2ToTV) ) {
       SetAntiFlicker(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       SetPhaseIncr(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo);
       SetYFilter(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       if(!(SiS_VBType & VB_SIS301BLV302BLV)) {
          SetEdgeEnhance(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       }
   }
}

/* TW: New and checked from 650/301LV BIOS */
void
SiS_OEMLCD(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                  UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
   USHORT tempbx,tempah,tempbl,tempbh,tempcl;

   if(SiS_IF_DEF_LVDS == 1) return;

   if(SiS_VBInfo & SetCRT2ToLCDA) {
      SiS_UnLockCRT2(HwDeviceExtension,BaseAddr);
      tempbh = SiS_GetReg1(SiS_Part1Port,0x1a);
      tempbh &= 0x38;
      tempbh >>= 3;
      tempbl = SiS_GetReg1(SiS_Part1Port,0x18);
      tempbx = (tempbh << 8) | tempbl;
      if(SiS_LCDTypeInfo == 1)  tempbx -= 0x12;
      SiS_SetReg1(SiS_Part1Port,0x18,tempbx & 0x00ff);
      tempah = (tempbx & 0xff00) >> 8;
      tempah &= 0x07;
      tempah <<= 3;
      SiS_SetRegANDOR(SiS_Part1Port,0x1a,0xc7,tempah);
      tempah = SiS_GetReg1(SiS_Part1Port,0x19);
      tempah &= 0x0f;
      if(SiS_LCDTypeInfo == 1)  tempah -= 2;
      tempah &= 0x0f;
      SiS_SetRegANDOR(SiS_Part1Port,0x19,0xF0,tempah);
      tempah = SiS_GetReg1(SiS_Part1Port,0x14);
      if(SiS_LCDTypeInfo == 1)  tempah++;
      tempah -= 8;
      SiS_SetReg1(SiS_Part1Port,0x14,tempah);
   } else if(SiS_VBInfo & SetCRT2ToLCD) {
      tempcl = tempbh = SiS_GetReg1(SiS_Part2Port,0x01);
      tempbh &= 0x70;
      tempbh >>= 4;
      tempbl = SiS_GetReg1(SiS_Part2Port,0x04);
      tempbx = (tempbh << 8) | tempbl;
      if(SiS_LCDTypeInfo == 1)  {
           tempbx -= 0x1e;
	   tempcl &= 0x0f;
	   tempcl -= 4;
	   tempcl &= 0x0f;
      }
      tempbl = tempbx & 0x00ff;
      tempbh = (tempbx >> 8) & 0x00ff;
      SiS_SetReg1(SiS_Part2Port,0x04,tempbl);
      tempbh <<= 4;
      tempbh |= tempcl;
      SiS_SetRegANDOR(SiS_Part2Port,0x01,0x80,tempbh);
   }
}
#endif


/*  =================  SiS 300 O.E.M. ================== */

#ifdef SIS300

#if 0   /* Not used */
USHORT
GetRevisionID(PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
   ULONG temp1;
#ifndef LINUX_XF86
   ULONG base;
#endif
   USHORT temp2 = 0;

   if((HwDeviceExtension->jChipType==SIS_540)||
      (HwDeviceExtension->jChipType==SIS_630)||
      (HwDeviceExtension->jChipType==SIS_730)) {
#ifndef LINUX_XF86
     	base = 0x80000008;
     	OutPortLong(base,0xcf8);
     	temp1 = InPortLong(0xcfc);
#else
	temp1=pciReadLong(0x00000000, 0x08);
#endif
     	temp1 &= 0x000000FF;
     	temp2 = (USHORT)(temp1);
    	return temp2;
   }
   return 0;
}
#endif

/* TW: Checked against 630/301B BIOS (incl data) */
USHORT
GetOEMLCDPtr(PSIS_HW_DEVICE_INFO HwDeviceExtension, int Flag)
{
  USHORT tempbx=0;
  UCHAR customtable[] = {
  	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
  };

  if(Flag) {
      if(customtable[SiS_LCDTypeInfo] == 0xFF) return 0xFFFF;
  }
  if(SiS_IF_DEF_LVDS == 0) {
        tempbx = SiS_LCDTypeInfo << 2;
	if(SiS_VBInfo & SetInSlaveMode) tempbx += 2;
	if(SiS_LCDInfo & LCDNonExpanding) tempbx++;
  } else {
  	tempbx = SiS_LCDTypeInfo;
	if(SiS_LCDInfo & LCDNonExpanding) tempbx += 16;
  }
  return tempbx;
}

/* TW: Checked against 630/301B and 630/LVDS BIOS (incl data) */
void
SetOEMLCDDelay(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
               UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT Part1Port;
  USHORT index,temp;

  /* TW: The Panel Compensation Delay should be set according to tables
   *     here. Unfortunately, the different BIOS versions don't case about
   *     a uniform way using eg. ROM byte 0x220, but use different
   *     hard coded delays (0x04, 0x20, 0x18) in SetGroup1(). So we can't
   *     rely on the other OEM bits in 0x237, 0x238 here either.
   */
#if 0
  if(ROMAddr) {
     if(!(ROMAddr[0x237] & 0x01)) return;
     if(!(ROMAddr[0x237] & 0x02)) return;
  }
#endif
  /* TW: We just check if a non-standard delay has been set; if not,
   * we use our tables. Otherwise don't do anything here.
   */
  if(ROMAddr) {
     if(ROMAddr[0x220] & 0x80) return;
  }
  /* TW: We don't need to set this if the user select a custom pdc */
  if(HwDeviceExtension->pdc) return;

  Part1Port = BaseAddr + SIS_CRT2_PORT_04;

  temp = GetOEMLCDPtr(HwDeviceExtension, 0);

  index = SiS_VBModeIDTable[ModeIdIndex].VB_LCDDelayIndex;

  if (SiS_IF_DEF_LVDS == 0) {
    	temp = SiS300_OEMLCDDelay2[temp][index];
  } else {
        temp = SiS300_OEMLCDDelay3[temp][index];
  }
  temp &= 0x3c;
  SiS_SetRegANDOR(Part1Port,0x13,~0x3C,temp);  /* index 0A D[6:4] */
}

/* TW: Checked against 630/301B and 630/LVDS BIOS */
USHORT
GetOEMTVPtr(void)
{
  USHORT index;

  index = 0;
  if(!(SiS_VBInfo & SetInSlaveMode)) index += 4;
  if(SiS_IF_DEF_LVDS == 0) {
     if(SiS_VBInfo & SetCRT2ToSCART) index += 2;
     else if (SiS_VBInfo & SetCRT2ToHiVisionTV) index += 3;
     else if(SiS_VBInfo & SetPALTV)  index += 1;
  } else {
     if(SiS_VBInfo & SetCHTVOverScan) index += 2;
     if(SiS_VBInfo & SetPALTV)  index += 1;
  }
  return index;
}

/* TW: Checked against 630/301B and 630/LVDS BIOS (incl data) */
void
SetOEMTVDelay(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
              UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT Part1Port;
  USHORT index,temp;

#if 0
  if(ROMAddr) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x02)) return;
  }
#endif

  Part1Port = BaseAddr + SIS_CRT2_PORT_04;

  temp = GetOEMTVPtr();

  index = SiS_VBModeIDTable[ModeIdIndex].VB_TVDelayIndex;

  if(SiS_IF_DEF_LVDS == 0) {
     temp = SiS300_OEMTVDelay301[temp][index];
  } else {
     temp = SiS300_OEMTVDelayLVDS[temp][index];
  }
  temp &= 0x3c;
  SiS_SetRegANDOR(Part1Port,0x13,~0x3C,temp);  /* index 0A D[6:4] */
}

/* TW: Checked against 630/301B BIOS (incl data) */
void
SetOEMAntiFlicker(PSIS_HW_DEVICE_INFO HwDeviceExtension,
                  USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
		  USHORT ModeIdIndex)
{
  USHORT Part2Port;
  USHORT index,temp;

  Part2Port = BaseAddr + SIS_CRT2_PORT_10;

  temp = GetOEMTVPtr();

  index = SiS_VBModeIDTable[ModeIdIndex].VB_TVFlickerIndex;

  temp = SiS300_OEMTVFlicker[temp][index];
  temp &= 0x70;
  SiS_SetRegANDOR(Part2Port,0x0A,0x8F,temp);  /* index 0A D[6:4] */
}

/* TW: Checked against 630/301B BIOS (incl data) */
void
SetOEMPhaseIncr(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT Part2Port;
  USHORT index,i,j,temp;

  if(SiS_VBInfo & SetCRT2ToHiVisionTV) return;

  Part2Port = BaseAddr + SIS_CRT2_PORT_10;

  temp = GetOEMTVPtr();

  index = SiS_VBModeIDTable[ModeIdIndex].VB_TVPhaseIndex;

  if(SiS_VBType & VB_SIS301BLV302BLV) {
       for(i=0x31, j=0; i<=0x34; i++, j++) {
          SiS_SetReg1(Part2Port,i,SiS300_Phase2[temp][index][j]);
       }
  } else {
       for(i=0x31, j=0; i<=0x34; i++, j++) {
          SiS_SetReg1(Part2Port,i,SiS300_Phase1[temp][index][j]);
       }
  }
}

/* TW: Checked against 630/301B BIOS (incl data) */
void
SetOEMYFilter(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
              UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT Part2Port;
  USHORT index,temp,temp1,i,j;

  if(SiS_VBInfo & (SetCRT2ToSCART | SetCRT2ToHiVisionTV)) return;

  Part2Port = BaseAddr + SIS_CRT2_PORT_10;

  temp = GetOEMTVPtr();

  index = SiS_VBModeIDTable[ModeIdIndex].VB_TVYFilterIndex;

  if(SiS_GetReg1(SiS_P3d4,0x31) & 0x01) {
       temp1 = SiS_GetReg1(SiS_P3d4,0x35);
       if(temp1 & (EnablePALMN | EnablePALN)) {
          temp = 16;
	  if(temp1 & EnablePALN) temp = 18;
       }
  }
  if(SiS_VBType & VB_SIS301BLV302BLV) {
      for(i=0x35, j=0; i<=0x38; i++, j++) {
       	SiS_SetReg1(Part2Port,i,SiS300_Filter2[temp][index][j]);
      }
      for(i=0x48; i<=0x4A; i++, j++) {
     	SiS_SetReg1(Part2Port,i,SiS300_Filter2[temp][index][j]);
      }
  } else {
      for(i=0x35, j=0; i<=0x38; i++, j++) {
       	SiS_SetReg1(Part2Port,i,SiS300_Filter1[temp][index][j]);
      }
  }
}

void
SiS_OEM300Setting(PSIS_HW_DEVICE_INFO HwDeviceExtension,
		  USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo)
{
  USHORT ModeIdIndex;

  ModeIdIndex = SiS_SearchVBModeID(ROMAddr,&ModeNo);
  if(!(ModeIdIndex)) return;

  if (SiS_VBInfo & SetCRT2ToLCD) {
       SetOEMLCDDelay(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
  }
  if (SiS_VBInfo & SetCRT2ToTV) {
       SetOEMTVDelay(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       if(SiS_IF_DEF_LVDS==0) {
       		SetOEMAntiFlicker(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
    		SetOEMPhaseIncr(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       		SetOEMYFilter(HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       }
  }
}
#endif


