/* i2o.c - Drivers for the I2O */

/* Copyright - Galileo technology. */

/*includes*/

#include <linux/module.h>

#ifdef __linux__
#include <asm/galileo-boards/evb64120A/core.h>
#include <asm/galileo-boards/evb64120A/i2o.h>
#else
#include "Core.h"
#include "i2o.h"
#endif

/********************************************************************
* getInBoundMessage - When the GT is configured for I2O support
*                     it can receive a message from an agent on the pci bus.
*                     This message is a 32 bit wide and can be read by
*                     the CPU.
*                     The messaging unit contains two sets of registers
*                     so, actually it can receive a 64 bit message.
*
* INPUTS: I2O_MESSAGE_REG messageRegNum - Selected set (0 or 1) register.
* OUTPUT: N/A.
* RETURNS: Data received from the remote agent.
*********************************************************************/
unsigned int getInBoundMessage(I2O_MESSAGE_REG messageRegNum)
{
	unsigned int regValue;

	GT_REG_READ(INBOUND_MESSAGE_REGISTER0_CPU_SIDE + 4 * messageRegNum,
		    &regValue);
	return (regValue);
}


/********************************************************************
* checkInboundIntAndClear - When a message is received an interrupt is
*                           generated, to enable polling instead the use of
*                           an interrupt handler the user can use this fuction.
*                           You will need to mask the incomming interrupt for
*                           proper use.
*
* INPUTS: I2O_MESSAGE_REG messageRegNum - Selected set (0 or 1) register.
* OUTPUT: N/A.
* RETURNS: true if the corresponding bit in the cause register is set otherwise
*          false.
*********************************************************************/
bool checkInBoundIntAndClear(I2O_MESSAGE_REG messageRegNum)
{
	unsigned int regValue;

	GT_REG_READ(INBOUND_INTERRUPT_CAUSE_REGISTER_CPU_SIDE, &regValue);
	/* clears bit 0 for message register 0 or bit 1 for message register 1 */
	GT_REG_WRITE(INBOUND_INTERRUPT_CAUSE_REGISTER_CPU_SIDE,
		     BIT1 * messageRegNum);
	switch (messageRegNum) {
	case MESSAGE_REG_0:
		if (regValue & BIT0)
			return true;
		break;
	case MESSAGE_REG_1:
		if (regValue & BIT1)
			return true;
		break;
	}
	return false;
}

/********************************************************************
* sendOutBoundMessage - When the GT is configured for I2O support
*                     it can send a message to an agent on the pci bus.
*                     This message is a 32 bit wide and can be read by
*                     the PCI agent.
*                     The messaging unit contains two sets of registers
*                     so, actually it can send a 64 bit message.
*
* INPUTS: I2O_MESSAGE_REG messageRegNum - Selected set (0 or 1) register.
*         unsigned int message - Message to be sent.
* OUTPUT: N/A.
* RETURNS: true.
*********************************************************************/
bool sendOutBoundMessage(I2O_MESSAGE_REG messageRegNum,
			 unsigned int message)
{
	GT_REG_WRITE(OUTBOUND_MESSAGE_REGISTER0_CPU_SIDE +
		     4 * messageRegNum, message);
	return true;
}

/********************************************************************
* checkOutboundInt - When the CPU sends a message to the Outbound
*                    register it generates an interrupt which is refelcted on
*                    the Outbound Interrupt cause register, the interrupt can
*                    be cleard only by the PCI agent which read the message.
*                    After sending the message you can acknowledge it by
*                    monitoring the corresponding bit in the cause register.
*
* INPUTS: I2O_MESSAGE_REG messageRegNum - Selected set (0 or 1) register.
* OUTPUT: N/A.
* RETURNS: true if the corresponding bit in the cause register is set otherwise
*          false.
*********************************************************************/
bool outBoundMessageAcknowledge(I2O_MESSAGE_REG messageRegNum)
{
	unsigned int regValue;

	GT_REG_READ(OUTBOUND_INTERRUPT_CAUSE_REGISTER_CPU_SIDE, &regValue);
	switch (messageRegNum) {
	case MESSAGE_REG_0:
		if (regValue & BIT0)
			return true;
		break;
	case MESSAGE_REG_1:
		if (regValue & BIT1)
			return true;
		break;
	}
	return false;
}

/********************************************************************
* maskInBoundMessageInterrupt - Mask the inbound interrupt, when masking
*                               the interrupt you can work in polling mode
*                               using the checkInboundIntAndClear function.
*
* INPUTS: I2O_MESSAGE_REG messageRegNum - Selected set (0 or 1) register.
* OUTPUT: N/A.
* RETURNS: true.
*********************************************************************/
bool maskInBoundMessageInterrupt(I2O_MESSAGE_REG messageRegNum)
{
	switch (messageRegNum) {
	case MESSAGE_REG_0:
		SET_REG_BITS(INBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE,
			     BIT0);
		break;
	case MESSAGE_REG_1:
		SET_REG_BITS(INBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE,
			     BIT1);
		break;
	}
	return true;
}

/********************************************************************
* enableInBoundMessageInterrupt - unMask the inbound interrupt.
*
* INPUTS: I2O_MESSAGE_REG messageRegNum - Selected set (0 or 1) register.
* OUTPUT: N/A.
* RETURNS: true.
*********************************************************************/
bool enableInBoundMessageInterrupt(I2O_MESSAGE_REG messageRegNum)
{
	switch (messageRegNum) {
	case MESSAGE_REG_0:
		RESET_REG_BITS(INBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE,
			       BIT0);
		break;
	case MESSAGE_REG_1:
		RESET_REG_BITS(INBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE,
			       BIT1);
		break;
	}
	return true;
}

/********************************************************************
* maskOutboundMessageInterrupt - Mask the out bound interrupt, when doing so
*                           the PCI agent needs to poll on the interrupt
*                           cause register to monitor an incoming message.
*
* INPUTS: I2O_MESSAGE_REG messageRegNum - Selected set (0 or 1) register.
* OUTPUT: N/A.
* RETURNS: true.
*********************************************************************/
bool maskOutBoundMessageInterrupt(I2O_MESSAGE_REG messageRegNum)
{
	switch (messageRegNum) {
	case MESSAGE_REG_0:
		SET_REG_BITS(OUTBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE,
			     BIT0);
		break;
	case MESSAGE_REG_1:
		SET_REG_BITS(OUTBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE,
			     BIT1);
		break;
	}
	return true;
}

/********************************************************************
* enableOutboundMessageInterrupt - Mask the out bound interrupt, when doing so
*                           the PCI agent needs to poll on the interrupt
*                           cause register to monitor an incoming message.
*
* INPUTS: I2O_MESSAGE_REG messageRegNum - Selected set (0 or 1) register.
* OUTPUT: N/A.
* RETURNS: true.
*********************************************************************/
bool enableOutBoundMessageInterrupt(I2O_MESSAGE_REG messageRegNum)
{
	switch (messageRegNum) {
	case MESSAGE_REG_0:
		RESET_REG_BITS(OUTBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE,
			       BIT0);
		break;
	case MESSAGE_REG_1:
		RESET_REG_BITS(OUTBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE,
			       BIT1);
		break;
	}
	return true;
}

/********************************************************************
* initiateOutBoundDoorBellInt - Setting a bit in this register to '1' by the
*                       CPU generates a PCI interrupt (if it is not masked by
*                       the Outbound interrupt Mask register)
*                       Only the PCI agent which recieved the interrupt can
*                       clear it, only after clearing all the bits the
*                       interrupt will be de-asserted.
*
* INPUTS: unsigned int data - Requested interrupt bits.
* OUTPUT: N/A.
* RETURNS: true.
*********************************************************************/
bool initiateOutBoundDoorBellInt(unsigned int data)
{
	GT_REG_WRITE(OUTBOUND_DOORBELL_REGISTER_CPU_SIDE, data);
	return true;
}

/********************************************************************
* readInBoundDoorBellInt - Read the in bound door bell interrupt cause
*                          register.
*
* OUTPUT:  N/A.
* RETURNS: The 32 bit interrupt cause register.
*********************************************************************/
unsigned int readInBoundDoorBellInt()
{
	unsigned int regData;
	GT_REG_READ(INBOUND_DOORBELL_REGISTER_CPU_SIDE, &regData);
	return regData;
}

/********************************************************************
* clearInBoundDoorBellInt - An interrupt generated by a PCI agent through
*                           the in bound door bell mechanisem can be cleared
*                           only by the CPU. The interrupt will be de-asserted
*                           only if all the bits which where set by the PCI
*                           agent are cleared.
*
* INPUTS:  unsigned int data - Bits to be cleared.
* OUTPUT:  N/A.
* RETURNS: true.
*********************************************************************/
bool clearInBoundDoorBellInt(unsigned int data)
{
	GT_REG_WRITE(INBOUND_DOORBELL_REGISTER_CPU_SIDE, data);
	return true;
}

/********************************************************************
* isInBoundDoorBellInterruptSet - Check if Inbound Doorbell Interrupt is set,
*                                 can be used for polling mode.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: true if the corresponding bit in the cause register is set otherwise
*          false.
*********************************************************************/
bool isInBoundDoorBellInterruptSet()
{
	unsigned int regData;

	GT_REG_READ(INBOUND_INTERRUPT_CAUSE_REGISTER_CPU_SIDE, &regData);
	return (regData & BIT2);
}

/********************************************************************
* isOutBoundDoorBellInterruptSet - Check if out bound Doorbell Interrupt is
*                                  set, can be used for acknowledging interrupt
*                                  handling by the agent who recieived the
*                                  interrupt.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: true if the corresponding bit in the cause register is set otherwise
*          false.
*********************************************************************/
bool isOutBoundDoorBellInterruptSet()
{
	unsigned int regData;

	GT_REG_READ(OUTBOUND_INTERRUPT_CAUSE_REGISTER_CPU_SIDE, &regData);
	return (regData & BIT2);
}

/********************************************************************
* maskInboundDoorBellInterrupt - Mask the Inbound Doorbell Interrupt.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: true.
*********************************************************************/
bool maskInBoundDoorBellInterrupt()
{
	SET_REG_BITS(INBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE, BIT2);
	return true;
}

/********************************************************************
* enableInboundDoorBellInterrupt - unMask the Inbound Doorbell Interrupt.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: true.
*********************************************************************/
bool enableInBoundDoorBellInterrupt()
{
	RESET_REG_BITS(INBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE, BIT2);
	return true;
}

/********************************************************************
* maskOutboundDoorBellInterrupt - Mask the Outbound Doorbell Interrupt.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: true.
*********************************************************************/
bool maskOutBoundDoorBellInterrupt()
{
	SET_REG_BITS(OUTBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE, BIT2);
	return true;
}

/********************************************************************
* enableOutboundDoorBellInterrupt - unMask the Outbound Doorbell Interrupt.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: true.
*********************************************************************/
bool enableOutBoundDoorBellInterrupt()
{
	RESET_REG_BITS(OUTBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE, BIT2);
	return true;
}

/********************************************************************
* circularQueueEnable - Initialize the I2O messaging mechanism.
*
* INPUTS:   CIRCULE_QUEUE_SIZE cirQueSize - Bits 5:1 in the:
*           Queue Control Register, Offset 0x50 (0x1c50).
*           Defines the queues size (refer to the data sheet
*           for more information)
*          unsigned int queueBaseAddr - The base address for the first queue.
*           The other queues base Address will be determined as follows:
*           Inbound Free = queueBaseAddr
*           Inbound Post = queueBaseAddr + cirQueSize
*           Outbound Post = queueBaseAddr + cirQueSize
*
* OUTPUT:  N/A.
* RETURNS: true.
*
*  The Circular Queue Starting Addresses as written in the spec:
*  ----------------------------------------
*  |    Queue       |  Starting Address   |
*  |----------------|---------------------|
*  | Inbound Free   |       QBAR          |
*  | Inbound Post   | QBAR + Queue Size   |
*  | Outbound Post  | QBAR + 2*Queue Size |
*  | Outbound Free  | QBAR + 3*Queue Size |
*  ----------------------------------------
*********************************************************************/
bool circularQueueEnable(CIRCULAR_QUEUE_SIZE cirQueSize,
			 unsigned int queueBaseAddr)
{
	unsigned int regData;

	regData = BIT0 | (cirQueSize << 1);
	/* Enable Queue Operation */
	GT_REG_WRITE(QUEUE_CONTROL_REGISTER_CPU_SIDE, regData);
	/* Writing The base Address for the 4 Queues */
	GT_REG_WRITE(QUEUE_BASE_ADDRESS_REGISTER_CPU_SIDE, queueBaseAddr);
	/* Update The Inbound Free Queue Base Address, offset=0 */
	GT_REG_WRITE(INBOUND_FREE_HEAD_POINTER_REGISTER_CPU_SIDE, 0);
	GT_REG_WRITE(INBOUND_FREE_TAIL_POINTER_REGISTER_CPU_SIDE, 0);
	/* Update The Inbound Post Queue Base Address, offset=_16K*cirQueSize */
	GT_REG_WRITE(INBOUND_POST_HEAD_POINTER_REGISTER_CPU_SIDE,
		     _16K * cirQueSize);
	GT_REG_WRITE(INBOUND_POST_TAIL_POINTER_REGISTER_CPU_SIDE,
		     _16K * cirQueSize);
	/* Update The Outbound Post Queue Base Address, offset=2*_16K*cirQueSize */
	GT_REG_WRITE(OUTBOUND_POST_HEAD_POINTER_REGISTER_CPU_SIDE,
		     2 * _16K * cirQueSize);
	GT_REG_WRITE(OUTBOUND_POST_TAIL_POINTER_REGISTER_CPU_SIDE,
		     2 * _16K * cirQueSize);
	/* Update The Outbound Free Queue Base Address, offset=3*_16K*cirQueSize */
	GT_REG_WRITE(OUTBOUND_FREE_HEAD_POINTER_REGISTER_CPU_SIDE,
		     3 * _16K * cirQueSize);
	GT_REG_WRITE(OUTBOUND_FREE_TAIL_POINTER_REGISTER_CPU_SIDE,
		     3 * _16K * cirQueSize);
	return true;
}

/********************************************************************
* inBoundPostQueuePop - Two actions are being taken upon pop:
*           1) Getting out the data from the Queue`s head.
*           2) Increment the tail pointer in a cyclic way (The HEAD is
*              incremented automaticaly by the GT)
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: Data pointed by tail.
*********************************************************************/
unsigned int inBoundPostQueuePop()
{
	unsigned int tailAddrPointer;
	unsigned int data;
	unsigned int cirQueSize;
	unsigned int qBar;
	unsigned int inBoundPostQbase;

	/* Gets the Inbound Post TAIL pointer */
	GT_REG_READ(INBOUND_POST_TAIL_POINTER_REGISTER_CPU_SIDE,
		    &tailAddrPointer);
	/* Gets the Data From the pointer Address */
	READ_WORD(tailAddrPointer, &data);
	/* incrementing head process: */
	/* Gets the fifo's base Address */
	GT_REG_READ(QUEUE_BASE_ADDRESS_REGISTER_CPU_SIDE, &qBar);
	qBar = qBar & 0xfff00000;
	/* Gets the fifo's size */
	GT_REG_READ(QUEUE_CONTROL_REGISTER_CPU_SIDE, &cirQueSize);
	cirQueSize = 0x1f && (cirQueSize >> 1);
	/* calculating The Inbound Post Queue Base Address */
	inBoundPostQbase = qBar + 1 * cirQueSize * _16K;
	/* incrementing Inbound Post queue TAIL in a cyclic loop */
	tailAddrPointer = inBoundPostQbase + ((tailAddrPointer + 4) %
					      (_16K * cirQueSize));
	/* updating the pointer back to INBOUND_POST_TAIL_POINTER_REGISTER */
	GT_REG_WRITE(INBOUND_POST_TAIL_POINTER_REGISTER_CPU_SIDE,
		     tailAddrPointer);
	return data;
}

/********************************************************************
* isInBoundPostQueueInterruptSet - Check if in bound interrupt is set.
*                                  can be used for polling mode.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: true if the corresponding bit in the cause register is set otherwise
*          false.
*********************************************************************/
bool isInBoundPostQueueInterruptSet()
{
	unsigned int regData;

	GT_REG_READ(INBOUND_INTERRUPT_CAUSE_REGISTER_CPU_SIDE, &regData);
	return (regData & BIT4);	/* if set return '1' (true), else '0' (false) */
}

/********************************************************************
* clearInBoundPostQueueInterrupt - Clears the Post queue interrupt.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: true.
*********************************************************************/
bool clearInBoundPostQueueInterrupt()
{
	GT_REG_WRITE(INBOUND_INTERRUPT_CAUSE_REGISTER_CPU_SIDE, BIT4);
	return true;
}

/********************************************************************
* maskInBoundPostQueueInterrupt - Mask the inbound interrupt, when masking
*                                 the interrupt you can work in polling mode.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS:
*********************************************************************/
void maskInBoundPostQueueInterrupt()
{
	unsigned int regData;

	GT_REG_READ(INBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE, &regData);
	GT_REG_WRITE(INBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE,
		     regData | BIT4);

}

/********************************************************************
* enableInBoundPostQueueInterrupt - Enable interrupt when ever there is a new
*                                   message from the PCI agent.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS:
*********************************************************************/
void enableInBoundPostQueueInterrupt()
{
	unsigned int regData;

	GT_REG_READ(INBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE, &regData);
	GT_REG_WRITE(INBOUND_INTERRUPT_MASK_REGISTER_CPU_SIDE,
		     regData & 0xfffffffb);
}

/********************************************************************
* inBoundFreeQueuePush - Two actions are being taken upon push:
*           1) Place the user`s data on the Queue`s head.
*           2) Increment the haed pointer in a cyclic way (The tail is
*              decremented automaticaly by the GT)
*
* INPUTS:  unsigned int data - Data to be placed in the queue.
* OUTPUT:  N/A.
* RETURNS: true.
*********************************************************************/
bool inBoundFreeQueuePush(unsigned int data)
{
	unsigned int headPointer;
	unsigned int cirQueSize;
	unsigned int qBar;
	unsigned int inBoundFreeQbase;

	GT_REG_READ(INBOUND_FREE_HEAD_POINTER_REGISTER_CPU_SIDE,
		    &headPointer);
	/* placing the data in the queue */
	WRITE_WORD(headPointer, data);
	/* incrementing head process: */
	/* Gets the fifo's base Address */
	GT_REG_READ(QUEUE_BASE_ADDRESS_REGISTER_CPU_SIDE, &qBar);
	qBar = qBar & 0xfff00000;
	/* Gets the fifo's size */
	GT_REG_READ(QUEUE_CONTROL_REGISTER_CPU_SIDE, &cirQueSize);
	cirQueSize = 0x1f && (cirQueSize >> 1);
	/* calculating The Inbound Free Queue Base Address */
	inBoundFreeQbase = qBar;
	/* incrementing Inbound Free queue HEAD in a cyclic loop */
	headPointer =
	    inBoundFreeQbase + ((headPointer + 4) % (_16K * cirQueSize));
	/* updating the pointer back to OUTBOUND_POST_HEAD_POINTER_REGISTER */
	GT_REG_WRITE(INBOUND_FREE_HEAD_POINTER_REGISTER_CPU_SIDE,
		     headPointer);
	return true;
}

/********************************************************************
* isInBoundFreeQueueEmpty - Check if Inbound Free Queue Empty.
*                           Can be used for acknowledging the messages
*                           being sent by us to the PCI agent.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: true if the queue is empty , otherwise false.
*********************************************************************/
bool isInBoundFreeQueueEmpty()
{
	unsigned int inBoundFreeQueHead;
	unsigned int inBoundFreeQueTail;

	GT_REG_READ(INBOUND_FREE_HEAD_POINTER_REGISTER_CPU_SIDE,
		    &inBoundFreeQueHead);
	GT_REG_READ(INBOUND_FREE_TAIL_POINTER_REGISTER_CPU_SIDE,
		    &inBoundFreeQueTail);
	if (inBoundFreeQueHead == inBoundFreeQueTail) {
		return true;
	} else
		return false;
}

/********************************************************************
* outBoundPostQueuePush  - Two actions are being taken upon push:
*           1) Place the user`s data on the Queue`s head.
*           2) Increment the haed pointer in a cyclic way (The tail is
*              decremented automaticaly by the GT when the Agent on the
*              PCI have read data from the Outbound Port).
*
* INPUTS:  unsigned int data - Data to be placed in the queue`s head.
* OUTPUT:  N/A.
* RETURNS: true.
*********************************************************************/
bool outBoundPostQueuePush(unsigned int data)
{
	unsigned int headPointer;
	unsigned int cirQueSize;
	unsigned int qBar;
	unsigned int outBoundPostQbase;

	GT_REG_READ(OUTBOUND_POST_HEAD_POINTER_REGISTER_CPU_SIDE,
		    &headPointer);
	/* placing the data in the queue (where the head point to..) */
	WRITE_WORD(headPointer, data);
	/* incrementing head process: */
	/* Gets the fifo's base Address */
	GT_REG_READ(QUEUE_BASE_ADDRESS_REGISTER_CPU_SIDE, &qBar);
	qBar = qBar & 0xfff00000;
	/* Gets the fifo's size */
	GT_REG_READ(QUEUE_CONTROL_REGISTER_CPU_SIDE, &cirQueSize);
	cirQueSize = 0x1f && (cirQueSize >> 1);
	/* calculating The Outbound Post Queue Base Address */
	outBoundPostQbase = qBar + 2 * cirQueSize * _16K;
	/* incrementing Outbound Post queue in a cyclic loop */
	headPointer =
	    outBoundPostQbase + ((headPointer + 4) % (_16K * cirQueSize));
	/* updating the pointer back to OUTBOUND_POST_HEAD_POINTER_REGISTER */
	GT_REG_WRITE(OUTBOUND_POST_HEAD_POINTER_REGISTER_CPU_SIDE,
		     headPointer);
	return true;
}

/********************************************************************
* isOutBoundPostQueueEmpty - Check if Outbound Post Queue Empty.
*                            Can be used for acknowledging the messages
*                            being sent by us to the PCI agent.
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: true if the queue is empty , otherwise false.
*********************************************************************/
bool isOutBoundPostQueueEmpty()
{
	unsigned int outBoundPostQueHead;
	unsigned int outBoundPostQueTail;

	GT_REG_READ(INBOUND_FREE_HEAD_POINTER_REGISTER_CPU_SIDE,
		    &outBoundPostQueHead);
	GT_REG_READ(INBOUND_FREE_TAIL_POINTER_REGISTER_CPU_SIDE,
		    &outBoundPostQueTail);
	if (outBoundPostQueHead == outBoundPostQueTail) {
		return true;
	} else
		return false;
}

/********************************************************************
* outBoundFreeQueuePop - Two actions are being taken upon pop:
*           1) Getting out the data from the Queue`s head.
*           2) Increment the tail pointer in a cyclic way (The HEAD is
*              incremented automaticaly by the GT)
*
* INPUTS:  N/A.
* OUTPUT:  N/A.
* RETURNS: Data pointed by tail.
*********************************************************************/
unsigned int outBoundFreeQueuePop()
{
	unsigned int tailAddrPointer;
	unsigned int data;
	unsigned int cirQueSize;
	unsigned int qBar;
	unsigned int outBoundFreeQbase;

	/* Gets the Inbound Post TAIL pointer */
	GT_REG_READ(OUTBOUND_FREE_TAIL_POINTER_REGISTER_CPU_SIDE,
		    &tailAddrPointer);
	/* Gets the Data From the pointer Address */
	READ_WORD(tailAddrPointer, &data);
	/* incrementing head process: */
	/* Gets the fifo's base Address */
	GT_REG_READ(QUEUE_BASE_ADDRESS_REGISTER_CPU_SIDE, &qBar);
	qBar = qBar & 0xfff00000;
	/* Gets the fifo's size */
	GT_REG_READ(QUEUE_CONTROL_REGISTER_CPU_SIDE, &cirQueSize);
	cirQueSize = 0x1f && (cirQueSize >> 1);
	/* calculating The Inbound Post Queue Base Address */
	outBoundFreeQbase = qBar + 3 * cirQueSize * _16K;
	/* incrementing Outbound Free queue TAlL in a cyclic loop */
	tailAddrPointer = outBoundFreeQbase + ((tailAddrPointer + 4) %
					       (_16K * cirQueSize));
	/* updating the pointer back to OUTBOUND_FREE_TAIL_POINTER_REGISTER */
	GT_REG_WRITE(OUTBOUND_FREE_TAIL_POINTER_REGISTER_CPU_SIDE,
		     tailAddrPointer);
	return data;
}


EXPORT_SYMBOL(isInBoundDoorBellInterruptSet);
EXPORT_SYMBOL(initiateOutBoundDoorBellInt);
EXPORT_SYMBOL(clearInBoundDoorBellInt);
