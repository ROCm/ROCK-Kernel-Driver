/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/sis/init301.c,v 1.3 2002/22/04 01:16:16 dawes Exp $ */
/*
 * Mode switching code (CRT2 section) for SiS 300/540/630/730/315/550/650/740/330
 * (Universal module for Linux kernel framebuffer, XFree86 4.x)
 *
 * Assembler-To-C translation
 * Copyright 2002, 2003 by Thomas Winischhofer <thomas@winischhofer.net>
 * Minor parts Copyright SiS, Inc.
 *
 * Based on BIOS
 *     1.10.07, 1.10a for 650/CH7019
 *     1.11.21a for 740/CH7019
 *     1.11.05 for 650/LVDS (w/o Chrontel)
 *     1.07.1b, 1.10.6s, 1.11.6w, 1.11.7w, 1.11.8r for 650/301(B/LV), 650/302LV
 *     2.04.50 (I) and 2.04.5c (II) for 630/301(B)
 *     2.02.3b, 2.03.02, 2.04.2c, 2.04.5c, 2.07a and 2.08.b3 for 630/LVDS/LVDS+CH7005
 *     2.04.5c, 2.04.6c for 730+LVDS+CH7005
 *     1.09b for 315/301(B)
 *     1.16.51 for 300+301LV (ECS A907)
 *     1.01.03 for 330 (Xabre 400)
 *
 * Known bugs:
 *   1024x768 panel, expanding (CR37=1): Mode 640x480 does not work on SOME panels
 *       therefore, we always do the scaling ourselves for now.
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
 * TW says: This code looks awful, I know. But please don't do anything about
 * this otherwise debugging will be hell.
 * The code is extremely fragile as regards the different chipsets, different
 * video bridges and combinations thereof. If anything is changed, extreme
 * care has to be taken that that change doesn't break it for other chipsets,
 * bridges or combinations thereof.
 * All comments in this file are by me, regardless if they are marked TW or not.
 *
 */
 
#if 1 
#define NEWCH701x
#endif

#include "init301.h"

#if 0
#define TWNEWPANEL
#endif

#ifdef SIS300
#include "oem300.h"
#endif

#ifdef SIS315H
#include "oem310.h"
#endif

#define SiS_I2CDELAY      1000
#define SiS_I2CDELAYSHORT  150

BOOLEAN
SiS_SetCRT2Group301(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                    PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
   USHORT ModeIdIndex;
   USHORT RefreshRateTableIndex;

   SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;

   if(!SiS_Pr->UseCustomMode) {
      SiS_SearchModeID(SiS_Pr,ROMAddr,&ModeNo,&ModeIdIndex);
   } else {
      ModeIdIndex = 0;
   }      

   /* TW: Used for shifting CR33 */
   SiS_Pr->SiS_SelectCRT2Rate = 4;

   SiS_UnLockCRT2(SiS_Pr, HwDeviceExtension, BaseAddr);

   RefreshRateTableIndex = SiS_GetRatePtrCRT2(SiS_Pr, ROMAddr,ModeNo,ModeIdIndex,HwDeviceExtension);

   SiS_SaveCRT2Info(SiS_Pr,ModeNo);

   if(SiS_LowModeStuff(SiS_Pr,ModeNo,HwDeviceExtension)) {
      SiS_DisableBridge(SiS_Pr,HwDeviceExtension,BaseAddr);
      if((SiS_Pr->SiS_IF_DEF_LVDS == 1) && (HwDeviceExtension->jChipType == SIS_730)) {
         SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x00,0x80);
      }
      SiS_SetCRT2ModeRegs(SiS_Pr,BaseAddr,ModeNo,ModeIdIndex,HwDeviceExtension);
   }

   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
      SiS_LockCRT2(SiS_Pr,HwDeviceExtension, BaseAddr);
      SiS_DisplayOn(SiS_Pr);
      return(TRUE);
   }

   if(SiS_Pr->UseCustomMode) return(FALSE);
   
   SiS_GetCRT2Data(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                   HwDeviceExtension);

   /* Set up Panel Link for LVDS, 301BDH and 650/30xLV(for LCDA) */
   if( (SiS_Pr->SiS_IF_DEF_LVDS == 1) ||
       ((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) ||
       ((HwDeviceExtension->jChipType >= SIS_315H) && (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) ) {
   	SiS_GetLVDSDesData(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
	                   HwDeviceExtension);
   } else {
        SiS_Pr->SiS_LCDHDES = SiS_Pr->SiS_LCDVDES = 0;
   }

#ifdef LINUX_XF86
#ifdef TWDEBUG
  xf86DrvMsg(0, X_INFO, "(init301: LCDHDES 0x%03x LCDVDES 0x%03x)\n", SiS_Pr->SiS_LCDHDES, SiS_Pr->SiS_LCDVDES);
  xf86DrvMsg(0, X_INFO, "(init301: HDE     0x%03x VDE     0x%03x)\n", SiS_Pr->SiS_HDE, SiS_Pr->SiS_VDE);
  xf86DrvMsg(0, X_INFO, "(init301: VGAHDE  0x%03x VGAVDE  0x%03x)\n", SiS_Pr->SiS_VGAHDE, SiS_Pr->SiS_VGAVDE);
  xf86DrvMsg(0, X_INFO, "(init301: HT      0x%03x VT      0x%03x)\n", SiS_Pr->SiS_HT, SiS_Pr->SiS_VT);
  xf86DrvMsg(0, X_INFO, "(init301: VGAHT   0x%03x VGAVT   0x%03x)\n", SiS_Pr->SiS_VGAHT, SiS_Pr->SiS_VGAVT);
#endif
#endif

   if(SiS_LowModeStuff(SiS_Pr,ModeNo,HwDeviceExtension)) {
      SiS_SetGroup1(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
                    HwDeviceExtension,RefreshRateTableIndex);
   }

   if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {

        if(SiS_LowModeStuff(SiS_Pr,ModeNo,HwDeviceExtension)) {

	   SiS_SetGroup2(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                 RefreshRateTableIndex,HwDeviceExtension);
      	   SiS_SetGroup3(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                 HwDeviceExtension);
      	   SiS_SetGroup4(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                 RefreshRateTableIndex,HwDeviceExtension);
      	   SiS_SetGroup5(SiS_Pr,HwDeviceExtension, BaseAddr,ROMAddr,
	                 ModeNo,ModeIdIndex);

	   /* TW: For 301BDH (Panel link initialization): */
	   if((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
	      if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {	 
		 if(!((SiS_Pr->SiS_SetFlag & SetDOSMode) && ((ModeNo == 0x03) || (ModeNo = 0x10)))) {
		    if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
		       SiS_ModCRT1CRTC(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,
		                       RefreshRateTableIndex,HwDeviceExtension);
		    }
                 }
	      }
	      SiS_SetCRT2ECLK(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,
		              RefreshRateTableIndex,HwDeviceExtension);
	   }
        }

   } else {

        if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
	   if(SiS_Pr->SiS_IF_DEF_TRUMPION == 0) {
    	      SiS_ModCRT1CRTC(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,
	                      RefreshRateTableIndex,HwDeviceExtension);
	   }
	}
        if(SiS_Pr->SiS_IF_DEF_FSTN == 0) {
     	   SiS_SetCRT2ECLK(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,
	 	           RefreshRateTableIndex,HwDeviceExtension);
	}
	if(SiS_LowModeStuff(SiS_Pr,ModeNo,HwDeviceExtension)) {
     	   if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	      if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
	         if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
#ifdef SIS315H		 
		    SiS_SetCH701xForLCD(SiS_Pr,HwDeviceExtension,BaseAddr);
#endif		    
		 }
	      }
	      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
       		 SiS_SetCHTVReg(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,
		               RefreshRateTableIndex);
	      }
     	   }
	}

   }

#ifdef SIS300
   if ( (HwDeviceExtension->jChipType == SIS_540) ||
        (HwDeviceExtension->jChipType == SIS_630) ||
        (HwDeviceExtension->jChipType == SIS_730) ||
        (HwDeviceExtension->jChipType == SIS_300) )
    {
	if(SiS_LowModeStuff(SiS_Pr,ModeNo,HwDeviceExtension)) {
	   if(SiS_Pr->SiS_UseOEM) {
	      if((SiS_Pr->SiS_UseROM) && ROMAddr && (SiS_Pr->SiS_UseOEM == -1)) {
	         if((ROMAddr[0x233] == 0x12) && (ROMAddr[0x234] == 0x34)) {
	            SiS_OEM300Setting(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo);
	         }
	      } else {
       	         SiS_OEM300Setting(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo);
	      }
	   }
	}
    }
#endif

#ifdef SIS315H
   if ( (HwDeviceExtension->jChipType == SIS_315H)  ||
        (HwDeviceExtension->jChipType == SIS_315)   ||
	(HwDeviceExtension->jChipType == SIS_315PRO)||
        (HwDeviceExtension->jChipType == SIS_550)   ||
        (HwDeviceExtension->jChipType == SIS_740)   ||
        (HwDeviceExtension->jChipType == SIS_650)   ||
	(HwDeviceExtension->jChipType == SIS_330) )
   {
        if(SiS_LowModeStuff(SiS_Pr,ModeNo,HwDeviceExtension)) {
	   SiS_FinalizeLCD(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex, HwDeviceExtension);
#if 0      /* Instead of FinalizeLCD(), older BIOSes (A92x) used OEMLCD() */
	   SiS_OEMLCD(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
#endif
           if(SiS_Pr->SiS_UseOEM) {
              SiS_OEM310Setting(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
           }
           SiS_CRT2AutoThreshold(SiS_Pr,BaseAddr);
        }
   }
#endif

   if(HwDeviceExtension->jChipType < SIS_315H) {
      if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
          if(HwDeviceExtension->jChipType != SIS_730) {
             SiS_DisplayOn(SiS_Pr);
	  }
      }
   }

   if(SiS_LowModeStuff(SiS_Pr,ModeNo,HwDeviceExtension)) {
      if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
         if(HwDeviceExtension->jChipType == SIS_730) {
            SiS_DisplayOn(SiS_Pr);
	 }
      }
      SiS_EnableBridge(SiS_Pr,HwDeviceExtension,BaseAddr);
   }

   SiS_DisplayOn(SiS_Pr);

   if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
	if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	     /* TW: Disable LCD panel when using TV */
	     SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x11,0x0C);
	} else {
	     /* TW: Disable TV when using LCD */
	     SiS_SetCH70xxANDOR(SiS_Pr,0x010E,0xF8);
	}
   }

   if(SiS_LowModeStuff(SiS_Pr,ModeNo,HwDeviceExtension)) {
      SiS_LockCRT2(SiS_Pr,HwDeviceExtension, BaseAddr);
   }

   return 1;
}

BOOLEAN
SiS_LowModeStuff(SiS_Private *SiS_Pr, USHORT ModeNo,
                 PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
    USHORT temp,temp1,temp2;

    if((ModeNo != 0x03) && (ModeNo != 0x10) && (ModeNo != 0x12))
         return(1);
    temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x11);
    SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x11,0x80);
    temp1 = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x00);
    SiS_SetReg1(SiS_Pr->SiS_P3d4,0x00,0x55);
    temp2 = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x00);
    SiS_SetReg1(SiS_Pr->SiS_P3d4,0x00,temp1);
    SiS_SetReg1(SiS_Pr->SiS_P3d4,0x11,temp);
    if((HwDeviceExtension->jChipType >= SIS_315H) ||
       (HwDeviceExtension->jChipType == SIS_300)) {
       if(temp2 == 0x55) return(0);
       else return(1);
    } else {
       if(temp2 != 0x55) return(1);
       else {
          SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x35,0x01);
          return(0);
       }
    }
}

/* TW: Set Part1 registers */
void
SiS_SetGroup1(SiS_Private *SiS_Pr,USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
              USHORT ModeIdIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension,
	      USHORT RefreshRateTableIndex)
{
  USHORT  temp=0, tempax=0, tempbx=0, tempcx=0;
  USHORT  pushbx=0, CRT1Index=0;
#ifdef SIS315H
  USHORT  pushcx=0, tempbl=0;
#endif
  USHORT  modeflag, resinfo=0;

  if(ModeNo<=0x13) {
	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
    	CRT1Index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {

	   SiS_SetCRT2Sync(SiS_Pr,BaseAddr,ROMAddr,ModeNo,
                           RefreshRateTableIndex,HwDeviceExtension);
#ifdef SIS315H
	   SiS_SetGroup1_LCDA(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
     	                      HwDeviceExtension,RefreshRateTableIndex);
#endif
  } else {

     if( (HwDeviceExtension->jChipType >= SIS_315H) &&
         (SiS_Pr->SiS_IF_DEF_LVDS == 1) &&
	 (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {

        SiS_SetCRT2Sync(SiS_Pr,BaseAddr,ROMAddr,ModeNo,
                        RefreshRateTableIndex,HwDeviceExtension);

     } else {

        SiS_SetCRT2Offset(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,
      		          RefreshRateTableIndex,HwDeviceExtension);

        if (HwDeviceExtension->jChipType < SIS_315H ) {
#ifdef SIS300
    	      SiS_SetCRT2FIFO_300(SiS_Pr,ROMAddr,ModeNo,HwDeviceExtension);
#endif
        } else {
#ifdef SIS315H
              SiS_SetCRT2FIFO_310(SiS_Pr,ROMAddr,ModeNo,HwDeviceExtension);
#endif
	}

        SiS_SetCRT2Sync(SiS_Pr,BaseAddr,ROMAddr,ModeNo,
                        RefreshRateTableIndex,HwDeviceExtension);

	/* 1. Horizontal setup */

        if (HwDeviceExtension->jChipType < SIS_315H ) {

#ifdef SIS300   /* ------------- 300 series --------------*/

    		temp = (SiS_Pr->SiS_VGAHT - 1) & 0x0FF;   			/* BTVGA2HT 0x08,0x09 */
    		SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x08,temp);                   /* TW: CRT2 Horizontal Total */

    		temp = (((SiS_Pr->SiS_VGAHT - 1) & 0xFF00) >> 8) << 4;
    		SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x09,0x0f,temp);          /* TW: CRT2 Horizontal Total Overflow [7:4] */

    		temp = (SiS_Pr->SiS_VGAHDE + 12) & 0x0FF;                       /* BTVGA2HDEE 0x0A,0x0C */
    		SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0A,temp);                   /* TW: CRT2 Horizontal Display Enable End */

    		pushbx = SiS_Pr->SiS_VGAHDE + 12;                               /* bx  BTVGA@HRS 0x0B,0x0C */
    		tempcx = (SiS_Pr->SiS_VGAHT - SiS_Pr->SiS_VGAHDE) >> 2;
    		tempbx = pushbx + tempcx;
    		tempcx <<= 1;
    		tempcx += tempbx;

    		if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
      			if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC){
			        /* CRT1Index &= 0x3F; - Not any longer */
        			tempbx = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[4];
        			tempbx |= ((SiS_Pr->SiS_CRT1Table[CRT1Index].CR[14] & 0xC0) << 2);
        			tempbx = (tempbx - 1) << 3;
        			tempcx = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[5];
        			tempcx &= 0x1F;
        			temp = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[15];
        			temp = (temp & 0x04) << (6-2);
        			tempcx = (tempcx | temp);
				tempcx--;
				tempcx <<= 3;
      			}

    			if((SiS_Pr->SiS_VBInfo & SetCRT2ToTV) && (resinfo == 0x08)){
        			if(!(SiS_Pr->SiS_VBInfo & SetPALTV)){
      					tempbx = 1040;
      					tempcx = 1042;
      				}
    			}
	        }

    		temp = tempbx & 0x00FF;
    		SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0B,temp);                   /* TW: CRT2 Horizontal Retrace Start */
#endif /* SIS300 */

 	} else {

#ifdef SIS315H  /* ----------------- 310/325/330 series ------------- */

	        tempcx = SiS_Pr->SiS_VGAHT;				       /* BTVGA2HT 0x08,0x09 */
		pushcx = tempcx;
		if(modeflag & HalfDCLK) {
#ifndef NEWCH701x		
		    if((SiS_Pr->SiS_IF_DEF_LVDS == 1) && (SiS_Pr->SiS_IF_DEF_CH70xx == 0)) {
#endif		    
		          tempax = SiS_Pr->SiS_VGAHDE >> 1;
			  tempcx = SiS_Pr->SiS_HT - SiS_Pr->SiS_HDE + tempax;
			  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
			      tempcx = SiS_Pr->SiS_HT - tempax;
			  }
#ifndef NEWCH701x					  
		    } else {
			  tempcx >>= 1;
		    }
#endif		    
		}
		tempcx--;

		temp = tempcx & 0xff;
		SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x08,temp);                  /* TW: CRT2 Horizontal Total */

		temp = ((tempcx & 0xff00) >> 8) << 4;
		SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x09,0x0F,temp);         /* TW: CRT2 Horizontal Total Overflow [7:4] */

		tempcx = pushcx;					       /* BTVGA2HDEE 0x0A,0x0C */
		tempbx = SiS_Pr->SiS_VGAHDE;
		tempcx -= tempbx;
		tempcx >>= 2;
		if(modeflag & HalfDCLK) {
		    tempbx >>= 1;
		    tempcx >>= 1;
		}
		tempbx += 16;

		temp = tempbx & 0xff;
		SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0A,temp);                  /* TW: CRT2 Horizontal Display Enable End */

		pushbx = tempbx;
		tempcx >>= 1;
		tempbx += tempcx;
		tempcx += tempbx;

		if(SiS_Pr->SiS_IF_DEF_LVDS==0) {
             	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {
                	tempbx = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[4];
                	tempbx |= ((SiS_Pr->SiS_CRT1Table[CRT1Index].CR[14] & 0xC0) << 2);
                	tempbx = (tempbx - 3) << 3;         		/*(VGAHRS-3)*8 */
                	tempcx = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[5];
               		tempcx &= 0x1F;
                	temp = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[15];
                	temp = (temp & 0x04) << (5-2);      		/* VGAHRE D[5] */
                	tempcx = (tempcx | temp);	  	  	/* (VGAHRE-3)*8 */
			tempcx -= 3;
			tempcx <<= 3;
			tempcx &= 0x00FF;
			tempcx |= (tempbx & 0xFF00);
                	tempbx += 16;
                	tempcx += 16;
			tempax = SiS_Pr->SiS_VGAHT;
			if(modeflag & HalfDCLK)  tempax >>= 1;
			tempax--;
			if(tempcx > tempax)  tempcx = tempax;
             	   }
         	   if((SiS_Pr->SiS_VBInfo & SetCRT2ToTV) && (resinfo == 0x08)){
             	      if(!(SiS_Pr->SiS_VBInfo & SetPALTV)){
      		 	 tempbx = 1040;
      		 	 tempcx = 1042;
      	     	      }
         	   }
		   /* TW: Makes no sense, but is in 650/302LV 1.10.6s */
         	   if((SiS_Pr->SiS_VBInfo & SetCRT2ToTV) && (resinfo == 0x08)){
		      if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV)) {
             	         if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
      		 	    tempbx = 1040;
      		 	    tempcx = 1042;
      	     	         }
		      }
         	   }
                }

		temp = tempbx & 0xff;
	 	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0B,temp);                 /* TW: CRT2 Horizontal Retrace Start */
#endif  /* SIS315H */

     	}  /* 310/325/330 series */

  	/* TW: The following is done for all bridge/chip types/series */

  	tempax = tempbx & 0xFF00;
  	tempbx = pushbx;
  	tempbx = (tempbx & 0x00FF) | ((tempbx & 0xFF00) << 4);
  	tempax |= (tempbx & 0xFF00);
  	temp = (tempax & 0xFF00) >> 8;
  	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0C,temp);                        /* TW: Overflow */

  	temp = tempcx & 0x00FF;
  	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0D,temp);                        /* TW: CRT2 Horizontal Retrace End */

  	/* 2. Vertical setup */

  	tempcx = SiS_Pr->SiS_VGAVT - 1;
  	temp = tempcx & 0x00FF;

        if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	     if(HwDeviceExtension->jChipType < SIS_315H) {
	          if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
		       if(SiS_Pr->SiS_VBInfo & (SetCRT2ToSVIDEO | SetCRT2ToAVIDEO)) {
		           temp--;
		       }
                  }
	     } else {
 		      temp--;
             }
        } else if(HwDeviceExtension->jChipType >= SIS_315H) {
	    /* TW: 650/30xLV 1.10.6s */
	    temp--;
	}
  	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0E,temp);                        /* TW: CRT2 Vertical Total */

  	tempbx = SiS_Pr->SiS_VGAVDE - 1;
  	temp = tempbx & 0x00FF;
  	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0F,temp);                        /* TW: CRT2 Vertical Display Enable End */

  	temp = ((tempbx & 0xFF00) << 3) >> 8;
  	temp |= ((tempcx & 0xFF00) >> 8);
  	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x12,temp);                        /* TW: Overflow (and HWCursor Test Mode) */

	/* TW: 650/LVDS (1.10.07), 650/30xLV (1.10.6s) */
	if(HwDeviceExtension->jChipType >= SIS_315H) {
           tempbx++;
   	   tempax = tempbx;
	   tempcx++;
	   tempcx -= tempax;
	   tempcx >>= 2;
	   tempbx += tempcx;
	   if(tempcx < 4) tempcx = 4;
	   tempcx >>= 2;
	   tempcx += tempbx;
	   tempcx++;
	} else {
	   /* TW: 300 series, LVDS/301B: */
  	   tempbx = (SiS_Pr->SiS_VGAVT + SiS_Pr->SiS_VGAVDE) >> 1;                 /*  BTVGA2VRS     0x10,0x11   */
  	   tempcx = ((SiS_Pr->SiS_VGAVT - SiS_Pr->SiS_VGAVDE) >> 4) + tempbx + 1;  /*  BTVGA2VRE     0x11        */
	}

  	if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
    	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC){
      		tempbx = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[8];
      		temp = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[7];
      		if(temp & 0x04) tempbx |= 0x0100;
      		if(temp & 0x80) tempbx |= 0x0200;
      		temp = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[13];
      		if(temp & 0x08) tempbx |= 0x0400;
      		temp = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[9];
      		tempcx = (tempcx & 0xFF00) | (temp & 0x00FF);
    	   }
  	}
  	temp = tempbx & 0x00FF;
  	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x10,temp);           /* TW: CRT2 Vertical Retrace Start */

  	temp = ((tempbx & 0xFF00) >> 8) << 4;
  	temp |= (tempcx & 0x000F);
  	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x11,temp);           /* TW: CRT2 Vert. Retrace End; Overflow; "Enable CRTC Check" */

  	/* 3. Panel compensation delay */

  	if(HwDeviceExtension->jChipType < SIS_315H) {

#ifdef SIS300  /* ---------- 300 series -------------- */

	   if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
	        temp = 0x20;

		if(HwDeviceExtension->jChipType == SIS_300) {
		   temp = 0x10;
		   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)  temp = 0x2c;
		   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) temp = 0x20;
		   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960)  temp = 0x24;
		}
		if(SiS_Pr->SiS_VBType & VB_SIS301) {
		   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) temp = 0x20;
		}
		if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960)     temp = 0x24;
		if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) 		temp = 0x08;
		if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {
      		   if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) 	temp = 0x2c;
      		   else 					temp = 0x20;
    	        }
		if((ROMAddr) && (SiS_Pr->SiS_UseROM) && (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) {
		    if(ROMAddr[0x220] & 0x80) {
		        if(SiS_Pr->SiS_VBInfo & (SetCRT2ToTV-SetCRT2ToHiVisionTV))
				temp = ROMAddr[0x221];
			else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV)
				temp = ROMAddr[0x222];
		        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)
				temp = ROMAddr[0x223];
			else
				temp = ROMAddr[0x224];
			temp &= 0x3c;
		    }
		}
		if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
		   if(HwDeviceExtension->pdc) {
			temp = HwDeviceExtension->pdc & 0x3c;
		   }
		}
	   } else {
	        temp = 0x20;
		if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {
		   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) temp = 0x04;
		}
		if((ROMAddr) && SiS_Pr->SiS_UseROM) {
		    if(ROMAddr[0x220] & 0x80) {
		        temp = ROMAddr[0x220] & 0x3c;
		    }
		}
		if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
		   if(HwDeviceExtension->pdc) {
			temp = HwDeviceExtension->pdc & 0x3c;
		   }
		}
	   }

    	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,~0x03C,temp);         /* TW: Panel Link Delay Compensation; (Software Command Reset; Power Saving) */

#endif  /* SIS300 */

  	} else {

#ifdef SIS315H   /* ----------- 310/325/330 series ---------------*/

	   if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
                temp = 0x10;
                if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)  temp = 0x2c;
    	        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) temp = 0x20;
    	        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960)  temp = 0x24;
		if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
		   temp = 0x08;
		   if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {
		      switch(SiS_Pr->SiS_HiVision) {
		      case 2:
		      case 1:
		      case 0:
		         temp = 0x08;
			 break;
		      default:
      		         if(SiS_Pr->SiS_VBInfo & SetInSlaveMode)  temp = 0x2c;
      		         else 					  temp = 0x20;
		      }
    	           }
		}
		if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
		   tempbl = 0x00;
		   if((ROMAddr) && (SiS_Pr->SiS_UseROM)) {
		      if(HwDeviceExtension->jChipType < SIS_330) {
		         if(ROMAddr[0x13c] & 0x80) tempbl = 0xf0;
		      } else {
		         if(ROMAddr[0x1bc] & 0x80) tempbl = 0xf0;
		      }
		   }
		} else {  /* LV (550/301LV checks ROM byte, other LV BIOSes do not) */
		   tempbl = 0xF0;
		}
	   } else {
	        if(HwDeviceExtension->jChipType == SIS_740) {
		   temp = 0x03;
	        } else {
		   temp = 0x00;
		}
		if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) temp = 0x0a;
		tempbl = 0xF0;
		if(HwDeviceExtension->jChipType == SIS_650) {
		   if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
		      if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) tempbl = 0x0F;
		   }
		}
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,tempbl,temp);	    /* TW: Panel Link Delay Compensation */

    	   tempax = 0;
    	   if (modeflag & DoubleScanMode) tempax |= 0x80;
    	   if (modeflag & HalfDCLK)       tempax |= 0x40;
    	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2C,0x3f,tempax);

#endif  /* SIS315H */

  	}

     }  /* Slavemode */

     if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {

        /* TW: For 301BDH, we set up the Panel Link */
        if( (SiS_Pr->SiS_VBType & VB_NoLCD) &&
	    (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) ) {

	    SiS_SetGroup1_LVDS(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                       HwDeviceExtension,RefreshRateTableIndex);

        } else if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {                             

    	    SiS_SetGroup1_301(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                      HwDeviceExtension,RefreshRateTableIndex);
        }

     } else {

        if(HwDeviceExtension->jChipType < SIS_315H) {
	
	   SiS_SetGroup1_LVDS(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                        HwDeviceExtension,RefreshRateTableIndex);
	} else {
	
	   if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
              if((!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) || (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
    	          SiS_SetGroup1_LVDS(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                              HwDeviceExtension,RefreshRateTableIndex);
              }
	   } else {
	      SiS_SetGroup1_LVDS(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
	                         HwDeviceExtension,RefreshRateTableIndex);
	   }
	
	}

     }
   } /* LCDA */
}

void
SiS_SetGroup1_301(SiS_Private *SiS_Pr, USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                  PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex)
{
  USHORT  push1,push2;
  USHORT  tempax,tempbx,tempcx,temp;
  USHORT  resinfo,modeflag;

  if(ModeNo <= 0x13) {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  /* TW: The following is only done if bridge is in slave mode: */

  tempax = 0xFFFF;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV))  tempax = SiS_GetVGAHT2(SiS_Pr);

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
  	modeflag |= Charx8Dot;
  }

  if(modeflag & Charx8Dot) tempcx = 0x08;
  else                     tempcx = 0x09;

  if(tempax >= SiS_Pr->SiS_VGAHT) tempax = SiS_Pr->SiS_VGAHT;

  if(modeflag & HalfDCLK) tempax >>= 1;

  tempax = (tempax / tempcx) - 5;
  tempbx = tempax & 0x00FF;

  temp = 0xFF;                                                  /* set MAX HT */
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x03,temp);

  tempax = SiS_Pr->SiS_VGAHDE;                                 	/* 0x04 Horizontal Display End */
  if(modeflag & HalfDCLK) tempax >>= 1;
  tempax = (tempax / tempcx) - 1;
  tempbx |= ((tempax & 0x00FF) << 8);
  temp = tempax & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x04,temp);

  temp = (tempbx & 0xFF00) >> 8;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) {        
    	    temp += 2;
        }
  }	
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {
     if(SiS_Pr->SiS_HiVision == 3) {
              if(resinfo == 7) temp -= 2;
     }
  }
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x05,temp);                 /* 0x05 Horizontal Display Start */

  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x06,0x03);                 /* 0x06 Horizontal Blank end     */

  if((SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) &&
     (SiS_Pr->SiS_HiVision == 3)) {
    temp = (tempbx & 0x00FF) - 1;
    if(!(modeflag & HalfDCLK)) {
      temp -= 6;
      if(SiS_Pr->SiS_SetFlag & TVSimuMode) {
        temp -= 2;
        if(ModeNo > 0x13) temp -= 10;
      }
    }
  } else {
    tempcx = tempbx & 0x00FF;
    tempbx = (tempbx & 0xFF00) >> 8;
    tempcx = (tempcx + tempbx) >> 1;
    temp = (tempcx & 0x00FF) + 2;
    if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV){
       temp--;
       if(!(modeflag & HalfDCLK)){
          if((modeflag & Charx8Dot)){
             temp += 4;
             if(SiS_Pr->SiS_VGAHDE >= 800) temp -= 6;
             if(HwDeviceExtension->jChipType >= SIS_315H) {
	        if(SiS_Pr->SiS_VGAHDE == 800) temp += 2;
             }
          }
       }
    } else {
       if(!(modeflag & HalfDCLK)) {
          temp -= 4;
          if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x960) {
             if(SiS_Pr->SiS_VGAHDE >= 800) {
                temp -= 7;
	        if(HwDeviceExtension->jChipType < SIS_315H) {
	           /* 650/301LV(x) does not do this, 630/301B, 300/301LV do */
                   if(SiS_Pr->SiS_ModeType == ModeEGA) {
                      if(SiS_Pr->SiS_VGAVDE == 1024) {
                         temp += 15;
                         if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x1024) 
			    temp += 7;
                      }
                   }
	        }
                if(SiS_Pr->SiS_VGAHDE >= 1280) {
                   if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x960) {
                      if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) temp += 28;
                   }
                }
             }
          }
       }
    }
  }
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,temp);               	/* 0x07 Horizontal Retrace Start */

  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x08,0x00);                 /* 0x08 Horizontal Retrace End   */

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     if(SiS_Pr->SiS_SetFlag & TVSimuMode) {
            if(ModeNo <= 0x01) {
	        SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,0x2a);
		if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
		    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x08,0x61);
		} else {
		    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x08,0x41);
		}
	    } else if(SiS_Pr->SiS_ModeType == ModeText) {
	        if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
		    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,0x54);
		} else {
		    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,0x55);
		}
		SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x08,0x00);
	    } else if(ModeNo <= 0x13) {
	        if(modeflag & HalfDCLK) {
		    if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
		        SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,0x30);
			SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x08,0x03);
		    } else {
		        SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,0x2f);
			SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x08,0x02);
		    }
		} else {
		    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,0x5b);
		    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x08,0x03);
		}
	    } else if( ((HwDeviceExtension->jChipType >= SIS_315H) && (ModeNo == 0x50)) ||
	               ((HwDeviceExtension->jChipType < SIS_315H) && (resinfo == 0 || resinfo == 1)) ) {
	        if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
		    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,0x30);
		    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x08,0x03);
		} else {
		    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,0x2f);
		    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x08,0x03);
		}
	    }

     }
  }
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {
     if(SiS_Pr->SiS_HiVision & 0x03) {
        SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,0xb2);
	if(SiS_Pr->SiS_HiVision & 0x02) {
	   SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,0xab);
	}
     }
  }

  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,0x03);                	/* 0x18 SR08 (FIFO Threshold?)   */

  SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x19,0xF0);

  tempbx = SiS_Pr->SiS_VGAVT;
  push1 = tempbx;

  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x09,0xFF);                	/* 0x09 Set Max VT    */

  tempcx = 0x121;
  tempbx = SiS_Pr->SiS_VGAVDE;                               	/* 0x0E Vertical Display End */
  if(tempbx == 357) tempbx = 350;
  if(tempbx == 360) tempbx = 350;
  if(tempbx == 375) tempbx = 350;
  if(tempbx == 405) tempbx = 400;
  if(tempbx == 420) tempbx = 400;
  if(tempbx == 525) tempbx = 480;
  push2 = tempbx;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
    	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
      		if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
        		if(tempbx == 350) tempbx += 5;
        		if(tempbx == 480) tempbx += 5;
      		}
    	}
  }
  tempbx--;
  temp = tempbx & 0x00FF;
  tempbx--;
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x10,temp);        		/* 0x10 vertical Blank Start */

  tempbx = push2;
  tempbx--;
  temp = tempbx & 0x00FF;
#if 0
  /* TW: Missing code from 630/301B 2.04.5a and 650/302LV 1.10.6s (calles int 2f) */
  if(xxx()) {
      if(temp == 0xdf) temp = 0xda;
  }
#endif
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0E,temp);

  if(tempbx & 0x0100) {
  	tempcx |= 0x0002;
	if(SiS_Pr->SiS_VBType & VB_SIS301) tempcx |= 0x000a;
  }

  tempax = 0x000B;
  if(modeflag & DoubleScanMode) tempax |= 0x8000;

  if(tempbx & 0x0200) {
  	tempcx |= 0x0040;
	if(SiS_Pr->SiS_VBType & VB_SIS301) tempax |= 0x2000;
  }

  if(SiS_Pr->SiS_VBType & VB_SIS301) {
        if(SiS_Pr->SiS_VBInfo & SetPALTV) {
	      if(SiS_Pr->SiS_VGAVDE == 480) {
	             tempax = (tempax & 0x00ff) | 0x2000;
		     if(modeflag & DoubleScanMode)  tempax |= 0x8000;
	      }
	}
  }

  temp = (tempax & 0xFF00) >> 8;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0B,temp);

  if(tempbx & 0x0400) tempcx |= 0x0600;

  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x11,0x00);                	/* 0x11 Vertical Blank End */

  tempax = push1;
  tempax -= tempbx;
  tempax >>= 2;
  push1 = tempax;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
        /* TW: 650/30xLV 1.10.6s */
        if(ModeNo > 0x13) {
	    if(resinfo != 0x09) {  /* 1280x1024 */
	        tempax <<= 1;
		tempbx += tempax;
	    }
	} else {
	    if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1400x1050) {
	        tempax <<= 1;
		tempbx += tempax;
	    }
	}
  } else if((resinfo != 0x09) || (SiS_Pr->SiS_VBType & VB_SIS301)) {
    	tempax <<= 1;
    	tempbx += tempax;
  }

  if( (SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) &&
      (SiS_Pr->SiS_HiVision == 3) ) {
    	tempbx -= 10;
  } else {
    	if(SiS_Pr->SiS_SetFlag & TVSimuMode) {
      	   if(SiS_Pr->SiS_VBInfo & SetPALTV) {
	       if(!(SiS_Pr->SiS_HiVision & 0x03)) {
                    tempbx += 40;
		    if(HwDeviceExtension->jChipType >= SIS_315H) {
		       if(SiS_Pr->SiS_VGAHDE == 800) tempbx += 10;
		    }
      	       }
	   }
    	}
  }
  tempax = push1;
  tempax >>= 2;
  tempax++;
  tempax += tempbx;
  push1 = tempax;
  if(SiS_Pr->SiS_VBInfo & SetPALTV) {
    	if(tempbx <= 513)  {
      		if(tempax >= 513) tempbx = 513;
    	}
  }
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0C,temp);			/* 0x0C Vertical Retrace Start */

  if(!(SiS_Pr->SiS_VBType & VB_SIS301)) {
  	tempbx--;
  	temp = tempbx & 0x00FF;
  	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x10,temp);

	if(tempbx & 0x0100) tempcx |= 0x0008;

  	if(tempbx & 0x0200) {
    	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x0B,0x20);
	}

  	tempbx++;
  }
  if(tempbx & 0x0100) tempcx |= 0x0004;
  if(tempbx & 0x0200) tempcx |= 0x0080;
  if(tempbx & 0x0400) {
        if(SiS_Pr->SiS_VBType & VB_SIS301) tempcx |= 0x0800;
  	else                               tempcx |= 0x0C00;
  }

  tempbx = push1;
  temp = tempbx & 0x00FF;
  temp &= 0x0F;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0D,temp);        		/* 0x0D vertical Retrace End */

  if(tempbx & 0x0010) tempcx |= 0x2000;

  temp = tempcx & 0x00FF;
  if(SiS_Pr->SiS_VBType & VB_SIS301) {
	if(SiS_Pr->SiS_VBInfo & SetPALTV) {
	      if(SiS_Pr->SiS_VGAVDE == 480)  temp = 0xa3;
	}
  }
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0A,temp);              	/* 0x0A CR07 */

  temp = (tempcx & 0xFF00) >> 8;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x17,temp);              	/* 0x17 SR0A */

  tempax = modeflag;
  temp = (tempax & 0xFF00) >> 8;
  temp = (temp >> 1) & 0x09;
  if(!(SiS_Pr->SiS_VBType & VB_SIS301)) {
       /* Only use 8 dot clock */
       temp |= 0x01;
  }
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x16,temp);              	/* 0x16 SR01 */

  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0F,0x00);              	/* 0x0F CR14 */

  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x12,0x00);              	/* 0x12 CR17 */

  if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
       if(IS_SIS650) {
           /* TW: 650/30xLV 1.10.6s */
           if(SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00) & 0x01) {
	       temp = 0x80;
	   }
       } else temp = 0x80;
  } else  temp = 0x00;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1A,temp);                	/* 0x1A SR0E */

  return;
}

void
SiS_SetGroup1_LVDS(SiS_Private *SiS_Pr,USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
		   USHORT ModeIdIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension,
		   USHORT RefreshRateTableIndex)
{
  USHORT modeflag, resinfo;
  USHORT push1, push2, tempax, tempbx, tempcx, temp;
#ifdef SIS315H
  USHORT pushcx;
#endif
  ULONG  tempeax=0, tempebx, tempecx, tempvcfact=0;

  if(ModeNo <= 0x13) {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  /* TW: Set up Panel Link */

  /* 1. Horizontal setup */

  tempax = SiS_Pr->SiS_LCDHDES;

  if( (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) &&
      (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ) {
  	tempax -= 8;
  }

  tempcx = SiS_Pr->SiS_HT;    				  /* Horiz. Total */

  tempbx = SiS_Pr->SiS_HDE;                               /* Horiz. Display End */

  if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
     if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
        if((!SiS_Pr->SiS_IF_DEF_DSTN) && (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480)) {
 	   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)        tempbx =  800;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600)  tempbx = 1024;  /* TW */
    	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)  tempbx = 1024;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768)  tempbx = 1152;  /* TW */
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)  tempbx = 1280;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) tempbx = 1280; 
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) tempbx = 1400; 
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempbx = 1600; 
        }
     }
  }
  tempcx = (tempcx - tempbx) >> 2;		 /* HT-HDE / 4 */

  push1 = tempax;

  tempax += tempbx;

  if(tempax >= SiS_Pr->SiS_HT) tempax -= SiS_Pr->SiS_HT;

  push2 = tempax;
  
  if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) { 
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if((!SiS_Pr->SiS_IF_DEF_DSTN) && (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480)) {
     	   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)        tempcx = 0x0028;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600)  tempcx = 0x0018;
     	   else if( (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) ||
	            (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768) ) {
	  	   if(HwDeviceExtension->jChipType < SIS_315H) {
		      if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
		         tempcx = 0x0017;
#ifdef TWNEWPANEL
			 tempcx = 0x0018;
#endif
		      } else {
		         tempcx = 0x0017;  /* A901; sometimes 0x0018; */
		      }
		   } else {
		      tempcx = 0x0018;
		   }
	   }
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)  tempcx = 0x0028;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) tempcx = 0x0030;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) tempcx = 0x0030;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempcx = 0x0040;
        }
     }
  }

  tempcx += tempax;                              /* lcdhrs  */
  if(tempcx >= SiS_Pr->SiS_HT) tempcx -= SiS_Pr->SiS_HT;

  tempax = tempcx >> 3;                          /* BPLHRS */
  temp = tempax & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x14,temp);		 /* Part1_14h; TW: Panel Link Horizontal Retrace Start  */

  if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
     temp = (tempax & 0x00FF) + 2;
  } else {
     temp = (tempax & 0x00FF) + 10;
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if(!SiS_Pr->SiS_IF_DEF_DSTN) {
           if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
	      temp += 6;
              if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel800x600) {
	         temp++;
	         if(HwDeviceExtension->jChipType >= SIS_315H) {
	            if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1024x768) {
	               temp += 7;
		       if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1600x1200) {
		          temp -= 0x14;
			  if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x768) {
			     temp -= 10;
			  }
		       }
	            }
	         }
	      }
           }
        }
     }
  }

  temp &= 0x1F;
  temp |= ((tempcx & 0x0007) << 5);
  if(SiS_Pr->SiS_IF_DEF_FSTN) temp = 0x20;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x15,temp);    	 /* Part1_15h; TW: Panel Link Horizontal Retrace End/Skew */

  tempbx = push2;
  tempcx = push1;                                /* lcdhdes  */

  temp = (tempcx & 0x0007);                      /* BPLHDESKEW  */
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1A,temp);   	 /* Part1_1Ah; TW: Panel Link Vertical Retrace Start (2:0) */

  tempcx >>= 3;                                  /* BPLHDES */
  temp = (tempcx & 0x00FF);
  if(ModeNo == 0x5b) temp--;                     
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x16,temp);    	 /* Part1_16h; TW: Panel Link Horizontal Display Enable Start  */

  if(HwDeviceExtension->jChipType < SIS_315H) {  
     if(tempbx & 0x07) tempbx += 8;              
  }
  tempbx >>= 3;                                  /* BPLHDEE  */
  temp = tempbx & 0x00FF;
  if(ModeNo == 0x5b) temp--;			 
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x17,temp);   	 /* Part1_17h; TW: Panel Link Horizontal Display Enable End  */

  /* 2. Vertical setup */

  if(HwDeviceExtension->jChipType < SIS_315H) {
     tempcx = SiS_Pr->SiS_VGAVT;
     tempbx = SiS_Pr->SiS_VGAVDE;
     if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
        if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
	   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)       tempbx =  600;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) tempbx =  600;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) tempbx =  768;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768) tempbx =  768;
	   else								tempbx = 1024;
        }
     }
     tempcx -= tempbx;

  } else {

     tempcx = SiS_Pr->SiS_VGAVT - SiS_Pr->SiS_VGAVDE;          /* VGAVT-VGAVDE  */

  }

  tempbx = SiS_Pr->SiS_LCDVDES;	   		 	 	/* VGAVDES  */
  push1 = tempbx;

  tempax = SiS_Pr->SiS_VGAVDE;

  if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) && (!SiS_Pr->SiS_IF_DEF_DSTN)) {
     if( (SiS_Pr->SiS_IF_DEF_TRUMPION == 0)   && 
         (!(SiS_Pr->SiS_LCDInfo & LCDPass11)) &&
         (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) ) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)        tempax =  600;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600)  tempax =  600;  
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)  tempax =  768;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768)  tempax =  768;  
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)  tempax =  768;  
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) tempax = 1024; 
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) tempax = 1050; 
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempax = 1200; 
     }
  }

  tempbx += tempax;
  if(tempbx >= SiS_Pr->SiS_VT) tempbx -= SiS_Pr->SiS_VT;

  push2 = tempbx;

  tempcx >>= 1;

  if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) && (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480)) {
     if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
        if(!SiS_Pr->SiS_IF_DEF_DSTN) {
     	   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)        tempcx = 0x0001;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600)  tempcx = 0x0001;
     	   else if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) ||
	           (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768)) {
		   if(HwDeviceExtension->jChipType < SIS_315H) {
		      if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
			    tempcx = 0x0002;
#ifdef TWNEWPANEL
			    tempcx = 0x0003;
#endif
		      } else {
		            tempcx = 0x0002;   /* TW: A901; sometimes 0x0003; */
		      }
		   } else tempcx = 0x0003;
           }
     	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)  tempcx = 0x0003;
     	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) tempcx = 0x0001;
     	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) tempcx = 0x0001;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempcx = 0x0001;
     	   else 							 tempcx = 0x0057;
	}
     }
  }

  tempbx += tempcx;			 	/* BPLVRS  */

  if(HwDeviceExtension->jChipType < SIS_315H) {
      tempbx++;
  }

  if(tempbx >= SiS_Pr->SiS_VT) tempbx -= SiS_Pr->SiS_VT;

  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,temp);       	 /* Part1_18h; TW: Panel Link Vertical Retrace Start  */

  tempcx >>= 3;

  if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if( (HwDeviceExtension->jChipType < SIS_315H) &&
            (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) )     tempcx = 0x0001;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)    tempcx = 0x0003;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600)   tempcx = 0x0005;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768)   tempcx = 0x0005;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)   tempcx = 0x0011;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)  tempcx = 0x0005;	 
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)  tempcx = 0x0002;
        else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)  tempcx = 0x0011;
        else if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480)  {
     		if(HwDeviceExtension->jChipType < SIS_315H) {
		        if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
				tempcx = 0x0004;
#ifdef TWNEWPANEL
				tempcx = 0x0005;
#endif
		        } else {
				tempcx = 0x0004;   /* A901; Other BIOS sets 0x0005; */
			}
		} else {
			tempcx = 0x0005;
		}
        }
     }
  }

  tempcx = tempcx + tempbx + 1;                  /* BPLVRE  */
  temp = tempcx & 0x000F;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0xf0,temp); /* Part1_19h; TW: Panel Link Vertical Retrace End (3:0); Misc.  */

  temp = ((tempbx & 0x0700) >> 8) << 3;          /* BPLDESKEW =0 */
  if(SiS_Pr->SiS_VGAVDE != SiS_Pr->SiS_VDE)  temp |= 0x40;
  if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA)    temp |= 0x40;
  if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
      if(HwDeviceExtension->jChipType >= SIS_315H) {
         if(SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00) & 0x01) {
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
  }
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x1A,0x87,temp);  /* Part1_1Ah; TW: Panel Link Control Signal (7:3); Vertical Retrace Start (2:0) */

  if (HwDeviceExtension->jChipType < SIS_315H) {

#ifdef SIS300      /* 300 series */

        tempeax = SiS_Pr->SiS_VGAVDE << 6;
        temp = (USHORT)(tempeax % (ULONG)SiS_Pr->SiS_VDE);
        tempeax = tempeax / (ULONG)SiS_Pr->SiS_VDE;
        if(temp != 0) tempeax++;
        tempebx = tempeax;                         /* BPLVCFACT  */

  	if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA) {
	   tempebx = 0x003F;
	}

  	temp = (USHORT)(tempebx & 0x00FF);
  	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1E,temp);      /* Part1_1Eh; TW: Panel Link Vertical Scaling Factor */

#endif /* SIS300 */

  } else {

#ifdef SIS315H  /* 310/325 series */

#ifdef NEWCH701x
        SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x03);
#else
	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1E,0x23);
#endif	

	tempeax = SiS_Pr->SiS_VGAVDE << 18;
    	temp = (USHORT)(tempeax % (ULONG)SiS_Pr->SiS_VDE);
    	tempeax = tempeax / SiS_Pr->SiS_VDE;
    	if(temp != 0) tempeax++;
    	tempebx = tempeax;                         /* BPLVCFACT  */
        tempvcfact = tempeax;
    	temp = (USHORT)(tempebx & 0x00FF);
    	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x37,temp);      /* Part1_37h; TW: Panel Link Vertical Scaling Factor */
    	temp = (USHORT)((tempebx & 0x00FF00) >> 8);
    	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x36,temp);      /* Part1_36h; TW: Panel Link Vertical Scaling Factor */
    	temp = (USHORT)((tempebx & 0x00030000) >> 16);
    	if(SiS_Pr->SiS_VDE == SiS_Pr->SiS_VGAVDE) temp |= 0x04;
    	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x35,temp);      /* Part1_35h; TW: Panel Link Vertical Scaling Factor */

#endif /* SIS315H */

  }

  tempbx = push2;                                  /* BPLVDEE  */
  tempcx = push1;

  push1 = temp;					   

  if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
   	if(!SiS_Pr->SiS_IF_DEF_DSTN){
		if(HwDeviceExtension->jChipType < SIS_315H) {
			if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
      				if(resinfo == 15) tempcx++;
				if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
					if(resinfo == 7) tempcx++;
		    		}
			} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) {
      				if(resinfo == 7) tempcx++;
				if(resinfo == 8) tempcx++; /* TW: Doesnt make sense anyway... */
			} else  if(resinfo == 8) tempcx++;
		} else {
			if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) {
      				if(resinfo == 7) tempcx++;
			}
		}
	}
  }

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) {
     tempcx = SiS_Pr->SiS_VGAVDE;
     tempbx = SiS_Pr->SiS_VGAVDE - 1;
  }

  temp = ((tempbx & 0x0700) >> 8) << 3;
  temp |= ((tempcx & 0x0700) >> 8);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1D,temp);     	/* Part1_1Dh; TW: Vertical Display Overflow; Control Signal */

  temp = tempbx & 0x00FF;
  if(SiS_Pr->SiS_IF_DEF_FSTN) temp++;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1C,temp);      	/* Part1_1Ch; TW: Panel Link Vertical Display Enable End  */

  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1B,temp);      	/* Part1_1Bh; TW: Panel Link Vertical Display Enable Start  */

  /* 3. Additional horizontal setup (scaling, etc) */

  tempecx = SiS_Pr->SiS_VGAHDE;
  if(HwDeviceExtension->jChipType >= SIS_315H) {
     if(modeflag & HalfDCLK)
        tempecx >>= 1;
  }
  tempebx = SiS_Pr->SiS_HDE;
  if(tempecx == tempebx) tempeax = 0xFFFF;
  else {
     tempeax = tempecx;
     tempeax <<= 16;
     temp = (USHORT)(tempeax % tempebx);
     tempeax = tempeax / tempebx;
     if(HwDeviceExtension->jChipType >= SIS_315H) {
         if(temp) tempeax++;
     }
  }
  tempecx = tempeax;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
      tempeax = SiS_Pr->SiS_VGAHDE;
      if(modeflag & HalfDCLK) tempeax >>= 1;
      tempeax <<= 16;
      tempeax = (tempeax / tempecx) - 1;
  } else {
      tempeax = ((SiS_Pr->SiS_VGAHT << 16) / tempecx) - 1;
  }
  tempecx <<= 16;
  tempecx |= (tempeax & 0xFFFF);
  temp = (USHORT)(tempecx & 0x00FF);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1F,temp);  	 /* Part1_1Fh; TW: Panel Link DDA Operational Number in each horiz. line */

  tempbx = SiS_Pr->SiS_VDE;
  if(HwDeviceExtension->jChipType >= SIS_315H) {
      tempeax = (SiS_Pr->SiS_VGAVDE << 18) / tempvcfact;
      tempbx = (USHORT)(tempeax & 0x0FFFF);
  } else {
      tempax = SiS_Pr->SiS_VGAVDE << 6;
      tempbx = push1;
      tempbx &= 0x3f;
      if(tempbx == 0) tempbx = 64;
      tempax = tempax / tempbx;
      tempbx = tempax;
  }
  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) tempbx--;
  if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA)                 tempbx = 1;

  temp = ((tempbx & 0xFF00) >> 8) << 3;
  temp |= (USHORT)((tempecx & 0x0700) >> 8);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x20,temp);  	/* Part1_20h; TW: Overflow register */

  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x21,temp);  	/* Part1_21h; TW: Panel Link Vertical Accumulator Register */

  tempecx >>= 16;                               /* BPLHCFACT  */
  if(HwDeviceExtension->jChipType < SIS_315H) {
      if(modeflag & HalfDCLK) tempecx >>= 1;
  }
  temp = (USHORT)((tempecx & 0xFF00) >> 8);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x22,temp);     	/* Part1_22h; TW: Panel Link Horizontal Scaling Factor High */

  temp = (USHORT)(tempecx & 0x00FF);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x23,temp);         /* Part1_22h; TW: Panel Link Horizontal Scaling Factor Low */

  /* 630/301B and 630/LVDS do something for 640x480 panels here */

#ifdef SIS315H
  /* TW: DSTN/FSTN initialisation - hardcoded for 320x480 panel */
  if(SiS_Pr->SiS_IF_DEF_DSTN) {
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1E,0x01);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x25,0x00);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x26,0x00);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x27,0x00);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x28,0x87);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x29,0x5A);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x2A,0x4B);
     	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x44,~0x007,0x03);
     	tempbx = SiS_Pr->SiS_HDE + 64;                       	/*Blps = lcdhdee(lcdhdes+HDE) + 64*/
     	temp = tempbx & 0x00FF;
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x38,temp);
     	temp=((tempbx & 0xFF00) >> 8) << 3;
     	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x35,~0x078,temp);
     	tempbx += 32;		                     		/*Blpe=lBlps+32*/
     	temp = tempbx & 0x00FF;
     	if(SiS_Pr->SiS_IF_DEF_FSTN)  temp=0;
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x39,temp);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x3A,0x00);        	/*Bflml=0*/
     	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x3C,~0x007,0x00);
     	tempbx = SiS_Pr->SiS_VDE / 2;
     	temp = tempbx & 0x00FF;
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x3B,temp);
     	temp = ((tempbx & 0xFF00) >> 8) << 3;
     	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x3C,~0x038,temp);
     	tempeax = SiS_Pr->SiS_HDE << 2;                       	/* BDxFIFOSTOP = (HDE*4)/128 */
     	tempebx = 128;
     	temp = (USHORT)(tempeax % tempebx);
     	tempeax = tempeax / tempebx;
     	if(temp != 0)  tempeax++;
     	temp = (USHORT)(tempeax & 0x003F);
     	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x45,~0x0FF,temp);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x3F,0x00);         	/* BDxWadrst0 */
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x3E,0x00);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x3D,0x10);
     	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x3C,~0x040,0x00);
     	tempax = SiS_Pr->SiS_HDE >> 4;                        	/* BDxWadroff = HDE*4/8/8 */
     	pushcx = tempax;
     	temp = tempax & 0x00FF;
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x43,temp);
     	temp = ((tempax & 0xFF00) >> 8) << 3;
     	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x44,~0x0F8,temp);
     	tempax = SiS_Pr->SiS_VDE;                             /*BDxWadrst1 = BDxWadrst0 + BDxWadroff * VDE */
     	tempeax = (tempax * pushcx);
     	tempebx = 0x00100000 + tempeax;
     	temp = (USHORT)tempebx & 0x000000FF;
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x42,temp);
     	temp = (USHORT)((tempebx & 0x0000FF00)>>8);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x41,temp);
     	temp = (USHORT)((tempebx & 0x00FF0000)>>16);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x40,temp);
     	temp = (USHORT)(((tempebx & 0x01000000)>>24) << 7);
     	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x3C,~0x080,temp);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x2F,0x03);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x03,0x50);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x04,0x00);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x2F,0x01);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x13,0x00);
     	SiS_SetReg1(SiS_Pr->SiS_P3c4,0x05,0x86);        /* Unlock */
     	SiS_SetReg1(SiS_Pr->SiS_P3c4,0x1e,0x62);
     	if(SiS_Pr->SiS_IF_DEF_FSTN){
         	SiS_SetReg1(SiS_Pr->SiS_P3c4,0x2b,0x1b);
         	SiS_SetReg1(SiS_Pr->SiS_P3c4,0x2c,0xe3);
         	SiS_SetReg1(SiS_Pr->SiS_P3c4,0x1e,0x62);
         	SiS_SetReg1(SiS_Pr->SiS_P3c4,0x2e,0x04);
         	SiS_SetReg1(SiS_Pr->SiS_P3c4,0x2f,0x42);
         	SiS_SetReg1(SiS_Pr->SiS_P3c4,0x32,0x01);
         	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x2b,0x02);
         	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x2c,0x00);
         	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x2d,0x00);
     	}
     	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0f,0x30);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1e,0x7d);
     	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x2e,0xe0);
  }
#endif  /* SIS315H */

  return;

}

#ifdef SIS315H
void
SiS_CRT2AutoThreshold(SiS_Private *SiS_Pr, USHORT BaseAddr)
{
  SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x01,0x40);
}
#endif


#ifdef SIS315H
/* TW: For LVDS / 302B/30xLV - LCDA (this must only be called on 310/325 series!) */
void
SiS_SetGroup1_LCDA(SiS_Private *SiS_Pr, USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex)
{
  USHORT modeflag,resinfo;
  USHORT push1,push2,tempax,tempbx,tempcx,temp;
  ULONG tempeax=0,tempebx,tempecx,tempvcfact;

  if(IS_SIS330) {
     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x10);			/* Xabre 1.01.03 */
  } else if(IS_SIS740) {
     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {					/* 740/LVDS */
        SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,0xfb,0x04);      	/* 740/LVDS */
	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x03);
     } else {
        SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x10);			/* 740/301LV 1.10.1i */
     }
  } else {
     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {					/* 650/LVDS */
        SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,0xfb,0x04);      	/* 650/LVDS */
	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x00);			/* 650/LVDS 1.10.07 */
     } else {
        SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2D,0x0f);			/* 650/30xLv 1.10.6s */
     }
  }

  if(ModeNo <= 0x13) {
    modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
    modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  tempax = SiS_Pr->SiS_LCDHDES;
  tempbx = SiS_Pr->SiS_HDE;
  tempcx = SiS_Pr->SiS_HT;

  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)       tempbx = 1024;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) tempbx = 1400;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempbx = 1600;
	else 							      tempbx = 1280;
  }
  tempcx -= tempbx;                        	            	/* HT-HDE  */
  push1 = tempax;
  tempax += tempbx;	                                    	/* lcdhdee  */
  tempbx = SiS_Pr->SiS_HT;
  if(tempax >= tempbx)	tempax -= tempbx;

  push2 = tempax;						/* push ax   lcdhdee  */

  tempcx >>= 2;

  /* TW: 650/30xLV 1.10.6s, 740/LVDS */
  if( ((SiS_Pr->SiS_IF_DEF_LVDS == 0) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) ||
      ((SiS_Pr->SiS_IF_DEF_LVDS == 1) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) ) {
     if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)        tempcx = 0x28;
 	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)  tempcx = 0x18;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) tempcx = 0x30;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempcx = 0x40;
	else                                                          tempcx = 0x30;
     }
  }

  tempcx += tempax;  	                                  	/* lcdhrs  */
  if(tempcx >= tempbx) tempcx -= tempbx;
                                                           	/* v ah,cl  */
  tempax = tempcx;
  tempax >>= 3;   	                                     	/* BPLHRS */
  temp = tempax & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x14,temp);                 	/* Part1_14h  */

  temp += 10;
  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
	   temp += 6;
	   if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel800x600) {
	      temp++;
	      if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1024x768) {
	         temp += 7;
		 if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1600x1200) {
		    temp -= 10;
		 }
	      }
	   }
	}
     }
  }
  temp &= 0x1F;
  temp |= ((tempcx & 0x07) << 5);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x15,temp);                         /* Part1_15h  */

  tempbx = push2;                                          	/* lcdhdee  */
  tempcx = push1;                                          	/* lcdhdes  */
  temp = (tempcx & 0x00FF);
  temp &= 0x07;                                  		/* BPLHDESKEW  */
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1A,temp);                         /* Part1_1Ah  */

  tempcx >>= 3;   	                                     	/* BPLHDES */
  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x16,temp);                         /* Part1_16h  */

  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
     if(tempbx & 0x07) tempbx += 8;
  }
  tempbx >>= 3;                                        		/* BPLHDEE  */
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x17,temp);                        	/* Part1_17h  */

  tempcx = SiS_Pr->SiS_VGAVT;
  tempbx = SiS_Pr->SiS_VGAVDE;
  tempcx -= tempbx; 	                                   	/* GAVT-VGAVDE  */
  tempbx = SiS_Pr->SiS_LCDVDES;                                	/* VGAVDES  */
  push1 = tempbx;                                      		
  if(SiS_Pr->SiS_IF_DEF_TRUMPION == 0) {
    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)        tempax = 768;
    else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)   tempax = 768;
    else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)  tempax = 1024;
    else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)  tempax = 1050;
    else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)  tempax = 1200;
    else                                                           tempax = 960;
  } else tempax = SiS_Pr->SiS_VGAVDE;  /* Trumpion */

  tempbx += tempax;
  tempax = SiS_Pr->SiS_VT;                                    	/* VT  */
  if(tempbx >= tempax)  tempbx -= tempax;

  push2 = tempbx;                                      		
 
  tempcx >>= 2;	

  /* TW: 650/30xLV 1.10.6s, 740/LVDS */
  if( ((SiS_Pr->SiS_IF_DEF_LVDS == 0) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) ||
      ((SiS_Pr->SiS_IF_DEF_LVDS == 1) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) ) {
     if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)         tempcx = 1;
   	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)   tempcx = 3;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)   tempcx = 3;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)  tempcx = 1;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)  tempcx = 1;
	else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)  tempcx = 1;
	else                                                           tempcx = 0x0057;
     }
  }

  tempbx += tempcx;
  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
     tempbx++;                                                	/* BPLVRS  */
  }
  if(tempbx >= tempax)   tempbx -= tempax;
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,temp);                             /* Part1_18h  */

  tempcx >>= 3;
  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
	   if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)         tempcx = 3;
   	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)   tempcx = 5;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768)   tempcx = 5;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024)  tempcx = 5;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)  tempcx = 2;
	   else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)  tempcx = 2;
	}
     }
  }
  tempcx += tempbx;
  tempcx++;                                                	/* BPLVRE  */
  temp = tempcx & 0x00FF;
  temp &= 0x0F;
  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0xF0,temp);
  } else {
     /* TW: 650/30xLV 1.10.6s, Xabre */
     temp |= 0xC0;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0xF0,temp);             /* Part1_19h  */
  }

  temp = (tempbx & 0xFF00) >> 8;
  temp &= 0x07;
  temp <<= 3;  		                               		/* BPLDESKEW =0 */
  tempbx = SiS_Pr->SiS_VGAVDE;
  if(tempbx != SiS_Pr->SiS_VDE)              temp |= 0x40;
  if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA)    temp |= 0x40;
  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
        if(SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00) & 0x01) temp |= 0x80;
     }
  } else {
     if(IS_SIS650) {
        /* TW: 650/30xLV 1.10.6s */
        if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
           if(SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00) & 0x01) temp |= 0x80;
        }
     } else {
	if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)  temp |= 0x80;
     }
  }
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x1A,0x87,temp);            /* Part1_1Ah */

  tempbx = push2;                                      		/* BPLVDEE  */
  tempcx = push1;                                      		/* NPLVDES */
  push1 = (USHORT)(tempeax & 0xFFFF);

  if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) {
      if(resinfo == 7) tempcx++;
    }
  }
  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) {
    tempbx = SiS_Pr->SiS_VGAVDE;
    tempcx = tempbx;
    tempbx--;
  }

  temp = (tempbx & 0xFF00) >> 8;
  temp &= 0x07;
  temp <<= 3;
  temp = temp | (((tempcx & 0xFF00) >> 8) & 0x07);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1D,temp);                          /* Part1_1Dh */

  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1C,temp);                          /* Part1_1Ch  */

  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1B,temp);                          /* Part1_1Bh  */

  tempecx = SiS_Pr->SiS_VGAVT;
  tempebx = SiS_Pr->SiS_VDE;
  tempeax = SiS_Pr->SiS_VGAVDE;
  tempecx -= tempeax;    	                             	/* VGAVT-VGAVDE  */
  tempeax <<= 18;
  temp = (USHORT)(tempeax % tempebx);
  tempeax = tempeax / tempebx;
  if(temp)  tempeax++;
  tempebx = tempeax;                                        	/* BPLVCFACT  */
  tempvcfact = tempeax;
  temp = (USHORT)(tempebx & 0x00FF);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x37,temp);

  temp = (USHORT)((tempebx & 0x00FF00) >> 8);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x36,temp);

  temp = (USHORT)((tempebx & 0x00030000) >> 16);
  if(SiS_Pr->SiS_VDE == SiS_Pr->SiS_VGAVDE) temp |= 0x04;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x35,temp);

  tempecx = SiS_Pr->SiS_VGAHDE;
  if(modeflag & HalfDCLK) tempecx >>= 1;
  tempebx = SiS_Pr->SiS_HDE;
  tempeax = tempecx;
  tempeax <<= 16;
  temp = tempeax % tempebx;
  tempeax = tempeax / tempebx;
  if(temp) tempeax++;
  if(tempebx == tempecx)  tempeax = 0xFFFF;
  tempecx = tempeax;
  tempeax = SiS_Pr->SiS_VGAHDE;
  if(modeflag & HalfDCLK) tempeax >>= 1;
  tempeax <<= 16;
  tempeax = tempeax / tempecx;
  tempecx <<= 16;
  tempeax--;
  tempecx = tempecx | (tempeax & 0xFFFF);
  temp = (USHORT)(tempecx & 0x00FF);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1F,temp);                          /* Part1_1Fh  */

  tempeax = SiS_Pr->SiS_VGAVDE;
  tempeax <<= 18;
  tempeax = tempeax / tempvcfact;
  tempbx = (USHORT)(tempeax & 0x0FFFF);

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) tempbx--;

  if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA)  tempbx = 1;

  temp = ((tempbx & 0xFF00) >> 8) << 3;
  temp = temp | (USHORT)(((tempecx & 0x0000FF00) >> 8) & 0x07);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x20,temp);                         /* Part1_20h */

  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x21,temp);                         /* Part1_21h */

  tempecx >>= 16;   	                                  	/* BPLHCFACT  */
  if(modeflag & HalfDCLK) tempecx >>= 1;
  temp = (USHORT)((tempecx & 0x0000FF00) >> 8);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x22,temp);                         /* Part1_22h */

  temp=(USHORT)(tempecx & 0x000000FF);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x23,temp);

#if 0
  /* TW: Missing code (calles int 2f) (650/302LV 1.10.6s; 1.10.7w doesn't do this) */
  if(xxx()) {
      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x0e,0xda);
  }
#endif

  /* TW: Only for LVDS and 301LV/302LV */
  if((SiS_Pr->SiS_IF_DEF_LVDS == 1) || (SiS_Pr->SiS_VBInfo & VB_SIS301LV302LV)){
  	SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1e,0x20);
  }

  return;
}
#endif  /* SIS 315 */

void SiS_SetCRT2Offset(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,
                       USHORT ModeIdIndex ,USHORT RefreshRateTableIndex,
		       PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT offset;
  UCHAR temp;

  if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) return;

  offset = SiS_GetOffset(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                         HwDeviceExtension);
#if 0
  if(SiS_Pr->LCDResInfo == 13) offset >>= 1;
  if(SiS_Pr->LCDResInfo == 12) offset >>= 1;
#endif			 
  temp = (UCHAR)(offset & 0xFF);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x07,temp);
  temp = (UCHAR)((offset & 0xFF00) >> 8);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x09,temp);
  temp = (UCHAR)(((offset >> 3) & 0xFF) + 1);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x03,temp);
}

USHORT
SiS_GetOffset(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
              USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,colordepth;
  USHORT modeinfo,index,infoflag;

  if(SiS_Pr->UseCustomMode) {
     infoflag = SiS_Pr->CInfoFlag;
     temp = SiS_Pr->CHDisplay / 16;
  } else {
     infoflag = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag;
     modeinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeInfo;
  
     if(HwDeviceExtension->jChipType < SIS_315H ) {
    	index = (modeinfo >> 4) & 0xFF;
     } else {
    	index = (modeinfo >> 8) & 0xFF;
     }

     temp = SiS_Pr->SiS_ScreenOffset[index];
  }
  
  colordepth = SiS_GetColorDepth(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex);

  if(infoflag & InterlaceMode) temp <<= 1;

  temp *= colordepth;

  /* TW: For 1400x1050 and 856x480 */
  if( ( ((ModeNo >= 0x26) && (ModeNo <= 0x28)) || 
        ModeNo == 0x3f || 
	ModeNo == 0x42 || 
	ModeNo == 0x45 ) ||
      (SiS_Pr->UseCustomMode && (SiS_Pr->CHDisplay % 16)) ) {
        colordepth >>= 1;
	temp += colordepth;
  }

  return(temp);
}

USHORT
SiS_GetColorDepth(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT ColorDepth[6] = { 1, 2, 4, 4, 6, 8};
  SHORT  index;
  USHORT modeflag;

  if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
  } else {
     if(ModeNo <= 0x13)
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     else
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }	

  index = (modeflag & ModeInfoFlag) - ModeEGA;
  if(index < 0) index = 0;
  return(ColorDepth[index]);
}

void
SiS_SetCRT2Sync(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempah=0,tempbl,infoflag,flag;

  flag = 0;
  tempbl = 0xC0;

  infoflag = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag;

  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {					/* LVDS */

    if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
       tempah = 0;
    } else if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) && (SiS_Pr->SiS_LCDInfo & LCDSync)) {
       tempah = SiS_Pr->SiS_LCDInfo;
    } else tempah = infoflag >> 8;
    
    tempah &= 0xC0;
    
    tempah |= 0x20;
    if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;

    if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
       if(HwDeviceExtension->jChipType >= SIS_315H) {
          tempah >>= 3;
          SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,0xE7,tempah);
       }
    } else {
       SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);
    }

  } else {

     if(HwDeviceExtension->jChipType < SIS_315H) {

#ifdef SIS300  /* ---- 300 series --- */

        if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {			/* 630 - 301B(-DH) */

            if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
               tempah = SiS_Pr->SiS_LCDInfo;
	       if(SiS_Pr->SiS_LCDInfo & LCDSync) {
                  flag = 1;
               }
            }
            if(flag != 1) tempah = infoflag >> 8;
            tempah &= 0xC0;
	    
            tempah |= 0x20;
            if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;

            if (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) {
	       	/* TW: BIOS does something here @@@ */
            }

 	    tempah &= 0x3f;
  	    tempah |= tempbl;
            SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);

         } else {							/* 630 - 301 */

            if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
               tempah = SiS_Pr->SiS_LCDInfo;
	       if(SiS_Pr->SiS_LCDInfo & DontExpandLCDShift) { /* ! */
	          flag = 1;
	       }
            }
            if(flag != 1) tempah = infoflag >> 8;
            tempah &= 0xC0;
            tempah |= 0x30;
            SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x3F,tempah);

         }

#endif /* SIS300 */

      } else {

#ifdef SIS315H  /* ----- 310/325 series ---- */

         if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {	  		/* 310/325 - 30xLV */

            tempah = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x37);
            tempah &= 0xC0;
            tempah |= 0x20;
            if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;
            SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);

         } else {							/* 310/325 - 301, 301B */

            tempah = infoflag >> 8;
            tempah &= 0xC0;
	    if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	       if(SiS_Pr->SiS_LCDInfo & LCDSync) {
	          tempah = SiS_Pr->SiS_LCDInfo;
	          tempah &= 0xC0;
	       }
	    }
	    
            tempah |= 0x20;
            if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;
#if 0
            if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) {
		/* TW: BIOS does something here @@@ */
            }
#endif	    
            SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);

         } 
	 
#endif  /* SIS315H */
      }
   }
}

/* TW: Set CRT2 FIFO on 300/630/730 */
#ifdef SIS300
void
SiS_SetCRT2FIFO_300(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,
                    PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,index;
  USHORT modeidindex,refreshratetableindex;
  USHORT VCLK=0,MCLK,colorth=0,data2=0;
  USHORT tempal, tempah, tempbx, tempcl, tempax;
  USHORT CRT1ModeNo,CRT2ModeNo;
  USHORT SelectRate_backup;
  ULONG  data,eax;
  const UCHAR  LatencyFactor[] = {
  	97, 88, 86, 79, 77, 00,       /*; 64  bit    BQ=2   */
        00, 87, 85, 78, 76, 54,       /*; 64  bit    BQ=1   */
        97, 88, 86, 79, 77, 00,       /*; 128 bit    BQ=2   */
        00, 79, 77, 70, 68, 48,       /*; 128 bit    BQ=1   */
        80, 72, 69, 63, 61, 00,       /*; 64  bit    BQ=2   */
        00, 70, 68, 61, 59, 37,       /*; 64  bit    BQ=1   */
        86, 77, 75, 68, 66, 00,       /*; 128 bit    BQ=2   */
        00, 68, 66, 59, 57, 37        /*; 128 bit    BQ=1   */
  };
  const UCHAR  LatencyFactor730[] = {
         69, 63, 61, 
	 86, 79, 77,
	103, 96, 94,
	120,113,111,
	137,130,128,    /* <-- last entry, data below */
	137,130,128,	/* to avoid using illegal values */
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
  };
  const UCHAR ThLowB[]   = {
  	81, 4, 72, 6, 88, 8,120,12,
        55, 4, 54, 6, 66, 8, 90,12,
        42, 4, 45, 6, 55, 8, 75,12
  };
  const UCHAR ThTiming[] = {
  	1, 2, 2, 3, 0, 1, 1, 2
  };
  
  SelectRate_backup = SiS_Pr->SiS_SelectCRT2Rate;

  if(!SiS_Pr->CRT1UsesCustomMode) {
  
     CRT1ModeNo = SiS_Pr->SiS_CRT1Mode;                                 /* get CRT1 ModeNo */
     SiS_SearchModeID(SiS_Pr,ROMAddr,&CRT1ModeNo,&modeidindex);
     SiS_Pr->SiS_SetFlag &= (~ProgrammingCRT2);
     SiS_Pr->SiS_SelectCRT2Rate = 0;
     refreshratetableindex = SiS_GetRatePtrCRT2(SiS_Pr,ROMAddr,CRT1ModeNo,
						modeidindex,HwDeviceExtension);

     if(CRT1ModeNo >= 0x13) {
       index = SiS_Pr->SiS_RefIndex[refreshratetableindex].Ext_CRTVCLK;
       index &= 0x3F;
       VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;			/* Get VCLK */
       data2 = SiS_Pr->SiS_ModeType - 2;
     }
     
  } else {
  
     CRT1ModeNo = 0xfe;
     VCLK = SiS_Pr->CSRClock;						/* Get VCLK */
     data2 = (SiS_Pr->CModeFlag & ModeInfoFlag) - 2;
  
  }			
     
  if(CRT1ModeNo >= 0x13) {
    if(HwDeviceExtension->jChipType == SIS_300) {
       index = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x3A);
    } else {
       index = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x1A);
    }
    index &= 0x07;
    MCLK = SiS_Pr->SiS_MCLKData_0[index].CLOCK;				/* Get MCLK */
    
#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "FIFO2: CRT1Mode 0x%x VCLK %d MCLK %d modetype-2 = %d\n",
    	CRT1ModeNo, VCLK, MCLK, data2);
#endif  
  
    switch(data2) {							/* Get color depth */
      case 0 : 	colorth = 1; break;
      case 1 : 	colorth = 1; break;
      case 2 : 	colorth = 2; break;
      case 3 : 	colorth = 2; break;
      case 4 : 	colorth = 3; break;
      case 5 : 	colorth = 4; break;
      default:  colorth = 2; break;
    }
    data2 = (colorth * VCLK) / MCLK;  

    temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x14);
    temp = ((temp & 0x00FF) >> 6) << 1;
    if(temp == 0) temp = 1;
    temp <<= 2;
    temp &= 0xff;

    data2 = temp - data2;
    
#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "FIFO2: data2 (step1) = %d\n",
    	data2);
#endif    

    if((28 * 16) % data2) {
      	data2 = (28 * 16) / data2;
      	data2++;
    } else {
      	data2 = (28 * 16) / data2;
    }
    
#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "FIFO2: data2 (step2) = %d\n",
    	data2);
#endif

    if(HwDeviceExtension->jChipType == SIS_300) {

	tempah = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x18);
	tempah &= 0x62;
	tempah >>= 1;
	tempal = tempah;
	tempah >>= 3;
	tempal |= tempah;
	tempal &= 0x07;
	tempcl = ThTiming[tempal];
	tempbx = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x16);
	tempbx >>= 6;
	tempah = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x14);
	tempah >>= 4;
	tempah &= 0x0c;
	tempbx |= tempah;
	tempbx <<= 1;
	tempal = ThLowB[tempbx + 1];
	tempal *= tempcl;
	tempal += ThLowB[tempbx];
	data = tempal;

    } else if(HwDeviceExtension->jChipType == SIS_730) {
       
#ifndef LINUX_XF86
       SiS_SetReg4(0xcf8,0x80000050);
       eax = SiS_GetReg3(0xcfc);
#else
       eax = pciReadLong(0x00000000, 0x50);
#endif
       tempal = (USHORT)(eax >> 8);
       tempal &= 0x06;
       tempal <<= 5;

#ifndef LINUX_XF86
       SiS_SetReg4(0xcf8,0x800000A0);
       eax = SiS_GetReg3(0xcfc);
#else
       eax = pciReadLong(0x00000000, 0xA0);
#endif
       temp = (USHORT)(eax >> 28);
       temp &= 0x0F;   
       tempal |= temp;

#ifdef TWDEBUG
       xf86DrvMsg(0, X_INFO, "FIFO2: Latencyfactorindex = 0x%x\n", tempal);
#endif
      
       tempbx = tempal;   /* BIOS BUG (2.04.5d, 2.04.6a use ah here, which is unset!) */
       tempbx = 0;        /* -- do it like the BIOS anyway... */
       tempax = tempbx;
       tempbx &= 0xc0;
       tempbx >>= 6;
       tempax &= 0x0f;
       tempax *= 3;
       tempbx += tempax;
       
       data = LatencyFactor730[tempbx];
       data += 15;
       temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x14);
       if(!(temp & 0x80)) data += 5;
       
    } else {

       index = 0;
       temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x14);
       if(temp & 0x0080) index += 12;

#ifndef LINUX_XF86
       SiS_SetReg4(0xcf8,0x800000A0);
       eax = SiS_GetReg3(0xcfc);
#else
       /* TW: We use pci functions X offers. We use tag 0, because
        * we want to read/write to the host bridge (which is always
        * 00:00.0 on 630, 730 and 540), not the VGA device.
        */
       eax = pciReadLong(0x00000000, 0xA0);
#endif
       temp = (USHORT)(eax >> 24);
       if(!(temp&0x01)) index += 24;

#ifndef LINUX_XF86
       SiS_SetReg4(0xcf8,0x80000050);
       eax = SiS_GetReg3(0xcfc);
#else
       eax = pciReadLong(0x00000000, 0x50);
#endif
       temp=(USHORT)(eax >> 24);
       if(temp & 0x01) index += 6;

       temp = (temp & 0x0F) >> 1;
       index += temp;
       
       data = LatencyFactor[index];
       data += 15;
       temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x14);
       if(!(temp & 0x80)) data += 5;
    }
    
#ifdef TWDEBUG    
    xf86DrvMsg(0, X_INFO, "FIFO2: latencyfactor (CRT1) = %d\n", data);
#endif

    data += data2;				/* CRT1 Request Period */
    
#ifdef TWDEBUG    
    xf86DrvMsg(0, X_INFO, "FIFO2: CRT1 request period = %d\n", data);
#endif

    CRT2ModeNo = ModeNo;
    SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
    SiS_Pr->SiS_SelectCRT2Rate = SelectRate_backup;
    SiS_SearchModeID(SiS_Pr,ROMAddr,&CRT2ModeNo,&modeidindex);    

    refreshratetableindex = SiS_GetRatePtrCRT2(SiS_Pr,ROMAddr,CRT2ModeNo,
                                               modeidindex,HwDeviceExtension);

    index = SiS_GetVCLK2Ptr(SiS_Pr,ROMAddr,CRT2ModeNo,modeidindex,
                            refreshratetableindex,HwDeviceExtension);
    VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;                         	/* Get VCLK  */
    
    data2 = SiS_Pr->SiS_ModeType - 2;
    switch(data2) {							/* Get color depth */
      case 0 : 	colorth = 1; break;
      case 1 : 	colorth = 1; break;
      case 2 : 	colorth = 2; break;
      case 3 : 	colorth = 2; break;
      case 4 : 	colorth = 3; break;
      case 5 : 	colorth = 4; break;
      default:  colorth = 2; break;
    }
    
#ifdef TWDEBUG    
    xf86DrvMsg(0, X_INFO, "FIFO2: CRT2Mode 0x%x VCLK %d MCLK %d modetype-2 = %d, colorth %d\n",
    	CRT2ModeNo, VCLK, MCLK, data2, colorth);
#endif

    data = data * VCLK * colorth;
    if(data % (MCLK << 4)) {
      	data = data / (MCLK << 4);
      	data++;
    } else {
      	data = data / (MCLK << 4);
    }
    
#ifdef TWDEBUG    
    xf86DrvMsg(0, X_INFO, "FIFO2: data (unclipped) = 0x%x\n", data);
#endif    
    
    if(data <= 6) data = 6;
    if(data > 0x14) data = 0x14;

    temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x01);
    if(HwDeviceExtension->jChipType == SIS_300) {
       if(data <= 0x0f) temp = (temp & (~0x1F)) | 0x13;
       else             temp = (temp & (~0x1F)) | 0x16;
       if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
       		temp = (temp & (~0x1F)) | 0x13;
       }
    } else {
       if( ( (HwDeviceExtension->jChipType == SIS_630) ||
             (HwDeviceExtension->jChipType == SIS_730) )  &&
           (HwDeviceExtension->jChipRevision >= 0x30) ) /* 630s or 730(s?) */
      {
	  temp = (temp & (~0x1F)) | 0x1b;
      } else {
	  temp = (temp & (~0x1F)) | 0x16;
      }
    }
    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x01,0xe0,temp);

    if( (HwDeviceExtension->jChipType == SIS_630) &&
        (HwDeviceExtension->jChipRevision >= 0x30) ) /* 630s, NOT 730 */
    {
   	if(data > 0x13) data = 0x13;
    }
    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x02,0xe0,data);
    
  } else {  /* If mode <= 0x13, we just restore everything */
  
    SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
    SiS_Pr->SiS_SelectCRT2Rate = SelectRate_backup;
    
  }
}
#endif

/* TW: Set FIFO on 310/325/330 series */
#ifdef SIS315H
void
SiS_SetCRT2FIFO_310(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,
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
  
  SelectRate_backup = SiS_Pr->SiS_SelectCRT2Rate;
  
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x01,0x3B);

  if(!SiS_Pr->CRT1UsesCustomMode) {
  
     CRT1ModeNo = SiS_Pr->SiS_CRT1Mode;                                 /* get CRT1 ModeNo */
     SiS_SearchModeID(SiS_Pr,ROMAddr,&CRT1ModeNo,&ModeIdIndex);

     SiS_Pr->SiS_SetFlag &= (~ProgrammingCRT2);   
     SiS_Pr->SiS_SelectCRT2Rate = 0;

     /* Get REFIndex for crt1 refreshrate */
     RefreshRateTableIndex = SiS_GetRatePtrCRT2(SiS_Pr,ROMAddr,CRT1ModeNo,
                                             ModeIdIndex,HwDeviceExtension);

     index = SiS_GetVCLK2Ptr(SiS_Pr,ROMAddr,CRT1ModeNo,ModeIdIndex,
                          RefreshRateTableIndex,HwDeviceExtension);
     tempax = SiS_Pr->SiS_VCLKData[index].CLOCK;                        /* Get VCLK */
     
     tempbx = SiS_GetColorDepth(SiS_Pr,ROMAddr,CRT1ModeNo,ModeIdIndex); /* Get colordepth */
     tempbx >>= 1;
     if(!tempbx) tempbx++; 
     
  } else {
  
     tempax = SiS_Pr->CSRClock;						/* Get VCLK */
     tempbx = (SiS_Pr->CModeFlag & ModeInfoFlag) - 2;
     switch(tempbx) {							/* Get color depth */
       case 0 : 	tempbx = 1; break;
       case 1 : 	tempbx = 1; break;
       case 2 : 	tempbx = 2; break;
       case 3 : 	tempbx = 2; break;
       case 4 : 	tempbx = 3; break;
       case 5 : 	tempbx = 4; break;
       default:  	tempbx = 2; break;
     }
  
  }
    
  tempax *= tempbx;

  tempbx = SiS_GetMCLK(SiS_Pr,ROMAddr, HwDeviceExtension);     		/* Get MCLK */

  tempax /= tempbx;

  tempbx = tempax;

#if 0 /* TW: BIOS code is skrewed */
  if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x14) & 0x02) {
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
  tempcl = (SiS_GetReg1(SiS_Pr->SiS_P3c4,0x17) >> 3) & 0x7;     /* SR17[5:3] DRAM Queue depth */
  tempch = (SiS_GetReg1(SiS_Pr->SiS_P3c4,0x17) >> 6) & 0x3;     /* SR17[7:6] DRAM Grant length */

  for (temp3 = 0; temp3 < 16; temp3 += 2) {
    if ((CombCode[temp3] == tempcl) && (CombCode[temp3+1] == tempch)) {
      temp3 = CRT2ThLow[temp3 >> 1];
    }
  }

  tempcx +=  temp3;                                      /* CRT1 Request Period */

  CRT2ModeNo = ModeNo;                                                 /* get CRT2 ModeNo */
  SiS_SearchModeID(SiS_Pr,ROMAddr,&CRT2ModeNo,&ModeIdIndex);           /* Get ModeID Table */

  SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
  SiS_Pr->SiS_SelectCRT2Rate = SelectRate_backup;

  RefreshRateTableIndex = SiS_GetRatePtrCRT2(SiS_Pr,ROMAddr,CRT2ModeNo,
                                           ModeIdIndex,HwDeviceExtension);

  index = SiS_GetVCLK2Ptr(SiS_Pr,ROMAddr,CRT2ModeNo,ModeIdIndex,
                          RefreshRateTableIndex,HwDeviceExtension);
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
     tempax = SiS_Pr->SiS_VCLKData[index].CLOCK;                       /* Get VCLK  */
  } else {
     tempax = SiS_Pr->SiS_VBVCLKData[index].CLOCK;                     /* Get VCLK  */
  }

  tempbx = SiS_GetColorDepth(SiS_Pr,ROMAddr,CRT2ModeNo,ModeIdIndex);   /* Get colordepth */
  tempbx >>= 1;
  if(!tempbx) tempbx++;

  tempax *= tempbx;

  tempax *= tempcx;

  tempbx = SiS_GetMCLK(SiS_Pr,ROMAddr, HwDeviceExtension);	       /* Get MCLK */
  tempbx <<= 4;

  tempcx = tempax;
  tempax /= tempbx;
  if(tempcx % tempbx) tempax++;		/* CRT1 Request period * TCLK * BytePerPixel / (MCLK*16) */

  if (tempax > 0x37)  tempax = 0x37;

  /* TW: 650/LVDS (1.10.07, 1.10.00), 650/301LV, 740, 330 overrule calculated value; 315 does not */
  if(HwDeviceExtension->jChipType >= SIS_650) {
  	tempax = 0x04;
  }
  
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x02,~0x3F,tempax);
}

USHORT
SiS_GetMCLK(SiS_Private *SiS_Pr, UCHAR *ROMAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT index;

  index = SiS_Get310DRAMType(SiS_Pr,ROMAddr,HwDeviceExtension);
  if(index >= 4) {
    index -= 4;
    return(SiS_Pr->SiS_MCLKData_1[index].CLOCK);
  } else {
    return(SiS_Pr->SiS_MCLKData_0[index].CLOCK);
  }
}
#endif

/* TW: Checked against 650/LVDS 1.10.07 BIOS */
void
SiS_GetLVDSDesData(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex,
		   PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT modeflag;
  USHORT PanelIndex,ResIndex;
  const  SiS_LVDSDesStruct *PanelDesPtr = NULL;

  if((SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) ) {

#ifdef SIS315H  
     SiS_GetLVDSDesPtrA(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                        &PanelIndex,&ResIndex);
			
     switch (PanelIndex)
     {
     	case  0: PanelDesPtr = SiS_Pr->LVDS1024x768Des_1;   break;  /* --- expanding --- */
     	case  1: PanelDesPtr = SiS_Pr->LVDS1280x1024Des_1;  break;
	case  2: PanelDesPtr = SiS_Pr->LVDS1400x1050Des_1;  break;
	case  3: PanelDesPtr = SiS_Pr->LVDS1600x1200Des_1;  break;
     	case  4: PanelDesPtr = SiS_Pr->LVDS1024x768Des_2;   break;  /* --- non expanding --- */
     	case  5: PanelDesPtr = SiS_Pr->LVDS1280x1024Des_2;  break;
	case  6: PanelDesPtr = SiS_Pr->LVDS1400x1050Des_2;  break;
	case  7: PanelDesPtr = SiS_Pr->LVDS1600x1200Des_2;  break;
	default: PanelDesPtr = SiS_Pr->LVDS1024x768Des_1;   break;
     }
#endif

  } else {

     SiS_GetLVDSDesPtr(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                       &PanelIndex,&ResIndex,HwDeviceExtension);

     switch (PanelIndex)
     {
     	case  0: PanelDesPtr = SiS_Pr->SiS_PanelType00_1;   break;   /* ---  */
     	case  1: PanelDesPtr = SiS_Pr->SiS_PanelType01_1;   break;
     	case  2: PanelDesPtr = SiS_Pr->SiS_PanelType02_1;   break;
     	case  3: PanelDesPtr = SiS_Pr->SiS_PanelType03_1;   break;
     	case  4: PanelDesPtr = SiS_Pr->SiS_PanelType04_1;   break;
     	case  5: PanelDesPtr = SiS_Pr->SiS_PanelType05_1;   break;
     	case  6: PanelDesPtr = SiS_Pr->SiS_PanelType06_1;   break;
     	case  7: PanelDesPtr = SiS_Pr->SiS_PanelType07_1;   break;
     	case  8: PanelDesPtr = SiS_Pr->SiS_PanelType08_1;   break;
     	case  9: PanelDesPtr = SiS_Pr->SiS_PanelType09_1;   break;
     	case 10: PanelDesPtr = SiS_Pr->SiS_PanelType0a_1;   break;
     	case 11: PanelDesPtr = SiS_Pr->SiS_PanelType0b_1;   break;
     	case 12: PanelDesPtr = SiS_Pr->SiS_PanelType0c_1;   break;
     	case 13: PanelDesPtr = SiS_Pr->SiS_PanelType0d_1;   break;
     	case 14: PanelDesPtr = SiS_Pr->SiS_PanelType0e_1;   break;
     	case 15: PanelDesPtr = SiS_Pr->SiS_PanelType0f_1;   break;
     	case 16: PanelDesPtr = SiS_Pr->SiS_PanelType00_2;   break;    /* --- */
     	case 17: PanelDesPtr = SiS_Pr->SiS_PanelType01_2;   break;
     	case 18: PanelDesPtr = SiS_Pr->SiS_PanelType02_2;   break;
     	case 19: PanelDesPtr = SiS_Pr->SiS_PanelType03_2;   break;
     	case 20: PanelDesPtr = SiS_Pr->SiS_PanelType04_2;   break;
     	case 21: PanelDesPtr = SiS_Pr->SiS_PanelType05_2;   break;
     	case 22: PanelDesPtr = SiS_Pr->SiS_PanelType06_2;   break;
     	case 23: PanelDesPtr = SiS_Pr->SiS_PanelType07_2;   break;
     	case 24: PanelDesPtr = SiS_Pr->SiS_PanelType08_2;   break;
     	case 25: PanelDesPtr = SiS_Pr->SiS_PanelType09_2;   break;
     	case 26: PanelDesPtr = SiS_Pr->SiS_PanelType0a_2;   break;
     	case 27: PanelDesPtr = SiS_Pr->SiS_PanelType0b_2;   break;
     	case 28: PanelDesPtr = SiS_Pr->SiS_PanelType0c_2;   break;
     	case 29: PanelDesPtr = SiS_Pr->SiS_PanelType0d_2;   break;
     	case 30: PanelDesPtr = SiS_Pr->SiS_PanelType0e_2;   break;
     	case 31: PanelDesPtr = SiS_Pr->SiS_PanelType0f_2;   break;
	case 32: PanelDesPtr = SiS_Pr->SiS_PanelTypeNS_1;   break;    /* pass 1:1 */
	case 33: PanelDesPtr = SiS_Pr->SiS_PanelTypeNS_2;   break;
     	case 50: PanelDesPtr = SiS_Pr->SiS_CHTVUNTSCDesData;   break; /* TV */
     	case 51: PanelDesPtr = SiS_Pr->SiS_CHTVONTSCDesData;   break;
     	case 52: PanelDesPtr = SiS_Pr->SiS_CHTVUPALDesData;    break;
     	case 53: PanelDesPtr = SiS_Pr->SiS_CHTVOPALDesData;    break;
	default:
		if(HwDeviceExtension->jChipType < SIS_315H)
		   PanelDesPtr = SiS_Pr->SiS_PanelType0e_1;
		else
		   PanelDesPtr = SiS_Pr->SiS_PanelType01_1;
		break;
     }
  }
  SiS_Pr->SiS_LCDHDES = (PanelDesPtr+ResIndex)->LCDHDES;
  SiS_Pr->SiS_LCDVDES = (PanelDesPtr+ResIndex)->LCDVDES;

  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD){
     if((SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
        if(ModeNo <= 0x13) {
           modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	   if(!(modeflag & HalfDCLK)) {
	      SiS_Pr->SiS_LCDHDES = 632;
	   }
        }
     } else {
        if(!(SiS_Pr->SiS_SetFlag & SetDOSMode)) {
           if( (HwDeviceExtension->jChipType < SIS_315H) || 
	       (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x1024) ) {  
              if(SiS_Pr->SiS_LCDResInfo >= SiS_Pr->SiS_Panel1024x768){
                 if(ModeNo <= 0x13) {
	            modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	            if(HwDeviceExtension->jChipType < SIS_315H) {
	               if(!(modeflag & HalfDCLK)) SiS_Pr->SiS_LCDHDES = 320;
	            } else {
	               if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)
	                  SiS_Pr->SiS_LCDHDES = 480;
                       if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)
	                  SiS_Pr->SiS_LCDHDES = 804;
		       if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)
	                  SiS_Pr->SiS_LCDHDES = 704;
                       if(!(modeflag & HalfDCLK)) {
                          SiS_Pr->SiS_LCDHDES = 320;
	                  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)
	                     SiS_Pr->SiS_LCDHDES = 632;
		          else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)
	                     SiS_Pr->SiS_LCDHDES = 542;
                       }
                    }
                 }
              }
           }
        }
     }
  }
  return;
}

void
SiS_GetLVDSDesPtr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                  USHORT RefreshRateTableIndex,USHORT *PanelIndex,
		  USHORT *ResIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempbx,tempal,modeflag;

  if(ModeNo <= 0x13) {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  tempbx = 0;
  if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
     if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
        tempbx = 50;
        if((SiS_Pr->SiS_VBInfo & SetPALTV) && (!SiS_Pr->SiS_CHPALM)) tempbx += 2;
        if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) tempbx += 1;
        /* TW: Nothing special needed for SOverscan    */
        /*     PALM uses NTSC data, PALN uses PAL data */
     }
  }
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
     tempbx = SiS_Pr->SiS_LCDTypeInfo;
     if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 16;
     if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
        tempbx = 32;
        if(modeflag & HalfDCLK) tempbx++;
     }
  }
  /* TW: 630/LVDS and 650/LVDS (1.10.07) BIOS */
  if(SiS_Pr->SiS_SetFlag & SetDOSMode) {
     if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480)  {
        tempal = 0x07;
        if(HwDeviceExtension->jChipType < SIS_315H) {
           if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x13) & 0x80) tempal++;
        }
     }
  }

  *PanelIndex = tempbx;
  *ResIndex = tempal & 0x1F;
}

#ifdef SIS315H
void
SiS_GetLVDSDesPtrA(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex,USHORT *PanelIndex,USHORT *ResIndex)
{
  USHORT tempbx=0,tempal;

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)      tempbx = 2;
  else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempbx = 3;
  else tempbx = SiS_Pr->SiS_LCDResInfo - SiS_Pr->SiS_PanelMinLVDS;

  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx += 4;

  if(ModeNo <= 0x13)
     tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
     tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  *PanelIndex = tempbx;
  *ResIndex = tempal & 0x1F;
}
#endif

void
SiS_SetCRT2ModeRegs(SiS_Private *SiS_Pr, USHORT BaseAddr, USHORT ModeNo, USHORT ModeIdIndex,
                    PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT i,j,modeflag;
  USHORT tempcl,tempah=0;
#ifdef SIS300
  USHORT temp;
#endif
#ifdef SIS315H
  USHORT tempbl;
#endif

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
     if(SiS_Pr->UseCustomMode) {
        modeflag = SiS_Pr->CModeFlag;
     } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     }
  }
  
  /* TW: BIOS does not do this (neither 301 nor LVDS) */
  /*     (But it's harmless; see SetCRT2Offset) */
  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x03,0x00);   /* fix write part1 index 0  BTDRAM bit Bug */

  /* TW: Removed 301B302B301LV302LV check here to match 650/LVDS BIOS */
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {

	/* TW:   1. for LVDS/302B/302LV **LCDA** */

      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x00,0xAF,0x40); /* FUNCTION CONTROL */
      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2E,0xF7);

  } else {

    for(i=0,j=4; i<3; i++,j++) SiS_SetReg1(SiS_Pr->SiS_Part1Port,j,0);

    tempcl = SiS_Pr->SiS_ModeType;

    if(HwDeviceExtension->jChipType < SIS_315H) {

#ifdef SIS300    /* ---- 300 series ---- */

      /* For 301BDH: */
      if(SiS_Pr->SiS_VBType & VB_NoLCD) {
	  temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x32);
	  temp &= 0xef;
	  temp |= 0x02;
	  if((SiS_Pr->SiS_VBInfo & SetCRT2ToTV) || (SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC)) {
	     temp |= 0x10;
	     temp &= 0xfd;
	  }
	  SiS_SetReg1(SiS_Pr->SiS_P3c4,0x32,temp);
      }

      if(ModeNo > 0x13) {
        tempcl -= ModeVGA;
        if((tempcl > 0) || (tempcl == 0)) {      /* TW: tempcl is USHORT -> always true! */
           tempah = ((0x10 >> tempcl) | 0x80);
        }
      } else tempah = 0x80;

      if(SiS_Pr->SiS_VBInfo & SetInSlaveMode)  tempah ^= 0xA0;

#endif  /* SIS300 */

    } else {

#ifdef SIS315H    /* ---- 310/325/330 series ---- */

      if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
         if(SiS_Pr->SiS_VBInfo & CRT2DisplayFlag) {
	    SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2e,0x08);
         }
      }

      if(ModeNo > 0x13) {
        tempcl -= ModeVGA;
        if((tempcl > 0) || (tempcl == 0)) {  /* TW: tempcl is USHORT -> always true! */
           tempah = (0x08 >> tempcl);
           if (tempah == 0) tempah = 1;
           tempah |= 0x40;
        }
      } else tempah = 0x40;

      if(SiS_Pr->SiS_VBInfo & SetInSlaveMode)  tempah ^= 0x50;

#endif  /* SIS315H */

    }

    if(SiS_Pr->SiS_VBInfo & CRT2DisplayFlag)  tempah = 0;

    if(HwDeviceExtension->jChipType < SIS_315H) {
       SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x00,tempah);  		/* FUNCTION CONTROL */
    } else {
       if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
          SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x00,0xa0,tempah);  	/* FUNCTION CONTROL */
       } else {
          if(IS_SIS740) {
	     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x00,tempah);  		/* FUNCTION CONTROL */
	  } else {
             SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x00,0xa0,tempah);  	/* FUNCTION CONTROL */
	  }
       }
    }

    if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {

	/* TW:   2. for 301 (301B, 302B 301LV, 302LV non-LCDA) */

    	tempah = 0x01;
    	if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
      		tempah |= 0x02;
    	}
    	if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC)) {
      		tempah ^= 0x05;
      		if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
        		tempah ^= 0x01;
      		}
    	}

	if(SiS_Pr->SiS_VBInfo & CRT2DisplayFlag)  tempah = 0;

    	if(HwDeviceExtension->jChipType < SIS_315H) {

		/* --- 300 series --- */

      		tempah = (tempah << 5) & 0xFF;
      		SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x01,tempah);
      		tempah = (tempah >> 5) & 0xFF;

    	} else {

		/* --- 310 series --- */

      		SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2E,0xF8,tempah);

    	}

    	if((SiS_Pr->SiS_ModeType == ModeVGA) && (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode))) {
      		tempah |= 0x10;
	}

	/* TW: 630/301 BIOS */
	if((HwDeviceExtension->jChipType < SIS_315H) && (SiS_Pr->SiS_VBType & VB_SIS301)) {
		if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
			tempah |= 0x80;
		}
	} else {
		tempah |= 0x80;
	}

    	if(SiS_Pr->SiS_VBInfo & (SetCRT2ToTV - SetCRT2ToHiVisionTV)) {
      		if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
			if(!(SiS_Pr->SiS_HiVision & 0x03)) {
          			tempah |= 0x20;
			}
      		}
    	}

    	SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x0D,0x40,tempah);

    	tempah = 0;
    	if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	   if(!(SiS_Pr->SiS_HiVision & 0x03)) {
      	      if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	         if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV)) {
       		    if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
            	       SiS_Pr->SiS_SetFlag |= RPLLDIV2XO;
            	       tempah |= 0x40;
       		    } else {
        	       if(!(SiS_Pr->SiS_SetFlag & TVSimuMode)) {
          		  SiS_Pr->SiS_SetFlag |= RPLLDIV2XO;
            		  tempah |= 0x40;
        	       }
      		    }
		 }
     	      } else {
        	SiS_Pr->SiS_SetFlag |= RPLLDIV2XO;
        	tempah |= 0x40;
      	      }
	   }
    	}
	/* TW: For 302LV dual-channel */
	if(HwDeviceExtension->jChipType >= SIS_315H) {
	    if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
	        if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x39) & 0x04)
		    tempah |= 0x40;
	    }
	}

	if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) ||
	   (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960)) {
		tempah |= 0x80;
	}

    	SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x0C,tempah);

    } else {

    	/* TW: 3. for LVDS */

	if(HwDeviceExtension->jChipType >= SIS_315H) {

	   /* TW: Inserted this entire section (BIOS 650/LVDS); added ModeType check
	    *     (LVDS can only be slave in 8bpp modes)
	    */
	   tempah = 0x80;
	   if( (modeflag & CRT2Mode) && (SiS_Pr->SiS_ModeType > ModeVGA) ) {
	       if (SiS_Pr->SiS_VBInfo & DriverMode) {
	           tempah |= 0x02;
	       }
	   }

	   if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
               tempah |= 0x02;
    	   }

	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	       tempah ^= 0x01;
	   }

	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
	       tempah = 1;
	   }

    	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2e,0xF0,tempah);

	} else {

	   /* TW: (added ModeType check) */
	   tempah = 0;
	   if( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) && (SiS_Pr->SiS_ModeType > ModeVGA) ) {
               	  tempah |= 0x02;
    	   }
	   tempah <<= 5;

	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display)  tempah = 0;

	   SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x01,tempah);

	}

    }

  }

  /* TW: Inserted the entire following section */

  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {

      if(HwDeviceExtension->jChipType >= SIS_315H) {

#ifdef SIS315H
         if(!(IS_SIS740)) {
            tempah = 0x04;						   /* For all bridges */
            tempbl = 0xfb;
            if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
               tempah = 0x00;
	       if(SiS_IsDualEdge(SiS_Pr, HwDeviceExtension, BaseAddr))
	          tempbl = 0xff;
            }
            SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,tempbl,tempah);   
	 }
	 
	 if(IS_SIS740) {						
	    tempah = 0x30;
	    tempbl = 0xcf;
	    if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
	       tempah = 0x00;
	    }
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2c,tempbl,tempah);    
	 } else {
	    /* TW: This in order to fix "TV-blue-bug" on 315+301 */
            if(SiS_Pr->SiS_VBType & VB_SIS301) {
	       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2c,0xCF);             /* For 301   */
	    } else {
	       if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	          SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2c,0xCF,0x30);   /* For 30xLV */
	       } else {
	          tempah = 0x30;					   /* For 301B  */
	          tempbl = 0xcf;
	          if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
	             tempah = 0x00;
		     if(SiS_IsDualEdge(SiS_Pr, HwDeviceExtension, BaseAddr)) {
		        tempbl = 0xff;
		     }
	          }
	          SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2c,tempbl,tempah);    
	       }
	    }
	 }

	 if(IS_SIS740) {
	    tempah = 0xc0;
  	    tempbl = 0x3f;
	    if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
	       tempah = 0x00;
	    } 
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x21,tempbl,tempah);     
	 } else {
	    if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {			/* For 30xLV */
	       SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x21,0x3f,0xc0);
	    } else {							/* For 301, 301B */ 
	        tempah = 0xc0;
	        tempbl = 0x3f;
	        if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
	           tempah = 0x00;
		   if(SiS_IsDualEdge(SiS_Pr, HwDeviceExtension, BaseAddr)) {
		      tempbl = 0xff;
		   }
	        }
	        SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x21,tempbl,tempah);     
	    }
	 }

	 if(IS_SIS740) {						
	    tempah = 0x80;
	    tempbl = 0x7f;
	    if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
	       tempah = 0x00;
	    } 
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x23,tempbl,tempah);
	 } else {
	    tempah = 0x00;						/* For all bridges */	
            tempbl = 0x7f;
            if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
               tempbl = 0xff;
	       if(!(SiS_IsDualEdge(SiS_Pr, HwDeviceExtension, BaseAddr)))
	          tempah |= 0x80;
            }
            SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x23,tempbl,tempah);
	 }

#endif /* SIS315H */

      } else if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {

         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x21,0x3f);

         if((SiS_Pr->SiS_VBInfo & DisableCRT2Display) ||
            (   (SiS_Pr->SiS_VBType & VB_NoLCD) &&
	        (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) ) ) {
	    SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x23,0x7F);
	 } else {
	    SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x23,0x80);
	 }

      }

  } else {  /* LVDS */

#ifdef SIS315H
      if(HwDeviceExtension->jChipType >= SIS_315H) {

         if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {

            tempah = 0x04;
	    tempbl = 0xfb;
            if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
               tempah = 0x00;
	       if(SiS_IsDualEdge(SiS_Pr, HwDeviceExtension, BaseAddr))
	          tempbl = 0xff;
            }
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,tempbl,tempah);

	    if(SiS_Pr->SiS_VBInfo & DisableCRT2Display)
	       SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,0xfb,0x00);

	    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2c,0xcf,0x30);

	 }

      }
#endif

  }

}

void
SiS_GetCRT2Data(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                USHORT RefreshRateTableIndex,
		PSIS_HW_DEVICE_INFO HwDeviceExtension)
{

  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {

     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {

        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {

           SiS_GetCRT2DataLVDS(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
	                      HwDeviceExtension);
        } else {

	   if( (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) &&
	       (SiS_Pr->SiS_VBType & VB_NoLCD) ) {
	       
	      /* TW: Need LVDS Data for LCD on 301BDH */
	      SiS_GetCRT2DataLVDS(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
	                          HwDeviceExtension);
				  
	   } else {
	
	      SiS_GetCRT2Data301(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
	                         HwDeviceExtension);
           }

        }

     } else {

     	SiS_GetCRT2Data301(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
	                   HwDeviceExtension);
     }

  } else {
  
     SiS_GetCRT2DataLVDS(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                         HwDeviceExtension);
  }
}

/* Checked with 650/LVDS 1.10.07 BIOS */
void
SiS_GetCRT2DataLVDS(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                    USHORT RefreshRateTableIndex,
		    PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
   USHORT CRT2Index, ResIndex;
   const SiS_LVDSDataStruct *LVDSData = NULL;

   SiS_GetCRT2ResInfo(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,HwDeviceExtension);
   
   if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      SiS_Pr->SiS_RVBHCMAX  = 1;
      SiS_Pr->SiS_RVBHCFACT = 1;
      SiS_Pr->SiS_NewFlickerMode = 0;
      SiS_Pr->SiS_RVBHRS = 50;
      SiS_Pr->SiS_RY1COE = 0;
      SiS_Pr->SiS_RY2COE = 0;
      SiS_Pr->SiS_RY3COE = 0;
      SiS_Pr->SiS_RY4COE = 0;
   }

   if((SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {

#ifdef SIS315H   
      SiS_GetCRT2PtrA(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                      &CRT2Index,&ResIndex);

      switch (CRT2Index) {
      	case  0:  LVDSData = SiS_Pr->SiS_LVDS1024x768Data_1;    break;
      	case  1:  LVDSData = SiS_Pr->SiS_LVDS1280x1024Data_1;   break;
        case  2:  LVDSData = SiS_Pr->SiS_LVDS1280x960Data_1;    break;
	case  3:  LVDSData = SiS_Pr->SiS_LCDA1400x1050Data_1;   break;
	case  4:  LVDSData = SiS_Pr->SiS_LCDA1600x1200Data_1;   break;
      	case  5:  LVDSData = SiS_Pr->SiS_LVDS1024x768Data_2;    break;
      	case  6:  LVDSData = SiS_Pr->SiS_LVDS1280x1024Data_2;   break;
      	case  7:  LVDSData = SiS_Pr->SiS_LVDS1280x960Data_2;    break;
	case  8:  LVDSData = SiS_Pr->SiS_LCDA1400x1050Data_2;   break;
	case  9:  LVDSData = SiS_Pr->SiS_LCDA1600x1200Data_2;   break;
	default:  LVDSData = SiS_Pr->SiS_LVDS1024x768Data_1;    break;
      }
#endif      

   } else {

      /* TW: 301BDH needs LVDS Data */
      if( (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) &&
          (SiS_Pr->SiS_VBType & VB_NoLCD) ) {
	      SiS_Pr->SiS_IF_DEF_LVDS = 1;
      }

      SiS_GetCRT2Ptr(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                     &CRT2Index,&ResIndex,HwDeviceExtension);

      /* TW: 301BDH needs LVDS Data */
      if( (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) &&
          (SiS_Pr->SiS_VBType & VB_NoLCD) ) {
              SiS_Pr->SiS_IF_DEF_LVDS = 0;
      }

      switch (CRT2Index) {
      	case  0:  LVDSData = SiS_Pr->SiS_LVDS800x600Data_1;    break;
      	case  1:  LVDSData = SiS_Pr->SiS_LVDS1024x768Data_1;   break;
      	case  2:  LVDSData = SiS_Pr->SiS_LVDS1280x1024Data_1;  break;
      	case  3:  LVDSData = SiS_Pr->SiS_LVDS800x600Data_2;    break;
      	case  4:  LVDSData = SiS_Pr->SiS_LVDS1024x768Data_2;   break;
      	case  5:  LVDSData = SiS_Pr->SiS_LVDS1280x1024Data_2;  break;
	case  6:  LVDSData = SiS_Pr->SiS_LVDS640x480Data_1;    break;
        case  7:  LVDSData = SiS_Pr->SiS_LVDSXXXxXXXData_1;    break;
	case  8:  LVDSData = SiS_Pr->SiS_LVDS1400x1050Data_1;  break;
	case  9:  LVDSData = SiS_Pr->SiS_LVDS1400x1050Data_2;  break;
      	case 10:  LVDSData = SiS_Pr->SiS_CHTVUNTSCData;        break;
      	case 11:  LVDSData = SiS_Pr->SiS_CHTVONTSCData;        break;
      	case 12:  LVDSData = SiS_Pr->SiS_CHTVUPALData;         break;
      	case 13:  LVDSData = SiS_Pr->SiS_CHTVOPALData;         break;
      	case 14:  LVDSData = SiS_Pr->SiS_LVDS320x480Data_1;    break;
	case 15:  LVDSData = SiS_Pr->SiS_LVDS1024x600Data_1;   break;
	case 16:  LVDSData = SiS_Pr->SiS_LVDS1152x768Data_1;   break;
	case 17:  LVDSData = SiS_Pr->SiS_LVDS1024x600Data_2;   break;
	case 18:  LVDSData = SiS_Pr->SiS_LVDS1152x768Data_2;   break;
	case 19:  LVDSData = SiS_Pr->SiS_LVDS1280x768Data_1;   break;
	case 20:  LVDSData = SiS_Pr->SiS_LVDS1280x768Data_2;   break;
	case 21:  LVDSData = SiS_Pr->SiS_LVDS1600x1200Data_1;  break;
	case 22:  LVDSData = SiS_Pr->SiS_LVDS1600x1200Data_2;  break;
	case 90:  LVDSData = SiS_Pr->SiS_CHTVUPALMData;        break;
      	case 91:  LVDSData = SiS_Pr->SiS_CHTVOPALMData;        break;
      	case 92:  LVDSData = SiS_Pr->SiS_CHTVUPALNData;        break;
      	case 93:  LVDSData = SiS_Pr->SiS_CHTVOPALNData;        break;
	case 99:  LVDSData = SiS_Pr->SiS_CHTVSOPALData;	       break;  /* TW: Super Overscan */
	default:  LVDSData = SiS_Pr->SiS_LVDS1024x768Data_1;   break;
     }
   }

   SiS_Pr->SiS_VGAHT = (LVDSData+ResIndex)->VGAHT;
   SiS_Pr->SiS_VGAVT = (LVDSData+ResIndex)->VGAVT;
   SiS_Pr->SiS_HT    = (LVDSData+ResIndex)->LCDHT;
   SiS_Pr->SiS_VT    = (LVDSData+ResIndex)->LCDVT;

  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {

    if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)){
         if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768){
           SiS_Pr->SiS_HDE = 1024;
           SiS_Pr->SiS_VDE =  768;
         } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024){
           SiS_Pr->SiS_HDE = 1280;
           SiS_Pr->SiS_VDE = 1024;
	 } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050){
           SiS_Pr->SiS_HDE = 1400;
           SiS_Pr->SiS_VDE = 1050;
	 } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200){
           SiS_Pr->SiS_HDE = 1600;
           SiS_Pr->SiS_VDE = 1200;
         } else {
	   SiS_Pr->SiS_HDE = 1280;
	   SiS_Pr->SiS_VDE =  960;
	 }
    }

  } else {

    if(SiS_Pr->SiS_IF_DEF_TRUMPION == 0) {
      if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) && (!(SiS_Pr->SiS_LCDInfo & LCDPass11))) {
        if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) {
          if((!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) || (SiS_Pr->SiS_SetFlag & SetDOSMode)) {
            if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) {
              SiS_Pr->SiS_HDE =  800;
              SiS_Pr->SiS_VDE =  600;
	    } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
	      SiS_Pr->SiS_HDE = 1024;
              SiS_Pr->SiS_VDE =  600;  
            } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
              SiS_Pr->SiS_HDE = 1024;
              SiS_Pr->SiS_VDE =  768;
 	    } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768) {
	      SiS_Pr->SiS_HDE = 1152;
	      SiS_Pr->SiS_VDE =  768;	
	    } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x864) {
	      SiS_Pr->SiS_HDE = 1152;
	      SiS_Pr->SiS_VDE =  864;  
	    } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768) {
	      SiS_Pr->SiS_HDE = 1280;
	      SiS_Pr->SiS_VDE =  768;        
            } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
              SiS_Pr->SiS_HDE = 1280;
              SiS_Pr->SiS_VDE = 1024;
	    } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
	      SiS_Pr->SiS_HDE = 1400;
              SiS_Pr->SiS_VDE = 1050;
	    } else {
	      SiS_Pr->SiS_HDE = 1600;
	      SiS_Pr->SiS_VDE = 1200;
	    }
            if(SiS_Pr->SiS_IF_DEF_FSTN) {
              SiS_Pr->SiS_HDE = 320;
              SiS_Pr->SiS_VDE = 480;
            }
          }
        }
      }
    }
  }
}

void
SiS_GetCRT2Data301(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex,
		   PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempax,tempbx,modeflag;
  USHORT resinfo;
  USHORT CRT2Index,ResIndex;
  const SiS_LCDDataStruct *LCDPtr = NULL;
  const SiS_TVDataStruct  *TVPtr  = NULL;

  if(ModeNo <= 0x13) {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }
  
  SiS_Pr->SiS_NewFlickerMode = 0;
  SiS_Pr->SiS_RVBHRS = 50;
  SiS_Pr->SiS_RY1COE = 0;
  SiS_Pr->SiS_RY2COE = 0;
  SiS_Pr->SiS_RY3COE = 0;
  SiS_Pr->SiS_RY4COE = 0;

  SiS_GetCRT2ResInfo(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,HwDeviceExtension);

  /* TW: For VGA2 ("RAMDAC2") */

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC){
     SiS_GetRAMDAC2DATA(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                        HwDeviceExtension);
     return;
  }

  /* TW: For TV */

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {

    SiS_GetCRT2Ptr(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                   &CRT2Index,&ResIndex,HwDeviceExtension);

    switch (CRT2Index) {
      case  2:  TVPtr = SiS_Pr->SiS_ExtHiTVData;   break;
/*    case  7:  TVPtr = SiS_Pr->SiS_St1HiTVData;   break;  */
      case 12:  TVPtr = SiS_Pr->SiS_St2HiTVData;   break;
      case  3:  TVPtr = SiS_Pr->SiS_ExtPALData;    break;
      case  4:  TVPtr = SiS_Pr->SiS_ExtNTSCData;   break;
      case  8:  TVPtr = SiS_Pr->SiS_StPALData;     break;
      case  9:  TVPtr = SiS_Pr->SiS_StNTSCData;    break;
      default:  TVPtr = SiS_Pr->SiS_StPALData;     break;  /* TW: Just to avoid a crash */
    }

    SiS_Pr->SiS_RVBHCMAX  = (TVPtr+ResIndex)->RVBHCMAX;
    SiS_Pr->SiS_RVBHCFACT = (TVPtr+ResIndex)->RVBHCFACT;
    SiS_Pr->SiS_VGAHT     = (TVPtr+ResIndex)->VGAHT;
    SiS_Pr->SiS_VGAVT     = (TVPtr+ResIndex)->VGAVT;
    SiS_Pr->SiS_HDE       = (TVPtr+ResIndex)->TVHDE;
    SiS_Pr->SiS_VDE       = (TVPtr+ResIndex)->TVVDE;
    SiS_Pr->SiS_RVBHRS    = (TVPtr+ResIndex)->RVBHRS;
    SiS_Pr->SiS_NewFlickerMode = (TVPtr+ResIndex)->FlickerMode;
    if(modeflag & HalfDCLK) {
	SiS_Pr->SiS_RVBHRS    = (TVPtr+ResIndex)->HALFRVBHRS;
    }

    if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {  
    
       if(SiS_Pr->SiS_HiVision != 3) {
       
      	  if(resinfo == 0x08) SiS_Pr->SiS_NewFlickerMode = 0x40;
      	  if(resinfo == 0x09) SiS_Pr->SiS_NewFlickerMode = 0x40;
	  if(resinfo == 0x12) SiS_Pr->SiS_NewFlickerMode = 0x40;
	  
       } 
       
       switch(SiS_Pr->SiS_HiVision) {
       case 2:
       case 1:
       case 0:
          SiS_Pr->SiS_HT = 0x6b4;
          SiS_Pr->SiS_VT = 0x20d;
	  /* Don't care about TVSimuMode */
          break;
       default:
          if(SiS_Pr->SiS_VGAVDE == 350) SiS_Pr->SiS_SetFlag |= TVSimuMode;

          SiS_Pr->SiS_HT = ExtHiTVHT;
          SiS_Pr->SiS_VT = ExtHiTVVT;
          if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
             if(SiS_Pr->SiS_SetFlag & TVSimuMode) {
                SiS_Pr->SiS_HT = StHiTVHT;
                SiS_Pr->SiS_VT = StHiTVVT;
                if(!(modeflag & Charx8Dot)){
                   SiS_Pr->SiS_HT = StHiTextTVHT;
                   SiS_Pr->SiS_VT = StHiTextTVVT;
                }
             }
          }
       }

    } else {

      SiS_Pr->SiS_RY1COE = (TVPtr+ResIndex)->RY1COE;
      SiS_Pr->SiS_RY2COE = (TVPtr+ResIndex)->RY2COE;
      SiS_Pr->SiS_RY3COE = (TVPtr+ResIndex)->RY3COE;
      SiS_Pr->SiS_RY4COE = (TVPtr+ResIndex)->RY4COE;

      if(modeflag & HalfDCLK) {
         SiS_Pr->SiS_RY1COE = 0x00;
         SiS_Pr->SiS_RY2COE = 0xf4;
         SiS_Pr->SiS_RY3COE = 0x10;
         SiS_Pr->SiS_RY4COE = 0x38;
      }

      if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
        SiS_Pr->SiS_HT = NTSCHT;
	if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {  
	   if((ModeNo == 0x4a) || (ModeNo == 0x38)) SiS_Pr->SiS_HT = NTSC2HT;
	}  
        SiS_Pr->SiS_VT = NTSCVT;
      } else {
        SiS_Pr->SiS_HT = PALHT;
        SiS_Pr->SiS_VT = PALVT;
      }

    }

    return;
  }

  /* TW: For LCD */

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {

    SiS_GetCRT2Ptr(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                   &CRT2Index,&ResIndex,HwDeviceExtension);

    switch (CRT2Index) {
      case  0: LCDPtr = SiS_Pr->SiS_ExtLCD1024x768Data;        break; /* VESA Timing */
      case  1: LCDPtr = SiS_Pr->SiS_ExtLCD1280x1024Data;       break; /* VESA Timing */
      case  5: LCDPtr = SiS_Pr->SiS_StLCD1024x768Data;         break; /* Obviously unused */
      case  6: LCDPtr = SiS_Pr->SiS_StLCD1280x1024Data;        break; /* Obviously unused */
      case 10: LCDPtr = SiS_Pr->SiS_St2LCD1024x768Data;        break; /* Non-VESA Timing */
      case 11: LCDPtr = SiS_Pr->SiS_St2LCD1280x1024Data;       break; /* Non-VESA Timing */
      case 13: LCDPtr = SiS_Pr->SiS_NoScaleData1024x768;       break; /* Non-expanding */
      case 14: LCDPtr = SiS_Pr->SiS_NoScaleData1280x1024;      break; /* Non-expanding */
      case 15: LCDPtr = SiS_Pr->SiS_LCD1280x960Data;           break; /* 1280x960 */
      case 20: LCDPtr = SiS_Pr->SiS_ExtLCD1400x1050Data;       break; /* VESA Timing */
      case 21: LCDPtr = SiS_Pr->SiS_NoScaleData1400x1050;      break; /* Non-expanding (let panel scale) */
      case 22: LCDPtr = SiS_Pr->SiS_StLCD1400x1050Data;	       break; /* Non-VESA Timing (let panel scale) */
      case 23: LCDPtr = SiS_Pr->SiS_ExtLCD1600x1200Data;       break; /* VESA Timing */
      case 24: LCDPtr = SiS_Pr->SiS_NoScaleData1600x1200;      break; /* Non-expanding */
      case 25: LCDPtr = SiS_Pr->SiS_StLCD1600x1200Data;	       break; /* Non-VESA Timing */
      default: LCDPtr = SiS_Pr->SiS_ExtLCD1024x768Data;	       break; /* Just to avoid a crash */
    }

    SiS_Pr->SiS_RVBHCMAX  = (LCDPtr+ResIndex)->RVBHCMAX;
    SiS_Pr->SiS_RVBHCFACT = (LCDPtr+ResIndex)->RVBHCFACT;
    SiS_Pr->SiS_VGAHT     = (LCDPtr+ResIndex)->VGAHT;
    SiS_Pr->SiS_VGAVT     = (LCDPtr+ResIndex)->VGAVT;
    SiS_Pr->SiS_HT        = (LCDPtr+ResIndex)->LCDHT;
    SiS_Pr->SiS_VT        = (LCDPtr+ResIndex)->LCDVT;
    
#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO,
    	"GetCRT2Data: Index %d ResIndex %d\n", CRT2Index, ResIndex);
#endif    

    tempax = 1024;
    if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
      if(HwDeviceExtension->jChipType < SIS_315H) {
         if     (SiS_Pr->SiS_VGAVDE == 350) tempbx = 560;
         else if(SiS_Pr->SiS_VGAVDE == 400) tempbx = 640;
         else                               tempbx = 768;
      } else {      
         tempbx = 768; 
      }
    } else {
      if     (SiS_Pr->SiS_VGAVDE == 357) tempbx = 527;
      else if(SiS_Pr->SiS_VGAVDE == 420) tempbx = 620;
      else if(SiS_Pr->SiS_VGAVDE == 525) tempbx = 775;
      else if(SiS_Pr->SiS_VGAVDE == 600) tempbx = 775;
      else if(SiS_Pr->SiS_VGAVDE == 350) tempbx = 560;
      else if(SiS_Pr->SiS_VGAVDE == 400) tempbx = 640;
      else                               tempbx = 768;
    }
    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
      tempax = 1280;
      if     (SiS_Pr->SiS_VGAVDE == 360) tempbx = 768;
      else if(SiS_Pr->SiS_VGAVDE == 375) tempbx = 800;
      else if(SiS_Pr->SiS_VGAVDE == 405) tempbx = 864;
      else                               tempbx = 1024;
    }
    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960) {
      tempax = 1280;
      if     (SiS_Pr->SiS_VGAVDE == 350)  tempbx = 700;
      else if(SiS_Pr->SiS_VGAVDE == 400)  tempbx = 800;
      else if(SiS_Pr->SiS_VGAVDE == 1024) tempbx = 960;
      else                                tempbx = 960;
    }
    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
      tempax = 1400;
      tempbx = 1050;
    }
    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
      tempax = 1600;
      tempbx = 1200;
    }
    if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
       tempax = SiS_Pr->SiS_VGAHDE;
       tempbx = SiS_Pr->SiS_VGAVDE;
    }
    SiS_Pr->SiS_HDE = tempax;
    SiS_Pr->SiS_VDE = tempbx;
    return;
  }
}

USHORT
SiS_GetResInfo(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT resindex;

  if(ModeNo <= 0x13)
    	resindex=SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  else
    	resindex=SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;

  return(resindex);
}

void
SiS_GetCRT2ResInfo(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT xres,yres,modeflag=0,resindex;

  resindex = SiS_GetResInfo(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex);

  if(ModeNo <= 0x13) {
    	xres = SiS_Pr->SiS_StResInfo[resindex].HTotal;
    	yres = SiS_Pr->SiS_StResInfo[resindex].VTotal;
  } else {
	xres = SiS_Pr->SiS_ModeResInfo[resindex].HTotal;
    	yres = SiS_Pr->SiS_ModeResInfo[resindex].VTotal;
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  if((HwDeviceExtension->jChipType >= SIS_315H) && (SiS_Pr->SiS_IF_DEF_LVDS == 1)) {
      if((ModeNo != 0x03) && (SiS_Pr->SiS_SetFlag & SetDOSMode)) {
          if(yres == 350) yres = 400;
      }
      if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x3a) & 0x01) {
 	  if(ModeNo == 0x12) yres = 400;
      }
  }

  if(ModeNo > 0x13) {
      if(SiS_Pr->SiS_IF_DEF_FSTN == 1){
            xres *= 2;
            yres *= 2;
      } else {
  	    if(modeflag & HalfDCLK)       xres *= 2;
  	    if(modeflag & DoubleScanMode) yres *= 2;
      }
  }

  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
           if(xres == 720) xres = 640;
	} else {
	   if(SiS_Pr->SiS_VBType & VB_NoLCD) {           /* TW: 301BDH */
	        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
                   if(xres == 720) xres = 640;
		}
		if(SiS_Pr->SiS_SetFlag & SetDOSMode) {
	           yres = 400;
	           if(HwDeviceExtension->jChipType >= SIS_315H) {
	              if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x17) & 0x80) yres = 480;
	           } else {
	              if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x13) & 0x80) yres = 480;
	           }
	        }
	   } else {
	      if(SiS_Pr->SiS_VBInfo & (SetCRT2ToAVIDEO |           /* (Allow 720 for VGA2) */
	      			       SetCRT2ToSVIDEO |
	                               SetCRT2ToSCART  | 
				       SetCRT2ToLCD    | 
				       SetCRT2ToHiVisionTV)) {
	         if(xres == 720) xres = 640;
	      }
	      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
    	         if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
		    if(ModeNo <= 0x13) {
		       /* TW: This is wrong for 640x400 *graphics* mode */
      		       if(yres == 400) yres = 405;
		    }
      		    if(yres == 350) yres = 360;
      		    if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
        	       if(yres == 360) yres = 375;
      		    }
   	         }
    	         if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768){
      		    if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
        	       if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
          	          if(yres == 350) yres = 357;
          	          if(yres == 400) yres = 420;
            	          if(yres == 480) yres = 525;
        	       }
      		    }
    	         }
	      }
	   }
	}
  } else {
    	if(xres == 720) xres = 640;
	if(SiS_Pr->SiS_SetFlag & SetDOSMode) {
	      yres = 400;
	      if(HwDeviceExtension->jChipType >= SIS_315H) {
	          if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x17) & 0x80) yres = 480;
	      } else {
	          if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x13) & 0x80) yres = 480;
	      }
	}
  }
  SiS_Pr->SiS_VGAHDE = SiS_Pr->SiS_HDE = xres;
  SiS_Pr->SiS_VGAVDE = SiS_Pr->SiS_VDE = yres;
}

void
SiS_GetCRT2Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
	       USHORT RefreshRateTableIndex,USHORT *CRT2Index,USHORT *ResIndex,
	       PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempbx=0,tempal=0;
  USHORT Flag,resinfo=0;

  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {

    	if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {                            /* LCD */

	        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960) {
			tempbx = 15;
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
		        tempbx = 20;
			if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)         tempbx = 21;
			else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx = 22;
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
		        tempbx = 23;
			if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)         tempbx = 24;
			else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx = 25;
		} else if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
			tempbx = 13;
			if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) tempbx++;
		} else {
      		   tempbx = SiS_Pr->SiS_LCDResInfo - SiS_Pr->SiS_Panel1024x768;
      		   if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
        		tempbx += 5;
                        /* GetRevisionID();  */
			/* TW: BIOS only adds 5 once */
        		tempbx += 5;
       		   }
	        }

     	} else {						  	/* TV */
	
       		if((SiS_Pr->SiS_VBType & VB_SIS301B302B) &&
		   (SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV)) {
         		if(SiS_Pr->SiS_VGAVDE > 480) SiS_Pr->SiS_SetFlag &= (~TVSimuMode);
         		tempbx = 2;
         		if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
            			if(!(SiS_Pr->SiS_SetFlag & TVSimuMode)) tempbx = 12; 
         		}
       		} else {
         		if(SiS_Pr->SiS_VBInfo & SetPALTV) tempbx = 3;
         		else tempbx = 4;
         		if(SiS_Pr->SiS_SetFlag & TVSimuMode) tempbx += 5;
       		}
		
     	}

     	if(ModeNo <= 0x13) {
       		tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
     	} else {
       		tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
		resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
        }

     	tempal &= 0x3F;

      	if((SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) &&
           (SiS_Pr->SiS_VBInfo & (SetCRT2ToTV-SetCRT2ToHiVisionTV))) {
      		if(tempal == 0x06) tempal = 0x07;
        }

	/* TW: 300/301LV BIOS */
	if((HwDeviceExtension->jChipType == SIS_300) &&
	   (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) {
	    if(ModeNo > 0x13) {
	        if((resinfo == 0x0c) || (resinfo == 0x0d))  /* 720 (index diff. on 310/325!) */
		    tempal = 6;
	    }
	}

	if(HwDeviceExtension->jChipType != SIS_300) {
           if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
              if((ModeNo == 0x31) || (ModeNo == 0x32)) tempal = 6;
	   }
	}

     	*CRT2Index = tempbx;
     	*ResIndex = tempal;

  } else {   /* LVDS, 301B-DH (if running on LCD) */

    	Flag = 1;
    	tempbx = 0;
    	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	
      		if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
        		Flag = 0;
        		tempbx = 10;
			if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) tempbx += 1;
        		if(SiS_Pr->SiS_VBInfo & SetPALTV) {
				tempbx += 2;
				if(SiS_Pr->SiS_CHSOverScan) tempbx = 99;
				if(SiS_Pr->SiS_CHPALM) {
					tempbx = 90;
					if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) tempbx += 1;
				} else if(SiS_Pr->SiS_CHPALN) {
					tempbx = 92;
					if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) tempbx += 1;
				}
				
			}
      		}
		
    	}

    	if(Flag) {
      		
		if(SiS_Pr->SiS_LCDResInfo <= SiS_Pr->SiS_Panel1280x1024) {
		   tempbx = SiS_Pr->SiS_LCDResInfo - SiS_Pr->SiS_PanelMinLVDS;
   	      	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx += 3;
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768) {
		   tempbx = 18;
		   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx++; 
	        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) { 
		   tempbx = 6;
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
		   tempbx = 15;
  		   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx += 2;
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768) {
		   tempbx = 16;
		   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx += 2;
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
		   tempbx = 8;
		   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx++;
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
		   tempbx = 21;
		   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx++;
		}
		
		if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
		   tempbx = 7;
        	}
		
	}

    	if(ModeNo <= 0x13)
      		tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
    	else {
      		tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
		resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
	}

	if(SiS_Pr->SiS_IF_DEF_FSTN){
       	 	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel320x480){
         		tempbx = 14;
         		tempal = 6;
        	}
    	}

	if(SiS_Pr->SiS_SetFlag & SetDOSMode) {
	        if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel640x480) tempal = 7;
		if(HwDeviceExtension->jChipType < SIS_315H) {
		    if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x13) & 0x80) tempal++;
		}

	}

	if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
	    if(ModeNo > 0x13) {
	        if(HwDeviceExtension->jChipType < SIS_315H) {
	           if((resinfo == 0x0c) || (resinfo == 0x0d))  /* 720 */
		       tempal = 6;
	        } else {
		   if((resinfo == 0x0d) || (resinfo == 0x0e))  /* 720 */
		       tempal = 6;
		}
	    }
	}

    	*CRT2Index = tempbx;
    	*ResIndex = tempal & 0x1F;
  }
}

#ifdef SIS315H
void
SiS_GetCRT2PtrA(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		USHORT RefreshRateTableIndex,USHORT *CRT2Index,
		USHORT *ResIndex)
{
  USHORT tempbx,tempal;

  tempbx = SiS_Pr->SiS_LCDResInfo;

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)      tempbx = 4;
  else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) tempbx = 3;
  else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960)  tempbx = 2;
  else tempbx -= SiS_Pr->SiS_Panel1024x768;

  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx += 5;

  if(ModeNo <= 0x13)
      	tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
      	tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  *CRT2Index = tempbx;
  *ResIndex = tempal & 0x1F;
}
#endif

void
SiS_GetCRT2Part2Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		    USHORT RefreshRateTableIndex,USHORT *CRT2Index,
		    USHORT *ResIndex)
{
  USHORT tempbx,tempal;

  if(ModeNo <= 0x13)
      	tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
      	tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  tempbx = SiS_Pr->SiS_LCDResInfo;

  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)      tempbx += 16;
  else if(SiS_Pr->SiS_SetFlag & LCDVESATiming) tempbx += 32;

  *CRT2Index = tempbx;
  *ResIndex = tempal & 0x3F;
}

USHORT
SiS_GetRatePtrCRT2(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
                   PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  SHORT  LCDRefreshIndex[] = { 0x00, 0x00, 0x03, 0x01,
                               0x01, 0x01, 0x01, 0x01,
			       0x01, 0x01, 0x01, 0x01,
			       0x01, 0x01, 0x01, 0x01 };
  USHORT RefreshRateTableIndex,i,backup_i;
  USHORT modeflag,index,temp,backupindex;

  if(SiS_Pr->UseCustomMode) return 0;
  
  if(ModeNo <= 0x13)
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  else
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;

  if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
    	if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
      		if(modeflag & HalfDCLK) return(0);
    	}
  }

  if(ModeNo < 0x14) return(0xFFFF);

 /* TW: CR33 holds refresh rate index for CRT1 [3:0] and CRT2 [7:4].
  *     On LVDS machines, CRT2 index is always 0 and will be
  *     set to 0 by the following code; this causes the function
  *     to take the first non-interlaced mode in SiS_Ext2Struct
  */

  index = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x33);
  index >>= SiS_Pr->SiS_SelectCRT2Rate;
  index &= 0x0F;
  backupindex = index;

  if(index > 0) index--;

  if(SiS_Pr->SiS_SetFlag & ProgrammingCRT2) {
      if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
        if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA))  index = 0;
      } else {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	    if(SiS_Pr->SiS_VBType & VB_NoLCD)
	       	    index = 0;
	    else if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)
	    	    index = backupindex = 0;
	}
      }
  }

  if(SiS_Pr->SiS_SetFlag & ProgrammingCRT2) {
    	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
      		if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        		index = 0;
      		}
    	}
    	if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
      		if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
			if(!(SiS_Pr->SiS_VBType & VB_NoLCD)) {
           		   temp = LCDRefreshIndex[SiS_Pr->SiS_LCDResInfo];
        		   if(index > temp) index = temp;
			}
      		} else {
        		index = 0;
      		}
    	}
  }

  RefreshRateTableIndex = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].REFindex;
  ModeNo = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].ModeID;

  /* TW: 650/LVDS 1.10.07, 650/30xLV 1.10.6s */
  if(HwDeviceExtension->jChipType >= SIS_315H) {
     if(!(SiS_Pr->SiS_VBInfo & DriverMode)) {
        if( (SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_VESAID == 0x105) ||
            (SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_VESAID == 0x107) ) {
           if(backupindex <= 1) RefreshRateTableIndex++;
        }
     }
  }

  i = 0;
  do {
    	if(SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + i].ModeID != ModeNo) break;
    	temp = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + i].Ext_InfoFlag;
    	temp &= ModeInfoFlag;
    	if(temp < SiS_Pr->SiS_ModeType) break;
    	i++;
    	index--;
  } while(index != 0xFFFF);

  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC)) {
    	if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
      		temp = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + i - 1].Ext_InfoFlag;
      		if(temp & InterlaceMode) {
        		i++;
      		}
    	}
  }

  i--;

  if((SiS_Pr->SiS_SetFlag & ProgrammingCRT2) && (!(SiS_Pr->SiS_VBInfo & DisableCRT2Display))) {
    	backup_i = i;
    	if (!(SiS_AdjustCRT2Rate(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,
	                             RefreshRateTableIndex,&i,HwDeviceExtension))) {
		/* TW: This is for avoiding random data to be used; i is
		 *     in an undefined state if no matching CRT2 mode is
		 *     found.
		 */
		i = backup_i;
	}
  }

  return(RefreshRateTableIndex + i);
}

/* Checked against all (incl 650/LVDS (1.10.07), 630/301) BIOSes */
BOOLEAN
SiS_AdjustCRT2Rate(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex,USHORT *i,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempax,tempbx,resinfo;
  USHORT modeflag,infoflag;

  if (ModeNo <= 0x13)
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  else
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;

  resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  tempbx = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + (*i)].ModeID;

  tempax = 0;
  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
    	if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {
      		tempax |= SupportRAMDAC2;
		if(HwDeviceExtension->jChipType >= SIS_315H) {
		    tempax |= SupportTV;
		    if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
		        if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
			    if(resinfo == 0x0a) tempax |= SupportTV1024;
			}
		    }
		}
    	}
    	if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
      		tempax |= SupportLCD;
		if(HwDeviceExtension->jChipType >= SIS_315H) {
                   if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1600x1200) {
		      if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1400x1050) {
		         if((resinfo == 6) && (SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
			    (*i) = 0;
                            return(1);
		         } else {
      		            if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x1024) {
        		      if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x960) {
           			if((resinfo == 6) && (SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
				    return(0);
				} else {
             			    if((resinfo >= 9) && (resinfo != 0x14)) {
               				return(0);
             			    }
           			}
        		      }
		            }
		         }
		      }
      		   }
		} else {
		  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
		     if((resinfo != 0x0f) && ((resinfo == 4) || (resinfo >= 8))) return(0);
		  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768) {
		     if((resinfo != 0x10) && (resinfo > 8)) return(0);
		  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960) {
		     if((resinfo != 0x0e) && (resinfo > 8)) return(0);
		  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
		     if(resinfo > 9) return(0);
		  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
		     if(resinfo > 8) return(0);
		  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) {
		     if((resinfo == 4) || (resinfo > 7)) return(0);
		  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) {
		     if((resinfo == 4) || (resinfo == 3) || (resinfo > 6)) return(0);
		  }
		}
    	}
    	if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) { 
	        if(SiS_Pr->SiS_HiVision == 3) {
		      	tempax |= SupportHiVisionTV2;
      			if(SiS_Pr->SiS_VBInfo & SetInSlaveMode){
        			if(resinfo == 4) return(0);
        			if(resinfo == 3) return(0);
				if(resinfo == 7) {
	          			if(SiS_Pr->SiS_SetFlag & TVSimuMode) return(0);
        			}
        			if(resinfo > 7) return(0);
			}
		} else {  
      			tempax |= SupportHiVisionTV;
      			if(SiS_Pr->SiS_VBInfo & SetInSlaveMode){
        			if(resinfo == 4) return(0);
        			if((resinfo == 3) || (resinfo == 7)) {
	          			if(SiS_Pr->SiS_SetFlag & TVSimuMode) return(0);
        			}
        			if(resinfo > 7) return(0);
			}
		}
    	} else {
      	   if(SiS_Pr->SiS_VBInfo & (SetCRT2ToAVIDEO|SetCRT2ToSVIDEO|SetCRT2ToSCART)) {
        	tempax |= SupportTV;
		tempax |= SupportTV1024;
		if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
		    if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
		        if((SiS_Pr->SiS_VBInfo & SetNotSimuMode) && (SiS_Pr->SiS_VBInfo & SetPALTV)) {
			     if(resinfo != 8) {
			         if( (!(SiS_Pr->SiS_VBInfo & SetPALTV)) ||
				     ((SiS_Pr->SiS_VBInfo & SetPALTV) && (resinfo != 4)) ) {
				     tempax &= ~(SupportTV1024);
				     if(HwDeviceExtension->jChipType >= SIS_315H) {
                                         if((modeflag & NoSupportSimuTV) && (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
				             if( (!(SiS_Pr->SiS_VBInfo & SetPALTV)) ||
			                         ((SiS_Pr->SiS_VBInfo & SetPALTV) && (resinfo != 7)) ) {
			                         if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) return(0);
		                             }
				         }
		                     } else {
				         if( (resinfo != 3) ||
					     (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
					     (SiS_Pr->SiS_VBInfo & SetNotSimuMode) ) {
					     if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
						 if((modeflag & NoSupportSimuTV) && (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
						     if(resinfo == 3) return(0);
						     if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) return (0);
						 }
		                             }
                                         } else return(0);
				     }
				 }
			     }
			} else {
			    tempax &= ~(SupportTV1024);
			    if(HwDeviceExtension->jChipType >= SIS_315H) {
			        if((modeflag & NoSupportSimuTV) && (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
			            if( (!(SiS_Pr->SiS_VBInfo & SetPALTV)) ||
			                ((SiS_Pr->SiS_VBInfo & SetPALTV) && (resinfo != 7)) ) {
			                if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) return(0);
		                    }
		                }
			    } else {
			        if( (resinfo != 3) ||
				    (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
				    (SiS_Pr->SiS_VBInfo & SetNotSimuMode) ) {
				     if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
					 if((modeflag & NoSupportSimuTV) && (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
					     if(resinfo == 3) return(0);
					     if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) return (0);
					 }
		                     }
                                } else return(0);
                            }
			}
		    } else {  /* slavemode */
			if(resinfo != 8) {
			    if( (!(SiS_Pr->SiS_VBInfo & SetPALTV)) ||
			        ((SiS_Pr->SiS_VBInfo & SetPALTV) && (resinfo != 4) ) ) {
				 tempax &= ~(SupportTV1024);
				 if(HwDeviceExtension->jChipType >= SIS_315H) {
				     if((modeflag & NoSupportSimuTV) && (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
				         if( (!(SiS_Pr->SiS_VBInfo & SetPALTV)) ||
			                     ((SiS_Pr->SiS_VBInfo & SetPALTV) && (resinfo != 7)) ) {
			                     if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode))  return(0);
		                         }
		                     }
			        } else {
				    if( (resinfo != 3) ||
				        (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
				        (SiS_Pr->SiS_VBInfo & SetNotSimuMode) ) {
				         if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
					     if((modeflag & NoSupportSimuTV) && (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
					         if(resinfo == 3) return(0);
					         if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) return (0);
					     }
		                         }
                                    } else return(0);
				}
			    }
			}
		    }
		} else {   /* 301 */
		    tempax &= ~(SupportTV1024);
		    if(HwDeviceExtension->jChipType >= SIS_315H) {
		        if((modeflag & NoSupportSimuTV) && (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
		            if( (!(SiS_Pr->SiS_VBInfo & SetPALTV)) ||
			        ((SiS_Pr->SiS_VBInfo & SetPALTV) && (resinfo != 7)) ) {
			        if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) return(0);
		            }
		        }
		    } else {
		        if( (resinfo != 3) ||
			    (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
			    (SiS_Pr->SiS_VBInfo & SetNotSimuMode) ) {
			    if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
			        if((modeflag & NoSupportSimuTV) && (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
				    if(resinfo == 3) return(0);
				    if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) return (0);
				}
		            }
                        } else return(0);
		    }
		}
           }
    	}
	
  } else {	/* TW: for LVDS  */

    	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
      		if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        		tempax |= SupportCHTV;
      		}
    	}
    	if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
      		tempax |= SupportLCD;
		if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768) {
		     if((resinfo != 0x14) && (resinfo > 0x09)) return(0);
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
		     if((resinfo != 0x0f) && (resinfo > 0x08)) return(0);
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768) {
		     if((resinfo != 0x10) && (resinfo > 0x08)) return(0);
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
		     if((resinfo != 0x15) && (resinfo > 0x09)) return(0);
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
                     if(resinfo > 0x09) return(0);
                } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
		     if(resinfo > 0x08) return(0);
		} else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600){
		     if(resinfo > 0x07) return(0);
		     if(resinfo == 0x04) return(0);
		}
    	}
  }
  /* TW: Look backwards in table for matching CRT2 mode */
  for(; SiS_Pr->SiS_RefIndex[RefreshRateTableIndex+(*i)].ModeID == tempbx; (*i)--) {
     	infoflag = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + (*i)].Ext_InfoFlag;
     	if(infoflag & tempax) {
       		return(1);
     	}
     	if ((*i) == 0) break;
  }
  /* TW: Look through the whole mode-section of the table from the beginning
   *     for a matching CRT2 mode if no mode was found yet.
   */
  for((*i) = 0; ; (*i)++) {
     	infoflag = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + (*i)].Ext_InfoFlag;
     	if(SiS_Pr->SiS_RefIndex[RefreshRateTableIndex + (*i)].ModeID != tempbx) {
       		return(0);
     	}
     	if(infoflag & tempax) {
       		return(1);
     	}
  }
  return(1);
}

void
SiS_SaveCRT2Info(SiS_Private *SiS_Pr, USHORT ModeNo)
{
  USHORT temp1,temp2;

  /* TW: We store CRT1 ModeNo in CR34 */
  SiS_SetReg1(SiS_Pr->SiS_P3d4,0x34,ModeNo);
  temp1 = (SiS_Pr->SiS_VBInfo & SetInSlaveMode) >> 8;
  temp2 = ~(SetInSlaveMode >> 8);
  SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x31,temp2,temp1);
}

void
SiS_GetVBInfo(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
              USHORT ModeIdIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension,
	      int checkcrt2mode)
{
  USHORT tempax,tempbx,temp;
  USHORT modeflag, resinfo=0;
  UCHAR  OutputSelect = *SiS_Pr->pSiS_OutputSelect;

  if(SiS_Pr->UseCustomMode) {
        modeflag = SiS_Pr->CModeFlag;
  } else {
    if (ModeNo <= 0x13)
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    else {
   	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
    }
  }    

  SiS_Pr->SiS_SetFlag = 0;

  SiS_Pr->SiS_ModeType = modeflag & ModeInfoFlag;

  tempbx = 0;
  if(SiS_BridgeIsOn(SiS_Pr,BaseAddr,HwDeviceExtension) == 0) {  
    	temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
#if 0	
	/* SiS_HiVision is only used on 310/325/330+30xLV */
	if(SiS_Pr->SiS_VBType & (VB_SIS301LV302LV)) {
	   if(SiS_Pr->SiS_HiVision & 0x03) {	/* TW: New from 650/30xLV 1.10.6s */
	      temp &= (SetCRT2ToHiVisionTV | SwitchToCRT2 | SetSimuScanMode); 	/* 0x83 */
	      temp |= SetCRT2ToHiVisionTV;   					/* 0x80 */
	   }
	   if(SiS_Pr->SiS_HiVision & 0x04) {	/* TW: New from 650/30xLV 1.10.6s */
	      temp &= (SetCRT2ToHiVisionTV | SwitchToCRT2 | SetSimuScanMode); 	/* 0x83 */
	      temp |= SetCRT2ToSVIDEO;  					/* 0x08 */
	   }
	}
#endif	
    	if(SiS_Pr->SiS_IF_DEF_FSTN) {   /* fstn must set CR30=0x21 */
       		temp = (SetCRT2ToLCD | SetSimuScanMode);
       		SiS_SetReg1(SiS_Pr->SiS_P3d4,0x30,temp);
    	}
    	tempbx |= temp;
    	tempax = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) << 8;
        tempax &= (LoadDACFlag | DriverMode | SetDispDevSwitch | SetNotSimuMode | SetPALTV);
    	tempbx |= tempax;
    	tempbx &= ~(SetCHTVOverScan | SetInSlaveMode | DisableCRT2Display);;

#ifdef SIS315H

	if(HwDeviceExtension->jChipType >= SIS_315H) {
    	   if(SiS_Pr->SiS_VBType & (VB_SIS302B | VB_SIS301LV | VB_SIS302LV)) {
	      /* From 1.10.7w, not in 1.10.8r */
	      if(ModeNo == 0x03) {   
	         /* Mode 0x03 is never in driver mode */
		 SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x31,0xbf);
	      }
	      /* From 1.10.7w, not in 1.10.8r */
	      if(!(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) & (DriverMode >> 8))) {
	         /* Reset LCDA setting */
		 SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x38,0xfc);
	      }
	      if(IS_SIS650) {
	         if(SiS_Pr->SiS_UseLCDA) {
		    if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x5f) & 0xF0) {
		       if((ModeNo <= 0x13) || (!(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) & (DriverMode >> 8)))) {
		          SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x38,(EnableDualEdge | SetToLCDA));  /* 3 */
		       }
		    }
		 }
#if 0		 /* We can't detect it this way; there are machines which do not use LCDA despite
                  * the chip revision
		  */      	      
		 if((tempbx & SetCRT2ToLCD) && (SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30) & SetCRT2ToLCD)) {
                    if((SiS_GetReg1(SiS_Pr->SiS_P3d4, 0x36) & 0x0f) == SiS_Pr->SiS_Panel1400x1050) {
		       if((ModeNo <= 0x13) || (!(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) & (DriverMode >> 8)))) {
		          if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x5f) & 0xF0) {
			     SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x38,(EnableDualEdge | SetToLCDA));  /* 3 */
			  }
		       }
		    } else {
		       if((ModeNo <= 0x13) || (!(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) & (DriverMode >> 8)))) {
			  if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x5f) & 0xF0) {
			     SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x38,(EnableDualEdge | SetToLCDA));  /* 3 */
		          }
		       }
		    }
		 }
#endif		 
	      }
	      temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
       	      if((temp & (EnableDualEdge | SetToLCDA)) == (EnableDualEdge | SetToLCDA)) {
          		tempbx |= SetCRT2ToLCDA;
	      }
    	   }

	   if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	        temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
	        if(temp & SetToLCDA)
		   tempbx |= SetCRT2ToLCDA;
		if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	           if(temp & EnableLVDSHiVision)
		      tempbx |= SetCRT2ToHiVisionTV;
		}
	   }
	}

#endif  /* SIS315H */

    	if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
	        temp = SetCRT2ToLCDA   | SetCRT2ToSCART      | SetCRT2ToLCD    |
		       SetCRT2ToRAMDAC | SetCRT2ToSVIDEO     | SetCRT2ToAVIDEO |  /* = 0x807C; */
                       SetCRT2ToHiVisionTV; 					  /* = 0x80FC; */
    	} else {
                if(HwDeviceExtension->jChipType >= SIS_315H) {
                    if(SiS_Pr->SiS_IF_DEF_CH70xx != 0)
        		temp = SetCRT2ToLCDA   | SetCRT2ToSCART |
			       SetCRT2ToLCD    | SetCRT2ToHiVisionTV |
			       SetCRT2ToAVIDEO | SetCRT2ToSVIDEO;  /* = 0x80bc */
      		    else
        		temp = SetCRT2ToLCDA | SetCRT2ToLCD;
		} else {
      		    if(SiS_Pr->SiS_IF_DEF_CH70xx != 0)
        		temp = SetCRT2ToTV | SetCRT2ToLCD;
      		    else
        		temp = SetCRT2ToLCD;
		}
    	}

    	if(!(tempbx & temp)) {
      		tempax = DisableCRT2Display;
      		tempbx = 0;
    	}

   	if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
      		if(tempbx & SetCRT2ToLCDA) {
        		tempbx &= (0xFF00|SwitchToCRT2|SetSimuScanMode);
      		}
		if(tempbx & SetCRT2ToRAMDAC) {
        		tempbx &= (0xFF00|SetCRT2ToRAMDAC|SwitchToCRT2|SetSimuScanMode);
      		}
		if((tempbx & SetCRT2ToLCD) /* && (!(SiS_Pr->SiS_VBType & VB_NoLCD)) */ ) {
		        /* We initialize the Panel Link of the type of bridge is DH */
        		tempbx &= (0xFF00|SetCRT2ToLCD|SwitchToCRT2|SetSimuScanMode);
      		}
		if(tempbx & SetCRT2ToSCART) {
        		tempbx &= (0xFF00|SetCRT2ToSCART|SwitchToCRT2|SetSimuScanMode);
        		tempbx |= SetPALTV;
      		}
		if(tempbx & SetCRT2ToHiVisionTV) {
        		tempbx &= (0xFF00|SetCRT2ToHiVisionTV|SwitchToCRT2|SetSimuScanMode);
        		tempbx |= SetPALTV;
      		}
   	} else { /* LVDS */
	        if(HwDeviceExtension->jChipType >= SIS_315H) {
		    if(tempbx & SetCRT2ToLCDA)
		        tempbx &= (0xFF00|SwitchToCRT2|SetSimuScanMode);
		}
      		if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
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

	/* TW: LVDS (LCD/TV) and 301BDH (LCD) can only be slave in 8bpp modes */
	if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	   if( (SiS_Pr->SiS_IF_DEF_LVDS == 1) ||
	       ((tempbx & SetCRT2ToLCD) && (SiS_Pr->SiS_VBType & VB_NoLCD)) ) {
	       modeflag &= (~CRT2Mode);
	   }
	}
	
    	if(!(tempbx & SetSimuScanMode)){
      	    if(tempbx & SwitchToCRT2) {
        	if((!(modeflag & CRT2Mode)) && (checkcrt2mode)) {
		     if( (HwDeviceExtension->jChipType >= SIS_315H) &&
			 (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) ) {
			 if(resinfo != 0x0a)
                              tempbx |= SetSimuScanMode;
		     } else {
            		      tempbx |= SetSimuScanMode;
	             }
        	}
      	    } else {
        	if(!(SiS_BridgeIsEnable(SiS_Pr,BaseAddr,HwDeviceExtension))) {
          	     if(!(tempbx & DriverMode)) {
            		  if(SiS_BridgeInSlave(SiS_Pr)) {
			      tempbx |= SetSimuScanMode;
            		  }
                     }
                }
      	    }
    	}

    	if(!(tempbx & DisableCRT2Display)) {
            if(tempbx & DriverMode) {
                if(tempbx & SetSimuScanMode) {
          	    if((!(modeflag & CRT2Mode)) && (checkcrt2mode)) {
	                if( (HwDeviceExtension->jChipType >= SIS_315H) &&
			    (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) ) {
			     if(resinfo != 0x0a) {  /* TW: 650/301 BIOS */
			          tempbx |= SetInSlaveMode;
            		          if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
              		 	     if(tempbx & SetCRT2ToTV) {
                		         if(!(tempbx & SetNotSimuMode))
					     SiS_Pr->SiS_SetFlag |= TVSimuMode;
              			     }
                                  }
			     }                      /* TW: 650/301 BIOS */
		        } else {
            		    tempbx |= SetInSlaveMode;
            		    if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
              		        if(tempbx & SetCRT2ToTV) {
                		    if(!(tempbx & SetNotSimuMode))
					SiS_Pr->SiS_SetFlag |= TVSimuMode;
              			}
            		    }
                        }
	            }
                }
            } else {
                tempbx |= SetInSlaveMode;
        	if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
          	    if(tempbx & SetCRT2ToTV) {
            		if(!(tempbx & SetNotSimuMode))
			    SiS_Pr->SiS_SetFlag |= TVSimuMode;
          	    }
        	}
      	    }
    	}
	
	if(SiS_Pr->SiS_CHOverScan) {
    	   if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
      		temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x35);
      		if((temp & TVOverScan) || (SiS_Pr->SiS_CHOverScan == 1) )
		      tempbx |= SetCHTVOverScan;
    	   }
	   if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
      		temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x79);
      		if( (temp & 0x80) || (SiS_Pr->SiS_CHOverScan == 1) )
		      tempbx |= SetCHTVOverScan;
    	   }
	}
  }

  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
#ifdef SIS300
     	if((HwDeviceExtension->jChipType==SIS_630) ||
           (HwDeviceExtension->jChipType==SIS_730)) {
	   	if(ROMAddr && SiS_Pr->SiS_UseROM) {
			OutputSelect = ROMAddr[0xfe];
                }
           	if(!(OutputSelect & EnablePALMN))
             		SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x35,0x3F);
           	if(tempbx & SetCRT2ToTV) {
              		if(tempbx & SetPALTV) {
                  		temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x35);
                  		if(temp & EnablePALM) tempbx &= (~SetPALTV);
             		}
          	}
      	}
#endif
#ifdef SIS315H
     	if(HwDeviceExtension->jChipType >= SIS_315H) {
	        if(ROMAddr && SiS_Pr->SiS_UseROM) {
		    OutputSelect = ROMAddr[0xf3];
		    if(HwDeviceExtension->jChipType == SIS_330) {
			OutputSelect = ROMAddr[0x11b];
		    }
                }
		if(!(OutputSelect & EnablePALMN))
        		SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x38,0x3F);
   		if(tempbx & SetCRT2ToTV) {
    			if(tempbx & SetPALTV) {
               			temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
               			if(temp & EnablePALM) tempbx &= (~SetPALTV);
              		}
        	}
  	}
#endif
  }
  
  /* PALM/PALN on Chrontel 7019 */
  SiS_Pr->SiS_CHPALM = SiS_Pr->SiS_CHPALN = FALSE;
  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
  	if(tempbx & SetCRT2ToTV) {
    		if(tempbx & SetPALTV) {
        		temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
        		if(temp & EnablePALM)      SiS_Pr->SiS_CHPALM = TRUE;
			else if(temp & EnablePALN) SiS_Pr->SiS_CHPALN = TRUE;
        	}
        }
  }

  SiS_Pr->SiS_VBInfo = tempbx;

  if(HwDeviceExtension->jChipType == SIS_630) {
       SiS_WhatIsThis(SiS_Pr, SiS_Pr->SiS_VBInfo);
  }

#ifdef TWDEBUG
#ifdef LINUX_KERNEL
  printk(KERN_DEBUG "sisfb: (VBInfo= 0x%04x, SetFlag=0x%04x)\n", 
      SiS_Pr->SiS_VBInfo, SiS_Pr->SiS_SetFlag);
#endif
#ifdef LINUX_XF86
  xf86DrvMsgVerb(0, X_PROBED, 3, "(init301: VBInfo=0x%04x, SetFlag=0x%04x)\n", 
      SiS_Pr->SiS_VBInfo, SiS_Pr->SiS_SetFlag);
#endif
#endif

#if 0  /* TW: Incomplete! (But does not seem to be required) */
  if(HwDeviceExtension->jChipType < SIS_315H) {
     /* TW: From A901/630+301B BIOS */
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
         if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x13) & 0x80)
     }
     if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
         if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x13) & 0x80)
	     if( [si] == 3) ModeIdIndex = 0x3f2b;
	 }
     }
     SiS_SetRegAND(SiS_Pr->SiS_P3d4, 0x31, 0xF7);
     if(ModeNo == 0x13) bp+4 = 0x03;
  } else {
     /* From 650/30xLV BIOS: */
     SiS_SetRegAND(SiS_Pr->SiS_P3d4, 0x31, 0xF7);
     if(ModeNo == 0x13) bp+4 = 0x03;
     else bp+4 = ModeNo;
  }
#endif

  /* TW: 630/301B and 650/301 (not 301LV!) BIOSes do more here, but this seems for DOS mode */

}

void
SiS_WhatIsThis(SiS_Private *SiS_Pr, USHORT myvbinfo)
{
   unsigned long eax, temp;
   unsigned short temp1;

   if(!(SiS_Pr->SiS_ChSW)) return;

#ifndef LINUX_XF86
   SiS_SetReg4(0xcf8,0x80000874);
   eax = SiS_GetReg3(0xcfc);
#else
   eax = pciReadLong(0x00000800, 0x74);
#endif
   eax &= 0xFFFF;
   temp = eax;
   eax += 0x3c;
   temp1 = SiS_GetReg4((USHORT)eax);
   temp1 &= 0xFEFF;
   SiS_SetReg5((USHORT)eax, temp1);
   temp1 = SiS_GetReg4((USHORT)eax);
   eax = temp;
   eax += 0x3a;
   temp1 = SiS_GetReg4((USHORT)eax);
   temp1 &= 0xFEFF;
   if(!(myvbinfo & SetCRT2ToTV)) {
      temp1 |= 0x0100;
   }
   SiS_SetReg5((USHORT)eax, temp1);
   temp1 = SiS_GetReg4((USHORT)eax);
}

void
SiS_GetRAMDAC2DATA(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                   USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempax=0,tempbx=0;
  USHORT temp1=0,modeflag=0,tempcx=0;
  USHORT StandTableIndex,CRT1Index;
#ifdef SIS315H   
  USHORT ResInfo,DisplayType,temp=0;
  const  SiS_LVDSCRT1DataStruct *LVDSCRT1Ptr = NULL;
#endif

  SiS_Pr->SiS_RVBHCMAX  = 1;
  SiS_Pr->SiS_RVBHCFACT = 1;

  if(ModeNo <= 0x13){

    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	StandTableIndex = SiS_GetModePtr(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex);
    	tempax = SiS_Pr->SiS_StandTable[StandTableIndex].CRTC[0];
    	tempbx = SiS_Pr->SiS_StandTable[StandTableIndex].CRTC[6];
	temp1 = SiS_Pr->SiS_StandTable[StandTableIndex].CRTC[7];

  } else {

     if( (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) ) {

#ifdef SIS315H     
    	temp = SiS_GetLVDSCRT1Ptr(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,
			RefreshRateTableIndex,&ResInfo,&DisplayType);

    	if(temp == 0)  return;

    	switch(DisplayType) {
    		case 0 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_1;		break;
    		case 1 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_1;          break;
    		case 2 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_1;         break;
    		case 3 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_1_H;         break;
    		case 4 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_1_H;        break;
    		case 5 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_1_H;       break;
    		case 6 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_2;           break;
    		case 7 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_2;          break;
    		case 8 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_2;         break;
    		case 9 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_2_H;         break;
    		case 10: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_2_H;        break;
    		case 11: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_2_H;       break;
		case 12: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1XXXxXXX_1;           break;
		case 13: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1XXXxXXX_1_H;         break;
		case 14: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_1;         break;
		case 15: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_1_H;       break;
		case 16: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_2;         break;
		case 17: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_2_H;       break;
    		case 18: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1UNTSC;               break;
    		case 19: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1ONTSC;               break;
    		case 20: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1UPAL;                break;
    		case 21: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1OPAL;                break;
    		case 22: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1320x480_1;           break;
		case 23: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_1;          break;
    		case 24: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_1_H;        break;
    		case 25: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_2;          break;
    		case 26: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_2_H;        break;
    		case 27: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_1;          break;
    		case 28: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_1_H;        break;
    		case 29: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_2;          break;
    		case 30: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_2_H;        break;
		case 36: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_1;         break;
		case 37: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_1_H;       break;
		case 38: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_2;         break;
		case 39: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_2_H;       break;
		case 99: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1OPAL;                break;
		default: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_1;          break;
    	}
	tempax = (LVDSCRT1Ptr+ResInfo)->CR[0];
	tempax |= (LVDSCRT1Ptr+ResInfo)->CR[14] << 8;
	tempax &= 0x03FF;
    	tempbx = (LVDSCRT1Ptr+ResInfo)->CR[6];
    	tempcx = (LVDSCRT1Ptr+ResInfo)->CR[13] << 8;
    	tempcx &= 0x0100;
    	tempcx <<= 2;
    	tempbx |= tempcx;
	temp1  = (LVDSCRT1Ptr+ResInfo)->CR[7];
#endif	

    } else {

    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	CRT1Index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;
#if 0   /* Not any longer */	
	if(HwDeviceExtension->jChipType < SIS_315H)  CRT1Index &= 0x3F;
#endif	
	tempax = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[0];
	tempax |= SiS_Pr->SiS_CRT1Table[CRT1Index].CR[14] << 8;
        tempax &= 0x03FF;
    	tempbx = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[6];
    	tempcx = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[13] << 8;
    	tempcx &= 0x0100;
    	tempcx <<= 2;
    	tempbx |= tempcx;
	temp1  = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[7];

    }

  }

  if(temp1 & 0x01) tempbx |= 0x0100;
  if(temp1 & 0x20) tempbx |= 0x0200;
  
  tempax += 5;

  /* Charx8Dot is no more used (and assumed), so we set it */
  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      modeflag |= Charx8Dot;
  }

  if(modeflag & Charx8Dot) tempax *= 8;
  else                     tempax *= 9;

  /* TW: From 650/30xLV 1.10.6s */
  if(modeflag & HalfDCLK)  tempax <<= 1;

  SiS_Pr->SiS_VGAHT = SiS_Pr->SiS_HT = tempax;
  tempbx++;
  SiS_Pr->SiS_VGAVT = SiS_Pr->SiS_VT = tempbx;
}

void
SiS_UnLockCRT2(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr)
{
  if(HwDeviceExtension->jChipType >= SIS_315H)
    	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2f,0x01);
  else
    	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x24,0x01);
}

void
SiS_LockCRT2(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr)
{
  if(HwDeviceExtension->jChipType >= SIS_315H)
    	SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2F,0xFE);
  else
     	SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x24,0xFE);
}

void
SiS_EnableCRT2(SiS_Private *SiS_Pr)
{
  SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
}

/* Checked against all BIOSes */
void
SiS_DisableBridge(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT tempah,pushax=0,modenum;
#endif
  USHORT temp=0;
  UCHAR *ROMAddr = HwDeviceExtension->pjVirtualRomBase;

  if (SiS_Pr->SiS_IF_DEF_LVDS == 0) {

      if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {   /* ===== TW: For 30xB/LV ===== */

        if(HwDeviceExtension->jChipType < SIS_315H) {

#ifdef SIS300	   /* 300 series */

           if(HwDeviceExtension->jChipType == SIS_300) {  /* New for 300+301LV */

	      if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwDeviceExtension))) {
	         if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	            SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xFE,0x00);
		    SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 3);
		 }
	      }
	      if(SiS_Is301B(SiS_Pr,BaseAddr)) {
	         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1f,0x3f);
	         SiS_ShortDelay(SiS_Pr,1);
	      }
	      SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xDF);
	      SiS_DisplayOff(SiS_Pr);
	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	      if( (!(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension))) ||
	          (!(SiS_CR36BIOSWord23d(SiS_Pr,HwDeviceExtension))) ) {
	         SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
                 SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xFB,0x04);
	      }

	   } else {

	      if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwDeviceExtension))) {
	         SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xF7,0x08);
	         SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 3);
	      }
	      if(SiS_Is301B(SiS_Pr,BaseAddr)) {
	         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1f,0x3f);
	         SiS_ShortDelay(SiS_Pr,1);
	      }
	      SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xDF);
	      SiS_DisplayOff(SiS_Pr);
	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	      SiS_UnLockCRT2(SiS_Pr,HwDeviceExtension,BaseAddr);
	      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x01,0x80);
	      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x02,0x40);
	      if( (!(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension))) ||
	          (!(SiS_CR36BIOSWord23d(SiS_Pr,HwDeviceExtension))) ) {
	         SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
                 SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xFB,0x04);
	      }
	   }

#endif  /* SIS300 */

        } else {

#ifdef SIS315H	   /* 310/325 series */

           if(IS_SIS650740) {		/* 650, 740 */

#if 0
	      if(SiS_GetReg1(SiS_Pr->SiS_Part4Port,0x00) != 1) return;	/* From 1.10.7w */
#endif

	      modenum = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x34);

              if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	      
	         SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x30,0x00);
		 
		 if( (modenum <= 0x13) ||
		     (!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) ||
		     (SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) ) {
	     	      SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xFE,0x00);
	         }
		 SiS_DDC2Delay(SiS_Pr,0xff00);
		 SiS_DDC2Delay(SiS_Pr,0x6000);
		 SiS_DDC2Delay(SiS_Pr,0x8000);

	         SiS_SetReg3(SiS_Pr->SiS_P3c6,0x00);

                 pushax = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x06);
		 
		 if(IS_SIS740) {
		    SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x06,0xE3);
		 }

	         SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 3);
		 
		 if(!(SiS_IsNotM650or651(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	            tempah = 0xef;
	            if(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	               tempah = 0xf7;
                    }
	            SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x4c,tempah);
	         }

              }

              if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
	         tempah = 0x3f;
	         if(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	            tempah = 0x7f;
	            if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		       tempah = 0xbf;
                    }
	         }
	         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,tempah);
	      }

              if((SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) ||
	         ((SiS_Pr->SiS_VBType & VB_SIS301LV302LV) && (modenum <= 0x13))) {

	         if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
		    SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1E,0xDF);
		    SiS_DisplayOff(SiS_Pr);
		    SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
		 } else {
	            SiS_DisplayOff(SiS_Pr);
	            SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
	            SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	            SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1E,0xDF);
		    if((SiS_Pr->SiS_VBType & VB_SIS301LV302LV) && (modenum <= 0x13)) {
		       SiS_DisplayOff(SiS_Pr);
	               SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	               SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
	               SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	               temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00);
                       SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	               SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	               SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x00,temp);
		    }
		 }

	      } else {

	         if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
		    if(!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		       SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xdf);
		       SiS_DisplayOff(SiS_Pr);
		    }
		    SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
		    SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
		    temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00);
                    SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	            SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	            SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x00,temp);
		 } else {
                    SiS_DisplayOff(SiS_Pr);
	            SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	            SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
	            SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	            temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00);
                    SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	            SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	            SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x00,temp);
		 }

	      }

	      if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {

		 SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1f,~0x10);    /* 1.10.8r */
		 
	         tempah = 0x3f;
	         if(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	            tempah = 0x7f;
	            if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		       tempah = 0xbf;
                    }
	         }
	         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,tempah);

		 if(SiS_IsNotM650or651(SiS_Pr,HwDeviceExtension, BaseAddr)) {   /* 1.10.8r */
	            SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
		 }								/* 1.10.8r */

	         if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	            SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xdf);
	         }

	         if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	            if(!(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension))) {
	               if(!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		          SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xFD,0x00);
                       }
                    }
	         }

	         SiS_SetReg1(SiS_Pr->SiS_P3c4,0x06,pushax);

  	      }

#if 0
          } else if(IS_SIS740) {	/* 740 */
	  
	     if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {   /* 30xLV */
	     
	        if( (!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) ||
		    (SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) ) {
	     	      SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xFE,0x00);
	        }
		
		SiS_SetReg3(SiS_Pr->SiS_P3c6,0x00);

                pushax = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x06);
		
		SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x06,0xE3);

	        SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 3);
		
		if(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) {
		   SiS_DisplayOff(SiS_Pr);
	           SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
	           SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	           SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1E,0xDF);
		} else {
		   SiS_DisplayOff(SiS_Pr);
	           SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	           SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
	           SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	           temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00);
                   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	           SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	           SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x00,temp);
		}
		
		SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,0x3F);
		SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,0xEF);  /* (from 650) */
		
		SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
		
		if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	           SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xdf);
	        }
		
		if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		   if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	              if(!(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension))) {
	                 if(!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		            SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xFD,0x00);
                         }
                      }
	           }
		}
	        SiS_SetReg1(SiS_Pr->SiS_P3c4,0x06,pushax);
	     
	     } else {	/* (301,) 301B */
	  
	        if(SiS_Is301B(SiS_Pr,BaseAddr)) {
	           SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,0x3F);
	        }
	     
	        SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xDF);
	        SiS_DisplayOff(SiS_Pr);
	        SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	        SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);

	        temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00);
                SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	        SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	        SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x00,temp);
		
	     }
#endif	  
	  } else {			/* 315, 330 - all bridge types */

	     if(SiS_Is301B(SiS_Pr,BaseAddr)) {
	        tempah = 0x3f;
	        if(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	           tempah = 0x7f;
	           if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		      tempah = 0xbf;
                   }
	        }
	        SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,tempah);
	        if(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	           SiS_DisplayOff(SiS_Pr);
		   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	        }
	     }
	     if( (!(SiS_Is301B(SiS_Pr,BaseAddr))) ||
	         (!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) ) {

 	 	if( (!(SiS_Is301B(SiS_Pr,BaseAddr))) ||
		    (!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) ) {

	           SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xDF);
	           SiS_DisplayOff(SiS_Pr);

		}

                SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);

                SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);

	        temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00);
                SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	        SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	        SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x00,temp);

	     }

	  }    /* 315/330 */

#endif /* SIS315H */

	}

      } else {     /* ============ TW: For 301 ================ */

        if(HwDeviceExtension->jChipType < SIS_315H) {
            if(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) {
                SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xF0,0x0B);
	        SiS_SetPanelDelay(SiS_Pr, ROMAddr, HwDeviceExtension, 1);
	    }
	}

        SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xDF);           /* disable VB */
        SiS_DisplayOff(SiS_Pr);

        if(HwDeviceExtension->jChipType >= SIS_315H) {
            SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	}

        SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);                /* disable lock mode */

	if(HwDeviceExtension->jChipType >= SIS_315H) {
            temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00);
            SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	    SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
	    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x00,temp);
	} else {
            SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);            /* disable CRT2 */
	}

      }

  } else {     /* ============ TW: For LVDS =============*/

    if(HwDeviceExtension->jChipType < SIS_315H) {

#ifdef SIS300	/* 300 series */

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
	    SiS_SetCH700x(SiS_Pr,0x090E);
	}

	if(HwDeviceExtension->jChipType == SIS_730) {
	   if(!(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x11) & 0x08)) {
	      SiS_WaitVBRetrace(SiS_Pr,HwDeviceExtension);
	   }
	   if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwDeviceExtension))) {
	      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xF7,0x08);
	      SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 3);
	   }
	} else {
	   if(!(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x11) & 0x08)) {

	      if(!(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x13) & 0x40)) {
  
  	          if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwDeviceExtension))) {

                     SiS_WaitVBRetrace(SiS_Pr,HwDeviceExtension);

		     if(!(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x06) & 0x1c)) {
		         SiS_DisplayOff(SiS_Pr);
	             }

	             SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xF7,0x08);
	             SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 3);
                  }
              }
	   }
	}

	SiS_DisplayOff(SiS_Pr);

	SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);

	SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	SiS_UnLockCRT2(SiS_Pr,HwDeviceExtension,BaseAddr);
	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x01,0x80);
	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x02,0x40);

	if( (!(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension))) ||
	              (!(SiS_CR36BIOSWord23d(SiS_Pr,HwDeviceExtension))) ) {
		SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
		SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xFB,0x04);
	}

#endif  /* SIS300 */

    } else {

#ifdef SIS315H	/* 310/325 series */

	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
		temp = SiS_GetCH701x(SiS_Pr,0x61);
		if(temp < 1) {
		   SiS_SetCH701x(SiS_Pr,0xac76);
		   SiS_SetCH701x(SiS_Pr,0x0066);
		}
		
		if(!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) {
			SiS_SetCH701x(SiS_Pr,0x3e49);
		} else if(SiS_IsTVOrYPbPrOrScart(SiS_Pr,HwDeviceExtension, BaseAddr))  {
			SiS_SetCH701x(SiS_Pr,0x3e49);
		}
		
		if(!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) {
			SiS_Chrontel701xBLOff(SiS_Pr);
			SiS_Chrontel701xOff(SiS_Pr);
		} else if(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) {
			SiS_Chrontel701xBLOff(SiS_Pr);
			SiS_Chrontel701xOff(SiS_Pr);
		}
		
	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
		SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xF7,0x08);
		SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 3);
	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
		SiS_DisplayOff(SiS_Pr);
	} else if(!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		SiS_DisplayOff(SiS_Pr);
	} else if(!(SiS_IsTVOrYPbPrOrScart(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		SiS_DisplayOff(SiS_Pr);
	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
		SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	} else if(!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	} else if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	}

	SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
		SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	} else if(!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	} else if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	        if(SiS_CRT2IsLCD(SiS_Pr, BaseAddr,HwDeviceExtension)) {
			SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xdf);
		}
	} else if(SiS_IsLCDOrLCDA(SiS_Pr,HwDeviceExtension, BaseAddr)) {
		SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xdf);
	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	    	if(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr)) {
			SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xff);
		} else {
			SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xfb);
		}
	}

	SiS_UnLockCRT2(SiS_Pr,HwDeviceExtension, BaseAddr);

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
		SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0xf7);
	} else if(!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0xf7);
	} else if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
		SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0xf7);
	}

#if 0  /* TW: BIOS code makes no sense */
       if(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) {
           if(!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	        if(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr)) {
		  /* Nothing there! */
		}
           }
       }
#endif
       if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
		if(SiS_CRT2IsLCD(SiS_Pr, BaseAddr,HwDeviceExtension)) {
			if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr))) {
				SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
				SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xFB,0x04);
			}
		}
       }

#endif  /* SIS315H */

    }  /* 310 series */

  }  /* LVDS */

}

/* TW: Checked against all BIOSes */
void
SiS_EnableBridge(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
  USHORT temp=0,tempah;
#ifdef SIS315H
  USHORT temp1,pushax=0;
  BOOLEAN delaylong = FALSE;
#endif
  UCHAR *ROMAddr = HwDeviceExtension->pjVirtualRomBase;

  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {

    if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {   /* TW: ====== For 301B et al  ====== */

      if(HwDeviceExtension->jChipType < SIS_315H) {

#ifdef SIS300     /* 300 series */

         if(HwDeviceExtension->jChipType == SIS_300) {

	    if(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) {
	       if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) { 
	          SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x02);
	          if(!(SiS_CR36BIOSWord23d(SiS_Pr,HwDeviceExtension))) {
	             SiS_SetPanelDelay(SiS_Pr, ROMAddr, HwDeviceExtension, 0);
	          }
	       }
	    }
	    temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x32) & 0xDF;             /* lock mode */
            if(SiS_BridgeInSlave(SiS_Pr)) {
               tempah = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
               if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
            }
            SiS_SetReg1(SiS_Pr->SiS_P3c4,0x32,temp);
	    SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1F,0x20);        /* enable VB processor */
	    if(SiS_Is301B(SiS_Pr,BaseAddr)) {
               SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,0xC0);
	       SiS_DisplayOn(SiS_Pr);
	    } else {
	       SiS_VBLongWait(SiS_Pr);
	       SiS_DisplayOn(SiS_Pr);
	       SiS_VBLongWait(SiS_Pr);
	    }
	    if(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) {
	       if(!(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x13) & 0x40)) {
		  if(!(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x16) & 0x10)) {
		     if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwDeviceExtension))) {
		           SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 1);
                     }
		     if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) { 
		         SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xfe,0x01);
	 	     }
		  }
               }
	    }

	 } else {

	    if((SiS_Pr->SiS_VBType & VB_NoLCD) &&
	       (SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension))) {
	       /* This is only for LCD output on 301B-DH via LVDS */
	       SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x11,0xFB);
	       if(!(SiS_CR36BIOSWord23d(SiS_Pr,HwDeviceExtension))) {
	          SiS_SetPanelDelay(SiS_Pr, ROMAddr, HwDeviceExtension, 0);
	       }
	       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);   /* Enable CRT2 */
/*	       DoSomeThingPCI_On(SiS_Pr) */
               SiS_DisplayOn(SiS_Pr);
	       SiS_UnLockCRT2(SiS_Pr,HwDeviceExtension, BaseAddr);
	       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x02,0xBF);
	       if(SiS_BridgeInSlave(SiS_Pr)) {
      		  SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x01,0x1F);
      	       } else {
      		  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x01,0x1F,0x40);
               }
	       if(!(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x13) & 0x40)) {
	           if(!(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x16) & 0x10)) {
		       if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwDeviceExtension))) {
		           SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 1);
                       }
		       SiS_WaitVBRetrace(SiS_Pr,HwDeviceExtension);
                       SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xF7,0x00);
                   }
	       }
            } else {
	       temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x32) & 0xDF;             /* lock mode */
               if(SiS_BridgeInSlave(SiS_Pr)) {
                  tempah = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
                  if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
               }
               SiS_SetReg1(SiS_Pr->SiS_P3c4,0x32,temp);
	       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
	       SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1F,0x20);        /* enable VB processor */
	       if(SiS_Is301B(SiS_Pr,BaseAddr)) {
                  SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,0xC0);
	          SiS_DisplayOn(SiS_Pr);
	       } else {
	          SiS_VBLongWait(SiS_Pr);
	          SiS_DisplayOn(SiS_Pr);
	          SiS_VBLongWait(SiS_Pr);
	       }
	    }

         }
#endif /* SIS300 */

      } else {

#ifdef SIS315H    /* 310/325 series */

	 if(IS_SIS650740) {		/* 650 */

#if 0
	    if(SiS_GetReg1(SiS_Pr->SiS_Part4Port,0x00) != 1) return;	/* From 1.10.7w */
#endif

	    if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	    
	       SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1f,0xef);  /* 1.10.7u */
	       SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x30,0x00);    /* 1.10.7u */

	       if(!(IS_SIS740)) {
                  if(!(SiS_IsNotM650or651(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	             tempah = 0x10;
	             if(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	                tempah = 0x08;
                     }
	             SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x4c,tempah);
	          }
	       }

	       SiS_SetReg3(SiS_Pr->SiS_P3c6,0x00);
	       SiS_DisplayOff(SiS_Pr);
	       pushax = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x06);
	       if(IS_SIS740) {
	          SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x06,0xE3);
	       }

	       if( (SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) ||
	           (SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) ) {
                   if(!(SiS_GetReg1(SiS_Pr->SiS_Part4Port,0x26) & 0x02)) {
		      SiS_SetPanelDelayLoop(SiS_Pr,ROMAddr, HwDeviceExtension, 3, 2);
		      SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x02);
	              SiS_SetPanelDelayLoop(SiS_Pr,ROMAddr, HwDeviceExtension, 3, 2);
	           }
	       }

	       if(!(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) & 0x40)) {
                  SiS_SetPanelDelayLoop(SiS_Pr,ROMAddr, HwDeviceExtension, 3, 10);
		  delaylong = TRUE;
	       }

	    }

	    if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
               temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x32) & 0xDF;
	       if(SiS_BridgeInSlave(SiS_Pr)) {
                  tempah = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
                  if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
               }
               SiS_SetReg1(SiS_Pr->SiS_P3c4,0x32,temp);

	       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);                   /* enable CRT2 */
	       
	       if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
	          SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
		  temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x2e);
		  if(!(temp & 0x80)) {
		     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2e,0x80);
		  }
	       } else {
	          SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
	       }
	    }

	    if(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	          SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1e,0x20);
	    }

	    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1f,0x20);

	    if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
	       temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x2e);
	       if(!(temp & 0x80)) {
		  SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2e,0x80);
	       }
	    }

	    tempah = 0xc0;
	    if(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	       tempah = 0x80;
	       if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	          tempah = 0x40;
               }
	    }
            SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,tempah);

	    if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
               SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
	    }

	    if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	    
	       SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x30,0x00);	/* All this from 1.10.7u */
	       SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x27,0x0c);
	       SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x30,0x20);
	       SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x31,0x05);  
	       SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x32,0x60);  
	       SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x33,0x00);  
	       SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x34,0x10); 
	       SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x30,0x40); 
	       
	       SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
	    
	       SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1f,0x10);  /* 1.10.8r */

	       SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2e,0x80);

	       if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	          if( (SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) ||
	              ((SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension))) ) {
		    SiS_SetPanelDelayLoop(SiS_Pr,ROMAddr, HwDeviceExtension, 3, 10);
		    if(delaylong) {
			SiS_SetPanelDelayLoop(SiS_Pr,ROMAddr, HwDeviceExtension, 3, 10);
		    }
                    SiS_WaitVBRetrace(SiS_Pr,HwDeviceExtension);
		    SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xfe,0x01);
	         }
	      }

	      SiS_SetReg1(SiS_Pr->SiS_P3c4,0x06,pushax);
	      SiS_DisplayOn(SiS_Pr);
	      SiS_SetReg3(SiS_Pr->SiS_P3c6,0xff);

	      if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	          SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
	      }
#if 0
              SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x30,0x00);
	      SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x27,0x0c);
	      SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x30,0x20);
	      SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x31,0x05);   /* 1.10.8r: 0x0d */
	      SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x32,0x60);   /* 1.10.8r: 0x70 */
	      SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x33,0x00);   /* 1.10.8r: 0x40 */
	      SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x34,0x10); 
	      SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x30,0x40); 
#endif	      

	  }

#if 0
         } else if(IS_SIS740) {		/* 740 */
	 
	   if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {  /* 30xLV */
	   
	      SiS_SetReg3(SiS_Pr->SiS_P3c6,0x00);
	      SiS_DisplayOff(SiS_Pr);
	      pushax = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x06);
	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x06,0xE3);
	      
	      if( (SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) ||
	          (SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) ) {
                   if(!(SiS_GetReg1(SiS_Pr->SiS_Part4Port,0x26) & 0x02)) {
		      SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x02);
	              SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 3);
		   }
	      }
	      
	      if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	         temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x32) & 0xDF;
	         if(SiS_BridgeInSlave(SiS_Pr)) {
                    tempah = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
                    if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
                 }
                 SiS_SetReg1(SiS_Pr->SiS_P3c4,0x32,temp);

	         SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);  	
		 SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 2);
	      }
	      
	      if(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	         SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x20);
	      }
	      
	      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1f,0x20);
	      SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,0xC0);
	      SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,0x10);  /* (taken from 650 1.10.8r) */
	      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);
	      
	      if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	         if( (SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) ||
	             (SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) ) {
		    SiS_SetPanelDelayLoop(SiS_Pr,ROMAddr, HwDeviceExtension, 3, 10);
		    if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) & 0x40) {
		       SiS_WaitVBRetrace(SiS_Pr,HwDeviceExtension);
		    }
		    SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xfe,0x01);
		    SiS_SetPanelDelayLoop(SiS_Pr,ROMAddr, HwDeviceExtension, 3, 10);
		    SiS_WaitVBRetrace(SiS_Pr,HwDeviceExtension);
	         }
              }
	      
	      SiS_SetReg1(SiS_Pr->SiS_P3c4,0x06,pushax);
	      SiS_DisplayOn(SiS_Pr);
	      SiS_SetReg3(SiS_Pr->SiS_P3c6,0xff);
	      
	      if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	         SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
	      }
	   
	   } else {	/* (301), 301B */
	 
	      if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	         temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x32) & 0xDF;
	         if(SiS_BridgeInSlave(SiS_Pr)) {
                    tempah = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
                    if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
                 }
                 SiS_SetReg1(SiS_Pr->SiS_P3c4,0x32,temp);

	         SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);                   /* enable CRT2 */

	         temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x2E);
                 if(!(temp & 0x80))
                    SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);
              }

	      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1f,0x20);

	      if(SiS_Is301B(SiS_Pr,BaseAddr)) { 
	         SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,0xC0);
	         SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
	      } else {
	         SiS_VBLongWait(SiS_Pr);
                 SiS_DisplayOn(SiS_Pr);
	         SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7F);
                 SiS_VBLongWait(SiS_Pr);
	      }
	      
	   }
#endif
	  
	 } else {			/* 315, 330 */

	   if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	      temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x32) & 0xDF;
	      if(SiS_BridgeInSlave(SiS_Pr)) {
                 tempah = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
                 if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
              }
              SiS_SetReg1(SiS_Pr->SiS_P3c4,0x32,temp);

	      SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);                   /* enable CRT2 */

	      temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x2E);
              if(!(temp & 0x80))
                 SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);
           }

	   SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1f,0x20);

	   if(SiS_Is301B(SiS_Pr,BaseAddr)) {

	      temp=SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x2E);
              if (!(temp & 0x80))
                 SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);

	      tempah = 0xc0;
	      if(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	         tempah = 0x80;
	         if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
	            tempah = 0x40;
                 }
	      }
              SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,tempah);

	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);

	   } else {
	   
	      SiS_VBLongWait(SiS_Pr);
              SiS_DisplayOn(SiS_Pr);
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7F);
              SiS_VBLongWait(SiS_Pr);

	   }

	 }   /* 315, 330 */

#endif /* SIS315H */

      }

    } else {	/* ============  TW: For 301 ================ */

       if(HwDeviceExtension->jChipType < SIS_315H) {
            if(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) {
                SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xF0,0x0B);
	        SiS_SetPanelDelay(SiS_Pr, ROMAddr, HwDeviceExtension, 0);
	    }
       }

       temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x32) & 0xDF;          /* lock mode */
       if(SiS_BridgeInSlave(SiS_Pr)) {
         tempah = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
         if(!(tempah & SetCRT2ToRAMDAC))  temp |= 0x20;
       }
       SiS_SetReg1(SiS_Pr->SiS_P3c4,0x32,temp);

       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);                  /* enable CRT2 */

       if(HwDeviceExtension->jChipType >= SIS_315H) {
         temp = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x2E);
         if(!(temp & 0x80))
           SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);         /* BVBDOENABLE=1 */
       }

       SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1F,0x20);     /* enable VB processor */

       SiS_VBLongWait(SiS_Pr);
       SiS_DisplayOn(SiS_Pr);
       if(HwDeviceExtension->jChipType >= SIS_315H) {
           SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
       }
       SiS_VBLongWait(SiS_Pr);

       if(HwDeviceExtension->jChipType < SIS_315H) {
            if(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) {
	        SiS_SetPanelDelay(SiS_Pr, ROMAddr, HwDeviceExtension, 1);
                SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,0xF0,0x03);
	    }
       }

    }

  } else {   /* =================== TW: For LVDS ================== */

    if(HwDeviceExtension->jChipType < SIS_315H) {

#ifdef SIS300    /* 300 series */

      if(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) {
         if(HwDeviceExtension->jChipType == SIS_730) {
	    SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 1);
	    SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 1);
	    SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 1);
	 }
         SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x11,0xFB);
	 if(!(SiS_CR36BIOSWord23d(SiS_Pr,HwDeviceExtension))) {
	    SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 0);
	 }
      }

      SiS_EnableCRT2(SiS_Pr);
      SiS_DisplayOn(SiS_Pr);
      SiS_UnLockCRT2(SiS_Pr,HwDeviceExtension, BaseAddr);
      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x02,0xBF);
      if(SiS_BridgeInSlave(SiS_Pr)) {
      	SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x01,0x1F);
      } else {
      	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x01,0x1F,0x40);
      }

      if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
        if(!(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension))) {
	        SiS_SetCH700x(SiS_Pr,0x0B0E);
        }
      }

      if(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) {
          if(!(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x13) & 0x40)) {
              if(!(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x16) & 0x10)) {
	          if(!(SiS_CR36BIOSWord23b(SiS_Pr,HwDeviceExtension))) {
			SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 1);
        		SiS_SetPanelDelay(SiS_Pr,ROMAddr, HwDeviceExtension, 1);
		  }
		  SiS_WaitVBRetrace(SiS_Pr,HwDeviceExtension);
                  SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x11,0xF7);
              }
	  }
      }

#endif  /* SIS300 */

    } else {

#ifdef SIS315H    /* 310/325 series */

#if 0  /* BIOS code makes no sense */
       if(SiS_IsVAMode()) {
          if(SiS_IsLCDOrLCDA()) {
	  }
       }
#endif

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	    if(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) {
	     	SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x11,0xFB);
	        SiS_SetPanelDelay(SiS_Pr, ROMAddr, HwDeviceExtension, 0);
            }
       }

       SiS_EnableCRT2(SiS_Pr);
       SiS_UnLockCRT2(SiS_Pr,HwDeviceExtension, BaseAddr);

       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0xf7);

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
          temp = SiS_GetCH701x(SiS_Pr,0x66);
	  temp &= 0x20;
	  SiS_Chrontel701xBLOff(SiS_Pr);
       }

       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
       
#ifdef NEWCH701x
       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
           if(SiS_IsLCDOrLCDA(SiS_Pr,HwDeviceExtension,BaseAddr)) {
	   	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x20);
	   }
       }
#endif       

       temp1 = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x2E);
       if (!(temp1 & 0x80))
           SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
           if(temp) {
	       SiS_Chrontel701xBLOn(SiS_Pr);
	   }
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
           if(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) {
	   	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x20);
	   }
       } 
#ifndef NEWCH701x       
         else if(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) {
           SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x20);
       }
#endif       

       if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr))) {
           SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {

       		if(SiS_IsTVOrYPbPrOrScart(SiS_Pr,HwDeviceExtension, BaseAddr)) {
           		SiS_Chrontel701xOn(SiS_Pr,HwDeviceExtension, BaseAddr);
         	}

         	if(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) {
           		SiS_ChrontelDoSomething1(SiS_Pr,HwDeviceExtension, BaseAddr);
         	} else if(SiS_IsLCDOrLCDA(SiS_Pr,HwDeviceExtension, BaseAddr)) {
           		SiS_ChrontelDoSomething1(SiS_Pr,HwDeviceExtension, BaseAddr);
        	}

       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
       	 	if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr))) {
 	   		if(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	            		SiS_Chrontel701xBLOn(SiS_Pr);
	            		SiS_ChrontelDoSomething4(SiS_Pr,HwDeviceExtension, BaseAddr);
           		} else if(SiS_IsLCDOrLCDA(SiS_Pr,HwDeviceExtension, BaseAddr))  {
       				SiS_Chrontel701xBLOn(SiS_Pr);
       				SiS_ChrontelDoSomething4(SiS_Pr,HwDeviceExtension, BaseAddr);
	   		}
       		}
       } else if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
       		if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr))) {
			if(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) {
				SiS_SetPanelDelay(SiS_Pr, ROMAddr, HwDeviceExtension, 1);
				SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x11,0xF7);
			}
		}
       }

#endif  /* SIS315H */

    } /* 310 series */

  }  /* LVDS */

}

void
SiS_SiS30xBLOn(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT  BaseAddr = (USHORT)HwDeviceExtension->ulIOAddress;

  /* TW: Switch on LCD backlight on SiS30xLV */
  if( (SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) ||
      (SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension)) ) {
    if(!(SiS_GetReg1(SiS_Pr->SiS_Part4Port,0x26) & 0x02)) {
	SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x02);
	SiS_WaitVBRetrace(SiS_Pr,HwDeviceExtension);
    }
    if(!(SiS_GetReg1(SiS_Pr->SiS_Part4Port,0x26) & 0x01)) {
        SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x01);
    }
  }
}

void
SiS_SiS30xBLOff(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT  BaseAddr = (USHORT)HwDeviceExtension->ulIOAddress;

  /* TW: Switch off LCD backlight on SiS30xLV */
  if( (!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) ||
      (SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr)) ) {
	 SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xFE,0x00);
  }

  if(!(SiS_IsVAMode(SiS_Pr,HwDeviceExtension, BaseAddr))) {
      if(!(SiS_CRT2IsLCD(SiS_Pr,BaseAddr,HwDeviceExtension))) {
          if(!(SiS_IsDualEdge(SiS_Pr,HwDeviceExtension, BaseAddr))) {
  	      SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xFD,0x00);
          }
      }
  }
}

BOOLEAN
SiS_CR36BIOSWord23b(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,temp1;
  UCHAR *ROMAddr;

  if((ROMAddr = (UCHAR *)HwDeviceExtension->pjVirtualRomBase) && SiS_Pr->SiS_UseROM) {
     temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x36) & 0xff;
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
SiS_CR36BIOSWord23d(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,temp1;
  UCHAR *ROMAddr;

  if((ROMAddr = (UCHAR *)HwDeviceExtension->pjVirtualRomBase) && SiS_Pr->SiS_UseROM) {
     temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x36) & 0xff;
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
SiS_SetPanelDelayLoop(SiS_Private *SiS_Pr, UCHAR *ROMAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
                      USHORT DelayTime, USHORT DelayLoop)
{
   int i;
   for(i=0; i<DelayLoop; i++) {
      SiS_SetPanelDelay(SiS_Pr, ROMAddr, HwDeviceExtension, DelayTime);
   }
}		     

void
SiS_SetPanelDelay(SiS_Private *SiS_Pr, UCHAR *ROMAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
                  USHORT DelayTime)
{
  USHORT PanelID, DelayIndex, Delay;
#ifdef SIS300
  USHORT temp;
#endif

  if(HwDeviceExtension->jChipType < SIS_315H) {

#ifdef SIS300

      if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {			/* 300 series, LVDS */

	  PanelID = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x36);

	  DelayIndex = PanelID >> 4;

	  if((DelayTime >= 2) && ((PanelID & 0x0f) == 1))  {
              Delay = 3;
          } else {
              if(DelayTime >= 2) DelayTime -= 2;

              if(!(DelayTime & 0x01)) {
       		  Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[0];
              } else {
       		  Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[1];
              }
	      if((ROMAddr) && (SiS_Pr->SiS_UseROM)) {
                  if(ROMAddr[0x220] & 0x40) {
                      if(!(DelayTime & 0x01)) {
	    	          Delay = (USHORT)ROMAddr[0x225];
                      } else {
	    	          Delay = (USHORT)ROMAddr[0x226];
                      }
                  }
              }
          }
	  SiS_ShortDelay(SiS_Pr,Delay);

      } else {							/* 300 series, 301(B) */

	  PanelID = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x36);
	  temp = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x18);
          if(!(temp & 0x10))  PanelID = 0x12;

          DelayIndex = PanelID >> 4;

	  if((DelayTime >= 2) && ((PanelID & 0x0f) == 1))  {
              Delay = 3;
          } else {
              if(DelayTime >= 2) DelayTime -= 2;

              if(!(DelayTime & 0x01)) {
       		  Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[0];
              } else {
       		  Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[1];
              }
	      if((ROMAddr) && (SiS_Pr->SiS_UseROM)) {
                  if(ROMAddr[0x220] & 0x40) {
                      if(!(DelayTime & 0x01)) {
	    	          Delay = (USHORT)ROMAddr[0x225];
                      } else {
	    	          Delay = (USHORT)ROMAddr[0x226];
                      }
                  }
              }
          }
	  SiS_ShortDelay(SiS_Pr,Delay);

      }

#endif  /* SIS300 */

   } else {

      if(HwDeviceExtension->jChipType == SIS_330) return;

#ifdef SIS315H

      if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {			/* 310/325 series, LVDS */

          if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
              PanelID = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x36);
	      DelayIndex = PanelID >> 4;
	      if((DelayTime >= 2) && ((PanelID & 0x0f) == 1))  {
                 Delay = 3;
              } else {
                 if(DelayTime >= 2) DelayTime -= 2;

                 if(!(DelayTime & 0x01)) {
       		     Delay = SiS_Pr->SiS_PanelDelayTblLVDS[DelayIndex].timer[0];
                 } else {
       		     Delay = SiS_Pr->SiS_PanelDelayTblLVDS[DelayIndex].timer[1];
                 }
	         if((ROMAddr) && (SiS_Pr->SiS_UseROM)) {
                    if(ROMAddr[0x13c] & 0x40) {
                        if(!(DelayTime & 0x01)) {
	    	           Delay = (USHORT)ROMAddr[0x17e];
                        } else {
	    	           Delay = (USHORT)ROMAddr[0x17f];
                        }
                    }
                 }
              }
	      SiS_ShortDelay(SiS_Pr,Delay);
	  }

      } else {							/* 310/325 series, 301(B) */

          PanelID = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x36);
	  DelayIndex = PanelID >> 4;
          if(!(DelayTime & 0x01)) {
       		Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[0];
          } else {
       		Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[1];
          }
	  SiS_DDC2Delay(SiS_Pr, Delay * 4);

      }

#endif /* SIS315H */

   }

}

void
SiS_LongDelay(SiS_Private *SiS_Pr, USHORT delay)
{
  while(delay--) {
    SiS_GenericDelay(SiS_Pr,0x19df);
  }
}

void
SiS_ShortDelay(SiS_Private *SiS_Pr, USHORT delay)
{
  while(delay--) {
      SiS_GenericDelay(SiS_Pr,0x42);
  }
}

void
SiS_GenericDelay(SiS_Private *SiS_Pr, USHORT delay)
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
SiS_Is301B(SiS_Private *SiS_Pr, USHORT BaseAddr)
{
  USHORT flag;

  flag = SiS_GetReg1(SiS_Pr->SiS_Part4Port,0x01);
  if(flag >= 0x0B0) return(1);
  else return(0);
}

BOOLEAN
SiS_CRT2IsLCD(SiS_Private *SiS_Pr, USHORT BaseAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT flag;

  if(HwDeviceExtension->jChipType == SIS_730) {
     flag = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x13);
     if(flag & 0x20) return(1);
  }
  flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
  if(flag & 0x20) return(1);
  else return(0);
}

BOOLEAN
SiS_IsDualEdge(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     if((HwDeviceExtension->jChipType != SIS_650) || (SiS_GetReg1(SiS_Pr->SiS_P3d4,0x5f) & 0xf0)) {
        flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
        if(flag & EnableDualEdge) return(1);
        else return(0);
     } else return(0);
  } else
#endif
     return(0);
}

BOOLEAN
SiS_IsVAMode(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
     if((flag & EnableDualEdge) && (flag & SetToLCDA))   return(1);
#if 0 /* Not done in 650/30xLV 1.10.6s, but in 650/301LV */
     else if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
       if(flag) return(1);
       else     return(0);   			         
     }
#endif
     else
       return(0);
  } else
#endif
     return(0);
 }

BOOLEAN
SiS_WeHaveBacklightCtrl(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x79);
     if(flag & 0x10)  return(1);
     else             return(0);
  } else
#endif
     return(0);
 }

#if 0
BOOLEAN
SiS_Is315E(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x5f);
     if(flag & 0x10)  return(1);
     else      	      return(0);
  } else
#endif
     return(0);
}
#endif

BOOLEAN
SiS_IsNotM650or651(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType == SIS_650) {
     flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x5f);
     flag &= 0xF0;
     if((flag == 0xb0) || (flag == 0x90)) return 0;
     else return 1;
  } else
#endif
    return 1;
}

BOOLEAN
SiS_IsYPbPr(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
     if(flag & EnableLVDSHiVision)  return(1);  /* = YPrPb = 0x08 */
     else      	                    return(0);
  } else
#endif
     return(0);
}

BOOLEAN
SiS_IsChScart(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef SIS315H
  USHORT flag;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
     if(flag & EnableLVDSScart)     return(1);  /* = Scart = 0x04 */
     else      	                    return(0);
  } else
#endif
     return(0);
}

BOOLEAN
SiS_IsTVOrYPbPrOrScart(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
  USHORT flag;

#ifdef SIS315H
  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
     if(flag & SetCRT2ToTV)        return(1);
     flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
     if(flag & EnableLVDSHiVision) return(1);  /* = YPrPb = 0x08 */
     if(flag & EnableLVDSScart)    return(1);  /* = Scart = 0x04- TW inserted */
     else                          return(0);
  } else
#endif
  {
     flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
     if(flag & SetCRT2ToTV) return(1);
  }
  return(0);
}

BOOLEAN
SiS_IsLCDOrLCDA(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
  USHORT flag;

#ifdef SIS315H
  if(HwDeviceExtension->jChipType >= SIS_315H) {
     flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
     if(flag & SetCRT2ToLCD) return(1);
     flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
     if(flag & SetToLCDA)    return(1);
     else                    return(0);
  } else
#endif
  {
   flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
   if(flag & SetCRT2ToLCD)   return(1);
  }
  return(0);

}

BOOLEAN
SiS_IsDisableCRT2(SiS_Private *SiS_Pr, USHORT BaseAddr)
{
  USHORT flag;

  flag = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x30);
  if(flag & 0x20) return(0);
  else            return(1);
}

BOOLEAN
SiS_BridgeIsOn(SiS_Private *SiS_Pr, USHORT BaseAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT flag;

  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     return(0);
  } else {
     flag = SiS_GetReg1(SiS_Pr->SiS_Part4Port,0x00);
     if((flag == 1) || (flag == 2)) return(0);
     else return(1);
  }
}

BOOLEAN
SiS_BridgeIsEnable(SiS_Private *SiS_Pr, USHORT BaseAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT flag;

  if(!(SiS_BridgeIsOn(SiS_Pr,BaseAddr,HwDeviceExtension))) {
    flag = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00);
    if(HwDeviceExtension->jChipType < SIS_315H) {
      /* 300 series (630/301B 2.04.5a) */
      flag &= 0xa0;
      if((flag == 0x80) || (flag == 0x20)) return 0;
      else	                           return 1;
    } else {
      /* 310/325 series (650/30xLV 1.10.6s) */
      flag &= 0x50;
      if((flag == 0x40) || (flag == 0x10)) return 0;
      else                                 return 1;
    }
  }
  return 1;
}

BOOLEAN
SiS_BridgeInSlave(SiS_Private *SiS_Pr)
{
  USHORT flag1;

  flag1 = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31);
  if(flag1 & (SetInSlaveMode >> 8)) return 1;
  else return 0;
}

void
SiS_SetHiVision(SiS_Private *SiS_Pr, USHORT BaseAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
#ifdef SIS315H
  USHORT temp;
#endif

  /* Note: This variable is only used on 30xLV systems.
     CR38 has a different meaning on LVDS/CH7019 systems.
   */

  SiS_Pr->SiS_HiVision = 0;
  if(HwDeviceExtension->jChipType >= SIS_315H) {
#ifdef SIS315H
     if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {
           temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
	   temp &= 0x38;
	   SiS_Pr->SiS_HiVision = (temp >> 3);
	}
     }
#endif /* SIS315H */
  }
}

BOOLEAN
SiS_GetLCDResInfo(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,
                  USHORT ModeIdIndex, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,modeflag,resinfo=0;
  const unsigned char SiS300SeriesLCDRes[] =
         { 0, 1, 2, 3, 7, 4, 5, 8,
	   0, 0, 0, 0, 0, 0, 0, 0 };

  SiS_Pr->SiS_LCDResInfo = 0;
  SiS_Pr->SiS_LCDTypeInfo = 0;
  SiS_Pr->SiS_LCDInfo = 0;

  if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
  } else {
     if(ModeNo <= 0x13) {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     }
  }

  if(!(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)))   return 0;

  if(!(SiS_Pr->SiS_VBInfo & (SetSimuScanMode | SwitchToCRT2))) return 0;

  temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x36);

  /* FSTN: Fake CR36 (TypeInfo 2, ResInfo SiS_Panel320x480) */
  if(SiS_Pr->SiS_IF_DEF_FSTN) {
   	temp = 0x20 | SiS_Pr->SiS_Panel320x480;
   	SiS_SetReg1(SiS_Pr->SiS_P3d4,0x36,temp);
  }

  if(HwDeviceExtension->jChipType < SIS_315H) {
  	SiS_Pr->SiS_LCDTypeInfo = temp >> 4;
  } else {
        SiS_Pr->SiS_LCDTypeInfo = (temp & 0x0F) - 1;
  }
  temp &= 0x0f;
  if(HwDeviceExtension->jChipType < SIS_315H) {
      /* TW: Translate 300 series LCDRes to 310/325 series for unified usage */
      temp = SiS300SeriesLCDRes[temp];
  }
  SiS_Pr->SiS_LCDResInfo = temp;

  if(SiS_Pr->SiS_IF_DEF_FSTN){
       	SiS_Pr->SiS_LCDResInfo = SiS_Pr->SiS_Panel320x480;
  }

  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
    	if(SiS_Pr->SiS_LCDResInfo < SiS_Pr->SiS_PanelMin301)
		SiS_Pr->SiS_LCDResInfo = SiS_Pr->SiS_PanelMin301;
  } else {
    	if(SiS_Pr->SiS_LCDResInfo < SiS_Pr->SiS_PanelMinLVDS)
		SiS_Pr->SiS_LCDResInfo = SiS_Pr->SiS_PanelMinLVDS;
  }

  if(SiS_Pr->SiS_LCDResInfo > SiS_Pr->SiS_PanelMax)
  	SiS_Pr->SiS_LCDResInfo = SiS_Pr->SiS_Panel1024x768;

  temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x37);
  if(SiS_Pr->SiS_IF_DEF_FSTN){
        /* TW: Fake LVDS bridge for FSTN */
      	temp = 0x04;
      	SiS_SetReg1(SiS_Pr->SiS_P3d4,0x37,temp);
  }
  SiS_Pr->SiS_LCDInfo = temp;
  
  if(!(SiS_Pr->UsePanelScaler))        SiS_Pr->SiS_LCDInfo &= ~DontExpandLCD;
  else if(SiS_Pr->UsePanelScaler == 1) SiS_Pr->SiS_LCDInfo |= DontExpandLCD;

  /* TW: Inserted entire 315-block from 650/LVDS/30xLV BIOSes */
  if(HwDeviceExtension->jChipType >= SIS_315H) {
#ifdef SIS315H
     if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
         if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
		 if(ModeNo == 0x3a || ModeNo == 0x4d || ModeNo == 0x65) {
		     /* Bridge does not scale to 1280x1024 */
		     SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
		 }
	     }
	     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
	         if(ModeNo == 0x7c || ModeNo == 0x7d || ModeNo == 0x7e) {
		     /* TW: Bridge does not scale to 1280x960 */
		     SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
		 }
		 if(ModeNo == 0x2f || ModeNo == 0x5d || ModeNo == 0x5e) {
		     /* TW: Bridge does not scale to 640x400 */
		     SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
		 }
	     }
	     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
	         if(ModeNo == 0x2f || ModeNo == 0x5d || ModeNo == 0x5e) {
		     /* TW: Most panels can't scale to 640x400 */
		     SiS_Pr->SiS_LCDInfo &= ~DontExpandLCD;
		 }
	     }
	 }
     }
     if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x39) & 0x01) {
         SiS_Pr->SiS_LCDInfo &= 0xFFEF;    
	 SiS_Pr->SiS_LCDInfo |= LCDPass11;
     }
#endif
  } else {
#ifdef SIS300
     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
        if((ROMAddr) && SiS_Pr->SiS_UseROM) {
           if(!(ROMAddr[0x235] & 0x02)) {
	      SiS_Pr->SiS_LCDInfo &= 0xEF;
	   }
        }
     } else {
        if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
	   if((SiS_Pr->SiS_SetFlag & SetDOSMode) && ((ModeNo == 0x03) || (ModeNo == 0x10))) {
               SiS_Pr->SiS_LCDInfo &= 0xEF;
	   }
	}
     }
#endif
  }
  
  /* TW: With Trumpion, always Expanding */
  if(SiS_Pr->SiS_IF_DEF_TRUMPION != 0){
       SiS_Pr->SiS_LCDInfo &= (~DontExpandLCD);
  }

  if(!((HwDeviceExtension->jChipType < SIS_315H) && (SiS_Pr->SiS_SetFlag & SetDOSMode))) {

     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
	   if(ModeNo > 0x13) {
	      if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
                 if((resinfo == 7) || (resinfo == 3)) {
                    SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
		 }
              }
           }
        }
	if(ModeNo == 0x12) {
	   if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
	      SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
	   }
	}
     }

     if(modeflag & HalfDCLK) {
        if(SiS_Pr->SiS_IF_DEF_TRUMPION == 0) {
           if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
	      if(!(((SiS_Pr->SiS_IF_DEF_LVDS == 1) || (HwDeviceExtension->jChipType < SIS_315H)) &&
	                                      (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480))) {
                 if(ModeNo > 0x13) {
                    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
                       if(resinfo == 4) SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;     /* 512x384  */
                    } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) {
                       if(resinfo == 3) SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;     /* 400x300  */
                    }
                 }
	      } else SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
           } else SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
        } else SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
     }

  }

  if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
    	if(SiS_Pr->SiS_VBInfo & SetNotSimuMode) {
      		SiS_Pr->SiS_SetFlag |= LCDVESATiming;
    	}
  } else {
    	SiS_Pr->SiS_SetFlag |= LCDVESATiming;
  }

#ifdef SIS315H
  /* TW: 650/30xLV 1.10.6s */
  if(HwDeviceExtension->jChipType >= SIS_315H) {
    if(SiS_Pr->SiS_VBType & (VB_SIS302B | VB_SIS302LV)) {
      /* Enable 302B/302LV dual link mode */
      /* (302B is a theory - not in any BIOS */
      temp = 0x00;
      if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) temp = 0x04;
      if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) temp = 0x04;
      if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) temp = 0x04;
      SiS_SetReg1(SiS_Pr->SiS_P3d4,0x39,temp);
    } else if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
      SiS_SetReg1(SiS_Pr->SiS_P3d4,0x39,0x00);
    }
  }
#endif

#ifdef LINUX_KERNEL
#ifdef TWDEBUG
  printk(KERN_DEBUG "sisfb: (LCDInfo=0x%04x LCDResInfo=0x%02x LCDTypeInfo=0x%02x)\n",
	SiS_Pr->SiS_LCDInfo, SiS_Pr->SiS_LCDResInfo, SiS_Pr->SiS_LCDTypeInfo);
#endif
#endif
#ifdef LINUX_XF86
  xf86DrvMsgVerb(0, X_PROBED, 3, 
  	"(init301: LCDInfo=0x%04x LCDResInfo=0x%02x LCDTypeInfo=0x%02x SetFlag=0x%04x)\n",
	SiS_Pr->SiS_LCDInfo, SiS_Pr->SiS_LCDResInfo, SiS_Pr->SiS_LCDTypeInfo, SiS_Pr->SiS_SetFlag);
#endif

  return 1;
}

void
SiS_PresetScratchregister(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  return;
  /*SiS_SetReg1(SiS_Pr->SiS_P3d4,0x30,0x21);  */
  /*SiS_SetReg1(SiS_Pr->SiS_P3d4,0x31,0x41);  */
  /*SiS_SetReg1(SiS_Pr->SiS_P3d4,0x32,0x28);  */
  /*SiS_SetReg1(SiS_Pr->SiS_P3d4,0x33,0x22);  */
  /*SiS_SetReg1(SiS_Pr->SiS_P3d4,0x35,0x43);  */
  /*SiS_SetReg1(SiS_Pr->SiS_P3d4,0x36,0x01);  */
  /*SiS_SetReg1(SiS_Pr->SiS_P3d4,0x37,0x00);  */
}

void
SiS_LongWait(SiS_Private *SiS_Pr)
{
  USHORT i;

  i = SiS_GetReg1(SiS_Pr->SiS_P3c4,0x1F);

  if(!(i & 0xC0)) {
    for(i=0; i<0xFFFF; i++) {
       if(!(SiS_GetReg2(SiS_Pr->SiS_P3da) & 0x08))
         break;
    }
    for(i=0; i<0xFFFF; i++) {
       if((SiS_GetReg2(SiS_Pr->SiS_P3da) & 0x08))
         break;
    }
  }
}

void
SiS_VBLongWait(SiS_Private *SiS_Pr)
{
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {
    SiS_VBWait(SiS_Pr);
  } else {
    SiS_LongWait(SiS_Pr);
  }
  return;
}

void
SiS_VBWait(SiS_Private *SiS_Pr)
{
  USHORT tempal,temp,i,j;

  temp = 0;
  for(i=0; i<3; i++) {
    for(j=0; j<100; j++) {
       tempal = SiS_GetReg2(SiS_Pr->SiS_P3da);
       if(temp & 0x01) {
          if((tempal & 0x08))  continue;
          if(!(tempal & 0x08)) break;
       } else {
          if(!(tempal & 0x08)) continue;
          if((tempal & 0x08))  break;
       }
    }
    temp ^= 0x01;
  }
}

void
SiS_WaitVBRetrace(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  if(HwDeviceExtension->jChipType < SIS_315H) {
#ifdef SIS300
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
        if(!(SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00) & 0x20)) return;
     }
     if(!(SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00) & 0x80)) {
        SiS_WaitRetrace1(SiS_Pr,HwDeviceExtension);
     } else {
        SiS_WaitRetrace2(SiS_Pr,HwDeviceExtension);
     }
#endif
  } else {
#ifdef SIS315H
     if(!(SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00) & 0x40)) {
        SiS_WaitRetrace1(SiS_Pr,HwDeviceExtension);
     } else {
        SiS_WaitRetrace2(SiS_Pr,HwDeviceExtension);
     }
#endif
  }
}

void
SiS_WaitRetrace1(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT watchdog;
#ifdef SIS300
  USHORT i;
#endif

  if(HwDeviceExtension->jChipType >= SIS_315H) {
#ifdef SIS315H
     if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x1f) & 0xc0) return;
     watchdog = 65535;
     while( (SiS_GetReg2(SiS_Pr->SiS_P3da) & 0x08) && --watchdog);
     watchdog = 65535;
     while( (!(SiS_GetReg2(SiS_Pr->SiS_P3da) & 0x08)) && --watchdog);
#endif
  } else {
#ifdef SIS300
#if 0  /* TW: Not done in A901 BIOS */
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
        if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x1f) & 0xc0) return;
     }
#endif
     for(i=0; i<10; i++) {
        watchdog = 65535;
        while( (SiS_GetReg2(SiS_Pr->SiS_P3da) & 0x08) && --watchdog);
	if(watchdog) break;
     }
     for(i=0; i<10; i++) {
        watchdog = 65535;
        while( (!(SiS_GetReg2(SiS_Pr->SiS_P3da) & 0x08)) && --watchdog);
	if(watchdog) break;
     }
#endif
  }
}

void
SiS_WaitRetraceDDC(SiS_Private *SiS_Pr)
{
  USHORT watchdog;

  if(SiS_GetReg1(SiS_Pr->SiS_P3c4,0x1f) & 0xc0) return;
  watchdog = 65535;
  while( (SiS_GetReg2(SiS_Pr->SiS_P3da) & 0x08) && --watchdog);
  watchdog = 65535;
  while( (!(SiS_GetReg2(SiS_Pr->SiS_P3da) & 0x08)) && --watchdog);
}

void
SiS_WaitRetrace2(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT watchdog;
#ifdef SIS300
  USHORT i;
#endif

  if(HwDeviceExtension->jChipType >= SIS_315H) {
#ifdef SIS315H
     watchdog = 65535;
     while( (SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x30) & 0x02) && --watchdog);
     watchdog = 65535;
     while( (!(SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x30) & 0x02)) && --watchdog);
#endif
  } else {
#ifdef SIS300
     for(i=0; i<10; i++) {
        watchdog = 65535;
	while( (SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x25) & 0x02) && --watchdog);
	if(watchdog) break;
     }
     for(i=0; i<10; i++) {
        watchdog = 65535;
	while( (!(SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x25) & 0x02)) && --watchdog);
	if(watchdog) break;
     }
#endif
  }
}

/* =========== Set and Get register routines ========== */

void
SiS_SetRegANDOR(USHORT Port,USHORT Index,USHORT DataAND,USHORT DataOR)
{
  USHORT temp;

  temp = SiS_GetReg1(Port,Index);    
  temp = (temp & (DataAND)) | DataOR;
  SiS_SetReg1(Port,Index,temp);
}

void
SiS_SetRegAND(USHORT Port,USHORT Index,USHORT DataAND)
{
  USHORT temp;

  temp = SiS_GetReg1(Port,Index);    
  temp &= DataAND;
  SiS_SetReg1(Port,Index,temp);
}

void SiS_SetRegOR(USHORT Port,USHORT Index,USHORT DataOR)
{
  USHORT temp;

  temp = SiS_GetReg1(Port,Index);    
  temp |= DataOR;
  SiS_SetReg1(Port,Index,temp);
}

/* ========================================================= */

/* TW: Set 301 TV Encoder (and some LCD relevant) registers */
void
SiS_SetGroup2(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr, USHORT ModeNo,
              USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
	      PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT      i, j, tempax, tempbx, tempcx, temp, temp1;
  USHORT      push1, push2;
  const       UCHAR *PhasePoint;
  const       UCHAR *TimingPoint;
#ifdef SIS315H   
  const       SiS_Part2PortTblStruct *CRT2Part2Ptr = NULL;
  USHORT      resindex, CRT2Index;
#endif  
  USHORT      modeflag, resinfo, crt2crtc;
  ULONG       longtemp, tempeax, tempebx, temp2, tempecx;
  const UCHAR atable[] = {
                 0xc3,0x9e,0xc3,0x9e,0x02,0x02,0x02,
	         0xab,0x87,0xab,0x9e,0xe7,0x02,0x02
  };

#ifdef SIS315H   
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
     /* TW: 650/30xLV 1.10.6s: (Is at end of SetGroup2!) */
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1a,0xfc,0x03);
	   temp = 1;
	   if(ModeNo <= 0x13) temp = 3;
	   SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x0b,temp);
	}
     }
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
       if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
         if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
           if((ModeNo == 0x4a) || (ModeNo == 0x38)) {
               SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1c,0xa7);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1d,0x07);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1e,0xf2);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1f,0x6e);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x20,0x17);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x21,0x8b);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x22,0x73);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x23,0x53);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x24,0x13);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x25,0x40);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x26,0x34);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x27,0xf4);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x28,0x63);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x29,0xbb);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2a,0xcc);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2b,0x7a);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2c,0x58);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2d,0xe4);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2e,0x73);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2f,0xda);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x30,0x13);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x43,0x72);
           }
         }
       }
     }
     return;
  }
#endif  

  if(ModeNo<=0x13) {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;      /* si+St_ResInfo */
    	resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
    	crt2crtc = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;     /* si+Ext_ResInfo */
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
    	crt2crtc = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  tempcx = SiS_Pr->SiS_VBInfo;
  tempax = (tempcx & 0x00FF) << 8;
  tempbx = (tempcx & 0x00FF) | ((tempcx & 0x00FF) << 8);
  tempbx &= 0x0410;
  temp = (tempax & 0x0800) >> 8;
  temp >>= 1;
  temp |= (((tempbx & 0xFF00) >> 8) << 1);
  temp |= ((tempbx & 0x00FF) >> 3);
  temp ^= 0x0C;

  /* TW: From 1.10.7w (no vb check there; don't care - this only disables SVIDEO and CVBS signal) */
  if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
  	temp |= 0x0c;
  }

  PhasePoint  = SiS_Pr->SiS_PALPhase;
  TimingPoint = SiS_Pr->SiS_PALTiming;
  
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {          
  
    temp ^= 0x01;
    if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
      TimingPoint = SiS_Pr->SiS_HiTVSt2Timing;
      if(SiS_Pr->SiS_SetFlag & TVSimuMode) {
        if(modeflag & Charx8Dot) TimingPoint = SiS_Pr->SiS_HiTVSt1Timing;
        else TimingPoint = SiS_Pr->SiS_HiTVTextTiming;
      }
    } else TimingPoint = SiS_Pr->SiS_HiTVExtTiming;
    
    if(SiS_Pr->SiS_HiVision & 0x03) temp &= 0xfe;
    
  } else {
  
    if(SiS_Pr->SiS_VBInfo & SetPALTV){

      TimingPoint = SiS_Pr->SiS_PALTiming;
      PhasePoint  = SiS_Pr->SiS_PALPhase;

      if( (SiS_Pr->SiS_VBType & VB_SIS301B302B) &&
          ( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
	    (SiS_Pr->SiS_SetFlag & TVSimuMode) ) ) {
         PhasePoint = SiS_Pr->SiS_PALPhase2;
      }

    } else {

        temp |= 0x10;
	TimingPoint = SiS_Pr->SiS_NTSCTiming;
	PhasePoint  = SiS_Pr->SiS_NTSCPhase;

        if( (SiS_Pr->SiS_VBType & VB_SIS301B302B) &&
	    ( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
	      (SiS_Pr->SiS_SetFlag & TVSimuMode) ) ) {
        	PhasePoint = SiS_Pr->SiS_NTSCPhase2;
        }

    }
    
  }
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x00,temp);

  temp = 0;
  if((HwDeviceExtension->jChipType == SIS_630)||
     (HwDeviceExtension->jChipType == SIS_730)) {
     temp = 0x35;
  }
  if(HwDeviceExtension->jChipType >= SIS_315H) {
     temp = 0x38;
  }
  if(temp) {
    if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
      if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) & 0x01) {
          temp1 = SiS_GetReg1(SiS_Pr->SiS_P3d4,temp);
          if(temp1 & EnablePALM) {	/* 0x40 */
              	PhasePoint = SiS_Pr->SiS_PALMPhase;
		if( (SiS_Pr->SiS_VBType & VB_SIS301B302B) &&
		    ( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
		      (SiS_Pr->SiS_SetFlag & TVSimuMode) ) ) {
	           PhasePoint = SiS_Pr->SiS_PALMPhase2;
		}
	  }
          if(temp1 & EnablePALN) {	/* 0x80 */
               	PhasePoint = SiS_Pr->SiS_PALNPhase;
		if( (SiS_Pr->SiS_VBType & VB_SIS301B302B) &&
		    ( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
		      (SiS_Pr->SiS_SetFlag & TVSimuMode) ) ) {
	           PhasePoint = SiS_Pr->SiS_PALNPhase2;
		}
	  }
      }
    }
  }

#ifdef SIS315H
  /* TW: 650/301LV BIOS */
  if(HwDeviceExtension->jChipType >= SIS_315H) {
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {  
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
           if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
              if((ModeNo == 0x4a) || (ModeNo == 0x38)) {
	         PhasePoint = SiS_Pr->SiS_SpecialPhase;
	      }
           }
        }
     }
  }
#endif

  for(i=0x31, j=0; i<=0x34; i++, j++) {
     SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,PhasePoint[j]);
  }

  for(i=0x01, j=0; i<=0x2D; i++, j++) {
     SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,TimingPoint[j]);
  }
  for(i=0x39; i<=0x45; i++, j++) {
     SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,TimingPoint[j]);
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
    if(HwDeviceExtension->jChipType >= SIS_315H) {
      if(!(SiS_Pr->SiS_ModeType & 0x07))
        SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x3A,0x1F);
    } else {
      SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x3A,0x1F);
    }
  }

  SiS_SetRegOR(SiS_Pr->SiS_Part2Port,0x0A,SiS_Pr->SiS_NewFlickerMode);

  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x35,SiS_Pr->SiS_RY1COE);
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x36,SiS_Pr->SiS_RY2COE);
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x37,SiS_Pr->SiS_RY3COE);
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x38,SiS_Pr->SiS_RY4COE);

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {
      if(SiS_Pr->SiS_HiVision == 3) tempax = 950;
      else tempax = 440;
  } else {
    if(SiS_Pr->SiS_VBInfo & SetPALTV) tempax = 520;
    else tempax = 440;
  }

  if( ( ( (!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) || (SiS_Pr->SiS_HiVision == 3) ) && (SiS_Pr->SiS_VDE <= tempax) ) ||
      ( (SiS_Pr->SiS_VBInfo & SetCRT2ToTV) && (SiS_Pr->SiS_HiVision != 3) &&
        ( (SiS_Pr->SiS_VGAHDE == 1024) || ((SiS_Pr->SiS_VGAHDE != 1024) && (SiS_Pr->SiS_VDE <= tempax)) ) ) ) {

     tempax -= SiS_Pr->SiS_VDE;
     tempax >>= 2;
     tempax = (tempax & 0x00FF) | ((tempax & 0x00FF) << 8);

     temp = (tempax & 0xFF00) >> 8;
     temp += (USHORT)TimingPoint[0];
     SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x01,temp);

     temp = (tempax & 0xFF00) >> 8;
     temp += (USHORT)TimingPoint[1];
     SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x02,temp);

     if( (SiS_Pr->SiS_VBInfo & (SetCRT2ToTV - SetCRT2ToHiVisionTV)) &&
        (SiS_Pr->SiS_HiVision != 3) &&
        (SiS_Pr->SiS_VGAHDE >= 1024) ) {
        if(SiS_Pr->SiS_VBInfo & SetPALTV) {
           SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x01,0x19);
           SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x02,0x52);
        } else {
           if(HwDeviceExtension->jChipType >= SIS_315H) {
             SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x01,0x17);
             SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x02,0x1d);
	   } else {
             SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x01,0x0b);
             SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x02,0x11);
	   }
        }
     }

  }

  tempcx = SiS_Pr->SiS_HT;

  /* TW: 650/30xLV 1.10.6s */
  if(HwDeviceExtension->jChipType >= SIS_315H) {
      if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x39) & 0x04) {
      	   tempcx >>= 1;
      }
  }

  tempcx--;
  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
        tempcx--;
  }
  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1B,temp);
  temp = (tempcx & 0xFF00) >> 8;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1D,0xF0,temp);

  tempcx++;
  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
        tempcx++;
  }
  tempcx >>= 1;

  push1 = tempcx;

  tempcx += 7;
  if((SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) &&
     (SiS_Pr->SiS_HiVision == 3)) {
       tempcx -= 4;
  }
  temp = (tempcx & 0x00FF) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x22,0x0F,temp);

  tempbx = TimingPoint[j] | ((TimingPoint[j+1]) << 8);
  tempbx += tempcx;

  push2 = tempbx;

  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x24,temp);
  temp = ((tempbx & 0xFF00) >> 8) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0x0F,temp);

  tempbx = push2;

  tempbx += 8;
  if((SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) &&
     (SiS_Pr->SiS_HiVision == 3)) {
    tempbx -= 4;
    tempcx = tempbx;
  }
  temp = (tempbx & 0x00FF) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x29,0x0F,temp);

  j += 2;
  tempcx += ((TimingPoint[j] | ((TimingPoint[j+1]) << 8)));
  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x27,temp);
  temp = ((tempcx & 0xFF00) >> 8) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x28,0x0F,temp);

  tempcx += 8;
  if((SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) &&
     (SiS_Pr->SiS_HiVision == 3)) {
     tempcx -= 4; 
  }
  temp = (tempcx & 0x00FF) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x2A,0x0F,temp);

  tempcx = push1;

  j += 2;
  tempcx -= (TimingPoint[j] | ((TimingPoint[j+1]) << 8));
  temp = (tempcx & 0x00FF) << 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x2D,0x0F,temp);

  tempcx -= 11;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {
    tempax = SiS_GetVGAHT2(SiS_Pr) - 1;
    tempcx = tempax;
  }
  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2E,temp);

  tempbx = SiS_Pr->SiS_VDE;
  if(SiS_Pr->SiS_VGAVDE == 360) tempbx = 746;
  if(SiS_Pr->SiS_VGAVDE == 375) tempbx = 746;
  if(SiS_Pr->SiS_VGAVDE == 405) tempbx = 853;
  if(HwDeviceExtension->jChipType < SIS_315H) {
  	if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) tempbx >>= 1;
  } else {
	if((SiS_Pr->SiS_VBInfo & SetCRT2ToTV) && (!(SiS_Pr->SiS_HiVision & 0x03))) {
	   tempbx >>= 1;
	   if(SiS_Pr->SiS_SetFlag & TVSimuMode) {
	      if(ModeNo <= 0x13) {
	         if(crt2crtc == 1) {
	            tempbx++;
                 }
	      }
	   } else {
              if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	         if(crt2crtc == 4)   /* TW: BIOS calls GetRatePtrCRT2 here - does not make sense */
                    if(SiS_Pr->SiS_ModeType <= 3) tempbx++;
	      }
	   }
        }
  }
  tempbx -= 2;
  temp = tempbx & 0x00FF;
  if((SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) &&
     (SiS_Pr->SiS_HiVision == 3)) {
    if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
      if(ModeNo == 0x2f) temp++;
    }
  }
  /* TW: From 1.10.7w - doesn't make sense */
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
        if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
	   if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {   /* SetFlag?? */
	       if(ModeNo == 0x03) temp++;
	   }
	}
     }
  }
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2F,temp);

  tempax = (tempcx & 0xFF00) | (tempax & 0x00FF);
  tempbx = ((tempbx & 0xFF00) << 6) | (tempbx & 0x00FF);
  tempax |= (tempbx & 0xFF00);
  if(HwDeviceExtension->jChipType < SIS_315H) {
     if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV)) {
        if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToSCART)) {		/* TW: New from 630/301B (II) BIOS */
           tempax |= 0x1000;
           if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToSVIDEO))  tempax |= 0x2000;
        }
     }
  } else {
     /* TODO Check this with other BIOSes */
     if((!(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV)) && 
        (SiS_Pr->SiS_HiVision == 3)) {
	tempax |= 0x1000;
        if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToSVIDEO))  tempax |= 0x2000;
     }
  }
  temp = (tempax & 0xFF00) >> 8;
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x30,temp);

  /* TW: 650/30xLV 1.10.6s */
  if(HwDeviceExtension->jChipType > SIS_315H) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if( (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) ||
            (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) ) {
            SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x10,0x60);
        }
     }
  }
  
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {
     if(SiS_Pr->SiS_HiVision != 3) {
	for(i=0, j=0; i<=0x2d; i++, j++) {
	    SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS_HiVisionTable[SiS_Pr->SiS_HiVision][j]);
	}
	for(i=0x39; i<=0x45; i++, j++) {
	    SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS_HiVisionTable[SiS_Pr->SiS_HiVision][j]);
	}
     }
  }

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {     
    tempbx = SiS_Pr->SiS_VDE;
    if((SiS_Pr->SiS_VBInfo & SetCRT2ToTV) && (!(SiS_Pr->SiS_HiVision & 0x03))) {
         tempbx >>= 1;
    }
    tempbx -= 3;
    tempbx &= 0x03ff;
    temp = ((tempbx & 0xFF00) >> 8) << 5;
    temp |= 0x18;
    SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x46,temp);
    temp = tempbx & 0x00FF;
    SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x47,temp);	/* tv gatingno */
    if(HwDeviceExtension->jChipType >= SIS_315H) {	/* TW: 650/30xLV 1.10.6s */
       if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
          tempax = 0;
          if(SiS_Pr->SiS_HiVision & 0x03) {
	     tempax = 0x3000;
	     if(SiS_Pr->SiS_HiVision & 0x01) tempax = 0x5000;
	  }
	  temp = (tempax & 0xFF00) >> 8;
          SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x4d,temp);
       }
    }
  }

  tempbx &= 0x00FF;
  if(!(modeflag & HalfDCLK)) {
    tempcx = SiS_Pr->SiS_VGAHDE;
    if(tempcx >= SiS_Pr->SiS_HDE) {
      tempbx |= 0x2000;
      tempax &= 0x00FF;
    }
  }

  tempcx = 0x0101;
/*if(SiS_Pr->SiS_VBInfo & (SetPALTV | SetCRT2ToTV)) {  */ /*301b- TW: BIOS BUG? */
  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToTV - SetCRT2ToHiVisionTV)) {
    if(!(SiS_Pr->SiS_HiVision & 0x03)) {
      if(SiS_Pr->SiS_VGAHDE >= 1024) {
        if((!(modeflag & HalfDCLK)) || (HwDeviceExtension->jChipType < SIS_315H)) {   /* TW: This check not in 630/301B */
          tempcx = 0x1920;
          if(SiS_Pr->SiS_VGAHDE >= 1280) {
            tempcx = 0x1420;
            tempbx &= 0xDFFF;
          }
        }
      }
    }
  }

  if(!(tempbx & 0x2000)) {
    if(modeflag & HalfDCLK) {
         tempcx = (tempcx & 0xFF00) | (((tempcx & 0x00FF) << 1) & 0xff);
    }
    push1 = tempbx;
    tempeax = SiS_Pr->SiS_VGAHDE;
    tempebx = (tempcx & 0xFF00) >> 8;
    longtemp = tempeax * tempebx;
    tempecx = tempcx & 0x00FF;
    longtemp /= tempecx;
    longtemp <<= 0x0d;
    if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
     	longtemp <<= 3;
    }
    tempecx = SiS_Pr->SiS_HDE;
    temp2 = longtemp % tempecx;
    tempeax = longtemp / tempecx;
    if(temp2 != 0) tempeax++;
    tempax = (USHORT)tempeax;
    tempbx = push1;
    tempcx = (tempcx & 0xff00) | (((tempax & 0xFF00) >> 8) >> 5);
    tempbx |= (tempax & 0x1F00);
    tempax = ((tempax & 0x00FF) << 8) | (tempax & 0x00FF);
  }

  temp = (tempax & 0xFF00) >> 8;
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x44,temp);
  temp = (tempbx & 0xFF00) >> 8;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x45,0xC0,temp);

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
       temp = tempcx & 0x00FF;
       if(tempbx & 0x2000) temp = 0;
       temp |= 0x18;
       SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x46,0xE0,temp);
       if(SiS_Pr->SiS_VBInfo & SetPALTV) {
             tempbx = 0x0382;  
             tempcx = 0x007e;  
       } else {
             tempbx = 0x0369;  
             tempcx = 0x0061;  
       }
       temp = (tempbx & 0x00FF) ;
       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x4B,temp);
       temp = (tempcx & 0x00FF) ;
       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x4C,temp);
       tempbx &= 0x03FF;
       temp = (tempcx & 0xFF00) >> 8;
       temp = (temp & 0x0003) << 2;
       temp |= (tempbx >> 8);
       if(HwDeviceExtension->jChipType < SIS_315H) {
          SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x4D,temp);
       } else {
          SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x4D,0xF0,temp);
       }

       temp = SiS_GetReg1(SiS_Pr->SiS_Part2Port,0x43);
       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x43,(USHORT)(temp - 3));
  }

  temp = 0;
  if((HwDeviceExtension->jChipType == SIS_630) ||
     (HwDeviceExtension->jChipType == SIS_730)) {
     temp = 0x35;
  } else if(HwDeviceExtension->jChipType >= SIS_315H) {
     temp = 0x38;
  }
  if(temp) {
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
          if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) & 0x01) {
               if(SiS_GetReg1(SiS_Pr->SiS_P3d4,temp) & EnablePALM) {  /* 0x40 */
                     SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xEF);
                     temp = SiS_GetReg1(SiS_Pr->SiS_Part2Port,0x01);
                     SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x01,temp - 1);
               }
          }
      }
  }

  if( (SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) &&
      (!(SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) ) {
    if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x0B,0x00);
    }
  }

  if(HwDeviceExtension->jChipType < SIS_315H) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     	SiS_Set300Part2Regs(SiS_Pr, HwDeviceExtension, ModeIdIndex,
			    RefreshRateTableIndex, BaseAddr, ModeNo);
	return;
     }
  } else {
     /* TW: !!! The following is a duplicate, done for LCDA as well (see above) */
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
       if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {    
         if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
           if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
             if((ModeNo == 0x4a) || (ModeNo == 0x38)) {
               SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1c,0xa7);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1d,0x07);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1e,0xf2);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1f,0x6e);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x20,0x17);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x21,0x8b);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x22,0x73);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x23,0x53);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x24,0x13);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x25,0x40);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x26,0x34);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x27,0xf4);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x28,0x63);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x29,0xbb);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2a,0xcc);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2b,0x7a);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2c,0x58);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2d,0xe4);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2e,0x73);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2f,0xda);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x30,0x13);
	       SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x43,0x72);
	     }
           }
         }
       }
       return;
     }
  }

  /* TW: From here: Part2 LCD setup */

  tempbx = SiS_Pr->SiS_HDE;
  if(HwDeviceExtension->jChipType >= SIS_315H) {
      /* TW: 650/30xLV 1.10.6s */
      if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x39) & 0x04) tempbx >>= 1;
  }
  tempbx--;			         	/* RHACTE=HDE-1 */
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2C,temp);
  temp = (tempbx & 0xFF00) >> 8;
  temp <<= 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x2B,0x0F,temp);

  temp = 0x01;
  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
    if(SiS_Pr->SiS_ModeType == ModeEGA) {
      if(SiS_Pr->SiS_VGAHDE >= 1024) {
        temp = 0x02;
	if(HwDeviceExtension->jChipType >= SIS_315H) {
           if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
             temp = 0x01;
	   }
	}
      }
    }
  }
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x0B,temp);

  tempbx = SiS_Pr->SiS_VDE;         		/* RTVACTEO=(VDE-1)&0xFF */
  push1 = tempbx;

  tempbx--;
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x03,temp);
  temp = ((tempbx & 0xFF00) >> 8) & 0x07;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x0C,0xF8,temp);

  tempcx = SiS_Pr->SiS_VT;
  push2 = tempcx;

  tempcx--;
  temp = tempcx & 0x00FF;  			 /* RVTVT=VT-1 */
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x19,temp);

  temp = (tempcx & 0xFF00) >> 8;
  temp <<= 5;
  
  /* Enable dithering; newer versions only do this for 32bpp mode */
  if((HwDeviceExtension->jChipType == SIS_300) && (SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) {
    if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) temp |= 0x10;
  } else if(HwDeviceExtension->jChipType < SIS_315H) {
    if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) temp |= 0x10;
    else {
      if(SiS_Pr->SiS_LCDInfo & LCDSync)       /* TW: 630/301 BIOS checks this */
         temp |= 0x10;
    }
  } else {
      if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
         /* TW: 650/30xLV 1.10.6s */
         if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
            if(SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x00) & 0x01) {  /* 32bpp mode? */
      	       temp |= 0x10;
	    }
         }
      } else {
         temp |= 0x10;
      }
  }

  /* 630/301 does not do all this */
  if((SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
     if((HwDeviceExtension->jChipType >= SIS_315H) && (SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) {
        /* TW: 650/30xLV 1.10.6s */
        temp |= (SiS_GetReg1(SiS_Pr->SiS_P3d4,0x37) >> 6);
	temp |= 0x08;   					/* From 1.10.7w */
	if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) temp |= 0x04; 	/* From 1.10.7w */
     } else {
        tempbx = (tempbx & 0xFF00) | (SiS_Pr->SiS_LCDInfo & 0x0FF);
        if(tempbx & LCDSync) {
           tempbx &= 0xFFE0;
           tempbx = (tempbx & 0xFF00) | ((tempbx & 0x00FF) >> 6);
           temp |= (tempbx & 0x00FF);
        }
     }
  }
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1A,temp);

  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x09,0xF0);
  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x0A,0xF0);

  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x17,0xFB);
  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x18,0xDF);

  /* 1280x960, 1280x1024 and 1600x1200 data invalid/missing in tables, use old calculation */
  if((HwDeviceExtension->jChipType >= SIS_315H)             && 
     (SiS_Pr->SiS_VBType & VB_SIS301LV302LV)                &&  
     (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x1024) &&
     (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1600x1200) &&
     (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x960)) {
     
#ifdef SIS315H 							/* ------------- 310/325/330 series ------------ */

      /* TW: Inserted this entire section from 650/301LV(x) BIOS */
      
      /* Using this on the 301B with an auto-expanding 1024 panel (CR37=1) results
       * in a black bar in modes < 1024; if the panel is non-expanding, the bridge
       * scales all modes to 1024. All modes in both variants (exp/non-exp) work.
       */

      SiS_GetCRT2Part2Ptr(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                         &CRT2Index,&resindex);

      switch(CRT2Index) {
        case Panel_1024x768      : CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_1;  break;  /* "Normal" */
        case Panel_1280x1024     : CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1280x1024_1; break;
	case Panel_1400x1050     : CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1400x1050_1; break;
	case Panel_1600x1200     : CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1600x1200_1; break;
        case Panel_1024x768  + 16: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_2;  break;  /* Non-Expanding */
        case Panel_1280x1024 + 16: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1280x1024_2; break;
	case Panel_1400x1050 + 16: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1400x1050_2; break;
	case Panel_1600x1200 + 16: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1600x1200_2; break;
        case Panel_1024x768  + 32: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_3;  break;  /* VESA Timing */
        case Panel_1280x1024 + 32: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1280x1024_3; break;
	case Panel_1400x1050 + 32: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1400x1050_3; break;
	case Panel_1600x1200 + 32: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1600x1200_3; break;
	default:                   CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_3;  break;
      }

      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x01,0x80,(CRT2Part2Ptr+resindex)->CR[0]);
      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x02,0x80,(CRT2Part2Ptr+resindex)->CR[1]);
      for(i = 2, j = 0x04; j <= 0x06; i++, j++ ) {
        SiS_SetReg1(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      for(j = 0x1c; j <= 0x1d; i++, j++ ) {
        SiS_SetReg1(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      for(j = 0x1f; j <= 0x21; i++, j++ ) {
        SiS_SetReg1(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x23,(CRT2Part2Ptr+resindex)->CR[10]);
      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0x0f,(CRT2Part2Ptr+resindex)->CR[11]);

      if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
        if(SiS_Pr->SiS_VGAVDE == 525) {
	  temp = 0xc3;
	  if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	     temp++;
	     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) temp += 2;
	  }
	  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2f,temp);
	  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x30,0xb3);
	}
	if(SiS_Pr->SiS_VGAVDE == 420) {
	  temp = 0x4d;
	  if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	     temp++;
	     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) temp++;
	  }
	  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2f,temp);
	}
     }

     /* TW: 650/30xLV 1.10.6s: */
     /* !!! This is a duplicate, done for LCDA as well - see above */
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1a,0xfc,0x03);   /* Not done in 1.10.7w */
	   temp = 1;
	   if(ModeNo <= 0x13) temp = 3;
	   SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x0b,temp);
	}
     }
#endif

  } else {   /* ------ 300 series and other bridges, other LCD resolutions ------ */

      /* Using this on the 301B with an auto-expanding 1024 panel (CR37=1) makes
       * the panel scale at modes < 1024 (no black bars); if the panel is non-expanding, 
       * the bridge scales all modes to 1024.
       * !!! Malfunction at 640x480 and 640x400 when panel is auto-expanding - black screen !!!
       */
  
    tempcx++;
    
    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)       tempbx =  768;
    else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) tempbx = 1024;
    else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) tempbx = 1200;
    else if(SiS_Pr->SiS_VDE != 1024) 				  tempbx =  960;
    else            						  tempbx = 1024;
    
#if 0  /* old */
    tempbx = 768;
    if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1024x768) {
      tempbx = 1024;
      if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x1024) {
         tempbx = 1200;
         if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1600x1200) {
            if(tempbx != SiS_Pr->SiS_VDE) {
               tempbx = 960;
            }
         }
      }
    }
#endif
    
    if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
      tempbx = SiS_Pr->SiS_VDE - 1;
      tempcx--;
    }
    
    tempax = 1;
    if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
      if(tempbx != SiS_Pr->SiS_VDE) {
        tempax = tempbx;
/*	if(SiS_Pr->SiS_VGAVDE == 525) tempax += 60;   in 650/301B BIOS */
        if(tempax < SiS_Pr->SiS_VDE) {
          tempax = 0;
          tempcx = 0;
        } else {
          tempax -= SiS_Pr->SiS_VDE;
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
    
#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "lcdvds 0x%x lcdvde 0x%x\n", tempcx, tempbx);
#endif    

    temp = tempcx & 0x00FF;   				/* RVEQ1EQ=lcdvdes */
    SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x05,temp);
    temp = tempbx & 0x00FF;   				/* RVEQ2EQ=lcdvdee */
    SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x06,temp);

    temp = ((tempbx & 0xFF00) >> 8) << 3;
    temp |= ((tempcx & 0xFF00) >> 8);
    SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x02,temp);

    tempbx = push2;
    tempax = push1;
    tempcx = tempbx;
    tempcx -= tempax;
    tempcx >>= 4;
    tempbx += tempax;
    tempbx >>= 1;
    if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)  tempbx -= 10;
    
#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "lcdvrs 0x%x\n", tempbx);
#endif

    temp = tempbx & 0x00FF;   				/* RTVACTEE=lcdvrs */
    SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x04,temp);

    temp = ((tempbx & 0xFF00) >> 8) << 4;
    tempbx += (tempcx + 1);
    temp |= (tempbx & 0x000F);
    SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x01,temp);

    /* TW: Code from 630/301B (I+II) BIOS */

    if( ( ( (HwDeviceExtension->jChipType == SIS_630) ||
            (HwDeviceExtension->jChipType == SIS_730) ) &&
          (HwDeviceExtension->jChipRevision > 2) )  &&
        (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) &&
        (!(SiS_Pr->SiS_SetFlag & LCDVESATiming))  &&
        (!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) ) {
            if(ModeNo == 0x13) {
              SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x04,0xB9);
              SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x05,0xCC);
              SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x06,0xA6);
            } else {
              if((crt2crtc & 0x3F) == 4) {
                SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x01,0x2B);
                SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x02,0x13);
                SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x04,0xE5);
                SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x05,0x08);
                SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x06,0xE2);
              }
            }
    }

    /* TW: Inserted missing code from 630/301B BIOS;
     *     Strangely, this is done in all 650 BIOSes as
     *     well (although LCDTypeInfo is not used there
     *     in the same way as on 300 series)
     */

    if(SiS_Pr->SiS_LCDTypeInfo == 0x0c) {
         crt2crtc &= 0x1f;
         tempcx = 0;
         if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) {
           if (SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
              tempcx += 7;
           }
         }
         tempcx += crt2crtc;
         if (crt2crtc >= 4) {
           SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x06,0xff);
         }

         if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) {
           if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
             if(crt2crtc == 4) {
                SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x01,0x28);
             }
           }
         }
         SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x02,0x18);
         SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x04,atable[tempcx]);
    }

    tempcx = (SiS_Pr->SiS_HT - SiS_Pr->SiS_HDE) >> 2;     /* (HT-HDE)>>2     */
    tempbx = SiS_Pr->SiS_HDE + 7;            		  /* lcdhdee         */
    if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
         tempbx += 2;
    }
    push1 = tempbx;
#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "lcdhde 0x%x\n", tempbx);
#endif    
    temp = tempbx & 0x00FF;    			          /* RHEQPLE=lcdhdee */
    SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x23,temp);
    temp = (tempbx & 0xFF00) >> 8;
    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0xF0,temp);

    temp = 7;
    if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
         temp += 2;
    }
    SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1F,temp);  	  /* RHBLKE=lcdhdes[7:0] */
    SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x20,0x0F);	  /* lcdhdes [11:8] */

    tempbx += tempcx;
    push2 = tempbx;
#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "lcdhrs 0x%x\n", tempbx);
#endif
    temp = tempbx & 0x00FF;            		          /* RHBURSTS=lcdhrs */
    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
       if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) { 
          if(SiS_Pr->SiS_HDE == 1280) temp = 0x47;
       }
    }
    SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x1C,temp);
    temp = ((tempbx & 0xFF00) >> 8) << 4;
    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1D,0x0F,temp);

    tempbx = push2;
    tempcx <<= 1;
    tempbx += tempcx;
#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "lcdhre 0x%x\n", tempbx);
#endif    
    temp = tempbx & 0x00FF;            		          /* RHSYEXP2S=lcdhre */
    SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x21,temp);

    if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
      if(SiS_Pr->SiS_VGAVDE == 525) {
        if(SiS_Pr->SiS_ModeType <= ModeVGA)
    	   temp=0xC6;
        else
       	   temp=0xC3;
        SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2f,temp);
        SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x30,0xB3);
      } else if(SiS_Pr->SiS_VGAVDE == 420) {
        if(SiS_Pr->SiS_ModeType <= ModeVGA)
	   temp=0x4F;
        else
       	   temp=0x4D;   
        SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x2f,temp);
      }
    }
    SiS_Set300Part2Regs(SiS_Pr, HwDeviceExtension, ModeIdIndex,
                        RefreshRateTableIndex, BaseAddr, ModeNo);

  } /* HwDeviceExtension */
}

USHORT
SiS_GetVGAHT2(SiS_Private *SiS_Pr)
{
  ULONG tempax,tempbx;

  tempbx = ((SiS_Pr->SiS_VGAVT - SiS_Pr->SiS_VGAVDE) * SiS_Pr->SiS_RVBHCMAX) & 0xFFFF;
  tempax = (SiS_Pr->SiS_VT - SiS_Pr->SiS_VDE) * SiS_Pr->SiS_RVBHCFACT;
  tempax = (tempax * SiS_Pr->SiS_HT) / tempbx;
  return((USHORT) tempax);
}

/* TW: New from 300/301LV BIOS 1.16.51 for ECS A907. Seems highly preliminary. */
void
SiS_Set300Part2Regs(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
    			USHORT ModeIdIndex, USHORT RefreshRateTableIndex,
			USHORT BaseAddr, USHORT ModeNo)
{
  USHORT crt2crtc, resindex;
  int    i,j;
  const  SiS_Part2PortTblStruct *CRT2Part2Ptr = NULL;

  if(HwDeviceExtension->jChipType != SIS_300) return;
  if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) return;

  if(ModeNo<=0x13) {
    	crt2crtc = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
    	crt2crtc = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  resindex = crt2crtc & 0x3F;
  if(SiS_Pr->SiS_SetFlag & LCDVESATiming) CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_1;
  else                                    CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_2;

  /* TW: The BIOS code (1.16.51) is obviously a fragment! */
  if(ModeNo > 0x13) {
     CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_1;
     resindex = 4;
  }

  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x01,0x80,(CRT2Part2Ptr+resindex)->CR[0]);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x02,0x80,(CRT2Part2Ptr+resindex)->CR[1]);
  for(i = 2, j = 0x04; j <= 0x06; i++, j++ ) {
        SiS_SetReg1(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
  }
  for(j = 0x1c; j <= 0x1d; i++, j++ ) {
        SiS_SetReg1(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
  }
  for(j = 0x1f; j <= 0x21; i++, j++ ) {
        SiS_SetReg1(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
  }
  SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x23,(CRT2Part2Ptr+resindex)->CR[10]);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0x0f,(CRT2Part2Ptr+resindex)->CR[11]);
}

/* TW: Set 301 Macrovision(tm) registers */
void
SiS_SetGroup3(SiS_Private *SiS_Pr, USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
              USHORT ModeIdIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp;
  USHORT i;
  const UCHAR  *tempdi;
  USHORT modeflag;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) return;

  if(ModeNo<=0x13)
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  else
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;

  SiS_SetReg1(SiS_Pr->SiS_Part3Port,0x00,0x00);

  if(SiS_Pr->SiS_VBInfo & SetPALTV) {
    SiS_SetReg1(SiS_Pr->SiS_Part3Port,0x13,0xFA);
    SiS_SetReg1(SiS_Pr->SiS_Part3Port,0x14,0xC8);
  } else {
    if(HwDeviceExtension->jChipType >= SIS_315H) {
      SiS_SetReg1(SiS_Pr->SiS_Part3Port,0x13,0xF5);
      SiS_SetReg1(SiS_Pr->SiS_Part3Port,0x14,0xB7);
    } else {
      SiS_SetReg1(SiS_Pr->SiS_Part3Port,0x13,0xF6);
      SiS_SetReg1(SiS_Pr->SiS_Part3Port,0x14,0xBf);
    }
  }

  temp = 0;
  if((HwDeviceExtension->jChipType == SIS_630)||
     (HwDeviceExtension->jChipType == SIS_730)) {
     temp = 0x35;
  } else if(HwDeviceExtension->jChipType >= SIS_315H) {
     temp = 0x38;
  }
  if(temp) {
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
          if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) & 0x01) {
              if(SiS_GetReg1(SiS_Pr->SiS_P3d4,temp) & EnablePALM){  /* 0x40 */
                  SiS_SetReg1(SiS_Pr->SiS_Part3Port,0x13,0xFA);
                  SiS_SetReg1(SiS_Pr->SiS_Part3Port,0x14,0xC8);
                  SiS_SetReg1(SiS_Pr->SiS_Part3Port,0x3D,0xA8);
              }
          }
      }
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {
    tempdi = SiS_Pr->SiS_HiTVGroup3Data;
    if(SiS_Pr->SiS_SetFlag & TVSimuMode) {
      tempdi = SiS_Pr->SiS_HiTVGroup3Simu;
      if(!(modeflag & Charx8Dot)) {
        tempdi = SiS_Pr->SiS_HiTVGroup3Text;
      }
    }
    if(SiS_Pr->SiS_HiVision & 0x03) {
       tempdi = SiS_HiTVGroup3_1;
       if(SiS_Pr->SiS_HiVision & 0x02) tempdi = SiS_HiTVGroup3_2;
    }
    for(i=0; i<=0x3E; i++){
       SiS_SetReg1(SiS_Pr->SiS_Part3Port,i,tempdi[i]);
    }
  }

  return;
}

/* TW: Set 301 VGA2 registers */
void
SiS_SetGroup4(SiS_Private *SiS_Pr, USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
              USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
	      PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempax,tempcx,tempbx,modeflag,temp,temp2,resinfo;
  ULONG tempebx,tempeax,templong;


  if(ModeNo<=0x13) {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  if(HwDeviceExtension->jChipType >= SIS_315H) {
      /* TW: From 650/302LV 1.10.6s (not for 300/301LV - no LCDA on this combination) */
     if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
           SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x24,0x0e);
        }
     }
  }

  if(SiS_Pr->SiS_VBType & VB_SIS302LV) {
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
          SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x10,0x9f);
      }
  }

  if(HwDeviceExtension->jChipType >= SIS_315H) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
        if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	   /* TW: From 650/301LV (any, incl. 1.10.6s, 1.10.7w) */
  	   /* TW: This is a duplicate; done at the end, too */
	   if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x39) & 0x04) {
		SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x27,0x2c);
	   }
	   SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x2a,0x00);
	   SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x30,0x00);
	   SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x34,0x10);
	}
   	return;
     }
  }

  temp = SiS_Pr->SiS_RVBHCFACT;
  SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x13,temp);

  tempbx = SiS_Pr->SiS_RVBHCMAX;
  temp = tempbx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x14,temp);

  temp2 = (((tempbx & 0xFF00) >> 8) << 7) & 0x00ff;

  tempcx = SiS_Pr->SiS_VGAHT - 1;
  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x16,temp);

  temp = (((tempcx & 0xFF00) >> 8) << 3) & 0x00ff;
  temp2 |= temp;

  tempcx = SiS_Pr->SiS_VGAVT - 1;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV))  tempcx -= 5;

  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x17,temp);

  temp = temp2 | ((tempcx & 0xFF00) >> 8);
  SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x15,temp);

  tempbx = SiS_Pr->SiS_VGAHDE;
  if(modeflag & HalfDCLK)  tempbx >>= 1;

  /* TW: New for 650/301LV and 630/301B */
  temp = 0xA0;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {
       temp = 0;
       if(tempbx > 800) {
          temp = 0xA0;
          if(tempbx != 1024) {
             temp = 0xC0;
             if(tempbx != 1280) temp = 0;
	  }
       }
  } else
    if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
      if(tempbx <= 800) {
         temp = 0x80;
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
            temp = 0;
            if(tempbx > 800) temp = 0x60;
         }
      }
  } else {
      temp = 0x80;
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
            temp = 0;
            if(tempbx > 800) temp = 0x60;
      }
  }
  if(SiS_Pr->SiS_HiVision & 0x03) {
        temp = 0;
	if(SiS_Pr->SiS_VGAHDE == 1024) temp = 0x20;
  }
  if(HwDeviceExtension->jChipType >= SIS_315H) {
  	if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x39) & 0x04) temp = 0;
  }

  if(SiS_Pr->SiS_VBType & VB_SIS301) {
     if(SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x1024)
        temp |= 0x0A;
  }

  SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x0E,0x10,temp);

  tempebx = SiS_Pr->SiS_VDE;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) {
     if(!(temp & 0xE0)) tempebx >>=1;
  }

  tempcx = SiS_Pr->SiS_RVBHRS;
  temp = tempcx & 0x00FF;
  SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x18,temp);

  tempeax = SiS_Pr->SiS_VGAVDE;
  tempcx |= 0x4000;
  if(tempeax <= tempebx){
    tempcx ^= 0x4000;
  } else {
    tempeax -= tempebx;
  }

  templong = (tempeax * 256 * 1024) % tempebx;
  tempeax = (tempeax * 256 * 1024) / tempebx;
  tempebx = tempeax;
  if(templong != 0) tempebx++;

  temp = (USHORT)(tempebx & 0x000000FF);
  SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x1B,temp);
  temp = (USHORT)((tempebx & 0x0000FF00) >> 8);
  SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x1A,temp);

  tempbx = (USHORT)(tempebx >> 16);
  temp = tempbx & 0x00FF;
  temp <<= 4;
  temp |= ((tempcx & 0xFF00) >> 8);
  SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x19,temp);

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {

         SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x1C,0x28);
	 tempbx = 0;
         tempax = SiS_Pr->SiS_VGAHDE;
         if(modeflag & HalfDCLK) tempax >>= 1;
         if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) || (SiS_Pr->SiS_HiVision & 0x03)) {
	     if(HwDeviceExtension->jChipType >= SIS_315H) {
	         if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x39) & 0x04) tempax >>= 1;
		 else if(tempax > 800) tempax -= 800;
	     } else {
                 if(tempax > 800) tempax -= 800;
             }
         }

/*       if((SiS_Pr->SiS_VBInfo & (SetCRT2ToTV | SetPALTV)) && (!(SiS_Pr->SiS_HiVision & 0x03))) {  */
 	 if((SiS_Pr->SiS_VBInfo & (SetCRT2ToTV - SetCRT2ToHiVisionTV)) && (!(SiS_Pr->SiS_HiVision & 0x03))) {
           if(tempax > 800) {
	      tempbx = 8;
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
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {		/* From 1.10.7w */
	 	if(ModeNo > 0x13) {			/* From 1.10.7w */
			if(resinfo == 8) tempax = 0x1f;	/* From 1.10.7w */
		}					/* From 1.10.7w */
	 }						/* From 1.10.7w */
	 SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x1D,tempax & 0x00FF);
	 temp <<= 4;
	 temp |= tempbx;
	 SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x1E,temp);

	 if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	    if(IS_SIS650740) {
	        temp = 0x0026;  /* 1.10.7w; 1.10.8r; needs corresponding code in Dis/EnableBridge! */
	    } else {
	        temp = 0x0036;
	    }
	 } else {
	     temp = 0x0036;
	 }
         if((SiS_Pr->SiS_VBInfo & (SetCRT2ToTV - SetCRT2ToHiVisionTV)) &&
	                               (!(SiS_Pr->SiS_HiVision & 0x03))) {
		temp |= 0x01;
	        if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	          if(!(SiS_Pr->SiS_SetFlag & TVSimuMode))
  	                  temp &= 0xFE;
		}
         }
         SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x1F,0xC0,temp);

	 tempbx = SiS_Pr->SiS_HT;
	 if(HwDeviceExtension->jChipType >= SIS_315H) {
	 	if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x39) & 0x04) tempbx >>= 1;
	 }
         tempbx >>= 1;
	 tempbx -= 2;
         temp = ((tempbx & 0x0700) >> 8) << 3;
         SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x21,0xC0,temp);
         temp = tempbx & 0x00FF;
         SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x22,temp);
	 
         if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	    if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
               SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x24,0x0e);
	    }
	 }

	 if(HwDeviceExtension->jChipType >= SIS_315H) {
	     /* TW: 650/LV BIOS does this for all bridge types - assumingly wrong */
	     /* 315, 330, 650+301B BIOS don't do this at all */
             /* TW: This is a duplicate; done for LCDA as well (see above) */
	     if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	        if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x39) & 0x04) {
		   SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x27,0x2c);
	        }
	        SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x2a,0x00);
	        SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x30,0x00);
	        SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x34,0x10);
	     }
         } else if(HwDeviceExtension->jChipType == SIS_300) {
	     /* TW: 300/301LV BIOS does this for all bridge types - assumingly wrong */
	     if(SiS_Pr->SiS_VBType & VB_SIS301LV302LV) {
	        SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x2a,0x00);
	        SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x30,0x00);
	        SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x34,0x10);
	     }
	 }

  }  /* 301B */

  SiS_SetCRT2VCLK(SiS_Pr,BaseAddr,ROMAddr,ModeNo,ModeIdIndex,
                  RefreshRateTableIndex,HwDeviceExtension);
}


void
SiS_SetCRT2VCLK(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                 USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT vclkindex;
  USHORT tempah;

  vclkindex = SiS_GetVCLK2Ptr(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                              HwDeviceExtension);

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
     tempah = SiS_Pr->SiS_VBVCLKData[vclkindex].Part4_A;
     SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x0A,tempah);
     tempah = SiS_Pr->SiS_VBVCLKData[vclkindex].Part4_B;
     SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x0B,tempah);
     if(HwDeviceExtension->jChipType >= SIS_315H) {
	if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
           if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	      if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {
                 if((ModeNo == 0x4a) || (ModeNo == 0x38)) {
		    SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x0a,0x57);
		    SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x0b,0x46);
		    SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x1f,0xf6);
                 }
              }
           }
	}
     }
  } else {	
     SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x0A,0x01);
     tempah = SiS_Pr->SiS_VBVCLKData[vclkindex].Part4_B;
     SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x0B,tempah);
     tempah = SiS_Pr->SiS_VBVCLKData[vclkindex].Part4_A;
     SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x0A,tempah);
  }
  SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x12,0x00);
  tempah = 0x08;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) tempah |= 0x20;
  SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x12,tempah);
}

USHORT
SiS_GetVCLK2Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempbx;
  const USHORT LCDXlat0VCLK[4]    = {VCLK40, VCLK40, VCLK40, VCLK40};
  const USHORT LVDSXlat1VCLK[4]   = {VCLK40, VCLK40, VCLK40, VCLK40};
#ifdef SIS300
  const USHORT LCDXlat1VCLK300[4] = {VCLK65,   VCLK65,   VCLK65,   VCLK65};
  const USHORT LCDXlat2VCLK300[4] = {VCLK108_2,VCLK108_2,VCLK108_2,VCLK108_2};
  const USHORT LVDSXlat2VCLK300[4]= {VCLK65,   VCLK65,   VCLK65,   VCLK65};
  const USHORT LVDSXlat3VCLK300[4]= {VCLK65,   VCLK65,   VCLK65,   VCLK65};
#endif
#ifdef SIS315H
  const USHORT LCDXlat1VCLK310[4] = {VCLK65+2,   VCLK65+2,   VCLK65+2,   VCLK65+2};
  const USHORT LCDXlat2VCLK310[4] = {VCLK108_2+5,VCLK108_2+5,VCLK108_2+5,VCLK108_2+5};
  const USHORT LVDSXlat2VCLK310[4]= {VCLK65+2,   VCLK65+2,   VCLK65+2,   VCLK65+2};
  const USHORT LVDSXlat3VCLK310[4]= {VCLK108_2+5,VCLK108_2+5,VCLK108_2+5,VCLK108_2+5};
#endif
  USHORT CRT2Index,VCLKIndex=0;
  USHORT modeflag,resinfo;
  const UCHAR *CHTVVCLKPtr=NULL;
  const USHORT *LCDXlatVCLK1 = NULL;
  const USHORT *LCDXlatVCLK2 = NULL;
  const USHORT *LVDSXlatVCLK2 = NULL;
  const USHORT *LVDSXlatVCLK3 = NULL;

  if(HwDeviceExtension->jChipType >= SIS_315H) {
#ifdef SIS315H
		LCDXlatVCLK1 = LCDXlat1VCLK310;
		LCDXlatVCLK2 = LCDXlat2VCLK310;
		LVDSXlatVCLK2 = LVDSXlat2VCLK310;
		LVDSXlatVCLK3 = LVDSXlat3VCLK310;
#endif
  } else {
#ifdef SIS300
		LCDXlatVCLK1 = LCDXlat1VCLK300;
		LCDXlatVCLK2 = LCDXlat2VCLK300;
		LVDSXlatVCLK2 = LVDSXlat2VCLK300;
		LVDSXlatVCLK3 = LVDSXlat3VCLK300;
#endif
  }

  if(ModeNo <= 0x13) {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
    	CRT2Index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
    	CRT2Index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {    /* 30x/B/LV */

     if(SiS_Pr->SiS_SetFlag & ProgrammingCRT2) {

        CRT2Index >>= 6;
        if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)){      /*  LCD */
            if(HwDeviceExtension->jChipType < SIS_315H) {
	       if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600)
	    		VCLKIndex = LCDXlat0VCLK[CRT2Index];
	       else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)
	    		VCLKIndex = LCDXlatVCLK1[CRT2Index];
	       else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600)
	    		VCLKIndex = LCDXlatVCLK1[CRT2Index];
	       else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768)
	    		VCLKIndex = LCDXlatVCLK1[CRT2Index];
	       else
	    		VCLKIndex = LCDXlatVCLK2[CRT2Index];
	    } else {
               /* TW: 330, 650/301LV BIOS does not check expanding, 315 does  */
	       if( (HwDeviceExtension->jChipType > SIS_315PRO) ||
	           (!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) ) {
      	          if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
		     VCLKIndex = 0x19;
		  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
		     VCLKIndex = 0x19;
		  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
		     VCLKIndex = 0x21;
		  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
		     VCLKIndex = LCDXlatVCLK1[CRT2Index];
                  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x960) {
		     VCLKIndex = 0x45;  /* TW: in VBVCLK table */
		     if(resinfo == 0x09) VCLKIndex++;
	          } else {
		     VCLKIndex = LCDXlatVCLK2[CRT2Index];
      	          }
	       } else {
                   VCLKIndex = (UCHAR)SiS_GetReg2((USHORT)(SiS_Pr->SiS_P3ca+0x02));  /*  Port 3cch */
         	   VCLKIndex = ((VCLKIndex >> 2) & 0x03);
        	   if(ModeNo > 0x13) {
          		VCLKIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
        	   }
		   if(ModeNo <= 0x13) {  /* TW: 315 BIOS */
		      if(SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC == 1) VCLKIndex = 0x42;
		   }
		   if(VCLKIndex == 0) VCLKIndex = 0x41;
		   if(VCLKIndex == 1) VCLKIndex = 0x43;
		   if(VCLKIndex == 4) VCLKIndex = 0x44;
	       }
	    }
        } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {                 /*  TV */
        	if( (SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) && 
		    (!(SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) ) {
          		if(SiS_Pr->SiS_SetFlag & RPLLDIV2XO)  VCLKIndex = HiTVVCLKDIV2;
     			else                                  VCLKIndex = HiTVVCLK;
          		if(SiS_Pr->SiS_SetFlag & TVSimuMode) {
            			if(modeflag & Charx8Dot)      VCLKIndex = HiTVSimuVCLK;
            			else 			      VCLKIndex = HiTVTextVCLK;
          		}
        	} else {
       			if(SiS_Pr->SiS_SetFlag & RPLLDIV2XO)  VCLKIndex = TVVCLKDIV2;
            		else         		              VCLKIndex = TVVCLK;
          	}
		if(HwDeviceExtension->jChipType >= SIS_315H) {
              		VCLKIndex += 25;
  		}
        } else {         					/* RAMDAC2 */
        	VCLKIndex = (UCHAR)SiS_GetReg2((USHORT)(SiS_Pr->SiS_P3ca+0x02));
        	VCLKIndex = ((VCLKIndex >> 2) & 0x03);
        	if(ModeNo > 0x13) {
          		VCLKIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
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

        VCLKIndex = (UCHAR)SiS_GetReg2((USHORT)(SiS_Pr->SiS_P3ca+0x02));
        VCLKIndex = ((VCLKIndex >> 2) & 0x03);
        if(ModeNo > 0x13) {
             VCLKIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
	     if(HwDeviceExtension->jChipType < SIS_315H) {
                VCLKIndex &= 0x3f;
		if( (HwDeviceExtension->jChipType != SIS_630) &&
		    (HwDeviceExtension->jChipType != SIS_300) ) {
		   if(VCLKIndex == 0x1b) VCLKIndex = 0x35;
		}
	     }
        }
    }

  } else {       /*   LVDS  */

    	VCLKIndex = CRT2Index;

	if(SiS_Pr->SiS_SetFlag & ProgrammingCRT2) {  /* programming CRT2 */

	   if( (SiS_Pr->SiS_IF_DEF_CH70xx != 0) && (SiS_Pr->SiS_VBInfo & SetCRT2ToTV) ) {

		VCLKIndex &= 0x1f;
        	tempbx = 0;
		if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) tempbx += 1;
        	if(SiS_Pr->SiS_VBInfo & SetPALTV) {
			tempbx += 2;
			if(SiS_Pr->SiS_CHSOverScan) tempbx = 8;
			if(SiS_Pr->SiS_CHPALM) {
				tempbx = 4;
				if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) tempbx += 1;
			} else if(SiS_Pr->SiS_CHPALN) {
				tempbx = 6;
				if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) tempbx += 1;
			}
		}
       		switch(tempbx) {
          	   case  0: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUNTSC;  break;
         	   case  1: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKONTSC;  break;
                   case  2: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUPAL;   break;
                   case  3: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPAL;   break;
		   case  4: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUPALM;  break;
         	   case  5: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPALM;  break;
                   case  6: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUPALN;  break;
                   case  7: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPALN;  break;
		   case  8: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKSOPAL;  break;
		   default: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPAL;   break;
        	}
        	VCLKIndex = CHTVVCLKPtr[VCLKIndex];

	   } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {

	        VCLKIndex >>= 6;
     		if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel800x600) ||
		   (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel320x480))
     			VCLKIndex = LVDSXlat1VCLK[VCLKIndex];
     		else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768)
     			VCLKIndex = LVDSXlatVCLK2[VCLKIndex];
		else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600)
                        VCLKIndex = LVDSXlatVCLK2[VCLKIndex];
		else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768)
                        VCLKIndex = LVDSXlatVCLK2[VCLKIndex];			
     		else    VCLKIndex = LVDSXlatVCLK3[VCLKIndex];

	   } else {

	        VCLKIndex = (UCHAR)SiS_GetReg2((USHORT)(SiS_Pr->SiS_P3ca+0x02));
                VCLKIndex = ((VCLKIndex >> 2) & 0x03);
                if(ModeNo > 0x13) {
                     VCLKIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
		     if(HwDeviceExtension->jChipType < SIS_315H) {
    		        VCLKIndex &= 0x3F;
                     }
		     if( (HwDeviceExtension->jChipType == SIS_630) &&
                         (HwDeviceExtension->jChipRevision >= 0x30) ) {
		         	if(VCLKIndex == 0x14) VCLKIndex = 0x2e;
		     }
	        }
	   }

	} else {  /* if not programming CRT2 */

	   VCLKIndex = (UCHAR)SiS_GetReg2((USHORT)(SiS_Pr->SiS_P3ca+0x02));
           VCLKIndex = ((VCLKIndex >> 2) & 0x03);
           if(ModeNo > 0x13) {
              VCLKIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
              if(HwDeviceExtension->jChipType < SIS_315H) {
	         VCLKIndex &= 0x3F;
	         if( (HwDeviceExtension->jChipType != SIS_630) &&
		     (HwDeviceExtension->jChipType != SIS_300) ) {
		        if(VCLKIndex == 0x1b) VCLKIndex = 0x35;
	         }
#if 0		 
		 if(HwDeviceExtension->jChipType == SIS_730) {
		    if(VCLKIndex == 0x0b) VCLKIndex = 0x40;   /* 1024x768-70 */
		    if(VCLKIndex == 0x0d) VCLKIndex = 0x41;   /* 1024x768-75 */ 
		 }
#endif		 
	      }
	   }

	}

  }
#ifdef TWDEBUG
  xf86DrvMsg(0, X_INFO, "VCLKIndex %d (0x%x)\n", VCLKIndex, VCLKIndex);
#endif
  return (VCLKIndex);
}

/* TW: Set 301 Palette address port registers */
/* TW: Checked against 650/301LV BIOS */
void
SiS_SetGroup5(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr,
              UCHAR *ROMAddr, USHORT ModeNo, USHORT ModeIdIndex)
{

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)  return;

  if(SiS_Pr->SiS_ModeType == ModeVGA){
     if(!(SiS_Pr->SiS_VBInfo & (SetInSlaveMode | LoadDACFlag))){
        SiS_EnableCRT2(SiS_Pr);
        SiS_LoadDAC(SiS_Pr,HwDeviceExtension,ROMAddr,ModeNo,ModeIdIndex);
     }
  }
}

void
SiS_ModCRT1CRTC(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT temp,tempah,i,modeflag,j;
  USHORT ResInfo,DisplayType;
  const SiS_LVDSCRT1DataStruct *LVDSCRT1Ptr=NULL;

  if(ModeNo <= 0x13) {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  temp = SiS_GetLVDSCRT1Ptr(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
                            &ResInfo,&DisplayType);

  if(temp == 0) return;

  if(HwDeviceExtension->jChipType < SIS_315H) {
     if(SiS_Pr->SiS_SetFlag & SetDOSMode) return;
  }

  switch(DisplayType) {
    case 0 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_1;           break;
    case 1 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_1;          break;
    case 2 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_1;         break;
    case 3 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_1_H;         break;
    case 4 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_1_H;        break;
    case 5 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_1_H;       break;
    case 6 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_2;           break;
    case 7 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_2;          break;
    case 8 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_2;         break;
    case 9 : LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1800x600_2_H;         break;
    case 10: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_2_H;        break;
    case 11: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x1024_2_H;       break;
    case 12: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1XXXxXXX_1;           break;
    case 13: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1XXXxXXX_1_H;         break;
    case 14: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_1;         break;
    case 15: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_1_H;       break;
    case 16: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_2;         break;
    case 17: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11400x1050_2_H;       break;
    case 18: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1UNTSC;               break;
    case 19: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1ONTSC;               break;
    case 20: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1UPAL;                break;
    case 21: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1OPAL;                break;
    case 22: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1320x480_1;           break; /* FSTN */
    case 23: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_1;          break;
    case 24: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_1_H;        break;
    case 25: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_2;          break;
    case 26: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_2_H;        break;
    case 27: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_1;          break;
    case 28: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_1_H;        break;
    case 29: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_2;          break;
    case 30: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11152x768_2_H;        break;
    case 36: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_1;         break;
    case 37: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_1_H;       break;
    case 38: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_2;         break;
    case 39: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11600x1200_2_H;       break;
    case 40: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x768_1;          break;
    case 41: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x768_1_H;        break;
    case 42: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x768_2;          break;
    case 43: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11280x768_2_H;        break;
    case 99: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1SOPAL;               break;
    default: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x768_1;          break;
  }

  SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x11,0x7f);                        /*unlock cr0-7  */

  tempah = (LVDSCRT1Ptr+ResInfo)->CR[0];
  SiS_SetReg1(SiS_Pr->SiS_P3d4,0x00,tempah);

  for(i=0x02,j=1;i<=0x05;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_Pr->SiS_P3d4,i,tempah);
  }
  for(i=0x06,j=5;i<=0x07;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_Pr->SiS_P3d4,i,tempah);
  }
  for(i=0x10,j=7;i<=0x11;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_Pr->SiS_P3d4,i,tempah);
  }
  for(i=0x15,j=9;i<=0x16;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_Pr->SiS_P3d4,i,tempah);
  }
  for(i=0x0A,j=11;i<=0x0C;i++,j++){
    tempah = (LVDSCRT1Ptr+ResInfo)->CR[j];
    SiS_SetReg1(SiS_Pr->SiS_P3c4,i,tempah);
  }

  tempah = (LVDSCRT1Ptr+ResInfo)->CR[14];
  tempah &= 0xE0;
  SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0E,0x1f,tempah);     

  tempah = (LVDSCRT1Ptr+ResInfo)->CR[14];
  tempah &= 0x01;
  tempah <<= 5;
  if(modeflag & DoubleScanMode)  tempah |= 0x080;
  SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x09,~0x020,tempah);

  /* TW: 650/LVDS BIOS - doesn't make sense */
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     if(modeflag & HalfDCLK)
        SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x11,0x7f);
  }
}

BOOLEAN
SiS_GetLVDSCRT1Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		   USHORT RefreshRateTableIndex,USHORT *ResInfo,
		   USHORT *DisplayType)
 {
  USHORT tempbx,modeflag=0;
  USHORT Flag,CRT2CRTC;

  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
        if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) return 0;
     }
  } else {
     if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) return 0;
  }

  if(ModeNo <= 0x13) {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
    	CRT2CRTC = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	CRT2CRTC = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  Flag = 1;
  tempbx = 0;
  if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
     if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
        Flag = 0;
        tempbx = 18;
        if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) tempbx++;
        if(SiS_Pr->SiS_VBInfo & SetPALTV) {
      	   tempbx += 2;
	   if(SiS_Pr->SiS_CHSOverScan) tempbx = 99;
	   if(SiS_Pr->SiS_CHPALM) {
	      tempbx = 18;  /* PALM uses NTSC data */
	      if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) tempbx++;
	   } else if(SiS_Pr->SiS_CHPALN) {
	      tempbx = 20;  /* PALN uses PAL data  */
	      if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) tempbx++;
	   }
        }
     }
  }
  if(Flag) {
     tempbx = SiS_Pr->SiS_LCDResInfo;
     tempbx -= SiS_Pr->SiS_PanelMinLVDS;
     if(SiS_Pr->SiS_LCDResInfo <= SiS_Pr->SiS_Panel1280x1024) {
        if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 6;
        if(modeflag & HalfDCLK) tempbx += 3;
     } else {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
           tempbx = 14;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x600) {
           tempbx = 23;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1152x768) {
           tempbx = 27;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
           tempbx = 36;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
        } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x768) {
           tempbx = 40;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 2;
	   if(modeflag & HalfDCLK) tempbx++;
        }
     }
     if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
        tempbx = 12;
	if(modeflag & HalfDCLK) tempbx++;
     }
  }
  if(SiS_Pr->SiS_IF_DEF_FSTN){
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel320x480){
        tempbx = 22;
     }
  }
  *ResInfo = CRT2CRTC & 0x3F;
  *DisplayType = tempbx;
  return 1;
}

void
SiS_SetCRT2ECLK(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex,
           USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempah,tempal,pushax;
  USHORT vclkindex=0;
    
  if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel640x480) || (SiS_Pr->SiS_IF_DEF_TRUMPION == 1)) {
	SiS_Pr->SiS_SetFlag &= (~ProgrammingCRT2);
        tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
    	tempal &= 0x3F;
	if(tempal == 2) RefreshRateTableIndex--;
	vclkindex = SiS_GetVCLK2Ptr(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,
                               RefreshRateTableIndex,HwDeviceExtension);
	SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
  } else {
        vclkindex = SiS_GetVCLK2Ptr(SiS_Pr,ROMAddr,ModeNo,ModeIdIndex,
                               RefreshRateTableIndex,HwDeviceExtension);
  }
  
  tempal = 0x02B;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
     if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
    	tempal += 3;
     }
  }
  SiS_SetReg1(SiS_Pr->SiS_P3c4,0x05,0x86);
  pushax = tempal;
  SiS_SetReg1(SiS_Pr->SiS_P3c4,0x31,0x20);
  tempah = SiS_Pr->SiS_VCLKData[vclkindex].SR2B;
  SiS_SetReg1(SiS_Pr->SiS_P3c4,tempal,tempah);
  tempal++;
  tempah = SiS_Pr->SiS_VCLKData[vclkindex].SR2C;
  SiS_SetReg1(SiS_Pr->SiS_P3c4,tempal,tempah);
  SiS_SetReg1(SiS_Pr->SiS_P3c4,0x31,0x10);
  tempal = pushax;
  tempah = SiS_Pr->SiS_VCLKData[vclkindex].SR2B;
  SiS_SetReg1(SiS_Pr->SiS_P3c4,tempal,tempah);
  tempal++;
  tempah = SiS_Pr->SiS_VCLKData[vclkindex].SR2C;
  SiS_SetReg1(SiS_Pr->SiS_P3c4,tempal,tempah);
  SiS_SetReg1(SiS_Pr->SiS_P3c4,0x31,0x00);
  tempal = pushax;
  tempah = SiS_Pr->SiS_VCLKData[vclkindex].SR2B;
  SiS_SetReg1(SiS_Pr->SiS_P3c4,tempal,tempah);
  tempal++;
  tempah = SiS_Pr->SiS_VCLKData[vclkindex].SR2C;
  SiS_SetReg1(SiS_Pr->SiS_P3c4,tempal,tempah);
  return;
}

#if 0  /* TW: Not used */
void
SiS_SetDefCRT2ExtRegs(SiS_Private *SiS_Pr, USHORT BaseAddr)
{
  USHORT  temp;

  if(SiS_Pr->SiS_IF_DEF_LVDS==0) {
    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x02,0x40);
    SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x10,0x80);
    temp=(UCHAR)SiS_GetReg1(SiS_Pr->SiS_P3c4,0x16);
    temp &= 0xC3;
    SiS_SetReg1(SiS_Pr->SiS_P3d4,0x35,temp);
  } else {
    SiS_SetReg1(SiS_Pr->SiS_P3d4,0x32,0x02);
    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x02,0x00);
  }
}
#endif

/* TW: Start of Chrontel 70xx functions ---------------------- */

/* Set-up the Chrontel Registers */
void
SiS_SetCHTVReg(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
               USHORT RefreshRateTableIndex)
{
  USHORT temp, tempbx, tempcl;
  USHORT TVType, resindex;
  const SiS_CHTVRegDataStruct *CHTVRegData = NULL;

  if(ModeNo <= 0x13)
    	tempcl = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
    	tempcl = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  TVType = 0;
  if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) TVType += 1;
  if(SiS_Pr->SiS_VBInfo & SetPALTV) {
  	TVType += 2;
	if(SiS_Pr->SiS_CHSOverScan) TVType = 8;
	if(SiS_Pr->SiS_CHPALM) {
		TVType = 4;
		if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) TVType += 1;
	} else if(SiS_Pr->SiS_CHPALN) {
		TVType = 6;
		if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) TVType += 1;
	}
  }
  switch(TVType) {
    	case  0: CHTVRegData = SiS_Pr->SiS_CHTVReg_UNTSC; break;
    	case  1: CHTVRegData = SiS_Pr->SiS_CHTVReg_ONTSC; break;
    	case  2: CHTVRegData = SiS_Pr->SiS_CHTVReg_UPAL;  break;
    	case  3: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPAL;  break;
	case  4: CHTVRegData = SiS_Pr->SiS_CHTVReg_UPALM; break;
    	case  5: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPALM; break;
    	case  6: CHTVRegData = SiS_Pr->SiS_CHTVReg_UPALN; break;
    	case  7: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPALN; break;
	case  8: CHTVRegData = SiS_Pr->SiS_CHTVReg_SOPAL; break;
	default: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPAL;  break;
  }
  resindex = tempcl & 0x3F;

  if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {

#ifdef SIS300
  
     /* Chrontel 7005 - I assume that it does not come with a 310/325 series chip */

     /* TW: We don't support modes >800x600 */
     if (resindex > 5) return;

     if(SiS_Pr->SiS_VBInfo & SetPALTV) {
    	SiS_SetCH700x(SiS_Pr,0x4304);   /* TW: 0x40=76uA (PAL); 0x03=15bit non-multi RGB*/
    	SiS_SetCH700x(SiS_Pr,0x6909);	/* TW: Black level for PAL (105)*/
     } else {
    	SiS_SetCH700x(SiS_Pr,0x0304);   /* TW: upper nibble=71uA (NTSC), 0x03=15bit non-multi RGB*/
    	SiS_SetCH700x(SiS_Pr,0x7109);	/* TW: Black level for NTSC (113)*/
     }

     temp = CHTVRegData[resindex].Reg[0];
     tempbx=((temp&0x00FF)<<8)|0x00;	/* TW: Mode register */
     SiS_SetCH700x(SiS_Pr,tempbx);
     temp = CHTVRegData[resindex].Reg[1];
     tempbx=((temp&0x00FF)<<8)|0x07;	/* TW: Start active video register */
     SiS_SetCH700x(SiS_Pr,tempbx);
     temp = CHTVRegData[resindex].Reg[2];
     tempbx=((temp&0x00FF)<<8)|0x08;	/* TW: Position overflow register */
     SiS_SetCH700x(SiS_Pr,tempbx);
     temp = CHTVRegData[resindex].Reg[3];
     tempbx=((temp&0x00FF)<<8)|0x0A;	/* TW: Horiz Position register */
     SiS_SetCH700x(SiS_Pr,tempbx);
     temp = CHTVRegData[resindex].Reg[4];
     tempbx=((temp&0x00FF)<<8)|0x0B;	/* TW: Vertical Position register */
     SiS_SetCH700x(SiS_Pr,tempbx);

     /* TW: Set minimum flicker filter for Luma channel (SR1-0=00),
                minimum text enhancement (S3-2=10),
   	        maximum flicker filter for Chroma channel (S5-4=10)
	        =00101000=0x28 (When reading, S1-0->S3-2, and S3-2->S1-0!)
      */
     SiS_SetCH700x(SiS_Pr,0x2801);

     /* TW: Set video bandwidth
            High bandwith Luma composite video filter(S0=1)
            low bandwith Luma S-video filter (S2-1=00)
	    disable peak filter in S-video channel (S3=0)
	    high bandwidth Chroma Filter (S5-4=11)
	    =00110001=0x31
     */
     SiS_SetCH700x(SiS_Pr,0xb103);       /* old: 3103 */

     /* TW: Register 0x3D does not exist in non-macrovision register map
            (Maybe this is a macrovision register?)
      */
     /* SiS_SetCH70xx(SiS_Pr,0x003D); */

     /* TW: Register 0x10 only contains 1 writable bit (S0) for sensing,
            all other bits a read-only. Macrovision?
      */
     SiS_SetCH70xxANDOR(SiS_Pr,0x0010,0x1F);

     /* TW: Register 0x11 only contains 3 writable bits (S0-S2) for
            contrast enhancement (set to 010 -> gain 1 Yout = 17/16*(Yin-30) )
      */
     SiS_SetCH70xxANDOR(SiS_Pr,0x0211,0xF8);

     /* TW: Clear DSEN
      */
     SiS_SetCH70xxANDOR(SiS_Pr,0x001C,0xEF);

     if(!(SiS_Pr->SiS_VBInfo & SetPALTV)) {		/* ---- NTSC ---- */
       if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) {
         if(resindex == 0x04) {   			/* 640x480 overscan: Mode 16 */
      	   SiS_SetCH70xxANDOR(SiS_Pr,0x0020,0xEF);   	/* loop filter off */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0121,0xFE);      /* ACIV on, no need to set FSCI */
         } else {
           if(resindex == 0x05) {    			/* 800x600 overscan: Mode 23 */
             SiS_SetCH70xxANDOR(SiS_Pr,0x0118,0xF0);	/* 0x18-0x1f: FSCI 469,762,048 */
             SiS_SetCH70xxANDOR(SiS_Pr,0x0C19,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x001A,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x001B,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x001C,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x001D,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x001E,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x001F,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x0120,0xEF);     /* Loop filter on for mode 23 */
             SiS_SetCH70xxANDOR(SiS_Pr,0x0021,0xFE);     /* ACIV off, need to set FSCI */
           }
         }
       } else {
         if(resindex == 0x04) {     			 /* ----- 640x480 underscan; Mode 17 */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0020,0xEF); 	 /* loop filter off */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0121,0xFE);
         } else {
           if(resindex == 0x05) {   			 /* ----- 800x600 underscan: Mode 24 */
             SiS_SetCH70xxANDOR(SiS_Pr,0x0118,0xF0);     /* (FSCI was 0x1f1c71c7 - this is for mode 22) */
             SiS_SetCH70xxANDOR(SiS_Pr,0x0919,0xF0);	 /* FSCI for mode 24 is 428,554,851 */
             SiS_SetCH70xxANDOR(SiS_Pr,0x081A,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x0b1B,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x031C,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x0a1D,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x061E,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x031F,0xF0);
             SiS_SetCH70xxANDOR(SiS_Pr,0x0020,0xEF);     /* loop filter off for mode 24 */
             SiS_SetCH70xxANDOR(SiS_Pr,0x0021,0xFE);	 /* ACIV off, need to set FSCI */
           }
         }
       }
     } else {				/* ---- PAL ---- */
           /* TW: We don't play around with FSCI in PAL mode */
         if (resindex == 0x04) {
           SiS_SetCH70xxANDOR(SiS_Pr,0x0020,0xEF); 	/* loop filter off */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0121,0xFE);      /* ACIV on */
         } else {
           SiS_SetCH70xxANDOR(SiS_Pr,0x0020,0xEF); 	/* loop filter off */
           SiS_SetCH70xxANDOR(SiS_Pr,0x0121,0xFE);      /* ACIV on */
         }
     }
     
#endif  /* 300 */

  } else {

     /* Chrontel 7019 - assumed that it does not come with a 300 series chip */

#ifdef SIS315H

     /* TW: We don't support modes >1024x768 */
     if (resindex > 6) return;

     temp = CHTVRegData[resindex].Reg[0];
     tempbx=((temp & 0x00FF) <<8 ) | 0x00;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[1];
     tempbx=((temp & 0x00FF) <<8 ) | 0x01;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[2];
     tempbx=((temp & 0x00FF) <<8 ) | 0x02;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[3];
     tempbx=((temp & 0x00FF) <<8 ) | 0x04;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[4];
     tempbx=((temp & 0x00FF) <<8 ) | 0x03;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[5];
     tempbx=((temp & 0x00FF) <<8 ) | 0x05;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[6];
     tempbx=((temp & 0x00FF) <<8 ) | 0x06;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[7];
     tempbx=((temp & 0x00FF) <<8 ) | 0x07;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[8];
     tempbx=((temp & 0x00FF) <<8 ) | 0x08;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[9];
     tempbx=((temp & 0x00FF) <<8 ) | 0x15;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[10];
     tempbx=((temp & 0x00FF) <<8 ) | 0x1f;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[11];
     tempbx=((temp & 0x00FF) <<8 ) | 0x0c;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[12];
     tempbx=((temp & 0x00FF) <<8 ) | 0x0d;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[13];
     tempbx=((temp & 0x00FF) <<8 ) | 0x0e;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[14];
     tempbx=((temp & 0x00FF) <<8 ) | 0x0f;
     SiS_SetCH701x(SiS_Pr,tempbx);

     temp = CHTVRegData[resindex].Reg[15];
     tempbx=((temp & 0x00FF) <<8 ) | 0x10;
     SiS_SetCH701x(SiS_Pr,tempbx);
     
#endif	/* 315 */

  }
}

/* TW: Chrontel 701x functions ================================= */

void
SiS_Chrontel701xBLOn(SiS_Private *SiS_Pr)
{
#ifndef NEWCH701x
  USHORT temp;
#endif  

  /* TW: Enable Chrontel 7019 LCD panel backlight */
  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
#ifdef NEWCH701x
        SiS_SetCH701x(SiS_Pr,0x6566);
#else  
        temp = SiS_GetCH701x(SiS_Pr,0x66);
        temp |= 0x20;
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x66);
#endif	
  }
}

void
SiS_Chrontel701xBLOff(SiS_Private *SiS_Pr)
{
  USHORT temp;

  /* TW: Disable Chrontel 7019 LCD panel backlight */
  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
        temp = SiS_GetCH701x(SiS_Pr,0x66);
        temp &= 0xDF;
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x66);
  }
}

#ifdef SIS315H  /* -------- 310/325 series only --------- */

void
SiS_SetCH701xForLCD(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr)
{
#ifdef NEWCH701x  
  UCHAR regtable[]  = { 0x1c, 0x5f, 0x64, 0x6f, 0x70, 0x71,
                        0x72, 0x73, 0x74, 0x76, 0x78, 0x7d, 0x66 };
  UCHAR table1024[] = { 0x60, 0x02, 0x00, 0x07, 0x40, 0xed,
                        0xa3, 0xc8, 0xc7, 0xac, 0xe0, 0x02, 0x44 }; 
  UCHAR table1280[] = { 0x60, 0x03, 0x11, 0x00, 0x40, 0xe3,
   			0xad, 0xdb, 0xf6, 0xac, 0xe0, 0x02, 0x44 }; 			
  UCHAR table1400[] = { 0x60, 0x03, 0x11, 0x00, 0x40, 0xe3,         
                        0xad, 0xdb, 0xf6, 0xac, 0xe0, 0x02, 0x44 }; 
  UCHAR table1600[] = { 0x60, 0x04, 0x11, 0x00, 0x40, 0xe3,
  			0xad, 0xde, 0xf6, 0xac, 0x60, 0x1a, 0x44 };
#else
  UCHAR regtable[]  = { 0x1c, 0x5f, 0x64, 0x6f, 0x70, 0x71,
                        0x72, 0x73, 0x74, 0x76, 0x78, 0x7d };
  UCHAR table1024[] = { 0x60, 0x02, 0x00, 0x07, 0x40, 0xed,
                        0xa3, 0xc8, 0xc7, 0xac, 0x60, 0x02 }; 
  UCHAR table1280[] = { 0x60, 0x03, 0x11, 0x00, 0x40, 0xe3,
   			0xad, 0xdb, 0xf6, 0xac, 0xe0, 0x02 }; 			
  UCHAR table1400[] = { 0x60, 0x03, 0x11, 0x00, 0x40, 0xef,   
                        0xad, 0xdb, 0xf6, 0xac, 0x60, 0x02 }; 
  UCHAR table1600[] = { 0x60, 0x04, 0x11, 0x00, 0x40, 0xe3,
  			0xad, 0xde, 0xf6, 0xac, 0x60, 0x1a };
#endif			
  UCHAR *tableptr = NULL;
  USHORT tempbh;
  int i;

  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
     tableptr = table1024;
  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) {
     tableptr = table1280;
  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
     tableptr = table1400;
  } else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) {
     tableptr = table1600;
  } else return;

  tempbh = SiS_GetCH701x(SiS_Pr,0x74);
  if((tempbh == 0xf6) || (tempbh == 0xc7)) {
     tempbh = SiS_GetCH701x(SiS_Pr,0x73);
     if(tempbh == 0xc8) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) return;
     } else if(tempbh == 0xdb) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) return;
	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) return;
     } else if(tempbh == 0xde) {
        if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) return;
     }
  }
#ifdef NEWCH701x     /* New from 740/LVDS: */    
  for(i=0; i<0x0d; i++) {	
#else
  for(i=0; i<0x0c; i++) {
#endif  
     SiS_SetCH701x(SiS_Pr,(tableptr[i] << 8) | regtable[i]);
  }
  SiS_ChrontelPowerSequencing(SiS_Pr);
  tempbh = SiS_GetCH701x(SiS_Pr,0x1e);
  tempbh |= 0xc0;
  SiS_SetCH701x(SiS_Pr,(tempbh << 8) | 0x1e);
  
#ifdef NEWCH701x     /* 740/LVDS: */
  tempbh = SiS_GetCH701x(SiS_Pr,0x1c);
  tempbh &= 0xfb;
  SiS_SetCH701x(SiS_Pr,(tempbh << 8) | 0x1c);
  SiS_SetReg1(SiS_Pr->SiS_Part1Port, 0x2d, 0x03);
  tempbh = SiS_GetCH701x(SiS_Pr,0x64);
  tempbh |= 0x40;
  SiS_SetCH701x(SiS_Pr,(tempbh << 8) | 0x64);
  tempbh = SiS_GetCH701x(SiS_Pr,0x03);
  tempbh &= 0x3f;
  SiS_SetCH701x(SiS_Pr,(tempbh << 8) | 0x03);
#endif  /* End 740/LVDS */
}

void
SiS_ChrontelPowerSequencing(SiS_Private *SiS_Pr)
{
  UCHAR regtable[]  = { 0x67, 0x68, 0x69, 0x6a, 0x6b };
#ifdef NEWCH701x  
  UCHAR table1024[] = { 0x01, 0x02, 0x01, 0x01, 0x01 };
  UCHAR table1400[] = { 0x01, 0x6e, 0x01, 0x01, 0x01 };
#else
  UCHAR table1024[] = { 0x01, 0x02, 0x01, 0x01, 0x02 };
  UCHAR table1400[] = { 0x01, 0x02, 0x01, 0x01, 0x02 };
#endif  
  UCHAR *tableptr = NULL;
  int i;

  /* Set up Power up/down timing */
  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
     tableptr = table1024;
  } else if((SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1280x1024) ||
            (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) ||
	    (SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200)) {
     tableptr = table1400;
  } else return;
  
  for(i=0; i<5; i++) {
     SiS_SetCH701x(SiS_Pr,(tableptr[i] << 8) | regtable[i]);
  }
}

void
SiS_Chrontel701xOn(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr)
{
  USHORT temp;

  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
#ifdef NEWCH701x
     temp = SiS_GetCH701x(SiS_Pr,0x1c);
     temp |= 0x04;
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x1c);
#endif 
     if(SiS_IsYPbPr(SiS_Pr,HwDeviceExtension, BaseAddr)) {
        temp = SiS_GetCH701x(SiS_Pr,0x01);
	temp &= 0x3f;
	temp |= 0x80;	/* TW: Enable YPrPb (HDTV) */
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x01);
     }
     if(SiS_IsChScart(SiS_Pr,HwDeviceExtension, BaseAddr)) {
        temp = SiS_GetCH701x(SiS_Pr,0x01);
	temp &= 0x3f;
	temp |= 0xc0;	/* TW: Enable SCART + CVBS */
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x01);
     }
#ifdef NEWCH701x
     SiS_ChrontelDoSomething5(SiS_Pr);
     SiS_SetCH701x(SiS_Pr,0x2049);   			/* TW: Enable TV path */
#else      
     SiS_SetCH701x(SiS_Pr,0x2049);   			/* TW: Enable TV path */
     temp = SiS_GetCH701x(SiS_Pr,0x49);
     if(SiS_IsYPbPr(SiS_Pr,HwDeviceExtension, BaseAddr)) {
        temp = SiS_GetCH701x(SiS_Pr,0x73);
	temp |= 0x60;
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x73);
     }
     temp = SiS_GetCH701x(SiS_Pr,0x47);
     temp &= 0x7f;
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);
     SiS_LongDelay(SiS_Pr,2);
     temp = SiS_GetCH701x(SiS_Pr,0x47);
     temp |= 0x80;
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);
#endif     
  }
}

void
SiS_Chrontel701xOff(SiS_Private *SiS_Pr)
{
  USHORT temp;

  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
        SiS_LongDelay(SiS_Pr,2);
	/* TW: Complete power down of LVDS */
	temp = SiS_GetCH701x(SiS_Pr,0x76);
	temp &= 0xfc;
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x76);
	SiS_SetCH701x(SiS_Pr,0x0066);
  }
}

#ifdef NEWCH701x
void
SiS_ChrontelDoSomething5(SiS_Private *SiS_Pr)
{
     unsigned char temp, temp1;
     
     temp1 = SiS_GetCH701x(SiS_Pr,0x49);
     SiS_SetCH701x(SiS_Pr,0x3e49);
     temp = SiS_GetCH701x(SiS_Pr,0x47);
     temp &= 0x7f;
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);
     SiS_LongDelay(SiS_Pr,3);
     temp = SiS_GetCH701x(SiS_Pr,0x47);
     temp |= 0x80;
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);
     SiS_SetCH701x(SiS_Pr,(temp1 << 8) | 0x49);
}
#endif

void
SiS_ChrontelResetDB(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef NEWCH701x
     USHORT temp;
     
     /* 740/LVDS: */
     temp = SiS_GetCH701x(SiS_Pr,0x4a);
     temp &= 0x01;
     if(!(temp)) {
     
        if(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	   temp = SiS_GetCH701x(SiS_Pr,0x49);
	   SiS_SetCH701x(SiS_Pr,0x3e49);
	}
	/* TW: Reset Chrontel 7019 datapath */
        SiS_SetCH701x(SiS_Pr,0x1048);
        SiS_LongDelay(SiS_Pr,1);
        SiS_SetCH701x(SiS_Pr,0x1848);
	
	if(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	   SiS_ChrontelDoSomething5(SiS_Pr);
	   SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x49);
	}    
     } else {
     
        temp = SiS_GetCH701x(SiS_Pr,0x5c);
	temp &= 0xef;
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x5c);
	temp = SiS_GetCH701x(SiS_Pr,0x5c);
	temp |= 0x10;
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x5c);
	temp = SiS_GetCH701x(SiS_Pr,0x5c);
	temp &= 0xef;
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x5c);
	temp = SiS_GetCH701x(SiS_Pr,0x61);
	if(!temp) {
	   SiS_SetCH701xForLCD(SiS_Pr,HwDeviceExtension,BaseAddr);
	}
     }
#else /* pre 740/LVDS code */     
     /* TW: Reset Chrontel 7019 datapath */
     SiS_SetCH701x(SiS_Pr,0x1048);
     SiS_LongDelay(SiS_Pr,1);
     SiS_SetCH701x(SiS_Pr,0x1848);
#endif     
}

void
SiS_ChrontelDoSomething4(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
#ifdef NEWCH701x
     if(!(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr))) {
        SiS_ChrontelDoSomething5(SiS_Pr);
     }
#else
     USHORT temp;

     SiS_SetCH701x(SiS_Pr,0xaf76);  /* Power up LVDS block */
     temp = SiS_GetCH701x(SiS_Pr,0x49);
     temp &= 1;
     if(temp != 1) {  /* TV block powered? (0 = yes, 1 = no) */
	temp = SiS_GetCH701x(SiS_Pr,0x47);
	temp &= 0x70;
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);  /* enable VSYNC */
	SiS_LongDelay(SiS_Pr,3);
	temp = SiS_GetCH701x(SiS_Pr,0x47);
	temp |= 0x80;
	SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x47);  /* disable VSYNC */
     }
#endif     
}

void
SiS_ChrontelDoSomething3(SiS_Private *SiS_Pr, USHORT ModeNo, PSIS_HW_DEVICE_INFO HwDeviceExtension,
                         USHORT BaseAddr)
{
#ifdef NEWCH701x
     USHORT temp;
     
     temp = SiS_GetCH701x(SiS_Pr,0x61);
     if(temp < 1) {
          temp++;
	  SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x61);
     }
     SiS_SetCH701x(SiS_Pr,0x4566);
     SiS_SetCH701x(SiS_Pr,0xaf76);
     SiS_LongDelay(SiS_Pr,1);
     SiS_GenericDelay(SiS_Pr,0x16ff);

#else
     USHORT temp,temp1;
     
     temp1 = 0;
     temp = SiS_GetCH701x(SiS_Pr,0x61);
     if(temp < 2) {
          temp++;
	  SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x61);
	  temp1 = 1;
     }
     SiS_SetCH701x(SiS_Pr,0xac76);
     temp = SiS_GetCH701x(SiS_Pr,0x66);
     temp |= 0x5f;
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x66);
     if(ModeNo > 0x13) {
         if(SiS_WeHaveBacklightCtrl(SiS_Pr,HwDeviceExtension, BaseAddr)) {
	    SiS_GenericDelay(SiS_Pr,0x3ff);
	 } else {
	    SiS_GenericDelay(SiS_Pr,0x2ff);
	 }
     } else {
         if(!temp1)
	    SiS_GenericDelay(SiS_Pr,0x2ff);
     }
     temp = SiS_GetCH701x(SiS_Pr,0x76);
     temp |= 0x03;
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x76);
     temp = SiS_GetCH701x(SiS_Pr,0x66);
     temp &= 0x7f;
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x66);
     SiS_LongDelay(SiS_Pr,1);
#endif     
}

void
SiS_ChrontelDoSomething2(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr)
{
     USHORT temp,tempcl,tempch;

     SiS_LongDelay(SiS_Pr, 1);
     tempcl = 3;
     tempch = 0;

     do {
       temp = SiS_GetCH701x(SiS_Pr,0x66);
       temp &= 0x04;
       if(temp == 0x04) break;
       
#ifdef NEWCH701x
       SiS_SetCH701x(SiS_Pr,0xac76);    /* 740/LVDS */
#endif       

       SiS_SetCH701xForLCD(SiS_Pr,HwDeviceExtension,BaseAddr);

       if(tempcl == 0) {
           if(tempch == 3) break;
	   SiS_ChrontelResetDB(SiS_Pr,HwDeviceExtension,BaseAddr);
	   tempcl = 3;
	   tempch++;
       }
       tempcl--;
       temp = SiS_GetCH701x(SiS_Pr,0x76);
       temp &= 0xfb;
       SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x76);
       SiS_LongDelay(SiS_Pr,2);
       temp = SiS_GetCH701x(SiS_Pr,0x76);
       temp |= 0x04;
       SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x76);
#ifdef NEWCH701x
       SiS_SetCH701x(SiS_Pr,0xe078);
#else       
       SiS_SetCH701x(SiS_Pr,0x6078);
#endif       
       SiS_LongDelay(SiS_Pr,2);
    } while(0);

    SiS_SetCH701x(SiS_Pr,0x0077);
}

void
SiS_ChrontelDoSomething1(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
                         USHORT BaseAddr)
{
     USHORT temp;

     temp = SiS_GetCH701x(SiS_Pr,0x03);
     temp |= 0x80;	/* Set datapath 1 to TV   */
     temp &= 0xbf;	/* Set datapath 2 to LVDS */
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x03);
     
#ifdef NEWCH701x   /* 740/LVDS: */

     temp = SiS_GetCH701x(SiS_Pr,0x1c);
     temp &= 0xfb;
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x1c);
     
     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x2d,0x03);
     
     temp = SiS_GetCH701x(SiS_Pr,0x64);
     temp |= 0x40;
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x64);
     
     temp = SiS_GetCH701x(SiS_Pr,0x03);
     temp &= 0x3f;	
     SiS_SetCH701x(SiS_Pr,(temp << 8) | 0x03);
     
     temp = SiS_GetCH701x(SiS_Pr,0x66);
     if(temp != 0x45) {
        SiS_ChrontelResetDB(SiS_Pr,HwDeviceExtension,BaseAddr);
        SiS_ChrontelDoSomething2(SiS_Pr,HwDeviceExtension,BaseAddr);
	temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x34);
        SiS_ChrontelDoSomething3(SiS_Pr,temp,HwDeviceExtension,BaseAddr);
     }     

#else  /* pre-740/LVDS: */     

     SiS_ChrontelResetDB(SiS_Pr);

     SiS_ChrontelDoSomething2(SiS_Pr,HwDeviceExtension,BaseAddr);

     temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x34);
     SiS_ChrontelDoSomething3(SiS_Pr,temp,HwDeviceExtension,BaseAddr);

     SiS_SetCH701x(SiS_Pr,0xaf76);
     
#endif  /* End of pre-740/LVDS */
}

#endif  /* 310/325 series --------------------------------- */

/* TW: End of Chrontel 701x functions ==================================== */

/* TW: Generic Read/write routines for Chrontel ========================== */

/* TW: The Chrontel is connected to the 630/730 via
 * the 630/730's DDC/I2C port.
 *
 * On 630(S)T chipset, the index changed from 0x11 to 0x0a,
 * possibly for working around the DDC problems
 */

void
SiS_SetCH70xx(SiS_Private *SiS_Pr, USHORT tempbx)
{
   if (SiS_Pr->SiS_IF_DEF_CH70xx == 1)
      SiS_SetCH700x(SiS_Pr,tempbx);
   else
      SiS_SetCH701x(SiS_Pr,tempbx);
}

/* TW: Write to Chrontel 700x */
/* Parameter is [Data (S15-S8) | Register no (S7-S0)] */
void
SiS_SetCH700x(SiS_Private *SiS_Pr, USHORT tempbx)
{
  USHORT tempah,temp,i;

  if(!(SiS_Pr->SiS_ChrontelInit)) {
     SiS_Pr->SiS_DDC_Index = 0x11;		   /* TW: Bit 0 = SC;  Bit 1 = SD */
     SiS_Pr->SiS_DDC_Data  = 0x02;                 /* Bitmask in IndexReg for Data */
     SiS_Pr->SiS_DDC_Clk   = 0x01;                 /* Bitmask in IndexReg for Clk */
     SiS_Pr->SiS_DDC_DataShift = 0x00;
     SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;  	   /* TW: DAB (Device Address Byte) */
  }

  for(i=0;i<10;i++) {	/* TW: Do only 10 attempts to write */
    /* SiS_SetSwitchDDC2(SiS_Pr); */
    if(SiS_SetStart(SiS_Pr)) continue;		/* TW: Set start condition */
    tempah = SiS_Pr->SiS_DDC_DeviceAddr;
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* TW: Write DAB (S0=0=write) */
    if(temp) continue;				/* TW:    (ERROR: no ack) */
    tempah = tempbx & 0x00FF;			/* TW: Write RAB */
    tempah |= 0x80;                             /* TW: (set bit 7, see datasheet) */
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);
    if(temp) continue;				/* TW:    (ERROR: no ack) */
    tempah = (tempbx & 0xFF00) >> 8;
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* TW: Write data */
    if(temp) continue;				/* TW:    (ERROR: no ack) */
    if(SiS_SetStop(SiS_Pr)) continue;		/* TW: Set stop condition */
    SiS_Pr->SiS_ChrontelInit = 1;
    return;
  }

  /* TW: For 630ST */
  if(!(SiS_Pr->SiS_ChrontelInit)) {
     SiS_Pr->SiS_DDC_Index = 0x0a;		/* TW: Bit 7 = SC;  Bit 6 = SD */
     SiS_Pr->SiS_DDC_Data  = 0x80;              /* Bitmask in IndexReg for Data */
     SiS_Pr->SiS_DDC_Clk   = 0x40;              /* Bitmask in IndexReg for Clk */
     SiS_Pr->SiS_DDC_DataShift = 0x00;
     SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;  	/* TW: DAB (Device Address Byte) */

     for(i=0;i<10;i++) {	/* TW: Do only 10 attempts to write */
       /* SiS_SetSwitchDDC2(SiS_Pr); */
       if (SiS_SetStart(SiS_Pr)) continue;	/* TW: Set start condition */
       tempah = SiS_Pr->SiS_DDC_DeviceAddr;
       temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* TW: Write DAB (S0=0=write) */
       if(temp) continue;			/* TW:    (ERROR: no ack) */
       tempah = tempbx & 0x00FF;		/* TW: Write RAB */
       tempah |= 0x80;                          /* TW: (set bit 7, see datasheet) */
       temp = SiS_WriteDDC2Data(SiS_Pr,tempah);
       if(temp) continue;			/* TW:    (ERROR: no ack) */
       tempah = (tempbx & 0xFF00) >> 8;
       temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* TW: Write data */
       if(temp) continue;			/* TW:    (ERROR: no ack) */
       if(SiS_SetStop(SiS_Pr)) continue;	/* TW: Set stop condition */
       SiS_Pr->SiS_ChrontelInit = 1;
       return;
    }
  }
}

/* TW: Write to Chrontel 701x */
/* Parameter is [Data (S15-S8) | Register no (S7-S0)] */
void
SiS_SetCH701x(SiS_Private *SiS_Pr, USHORT tempbx)
{
  USHORT tempah,temp,i;

  SiS_Pr->SiS_DDC_Index = 0x11;			/* TW: Bit 0 = SC;  Bit 1 = SD */
  SiS_Pr->SiS_DDC_Data  = 0x08;                 /* Bitmask in IndexReg for Data */
  SiS_Pr->SiS_DDC_Clk   = 0x04;                 /* Bitmask in IndexReg for Clk */
  SiS_Pr->SiS_DDC_DataShift = 0x00;
  SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;  		/* TW: DAB (Device Address Byte) */

  for(i=0;i<10;i++) {	/* TW: Do only 10 attempts to write */
    if (SiS_SetStart(SiS_Pr)) continue;		/* TW: Set start condition */
    tempah = SiS_Pr->SiS_DDC_DeviceAddr;
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* TW: Write DAB (S0=0=write) */
    if(temp) continue;				/* TW:    (ERROR: no ack) */
    tempah = tempbx & 0x00FF;
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* TW: Write RAB */
    if(temp) continue;				/* TW:    (ERROR: no ack) */
    tempah = (tempbx & 0xFF00) >> 8;
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* TW: Write data */
    if(temp) continue;				/* TW:    (ERROR: no ack) */
    if(SiS_SetStop(SiS_Pr)) continue;		/* TW: Set stop condition */
    return;
  }
}

/* TW: Read from Chrontel 70xx */
/* Parameter is [Register no (S7-S0)] */
USHORT
SiS_GetCH70xx(SiS_Private *SiS_Pr, USHORT tempbx)
{
   if (SiS_Pr->SiS_IF_DEF_CH70xx == 1)
      return(SiS_GetCH700x(SiS_Pr,tempbx));
   else
      return(SiS_GetCH701x(SiS_Pr,tempbx));
}

/* TW: Read from Chrontel 700x */
/* Parameter is [Register no (S7-S0)] */
USHORT
SiS_GetCH700x(SiS_Private *SiS_Pr, USHORT tempbx)
{
  USHORT tempah,temp,i;

  if(!(SiS_Pr->SiS_ChrontelInit)) {
     SiS_Pr->SiS_DDC_Index = 0x11;		/* TW: Bit 0 = SC;  Bit 1 = SD */
     SiS_Pr->SiS_DDC_Data  = 0x02;              /* Bitmask in IndexReg for Data */
     SiS_Pr->SiS_DDC_Clk   = 0x01;              /* Bitmask in IndexReg for Clk */
     SiS_Pr->SiS_DDC_DataShift = 0x00;
     SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;		/* TW: DAB */
  }

  SiS_Pr->SiS_DDC_ReadAddr = tempbx;

  for(i=0;i<20;i++) {	/* TW: Do only 20 attempts to read */
    /* SiS_SetSwitchDDC2(SiS_Pr); */
    if(SiS_SetStart(SiS_Pr)) continue;		/* TW: Set start condition */
    tempah = SiS_Pr->SiS_DDC_DeviceAddr;
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* TW: Write DAB (S0=0=write) */
    if(temp) continue;				/* TW:        (ERROR: no ack) */
    tempah = SiS_Pr->SiS_DDC_ReadAddr | 0x80;	/* TW: Write RAB | 0x80 */
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);
    if(temp) continue;				/* TW:        (ERROR: no ack) */
    if (SiS_SetStart(SiS_Pr)) continue;		/* TW: Re-start */
    tempah = SiS_Pr->SiS_DDC_DeviceAddr | 0x01; /* DAB | 0x01 = Read */
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* TW: DAB (S0=1=read) */
    if(temp) continue;				/* TW:        (ERROR: no ack) */
    tempah = SiS_ReadDDC2Data(SiS_Pr,tempah);	/* TW: Read byte */
    if (SiS_SetStop(SiS_Pr)) continue;		/* TW: Stop condition */
    SiS_Pr->SiS_ChrontelInit = 1;
    return(tempah);
  }

  /* TW: For 630ST */
  if(!SiS_Pr->SiS_ChrontelInit) {
     SiS_Pr->SiS_DDC_Index = 0x0a;		/* TW: Bit 0 = SC;  Bit 1 = SD */
     SiS_Pr->SiS_DDC_Data  = 0x80;              /* Bitmask in IndexReg for Data */
     SiS_Pr->SiS_DDC_Clk   = 0x40;              /* Bitmask in IndexReg for Clk */
     SiS_Pr->SiS_DDC_DataShift = 0x00;
     SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;  	/* TW: DAB (Device Address Byte) */

     for(i=0;i<20;i++) {	/* TW: Do only 20 attempts to read */
       /* SiS_SetSwitchDDC2(SiS_Pr); */
       if(SiS_SetStart(SiS_Pr)) continue;		/* TW: Set start condition */
       tempah = SiS_Pr->SiS_DDC_DeviceAddr;
       temp = SiS_WriteDDC2Data(SiS_Pr,tempah);		/* TW: Write DAB (S0=0=write) */
       if(temp) continue;				/* TW:        (ERROR: no ack) */
       tempah = SiS_Pr->SiS_DDC_ReadAddr | 0x80;	/* TW: Write RAB | 0x80 */
       temp = SiS_WriteDDC2Data(SiS_Pr,tempah);
       if(temp) continue;				/* TW:        (ERROR: no ack) */
       if (SiS_SetStart(SiS_Pr)) continue;		/* TW: Re-start */
       tempah = SiS_Pr->SiS_DDC_DeviceAddr | 0x01; 	/* DAB | 0x01 = Read */
       temp = SiS_WriteDDC2Data(SiS_Pr,tempah);		/* TW: DAB (S0=1=read) */
       if(temp) continue;				/* TW:        (ERROR: no ack) */
       tempah = SiS_ReadDDC2Data(SiS_Pr,tempah);	/* TW: Read byte */
       if (SiS_SetStop(SiS_Pr)) continue;		/* TW: Stop condition */
       SiS_Pr->SiS_ChrontelInit = 1;
       return(tempah);
     }
  }
  return(0xFFFF);
}

/* TW: Read from Chrontel 701x */
/* Parameter is [Register no (S7-S0)] */
USHORT
SiS_GetCH701x(SiS_Private *SiS_Pr, USHORT tempbx)
{
  USHORT tempah,temp,i;

  SiS_Pr->SiS_DDC_Index = 0x11;			/* TW: Bit 0 = SC;  Bit 1 = SD */
  SiS_Pr->SiS_DDC_Data  = 0x08;                 /* Bitmask in IndexReg for Data */
  SiS_Pr->SiS_DDC_Clk   = 0x04;                 /* Bitmask in IndexReg for Clk */
  SiS_Pr->SiS_DDC_DataShift = 0x00;
  SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;		/* TW: DAB */
  SiS_Pr->SiS_DDC_ReadAddr = tempbx;

   for(i=0;i<20;i++) {	/* TW: Do only 20 attempts to read */
    if(SiS_SetStart(SiS_Pr)) continue;		/* TW: Set start condition */
    tempah = SiS_Pr->SiS_DDC_DeviceAddr;
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* TW: Write DAB (S0=0=write) */
    if(temp) continue;				/* TW:        (ERROR: no ack) */
    tempah = SiS_Pr->SiS_DDC_ReadAddr;		/* TW: Write RAB */
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);
    if(temp) continue;				/* TW:        (ERROR: no ack) */
    if (SiS_SetStart(SiS_Pr)) continue;		/* TW: Re-start */
    tempah = SiS_Pr->SiS_DDC_DeviceAddr | 0x01; /* DAB | 0x01 = Read */
    temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* TW: DAB (S0=1=read) */
    if(temp) continue;				/* TW:        (ERROR: no ack) */
    tempah = SiS_ReadDDC2Data(SiS_Pr,tempah);	/* TW: Read byte */
    SiS_SetStop(SiS_Pr);			/* TW: Stop condition */
    return(tempah);
   }
  return 0xFFFF;
}

#ifdef LINUX_XF86
/* TW: Our own DDC functions */
USHORT
SiS_InitDDCRegs(SiS_Private *SiS_Pr, SISPtr pSiS, USHORT adaptnum, USHORT DDCdatatype,
		BOOLEAN checkcr32)
{
     unsigned char ddcdtype[] = { 0xa0, 0xa0, 0xa0, 0xa2, 0xa6 };
     unsigned char flag, cr32;
     USHORT        temp = 0, myadaptnum = adaptnum;

     if(adaptnum != 0) {
        if(!(pSiS->VBFlags & (VB_301|VB_301B|VB_302B))) return 0xFFFF;
	if((pSiS->VBFlags & VB_30xBDH) && (adaptnum == 1)) return 0xFFFF;
     }	
     
     /* adapternum for SiS bridges: 0 = CRT1, 1 = LCD, 2 = VGA2 */
     
     SiS_Pr->SiS_ChrontelInit = 0;   /* force re-detection! */

     SiS_Pr->SiS_DDC_SecAddr = 0;
     SiS_Pr->SiS_DDC_DeviceAddr = ddcdtype[DDCdatatype];
     SiS_Pr->SiS_DDC_Port = SiS_Pr->SiS_P3c4;
     SiS_Pr->SiS_DDC_Index = 0x11;
     flag = 0xff;

     cr32 = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x32);
  
     if(pSiS->VBFlags & VB_SISBRIDGE) {
	if(myadaptnum == 0) {
	   if(!(cr32 & 0x20)) {
	      myadaptnum = 2;
	      if(!(cr32 & 0x10)) {
	         myadaptnum = 1;
		 if(!(cr32 & 0x08)) {
		    myadaptnum = 0;
		 }
	      }
	   }
        }
     }

     if(pSiS->VGAEngine == SIS_300_VGA) {		/* 300 series */
	
        if(myadaptnum != 0) {
	   flag = 0;
	   if(pSiS->VBFlags & VB_SISBRIDGE) {
	      SiS_Pr->SiS_DDC_Port = SiS_Pr->SiS_Part4Port;
              SiS_Pr->SiS_DDC_Index = 0x0f;
	   }
        }

	if(!(pSiS->VBFlags & VB_301)) {
	   if((cr32 & 0x80) && (checkcr32)) {
              if(myadaptnum >= 1) {
	         if(!(cr32 & 0x08)) {
	             myadaptnum = 1;
		     if(!(cr32 & 0x10)) return 0xFFFF;
                 }
	      }
	   }
	}

	temp = 4 - (myadaptnum * 2);
	if(flag) temp = 0;

     } else {						/* 310/325/330 series */

     	/* here we simplify: 0 = CRT1, 1 = CRT2 (VGA, LCD) */
	
	if(pSiS->VBFlags & VB_SISBRIDGE) {
	   if(myadaptnum == 2) {
	      myadaptnum = 1;
           }
	}

        if(myadaptnum == 1) {
     	   flag = 0;
	   if(pSiS->VBFlags & VB_SISBRIDGE) {
	      SiS_Pr->SiS_DDC_Port = SiS_Pr->SiS_Part4Port;
              SiS_Pr->SiS_DDC_Index = 0x0f;
	   }
        }

        if((cr32 & 0x80) && (checkcr32)) {
           if(myadaptnum >= 1) {
	      if(!(cr32 & 0x08)) {
	         myadaptnum = 1;
		 if(!(cr32 & 0x10)) return 0xFFFF;
	      }
	   }
        }

        temp = myadaptnum;
        if(myadaptnum == 1) {
           temp = 0;
	   if(pSiS->VBFlags & VB_LVDS) flag = 0xff;
        }

	if(flag) temp = 0;
    }
    
    SiS_Pr->SiS_DDC_Data = 0x02 << temp;
    SiS_Pr->SiS_DDC_Clk  = 0x01 << temp;

#ifdef TWDEBUG
    xf86DrvMsg(0, X_INFO, "DDC Port %x Index %x Shift %d\n",
    		SiS_Pr->SiS_DDC_Port, SiS_Pr->SiS_DDC_Index, temp);
#endif	 
    
    return 0;
}

USHORT
SiS_WriteDABDDC(SiS_Private *SiS_Pr)
{
   if(SiS_SetStart(SiS_Pr)) return 0xFFFF;
   if(SiS_WriteDDC2Data(SiS_Pr, SiS_Pr->SiS_DDC_DeviceAddr)) {
#ifdef TWDEBUG
        xf86DrvMsg(0, X_INFO, "WriteDAB 1 failed\n");
#endif	 
  	return 0xFFFF;
   }
   if(SiS_WriteDDC2Data(SiS_Pr, SiS_Pr->SiS_DDC_SecAddr)) {
#ifdef TWDEBUG
        xf86DrvMsg(0, X_INFO, "WriteDAB 2 failed\n");
#endif	 
   	return 0xFFFF;
   }
   return(0);
}

USHORT
SiS_PrepareReadDDC(SiS_Private *SiS_Pr)
{
   if(SiS_SetStart(SiS_Pr)) return 0xFFFF;
   if(SiS_WriteDDC2Data(SiS_Pr, (SiS_Pr->SiS_DDC_DeviceAddr | 0x01))) {
#ifdef TWDEBUG
        xf86DrvMsg(0, X_INFO, "PrepareReadDDC 1 failed\n");
#endif	 
   	return 0xFFFF;
   }
   return(0);
}

USHORT
SiS_PrepareDDC(SiS_Private *SiS_Pr)
{
   if(SiS_WriteDABDDC(SiS_Pr)) SiS_WriteDABDDC(SiS_Pr);
   if(SiS_PrepareReadDDC(SiS_Pr)) return(SiS_PrepareReadDDC(SiS_Pr));
   return(0);
}

void
SiS_SendACK(SiS_Private *SiS_Pr, USHORT yesno)
{
   SiS_SetSCLKLow(SiS_Pr);
   if(yesno) {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port, SiS_Pr->SiS_DDC_Index,
                      ~SiS_Pr->SiS_DDC_Data, SiS_Pr->SiS_DDC_Data);
   } else {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port, SiS_Pr->SiS_DDC_Index,
                      ~SiS_Pr->SiS_DDC_Data, 0);
   }
   SiS_SetSCLKHigh(SiS_Pr);
}

USHORT
SiS_DoProbeDDC(SiS_Private *SiS_Pr)
{
    unsigned char mask, value;
    USHORT  temp, ret=0;
    BOOLEAN failed = FALSE;

    SiS_SetSwitchDDC2(SiS_Pr);
    if(SiS_PrepareDDC(SiS_Pr)) {
         SiS_SetStop(SiS_Pr);
#ifdef TWDEBUG
	 xf86DrvMsg(0, X_INFO, "DoProbeDDC 1 failed at PrepareDDC\n");
#endif	 
         return(0xFFFF);
    }
    mask = 0xf0;
    value = 0x20;
    if(SiS_Pr->SiS_DDC_DeviceAddr == 0xa0) {
       temp = (unsigned char)SiS_ReadDDC2Data(SiS_Pr, 0);
       SiS_SendACK(SiS_Pr, 0);
       if(temp == 0) {
           mask = 0xff;
	   value = 0xff;
       } else {
           failed = TRUE;
	   ret = 0xFFFF;
       }
    }
    if(failed == FALSE) {
       temp = (unsigned char)SiS_ReadDDC2Data(SiS_Pr, 0);
       SiS_SendACK(SiS_Pr, 1);
       temp &= mask;
       if(temp == value) ret = 0;
       else {
          ret = 0xFFFF;
          if(SiS_Pr->SiS_DDC_DeviceAddr == 0xa0) {
             if(temp == 0x30) ret = 0;
          }
       }
    }
    SiS_SetStop(SiS_Pr);
    return(ret);
}

USHORT
SiS_ProbeDDC(SiS_Private *SiS_Pr)
{
   USHORT flag;

   flag = 0x180;
   SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;
   if(!(SiS_DoProbeDDC(SiS_Pr))) flag |= 0x02;
   SiS_Pr->SiS_DDC_DeviceAddr = 0xa2;
   if(!(SiS_DoProbeDDC(SiS_Pr))) flag |= 0x08;
   SiS_Pr->SiS_DDC_DeviceAddr = 0xa6;
   if(!(SiS_DoProbeDDC(SiS_Pr))) flag |= 0x10;
   if(!(flag & 0x1a)) flag = 0;
   return(flag);
}

USHORT
SiS_ReadDDC(SiS_Private *SiS_Pr, SISPtr pSiS, USHORT DDCdatatype, unsigned char *buffer)
{
   USHORT flag, length, i;
   unsigned char chksum,gotcha;

   if(DDCdatatype > 4) return 0xFFFF;  

   flag = 0;
   SiS_SetSwitchDDC2(SiS_Pr);
   if(!(SiS_PrepareDDC(SiS_Pr))) {
      length = 127;
      if(DDCdatatype != 1) length = 255;
      chksum = 0;
      gotcha = 0;
      for(i=0; i<length; i++) {
         buffer[i] = (unsigned char)SiS_ReadDDC2Data(SiS_Pr, 0);
	 chksum += buffer[i];
	 gotcha |= buffer[i];
	 SiS_SendACK(SiS_Pr, 0);
      }
      buffer[i] = (unsigned char)SiS_ReadDDC2Data(SiS_Pr, 0);
      chksum += buffer[i];
      SiS_SendACK(SiS_Pr, 1);
      if(gotcha) flag = (USHORT)chksum;
      else flag = 0xFFFF;
   } else {
      flag = 0xFFFF;
   }
   SiS_SetStop(SiS_Pr);
   return(flag);
}

USHORT
SiS_ReadLCDDDC(SiS_Private *SiS_Pr, USHORT length, unsigned char *buffer)
{
   USHORT i=0, flag=0;

   length--;
   
   SiS_SetSwitchDDC2(SiS_Pr);
   if(!(SiS_PrepareDDC(SiS_Pr))) {
      for(i=0; i<length; i++) {
         buffer[i] = (unsigned char)SiS_ReadDDC2Data(SiS_Pr, 0);
         SiS_SendACK(SiS_Pr, 0);
      }
      buffer[i] = (unsigned char)SiS_ReadDDC2Data(SiS_Pr, 0);
      SiS_SendACK(SiS_Pr, 1);
   } else flag = 0xFFFF;
   
   SiS_SetStop(SiS_Pr);
   return(0);
}

/* TW: Our private DDC function

   It complies somewhat with the corresponding VESA function
   in arguments and return values.

   Since this is probably called before the mode is changed,
   we use our pre-detected pSiS-values instead of SiS_Pr as
   regards chipset and video bridge type.

   Arguments:
       adaptnum: 0=CRT1, 1=LCD, 2=VGA2
                 CRT2 DDC is only supported on SiS301, 301B (non-DH version), 302B.
       DDCdatatype: 0=Probe, 1=EDID, 2=EDID+VDIF, 3=EDID V2 (P&D), 4=EDID V2 (FPDI-2)
       buffer: ptr to 256 data bytes which will be filled with read data.

   Returns 0xFFFF if error, otherwise
       if DDCdatatype > 0:  Returns 0 if reading OK (included a correct checksum)
       if DDCdatatype = 0:  Returns supported DDC modes

 */
USHORT
SiS_HandleDDC(SiS_Private *SiS_Pr, SISPtr pSiS, USHORT adaptnum,
              USHORT DDCdatatype, unsigned char *buffer)
{
   if(adaptnum > 2) return 0xFFFF;
   if(DDCdatatype > 4) return 0xFFFF;
   if((!(pSiS->VBFlags & VB_VIDEOBRIDGE)) && (adaptnum > 0)) return 0xFFFF;
   if(SiS_InitDDCRegs(SiS_Pr, pSiS, adaptnum, DDCdatatype, TRUE) == 0xFFFF) return 0xFFFF;
   if(DDCdatatype == 0) {
       return(SiS_ProbeDDC(SiS_Pr));
   } else {
       return(SiS_ReadDDC(SiS_Pr, pSiS, DDCdatatype, buffer));
   }
}

/* Sense the LCD parameters (CR36, CR37) via DDC */
/* SiS30x(B) only */
USHORT
SiS_SenseLCDDDC(SiS_Private *SiS_Pr, SISPtr pSiS)
{
   USHORT DDCdatatype, paneltype, flag, xres, yres;
   USHORT index, myindex, lumsize, numcodes;
   unsigned char cr37=0, seekcode;
   BOOLEAN checkexpand = FALSE;
   int retry, i;
   unsigned char buffer[256];
   
   if(!(pSiS->VBFlags & (VB_301|VB_301B|VB_302B))) return 0;
   if(pSiS->VBFlags & VB_30xBDH) return 0;
  
   if(SiS_InitDDCRegs(SiS_Pr, pSiS, 1, 0, FALSE) == 0xFFFF) return 0;
   
   SiS_Pr->SiS_DDC_SecAddr = 0x00;
   
   /* Probe supported DA's */
   flag = SiS_ProbeDDC(SiS_Pr);
#ifdef TWDEBUG   
   xf86DrvMsg(pSiS->pScrn->scrnIndex, X_INFO, 
   	"CRT2 DDC capabilities 0x%x\n", flag);
#endif	
   if(flag & 0x10) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa6;	/* EDID V2 (FP) */
      DDCdatatype = 4;
   } else if(flag & 0x08) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa2;	/* EDID V2 (P&D-D Monitor) */
      DDCdatatype = 3;
   } else if(flag & 0x02) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;	/* EDID V1 */
      DDCdatatype = 1;
   } else return 0;				/* no DDC support (or no device attached) */
   
   /* Read the entire EDID */
   retry = 2;
   do {
      if(SiS_ReadDDC(SiS_Pr, pSiS, DDCdatatype, buffer)) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_INFO, 
	 	"CRT2: DDC read failed (attempt %d), %s\n", 
		(3-retry), (retry == 1) ? "giving up" : "retrying");
	 retry--;
	 if(retry == 0) return 0xFFFF;
      } else break;
   } while(1);
   
#ifdef TWDEBUG   
   for(i=0; i<256; i+=16) {
       xf86DrvMsg(pSiS->pScrn->scrnIndex, X_INFO,
       	"%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
	buffer[i],    buffer[i+1], buffer[i+2], buffer[i+3],
	buffer[i+4],  buffer[i+5], buffer[i+6], buffer[i+7],
	buffer[i+8],  buffer[i+9], buffer[i+10], buffer[i+11],
	buffer[i+12], buffer[i+13], buffer[i+14], buffer[i+15]);
   }
#endif   
   
   /* Analyze EDID and retrieve LCD panel information */
   paneltype = 0;
   switch(DDCdatatype) {
   case 1:							/* Analyze EDID V1 */
      /* Catch a few clear cases: */
      if(!(buffer[0x14] & 0x80)) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED, 
	        "CRT2: Attached display expects analog input (0x%02x)\n",
		buffer[0x14]);
      	 return 0;
      }
      
      if((buffer[0x18] & 0x18) != 0x08) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED, 
	 	"CRT2: Attached display is not of RGB but of %s type (0x%02x)\n", 
		((buffer[0x18] & 0x18) == 0x00) ? "monochrome/greyscale" :
		  ( ((buffer[0x18] & 0x18) == 0x10) ? "non-RGB multicolor" : 
		     "undefined"),
		buffer[0x18]);
	 return 0;
      }
      
      /* Now analyze the first Detailed Timing Block and hope
       * that the preferred timing mode is stored there.
       */	
      xres = buffer[0x38] | ((buffer[0x3a] & 0xf0) << 4);
      yres = buffer[0x3b] | ((buffer[0x3d] & 0xf0) << 4);
      checkexpand = FALSE;
      switch(xres) {
         case 800:
	     if(yres == 600) {
	     	paneltype = Panel310_800x600;
	     	checkexpand = TRUE;
	     }
	     break;
         case 1024:
	     if(yres == 768) {
	     	paneltype = Panel310_1024x768;
	     	checkexpand = FALSE;    /* expand causes error at 640x480, should otherwise be TRUE; */
	     }
	     break;
	 case 1280:
	     if(yres == 960) {
	        if(pSiS->VGAEngine == SIS_300_VGA) {
		   paneltype = Panel300_1280x960;
		} else {
		   paneltype = Panel310_1280x960; 
		}
	     } else if(yres == 1024) {
	     	paneltype = Panel310_1280x1024;  
		checkexpand = TRUE;
	     } else if(pSiS->VGAEngine == SIS_315_VGA) {
	        if(yres == 768) {
	       	   paneltype = Panel310_1280x768; 	/* Panel size 1280x768 not supported yet */
		   checkexpand = TRUE;	
	        }	
	     }
	     break;
	 case 1400:
	     if(pSiS->VGAEngine == SIS_315_VGA) {
	        if(yres == 1050) {
	           paneltype = Panel310_1400x1050;
		   checkexpand = TRUE; 
	        } 
	     }
      	     break;
	 case 1600:
	     if(pSiS->VGAEngine == SIS_315_VGA) {
	        if(yres == 1200) {
	           paneltype = Panel310_1600x1200;
		   checkexpand = TRUE;
	        } 
	     }
      	     break;
      }

      if(buffer[0x18] & 0x02) {
         /* If the preferred timing mode is stored in the first
	  * detailed timing block, we now can extract the sync
	  * polarisation information as well. This only works
	  * if the Flags indicate a digital separate output.
	  */
	  if((buffer[0x47] & 0x18) == 0x18) {
	     cr37 |= ((((buffer[0x47] & 0x06) ^ 0x06) << 5) | 0x20);
	  } else {
	     /* What now? There is no digital separate output timing... */
	     xf86DrvMsg(pSiS->pScrn->scrnIndex, X_WARNING, 
	     	"CRT2: Unable to retrieve Sync polarity information\n");
	  }
	  
      } else {
         /* If the preferred timing mode is *not* stored in the first
	  * detailed timing block, we need to guess the resolution
	  * from the supported Established Timings and assume the
	  * default sync polarity
	  */
	 paneltype = 0;
	 if(buffer[0x24] & 0x01) { 	
	 	paneltype = Panel310_1280x1024;
		checkexpand = TRUE;
		cr37 |= 0x20;
	 } else if(buffer[0x24] & 0x0e) {
	 	paneltype = Panel310_1024x768;
		cr37 |= 0xe0;
		checkexpand = FALSE;		/* Bug at 640x480 */
	 } else if(buffer[0x23] & 0x01) {
	 	paneltype = Panel310_800x600;
		cr37 |= 0xe0;
		checkexpand = TRUE;
         }
      }
      
      if(checkexpand) {
         /* If any of the Established low-res modes is supported, the 
	  * panel can scale automatically. For 800x600 panels, we only 
	  * check the even lower ones.
	  */
	 if(paneltype == Panel310_800x600) {
	    if(buffer[0x23] & 0xfc) cr37 |= 0x10;
	 } else {
            if(buffer[0x23])	    cr37 |= 0x10;
	 }
      }
       
      break;
      
   case 3:							/* Analyze EDID V2 */
   case 4:
      index = 0;
      if((buffer[0x41] & 0x0f) == 0x03) {
         index = 0x42 + 3;
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	 	"CRT2: Display supports TMDS input on primary interface\n");
      } else if((buffer[0x41] & 0xf0) == 0x30) {
         index = 0x46 + 3;
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	 	"CRT2: Display supports TMDS input on secondary interface\n");
      } else {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	 	"CRT2: Display does not support TMDS video interface (0x%02x)\n", 
		buffer[0x41]);
	 return 0;
      }
   
      xres = buffer[0x76] | (buffer[0x77] << 8);
      yres = buffer[0x78] | (buffer[0x79] << 8);
      switch(xres) {
         case 800:
	     if(yres == 600) {
	     	paneltype = Panel310_800x600;
	     	checkexpand = TRUE;
	     }
	     break;
         case 1024:
	     if(yres == 768) {
	     	paneltype = Panel310_1024x768;
	     	checkexpand = FALSE;			/* Bug at 640x480; we do the scaling ourselves */
	     }
	     break;
	 case 1280:
	     if(yres == 960) {
	        if(pSiS->VGAEngine == SIS_315_VGA) {
	     	   paneltype = Panel310_1280x960;
		} else {
		   paneltype = Panel300_1280x960;
		}
	     } else if(yres == 1024) {
	     	paneltype = Panel310_1280x1024;  
		checkexpand = TRUE;
	     } else if(pSiS->VGAEngine == SIS_315_VGA) {
	        if(yres == 768) {
	       	   paneltype = Panel310_1280x768; 	/* Panel size 1280x768 not supported yet */
		   checkexpand = TRUE;
	        }
	     } 
	     break;
	 case 1400:
	     if(pSiS->VGAEngine == SIS_315_VGA) {
	        if(yres == 1050) {
	           paneltype = Panel310_1400x1050;
		   checkexpand = TRUE;
	        } 
	     }
      	     break;
	 case 1600:
	     if(pSiS->VGAEngine == SIS_315_VGA) {
	        if(yres == 1200) {
	           paneltype = Panel310_1600x1200;
		   checkexpand = TRUE;
	        } 
	     }
      	     break;
      }
                 
      /* Determine if RGB18 or RGB24 */
      if(index) {
         if((buffer[index] == 0x20) || (buffer[index] == 0x34)) {
	    cr37 |= 0x01;
	 }
      }
      
      if(checkexpand) {
         /* TODO - for now, we let the panel scale */
	 cr37 |= 0x10;
      }
     
      /* Now seek 4-Byte Timing codes and extract sync pol info */
      index = 0x80;
      if(buffer[0x7e] & 0x20) {			    /* skip Luminance Table (if provided) */
         lumsize = buffer[0x80] & 0x1f;
	 if(buffer[0x80] & 0x80) lumsize *= 3;
	 lumsize++;
	 index += lumsize;
      }
      index += (((buffer[0x7e] & 0x1c) >> 2) * 8);   /* skip Frequency Ranges */
      index += ((buffer[0x7e] & 0x03) * 27);         /* skip Detailed Range Limits */
      numcodes = (buffer[0x7f] & 0xf8) >> 3;
      if(numcodes) {
         myindex = index;
 	 seekcode = (xres - 256) / 16;
     	 for(i=0; i<numcodes; i++) {
	    if(buffer[myindex] == seekcode) break;
	    myindex += 4;
	 }
	 if(buffer[myindex] == seekcode) {
	    cr37 |= ((((buffer[myindex + 1] & 0x0c) ^ 0x0c) << 4) | 0x20);
	 } else {
	    xf86DrvMsg(pSiS->pScrn->scrnIndex, X_WARNING, 
	    	"CRT2: Unable to retrieve Sync polarity information\n");    
	 }
      } else {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_WARNING, 
	    	"CRT2: Unable to retrieve Sync polarity information\n");
      }

      break;
     
   }
   
   /* 1280x960 panels are always RGB24, unable to scale and use
    * high active sync polarity
    */
   if(pSiS->VGAEngine == SIS_315_VGA) {
      if(paneltype == Panel310_1280x960) cr37 &= 0x0e; 
   } else {
      if(paneltype == Panel300_1280x960) cr37 &= 0x0e; 
   }
   
   if(paneltype) {
       cr37 &= 0xf1;
       cr37 |= 0x02;    /* SiS301 */
       SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x36,0xf0,paneltype);
       SiS_SetReg1(SiS_Pr->SiS_P3d4,0x37,cr37);
       SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x32,0x08);
#ifdef TWDEBUG       
       xf86DrvMsgVerb(pSiS->pScrn->scrnIndex, X_PROBED, 3, 
       	"CRT2: [DDC LCD results: 0x%02x, 0x%02x]\n", paneltype, cr37);
#endif	
   }
   return 0;
}
   
USHORT
SiS_SenseVGA2DDC(SiS_Private *SiS_Pr, SISPtr pSiS)
{
   USHORT DDCdatatype,flag;
   BOOLEAN foundcrt = FALSE;
   int retry;
   unsigned char buffer[256];

   if(!(pSiS->VBFlags & (VB_301|VB_301B|VB_302B))) return 0;
/* if(pSiS->VBFlags & VB_30xBDH) return 0;  */
   
   if(SiS_InitDDCRegs(SiS_Pr, pSiS, 2, 0, FALSE) == 0xFFFF) return 0;
   
   SiS_Pr->SiS_DDC_SecAddr = 0x00;
   
   /* Probe supported DA's */
   flag = SiS_ProbeDDC(SiS_Pr);
   if(flag & 0x10) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa6;	/* EDID V2 (FP) */
      DDCdatatype = 4;
   } else if(flag & 0x08) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa2;	/* EDID V2 (P&D-D Monitor) */
      DDCdatatype = 3;
   } else if(flag & 0x02) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;	/* EDID V1 */
      DDCdatatype = 1;
   } else {
   	xf86DrvMsg(pSiS->pScrn->scrnIndex, X_INFO, 
		"Do DDC answer\n");
   	return 0;				/* no DDC support (or no device attached) */
   }
   
   /* Read the entire EDID */
   retry = 2;
   do {
      if(SiS_ReadDDC(SiS_Pr, pSiS, DDCdatatype, buffer)) {
         xf86DrvMsg(pSiS->pScrn->scrnIndex, X_INFO, 
	 	"CRT2: DDC read failed (attempt %d), %s\n", 
		(3-retry), (retry == 1) ? "giving up" : "retrying");
	 retry--;
	 if(retry == 0) return 0xFFFF;
      } else break;
   } while(1);
   
   /* Analyze EDID. We don't have many chances to 
    * distinguish a flat panel from a CRT...
    */
   switch(DDCdatatype) {
   case 1:
      if(buffer[0x14] & 0x80) {			/* Display uses digital input */
          xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	  	"CRT2: Attached display expects digital input\n");
      	  return 0;  	
      }
      foundcrt = TRUE;
      break;
   case 3:
   case 4:
      if( ((buffer[0x41] & 0x0f) != 0x01) &&  	/* Display does not support analog input */
          ((buffer[0x41] & 0x0f) != 0x02) &&
	  ((buffer[0x41] & 0xf0) != 0x10) &&
	  ((buffer[0x41] & 0xf0) != 0x20) ) {
	  xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	     	"CRT2: Attached display does not support analog input (0x%02x)\n",
		buffer[0x41]);
	  return 0;
      }
      foundcrt = TRUE;
      break;	  
   }
   
   if(foundcrt) {
       SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x32,0x10);
   }
   return(0);
}

#if 0
   /* ----- */
USHORT
SiS_SenseLCDDDC(SiS_Private *SiS_Pr, SISPtr pSiS)
{
   USHORT DDCdatatype, paneltype, flag;
   unsigned char cr36=0, cr37=0;
   unsigned char tempal, tempah, tempbl, tempbh;
   USHORT tempax, tempbx, tempcx, push1, push2, push3;
   unsigned char addresstable[] = { 0x00, 0x22, 0x30, 0x40 };
   int i;
   unsigned char buffer[256];
   
   if(pSiS->VGAEngine != SIS_315_VGA) return 0xFFFF;
   if(!(pSiS->VBFlags & (VB_301B|VB_302B))) return 0xFFFF;
   
   if(SiS_InitDDCRegs(SiS_Pr, pSiS, 1, 0, FALSE) == 0xFFFF) return 0xFFFF;   
   
   flag = SiS_ProbeDDC(SiS_Pr);
   if(flag & 0x02) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;	/* EDID V1 */
      DDCdatatype = 1;
      SiS_Pr->SiS_DDC_SecAddr = 0x3a;
   } else if(flag & 0x08) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa2;	/* EDID V2 (P&D-D Monitor) */
      DDCdatatype = 3;
      SiS_Pr->SiS_DDC_SecAddr = 0x76;
   } else if(flag & 0x10) {
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa6;	/* EDID V2 (FP) */
      DDCdatatype = 4;
      SiS_Pr->SiS_DDC_SecAddr = 0x76;
   } else return 0xFFFF;
   
   
   SiS_ReadLCDDDC(SiS_Pr, 4, buffer);
   tempbl = buffer[0];  /* 3a - 76 */
   tempbh = buffer[1];  /* 3b - 77 */
   
   if(SiS_Pr->SiS_DDC_DeviceAddr == 0xa0) {
   
      /* Read and analyze EDID V1 (res) */
   
      tempah = 0x02;				/* 1024x768 by default */
      tempbl &= 0xf0;
      if(tempbl != 0x40) {			
         tempah = 0x03;				/* 1280x1024 by default */
	 if(tempbl == 0x50) {
	    if(!tempbh) {
	       tempbh = buffer[3] & 0xf0;
	       if(tempbh == 0x30) {
	           SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;
      		   SiS_Pr->SiS_DDC_SecAddr = 0x23;
		   SiS_ReadLCDDDC(SiS_Pr, 2, buffer);
		   tempbl = buffer[0];  /* 0x23 */
		   tempbh = buffer[1];  /* 0x24 */
		   if(tempbl) cr37 |= 0x10;
		   tempah = 0x0a;		/* 1280x768 */
	       }
	       if(tempbh == 0x40) {
	           SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;
      		   SiS_Pr->SiS_DDC_SecAddr = 0x23;
		   SiS_ReadLCDDDC(SiS_Pr, 2, buffer);
		   tempbl = buffer[0];  /* 0x23 */
		   tempbh = buffer[1];  /* 0x24 */
		   if(tempbl) cr37 |= 0x10;
		   tempah = 0x03;		/* 1280x1024 */
	       }
	       tempbh = 0x00;
	    }
	 }
	 if(tempbh == 0x00) goto cr36ready;
	 tempah = 0x07;				/* 1280x960 */
	 if(tempbh == 0xc0) goto cr36ready;
      }
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;
      SiS_Pr->SiS_DDC_SecAddr = 0x18;		/* feature support */
      SiS_ReadLCDDDC(SiS_Pr, 2, buffer);
      tempbl = buffer[0];
      tempbh = buffer[1];
      if(tempbl & 0x02) goto cr36ready;
      SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;
      SiS_Pr->SiS_DDC_SecAddr = 0x23;		/* Established Timings */
      SiS_ReadLCDDDC(SiS_Pr, 2, buffer);
      tempbl = buffer[0];
      tempbh = buffer[1];
      tempah = 0x03;
      if(!(tempbh & 0x01)) tempah = 0x02;
      if(!tempbl) cr37 |= 0x10;
      
  } else {
  
      /* Read and analyze EDID V2 (res) */
      
      tempah = 0x02;
      tempbx = tempbl | (tempbh << 8);
      if(tempbx != 1024) tempah = 0x03;
      
  }     

cr36ready:
  cr36 = tempah;      
  
  if(SiS_Pr->SiS_DDC_DeviceAddr == 0xa0) {
  
     /* Read and analyze EDID V1 (pol) */
  
     SiS_Pr->SiS_DDC_SecAddr = 0x47;
     SiS_ReadLCDDDC(SiS_Pr, 2, buffer);
     tempah = buffer[0];
     tempah &= 0x06;
     tempah ^= 0x06;
     tempah <<= 5;
     tempah |= 0x20;
     cr37 &= 0x1f;
     cr37 |= tempah;
     if((cr36 & 0x07) == 0x07) cr37 &= 0x0e;
     
  } else {
  
     /* Read and analyze EDID V2 (depth, pol) */
  
     push1 = tempah;
     SiS_Pr->SiS_DDC_SecAddr = 0x45;
     SiS_ReadLCDDDC(SiS_Pr, 2, buffer);
     tempah = 0x01;
     if((buffer[0] != 0x20) && (buffer[0] != 0x34)) {   /* RGB18 or 24? */
        tempah = 0x00;
     }
     cr37 &= 0xfe;
     cr37 |= tempah;
     
     SiS_Pr->SiS_DDC_SecAddr = 0x7e;
     SiS_ReadLCDDDC(SiS_Pr, 2, buffer);
     tempax = (USHORT)(buffer[0] | (buffer[1] << 8));
     push2 = tempax;
     tempax &= 0x0003;
     tempax *= 0x1b;
     push3 = tempax;
     tempax = (USHORT)buffer[0];
     tempax &= 0x001c;
     tempax >>= 2;
     tempax *= 8;
     tempbx = push3;
     tempbx += tempax;
     if(buffer[0] & 0x20) {		/* Luminance table provided? */
        SiS_Pr->SiS_DDC_SecAddr = 0x80;
	SiS_ReadLCDDDC(SiS_Pr, 2, buffer);
	tempax = buffer[0] | (buffer[1] << 8);
	tempax &= 0x1f;
	if(buffer[0] & 0x70) tempax <<= 1;
	tempax++;	
	tempbx += tempax;        	/* yes -> skip it */
     }
     tempcx = push2;
     tempax = push1 << 8;
     tempbx = (tempbx & 0xff00) | (((tempbx & 0x00ff) + 0x80) & 0x00ff);
     if(tempcx & 0xf800) {
        tempal = addresstable[((tempax & 0xff00) >> 8)];
	tempcx &= 0xf8ff;
	tempcx >>= 11;
	for(i=0; i<tempcx; i++) {
	   SiS_Pr->SiS_DDC_SecAddr = (tempbx & 0x00ff);
	   SiS_ReadLCDDDC(SiS_Pr, 2, buffer);
	   tempbx += 0x04;
	   if(buffer[0] == tempal) break;
	}
	tempah = buffer[1];
	tempah &= 0x0c;
	tempah ^= 0x0c;
	tempah <<= 4;
	tempah |= 0x20;
        cr37 &= 0x1f;
        cr37 |= tempah;
	if((cr36 & 0x07) == 0x07) cr37 &= 0x0e;
     }
  }
  xf86DrvMsg(0, X_INFO, "DDC: cr36 = 0x%02x, cr37 = 0x%02x\n", cr36, cr37);
  return 0;
}
#endif

/* TW: Generic I2C functions (compliant to i2c library) */

#if 0
USHORT
SiS_I2C_GetByte(SiS_Private *SiS_Pr)
{
   return(SiS_ReadDDC2Data(SiS_Pr,0));
}

Bool
SiS_I2C_PutByte(SiS_Private *SiS_Pr, USHORT data)
{
   if(SiS_WriteDDC2Data(SiS_Pr,data)) return FALSE;
   return TRUE;
}

Bool
SiS_I2C_Address(SiS_Private *SiS_Pr, USHORT addr)
{
   if(SiS_SetStart(SiS_Pr)) return FALSE;
   if(SiS_WriteDDC2Data(SiS_Pr,addr)) return FALSE;
   return TRUE;
}

void
SiS_I2C_Stop(SiS_Private *SiS_Pr)
{
   SiS_SetStop(SiS_Pr);
}
#endif

#endif

void
SiS_SetCH70xxANDOR(SiS_Private *SiS_Pr, USHORT tempax,USHORT tempbh)
{
  USHORT tempal,tempah,tempbl;

  tempal = tempax & 0x00FF;
  tempah =(tempax >> 8) & 0x00FF;
  tempbl = SiS_GetCH70xx(SiS_Pr,tempal);
  tempbl = (((tempbl & tempbh) | tempah) << 8 | tempal);
  SiS_SetCH70xx(SiS_Pr,tempbl);
}

/* TW: Generic I2C functions for Chrontel --------- */

void
SiS_SetSwitchDDC2(SiS_Private *SiS_Pr)
{
  SiS_SetSCLKHigh(SiS_Pr);
  /* SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAY); */
  SiS_WaitRetraceDDC(SiS_Pr);

  SiS_SetSCLKLow(SiS_Pr);
  /* SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAY); */
  SiS_WaitRetraceDDC(SiS_Pr);
}

/* TW: Set I2C start condition */
/* TW: This is done by a SD high-to-low transition while SC is high */
USHORT
SiS_SetStart(SiS_Private *SiS_Pr)
{
  if(SiS_SetSCLKLow(SiS_Pr)) return 0xFFFF;			           /* TW: (SC->low)  */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index,
                  ~SiS_Pr->SiS_DDC_Data,SiS_Pr->SiS_DDC_Data);             /* TW: SD->high */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			           /* TW: SC->high */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index,
                  ~SiS_Pr->SiS_DDC_Data,0x00);                             /* TW: SD->low = start condition */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			           /* TW: (SC->low) */
  return 0;
}

/* TW: Set I2C stop condition */
/* TW: This is done by a SD low-to-high transition while SC is high */
USHORT
SiS_SetStop(SiS_Private *SiS_Pr)
{
  if(SiS_SetSCLKLow(SiS_Pr)) return 0xFFFF;			           /* TW: (SC->low) */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index,
                  ~SiS_Pr->SiS_DDC_Data,0x00);          		   /* TW: SD->low   */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			           /* TW: SC->high  */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index,
                  ~SiS_Pr->SiS_DDC_Data,SiS_Pr->SiS_DDC_Data);  	   /* TW: SD->high = stop condition */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			           /* TW: (SC->high) */
  return 0;
}

/* TW: Write 8 bits of data */
USHORT
SiS_WriteDDC2Data(SiS_Private *SiS_Pr, USHORT tempax)
{
  USHORT i,flag,temp;

  flag=0x80;
  for(i=0;i<8;i++) {
    SiS_SetSCLKLow(SiS_Pr);				                      /* TW: SC->low */
    if(tempax & flag) {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index,
                      ~SiS_Pr->SiS_DDC_Data,SiS_Pr->SiS_DDC_Data);            /* TW: Write bit (1) to SD */
    } else {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index,
                      ~SiS_Pr->SiS_DDC_Data,0x00);                            /* TW: Write bit (0) to SD */
    }
    SiS_SetSCLKHigh(SiS_Pr);				                      /* TW: SC->high */
    flag >>= 1;
  }
  temp = SiS_CheckACK(SiS_Pr);				                      /* TW: Check acknowledge */
  return(temp);
}

USHORT
SiS_ReadDDC2Data(SiS_Private *SiS_Pr, USHORT tempax)
{
  USHORT i,temp,getdata;

  getdata=0;
  for(i=0; i<8; i++) {
    getdata <<= 1;
    SiS_SetSCLKLow(SiS_Pr);
    SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index,
                    ~SiS_Pr->SiS_DDC_Data,SiS_Pr->SiS_DDC_Data);
    SiS_SetSCLKHigh(SiS_Pr);
    temp = SiS_GetReg1(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index);
    if(temp & SiS_Pr->SiS_DDC_Data) getdata |= 0x01;
  }
  return(getdata);
}

USHORT
SiS_SetSCLKLow(SiS_Private *SiS_Pr)
{
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index,
                  ~SiS_Pr->SiS_DDC_Clk,0x00);      		/* SetSCLKLow()  */
  SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT);
  return 0;
}

USHORT
SiS_SetSCLKHigh(SiS_Private *SiS_Pr)
{
  USHORT temp,watchdog=1000;

  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index,
                  ~SiS_Pr->SiS_DDC_Clk,SiS_Pr->SiS_DDC_Clk);  	/* SetSCLKHigh()  */
  do {
    temp = SiS_GetReg1(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index);
  } while((!(temp & SiS_Pr->SiS_DDC_Clk)) && --watchdog);
  if (!watchdog) {
#ifdef TWDEBUG
        xf86DrvMsg(0, X_INFO, "SetClkHigh failed\n");
#endif	   
  	return 0xFFFF;
  }
  SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT);
  return 0;
}

void
SiS_DDC2Delay(SiS_Private *SiS_Pr, USHORT delaytime)
{
  USHORT i;

  for(i=0; i<delaytime; i++) {
    SiS_GetReg1(SiS_Pr->SiS_P3c4,0x05);
  }
}

/* TW: Check I2C acknowledge */
/* Returns 0 if ack ok, non-0 if ack not ok */
USHORT
SiS_CheckACK(SiS_Private *SiS_Pr)
{
  USHORT tempah;

  SiS_SetSCLKLow(SiS_Pr);				           /* TW: (SC->low) */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index,
                  ~SiS_Pr->SiS_DDC_Data,SiS_Pr->SiS_DDC_Data);     /* TW: (SD->high) */
  SiS_SetSCLKHigh(SiS_Pr);				           /* TW: SC->high = clock impulse for ack */
  tempah = SiS_GetReg1(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index);/* TW: Read SD */
  SiS_SetSCLKLow(SiS_Pr);				           /* TW: SC->low = end of clock impulse */
  if(tempah & SiS_Pr->SiS_DDC_Data) return(1);			   /* TW: Ack OK if bit = 0 */
  else return(0);
}

/* TW: End of I2C functions ----------------------- */


/* =============== SiS 310/325/330 O.E.M. ================= */

#ifdef SIS315H

static USHORT
GetRAMDACromptr(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, UCHAR *ROMAddr)
{
  USHORT romptr;

  if(HwDeviceExtension->jChipType < SIS_330) {
     romptr = ROMAddr[0x128] | (ROMAddr[0x129] << 8);  
     if(SiS_Pr->SiS_VBType & VB_SIS301B302B)
        romptr = ROMAddr[0x12a] | (ROMAddr[0x12b] << 8);
  } else {
     romptr = ROMAddr[0x1a8] | (ROMAddr[0x1a9] << 8);  
     if(SiS_Pr->SiS_VBType & VB_SIS301B302B)
        romptr = ROMAddr[0x1aa] | (ROMAddr[0x1ab] << 8);
  }
  return(romptr);
}

static USHORT
GetLCDromptr(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, UCHAR *ROMAddr)
{
  USHORT romptr;

  if(HwDeviceExtension->jChipType < SIS_330) {
     romptr = ROMAddr[0x120] | (ROMAddr[0x121] << 8);  
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)
        romptr = ROMAddr[0x122] | (ROMAddr[0x123] << 8);
  } else {
     romptr = ROMAddr[0x1a0] | (ROMAddr[0x1a1] << 8);  
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)
        romptr = ROMAddr[0x1a2] | (ROMAddr[0x1a3] << 8);
  }
  return(romptr);
}

static USHORT
GetTVromptr(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, UCHAR *ROMAddr)
{
  USHORT romptr;

  if(HwDeviceExtension->jChipType < SIS_330) {
     romptr = ROMAddr[0x114] | (ROMAddr[0x115] << 8);
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)
        romptr = ROMAddr[0x11a] | (ROMAddr[0x11b] << 8);
  } else {
     romptr = ROMAddr[0x194] | (ROMAddr[0x195] << 8);
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)
        romptr = ROMAddr[0x19a] | (ROMAddr[0x19b] << 8);
  }
  return(romptr);
}

static USHORT
GetLCDPtrIndexBIOS(SiS_Private *SiS_Pr)
{
  USHORT index;
  
  index = SiS_Pr->SiS_LCDResInfo & 0x0F;
  if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050)      index -= 5;
  else if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1600x1200) index -= 6;
  index--;
  index *= 3;
  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) index += 2;
  else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) index++;

  return index;
}

static USHORT
GetLCDPtrIndex(SiS_Private *SiS_Pr)
{
  USHORT index;

  index = SiS_Pr->SiS_LCDResInfo & 0x0F;
  index--;
  index *= 3;
  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) index += 2;
  else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) index++;

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
static USHORT
GetTVPtrIndex(SiS_Private *SiS_Pr)
{
  USHORT index;

  index = 0;
  if(SiS_Pr->SiS_VBInfo & SetPALTV) index++;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) index++;  /* Hivision TV use PAL */

  index <<= 1;

  if((SiS_Pr->SiS_VBInfo & SetInSlaveMode) && (SiS_Pr->SiS_SetFlag & TVSimuMode))
    index++;

  return index;
}

static void
SetDelayComp(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
             UCHAR *ROMAddr, USHORT ModeNo)
{
  USHORT delay,index,myindex,temp,romptr=0;
  
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {			/* VGA */
     
     if((ROMAddr) && SiS_Pr->SiS_UseROM) {
        romptr = GetRAMDACromptr(SiS_Pr, HwDeviceExtension, ROMAddr);
	if(!romptr) return;
	delay = ROMAddr[romptr];
     } else {
        delay = 0x04;
        if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
	   if(IS_SIS650) {
	      delay = 0x0a;
	   } else if(IS_SIS740) {
	      delay = 0x00;
	   } else if(HwDeviceExtension->jChipType < SIS_330) {
	      delay = 0x0c;
	   } else {
	      delay = 0x0c;
	   }
	}
        if(SiS_Pr->SiS_IF_DEF_LVDS == 1)
           delay = 0x00;
     }
  
  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {		/* LCD */
  
     index = GetLCDPtrIndexBIOS(SiS_Pr);
     myindex = GetLCDPtrIndex(SiS_Pr);
     
     if(IS_SIS650 && (SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) { 	/* 650+30xLV */
       if(SiS_IsNotM650or651(SiS_Pr,HwDeviceExtension, BaseAddr)) {
          if((ROMAddr) && SiS_Pr->SiS_UseROM) {
#if 0	     /* Always use the second pointer on 650; some BIOSes */
             /* still carry old 301 data at the first location    */  
	     romptr = ROMAddr[0x120] | (ROMAddr[0x121] << 8); 
	     if(SiS_Pr->SiS_VBType & VB_SIS302LV) 
#endif		
	        romptr = ROMAddr[0x122] | (ROMAddr[0x123] << 8);
	     if(!romptr) return;
	     delay = ROMAddr[(romptr + index)];
	  } else {
             delay = SiS310_LCDDelayCompensation_650301B[myindex];   
#if 0	     
	     if(SiS_Pr->SiS_VBType & VB_SIS302LV)
	        delay = SiS310_LCDDelayCompensation_650301B[myindex];
#endif		
	  }
       } else {
          delay = SiS310_LCDDelayCompensation_651301LV[myindex];     
	  if(SiS_Pr->SiS_VBType & VB_SIS302LV)
	     delay = SiS310_LCDDelayCompensation_651302LV[myindex];  
       }
     } else {
        if((ROMAddr) && SiS_Pr->SiS_UseROM && 				/* 315, 330, 740, 650+301B */
	   (SiS_Pr->SiS_LCDResInfo != SiS_Pr->SiS_Panel1280x1024)) { 
           romptr = GetLCDromptr(SiS_Pr, HwDeviceExtension, ROMAddr);
	   if(!romptr) return;
	   delay = ROMAddr[(romptr + index)];
        } else {
           delay = SiS310_LCDDelayCompensation_301[myindex];
           if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
#if 0 	      /* This data is (like the one in the BIOS) wrong. */	   
	      if(IS_SIS650740) {  /* V */
	         delay = SiS310_LCDDelayCompensation_650301B[myindex];
	      } else {
#endif	      
                 delay = SiS310_LCDDelayCompensation_3xx301B[myindex];
#if 0		 
	      }
#endif	      
	   }
           if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	      if(IS_SIS650) {
                 delay = SiS310_LCDDelayCompensation_LVDS650[myindex];
	      } else {
	         delay = SiS310_LCDDelayCompensation_LVDS740[myindex];
	      }
	   }
        }
     }
     
  } else {							/* TV */
  
     index = GetTVPtrIndex(SiS_Pr);
     
     if(IS_SIS650 && (SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) {
       if(SiS_IsNotM650or651(SiS_Pr,HwDeviceExtension, BaseAddr)) {
          if((ROMAddr) && SiS_Pr->SiS_UseROM) {
#if 0	     /* Always use the second pointer on 650; some BIOSes */
             /* still carry old 301 data at the first location    */  	  
             romptr = ROMAddr[0x114] | (ROMAddr[0x115] << 8);
	     if(SiS_Pr->SiS_VBType & VB_SIS302LV)
#endif	     
	        romptr = ROMAddr[0x11a] | (ROMAddr[0x11b] << 8);
	     if(!romptr) return;
	     delay = ROMAddr[romptr + index];
	  } else {
	     delay = SiS310_TVDelayCompensation_301B[index];     
#if 0	     
	     if(SiS_Pr->SiS_VBType & VB_SIS302LV)
	        delay = SiS310_TVDelayCompensation_301B[index];  
#endif		
	  }
       } else {
          delay = SiS310_TVDelayCompensation_651301LV[index];       
	  if(SiS_Pr->SiS_VBType & VB_SIS302LV)
	      delay = SiS310_TVDelayCompensation_651302LV[index];   
       }
     } else {
       if((ROMAddr) && SiS_Pr->SiS_UseROM) {
          romptr = GetTVromptr(SiS_Pr, HwDeviceExtension, ROMAddr);
	  if(!romptr) return;
	  delay = ROMAddr[romptr + index];
       } else {
	    delay = SiS310_TVDelayCompensation_301[index];
            if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
	       if(IS_SIS740) 
	          delay = SiS310_TVDelayCompensation_740301B[index];
	       else 
                  delay = SiS310_TVDelayCompensation_301B[index];
	    }
            if(SiS_Pr->SiS_IF_DEF_LVDS == 1)
               delay = SiS310_TVDelayCompensation_LVDS[index];
       }
     }
    
  }

  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0xF0,delay);
     } else {
        if(IS_SIS650 && (SiS_Pr->SiS_IF_DEF_CH70xx != 0)) {
           delay <<= 4;
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0x0F,delay);
        } else {
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0xF0,delay);
        }
     }
  } else {
     if(IS_SIS650 && (SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) {
        temp = (SiS_GetReg1(SiS_Pr->SiS_P3d4,0x36) & 0xf0) >> 4;
        if(temp == 8) {
	   delay &= 0x0f;
	   delay |= 0xb0;
        } else if(temp == 6) {
           delay &= 0x0f;
	   delay |= 0xc0;
        }
        SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x2D,delay);  /* index 2D D[3:0] */
     } else {
        SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0xF0,delay);
     }
  }
}

static void
SetAntiFlicker(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
               UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT index,temp,romptr=0;

  temp = GetTVPtrIndex(SiS_Pr);
  temp >>= 1;  	  /* 0: NTSC, 1: PAL, 2: HiTV */

  if(ModeNo<=0x13)
     index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].VB_StTVFlickerIndex;
  else
     index = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].VB_ExtTVFlickerIndex;

  if(ROMAddr && SiS_Pr->SiS_UseROM) {
     romptr = ROMAddr[0x112] | (ROMAddr[0x113] << 8);
     if(HwDeviceExtension->jChipType == SIS_330) {
        romptr = ROMAddr[0x192] | (ROMAddr[0x193] << 8);
     }
  }

  if(romptr) {
     temp <<= 1;
     temp = ROMAddr[romptr + temp + index];
  } else {
     temp = SiS310_TVAntiFlick1[temp][index];
  }
  temp <<= 4;

  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x0A,0x8f,temp);  /* index 0A D[6:4] */
}

static void
SetEdgeEnhance(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
               UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT index,temp,romptr=0;

  temp = GetTVPtrIndex(SiS_Pr);
  temp >>= 1;              	/* 0: NTSC, 1: PAL, 2: HiTV */

  if(ModeNo<=0x13)
     index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].VB_StTVEdgeIndex;
  else
     index = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].VB_ExtTVEdgeIndex;

  if(ROMAddr && SiS_Pr->SiS_UseROM) {
     romptr = ROMAddr[0x124] | (ROMAddr[0x125] << 8);
     if(HwDeviceExtension->jChipType == SIS_330) {
        romptr = ROMAddr[0x1a4] | (ROMAddr[0x1a5] << 8);
     }
  }

  if(romptr) {
     temp <<= 1;
     temp = ROMAddr[romptr + temp + index];
  } else {
     temp = SiS310_TVEdge1[temp][index];
  }
  temp <<= 5;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x3A,0x1F,temp);  /* index 0A D[7:5] */
}

static void
SetYFilter(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
           UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT index, temp, i, j;
  UCHAR  OutputSelect = *SiS_Pr->pSiS_OutputSelect;

  temp = GetTVPtrIndex(SiS_Pr);
  temp >>= 1;  			/* 0: NTSC, 1: PAL, 2: HiTV */

  if (ModeNo<=0x13) {
    index =  SiS_Pr->SiS_SModeIDTable[ModeIdIndex].VB_StTVYFilterIndex;
  } else {
    index =  SiS_Pr->SiS_EModeIDTable[ModeIdIndex].VB_ExtTVYFilterIndex;
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV)  temp = 1;  /* Hivision TV uses PAL */

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
    for(i=0x35, j=0; i<=0x38; i++, j++) {
       SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_TVYFilter2[temp][index][j]);
    }
    for(i=0x48; i<=0x4A; i++, j++) {
       SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_TVYFilter2[temp][index][j]);
    }
  } else {
    for(i=0x35, j=0; i<=0x38; i++, j++){
       SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_TVYFilter1[temp][index][j]);
    }
  }

  if(ROMAddr && SiS_Pr->SiS_UseROM) {
  	OutputSelect = ROMAddr[0xf3];
	if(HwDeviceExtension->jChipType == SIS_330) {
	    OutputSelect = ROMAddr[0x11b];
	}
  }
  if(OutputSelect & EnablePALMN) {
      if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) & 0x01) {
         temp = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);
         temp &= (EnablePALM | EnablePALN);
         if(temp == EnablePALM) {
              if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
                 for(i=0x35, j=0; i<=0x38; i++, j++) {
                      SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_PALMFilter2[index][j]);
                 }
                 for(i=0x48; i<=0x4A; i++, j++) {
                      SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_PALMFilter2[index][j]);
                 }
              } else {
                 for(i=0x35, j=0; i<=0x38; i++, j++) {
                      SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_PALMFilter[index][j]);
                 }
              }
         }
         if(temp == EnablePALN) {
              if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
                 for(i=0x35, j=0; i<=0x38; i++, j++) {
                      SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_PALNFilter2[index][j]);
                 }
                 for(i=0x48, j=0; i<=0x4A; i++, j++) {
                      SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_PALNFilter2[index][j]);
                 }
             } else {
                 for(i=0x35, j=0; i<=0x38; i++, j++) {
                      SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_PALNFilter[index][j]);
		 }
             }
         }
      }
  }
}

static void
SetPhaseIncr(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
             UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT index,temp,temp1,i,j,resinfo,romptr=0;

  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) return;

  temp1 = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x38);        /* if PALM/N not set */
  temp1 &=  (EnablePALM | EnablePALN);
  if(temp1) return;

  if (ModeNo<=0x13) {
    resinfo =  SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
    resinfo =  SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  temp = GetTVPtrIndex(SiS_Pr);
  /* 0: NTSC Graphics, 1: NTSC Text,    2: PAL Graphics,
   * 3: PAL Text,      4: HiTV Graphics 5: HiTV Text
   */
  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     romptr = ROMAddr[0x116] | (ROMAddr[0x117] << 8);
     if(HwDeviceExtension->jChipType == SIS_330) {
        romptr = ROMAddr[0x196] | (ROMAddr[0x197] << 8);
     }
     if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
        romptr = ROMAddr[0x11c] | (ROMAddr[0x11d] << 8);
	if(HwDeviceExtension->jChipType == SIS_330) {
	   romptr = ROMAddr[0x19c] | (ROMAddr[0x19d] << 8);
	}
	if((SiS_Pr->SiS_VBInfo & SetInSlaveMode) && (!(SiS_Pr->SiS_SetFlag & TVSimuMode))) {
	   romptr = ROMAddr[0x116] | (ROMAddr[0x117] << 8);
	   if(HwDeviceExtension->jChipType == SIS_330) {
              romptr = ROMAddr[0x196] | (ROMAddr[0x197] << 8);
           }
	}
     }
  }
  if(romptr) {
     romptr += (temp << 2);
     for(j=0, i=0x31; i<=0x34; i++, j++) {
        SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr + j]);
     }
  } else {
     index = temp % 2;
     temp >>= 1;          /* 0:NTSC, 1:PAL, 2:HiTV */
     for(j=0, i=0x31; i<=0x34; i++, j++) {
        if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV))
	   SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_TVPhaseIncr1[temp][index][j]);
        else if((!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) || (SiS_Pr->SiS_SetFlag & TVSimuMode))
           SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_TVPhaseIncr2[temp][index][j]);
        else
           SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS310_TVPhaseIncr1[temp][index][j]);
     }
  }

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {    /* TW: 650/301LV: (VB_SIS301LV | VB_SIS302LV)) */
     if((!(SiS_Pr->SiS_VBInfo & SetPALTV)) && (ModeNo > 0x13)) {
        if(resinfo == 6) {
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x31,0x21);
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x32,0xf0);
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x33,0xf5);
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x34,0x7f);
	} else if (resinfo == 7) {
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x31,0x21);
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x32,0xf0);
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x33,0xf5);
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x34,0x7f);
	} else if (resinfo == 8) {
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x31,0x1e);
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x32,0x8b);
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x33,0xfb);
	      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x34,0x7b);
	}
     }
  }
}

void
SiS_OEM310Setting(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                  UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
   SetDelayComp(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo);
   /* TW: The TV functions are not for LVDS */
   if( (SiS_Pr->SiS_IF_DEF_LVDS == 0) && (SiS_Pr->SiS_VBInfo & SetCRT2ToTV) ) {
       SetAntiFlicker(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       SetPhaseIncr(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       SetYFilter(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) {
          SetEdgeEnhance(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       }
   }
}

/* FinalizeLCD
 * This finalizes some Part1 registers for the very panel used.
 * If we have a backup if these registers, we use it; otherwise
 * we set the register according to most BIOSes. However, this
 * function looks quite different in every BIOS, so you better
 * pray that we have a backup...
 */
void
SiS_FinalizeLCD(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                USHORT ModeIdIndex, PSIS_HW_DEVICE_INFO HwDeviceExtension)
{
  USHORT tempcl,tempch,tempbl,tempbh,tempbx,tempax,temp;
  USHORT resinfo,modeflag;

  if(!(SiS_Pr->SiS_VBType & VB_SIS301LV302LV)) return;

  if(ModeNo <= 0x13) {
	resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
	modeflag =  SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
    	resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
	modeflag =  SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
        SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x2a,0x00);
	SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x30,0x00);
	SiS_SetReg1(SiS_Pr->SiS_Part4Port,0x34,0x10);
     }
     tempch = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x36);
     tempch &= 0xf0;
     tempch >>= 4;
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
	   SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1f,0x76);
	}
	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {	
	   if((SiS_Pr->Backup == TRUE) && (SiS_Pr->Backup_Mode == ModeNo)) {
	      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x14,SiS_Pr->Backup_14);
	      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x15,SiS_Pr->Backup_15);
	      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x16,SiS_Pr->Backup_16);
	      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x17,SiS_Pr->Backup_17);
	      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,SiS_Pr->Backup_18);
	      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x19,SiS_Pr->Backup_19);
	      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1a,SiS_Pr->Backup_1a);
	      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1b,SiS_Pr->Backup_1b);
	      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1c,SiS_Pr->Backup_1c);
	      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1d,SiS_Pr->Backup_1d);
	   } else if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {	/* From 1.10.8w */
	       SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x14,0x90);
	       if(ModeNo <= 0x13) {
	          SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,0x11);
		  if((resinfo == 0) && (resinfo == 2)) return;
		  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,0x18);
		  if((resinfo == 1) && (resinfo == 3)) return;
	       }
	       SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,0x02);
	       if((ModeNo > 0x13) && (resinfo == 8)) {
	          SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,0x02);  /* 1.10.7u */
#if 0	       
	          tempbx = 806;  /* 0x326 */			 /* other older BIOSes */
		  tempbx--;
		  temp = tempbx & 0xff;
		  SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1b,temp);
		  temp = (tempbx >> 8) & 0x03;
		  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x1d,0xf8,temp);
#endif		  
	       }
	   } else {
	       if(ModeNo <= 0x13) {
	          if(ModeNo <= 1) {
		     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,0x70);
		     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x19,0xff);
		     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1b,0x48);
		     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1d,0x12);
		  }
		  if(!(modeflag & HalfDCLK)) {
		     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x14,0x20);
		     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x15,0x1a);
		     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x16,0x28);
		     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x17,0x00);
		     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,0x4c);
		     SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x19,0xdc);
		     if(ModeNo == 0x12) {
			 switch(tempch) {
			 case 0:
			    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,0x95);
			    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x19,0xdc);
			    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1a,0x10);
			    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1b,0x95);
			    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1c,0x48);
			    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1d,0x12);
			    break;
			 case 2:
			    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,0x95);
			    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1b,0x48);
			    break;
			 case 3:
			    SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x1b,0x95);
			    break;
			 }
		     }
		  }
	       }
	   }
	}
     } else {
        tempcl = tempbh = SiS_GetReg1(SiS_Pr->SiS_Part2Port,0x01);
	tempcl &= 0x0f;
	tempbh &= 0x70;
	tempbh >>= 4;
	tempbl = SiS_GetReg1(SiS_Pr->SiS_Part2Port,0x04);
	tempbx = (tempbh << 8) | tempbl;
	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
	   if((resinfo == 8) || (!(SiS_Pr->SiS_LCDInfo & DontExpandLCD))) {
	      if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
	      	tempbx = 770;
	      } else {
	        if(tempbx > 770) tempbx = 770;
		if(SiS_Pr->SiS_VGAVDE < 600) {
		   tempax = 768 - SiS_Pr->SiS_VGAVDE;
		   tempax >>= 4;  				/* From 1.10.7w; 1.10.6s: 3;  */
		   if(SiS_Pr->SiS_VGAVDE <= 480)  tempax >>= 4; /* From 1.10.7w; 1.10.6s: < 480; >>=1; */
		   tempbx -= tempax;
		}
	      }
	   } else return;
	}
#if 0
	if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1400x1050) {
	}
#endif
	temp = tempbx & 0xff;
	SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x04,temp);
	temp = (tempbx & 0xff00) >> 8;
	temp <<= 4;
	temp |= tempcl;
	SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x01,0x80,temp);
     }
  }
}

#if 0
/* TW: New and checked from 650/301LV BIOS */
/* This might clash with newer "FinalizeLCD()" function */
void
SiS_OEMLCD(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                  UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
   USHORT tempbx,tempah,tempbl,tempbh,tempcl;

   if(!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) return;

   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
      SiS_UnLockCRT2(SiS_Pr,HwDeviceExtension,BaseAddr);
      tempbh = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x1a);
      tempbh &= 0x38;
      tempbh >>= 3;
      tempbl = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x18);
      tempbx = (tempbh << 8) | tempbl;
      if(SiS_Pr->SiS_LCDTypeInfo == 1)  tempbx -= 0x12;
      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x18,tempbx & 0x00ff);
      tempah = (tempbx & 0xff00) >> 8;
      tempah &= 0x07;
      tempah <<= 3;
      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x1a,0xc7,tempah);
      tempah = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x19);
      tempah &= 0x0f;
      if(SiS_Pr->SiS_LCDTypeInfo == 1)  tempah -= 2;
      tempah &= 0x0f;
      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0xF0,tempah);
      tempah = SiS_GetReg1(SiS_Pr->SiS_Part1Port,0x14);
      if(SiS_Pr->SiS_LCDTypeInfo == 1)  tempah++;
      tempah -= 8;
      SiS_SetReg1(SiS_Pr->SiS_Part1Port,0x14,tempah);
   } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
      tempcl = tempbh = SiS_GetReg1(SiS_Pr->SiS_Part2Port,0x01);
      tempbh &= 0x70;
      tempbh >>= 4;
      tempbl = SiS_GetReg1(SiS_Pr->SiS_Part2Port,0x04);
      tempbx = (tempbh << 8) | tempbl;
      if(SiS_Pr->SiS_LCDTypeInfo == 1)  {
           tempbx -= 0x1e;
	   tempcl &= 0x0f;
	   tempcl -= 4;
	   tempcl &= 0x0f;
      }
      tempbl = tempbx & 0x00ff;
      tempbh = (tempbx >> 8) & 0x00ff;
      SiS_SetReg1(SiS_Pr->SiS_Part2Port,0x04,tempbl);
      tempbh <<= 4;
      tempbh |= tempcl;
      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x01,0x80,tempbh);
   }
}
#endif

#endif


/*  =================  SiS 300 O.E.M. ================== */

#ifdef SIS300

#if 0   /* Not used */
static USHORT
GetRevisionID(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension)
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

static USHORT
GetOEMLCDPtr(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, UCHAR *ROMAddr, int Flag)
{
  USHORT tempbx=0,romptr=0;
  UCHAR customtable300[] = {
  	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
  };
  UCHAR customtable630[] = {
  	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
  };

  if(HwDeviceExtension->jChipType == SIS_300) {

    tempbx = SiS_Pr->SiS_LCDResInfo - 2;
    if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx += 4;
    if(SiS_Pr->SiS_LCDResInfo == SiS_Pr->SiS_Panel1024x768) {
       if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 3;
    }
    if((ROMAddr) && SiS_Pr->SiS_UseROM) {
       if(ROMAddr[0x235] & 0x80) {
          tempbx = SiS_Pr->SiS_LCDTypeInfo;
          if(Flag) {
	    romptr = ROMAddr[0x255] | (ROMAddr[0x256] << 8);
	    if(romptr) {
	        tempbx = ROMAddr[romptr + SiS_Pr->SiS_LCDTypeInfo];
	    } else {
	        tempbx = customtable300[SiS_Pr->SiS_LCDTypeInfo];
	    }
            if(tempbx == 0xFF) return 0xFFFF;
          }
	  tempbx <<= 1;
	  if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx++;
       }
    }

  } else {

    if(Flag) {
      if((ROMAddr) && SiS_Pr->SiS_UseROM) {
         romptr = ROMAddr[0x255] | (ROMAddr[0x256] << 8);
	 if(romptr) {
	    tempbx = ROMAddr[romptr + SiS_Pr->SiS_LCDTypeInfo];
	 } else {
	    tempbx = 0xff;
	 }
      } else {
         tempbx = customtable630[SiS_Pr->SiS_LCDTypeInfo];
      }
      if(tempbx == 0xFF) return 0xFFFF;
      tempbx <<= 2;
      if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) tempbx += 2;
      if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
      return tempbx;
    }
    tempbx = SiS_Pr->SiS_LCDTypeInfo << 2;
    if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) tempbx += 2;
    if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
  }
  return tempbx;
}

static void
SetOEMLCDDelay(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
               UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT index,temp,romptr=0;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x237] & 0x01)) return;
     if(!(ROMAddr[0x237] & 0x02)) return;
     romptr = ROMAddr[0x24b] | (ROMAddr[0x24c] << 8);
  }

  /* TW: The Panel Compensation Delay should be set according to tables
   *     here. Unfortunately, various BIOS versions don't case about
   *     a uniform way using eg. ROM byte 0x220, but use different
   *     hard coded delays (0x04, 0x20, 0x18) in SetGroup1().
   *     Thus we don't set this if the user select a custom pdc or if
   *     we otherwise detected a valid pdc.
   */
  if(HwDeviceExtension->pdc) return;

  temp = GetOEMLCDPtr(SiS_Pr,HwDeviceExtension, ROMAddr, 0);

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_LCDDelayIndex;

  if(HwDeviceExtension->jChipType != SIS_300) {
	if(romptr) {
	   romptr += (temp * 2);
	   romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
	   romptr += index;
	   temp = ROMAddr[romptr];
	} else {
	   if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
    	      temp = SiS300_OEMLCDDelay2[temp][index];
	   } else {
              temp = SiS300_OEMLCDDelay3[temp][index];
           }
	}
  } else {
      if((ROMAddr) && SiS_Pr->SiS_UseROM && (ROMAddr[0x235] & 0x80)) {
	  if(romptr) {
	     romptr += (temp * 2);
	     romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
	     romptr += index;
	     temp = ROMAddr[romptr];
	  } else {
	     temp = SiS300_OEMLCDDelay5[temp][index];
	  }
      } else {
          if((ROMAddr) && SiS_Pr->SiS_UseROM) {
	     romptr = ROMAddr[0x249] | (ROMAddr[0x24a] << 8);
	     if(romptr) {
	        romptr += (temp * 2);
	        romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
	        romptr += index;
	        temp = ROMAddr[romptr];
	     } else {
	        temp = SiS300_OEMLCDDelay4[temp][index];
	     }
	  } else {
	     temp = SiS300_OEMLCDDelay4[temp][index];
	  }
      }
  }
  temp &= 0x3c;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,~0x3C,temp);  /* index 0A D[6:4] */
}

static void
SetOEMLCDData(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
               UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
#if 0  /* TW: Unfinished; VData table missing */
  USHORT index,temp;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x237] & 0x01)) return;
     if(!(ROMAddr[0x237] & 0x04)) return;
     /* No rom pointer in BIOS header! */
  }

  temp = GetOEMLCDPtr(SiS_Pr,HwDeviceExtension, ROMAddr, 1);
  if(temp = 0xFFFF) return;

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex]._VB_LCDHIndex;
  for(i=0x14, j=0; i<=0x17; i++, j++) {
      SiS_SetReg1(SiS_Pr->SiS_Part1Port,i,SiS300_LCDHData[temp][index][j]);
  }
  SiS_SetRegANDOR(SiS_SiS_Part1Port,0x1a, 0xf8, (SiS300_LCDHData[temp][index][j] & 0x07));

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex]._VB_LCDVIndex;
  SiS_SetReg1(SiS_SiS_Part1Port,0x18, SiS300_LCDVData[temp][index][0]);
  SiS_SetRegANDOR(SiS_SiS_Part1Port,0x19, 0xF0, SiS300_LCDVData[temp][index][1]);
  SiS_SetRegANDOR(SiS_SiS_Part1Port,0x1A, 0xC7, (SiS300_LCDVData[temp][index][2] & 0x38));
  for(i=0x1b, j=3; i<=0x1d; i++, j++) {
      SiS_SetReg1(SiS_Pr->SiS_Part1Port,i,SiS300_LCDVData[temp][index][j]);
  }
#endif
}

static USHORT
GetOEMTVPtr(SiS_Private *SiS_Pr)
{
  USHORT index;

  index = 0;
  if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode))  index += 4;
  if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToSCART)  index += 2;
     else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) index += 3;
     else if(SiS_Pr->SiS_VBInfo & SetPALTV)   index += 1;
  } else {
     if(SiS_Pr->SiS_VBInfo & SetCHTVOverScan) index += 2;
     if(SiS_Pr->SiS_VBInfo & SetPALTV)        index += 1;
  }
  return index;
}

static void
SetOEMTVDelay(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
              UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT index,temp,romptr=0;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x02)) return;
     romptr = ROMAddr[0x241] | (ROMAddr[0x242] << 8);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVDelayIndex;

  if(romptr) {
     romptr += (temp * 2);
     romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
     romptr += index;
     temp = ROMAddr[romptr];
  } else {
     if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
        temp = SiS300_OEMTVDelay301[temp][index];
     } else {
        temp = SiS300_OEMTVDelayLVDS[temp][index];
     }
  }
  temp &= 0x3c;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,~0x3C,temp);  /* index 0A D[6:4] */
}

static void
SetOEMAntiFlicker(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
                  USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
		  USHORT ModeIdIndex)
{
  USHORT index,temp,romptr=0;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x04)) return;
     romptr = ROMAddr[0x243] | (ROMAddr[0x244] << 8);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVFlickerIndex;

  if(romptr) {
     romptr += (temp * 2);
     romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
     romptr += index;
     temp = ROMAddr[romptr];
  } else {
     temp = SiS300_OEMTVFlicker[temp][index];
  }
  temp &= 0x70;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x0A,0x8F,temp);  /* index 0A D[6:4] */
}

static void
SetOEMPhaseIncr(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT index,i,j,temp,romptr=0;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVisionTV) return;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x08)) return;
     romptr = ROMAddr[0x245] | (ROMAddr[0x246] << 8);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVPhaseIndex;

  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
       for(i=0x31, j=0; i<=0x34; i++, j++) {
          SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS300_Phase2[temp][index][j]);
       }
  } else {
       if(romptr) {
          romptr += (temp * 2);
	  romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
	  romptr += (index * 4);
          for(i=0x31, j=0; i<=0x34; i++, j++) {
	     SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr + j]);
	  }
       } else {
          for(i=0x31, j=0; i<=0x34; i++, j++) {
             SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS300_Phase1[temp][index][j]);
	  }
       }
  }
}

static void
SetOEMYFilter(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
              UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex)
{
  USHORT index,temp,temp1,i,j,romptr=0;

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToSCART | SetCRT2ToHiVisionTV)) return;

  if((ROMAddr) && SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x10)) return;
     romptr = ROMAddr[0x247] | (ROMAddr[0x248] << 8);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVYFilterIndex;

  if(HwDeviceExtension->jChipType > SIS_300) {
     if(SiS_GetReg1(SiS_Pr->SiS_P3d4,0x31) & 0x01) {
       temp1 = SiS_GetReg1(SiS_Pr->SiS_P3d4,0x35);
       if(temp1 & (EnablePALM | EnablePALN)) {
          temp = 8;
	  if(!(temp1 & EnablePALM)) temp = 9;
       }
     }
  }
  if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      for(i=0x35, j=0; i<=0x38; i++, j++) {
       	SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS300_Filter2[temp][index][j]);
      }
      for(i=0x48; i<=0x4A; i++, j++) {
     	SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS300_Filter2[temp][index][j]);
      }
  } else {
      if(romptr) {
         romptr += (temp * 2);
	 romptr = ROMAddr[romptr] | (ROMAddr[romptr + 1] << 8);
	 romptr += (index * 4);
	 for(i=0x35, j=0; i<=0x38; i++, j++) {
       	    SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr + j]);
         }
      } else {
         for(i=0x35, j=0; i<=0x38; i++, j++) {
       	    SiS_SetReg1(SiS_Pr->SiS_Part2Port,i,SiS300_Filter1[temp][index][j]);
         }
      }
  }
}

void
SiS_OEM300Setting(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
		  USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo)
{
  USHORT ModeIdIndex;

  ModeIdIndex = SiS_SearchVBModeID(SiS_Pr,ROMAddr,&ModeNo);
  if(!(ModeIdIndex)) return;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
       SetOEMLCDDelay(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
            SetOEMLCDData(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       }
  }
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
       SetOEMTVDelay(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       if(SiS_Pr->SiS_IF_DEF_LVDS == 0) {
       		SetOEMAntiFlicker(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
    		SetOEMPhaseIncr(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       		SetOEMYFilter(SiS_Pr,HwDeviceExtension,BaseAddr,ROMAddr,ModeNo,ModeIdIndex);
       }
  }
}
#endif


