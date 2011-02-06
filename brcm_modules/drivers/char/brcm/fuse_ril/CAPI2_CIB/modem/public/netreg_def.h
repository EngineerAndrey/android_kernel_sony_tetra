//***************************************************************************
//
//	Copyright � 2002-2008 Broadcom Corporation
//	
//	This program is the proprietary software of Broadcom Corporation 
//	and/or its licensors, and may only be used, duplicated, modified 
//	or distributed pursuant to the terms and conditions of a separate, 
//	written license agreement executed between you and Broadcom (an 
//	"Authorized License").  Except as set forth in an Authorized 
//	License, Broadcom grants no license (express or implied), right 
//	to use, or waiver of any kind with respect to the Software, and 
//	Broadcom expressly reserves all rights in and to the Software and 
//	all intellectual property rights therein.  IF YOU HAVE NO 
//	AUTHORIZED LICENSE, THEN YOU HAVE NO RIGHT TO USE THIS SOFTWARE 
//	IN ANY WAY, AND SHOULD IMMEDIATELY NOTIFY BROADCOM AND DISCONTINUE 
//	ALL USE OF THE SOFTWARE.  
//	
//	Except as expressly set forth in the Authorized License,
//	
//	1.	This program, including its structure, sequence and 
//		organization, constitutes the valuable trade secrets 
//		of Broadcom, and you shall use all reasonable efforts 
//		to protect the confidentiality thereof, and to use 
//		this information only in connection with your use 
//		of Broadcom integrated circuit products.
//	
//	2.	TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE 
//		IS PROVIDED "AS IS" AND WITH ALL FAULTS AND BROADCOM 
//		MAKES NO PROMISES, REPRESENTATIONS OR WARRANTIES, 
//		EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, 
//		WITH RESPECT TO THE SOFTWARE.  BROADCOM SPECIFICALLY 
//		DISCLAIMS ANY AND ALL IMPLIED WARRANTIES OF TITLE, 
//		MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A 
//		PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR 
//		COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR 
//		CORRESPONDENCE TO DESCRIPTION. YOU ASSUME THE ENTIRE 
//		RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE.  
//
//	3.	TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT 
//		SHALL BROADCOM OR ITS LICENSORS BE LIABLE FOR 
//		(i) CONSEQUENTIAL, INCIDENTAL, SPECIAL, INDIRECT, OR 
//		EXEMPLARY DAMAGES WHATSOEVER ARISING OUT OF OR IN ANY 
//		WAY RELATING TO YOUR USE OF OR INABILITY TO USE THE 
//		SOFTWARE EVEN IF BROADCOM HAS BEEN ADVISED OF THE 
//		POSSIBILITY OF SUCH DAMAGES; OR (ii) ANY AMOUNT IN 
//		EXCESS OF THE AMOUNT ACTUALLY PAID FOR THE SOFTWARE 
//		ITSELF OR U.S. $1, WHICHEVER IS GREATER. THESE 
//		LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY FAILURE 
//		OF ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.
//
//***************************************************************************
/**
*
*   @file   netreg_def.h
*
*   @brief  This file contains definitions for the type for PCH 
*			(GPRS PDP Context Handler) API.
*
****************************************************************************/

#ifndef _NETREG_DEF_
#define _NETREG_DEF_

#include "rtc.h"

 typedef enum
{
	PLMN_DEREG,				///< is doing deregistration
	PLMN_MANUAL,			///< is doing manual registration
	PLMN_AUTO,				///< is doing auto registration
	PLMN_NONE										
}PLMNSession_t;
 
/// PDP REG Type
typedef enum 
{
	REG_GPRS_ONLY,	///<  register gprs only
	REG_GSM_ONLY,	///< register gsm only
	REG_BOTH		///< register both
}RegType_t;

/// PCH MS Class
typedef UInt8		MSClass_t; ///< valid values: GPRS_CLASS_A .. GPRS_CLASS_CG, GPRS_NO_CLASS

/// PLMN Select Mode
typedef enum
{
	PLMN_SELECT_AUTO				= 0,	///< automatic
	PLMN_SELECT_MANUAL				= 1,	///< manual
	PLMN_SELECT_DEREG				= 2,	///< deregister from network
	PLMN_SELECT_SET_ONLY			= 3,	///< set only
	PLMN_SELECT_MANUAL_AUTO			= 4,	///< if manual selection fails, automatic mode is entered
	PLMN_SELECT_MANUAL_FORCE_AUTO	= 5,	///< After manual selection finish, switch to Automatic mode
	PLMN_SELECT_USER_RESELECTION	= 6,	///< Trigger a plmn reselection in the current mode
	PLMN_SELECT_INVALID				= 7		///< Invalid Plmn selection
}PlmnSelectMode_t;


/// PLMN Select Format
typedef enum
{
	PLMN_FORMAT_LONG			= 0,	///< long format alphanumeric
	PLMN_FORMAT_SHORT			= 1,	///< short format alphanumeric
	PLMN_FORMAT_NUMERIC			= 2,	///< numeric
	PLMN_FORMAT_INVALID			= 3		///< Invalid
}PlmnSelectFormat_t;


/// Band select info
typedef enum
{
	BAND_NULL									= 0,			///< 0
	BAND_AUTO									= 0x0001,		///< 0 0000 0001
	BAND_GSM900_ONLY							= 0x0002,		///< 0 0000 0010
	BAND_DCS1800_ONLY							= 0x0004,		///< 0 0000 0100
	BAND_GSM900_DCS1800							= 0x0006,		///< 0 0000 0110
	BAND_PCS1900_ONLY							= 0x0008,		///< 0 0000 1000
	BAND_GSM850_ONLY							= 0x0010,		///< 0 0001 0000
	BAND_PCS1900_GSM850							= 0x0018,		///< 0 0001 1000
	BAND_ALL_GSM								= 0x001E,		///< 0 0001 1110 All GSM band(900/1800/850/1900)
	BAND_UMTS2100_ONLY							= 0x0020,		///< 0 0010 0000
	BAND_GSM900_DCS1800_UMTS2100				= 0x0026,		///< 0 0010 0110
	BAND_UMTS1900_ONLY							= 0x0040,		///< 0 0100 0000
	BAND_PCS1900_GSM850_UMTS1900_UMTS850		= 0x01D8,		///< 1 1101 1000 Add 1700 to this group temporarily
	BAND_UMTS850_ONLY							= 0x0080,		///< 0 1000 0000
	BAND_UMTS1700_ONLY							= 0x0100,		///< 1 0000 0000
 	BAND_UMTS900_ONLY          					= 0x0200,       ///< 10 0000 0000 - GNATS TR 13152 - Band 8
 	BAND_UMTS1800_ONLY             				= 0x0400,       ///< 100 0000 0000 - GNATS TR 13152 - Band 3
 	BAND_ALL_UMTS              					= 0x07E0,		///< 111 1110 0000 All UMTS band(2100/1900/850/1700/900/1800)
 	BAND_ALL                   					= 0x07FF	    ///< 111 1111 1111
} BandSelect_t;

/// RAT select info
typedef enum
{
	GSM_ONLY=0,				///< GSM Only
	DUAL_MODE_GSM_PREF=1,	///< GSM Prefer
	DUAL_MODE_UMTS_PREF=2,	///< UMTS prefer
	UMTS_ONLY=3,			///< UMTS only
	INVALID_RAT				///< Invalid RAT
} RATSelect_t;

///GAN select info
typedef enum
{
	GERAN_ONLY=0, 			///< GERN/UTRAN/E-UTRAN only
	GAN_ONLY=1,			///< GAN only
	GERAN_PREF=2,			///< GERN/UTRAN/E-UTRAN preferred
	GAN_PREF=3,			///< GAN preferred
	INVALID_GAN_SELECT	///< Invalid GERAN/GAN preferrence 
}GANSelect_t;

typedef enum
{
	GERAN_GAN_NOT_AVAILABLE=0,
	GERAN_USED=1,
	GAN_USED=2

}GANStatus_t;
///	Mobile Registration States
typedef enum 
{
	REG_STATE_NO_SERVICE,			///< GPRS service is not available.
	REG_STATE_NORMAL_SERVICE,		///< Mobile is in normal service, GPRS attached
	REG_STATE_SEARCHING,			///< Mobile is searching for network to camp.Services are not yet available.
	REG_STATE_LIMITED_SERVICE,		///< Mobile is in limited service - only Emergency calls are allowed. GPRS is available but not attached.	
	REG_STATE_UNKNOWN,				///< Unknown state. 
	REG_STATE_ROAMING_SERVICE,		///< Mobile is roaming. 
	REG_STATE_NO_CHANGE				///< No change in state from previously reported.
} MSRegState_t;

///	Phone Registration Info: MCC/MNC/LAC/Cell_ID/RAT/Band value valid only if GSM or GPRS is in Normal Service or Limited Service.
typedef struct {
	MSRegState_t	gsm_reg_state;		///< GSM Registration state
	MSRegState_t	gprs_reg_state;		///< GPRS Registration state
	UInt16			mcc;				///< MCC in Raw format (may include 3rd MNC digit), e.g. 0x13F0 for AT&T in Sunnyvale, CA
	UInt8			mnc;				///< MNC in Raw format, e.g. 0x71 for AT&T in Sunnyvale, CA
	UInt16			lac;				///< Location Area Code
	UInt16			cell_id;			///< Cell ID
	UInt8			rat;				///< Current Radio Access Technology, RAT_NOT_AVAILABLE, RAT_GSM or RAT_UMTS
	UInt8			band;				///< Current band. For possible values see MS_GetCurrentBand()
} MSRegStateInfo_t;

///This structure should reflect T_PLMN defined in msnu.h for stack. 
///This is because in mmregprim.c we typecast PLMN_t from T_PLMN
typedef struct
{
	UInt16	mcc;
	UInt8	mnc;
	
}PLMN_t;


//Should reflect the enum sent by stack T_RADIO_ACCESS_TECHNOLOGY.
typedef enum
{
	MS_RAT_NOT_AVAILABLE,
	MS_RAT_GSM,
	MS_RAT_UMTS

} Rat_t;
/// MS De-Registration causes
typedef enum
{
	PowerDown	  = 0,					///< MS is powered down
	SIMRemoved	  = 1,					///< SIM was removed
	DetachService = 6,					///< Detach was requested 
	InternalDeactivate = 10				///< Internal deactivation
} DeRegCause_t;							///< Deregistration cause

typedef enum {
	ATTACH_MODE_INVALID,			
	ATTACH_MODE_GSM_GPRS,         
	ATTACH_MODE_GSM_ONLY,         
	ATTACH_MODE_GPRS_ONLY      
}MSAttachMode_t;


#define PLMN_UNKNOWN_COUNTRY ""			///<	PLMN unknown country value

#define PLMN_MCC_UNKNOWN 0				///<	PLMN unknown MCC value

#define PLMN_CC_UNKNOWN 0				///<	PLMN unknown CC value


/**
 PLMN Country Table 
**/
typedef struct 
{
    UInt16	mcc; ///< Mobile Country Code
    UInt16	cc;  ///< Country Code 
    const char	*country_name;	///< Country Name
} PLMN_COUNTRY_t;


/**
 PLMN Table 
**/
typedef struct 
{
    UInt16		mnc;				///< Mobile Network Code
    const char	*plmn_long_name;	///< Long Network Name 
    const char	*plmn_short_name;	///< Short Network Name 
    UInt16		country_index;		///< Index in the "PLMN_COUNTRY_t" table 
} PLMN_ITEM_t;

/**
 UCS2 PLMN Items
**/
typedef struct 
{
	UInt16	mnc;
	const UInt8	*ucs2_long_name;	
	const UInt8 *ucs2_short_name;	
	UInt8	long_name_size;
	UInt8	short_name_size;
	UInt16	country_index;
} UCS2_PLMN_ITEM_t;

typedef enum
{
	TIMEZONE_UPDATEMODE_NONE,			///< No time zone update
	TIMEZONE_UPDATEMODE_AUTO,			///< Automatic time zone update
	TIMEZONE_UPDATEMODE_MANUAL,			///< Manual update
	TIMEZONE_UPDATEMODE_USERCONFIRM		///< Time zone update pending user confirmation
} TimeZoneUpdateMode_t;


//---------------------Unsolicited Responses structure defintion----------------------------
typedef enum 
{
	ATTACH_CNF,
	DETACH_CNF,
	SERVICE_IND,
	ATTACH_IND,
	DETACH_IND,
	NO_RESP 
}PchRespType_t;

typedef enum
{
	NOT_APPLICABLE,							
	SUPPORTED,
	NOT_SUPPORTED
} MSNetAccess_t;


/**
Structure:	MS Network Operation Mode Type
			Network operation mode of the PLMN that the UE is registered or camped to.
			As per 23.060 Section 6.3.3.1
			Should be synchronized with the corresponding stack definition in mmregprim.h
**/
typedef enum {
	
	MSNW_OPERATION_MODE_NONE,			///< No nom due to MS not being camped on a cell or registered
	MSNW_OPERATION_MODE_I,				///< NOM I where paging coordination between SGSN and MSC exists
 	MSNW_OPERATION_MODE_II,				///< NOM II 
 	MSNW_OPERATION_MODE_III,			///< NOM III
 	MSNW_OPERATION_MODE_INVALID =255	///< Used when stack doesn't provide the value
}MSNwOperationMode_t;					///< MS Network Operation Mode Type

/// PCH Reject Cause
typedef enum
{
	NO_REJECTION							=	0,		// 
	OPERATION_SUCCEED						=	1,
//	OPERATION_NOT_ALLOWED					=	3,
	
	NO_NETWORK_SERVICE						=	6,
	GPRS_NOT_ALLOWED						=	7,
	PCH_OPERATOR_DETERMINED_BARRING		= 8, 		
	PCH_PLMN_NOT_ALLOWED					=	11,
	LOCATION_NOT_ALLOWED					=	12,
	ROAMING_NOT_ALLOWED						=	13,

	LLC_OR_SNDCP_FAILURE					=	25,
	INSUFFICIENT_RESOURCES					=	26,		//
	MISSING_OR_UNKNOWN_APN					=	27,
	UNKNOWN_PDP_ADDRESS						=	28,
	USER_AUTH_FAILED						=	29,
	ACTIVATION_REJECTED_BY_GGSN				=	30,
	ACTIVATION_REJECTED_UNSPECIFIED			=	31,		//
	SERVICE_OPT_NOT_SUPPORTED				=	32,
	REQ_SERVICE_NOT_SUBSCRIBED				=	33,
	SERVICE_TEMP_OUT_OF_ORDER				=	34,
	NSAPI_ALREADY_USED						=	35,
	REGULAR_DEACTIVATION					=	36,
	QOS_NOT_ACCEPTED						=	37,
	PCH_NETWORK_FAILURE						=	38,
	REACTIVATION_REQUIRED					=	39,
	FEATURE_NOT_SUPPORTED					=	40,
	SEMANTIC_ERROR_IN_TFT					=	41,
	SYNTACTICAL_ERROR_IN_TFT				=	42,
	UNKNOWN_PDP_CONTEXT						=	43,
	SEMANTIC_ERROR_IN_PKT_FILTER			=	44,
	SYNTACTICAL_ERROR_IN_PKT_FILTER			=	45,
	CONTEXT_WITHOUT_TFT						=	46,
	INVALID_TI								=	81,
	SEMANT_INCORRECT_MSG					=	95,
	INV_MANDATORY_IE						=	96,
	MSG_TYPE_NOT_EXISTENT					=	97,
	MSG_TYPE_NOT_COMPATIBLE					=	98,
	IE_NON_EXISTENT							=	99,
	CONDITIONAL_IE_ERROR					=	100,
	MSG_NOT_COMPATIBLE						=	101,
	PCH_PROTOCOL_ERROR_UNSPECIFIED			=	111,
	PCH_APN_INCOMPATIBLE_W_ACTIVE_PDP		=   112,
	USER_ABORT								=	113

}PCHRejectCause_t;	

/**
Structure:	MS Network Information Type
**/
typedef struct {
	UInt8				rat;				///< RAT_NOT_AVAILABLE(0),RAT_GSM(1),RAT_UMTS(2)
	UInt8				bandInfo;			///< Band Information
	MSNetAccess_t		msc_r99;			///< MSC Release 99
	MSNetAccess_t		sgsn_r99;			///< SGSN Release 99
	MSNetAccess_t		gprs_supported;		///< GPRS Supported
	MSNetAccess_t		egprs_supported;	///< EGPRS Supported
	MSNetAccess_t		dtm_supported;		///< indicates dtm support by the network on which the UE is registered on
	MSNetAccess_t		hsdpa_supported;	///< indicates hsdpa support by the network
	MSNetAccess_t		hsupa_supported;	///< indicates hsupa support by the network 
	MSNwOperationMode_t	nom;				///< network operation mode sent by the network
	MSNwType_t			network_type;		///< network type
}MSNetworkInfo_t;							///< MS Network Information Type

/// Structure :  MS State/Information definition
typedef struct {
	RegisterStatus_t	cs_status;		///< status of registration to a Circuit-Switched network
	RegisterStatus_t	gprs_status;	///< status of registration to a GPRS network
	PCHRejectCause_t	cs_cause;
	PCHRejectCause_t	gprs_cause;
	PLMNId_t			curr_plmn;		///< current plmn
	LACode_t			lac;
	CellInfo_t			cell_info;
	UInt8				rac;			///< RAC
	UInt16				rnc_id;			///< Radio network controller ID
}MsState_t;
/**
GPRS GSM Reg Status
**/

typedef struct {
	MSRegState_t			regState;
	NetworkCause_t			cause;			// enum for invalid SIM, invalid ME, ....
	UInt16					lac;
	UInt16					cell_id;
	UInt16					mcc;		
	UInt8					mnc;
	UInt8					netCause;		//3GPP 24.008 cause from network
	MSNetworkInfo_t			netInfo;		//RAT/BandInfo/NetworkAccess	
	PchRespType_t			pchType;
	UInt8					rac;			///< RAC
	UInt16					rncId;
}MSRegInfo_t;

/// Network Identity and Time Zone Network Name
typedef struct
{
	UInt8		longName[255];		///< Long AlphaNumeric name
	UInt8		shortName[255];		///< Short for of network name
} nitzNetworkName_t;


/// Time Zone and Date structure
typedef struct
{
	Int8		timeZone;
	UInt8		dstAdjust;	///< Possible value (0, 1, 2 or "INVALID_DST_VALUE"). "INVALID_DST_VALUE" means network does not pass DST
							///< (Daylight Saving Time) info in MM Information or GMM Information messages. See Section 10.5.3.12 of 3GPP 24.008.
	RTCTime_t	adjustedTime;	///< Real time clock interface time
} TimeZoneDate_t;


// See Section 10.5.3.11 of 3GPP 24.008.
typedef struct
{
	Boolean fromGmmInfo;	// TRUE if LSA is from GMM Info; FALSE if from MM Info 								
	UInt8	lsaLength;		// If LSA length is 0, it indicates that MS has moved to an area where no LSA is available								
	UInt8	lsaArray[3];	// LSA ID, applicable only if lsaLength not equal to 0 (if not equal to 0, length must be 3)											
} lsaIdentity_t;


typedef struct
{
	Boolean 					status;
}Inter_HSDPAStatusInd_t;

typedef struct
{
	RadioDirection_t			radio_direction;
	RadioStatus_t				radio_status;
	UInt8					cid;
}MSRadioActivityInd_t;

/// Last good band info
typedef struct
{
	UInt8			lastGoodBand;			///< band info
	Boolean			IsLastGoodBandStored;	///< denotes if last band is saved
} LastGoodBandInfo_t;


#define 		MAX_TEST_ARFCNS 		8		///< 8 is because in the current code only 8 ARFCNs are taken from the list.

/**
Structure:	Production Test Data for each band. For use with MS_LOCAL_ELEM_TEST_CHAN
			The following structure is used by the upper layers to
			set the production test frequencies for each band.
*/
typedef struct
{
	BandSelect_t	band;			///< the band which the ARFCNs belong to.
 									///< Only BAND_GSM_900_ONLY, BAND_DCS1800_ONLY,
 									///< BAND_PCS1900_ONLY and BAND_GSM850_ONLY are allowed as inputs currently.
	UInt16			numChan;		///< Number of ARFCNs in the ChanListPtr, maximum value is 8 (MAX_TEST_ARFCNS in ms_setting.h)
	UInt16*			chanListPtr;	///< pointer to the list of ARFCNs
} MS_TestChan_t;					///< Test Channel Type


#endif
