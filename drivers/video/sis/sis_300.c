/* Recently Update by v1.09.50 */
#include <linux/config.h>
#include "sis_300.h"

#if defined(ALLOC_PRAGMA)
#pragma alloc_text(PAGE,SiSSetMode)
#pragma alloc_text(PAGE,SiSInit300)
#endif


#ifdef NOBIOS
BOOLEAN SiSInit300(PHW_DEVICE_EXTENSION HwDeviceExtension)
{
	ULONG	ROMAddr  = (ULONG)HwDeviceExtension->VirtualRomBase;
	ULONG	FBAddr	= (ULONG)HwDeviceExtension->VirtualVideoMemoryAddress;
	USHORT  BaseAddr = (USHORT)HwDeviceExtension->IOAddress;
	UCHAR	i,temp,AGP;
	ULONG	j,k,ulTemp;
	UCHAR	SR07,SR11,SR19,SR1A,SR1F,SR21,SR22,SR23,SR24,SR25,SR32;
	UCHAR	SR14;
	ULONG	Temp;

	if(ROMAddr==0)	return (FALSE);
	if(FBAddr==0)	 return (FALSE);
	if(BaseAddr==0)  return (FALSE);
	if(HwDeviceExtension->jChipID >= SIS_Trojan)
		if(!HwDeviceExtension->bIntegratedMMEnabled)  return (FALSE);

	P3c4=BaseAddr+0x14;
	P3d4=BaseAddr+0x24;
	P3c0=BaseAddr+0x10;
	P3ce=BaseAddr+0x1e;
	P3c2=BaseAddr+0x12;
	P3ca=BaseAddr+0x1a;
	P3c6=BaseAddr+0x16;
	P3c7=BaseAddr+0x17;
	P3c8=BaseAddr+0x18;
	P3c9=BaseAddr+0x19;
	P3da=BaseAddr+0x2A;
	Set_LVDS_TRUMPION();

	SetReg1(P3c4,0x05,0x86);	// 1.Openkey
	SR14 = (UCHAR)GetReg1(P3c4,0x14);
	SR19 = (UCHAR)GetReg1(P3c4,0x19);
	SR1A = (UCHAR)GetReg1(P3c4,0x1A);
	for(i=0x06;i< 0x20;i++) SetReg1(P3c4,i,0);	// 2.Reset Extended register
	for(i=0x21;i<=0x27;i++) SetReg1(P3c4,i,0);	//	Reset Extended register
	for(i=0x31;i<=0x3D;i++) SetReg1(P3c4,i,0);
	for(i=0x30;i<=0x37;i++) SetReg1(P3d4,i,0);

	if(HwDeviceExtension->jChipID >= SIS_Trojan)
		temp=(UCHAR)SR1A;		// 3.Set Define Extended register
	else
	{
		temp=*((UCHAR *)(ROMAddr+SoftSettingAddr));
		if((temp&SoftDRAMType)==0){					
			temp=(UCHAR)GetReg1(P3c4,0x3A);	// 3.Set Define Extended register
		}
	} 
	RAMType=temp&0x07;
	SetMemoryClock(ROMAddr);
	for(k=0; k<5; k++)
	{
		for(j=0; j<0xffff; j++)
		{
			ulTemp = (ULONG)GetReg1(P3c4, 0x05);
		}
	}
	Temp = (ULONG)GetReg1(P3c4, 0x3C);
	Temp = Temp | 0x01;
	SetReg1(P3c4, 0x3C, (USHORT)Temp);
	for(k=0; k<5; k++)
	{
		for(j=0; j<0xffff; j++)
		{
			Temp = (ULONG)GetReg1(P3c4, 0x05);
		}
	}
	Temp = (ULONG)GetReg1(P3c4, 0x3C);
	Temp = Temp & 0xFE;
	SetReg1(P3c4, 0x3C, (USHORT)Temp);
	for(k=0; k<5; k++)
	{
		for(j=0; j<0xffff; j++)
		{
			Temp = (ULONG)GetReg1(P3c4, 0x05);
		}
	}

	SR07=*((UCHAR *)(ROMAddr+0xA4));
	SetReg1(P3c4,0x07,SR07);
	if (HwDeviceExtension->jChipID == SIS_Glamour )
	{
		for(i=0x15;i<=0x1C;i++) 
		{
			temp=*((UCHAR *)(ROMAddr+0xA5+((i-0x15)*8)+RAMType));
			SetReg1(P3c4,i,temp);
		}
	}

	SR1F=*((UCHAR *)(ROMAddr+0xE5));
	SetReg1(P3c4,0x1F,SR1F);

	AGP=1;							// Get AGP
	temp=(UCHAR)GetReg1(P3c4,0x3A);
	temp=temp&0x30;
	if(temp==0x30) AGP=0;			// PCI

	SR21=*((UCHAR *)(ROMAddr+0xE6));
	if(AGP==0) SR21=SR21&0xEF;		// PCI
	SetReg1(P3c4,0x21,SR21);

	SR22=*((UCHAR *)(ROMAddr+0xE7));
	if(AGP==1) SR22=SR22&0x20;		// AGP
	SetReg1(P3c4,0x22,SR22);

	SR23=*((UCHAR *)(ROMAddr+0xE8));
	SetReg1(P3c4,0x23,SR23);

	SR24=*((UCHAR *)(ROMAddr+0xE9));
	SetReg1(P3c4,0x24,SR24);

	SR25=*((UCHAR *)(ROMAddr+0xEA));
	SetReg1(P3c4,0x25,SR25);

	SR32=*((UCHAR *)(ROMAddr+0xEB));
	SetReg1(P3c4,0x32,SR32);

	SR11=0x0F;
	SetReg1(P3c4,0x11,SR11);

	if(IF_DEF_LVDS==1){				//LVDS
		temp=ExtChipLVDS;
	}else if(IF_DEF_TRUMPION==1){ 	//Trumpion
		temp=ExtChipTrumpion;
	}else{ 			//301
		temp=ExtChip301;
	}
	SetReg1(P3d4,0x37,temp);

	//For SiS 630/540 Chip
	//Restore SR14, SR19 and SR1A
	SetReg1(P3c4,0x14,SR14);
	SetReg1(P3c4,0x19,SR19);
	SetReg1(P3c4,0x1A,SR1A);

	SetReg3(P3c6,0xff);		// Reset register
	ClearDAC(P3c8);			// Reset register
	DetectMonitor(HwDeviceExtension);	 //sense CRT1
	GetSenseStatus(HwDeviceExtension,BaseAddr,ROMAddr);//sense CRT2		
	
	return(TRUE);
}

VOID Set_LVDS_TRUMPION(VOID)
{
	IF_DEF_LVDS=0;
	IF_DEF_TRUMPION=0;	 
}

VOID SetMemoryClock(ULONG ROMAddr)
{
	UCHAR data,i;

	MCLKData=*((USHORT *)(ROMAddr+0x20C));		// MCLKData Table
	MCLKData=MCLKData+RAMType*5;
	ECLKData=MCLKData+0x28;

	for(i=0x28;i<=0x2A;i++) {					// Set MCLK
		data=*((UCHAR *)(ROMAddr+MCLKData));
		SetReg1(P3c4,i,data);
		MCLKData++;
	}

	for(i=0x2E;i<=0x30;i++) {					// Set ECLK
		data=*((UCHAR *)(ROMAddr+ECLKData));
		SetReg1(P3c4,i,data);
		ECLKData++;
	}
}
#endif   /* NOBIOS */

#ifdef CONFIG_FB_SIS_LINUXBIOS
BOOLEAN SiSInit300(PHW_DEVICE_EXTENSION HwDeviceExtension)
{
	ULONG	ROMAddr  = 0;
	USHORT  BaseAddr = (USHORT)HwDeviceExtension->IOAddress;
	UCHAR	i,temp,AGP;
	ULONG	j,k,ulTemp;
	UCHAR	SR07,SR11,SR19,SR1A,SR1F,SR21,SR22,SR23,SR24,SR25,SR32;
	UCHAR	SR14;
	ULONG	Temp;

	if(BaseAddr==0)  return (FALSE);
	if(HwDeviceExtension->jChipID >= SIS_Trojan)
		if(!HwDeviceExtension->bIntegratedMMEnabled)  return (FALSE);

	P3c4=BaseAddr+0x14;
	P3d4=BaseAddr+0x24;
	P3c0=BaseAddr+0x10;
	P3ce=BaseAddr+0x1e;
	P3c2=BaseAddr+0x12;
	P3ca=BaseAddr+0x1a;
	P3c6=BaseAddr+0x16;
	P3c7=BaseAddr+0x17;
	P3c8=BaseAddr+0x18;
	P3c9=BaseAddr+0x19;
	P3da=BaseAddr+0x2A;

	SetReg1(P3c4,0x05,0x86);	// 1.Openkey

	SR14 = (UCHAR)GetReg1(P3c4,0x14);
	SR19 = (UCHAR)GetReg1(P3c4,0x19);
	SR1A = (UCHAR)GetReg1(P3c4,0x1A);

	for(i=0x06;i< 0x20;i++) SetReg1(P3c4,i,0);	// 2.Reset Extended register
	for(i=0x21;i<=0x27;i++) SetReg1(P3c4,i,0);	//	Reset Extended register
	for(i=0x31;i<=0x3D;i++) SetReg1(P3c4,i,0);
	for(i=0x30;i<=0x37;i++) SetReg1(P3d4,i,0);

	temp=(UCHAR)SR1A;		// 3.Set Define Extended register

	RAMType=temp&0x07;
	SetMemoryClock(ROMAddr);
	for(k=0; k<5; k++)
		for(j=0; j<0xffff; j++)
			ulTemp = (ULONG)GetReg1(P3c4, 0x05);

	Temp = (ULONG)GetReg1(P3c4, 0x3C);
	Temp = Temp | 0x01;
	SetReg1(P3c4, 0x3C, (USHORT)Temp);

	for(k=0; k<5; k++)
		for(j=0; j<0xffff; j++)
			Temp = (ULONG)GetReg1(P3c4, 0x05);

	Temp = (ULONG)GetReg1(P3c4, 0x3C);
	Temp = Temp & 0xFE;
	SetReg1(P3c4, 0x3C, (USHORT)Temp);

	for(k=0; k<5; k++)
		for(j=0; j<0xffff; j++)
			Temp = (ULONG)GetReg1(P3c4, 0x05);

	SR07=SRegsInit[0x07];
	SetReg1(P3c4,0x07,SR07);

	SR1F=SRegsInit[0x1F];
	SetReg1(P3c4,0x1F,SR1F);

	AGP=1;							// Get AGP
	temp=(UCHAR)GetReg1(P3c4,0x3A);
	temp=temp&0x30;
	if(temp==0x30) AGP=0;			// PCI

	SR21=SRegsInit[0x21];
	if(AGP==0) SR21=SR21&0xEF;		// PCI
	SetReg1(P3c4,0x21,SR21);

	SR22=SRegsInit[0x22];
	if(AGP==1) SR22=SR22&0x20;		// AGP
	SetReg1(P3c4,0x22,SR22);

	SR23=SRegsInit[0x23];
	SetReg1(P3c4,0x23,SR23);

	SR24=SRegsInit[0x24];
	SetReg1(P3c4,0x24,SR24);

	SR25=SRegsInit[0x25];
	SetReg1(P3c4,0x25,SR25);

	SR32=SRegsInit[0x32];
	SetReg1(P3c4,0x32,SR32);

	SR11=0x0F;
	SetReg1(P3c4,0x11,SR11);

	temp=ExtChip301;
	SetReg1(P3d4,0x37,temp);

	SetReg1(P3c4,0x14,SR14);
	SetReg1(P3c4,0x19,SR19);
	SetReg1(P3c4,0x1A,SR1A);

	SetReg3(P3c6,0xff);		// Reset register
	ClearDAC(P3c8);			// Reset register
	DetectMonitor(HwDeviceExtension);	 //sense CRT1
	
	return(TRUE);
}

VOID SetMemoryClock(ULONG ROMAddr)
{
	UCHAR  i;
	USHORT idx;

	u8 MCLK[] = {
		0x5A, 0x64, 0x80, 0x66, 0x00,	// SDRAM
		0xB3, 0x45, 0x80, 0x83, 0x00,	// SGRAM
		0x37, 0x61, 0x80, 0x00, 0x01,	// ESDRAM
		0x37, 0x22, 0x80, 0x33, 0x01,
		0x37, 0x61, 0x80, 0x00, 0x01,
		0x37, 0x61, 0x80, 0x00, 0x01,
		0x37, 0x61, 0x80, 0x00, 0x01,
		0x37, 0x61, 0x80, 0x00, 0x01
	};

	u8 ECLK[] = {
		0x54, 0x43, 0x80, 0x00, 0x01,
		0x53, 0x43, 0x80, 0x00, 0x01,
		0x55, 0x43, 0x80, 0x00, 0x01,
		0x52, 0x43, 0x80, 0x00, 0x01,
		0x3f, 0x42, 0x80, 0x00, 0x01,
		0x54, 0x43, 0x80, 0x00, 0x01,
		0x54, 0x43, 0x80, 0x00, 0x01,
		0x54, 0x43, 0x80, 0x00, 0x01
	};

	idx = RAMType * 5;

	for (i = 0x28; i <= 0x2A; i++) {	// Set MCLK
		SetReg1(P3c4, i, MCLK[idx]);
		idx++;
	}

	idx = RAMType * 5;
	for (i = 0x2E; i <= 0x30; i++) {	// Set ECLK
		SetReg1(P3c4, i, ECLK[idx]);
		idx++;
	}

}

#endif   /* CONFIG_FB_SIS_LINUXBIOS */

// =========================================
// ======== SiS SetMode Function  ==========
// =========================================

#ifdef CONFIG_FB_SIS_LINUXBIOS
BOOLEAN SiSSetMode(PHW_DEVICE_EXTENSION HwDeviceExtension,
						 USHORT ModeNo)
{
	ULONG	i;
	USHORT  cr30flag,cr31flag;
	ULONG	ROMAddr  = (ULONG)HwDeviceExtension->VirtualRomBase;
	USHORT  BaseAddr = (USHORT)HwDeviceExtension->IOAddress;

	P3c4=BaseAddr+0x14;
	P3d4=BaseAddr+0x24;
	P3c0=BaseAddr+0x10;
	P3ce=BaseAddr+0x1e;
	P3c2=BaseAddr+0x12;
	P3ca=BaseAddr+0x1a;
	P3c6=BaseAddr+0x16;
	P3c7=BaseAddr+0x17;
	P3c8=BaseAddr+0x18;
	P3c9=BaseAddr+0x19;
	P3da=BaseAddr+0x2A;

	cr30flag=(UCHAR)GetReg1(P3d4,0x30);

	if(((cr30flag&0x01)==1)||((cr30flag&0x02)==0)){
		SetReg1(P3d4,0x34,ModeNo); 
		//SetSeqRegs(ROMAddr);
		{
			UCHAR SRdata;
			SRdata = SRegs[0x01] | 0x20;
			SetReg1(P3c4, 0x01, SRdata);

			for (i = 02; i <= 04; i++)
				SetReg1(P3c4, i, SRegs[i]);
		}

		//SetMiscRegs(ROMAddr);
		{
			SetReg3(P3c2, 0x23);
		}

		//SetCRTCRegs(ROMAddr);
		{
			UCHAR CRTCdata;

			CRTCdata = (UCHAR) GetReg1(P3d4, 0x11);
			SetReg1(P3d4, 0x11, CRTCdata);

			for (i = 0; i <= 0x18; i++)
				SetReg1(P3d4, i, CRegs[i]);
		}

		//SetATTRegs(ROMAddr);
		{
			for (i = 0; i <= 0x13; i++) {
				GetReg2(P3da);
				SetReg3(P3c0, i);
				SetReg3(P3c0, ARegs[i]);
			}
			GetReg2(P3da);
			SetReg3(P3c0, 0x14);
			SetReg3(P3c0, 0x00);
			GetReg2(P3da);
			SetReg3(P3c0, 0x20);
		}

		//SetGRCRegs(ROMAddr);
		{
			for (i = 0; i <= 0x08; i++)
				SetReg1(P3ce, i, GRegs[i]);
		}

		//ClearExt1Regs();
		{
			for (i = 0x0A; i <= 0x0E; i++)
				SetReg1(P3c4, i, 0x00);
		}


		//SetSync(ROMAddr);
		{
			SetReg3(P3c2, MReg);
		}

		//SetCRT1CRTC(ROMAddr);
		{
			UCHAR data;

			data = (UCHAR) GetReg1(P3d4, 0x11);
			data = data & 0x7F;
			SetReg1(P3d4, 0x11, data);

			for (i = 0; i <= 0x07; i++)
				SetReg1(P3d4, i, CRegs[i]);
			for (i = 0x10; i <= 0x12; i++)
				SetReg1(P3d4, i, CRegs[i]);
			for (i = 0x15; i <= 0x16; i++)
				SetReg1(P3d4, i, CRegs[i]);
			for (i = 0x0A; i <= 0x0C; i++)
				SetReg1(P3c4, i, SRegs[i]);

			data = SRegs[0x0E] & 0xE0;
			SetReg1(P3c4, 0x0E, data);

			SetReg1(P3d4, 0x09, CRegs[0x09]);
		}

		//SetCRT1Offset(ROMAddr);
		{
			SetReg1(P3c4, 0x0E, SRegs[0x0E]);
			SetReg1(P3c4, 0x10, SRegs[0x10]);
		}

		//SetCRT1VCLK(HwDeviceExtension, ROMAddr);
		{
			SetReg1(P3c4, 0x31, 0);

			for (i = 0x2B; i <= 0x2C; i++)
				SetReg1(P3c4, i, SRegs[i]);
			SetReg1(P3c4, 0x2D, 0x80);
		}

		//SetVCLKState(HwDeviceExtension, ROMAddr, ModeNo);
		{
			SetReg1(P3c4, 0x32, SRegs[0x32]);
			SetReg1(P3c4, 0x07, SRegs[0x07]);
		}

		//SetCRT1FIFO2(ROMAddr);
		{
			SetReg1(P3c4, 0x15, SRegs[0x15]);

			SetReg4(0xcf8, 0x80000050);
			SetReg4(0xcfc, 0xc5041e04);

			SetReg1(P3c4, 0x08, SRegs[0x08]);
			SetReg1(P3c4, 0x0F, SRegs[0x0F]);
			SetReg1(P3c4, 0x3b, 0x00);
			SetReg1(P3c4, 0x09, SRegs[0x09]);
		}

		//SetCRT1ModeRegs(ROMAddr, ModeNo);
		{
			SetReg1(P3c4, 0x06, SRegs[0x06]);
			SetReg1(P3c4, 0x01, SRegs[0x01]);
			SetReg1(P3c4, 0x0F, SRegs[0x0F]);
			SetReg1(P3c4, 0x21, SRegs[0x21]);
		}

		if(HwDeviceExtension->jChipID >= SIS_Trojan)
		{
			//SetInterlace(ROMAddr,ModeNo);
			SetReg1(P3d4, 0x19, CRegs[0x19]);
			SetReg1(P3d4, 0x1A, CRegs[0x1A]);
		}

		LoadDAC(ROMAddr);

		ClearBuffer(HwDeviceExtension);
	}

	cr31flag=(UCHAR)GetReg1(P3d4,0x31);
	DisplayOn();	// 16.DisplayOn
	return(NO_ERROR);
}

VOID LoadDAC(ULONG ROMAddr)
{
	USHORT data,data2;
	USHORT time,i,j,k;
	USHORT m,n,o;
	USHORT si,di,bx,dl;
	USHORT al,ah,dh;
	USHORT *table=VGA_DAC;

	time=256;
	table=VGA_DAC;
	j=16;

	SetReg3(P3c6,0xFF);
	SetReg3(P3c8,0x00);

	for(i=0;i<j;i++) {
		data=table[i];
		for(k=0;k<3;k++) {
			data2=0;
			if(data&0x01) data2=0x2A;
			if(data&0x02) data2=data2+0x15;
			SetReg3(P3c9,data2);
			data=data>>2;
		}
	}

	if(time==256) {
		for(i=16;i<32;i++) {
			data=table[i];
			for(k=0;k<3;k++) SetReg3(P3c9,data);
		}
		si=32;
		for(m=0;m<9;m++) {
			di=si;
			bx=si+0x04;
			dl=0;
			for(n=0;n<3;n++) {
				for(o=0;o<5;o++) {
					dh=table[si];
					ah=table[di];
					al=table[bx];
					si++;
					WriteDAC(dl,ah,al,dh);
				}	
				si=si-2;
				for(o=0;o<3;o++) {
					dh=table[bx];
					ah=table[di];
					al=table[si];
					si--;
					WriteDAC(dl,ah,al,dh);
				}	
				dl++;
			}
			si=si+5;
		}
	}
}

VOID WriteDAC(USHORT dl, USHORT ah, USHORT al, USHORT dh)
{
	USHORT temp;
	USHORT bh,bl;

	bh=ah;
	bl=al;
	if(dl!=0) {
		temp=bh;
		bh=dh;
		dh=temp;
		if(dl==1) {
			temp=bl;
			bl=dh;
			dh=temp;
		}
		else {
			temp=bl;
			bl=bh;
			bh=temp;
		}
	}
	SetReg3(P3c9,(USHORT)dh);
	SetReg3(P3c9,(USHORT)bh);
	SetReg3(P3c9,(USHORT)bl);
}


VOID DisplayOn()
{
	USHORT data;

	data=GetReg1(P3c4,0x01);
	data=data&0xDF;
	SetReg1(P3c4,0x01,data);
}


#else
BOOLEAN SiSSetMode(PHW_DEVICE_EXTENSION HwDeviceExtension,
						 USHORT ModeNo)
{
	ULONG	temp;
	USHORT  cr30flag,cr31flag;
	ULONG	ROMAddr  = (ULONG)HwDeviceExtension->VirtualRomBase;
	USHORT  BaseAddr = (USHORT)HwDeviceExtension->IOAddress;

	P3c4=BaseAddr+0x14;
	P3d4=BaseAddr+0x24;
	P3c0=BaseAddr+0x10;
	P3ce=BaseAddr+0x1e;
	P3c2=BaseAddr+0x12;
	P3ca=BaseAddr+0x1a;
	P3c6=BaseAddr+0x16;
	P3c7=BaseAddr+0x17;
	P3c8=BaseAddr+0x18;
	P3c9=BaseAddr+0x19;
	P3da=BaseAddr+0x2A;
	if(ModeNo&0x80){
	  ModeNo=ModeNo&0x7F;
	  flag_clearbuffer=0;		  
	}else{
	  flag_clearbuffer=1;
	}

	PresetScratchregister(P3d4,HwDeviceExtension); //add for CRT2

	SetReg1(P3c4,0x05,0x86);				// 1.Openkey
	temp=SearchModeID(ROMAddr,ModeNo);		// 2.Get ModeID Table
	if(temp==0)  return(0);
	
	SetTVSystem(HwDeviceExtension,ROMAddr);	//add for CRT2
	GetLCDDDCInfo(HwDeviceExtension);		//add for CRT2
	GetVBInfo(BaseAddr,ROMAddr);			//add for CRT2
	GetLCDResInfo(ROMAddr,P3d4);			//add for CRT2

	temp=CheckMemorySize(ROMAddr);			// 3.Check memory size
	if(temp==0) return(0);
	cr30flag=(UCHAR)GetReg1(P3d4,0x30);
	if(((cr30flag&0x01)==1)||((cr30flag&0x02)==0)){
		// if cr30 d[0]=1 or d[1]=0 set crt1  
		SetReg1(P3d4,0x34,ModeNo); 
		// set CR34->CRT1 ModeNofor CRT2 FIFO
		GetModePtr(ROMAddr,ModeNo);			// 4.GetModePtr
		SetSeqRegs(ROMAddr);				// 5.SetSeqRegs
		SetMiscRegs(ROMAddr);				// 6.SetMiscRegs
		SetCRTCRegs(ROMAddr);				// 7.SetCRTCRegs
		SetATTRegs(ROMAddr);				// 8.SetATTRegs
		SetGRCRegs(ROMAddr);				// 9.SetGRCRegs
		ClearExt1Regs();					// 10.Clear Ext1Regs
		temp=GetRatePtr(ROMAddr,ModeNo);	// 11.GetRatePtr
		if(temp) {
			SetSync(ROMAddr);				// 12.SetSync
			SetCRT1CRTC(ROMAddr);			// 13.SetCRT1CRTC
			SetCRT1Offset(ROMAddr);			// 14.SetCRT1Offset
			SetCRT1VCLK(HwDeviceExtension, ROMAddr);	// 15.SetCRT1VCLK
			SetVCLKState(HwDeviceExtension, ROMAddr, ModeNo);
			if(HwDeviceExtension->jChipID >= SIS_Trojan)
				SetCRT1FIFO2(ROMAddr);
			else
				SetCRT1FIFO(ROMAddr);
		}
		SetCRT1ModeRegs(ROMAddr, ModeNo);
		if(HwDeviceExtension->jChipID >= SIS_Trojan)
			SetInterlace(ROMAddr,ModeNo);
		LoadDAC(ROMAddr);
		if(flag_clearbuffer) ClearBuffer(HwDeviceExtension);
	}

	cr31flag=(UCHAR)GetReg1(P3d4,0x31);
	if(((cr30flag&0x01)==1)||((cr30flag&0x03)==0x02)
	  ||(((cr30flag&0x03)==0x00)&&((cr31flag&0x20)==0x20))){
		//if CR30 d[0]=1 or d[1:0]=10, set CRT2 or cr30 cr31== 0x00 0x20	
		SetCRT2Group(BaseAddr,ROMAddr,ModeNo, HwDeviceExtension); //CRT2
	}
	DisplayOn();	// 16.DisplayOn
	return(NO_ERROR);
}

BOOLEAN SearchModeID(ULONG ROMAddr, USHORT ModeNo)
{
	UCHAR ModeID;
	USHORT  usIDLength;

	ModeIDOffset=*((USHORT *)(ROMAddr+0x20A));		// Get EModeIDTable
	ModeID=*((UCHAR *)(ROMAddr+ModeIDOffset));		// Offset 0x20A
	usIDLength = GetModeIDLength(ROMAddr, ModeNo);
	while(ModeID!=0xff && ModeID!=ModeNo) {
		ModeIDOffset=ModeIDOffset+usIDLength;
		ModeID=*((UCHAR *)(ROMAddr+ModeIDOffset));
	}
	if(ModeID==0xff) return(FALSE);
	else return(TRUE);
}

BOOLEAN CheckMemorySize(ULONG ROMAddr)
{
	USHORT memorysize;
	USHORT modeflag;
	USHORT temp;

	modeflag=*((USHORT *)(ROMAddr+ModeIDOffset+0x01));	// si+St_ModeFlag
	ModeType=modeflag&ModeInfoFlag;				// Get mode type

	memorysize=modeflag&MemoryInfoFlag;
	memorysize=memorysize>MemorySizeShift;
	memorysize++;								// Get memory size

	temp=GetReg1(P3c4,0x14);					// Get DRAM Size
	temp=temp&0x3F;
	temp++;

	if(temp<memorysize) return(FALSE);
	else return(TRUE);
}

VOID GetModePtr(ULONG ROMAddr, USHORT ModeNo)
{
	UCHAR index;

	StandTable=*((USHORT *)(ROMAddr+0x202));	// Get First  0x202
												// StandTable Offset
	if(ModeNo<=13) {
		index=*((UCHAR *)(ROMAddr+ModeIDOffset+0x03));	// si+St_ModeFlag
	}
	else {
		if(ModeType <= 0x02) index=0x1B;		// 02 -> ModeEGA
		else index=0x0F;
	}

	StandTable=StandTable+64*index;				// Get ModeNo StandTable

}

VOID SetSeqRegs(ULONG ROMAddr)
{
	UCHAR SRdata;
	USHORT i;

	SetReg1(P3c4,0x00,0x03);					// Set SR0
	StandTable=StandTable+0x05;
	SRdata=*((UCHAR *)(ROMAddr+StandTable));	// Get SR01 from file
	if(IF_DEF_LVDS==1){
		if(VBInfo&SetCRT2ToLCD){
			if(VBInfo&SetInSlaveMode){
				if(LCDInfo&LCDNonExpanding){
					SRdata=SRdata|0x01;
				}
			}
		}
	}

	SRdata=SRdata|0x20;
	SetReg1(P3c4,0x01,SRdata);					// Set SR1
	for(i=02;i<=04;i++) {
		StandTable++;
		SRdata=*((UCHAR *)(ROMAddr+StandTable));	// Get SR2,3,4 from file
		SetReg1(P3c4,i,SRdata);					// Set SR2 3 4
	}
}

VOID SetMiscRegs(ULONG ROMAddr)
{
	UCHAR Miscdata;

	StandTable++;
	Miscdata=*((UCHAR *)(ROMAddr+StandTable));	// Get Misc from file
	SetReg3(P3c2,Miscdata);						// Set Misc(3c2)
}

VOID SetCRTCRegs(ULONG ROMAddr)
{
	UCHAR CRTCdata;
	USHORT i;

	CRTCdata=(UCHAR)GetReg1(P3d4,0x11);
	CRTCdata=CRTCdata&0x7f;
	SetReg1(P3d4,0x11,CRTCdata);				// Unlock CRTC

	for(i=0;i<=0x18;i++) {
		StandTable++;
		CRTCdata=*((UCHAR *)(ROMAddr+StandTable));		// Get CRTC from file
		SetReg1(P3d4,i,CRTCdata);				// Set CRTC(3d4)
	}
}

VOID SetATTRegs(ULONG ROMAddr)
{
	UCHAR ARdata;
	USHORT i;

	for(i=0;i<=0x13;i++) {
		StandTable++;
		ARdata=*((UCHAR *)(ROMAddr+StandTable));	  // Get AR for file
		if(IF_DEF_LVDS==1){  //for LVDS
			if(VBInfo&SetCRT2ToLCD){
				if(VBInfo&SetInSlaveMode){
					if(LCDInfo&LCDNonExpanding){
				 		if(i==0x13){
							ARdata=0;
				 		}
					}
				}
			}
		}
		GetReg2(P3da);			// reset 3da
		SetReg3(P3c0,i);		// set index
		SetReg3(P3c0,ARdata);	// set data
	}
	if(IF_DEF_LVDS==1){  //for LVDS
		if(VBInfo&SetCRT2ToLCD){
			if(VBInfo&SetInSlaveMode){
				if(LCDInfo&LCDNonExpanding){
				
				}
		 	}
		}
	}
	GetReg2(P3da);			// reset 3da
	SetReg3(P3c0,0x14);		// set index
	SetReg3(P3c0,0x00);		// set data

	GetReg2(P3da);			// Enable Attribute
	SetReg3(P3c0,0x20);
}

VOID SetGRCRegs(ULONG ROMAddr)
{
	UCHAR GRdata;
	USHORT i;

	for(i=0;i<=0x08;i++) {
		StandTable++;
		GRdata=*((UCHAR *)(ROMAddr+StandTable));	// Get GR from file
		SetReg1(P3ce,i,GRdata);						// Set GR(3ce)
	}
	if(ModeType>ModeVGA){
		GRdata=(UCHAR)GetReg1(P3ce,0x05);
		GRdata=GRdata&0xBF;
		SetReg1(P3ce,0x05,GRdata); 
	}
}

VOID ClearExt1Regs()
{
	USHORT i;

	for(i=0x0A;i<=0x0E;i++) SetReg1(P3c4,i,0x00);	// Clear SR0A-SR0E
}


BOOLEAN GetRatePtr(ULONG ROMAddr, USHORT ModeNo)
{
	SHORT	index;
  	USHORT temp;
  	USHORT ulRefIndexLength;

	if(ModeNo<0x14) return(FALSE);				// Mode No <= 13h then return

	index=GetReg1(P3d4,0x33);					// Get 3d4 CRTC33
	index=index&0x0F;							// Frame rate index
	if(index!=0) index--;
	REFIndex=*((USHORT *)(ROMAddr+ModeIDOffset+0x04));	// si+Ext_point

	ulRefIndexLength = GetRefindexLength(ROMAddr, ModeNo);
	do {
		temp=*((USHORT *)(ROMAddr+REFIndex));	// di => REFIndex
		if(temp==0xFFFF) break;
		temp=temp&ModeInfoFlag;
		if(temp<ModeType) break;

		REFIndex=REFIndex+ulRefIndexLength;		// rate size
		index--;
	} while(index>=0);

	REFIndex=REFIndex-ulRefIndexLength;			// rate size
	return(TRUE);
}

VOID SetSync(ULONG ROMAddr)
{
	USHORT sync;
	USHORT temp;

	sync=*((USHORT *)(ROMAddr+REFIndex));		// di+0x00
	sync=sync&0xC0;
	temp=0x2F;
	temp=temp|sync;
	SetReg3(P3c2,temp);							// Set Misc(3c2)
}

VOID SetCRT1CRTC(ULONG ROMAddr)
{
	UCHAR  index;
	UCHAR  data;
	USHORT i;

	index=*((UCHAR *)(ROMAddr+REFIndex+0x02));	// Get index
	index=index&0x03F;
	CRT1Table=*((USHORT *)(ROMAddr+0x204));		// Get CRT1Table
	CRT1Table=CRT1Table+index*CRT1Len;

	data=(UCHAR)GetReg1(P3d4,0x11);
	data=data&0x7F;
	SetReg1(P3d4,0x11,data);					// Unlock CRTC

	CRT1Table--;
	for(i=0;i<=0x05;i++) {
		CRT1Table++;
		data=*((UCHAR *)(ROMAddr+CRT1Table));
		SetReg1(P3d4,i,data);
	}
	for(i=0x06;i<=0x07;i++) {
		CRT1Table++;
		data=*((UCHAR *)(ROMAddr+CRT1Table));
		SetReg1(P3d4,i,data);
	}
	for(i=0x10;i<=0x12;i++) {
		CRT1Table++;
		data=*((UCHAR *)(ROMAddr+CRT1Table));
		SetReg1(P3d4,i,data);
	}
	for(i=0x15;i<=0x16;i++) {
		CRT1Table++;
		data=*((UCHAR *)(ROMAddr+CRT1Table));
		SetReg1(P3d4,i,data);
	}
	for(i=0x0A;i<=0x0C;i++) {
		CRT1Table++;
		data=*((UCHAR *)(ROMAddr+CRT1Table));
		SetReg1(P3c4,i,data);
	}

	CRT1Table++;
	data=*((UCHAR *)(ROMAddr+CRT1Table));
	data=data&0xE0;
	SetReg1(P3c4,0x0E,data);

	data=(UCHAR)GetReg1(P3d4,0x09);
	data=data&0xDF;
	i=*((UCHAR *)(ROMAddr+CRT1Table));
	i=i&0x01;
	i=i<<5;
	data=data|i;
	i=*((USHORT *)(ROMAddr+ModeIDOffset+0x01));
	i=i&DoubleScanMode;
	if(i) data=data|0x80;
	SetReg1(P3d4,0x09,data);

	if(ModeType>0x03) SetReg1(P3d4,0x14,0x4F);
}

VOID SetCRT1Offset(ULONG ROMAddr)
{
	USHORT temp,ah,al;
	USHORT temp2,i;
	USHORT DisplayUnit;

	temp=*((UCHAR *)(ROMAddr+ModeIDOffset+0x03));		// si+Ext_ModeInfo
	temp=temp>>4;										// index
	ScreenOffset=*((USHORT *)(ROMAddr+0x206));			// ScreenOffset
	temp=*((UCHAR *)(ROMAddr+ScreenOffset+temp));		// data

	temp2=*((USHORT *)(ROMAddr+REFIndex+0x00));
	temp2=temp2&InterlaceMode;
	if(temp2) temp=temp<<1;
	temp2=ModeType-ModeEGA;
	switch (temp2) {
		case 0 : temp2=1; break;
		case 1 : temp2=2; break;
		case 2 : temp2=4; break;
		case 3 : temp2=4; break;
		case 4 : temp2=6; break;
		case 5 : temp2=8; break;
	}
	temp=temp*temp2;
	DisplayUnit=temp;

	temp2=temp;
	temp=temp>>8;		
	temp=temp&0x0F;
	i=GetReg1(P3c4,0x0E);
	i=i&0xF0;
	i=i|temp;
	SetReg1(P3c4,0x0E,i);

	temp=(UCHAR)temp2;
	temp=temp&0xFF;		
	SetReg1(P3d4,0x13,temp);

	temp2=*((USHORT *)(ROMAddr+REFIndex+0x00));
	temp2=temp2&InterlaceMode;
	if(temp2) DisplayUnit>>=1;

	DisplayUnit=DisplayUnit<<5;
	ah=(DisplayUnit&0xff00)>>8;
	al=DisplayUnit&0x00ff;
	if(al==0) ah=ah+1;
	else ah=ah+2;
	SetReg1(P3c4,0x10,ah);
}


VOID SetCRT1VCLK(PHW_DEVICE_EXTENSION HwDeviceExtension, ULONG ROMAddr)
{
	USHORT i;
	UCHAR  index,data;

	index=*((UCHAR *)(ROMAddr+REFIndex+0x03));
	index=index&0x03F;
	CRT1VCLKLen=GetVCLKLen(ROMAddr);
	data=index*CRT1VCLKLen;
	VCLKData=*((USHORT *)(ROMAddr+0x208));
	VCLKData=VCLKData+data;

	SetReg1(P3c4,0x31,0);
	for(i=0x2B;i<=0x2C;i++) {
		data=*((UCHAR *)(ROMAddr+VCLKData));
		SetReg1(P3c4,i,data);
		VCLKData++;
	}
	SetReg1(P3c4,0x2D,0x80);
}


VOID SetCRT1ModeRegs(ULONG ROMAddr, USHORT ModeNo)
{

	USHORT data,data2,data3;

	if(ModeNo>0x13)	data=*((USHORT *)(ROMAddr+REFIndex+0x00));
	else data=0;

	data2=0;
	if(ModeNo>0x13)
		if(ModeType>0x02) {
			data2=data2|0x02;
			data3=ModeType-ModeVGA;
			data3=data3<<2;
			data2=data2|data3;
	 	}

	data=data&InterlaceMode;
	if(data) data2=data2|0x20;
	SetReg1(P3c4,0x06,data2);

	data=GetReg1(P3c4,0x01);
	data=data&0xF7;
	data2=*((USHORT *)(ROMAddr+ModeIDOffset+0x01));
	data2=data2&HalfDCLK;
	if(data2) data=data|0x08;
	SetReg1(P3c4,0x01,data);

	data=GetReg1(P3c4,0x0F);
	data=data&0xF7;
	data2=*((USHORT *)(ROMAddr+ModeIDOffset+0x01));
	data2=data2&LineCompareOff;
	if(data2) data=data|0x08;
	SetReg1(P3c4,0x0F,data);

	data=GetReg1(P3c4,0x21);
	data=data&0x1F;
	if(ModeType==0x00) data=data|0x60;			// Text Mode
	else if(ModeType<=0x02) data=data|0x00;		// EGA Mode
	else data=data|0xA0;						// VGA Mode
	SetReg1(P3c4,0x21,data);
}

VOID SetVCLKState(PHW_DEVICE_EXTENSION HwDeviceExtension, ULONG ROMAddr, USHORT ModeNo)
{
	USHORT data,data2;
	USHORT VCLK;
	UCHAR  index;

	index=*((UCHAR *)(ROMAddr+REFIndex+0x03));
	index=index&0x03F;
	CRT1VCLKLen=GetVCLKLen(ROMAddr);
	data=index*CRT1VCLKLen;
	VCLKData=*((USHORT *)(ROMAddr+0x208));
	VCLKData=VCLKData+data+(CRT1VCLKLen-2);
	VCLK=*((USHORT *)(ROMAddr+VCLKData));
	if(ModeNo<=0x13) VCLK=0;

	data=GetReg1(P3c4,0x07);
	data=data&0x7B;
	if(VCLK>=150) data=data|0x80;	// VCLK > 150
	SetReg1(P3c4,0x07,data);

	data=GetReg1(P3c4,0x32);
	data=data&0xD7;
	if(VCLK>=150) data=data|0x08;	// VCLK > 150
	SetReg1(P3c4,0x32,data);

	data2=0x03;
	if(VCLK>135) data2=0x02;
	if(VCLK>160) data2=0x01;
	if(VCLK>260) data2=0x00;
	data=GetReg1(P3c4,0x07);
	data=data&0xFC;
	data=data|data2;
	SetReg1(P3c4,0x07,data);
}

VOID LoadDAC(ULONG ROMAddr)
{
	USHORT data,data2;
	USHORT time,i,j,k;
	USHORT m,n,o;
	USHORT si,di,bx,dl;
	USHORT al,ah,dh;
	USHORT *table=VGA_DAC;

	data=*((USHORT *)(ROMAddr+ModeIDOffset+0x01));
	data=data&DACInfoFlag;
	time=64;
	if(data==0x00) table=MDA_DAC;
	if(data==0x08) table=CGA_DAC;
	if(data==0x10) table=EGA_DAC;
	if(data==0x18) {
		time=256;
		table=VGA_DAC;
	}
	if(time==256) j=16;
	else j=time;

	SetReg3(P3c6,0xFF);
	SetReg3(P3c8,0x00);

	for(i=0;i<j;i++) {
		data=table[i];
		for(k=0;k<3;k++) {
			data2=0;
			if(data&0x01) data2=0x2A;
			if(data&0x02) data2=data2+0x15;
			SetReg3(P3c9,data2);
			data=data>>2;
		}
	}

	if(time==256) {
		for(i=16;i<32;i++) {
			data=table[i];
			for(k=0;k<3;k++) SetReg3(P3c9,data);
		}
		si=32;
		for(m=0;m<9;m++) {
			di=si;
			bx=si+0x04;
			dl=0;
			for(n=0;n<3;n++) {
				for(o=0;o<5;o++) {
					dh=table[si];
					ah=table[di];
					al=table[bx];
					si++;
					WriteDAC(dl,ah,al,dh);
				}	
				si=si-2;
				for(o=0;o<3;o++) {
					dh=table[bx];
					ah=table[di];
					al=table[si];
					si--;
					WriteDAC(dl,ah,al,dh);
				}	
				dl++;
			}
			si=si+5;
		}
	}
}

VOID WriteDAC(USHORT dl, USHORT ah, USHORT al, USHORT dh)
{
	USHORT temp;
	USHORT bh,bl;

	bh=ah;
	bl=al;
	if(dl!=0) {
		temp=bh;
		bh=dh;
		dh=temp;
		if(dl==1) {
			temp=bl;
			bl=dh;
			dh=temp;
		}
		else {
			temp=bl;
			bl=bh;
			bh=temp;
		}
	}
	SetReg3(P3c9,(USHORT)dh);
	SetReg3(P3c9,(USHORT)bh);
	SetReg3(P3c9,(USHORT)bl);
}


VOID DisplayOn()
{
	USHORT data;

	data=GetReg1(P3c4,0x01);
	data=data&0xDF;
	SetReg1(P3c4,0x01,data);
}

USHORT GetModeIDLength(ULONG ROMAddr, USHORT ModeNo)
{
	USHORT modeidlength;
	USHORT usModeIDOffset;
	USHORT PreviousWord,CurrentWord;

	modeidlength=0;
	usModeIDOffset=*((USHORT *)(ROMAddr+0x20A));		// Get EModeIDTable
	//	maybe = 2Exx or xx2E		
	CurrentWord=*((USHORT *)(ROMAddr+usModeIDOffset));	// Offset 0x20A
	PreviousWord=*((USHORT *)(ROMAddr+usModeIDOffset-2));	// Offset 0x20A
	while((CurrentWord!=0x2E07)||(PreviousWord!=0x0801)) {
		modeidlength++;
		usModeIDOffset=usModeIDOffset+1;				// 10 <= ExtStructSize
		CurrentWord=*((USHORT *)(ROMAddr+usModeIDOffset));
		PreviousWord=*((USHORT *)(ROMAddr+usModeIDOffset-2)); 
	}
	modeidlength++;
	return(modeidlength);
}

USHORT GetRefindexLength(ULONG ROMAddr, USHORT ModeNo)
{
	UCHAR ModeID;
	UCHAR temp;
	USHORT refindexlength;
	USHORT usModeIDOffset;
	USHORT usREFIndex;
	USHORT usIDLength;

	usModeIDOffset=*((USHORT *)(ROMAddr+0x20A));	// Get EModeIDTable
	ModeID=*((UCHAR *)(ROMAddr+usModeIDOffset));	// Offset 0x20A
	usIDLength = GetModeIDLength(ROMAddr, ModeNo);
	while(ModeID!=0x40) {
		usModeIDOffset=usModeIDOffset+usIDLength; 	// 10 <= ExtStructSize
		ModeID=*((UCHAR *)(ROMAddr+usModeIDOffset));
	}

	refindexlength=1;
	usREFIndex=*((USHORT *)(ROMAddr+usModeIDOffset+0x04));	// si+Ext_point
	usREFIndex++;
	temp=*((UCHAR *)(ROMAddr+usREFIndex));			// di => REFIndex
	while(temp!=0xFF) {
		refindexlength++;
		usREFIndex++;
		temp=*((UCHAR *)(ROMAddr+usREFIndex));		// di => REFIndex
	}
	return(refindexlength);
}

VOID SetInterlace(ULONG ROMAddr, USHORT ModeNo)
{
	ULONG Temp;
	USHORT data,Temp2;

	Temp = (ULONG)GetReg1(P3d4, 0x01);
	Temp++;
	Temp=Temp*8;

	if(Temp==1024) data=0x0035;
	else if(Temp==1280) data=0x0048;
	else data=0x0000;

	Temp2=*((USHORT *)(ROMAddr+REFIndex+0x00));
	Temp2 &= InterlaceMode;
	if(Temp2 == 0) data=0x0000;

	SetReg1(P3d4,0x19,data);

	Temp = (ULONG)GetReg1(P3d4, 0x1A);
	Temp2= (USHORT)(Temp & 0xFC);
	SetReg1(P3d4,0x1A,(USHORT)Temp);

	Temp = (ULONG)GetReg1(P3c4, 0x0f);
	Temp2= (USHORT)Temp & 0xBF;
	if(ModeNo==0x37) Temp2=Temp2|0x40;
	SetReg1(P3d4,0x1A,(USHORT)Temp2);
}

VOID SetCRT1FIFO(ULONG ROMAddr)
{
	USHORT	colorth=0,index,data,VCLK,data2,MCLKOffset,MCLK;
	USHORT  ah,bl,A,B;

	index=*((UCHAR *)(ROMAddr+REFIndex+0x03));
	index=index&0x03F;
	CRT1VCLKLen=GetVCLKLen(ROMAddr);
	data=index*CRT1VCLKLen;
	VCLKData=*((USHORT *)(ROMAddr+0x208));
	VCLKData=VCLKData+data+(CRT1VCLKLen-2);
	VCLK=*((USHORT *)(ROMAddr+VCLKData));		// Get VCLK

	MCLKOffset=*((USHORT *)(ROMAddr+0x20C));
	index=GetReg1(P3c4,0x3A);
	index=index&07;
	MCLKOffset=MCLKOffset+index*5;
	MCLK=*((UCHAR *)(ROMAddr+MCLKOffset+0x03));	// Get MCLK

	data2=ModeType-0x02;
	switch (data2) {
		case 0 : colorth=1; break;
		case 1 : colorth=2; break;
		case 2 : colorth=4; break;
		case 3 : colorth=4; break;
		case 4 : colorth=6; break;
		case 5 : colorth=8; break;
	 }

	do{
		B=(CalcDelay(ROMAddr,0)*VCLK*colorth);
		B=B/(16*MCLK);
		B++;

		A=(CalcDelay(ROMAddr,1)*VCLK*colorth);
		A=A/(16*MCLK);
		A++;

		if(A<4) A=0;
		else A=A-4;

		if(A>B)	bl=A;
		else bl=B;

		bl++;
		if(bl>0x13) {
			data=GetReg1(P3c4,0x16);
			data=data>>6;
			if(data!=0) {
				data--;
				data=data<<6;
				data2=GetReg1(P3c4,0x16);
				data2=(data2&0x3f)|data;
				SetReg1(P3c4,0x16,data2);
			}
			else bl=0x13;
		}
	} while(bl>0x13);

	ah=bl;
	ah=ah<<4;
	ah=ah|0x0f;
	SetReg1(P3c4,0x08,ah);

	data=bl;
	data=data&0x10;
	data=data<<1;
	data2=GetReg1(P3c4,0x0F);
	data2=data2&0x9f;
	data2=data2|data;
	SetReg1(P3c4,0x0F,data2);

	data=bl+3;
	if(data>0x0f) data=0x0f;
	SetReg1(P3c4,0x3b,0x00);
	data2=GetReg1(P3c4,0x09);
	data2=data2&0xF0;
	data2=data2|data;
	SetReg1(P3c4,0x09,data2);
}

static USHORT CalcDelay(ULONG ROMAddr,USHORT key)
{
	USHORT data,data2,temp0,temp1;
	UCHAR  ThLowA[]={61,3,52,5,68,7,100,11,
					 43,3,42,5,54,7, 78,11,
					 34,3,37,5,47,7, 67,11};
	UCHAR  ThLowB[]={81,4,72,6,88,8,120,12,
					 55,4,54,6,66,8, 90,12,
					 42,4,45,6,55,8, 75,12};
	UCHAR  ThTiming[]= {1,2,2,3,0,1,1,2};

	data=GetReg1(P3c4,0x16);
	data=data>>6;
	data2=GetReg1(P3c4,0x14);
	data2=(data2>>4)&0x0C;
	data=data|data2;
	data=data<1;
	if(key==0) {
		temp0=(USHORT)ThLowA[data];
		temp1=(USHORT)ThLowA[data+1];
	}
	else {
		temp0=(USHORT)ThLowB[data];
		temp1=(USHORT)ThLowB[data+1];
	}

	data2=0;
	data=GetReg1(P3c4,0x18);
	if(data&0x02) data2=data2|0x01;
	if(data&0x20) data2=data2|0x02;
	if(data&0x40) data2=data2|0x04;

	data=temp1*ThTiming[data2]+temp0;
	return(data);
}

VOID SetCRT1FIFO2(ULONG ROMAddr)
{
	USHORT  colorth=0,index,data,VCLK,data2,MCLKOffset,MCLK;
	USHORT  ah,bl,B;
	ULONG	eax;

	index=*((UCHAR *)(ROMAddr+REFIndex+0x03));
	index=index&0x03F;
	CRT1VCLKLen=GetVCLKLen(ROMAddr);
	data=index*CRT1VCLKLen;
	VCLKData=*((USHORT *)(ROMAddr+0x208));
	VCLKData=VCLKData+data+(CRT1VCLKLen-2);
	VCLK=*((USHORT *)(ROMAddr+VCLKData));			// Get VCLK

	MCLKOffset=*((USHORT *)(ROMAddr+0x20C));
	index=GetReg1(P3c4,0x1A);
	index=index&07;
	MCLKOffset=MCLKOffset+index*5;
	MCLK=*((USHORT *)(ROMAddr+MCLKOffset+0x03));	// Get MCLK  

	data2=ModeType-0x02;
	switch (data2) {
		case 0 : colorth=1; break;
		case 1 : colorth=1; break;
		case 2 : colorth=2; break;
		case 3 : colorth=2; break;
		case 4 : colorth=3; break;
		case 5 : colorth=4; break;
	}

	do{
		B=(CalcDelay2(ROMAddr,0)*VCLK*colorth);
		if (B%(16*MCLK) == 0)
		{
			B=B/(16*MCLK);
			bl=B+1;
		}
		else
		{
			B=B/(16*MCLK);
			bl=B+2;
		}

		if(bl>0x13) {
			data=GetReg1(P3c4,0x15);
			data=data&0xf0;
			if(data!=0xb0) {
				data=data+0x20;
				if(data==0xa0) data=0x30;

				data2=GetReg1(P3c4,0x15);
				data2=(data2&0x0f)|data;
				SetReg1(P3c4,0x15,data2);
			}
			else bl=0x13;
		}
	} while(bl>0x13);

	data2=GetReg1(P3c4,0x15);
	data2=(data2&0xf0)>>4;
	data2=data2<<24;

	SetReg4(0xcf8,0x80000050);
	eax=GetReg3(0xcfc);
	eax=eax&0x0f0ffffff;
	eax=eax|data2;
	SetReg4(0xcfc,eax);

	ah=bl;
	ah=ah<<4;
	ah=ah|0x0f;
	SetReg1(P3c4,0x08,ah);

	data=bl;
	data=data&0x10;
	data=data<<1;
	data2=GetReg1(P3c4,0x0F);
	data2=data2&0x9f;
	data2=data2|data;
	SetReg1(P3c4,0x0F,data2);

	data=bl+3;
	if(data>0x0f) data=0x0f;
	SetReg1(P3c4,0x3b,0x00);
	data2=GetReg1(P3c4,0x09);
	data2=data2&0xF0;
	data2=data2|data;
	SetReg1(P3c4,0x09,data2);
}

USHORT CalcDelay2(ULONG ROMAddr,USHORT key)
{
	USHORT data,index;
	UCHAR  LatencyFactor[]={88,80,78,72,70,00,
							00,79,77,71,69,49,
							88,80,78,72,70,00,
							00,72,70,64,62,44};

	index=0;
	data=GetReg1(P3c4,0x14);
	if(data&0x80) index=index+12;

	data=GetReg1(P3c4,0x15);
	data=(data&0xf0)>>4;
	if(data&0x01) index=index+6;

	data=data>>1;
	index=index+data;
	data=LatencyFactor[index];

	return(data);
}

#endif /* CONFIG_FB_SIS_LINUXBIOS */
