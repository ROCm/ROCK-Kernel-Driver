/* DMA.C - DMA functions and definitions */

/* Copyright Galileo Technology. */

/*
DESCRIPTION
This file gives the user a complete interface to the powerful DMA engines,
including functions for controling the priority mechanism.
To fully understand the capabilities of the DMA engines please spare some
time to go trough the spec.
*/

/* includes */

#ifdef __linux__
#include <asm/galileo/evb64120A/core.h>
#include <asm/galileo/evb64120A/dma.h>
#else
#include "Core.h"
#include "DMA.h"
#endif
/********************************************************************
* dmaCommand - Write a command to a DMA channel
*
* Inputs: DMA_ENGINE channel - choosing one of the four engine.
*         unsigned int command - The command to be written to the control register.
* Returns: false if one of the parameters is erroneous else returns true.
*********************************************************************/

bool dmaCommand(DMA_ENGINE channel, unsigned int command)
{
	if (channel > LAST_DMA_ENGINE)
		return false;
	GT_REG_WRITE(CHANNEL0CONTROL + channel * 4, command);
	return true;
}

/********************************************************************
* dmaTransfer - transfer data from sourceAddr to destAddr on DMA channel
* Inputs:
*   DMA_RECORED *nextRecoredPointer: If we are using chain mode DMA transfer,
*   then this pointer should point to the next recored,otherwise it should be
*   NULL.
*   VERY IMPORTANT !!! When using chain mode, the records must be 16 Bytes
*   aligned, the function will take care of that for you, but you need to
*   allocate one more record for that, meaning: if you are having 3 records ,
*   declare 4 (see the example bellow) and start using the second one.
*   Example:
*   Performing a chain mode DMA transfer(Copy a 1/4 mega of data using
*   chain mode DMA):
*    DMA_RECORED dmaRecoredArray[4];
*    dmaRecoredArray[1].ByteCnt = _64KB;
*    dmaRecoredArray[1].DestAdd = destAddress + _64KB;
*    dmaRecoredArray[1].SrcAdd  = sourceAddress + _64KB;
*    dmaRecoredArray[1].NextRecPtr = &dmaRecoredArray[2];
*    dmaRecoredArray[2].ByteCnt = _64KB;
*    dmaRecoredArray[2].DestAdd = destAddress + 2*_64KB;
*    dmaRecoredArray[2].SrcAdd  = sourceAddress + 2*_64KB;
*    dmaRecoredArray[2].NextRecPtr = &dmaRecoredArray[3];
*    dmaRecoredArray[3].ByteCnt = _64KB;
*    dmaRecoredArray[3].DestAdd = destAddress + 3*_64KB;
*    dmaRecoredArray[3].SrcAdd  = sourceAddress + 3*_64KB;
*    dmaRecoredArray[3].NextRecPtr = NULL;
*    performCmDma(0,sourceAddress,destAddress,_64KB,PLAIN,WAIT_TO_END,
*                            &dmaRecoredArray[1]);
* Returns: NO_SUCH_CHANNEL if channel does not exist, CHANNEL_BUSY if channel
*          is active and true if the transfer ended successfully
*********************************************************************/

DMA_STATUS dmaTransfer(DMA_ENGINE channel, unsigned int sourceAddr,
		       unsigned int destAddr, unsigned int numOfBytes,
		       unsigned int command,
		       DMA_RECORED * nextRecoredPointer)
{
	unsigned int tempData, checkBits, alignmentOffset = 0;
	DMA_RECORED *next = nextRecoredPointer;

	if (channel > LAST_DMA_ENGINE)
		return NO_SUCH_CHANNEL;
	if (numOfBytes > 0xffff)
		return GENERAL_ERROR;
	if (isDmaChannelActive(channel))
		return CHANNEL_BUSY;
	if (next != NULL) {	/* case of chain Mode */
		alignmentOffset = ((unsigned int) next % 16);
	}
	checkBits = command & 0x6000000;
	if (checkBits == 0) {
		while (next != NULL) {
			WRITE_WORD((unsigned int) next - alignmentOffset,
				   next->ByteCnt);
			tempData = (unsigned int) next->SrcAdd;
			WRITE_WORD((unsigned int) next + 4 -
				   alignmentOffset, tempData & 0x5fffffff);
			tempData = (unsigned int) next->DestAdd;
			WRITE_WORD((unsigned int) next + 8 -
				   alignmentOffset, tempData & 0x5fffffff);
			tempData = (unsigned int) next->NextRecPtr;
			WRITE_WORD((unsigned int) next + 12 -
				   alignmentOffset,
				   tempData & 0x5fffffff -
				   alignmentOffset);
			next = (DMA_RECORED *) tempData;
			if (next == nextRecoredPointer)
				next = NULL;
		}
	}
	GT_REG_WRITE(CHANNEL0_DMA_BYTE_COUNT + channel * 4, numOfBytes);
	tempData = sourceAddr;
	GT_REG_WRITE(CHANNEL0_DMA_SOURCE_ADDRESS + channel * 4,
		     tempData & 0x5fffffff);
	tempData = destAddr;
	GT_REG_WRITE(CHANNEL0_DMA_DESTINATION_ADDRESS + channel * 4,
		     tempData & 0x5fffffff);
	if (nextRecoredPointer != NULL) {
		tempData =
		    (unsigned int) nextRecoredPointer - alignmentOffset;
		GT_REG_WRITE(CHANNEL0NEXT_RECORD_POINTER + 4 * channel,
			     tempData & 0x5fffffff);
		command = command | CHANNEL_ENABLE;
	} else {
		command = command | CHANNEL_ENABLE | NON_CHAIN_MOD;
	}
	/* Activate DMA engine By writting to dmaControlRegister */
	GT_REG_WRITE(CHANNEL0CONTROL + channel * 4, command);

	return DMA_OK;
}

/********************************************************************
* isDmaChannelActive - check if channel is busy
*
* Inputs: channel number
* RETURNS: True if the channel is busy, false otherwise.
*********************************************************************/

bool isDmaChannelActive(DMA_ENGINE channel)
{
	unsigned int data;

	if (channel > LAST_DMA_ENGINE)
		return false;
	GT_REG_READ(CHANNEL0CONTROL + 4 * channel, &data);
	if (data & DMA_ACTIVITY_STATUS)
		return true;
	else
		return false;
}


/********************************************************************
* changeDmaPriority - update the arbiter`s priority for channels 0-3
*
* Inputs: priority  for channels 0-1, priority  for channels 2-3,
          priority for groups and other priority options
* RETURNS: false if one of the parameters is erroneous and true else
*********************************************************************/

bool changeDmaPriority(PRIO_CHAN_0_1 prio_01, PRIO_CHAN_2_3 prio_23,
		       PRIO_GROUP prioGrp, PRIO_OPT prioOpt)
{
	unsigned int prioReg = 0;

	prioReg = (prio_01 & 0x3) + ((prio_23 & 0x3) << 2) +
	    ((prioGrp & 0x3) << 4) + (prioOpt << 6);
	GT_REG_WRITE(ARBITER_CONTROL, prioReg);
	return true;
}
