/************************************************************************/
/* File iSeries_FlightRecorder.c created by Al Trautman on Jan 22 2001. */
/************************************************************************/
/* This code supports the pci interface on the IBM iSeries systems.     */
/* Copyright (C) 20yy  <Allan H Trautman> <IBM Corp>                    */
/*                                                                      */
/* This program is free software; you can redistribute it and/or modify */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation; either version 2 of the License, or    */
/* (at your option) any later version.                                  */
/*                                                                      */
/* This program is distributed in the hope that it will be useful,      */ 
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU General Public License for more details.                         */
/*                                                                      */
/* You should have received a copy of the GNU General Public License    */ 
/* along with this program; if not, write to the:                       */
/* Free Software Foundation, Inc.,                                      */ 
/* 59 Temple Place, Suite 330,                                          */ 
/* Boston, MA  02111-1307  USA                                          */
/************************************************************************/
/* Change Activity:                                                     */
/*   Created, Jan 22, 2001                                              */
/*   Added Time Stamps, April 12, 2001                                  */
/* End Change Activity                                                  */
/************************************************************************/
#include <linux/config.h>
#include <linux/kernel.h>
#include <asm/iSeries/iSeries_FlightRecorder.h>
#include <linux/rtc.h>
#include <asm/iSeries/mf.h>
/************************************************************************/
/* Log entry into buffer,                                               */
/* ->If entry is going to wrap, log "WRAP" and start at the top.        */
/************************************************************************/
void iSeries_LogFr_Entry(FlightRecorder* Fr, char* LogText) {
    int Residual, TextLen;
    if(Fr->StartingPointer > 0) {	/* Initialized yet?             */
	Residual  = FlightRecorderSize - (Fr->CurrentPointer - Fr->StartingPointer);
	TextLen = strlen(LogText);	/* Length of Text               */
	if(TextLen+16 > Residual) {	/* Room for text or need to wrap*/
	    strcpy(Fr->CurrentPointer,"WRAP");
	    ++Fr->WrapCount;		/* Increment Wraps              */
	    Fr->CurrentPointer  = Fr->StartingPointer;
	}
	strcpy(Fr->CurrentPointer,LogText);
	Fr->CurrentPointer += TextLen+1;
	strcpy(Fr->CurrentPointer,"<=");
    }
}
/************************************************************************/
/* Log entry with time                                                  */
/************************************************************************/
void iSeries_LogFr_Time(FlightRecorder* Fr, char* LogText) {
    struct   rtc_time  Rtc;
    char     LogBuffer[256];
    mf_getRtc(&Rtc);    
    sprintf(LogBuffer,"%02d:%02d:%02d %s",
            Rtc.tm_hour,Rtc.tm_min,Rtc.tm_sec,
            LogText);
    iSeries_LogFr_Entry(Fr,LogBuffer);
}
/************************************************************************/
/* Log Entry with Date and call Time Log                                */
/************************************************************************/
void iSeries_LogFr_Date(FlightRecorder* Fr, char* LogText) {
    struct   rtc_time  Rtc;
    char     LogBuffer[256];
    mf_getRtc(&Rtc);
    sprintf(LogBuffer,"%02d.%02d.%02d %02d:%02d:%02d %s",
            Rtc.tm_year+1900, Rtc.tm_mon, Rtc.tm_mday,
            Rtc.tm_hour,Rtc.tm_min,Rtc.tm_sec,
            LogText);
    iSeries_LogFr_Entry(Fr,LogBuffer);
}
/************************************************************************/
/* Initialized the Flight Recorder                                      */  
/************************************************************************/
void iSeries_Fr_Initialize(FlightRecorder* Fr, char* Signature) {
    if(strlen(Signature) > 16) memcpy(Fr->Signature,Signature,16);
    else                       strcpy(Fr->Signature,Signature);
    Fr->StartingPointer = &Fr->Buffer[0];
    Fr->CurrentPointer  = Fr->StartingPointer;
    Fr->logEntry        = iSeries_LogFr_Entry;
    Fr->logDate         = iSeries_LogFr_Date;
    Fr->logTime         = iSeries_LogFr_Time;
    Fr->logEntry(Fr,"FR Initialized."); /* Note, can't use time yet!   */
}
