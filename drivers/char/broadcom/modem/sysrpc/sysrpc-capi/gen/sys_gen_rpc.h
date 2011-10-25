/****************************************************************************
*																			
*     Copyright (c) 2007-2008 Broadcom Corporation								
*																			
*   Unless you and Broadcom execute a separate written software license		
*   agreement governing use of this software, this software is licensed to you	
*   under the terms of the GNU General Public License version 2, available	
*    at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").	
*																			
*   Notwithstanding the above, under no circumstances may you combine this	
*   software in any way with any other Broadcom software provided under a license 
*   other than the GPL, without Broadcom's express prior written consent.	
*																			
****************************************************************************/
/****************************************************************************
*																			
*     WARNING!!!! Generated File ( Do NOT Modify !!!! )					
*																			
****************************************************************************/
#ifndef SYS_GEN_MSG_H
#define SYS_GEN_MSG_H


//***************** < 1 > **********************






typedef struct
{
	PMU_SIMLDO_t  simldo;
}CAPI2_SYSRPC_PMU_IsSIMReady_Req_t;

typedef struct
{
	Boolean	val;
}CAPI2_SYSRPC_PMU_IsSIMReady_Rsp_t;

typedef struct
{
	PMU_SIMLDO_t  simldo;
	PMU_SIMVolt_t  volt;
}CAPI2_SYSRPC_PMU_ActivateSIM_Req_t;

//***************** < 2 > **********************






bool_t xdr_CAPI2_SYSRPC_PMU_IsSIMReady_Req_t(void* xdrs, CAPI2_SYSRPC_PMU_IsSIMReady_Req_t *rsp);
bool_t xdr_CAPI2_SYSRPC_PMU_IsSIMReady_Rsp_t(void* xdrs, CAPI2_SYSRPC_PMU_IsSIMReady_Rsp_t *rsp);
bool_t xdr_CAPI2_SYSRPC_PMU_ActivateSIM_Req_t(void* xdrs, CAPI2_SYSRPC_PMU_ActivateSIM_Req_t *rsp);

//***************** < 3 > **********************






Result_t Handle_CAPI2_SYSRPC_PMU_IsSIMReady(RPC_Msg_t* pReqMsg, PMU_SIMLDO_t simldo);
Result_t Handle_CAPI2_SYSRPC_PMU_ActivateSIM(RPC_Msg_t* pReqMsg, PMU_SIMLDO_t simldo, PMU_SIMVolt_t volt);

//***************** < 12 > **********************





//***************************************************************************************
/**
	Function response for the CAPI2_SYSRPC_PMU_IsSIMReady
	@param		tid (in) Unique exchange/transaction id which is passed in the request
	@param		clientID (in) Client ID
	@param		simldo(in) param of type PMU_SIMLDO_t
	@return		Not Applicable
	@note
	Payload: Boolean
	@n Response to CP will be notified via ::MSG_PMU_IS_SIM_READY_RSP
**/
void CAPI2_SYSRPC_PMU_IsSIMReady(UInt32 tid, UInt8 clientID, PMU_SIMLDO_t simldo);

//***************************************************************************************
/**
	Function response for the CAPI2_SYSRPC_PMU_ActivateSIM
	@param		tid (in) Unique exchange/transaction id which is passed in the request
	@param		clientID (in) Client ID
	@param		simldo(in) param of type PMU_SIMLDO_t
	@param		volt(in) param of type PMU_SIMVolt_t
	@return		Not Applicable
	@note
	Payload: default_proc
	@n Response to CP will be notified via ::MSG_PMU_ACTIVATE_SIM_RSP
**/
void CAPI2_SYSRPC_PMU_ActivateSIM(UInt32 tid, UInt8 clientID, PMU_SIMLDO_t simldo, PMU_SIMVolt_t volt);


//***************** < 16 > **********************



#endif
