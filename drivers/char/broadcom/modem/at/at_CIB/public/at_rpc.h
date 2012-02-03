//***************************************************************************
/****************************************************************************
* (c)1999-2011 Broadcom Corporation
*
* Unless you and Broadcom execute a separate written software license agreement governing use of this software,
* this software is licensed to you under the terms of the GNU General Public License version 2,
* available at http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
****************************************************************************/
/**
*
*   @file   at_rpc.h
*
*   @brief  This file contains the function prototypes for the ATC RPC.
*
****************************************************************************/

#ifndef _AT_RPC_H_
#define _AT_RPC_H_

typedef struct 
{
	UInt8			channel;		///< AP Channel Info
	Boolean 			unsolicited;		///< unsolicited flag
	UInt8			sdevID;		///< serial device ID
}AtRegisterInfo_t;

typedef struct
{
	Boolean 		turn_on;		///< on/off
	UInt8		tone_id;			///< tone ID
	UInt32		duration;		///< duration
}AtToneInfo_t;

typedef struct 
{
	UInt8			channel;		///< CP Channel Info
	UInt8 			contextId;		///< context Id
	UInt8			simID;			///< SIM Id
	UInt16			speed;			///< max speed
}AtDUNInfo_t;

typedef struct 
{
	UInt8			channel;		///< AP Channel Info
	Boolean 			flight_mode;		///< unsolicited flag
}AtFlightMode_t;

void AT_InitRpc(void);
Result_t AT_SendRpcMsg(UInt32 tid, UInt8 chan, UInt32 msgId, void *val);
Result_t AT_PostRpcCommandStr(UInt8 chan, const UInt8* atCmdStr);
Result_t AT_RegisterCPTerminal(UInt8 chan, Boolean unsolicited);

Result_t AT_RPCTone(UInt8 chan, Boolean turn_on, UInt8 tone, UInt32 duration);
Result_t AT_RPCAudioPath(UInt8 chan, Boolean turn_on);

Result_t AT_RPC_DUNConnect(UInt8 chan, UInt8 contextId, UInt16 speed);
Result_t AT_RPC_DUNDisconnect(UInt8 chan, UInt8 contextId);
Result_t AT_RPC_APCommandStr(UInt8 chan, const UInt8* atCmdStr);
Result_t AT_RPC_FlightMode(UInt8 chan, Boolean flight_mode);
Result_t  AT_RPC_RegisterAPTerminal(UInt8 chan, UInt8 clientID);

Result_t AT_RPC_PowerReset(UInt8 chan, UInt8 val);
Result_t AT_RPC_LowPowerMode(UInt8 chan);

#endif //_AT_RPC_H_
