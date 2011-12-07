/************************************************************************************************/
/*                                                                                              */
/*  Copyright 2009 - 2011  Broadcom Corporation                                                        */
/*                                                                                              */
/*     Unless you and Broadcom execute a separate written software license agreement governing  */
/*     use of this software, this software is licensed to you under the terms of the GNU        */
/*     General Public License version 2 (the GPL), available at                                 */
/*                                                                                              */
/*          http://www.broadcom.com/licenses/GPLv2.php                                          */
/*                                                                                              */
/*     with the following added to such license:                                                */
/*                                                                                              */
/*     As a special exception, the copyright holders of this software give you permission to    */
/*     link this software with independent modules, and to copy and distribute the resulting    */
/*     executable under terms of your choice, provided that you also meet, for each linked      */
/*     independent module, the terms and conditions of the license of that module.              */
/*     An independent module is a module which is not derived from this software.  The special  */
/*     exception does not apply to any modifications of the software.                           */
/*                                                                                              */
/*     Notwithstanding the above, under no circumstances may you combine this software in any   */
/*     way with any other Broadcom software provided under a license other than the GPL,        */
/*     without Broadcom's express prior written consent.                                        */
/*                                                                                              */
/************************************************************************************************/

/**
*
* @file   audio_vdriver.c
* @brief  Audio VDriver API for CAPH
*
******************************************************************************/

//=============================================================================
// Include directives
//=============================================================================

#include "mobcom_types.h"
#include "resultcode.h"
#include "audio_consts.h"
#include "dspcmd.h"
#include "csl_apcmd.h"

#include "bcm_fuse_sysparm_CIB.h"
#include "ostask.h"
#include "osheap.h"
#include "log.h"
#include "csl_caph.h"
#include "csl_apcmd.h"
#include "csl_dsp.h"
#include "csl_caph_audioh.h"
#include "csl_caph_hwctrl.h"
#include "audio_vdriver.h"
#include <mach/comms/platform_mconfig.h>
#include "io.h"
#include "csl_dsp_cneon_api.h"
#if defined(ENABLE_DMA_VOICE)
#include "csl_dsp_caph_control_api.h"
#endif

#include "audio_pmu_adapt.h"
#include "audio_controller.h"

/**
*
* @addtogroup AudioDriverGroup
* @{
*/

//=============================================================================
// Private Type and Constant declarations
//=============================================================================

typedef void (*AUDDRV_User_CB) (UInt32 param1, UInt32 param2, UInt32 param3);

typedef struct AUDDRV_PathID_t{
	CSL_CAPH_PathID ulPathID;
	CSL_CAPH_PathID ul2PathID;
	CSL_CAPH_PathID dlPathID;
} AUDDRV_PathID_t;

//=============================================================================
// Private Variable declarations
//=============================================================================
#if defined(CONFIG_BCM_MODEM)
#define AUDIO_MODEM(a) a
#else
#define AUDIO_MODEM(a)
#endif

//static Boolean voiceInPathEnabled = FALSE;  //this is needed because DSPCMD_AUDIO_ENABLE sets/clears AMCR.AUDEN for both voiceIn and voiceOut
static Boolean voicePlayOutpathEnabled = FALSE;  //this is needed because DSPCMD_AUDIO_ENABLE sets/clears AMCR.AUDEN

static AUDDRV_PathID_t telephonyPathID;

static void *sUserCB = NULL;
static CSL_CAPH_DEVICE_e sink = CSL_CAPH_DEV_NONE;
static Boolean userEQOn = FALSE; // If TRUE, bypass EQ filter setting request from audio controller.

static AUDIO_SOURCE_Enum_t  currVoiceMic = AUDIO_SOURCE_UNDEFINED;	 //used in pcm i/f control. assume one mic, one spkr.
static AUDIO_SINK_Enum_t	currVoiceSpkr = AUDIO_SINK_UNDEFINED;	//used in pcm i/f control. assume one mic, one spkr.
static Boolean bInVoiceCall = FALSE;
static Boolean bmuteVoiceCall = FALSE;
static Boolean bDuringTelephonySwitchMicSpkr = FALSE;

static int telephony_digital_gain_dB = 12;  //dB
static int telephony_ul_gain_mB = 0;  // 0 mB

static Boolean controlFlagForCustomGain = FALSE;

typedef struct Audio_Driver_t
{
	UInt8					isRunning;
	Task_t					task;
	UInt32					taskID;
	Queue_t 				msgQueue;
} Audio_Driver_t;

static Audio_Driver_t sAudDrv = {0};
static unsigned int voiceCallSampleRate = AUDIO_SAMPLING_RATE_8000;  // defalut to 8K Hz

static Boolean IsBTM_WB = FALSE;  //this flag remembers if the Bluetooth headset is a wideband headset (16KHz voice)

static audio_codecId_handler_t codecId_handler = NULL;
static UInt8 audio_codecID = 6; //default CODEC ID = AMR NB

//=============================================================================
// Private function prototypes
//=============================================================================

//on AP:
static SysAudioParm_t* AUDIO_GetParmAccessPtr(void)
{
#ifdef BSP_ONLY_BUILD
	return NULL;
#else
	return APSYSPARM_GetAudioParmAccessPtr();
#endif
}

#define AUDIOMODE_PARM_ACCESSOR(mode)	 AUDIO_GetParmAccessPtr()[mode]
#define AUDIOMODE_PARM_MM_ACCESSOR(mode)	 AUDIO_GetParmMMAccessPtr()[mode]

static UInt32* AUDIO_GetIHF48KHzBufferBaseAddress (void);

static void AUDDRV_Telephony_InitHW (AUDIO_SOURCE_Enum_t mic,
                                    AUDIO_SINK_Enum_t speaker,
                                    AUDIO_SAMPLING_RATE_t sample_rate,
                                    Boolean bNeedDualMic);

static void AUDDRV_Telephony_DeinitHW ( void );
static CSL_CAPH_DEVICE_e AUDDRV_GetDRVDeviceFromSpkr (AUDIO_SINK_Enum_t spkr);
static CSL_CAPH_DEVICE_e AUDDRV_GetDRVDeviceFromMic (AUDIO_SOURCE_Enum_t mic);

static void AUDDRV_HWControl_SetFilter(AUDDRV_HWCTRL_FILTER_e filter, void* coeff);
static void AUDDRV_HWControl_EnableSideTone(AudioMode_t audio_mode);
static void AUDDRV_HWControl_DisableSideTone(AudioMode_t audio_mode);

static void auddrv_SetAudioMode_mic( AudioMode_t audio_mode, unsigned int arg_pathID );
static void auddrv_SetAudioMode_speaker( AudioMode_t audio_mode, unsigned int arg_pathID );

extern CSL_CAPH_HWConfig_Table_t HWConfig_Table[MAX_AUDIO_PATH];

//=============================================================================
// Functions
//=============================================================================


extern void Audio_InitRpc(void);

extern void VPU_Capture_Request(UInt16 buf_index);
extern void VPU_Render_Request(UInt16 bufferIndex);
extern Result_t AUDDRV_USB_HandleDSPInt(UInt16 arg0, UInt16 inBuf, UInt16 outBuf);
extern void ARM2SP_Render_Request(UInt16 buf_index);
extern void ARM2SP2_Render_Request(UInt16 buf_index);
extern void AP_ProcessStatusMainAMRDone(UInt16 codecType);
extern void VOIP_ProcessVOIPDLDone(void);
extern void AUDLOG_ProcessLogChannel(UInt16 audio_stream_buffer_idx);
extern void VOIF_Buffer_Request(UInt32 bufferIndex, UInt32 samplingRate);


//=============================================================================
//
// Function Name: AUDDRV_Init
//
// Description:   Inititialize audio system
//
//=============================================================================
void AUDDRV_Init( void )
{
	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_Init *\n\r");

	if (sAudDrv.isRunning == TRUE)
		return;
	/* register DSP VPU status processing handlers */

	AUDIO_MODEM(CSL_RegisterVPUCaptureStatusHandler((VPUCaptureStatusCB_t)&VPU_Capture_Request);)
#if 0  // These features are not needed in LMP now.
	AUDIO_MODEM(CSL_RegisterVPURenderStatusHandler((VPURenderStatusCB_t)&VPU_Render_Request);)
	AUDIO_MODEM(CSL_RegisterUSBStatusHandler((USBStatusCB_t)&AUDDRV_USB_HandleDSPInt);)
#endif
	AUDIO_MODEM(CSL_RegisterVoIPStatusHandler((VoIPStatusCB_t)&VOIP_ProcessVOIPDLDone);)
	AUDIO_MODEM(CSL_RegisterMainAMRStatusHandler((MainAMRStatusCB_t)&AP_ProcessStatusMainAMRDone);)
	AUDIO_MODEM(CSL_RegisterARM2SPRenderStatusHandler((ARM2SPRenderStatusCB_t)&ARM2SP_Render_Request);)
	AUDIO_MODEM(CSL_RegisterARM2SP2RenderStatusHandler((ARM2SP2RenderStatusCB_t)&ARM2SP2_Render_Request);)
	AUDIO_MODEM(CSL_RegisterAudioLogHandler((AudioLogStatusCB_t)&AUDLOG_ProcessLogChannel);)
	AUDIO_MODEM(CSL_RegisterVOIFStatusHandler((VOIFStatusCB_t)&VOIF_Buffer_Request);)

	Audio_InitRpc();
	sAudDrv.isRunning = TRUE;
}

//*********************************************************************
//
//	 Shutdown audio driver task
//
//	 @return	void
//	 @note
//**********************************************************************/
void AUDDRV_Shutdown(void)
{
	if (sAudDrv.isRunning == FALSE)
		return;

	sAudDrv.isRunning = FALSE;
}


//=============================================================================
//
// Function Name: AUDDRV_Telephony_Init
//
// Description:   Initialize audio system for voice call
//
//=============================================================================

//Prepare DSP before turn on hardware audio path for voice call.
//	This is part of the control sequence for starting telephony audio.
void AUDDRV_Telephony_Init ( AUDIO_SOURCE_Enum_t	mic,
		AUDIO_SINK_Enum_t speaker)
{
	AudioMode_t mode;
	Boolean bDualMic_IsNeeded = FALSE;
#if defined(ENABLE_DMA_VOICE)
	UInt16 dma_mic_spk;
#endif

	telephonyPathID.ulPathID = 0;
	telephonyPathID.ul2PathID = 0;
	telephonyPathID.dlPathID = 0;

	///////////////////////////////////////////////////////////////////////
	// Phone Setup Sequence
	// 1. Init CAPH HW
	// 2. Send DSP command DSPCMD_TYPE_AUDIO_ENABLE
	// 3. If requires 48KHz for example in IHF mode, Send VPRIPCMDQ_ENABLE_48KHZ_SPEAKER_OUTPUT
	//////////////////////////////////////////////////////////////////////

	Log_DebugPrintf(LOGID_AUDIO, "AUDDRV_Telephony_Init");

	//control HW and flags at AP
	bInVoiceCall = TRUE;  //to prevent sending DSP Audio Enable when enable voice path.
	currVoiceMic = mic;
	currVoiceSpkr = speaker;

	audio_control_dsp( DSPCMD_TYPE_MUTE_DSP_UL, 0, 0, 0, 0, 0 );
	audio_control_dsp( DSPCMD_TYPE_EC_NS_ON, FALSE, FALSE, 0, 0, 0 );
	audio_control_dsp( DSPCMD_TYPE_DUAL_MIC_ON, FALSE, 0, 0, 0, 0 );
	audio_control_dsp( DSPCMD_TYPE_AUDIO_TURN_UL_COMPANDEROnOff, FALSE, 0, 0, 0, 0 );
	audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_UL, FALSE, 0, 0, 0, 0 );
	audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_DL, FALSE, 0, 0, 0, 0 );

	mode = AUDDRV_GetAudioModeBySink( speaker );
	//check to see the network speech coder's sampling rate and determine our HW sampling rate (and audio mode).
	if (voiceCallSampleRate == AUDIO_SAMPLING_RATE_16000)
	{
		mode = mode + AUDIO_MODE_NUMBER;	//WB
	}

	bDualMic_IsNeeded = (AUDIO_GetParmAccessPtr()[mode].dual_mic_enable != 0);  //in parm_audio.txt, VOICE_DUALMIC_ENABLE

	if ( mode==AUDIO_MODE_BLUETOOTH || mode==AUDIO_MODE_BLUETOOTH_WB )
	{
	  //if use BT headset, BT headset's NB/WB capability is the determining factor, supersdes the above.
	  //BT WB headset always works at 16 KHz PCM interface in hardware.
	  //BT NB headset always works at 8 KHz PCM interface in hardware.

	  //user space code (BT stack) knows BT headset type and calls AUDDRV_SetBTMTypeWB( ).

	  if ( AUDDRV_IsBTMWB() )
	    mode = AUDIO_MODE_BLUETOOTH_WB;
	  else
	    mode = AUDIO_MODE_BLUETOOTH;

	  bDualMic_IsNeeded = FALSE;
	}

	if( mode >= AUDIO_MODE_NUMBER )
	{
		AUDDRV_Telephony_InitHW ( mic, speaker, AUDIO_SAMPLING_RATE_16000, bDualMic_IsNeeded );
#if defined(ENABLE_DMA_VOICE)
		csl_dsp_caph_control_aadmac_set_samp_rate(AUDIO_SAMPLING_RATE_16000);
#endif
	} else {
		AUDDRV_Telephony_InitHW ( mic, speaker, AUDIO_SAMPLING_RATE_8000, bDualMic_IsNeeded );
#if defined(ENABLE_DMA_VOICE)
		csl_dsp_caph_control_aadmac_set_samp_rate(AUDIO_SAMPLING_RATE_8000);
#endif
	}

    // Set new filter coef, sidetone filters, gains.
    AUDDRV_SetAudioMode( mode );

	if(speaker == AUDIO_SINK_LOUDSPK)
	{
#if defined(ENABLE_DMA_VOICE)
		//csl_dsp_caph_control_aadmac_disable_path((UInt16)(DSP_AADMAC_SEC_MIC_EN)|(UInt16)(DSP_AADMAC_SPKR_EN));
		csl_dsp_caph_control_aadmac_disable_path((UInt16)(DSP_AADMAC_SPKR_EN));
		dma_mic_spk = (UInt16)DSP_AADMAC_PRI_MIC_EN;
		csl_dsp_caph_control_aadmac_enable_path(dma_mic_spk);
#endif
		audio_control_dsp( DSPCMD_TYPE_AUDIO_ENABLE, TRUE, 0, AUDDRV_IsCall16K( AUDDRV_GetAudioMode() ), 0, 0 );

	// The dealy is to make sure DSPCMD_TYPE_AUDIO_ENABLE is done since it is a command via CP.
		OSTASK_Sleep(1);
		AUDIO_MODEM(VPRIPCMDQ_ENABLE_48KHZ_SPEAKER_OUTPUT(TRUE,FALSE,FALSE);)
	}
	else
	{
#if defined(ENABLE_DMA_VOICE)
		dma_mic_spk = (UInt16)(DSP_AADMAC_PRI_MIC_EN)|(UInt16)(DSP_AADMAC_SPKR_EN);
		csl_dsp_caph_control_aadmac_enable_path(dma_mic_spk);
#endif
		audio_control_dsp( DSPCMD_TYPE_AUDIO_ENABLE, TRUE, 0, AUDDRV_IsCall16K( AUDDRV_GetAudioMode() ), 0, 0 );

		// The dealy is to make sure DSPCMD_TYPE_AUDIO_ENABLE is done since it is a command via CP.
		OSTASK_Sleep(1);
	}

	AUDDRV_SetVoiceCallFlag(TRUE);  //let HW control logic know.

#if defined(ENABLE_DMA_VOICE)
	audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_DL, TRUE, 0, 0, 0, 0 );
#else
	audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_DL, TRUE, AUDDRV_IsCall16K( AUDDRV_GetAudioMode() ), 0, 0, 0 );
#endif
	OSTASK_Sleep( 40 );

#if defined(ENABLE_DMA_VOICE)
	audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_UL, TRUE, 0, 0, 0, 0 );
#else
	audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_UL, TRUE, AUDDRV_IsCall16K( AUDDRV_GetAudioMode() ), 0, 0, 0 );
#endif
	audio_control_dsp( DSPCMD_TYPE_EC_NS_ON, TRUE, TRUE, 0, 0, 0 );

    if(bDualMic_IsNeeded==TRUE)
    {
		audio_control_dsp( DSPCMD_TYPE_DUAL_MIC_ON, TRUE, 0, 0, 0, 0 );
    }

	audio_control_dsp( DSPCMD_TYPE_AUDIO_TURN_UL_COMPANDEROnOff, TRUE, 0, 0, 0, 0 );

	//for the next call, upper layer in Android un-mute mic before start the next voice call.
	//but in LMP (it has no Android code).
	// in case it was muted from last voice call, need to un-mute it.
	if (bDuringTelephonySwitchMicSpkr == FALSE)
	{
		//pr_info("UnMute\r\n");
		bmuteVoiceCall = FALSE;
	}

	//pr_info("bmuteVoiceCall = %d \r\n", bmuteVoiceCall);
	if (bmuteVoiceCall == FALSE)
	{
		//pr_info("UnMute\r\n");
		audio_control_dsp( DSPCMD_TYPE_UNMUTE_DSP_UL, 0, 0, 0, 0, 0 );
	}

	//per call basis: enable the DTX by calling stack api when call connected
	audio_control_generic( AUDDRV_CPCMD_ENABLE_DSP_DTX, TRUE, 0, 0, 0, 0 );

	if (speaker == AUDIO_SINK_BTM)
		AUDDRV_SetPCMOnOff( 1 );
	else
	{
		if(currVoiceMic != AUDIO_SOURCE_BTM) //need to check mic too.
			AUDDRV_SetPCMOnOff( 0 );
	}

	return;
}


//=============================================================================
//
// Function Name: AUDDRV_Telephony_RateChange
//
// Description:   Change the sample rate for voice call
//
//=============================================================================

void AUDDRV_Telephony_RateChange( unsigned int sample_rate )
{
    AudioMode_t mode;
    Boolean bDualMic_IsNeeded = FALSE;

    Log_DebugPrintf(LOGID_AUDIO, "AUDDRV_Telephony_RateChange, sampleRate = %d\n\r", sample_rate);

#if defined(FUSE_APPS_PROCESSOR)&&!defined(BSP_ONLY_BUILD)
	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_Telephony_RateChange AP  *\n\r");
#else
	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_Telephony_RateChange CP *\n\r");
#endif

    if ( voiceCallSampleRate == sample_rate )
      return;

    voiceCallSampleRate = sample_rate;  //remember the rate for current call. (or for the incoming call in ring state.)

    if ( AUDDRV_InVoiceCall() )
	{

	audio_control_dsp( DSPCMD_TYPE_MUTE_DSP_UL, 0, 0, 0, 0, 0 );
	audio_control_dsp( DSPCMD_TYPE_EC_NS_ON, FALSE, FALSE, 0, 0, 0 );
	audio_control_dsp( DSPCMD_TYPE_DUAL_MIC_ON, FALSE, 0, 0, 0, 0 );
	audio_control_dsp( DSPCMD_TYPE_AUDIO_TURN_UL_COMPANDEROnOff, FALSE, 0, 0, 0, 0 );
	audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_UL, FALSE, 0, 0, 0, 0 );
	audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_DL, FALSE, 0, 0, 0, 0 );

    if( voiceCallSampleRate==AUDIO_SAMPLING_RATE_8000 )
      mode = AUDDRV_GetAudioMode() % AUDIO_MODE_NUMBER;
    else
      mode = (AUDDRV_GetAudioMode() % AUDIO_MODE_NUMBER) + AUDIO_MODE_NUMBER;

    AUDDRV_SetAudioMode( mode );

	//AUDDRV_Enable_Output (AUDDRV_VOICE_OUTPUT, speaker, TRUE, AUDIO_SAMPLING_RATE_8000);
#if defined(ENABLE_DMA_VOICE)
	if (AUDDRV_IsCall16K( AUDDRV_GetAudioMode() ))
		csl_dsp_caph_control_aadmac_set_samp_rate(AUDIO_SAMPLING_RATE_16000);
	else
		csl_dsp_caph_control_aadmac_set_samp_rate(AUDIO_SAMPLING_RATE_8000);
	audio_control_dsp( DSPCMD_TYPE_AUDIO_ENABLE, TRUE, 0, AUDDRV_IsCall16K( AUDDRV_GetAudioMode() ), 0, 0 );
#else
	audio_control_dsp( DSPCMD_TYPE_AUDIO_ENABLE, TRUE, 0, AUDDRV_IsCall16K( AUDDRV_GetAudioMode() ), 0, 0 );
#endif
	audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_DL, TRUE, 0, 0, 0, 0 );

	//AUDDRV_Enable_Input ( AUDDRV_VOICE_INPUT, mic, AUDIO_SAMPLING_RATE_8000);

	OSTASK_Sleep( 40 );

	audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_UL, TRUE, 0, 0, 0, 0 );
	audio_control_dsp( DSPCMD_TYPE_EC_NS_ON, TRUE, TRUE, 0, 0, 0 );

	bDualMic_IsNeeded = (AUDIO_GetParmAccessPtr()[mode].dual_mic_enable != 0);  //in parm_audio.txt, VOICE_DUALMIC_ENABLE

    if(bDualMic_IsNeeded==TRUE)
    {
		audio_control_dsp( DSPCMD_TYPE_DUAL_MIC_ON, TRUE, 0, 0, 0, 0 );
    }

	audio_control_dsp( DSPCMD_TYPE_AUDIO_TURN_UL_COMPANDEROnOff, TRUE, 0, 0, 0, 0 );

	if (bmuteVoiceCall == FALSE)
	{
		audio_control_dsp( DSPCMD_TYPE_UNMUTE_DSP_UL, 0, 0, 0, 0, 0 );
	}

	}

	return;
}


//*********************************************************************
//
//	 Registers callback for rate change request
//
//	@param	   callback function
//	@return 	void
//	 @note
//**********************************************************************/

void AUDDRV_RegisterRateChangeCallback( audio_codecId_handler_t codecId_cb )
{
	Log_DebugPrintf(LOGID_SOC_AUDIO, "\n\r\t*  AUDDRV_RegisterRateChangeCallback, 0x%lx\n\r", (long unsigned int)codecId_cb);
	codecId_handler = codecId_cb;
}

//*********************************************************************
//
//	 Post rate change message for telephony session
//
//	@param		codec ID
//	@return 	void
//	 @note
//**********************************************************************/
void AUDDRV_Telephone_RequestRateChange(int codecID)
{
	//if((audio_codecID != codecID) && (codecId_handler != NULL)) //if current codecID is same as new, ignore the request
	if(codecId_handler != NULL)
	{
		audio_codecID = codecID;
		codecId_handler(codecID);
	}

}

//=============================================================================
//
// Function Name: AUDDRV_Telephone_GetSampleRate
//
// Description:   Get the sample rate for voice call
//
//=============================================================================
unsigned int AUDDRV_Telephone_GetSampleRate()
{
	return voiceCallSampleRate;
}

//=============================================================================
//
// Function Name: AUDDRV_Telephone_SaveSampleRate
//
// Description:   Set the sample rate for voice call
//
//=============================================================================
void AUDDRV_Telephone_SaveSampleRate( unsigned int sample_rate )
{
	voiceCallSampleRate = sample_rate;
}

//=============================================================================
//
// Function Name: AUDDRV_Telephony_Deinit
//
// Description:   DeInitialize audio system for voice call
//
//=============================================================================

//Prepare DSP before turn off hardware audio path for voice call.
// This is part of the control sequence for ending telephony audio.
void AUDDRV_Telephony_Deinit (void)
{
#if defined(ENABLE_DMA_VOICE)
	UInt16 dma_mic_spk;
#endif

	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_Telephony_Deinit voicePlayOutpathEnabled = %d*\n\r", voicePlayOutpathEnabled);

	AUDDRV_SetVoiceCallFlag(FALSE);  //let HW control logic know.

	// continues speech playback when end the phone call.
	// continues speech recording when end the phone call.
//	if ( FALSE==vopath_enabled && FALSE==DspVoiceIfActive_DL() && FALSE==vipath_enabled )  //VO path is off and DSP IF is off, VI path is off
//	{

	// a quick fix not to disable voice path for speech playbck when end the phone call.
	if (voicePlayOutpathEnabled == FALSE)
	{
		  //per call basis: disable the DTX by calling stack api when call disconnected
		audio_control_generic( AUDDRV_CPCMD_ENABLE_DSP_DTX, FALSE, 0, 0, 0, 0 );

		audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_DL, FALSE, 0, 0, 0, 0 );
		audio_control_dsp( DSPCMD_TYPE_AUDIO_CONNECT_UL, FALSE, 0, 0, 0, 0 );
		audio_control_dsp( DSPCMD_TYPE_EC_NS_ON, FALSE, FALSE, 0, 0, 0 );
		audio_control_dsp( DSPCMD_TYPE_DUAL_MIC_ON, FALSE, 0, 0, 0, 0 );
		audio_control_dsp( DSPCMD_TYPE_AUDIO_TURN_UL_COMPANDEROnOff, FALSE, 0, 0, 0, 0 );

		if(currVoiceSpkr == AUDIO_SINK_LOUDSPK)
		{
			AUDIO_MODEM(VPRIPCMDQ_ENABLE_48KHZ_SPEAKER_OUTPUT(FALSE,FALSE,FALSE);)
		}

#if defined(ENABLE_DMA_VOICE)
		dma_mic_spk = (UInt16)(DSP_AADMAC_PRI_MIC_EN)|(UInt16)(DSP_AADMAC_SPKR_EN);
		csl_dsp_caph_control_aadmac_disable_path(dma_mic_spk);
#endif
		audio_control_dsp( DSPCMD_TYPE_MUTE_DSP_UL, 0, 0, 0, 0, 0 );

		audio_control_dsp( DSPCMD_TYPE_AUDIO_ENABLE, FALSE, 0, 0, 0, 0 );

		OSTASK_Sleep( 3 ); //make sure audio is off, rtos does not have this.

		AUDDRV_Telephony_DeinitHW( );
	}

	if (AUDIO_MODE_BLUETOOTH == AUDDRV_GetAudioMode() )
		audio_control_dsp( DSPCMD_TYPE_AUDIO_SET_PCM, FALSE, 0, 0, 0, 0 );

	//at last
	bInVoiceCall = FALSE;
	currVoiceMic = AUDIO_SOURCE_UNDEFINED;
	currVoiceSpkr = AUDIO_SINK_UNDEFINED;

	if (bDuringTelephonySwitchMicSpkr == FALSE)
	{
		//reset to 8KHz as default for the next call
		voiceCallSampleRate = AUDIO_SAMPLING_RATE_8000;
	}

	telephonyPathID.ulPathID = 0;
	telephonyPathID.ul2PathID = 0;
	telephonyPathID.dlPathID = 0;

	return;
}


//=============================================================================
//
// Function Name: AUDDRV_Telephony_SelectMicSpkr
//
// Description:   Select the mic and speaker for voice call
//
//=============================================================================

void AUDDRV_Telephony_SelectMicSpkr (AUDIO_SOURCE_Enum_t mic,
                                    AUDIO_SINK_Enum_t speaker)
{
    if(currVoiceMic==mic && currVoiceSpkr==speaker)
      return;

    bDuringTelephonySwitchMicSpkr = TRUE;

    AUDDRV_Telephony_Deinit( );
    AUDDRV_Telephony_Init ( mic, speaker );

    bDuringTelephonySwitchMicSpkr = FALSE;
}


//=============================================================================
//
// Function Name: AUDDRV_EnableDSPOutput
//
// Description:   Enable audio DSP output for voice call
//
//=============================================================================

void AUDDRV_EnableDSPOutput (
			    AUDIO_SINK_Enum_t	   mixer_speaker_selection,
			    AUDIO_SAMPLING_RATE_t  sample_rate
			    )
{
	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_EnableDSPOutput mixer %d, bInVoiceCall %d, sample_rate %ld *\n\r", mixer_speaker_selection, bInVoiceCall, sample_rate);

	OSTASK_Sleep( 5 );	//sometimes BBC video has no audio. This delay may help the mixer filter and mixer gain loading.
	currVoiceSpkr = mixer_speaker_selection;

	if(bInVoiceCall != TRUE)
	{
		//if bInVoiceCall== TRUE, assume the telphony_init() function sends ENABLE and CONNECT_DL
		if (sample_rate == AUDIO_SAMPLING_RATE_8000)
		{
#if defined(ENABLE_DMA_VOICE)
			csl_dsp_caph_control_aadmac_set_samp_rate(AUDIO_SAMPLING_RATE_8000);
			csl_dsp_caph_control_aadmac_enable_path((UInt16)(DSP_AADMAC_SPKR_EN));
			audio_control_dsp(DSPCMD_TYPE_AUDIO_ENABLE, DSP_AADMAC_SPKR_EN, 0, 0, 0, 0 );
			audio_control_dsp(DSPCMD_TYPE_AUDIO_CONNECT_DL, 1, 0, 0, 0, 0 );
#else
			audio_control_dsp(DSPCMD_TYPE_AUDIO_ENABLE, 1, 0, 0, 0, 0 );
			audio_control_dsp(DSPCMD_TYPE_AUDIO_CONNECT_DL, 1, 0, 0, 0, 0 );
#endif
		}
		else
		{
#if defined(ENABLE_DMA_VOICE)
			csl_dsp_caph_control_aadmac_set_samp_rate(AUDIO_SAMPLING_RATE_16000);
			csl_dsp_caph_control_aadmac_enable_path((UInt16)(DSP_AADMAC_SPKR_EN));
			audio_control_dsp(DSPCMD_TYPE_AUDIO_ENABLE, DSP_AADMAC_SPKR_EN, 0, 0, 0, 0 );
			audio_control_dsp(DSPCMD_TYPE_AUDIO_CONNECT_DL, 1, 0, 0, 0, 0 );
#else
			audio_control_dsp(DSPCMD_TYPE_AUDIO_ENABLE, 1, 1, 0, 0, 0 );
			audio_control_dsp(DSPCMD_TYPE_AUDIO_CONNECT_DL, 1, 1, 0, 0, 0 );
#endif
		}
		voicePlayOutpathEnabled = TRUE;

		Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_Enable_Output: bInVoiceCall = %d, voicePlayOutpathEnabled = %d\n\r", bInVoiceCall, voicePlayOutpathEnabled);

#if 0
		if (currVoiceSpkr == AUDIO_SINK_BTM)
			AUDDRV_SetPCMOnOff( 1 );
		else
		{
			if(currVoiceMic != AUDIO_SOURCE_BTM) //need to check mic too.
			AUDDRV_SetPCMOnOff( 0 );
		}
#endif

	}
}

//=============================================================================
//
// Function Name: AUDDRV_DisableDSPOutput
//
// Description:   Disable audio DSP output for playback
//
//=============================================================================

void AUDDRV_DisableDSPOutput (void)
{
	Log_DebugPrintf(LOGID_AUDIO, "\n\r AUDDRV_DisableDSPOutput bInVoiceCall %d\n\r", bInVoiceCall);

	if(bInVoiceCall != TRUE)
	{
		//if bInVoiceCall== TRUE, assume the telphony_deinit() function sends DISABLE
		audio_control_dsp(DSPCMD_TYPE_AUDIO_CONNECT_DL, FALSE, 0, 0, 0, 0 );

#if defined(ENABLE_DMA_VOICE)
		csl_dsp_caph_control_aadmac_disable_path((UInt16)DSP_AADMAC_SPKR_EN);
#endif
		audio_control_dsp(DSPCMD_TYPE_AUDIO_ENABLE, FALSE, 0, 0, 0, 0 );

		voicePlayOutpathEnabled = FALSE;
	}
}


//=============================================================================
//
// Function Name: AUDDRV_EnableDSPInput
//
// Description:   Enable audio DSP output for voice call
//
//=============================================================================

void AUDDRV_EnableDSPInput (
			  AUDIO_SOURCE_Enum_t	  mic_selection,
			  AUDIO_SAMPLING_RATE_t  sample_rate
			  )
{
	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_EnableDSPInput mic_selection %d *\n\r", mic_selection );

	if(bInVoiceCall != TRUE)
	{
		//if bInVoiceCall== TRUE, assume the telphony_init() function sends ENABLE and CONNECT_UL
		if (sample_rate == AUDIO_SAMPLING_RATE_8000)
		{
#if defined(ENABLE_DMA_VOICE)
			csl_dsp_caph_control_aadmac_set_samp_rate(AUDIO_SAMPLING_RATE_8000);
			csl_dsp_caph_control_aadmac_enable_path((UInt16)(DSP_AADMAC_PRI_MIC_EN));
			audio_control_dsp(DSPCMD_TYPE_AUDIO_ENABLE, DSP_AADMAC_PRI_MIC_EN, 0, 0, 0, 0 );
			audio_control_dsp(DSPCMD_TYPE_AUDIO_CONNECT_UL, 1, 0, 0, 0, 0); //DSPCMD_TYPE_AUDIO_CONNECT should be called after DSPCMD_TYPE_AUDIO_ENABLE
#else
			audio_control_dsp(DSPCMD_TYPE_AUDIO_ENABLE, 1, 0, 0, 0, 0 );
			audio_control_dsp(DSPCMD_TYPE_AUDIO_CONNECT_UL, 1, 0, 0, 0, 0);
#endif
		}
		else
		{
#if defined(ENABLE_DMA_VOICE)
			csl_dsp_caph_control_aadmac_set_samp_rate(AUDIO_SAMPLING_RATE_16000);
			csl_dsp_caph_control_aadmac_enable_path((UInt16)(DSP_AADMAC_PRI_MIC_EN));
			audio_control_dsp(DSPCMD_TYPE_AUDIO_ENABLE, DSP_AADMAC_PRI_MIC_EN, 1, 0, 0, 0 );
			audio_control_dsp(DSPCMD_TYPE_AUDIO_CONNECT_UL, 1, 1, 0, 0, 0);
#else
			audio_control_dsp(DSPCMD_TYPE_AUDIO_ENABLE, 1, 1, 0, 0, 0 );
			audio_control_dsp(DSPCMD_TYPE_AUDIO_CONNECT_UL, 1, 1, 0, 0, 0);
#endif
		}
//		voiceInPathEnabled = TRUE;

		/*When voice call ends, DSPCMD_TYPE_MUTE_DSP_UL is being sent to DSP and this command mutes UL record gain
		Not sure why this is done and there is no clarification if we really need to send the MUTE UL command when
		disconnecting the voice call. For now, when voice record is started, UMUTE UL command will be sent */

		audio_control_dsp( DSPCMD_TYPE_UNMUTE_DSP_UL, 0, 0, 0, 0, 0 );

	}

#if 0
	currVoiceMic = mic_selection;
	if (currVoiceMic == AUDIO_SOURCE_BTM)
		AUDDRV_SetPCMOnOff( 1 );
	else
	{
		if (currVoiceSpkr != AUDIO_SINK_BTM) //need to check spkr too.
			AUDDRV_SetPCMOnOff( 0 );
	}

#endif

}

//=============================================================================
//
// Function Name: AUDDRV_DisableDSPInput
//
// Description:   Disable audio DSP input for record
//
//=============================================================================

void AUDDRV_DisableDSPInput (void)
{
	Log_DebugPrintf(LOGID_AUDIO, "\n\r AUDDRV_DisableDSPInput bInVoiceCall %d\n\r",bInVoiceCall);

	if(bInVoiceCall != TRUE)
	{
		//if bInVoiceCall== TRUE, assume the telphony_deinit() function sends DISABLE

		audio_control_dsp(DSPCMD_TYPE_AUDIO_CONNECT_UL, FALSE, 0, 0, 0, 0 );

#if defined(ENABLE_DMA_VOICE)
		csl_dsp_caph_control_aadmac_disable_path((UInt16)DSP_AADMAC_PRI_MIC_EN);
#endif
		audio_control_dsp( DSPCMD_TYPE_MUTE_DSP_UL, 0, 0, 0, 0, 0 );
		audio_control_dsp(DSPCMD_TYPE_AUDIO_ENABLE, FALSE, 0, 0, 0, 0 );
	}
}

//=============================================================================
//
// Function Name: AUDDRV_IsVoiceCallWB
//
// Description:   Return Wideband Voice Call status
//
//=============================================================================

Boolean AUDDRV_IsVoiceCallWB(AudioMode_t audio_mode)
{
	Boolean is_wb;
	if(audio_mode<AUDIO_MODE_NUMBER)
		is_wb = FALSE;
	else
		is_wb = TRUE;
	return is_wb;
}

//=============================================================================
//
// Function Name: AUDDRV_IsCall16K
//
// Description:   Return Voice Call 16KHz sample rate status
//
//=============================================================================

Boolean AUDDRV_IsCall16K(AudioMode_t voiceMode)
{
	Boolean is_call16k = FALSE;
	switch(voiceMode)
		{
		case	AUDIO_MODE_HANDSET_WB:
		case	AUDIO_MODE_HEADSET_WB:
		case	AUDIO_MODE_HANDSFREE_WB:
		//case	AUDIO_MODE_BLUETOOTH_WB:
		case	AUDIO_MODE_SPEAKERPHONE_WB:
		case	AUDIO_MODE_TTY_WB:
		case	AUDIO_MODE_HAC_WB:
		case	AUDIO_MODE_USB_WB:
		case	AUDIO_MODE_RESERVE_WB:
			is_call16k = TRUE;
			break;


		case	AUDIO_MODE_BLUETOOTH: //BT headset needs to consider NB or WB too
		case	AUDIO_MODE_BLUETOOTH_WB: //BT headset needs to consider NB or WB too
			is_call16k = IsBTM_WB;
			break;

		default:
			break;
		}
	return is_call16k;
}

//=============================================================================
//
// Function Name: AUDDRV_InVoiceCall
//
// Description:   Return Voice Call status
//
//=============================================================================

Boolean AUDDRV_InVoiceCall( void )
{
	return bInVoiceCall;
}


//=============================================================================
//
// Function Name: AUDDRV_SetVoiceCallFlag
//
// Description:   Set voice call flag for HW control loic.
//
//=============================================================================
void AUDDRV_SetVoiceCallFlag( Boolean inVoiceCall )
{
	bInVoiceCall = inVoiceCall;
}

//=============================================================================
//
// Function Name: AUDDRV_SaveAudioMode
//
// Description:   save audio mode.
//
//=============================================================================

// intentionally put this static var here to prevent any code above this lines to use this var.
static AudioMode_t currAudioMode = AUDIO_MODE_HANDSET;	//need to update this on AP and also in audioapi.c on CP.
//static AudioMode_t currAudioMode_ForPlayback = AUDIO_MODE_HANDSET;
//static AudioMode_t currAudioMode_ForRecord = AUDIO_MODE_HANDSET;


void AUDDRV_SaveAudioMode( AudioMode_t audio_mode )
{
	currAudioMode = audio_mode; // update mode
}

//=============================================================================
//
// Function Name: AUDDRV_SetAudioMode
//
// Description:   set audio mode for voice call.
//
//=============================================================================

void AUDDRV_SetAudioMode( AudioMode_t audio_mode )
{
	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_SetAudioMode() audio_mode==%d\n\r", audio_mode );

	// load DSP parameters:
	//if ( audio_mode >= AUDIO_MODE_NUMBER )
	if ( audio_mode >= AUDIO_MODE_NUMBER_VOICE )
		return;  //error

	if ( AUDDRV_InVoiceCall() )
	{
		// BTM needs to support NB or WB too
		if ((audio_mode == AUDIO_MODE_BLUETOOTH_WB) || (audio_mode == AUDIO_MODE_BLUETOOTH) )
		{
			//HW sampling rate depends on BT headset's capability
			if (AUDDRV_IsBTMWB())
				audio_mode = AUDIO_MODE_BLUETOOTH_WB;  //16Khz HW sampling rate
			else
				audio_mode = AUDIO_MODE_BLUETOOTH;  //8KHz HW sampling rate
		}
		Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_SetAudioMode() AUDDRV_InVoiceCall audio_mode=%d \n\r", audio_mode );
	}

	currAudioMode = audio_mode; // update mode
	//currMusicAudioMode = currAudioMode;

	audio_control_generic( AUDDRV_CPCMD_PassAudioMode, (UInt32)audio_mode, 0, 0, 0, 0 );  //??
	audio_control_generic( AUDDRV_CPCMD_SetAudioMode, (UInt32)audio_mode, 0, 0, 0, 0 );

	if (userEQOn == FALSE)
	{
		//load speaker EQ filter and Mic EQ filter from sysparm to DSP
		audio_control_generic( AUDDRV_CPCMD_SetFilter, audio_mode, 7, 0, 0, 0 );  //7 can be removed later on. It means mic1, mic2, speaker
	}
	//else
		//There is no need for this function to load the ECI-headset-provided speaker EQ filter and Mic EQ filter to DSP.
		//The ECI headset enable/disable request comes with the data. It means we'll get the coefficients every time they want to switch ECI headset on.
		//audio_cmf_filter((AudioCompfilter_t *) &copy_of_AudioCompfilter );


	auddrv_SetAudioMode_mic(audio_mode, 0);
	auddrv_SetAudioMode_speaker(audio_mode, 0);

}


//=============================================================================
//
// Function Name: AUDDRV_SetAudioMode_ForMusicPlayback
//
// Description:   set audio mode.
//
//=============================================================================

void AUDDRV_SetAudioMode_ForMusicPlayback( AudioMode_t audio_mode, unsigned int arg_pathID)
 //add a second audio_mode for broadcast case
{
	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_SetAudioMode_ForMusicPlayback() audio_mode==%d\n\r", audio_mode );

	auddrv_SetAudioMode_speaker(audio_mode, 0);
}


//=============================================================================
//
// Function Name: AUDDRV_SetAudioMode_ForMusicRecord
//
// Description:   set audio mode.
//
//=============================================================================

void AUDDRV_SetAudioMode_ForMusicRecord( AudioMode_t audio_mode, unsigned int arg_pathID)
{
	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_SetAudioMode_ForMusicRecord() audio_mode==%d\n\r", audio_mode );

	auddrv_SetAudioMode_mic(audio_mode, 0);
	
	//for 48 KHz recording no DSP and 8KHz recording through DSP.
	//AUDDRV_SetAudioMode( audio_mode );
}


//=============================================================================
//
// Function Name: AUDDRV_GetAudioMode
//
// Description:   Return Audio mode
//
//=============================================================================

AudioMode_t AUDDRV_GetAudioMode( void )
{
	return currAudioMode;
}

//*********************************************************************
//Description:
//	Get audio mode from sink
//Parameters
//	mode -- audio mode
//	sink -- Sink device coresponding to audio mode
//Return	none
//**********************************************************************/
AudioMode_t AUDDRV_GetAudioModeBySink(AUDIO_SINK_Enum_t sink)
{
    switch(sink)
    {
    case AUDIO_SINK_HANDSET:
          return AUDIO_MODE_HANDSET;

    case AUDIO_SINK_HEADSET:
          return AUDIO_MODE_HEADSET;

    case AUDIO_SINK_HANDSFREE:
          return AUDIO_MODE_HANDSFREE;

    case AUDIO_SINK_BTM:
    case AUDIO_SINK_BTS:
          return AUDIO_MODE_BLUETOOTH;

    case AUDIO_SINK_LOUDSPK:
          return AUDIO_MODE_SPEAKERPHONE;

    case AUDIO_SINK_TTY:
          return AUDIO_MODE_TTY;

    case AUDIO_SINK_HAC:
          return AUDIO_MODE_HAC;

    case AUDIO_SINK_USB:
          return AUDIO_MODE_USB;

    case AUDIO_SINK_I2S:
    case AUDIO_SINK_VIBRA:
	default:
		Log_DebugPrintf(LOGID_AUDIO,"AUDCTRL_GetAudioModeBySink(): can not find mode for sink %d \n", sink);
		return AUDIO_MODE_INVALID;
}
}

//=============================================================================
//
// Function Name: AUDDRV_User_CtrlDSP
//
// Description:   Control DSP Algo
//
//=============================================================================
int AUDDRV_User_CtrlDSP ( AudioDrvUserCtrl_t UserCtrlType, Boolean enable, Int32 size, void *param)
{
#if defined(CONFIG_BCM_MODEM)
	static Boolean ConfigSP = FALSE;
	static UInt32 *spCtrl = NULL, *spVar = NULL;

	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_User_CtrlDSP *\n\r");

	switch (UserCtrlType)
	{
		case AUDDRV_USER_SP_QUERY:
			Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_UserCtrlDSP, AUDDRV_USER_SP_QUERY *\n\r");

			if (param == NULL)
				return -EINVAL;

			csl_dsp_sp_query_msg((UInt32 *)param);

			break;
		case AUDDRV_USER_SP_CTRL:
			Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_UserCtrlDSP, AUDDRV_USER_SP_CTRL *\n\r");

			if (enable)
			{
				if (size <= 0 || param == NULL)
					return -EINVAL;

				if (spVar != NULL)
					csl_dsp_sp_cnfg_msg((UInt16)enable, 0, 1, (UInt32 *)param, spVar);
				else
				{
					spCtrl = (UInt32 *) kzalloc(size, GFP_KERNEL);
					if (!spCtrl)
						return -ENOMEM;

					memcpy(spCtrl, param, size);
					ConfigSP = TRUE;
				}
			}
			else
			{
				if (spVar != NULL)
					kfree(spVar);
				if (spCtrl != NULL)
					kfree(spCtrl);
				spVar = NULL;
				spCtrl = NULL;
				csl_dsp_sp_cnfg_msg((UInt16)enable, 0, 0, NULL, NULL);
			}
			break;
		case AUDDRV_USER_SP_VAR:
			Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_UserCtrlDSP, AUDDRV_USER_SET_SP_VAR *\n\r");

			if (ConfigSP == TRUE && spCtrl != NULL)
			{
				if (param == NULL)
					return -EINVAL;

				csl_dsp_sp_cnfg_msg((UInt16)enable, 0, 1, spCtrl, (UInt32 *)param);
				ConfigSP = FALSE;
			}
			else
			{
				if (size <= 0 || param == NULL)
					return -EINVAL;

				if (spVar == NULL)
				{
					spVar = (UInt32 *) kzalloc(size, GFP_KERNEL);
					if (!spVar)
						return -ENOMEM;
					memcpy(spVar, param, size);
				}
				csl_dsp_sp_ctrl_msg((UInt32 *)param);
			}

			break;
		case AUDDRV_USER_EQ_CTRL:
			Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_UserCtrlDSP, AUDDRV_USER_EQ_CTRL *\n\r");

			if (enable == TRUE)
			{
				if (param == NULL)
					return -EINVAL;

				userEQOn = TRUE;
				audio_cmf_filter((AudioCompfilter_t *) param);
			}
			else
				userEQOn = FALSE;

			break;
		default:
			Log_DebugPrintf(LOGID_AUDIO, "AUDDRV_User_CtrlDSP: Invalid request %d \n\r", UserCtrlType);
			break;
	}
#else
	Log_DebugPrintf(LOGID_AUDIO, "AUDDRV_User_CtrlDSP : dummy for AP only");
#endif
	return 0;
}

//=============================================================================
//
// Function Name: AUDDRV_User_HandleDSPInt
//
// Description:   Handle DSP Interrupt
//
//=============================================================================

void AUDDRV_User_HandleDSPInt ( UInt32 param1, UInt32 param2, UInt32 param3 )
{
	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* AUDDRV_User_HandleDSPInt *\n\r");
	if(sUserCB)
		((AUDDRV_User_CB) sUserCB) (param1, param2, param3);
}


//=============================================================================
//
// Function Name: AUDDRV_SetPCMOnOff
//
// Description: 	set PCM on/off for BT
//	this command will be removed from Rhea.
//
//=============================================================================

void AUDDRV_SetPCMOnOff(Boolean on_off)
{
	// By default the PCM port is occupied by trace port on development board
	if(on_off)
	{
		audio_control_dsp(DSPCMD_TYPE_COMMAND_DIGITAL_SOUND, on_off, 0, 0, 0, 0);

	}
	else
	{
		audio_control_dsp(DSPCMD_TYPE_COMMAND_DIGITAL_SOUND, on_off, 0, 0, 0, 0);

	}
}


//=============================================================================
//
// Function Name: AUDDRV_ControlFlagFor_CustomGain
//
// Description:   Set a flag to allow custom gain settings.
//				  If the flag is set the above three parameters are not set
//				  in AUDDRV_SetAudioMode( ) .
//
//=============================================================================

void AUDDRV_ControlFlagFor_CustomGain( Boolean on_off )
{
	controlFlagForCustomGain = on_off;
}

//============================================================================
//
// Function Name: AUDDRV_SetTelephonyMicGain
//
// Description:   Set ul gain of telephony path
//
//============================================================================
void AUDDRV_SetTelephonyMicGain(
			AUDIO_SOURCE_Enum_t	mic,
			Int16					gain,
			AUDIO_GAIN_FORMAT_t		gain_format
			)
{
    Log_DebugPrintf(LOGID_AUDIO,"AUDDRV_SetTelephonyMicGain: gain = 0x%x\n", gain);

    if (gain_format == AUDIO_GAIN_FORMAT_mB)
    {
      telephony_ul_gain_mB = gain;
    }

    audio_control_generic( AUDDRV_CPCMD_SetBasebandUplinkGain, telephony_ul_gain_mB, 0, 0, 0, 0);

    //sysparm.c(4990):	pg1_mem->shared_echo_fast_NLP_gain[1] = SYSPARM_GetAudioParmAccessPtr()->audio_parm[currentAudioMode].echoNlp_parms.echo_nlp_gain;
    //should also load this parameter in SetAudioMode() in CP build.
}

//============================================================================
//
// Function Name: AUDDRV_SetTelephonySpkrVolume
//
// Description:   Set dl volume of telephony path
//
//============================================================================
void AUDDRV_SetTelephonySpkrVolume(
			AUDIO_SINK_Enum_t		speaker,
			int						volume,
			AUDIO_GAIN_FORMAT_t		gain_format
			)
{
    Log_DebugPrintf(LOGID_AUDIO,"AUDDRV_SetTelephonySpkrVolume: volume = %d \n", volume );

    if (gain_format == AUDIO_GAIN_FORMAT_mB)
    {
      telephony_digital_gain_dB = (volume / 100) + 36;
      if ( telephony_digital_gain_dB > AUDIO_GetParmAccessPtr()[AUDDRV_GetAudioMode()].voice_volume_max )	//dB
        telephony_digital_gain_dB = AUDIO_GetParmAccessPtr()[AUDDRV_GetAudioMode()].voice_volume_max; //dB

      if( volume <=-10000 )  //less than -100dB
      {  //mute
        audio_control_generic( AUDDRV_CPCMD_SetBasebandDownlinkMute, 0, 0, 0, 0, 0);
      }
      else
      {
/*********
	OmegaVoice_Sysparm_t *omega_voice_parms = NULL;

	omega_voice_parms = AUDIO_GetParmAccessPtr()[AUDDRV_GetAudioMode()].omega_voice_parms;	//dB
	audio_control_generic(AUDDRV_CPCMD_SetOmegaVoiceParam,
				(UInt32)(&(omega_voice_parms[telephony_digital_gain_dB])),	//?
				0, 0, 0, 0);
********/

        //if parm4 (OV_volume_step) is zero, volumectrl.c will calculate OV volume step based on digital_gain_dB, VOICE_VOLUME_MAX and NUM_SUPPORTED_VOLUME_LEVELS.
        audio_control_generic( AUDDRV_CPCMD_SetBasebandDownlinkGain, volume,  //DSP accepts [-3600, 0] mB
               0, 0, 0, 0);
      }
    }
}

//=============================================================================
//
// Function Name: Spkr AUDDRV_Telephony_MuteMic
//
// Description:   Mute mic for voice call
//
//=============================================================================

void AUDDRV_Telephony_MuteMic (AUDIO_SOURCE_Enum_t mic)
{
	bmuteVoiceCall = TRUE;
	//pr_info(" bmuteVoiceCall = TRUE\r\n");
	audio_control_dsp( DSPCMD_TYPE_MUTE_DSP_UL, 0, 0, 0, 0, 0 );
}

//=============================================================================
//
// Function Name: Spkr AUDDRV_Telephony_UnmuteMic
//
// Description:   UnMute mic for voice call
//
//=============================================================================

void AUDDRV_Telephony_UnmuteMic (AUDIO_SOURCE_Enum_t mic)
{
	bmuteVoiceCall = FALSE;
	//pr_info(" bmuteVoiceCall = FALSE\r\n");
	audio_control_dsp( DSPCMD_TYPE_UNMUTE_DSP_UL, 0, 0, 0, 0, 0 );
}

//=============================================================================
//
// Function Name: AUDDRV_Telephony_MuteSpkr
//
// Description:   Mute speaker for voice call
//
//=============================================================================

void AUDDRV_Telephony_MuteSpkr (AUDIO_SINK_Enum_t speaker)
{
	audio_control_generic( AUDDRV_CPCMD_SetBasebandDownlinkMute, 0, 0, 0, 0, 0);
}

//=============================================================================
//
// Function Name: AUDDRV_Telephony_UnmuteSpkr
//
// Description:   UnMute speaker for voice call
//
//=============================================================================

void AUDDRV_Telephony_UnmuteSpkr (AUDIO_SINK_Enum_t speaker)
{
	audio_control_generic( AUDDRV_CPCMD_SetBasebandDownlinkUnmute, 0, 0, 0, 0, 0);
}

//=============================================================================
//
// Function Name: AUDDRV_SetULSpeechRecordGain
//
// Description:   set UL speech recording gain
//
//=============================================================================

void AUDDRV_SetULSpeechRecordGain(Int16 gain)
{
	audio_control_generic( AUDDRV_CPCMD_SetULSpeechRecordGain,
				(UInt32)gain, 0, 0, 0, 0 );
	return;
}

//*********************************************************************
/**
*	Get BTM headset NB or WB info

*	@return 	Boolean, TRUE for WB and FALSE for NB (8k)
*	@note
**********************************************************************/
Boolean AUDDRV_IsBTMWB( void )
{
	return IsBTM_WB;
}

//*********************************************************************
/**
*	Set BTM type

*	@param		Boolean isWB
*	@return 	none
*
*	@note	isWB=TRUE for BT WB headset; =FALSE for BT NB (8k) headset.
**********************************************************************/
void AUDDRV_SetBTMTypeWB( Boolean isWB)
{
	IsBTM_WB = isWB;
	//AUDDRV_SetPCMRate(IsBTM_WB);
}


//=============================================================================
// Private function prototypes
//=============================================================================


//=============================================================================
//
// Function Name: AUDDRV_Telephony_InitHW
//
// Description:   Enable the HW for Telephone voice call
//
//=============================================================================
static void AUDDRV_Telephony_InitHW (AUDIO_SOURCE_Enum_t mic,
                                    AUDIO_SINK_Enum_t speaker,
                                    AUDIO_SAMPLING_RATE_t sample_rate,
                                    Boolean bNeedDualMic)
{
    CSL_CAPH_HWCTRL_CONFIG_t config;
    UInt32 *memAddr = 0;

    Log_DebugPrintf(LOGID_AUDIO,
                    "\n\r\t* AUDDRV_Telephony_InitHW mic=%d, spkr=%d sample_rate=%ld*\n\r",
                    mic, speaker, sample_rate);

    memset(&config, 0, sizeof(CSL_CAPH_HWCTRL_CONFIG_t));

    //DL
    config.streamID = CSL_CAPH_STREAM_NONE;
    config.pathID = 0;
    config.source = CSL_CAPH_DEV_DSP;
    config.sink = AUDDRV_GetDRVDeviceFromSpkr(speaker);
    config.dmaCH = CSL_CAPH_DMA_NONE;
    //If DSP DL goes to IHF, Sample rate should be 48KHz.
    if (speaker == AUDIO_SINK_LOUDSPK)
    {
        config.source = CSL_CAPH_DEV_DSP_throughMEM;
    	config.src_sampleRate = AUDIO_SAMPLING_RATE_48000;
    }
    else
    {
    	config.src_sampleRate = sample_rate;
    }
    config.snk_sampleRate = AUDIO_SAMPLING_RATE_48000;

	if(config.sink == CSL_CAPH_DEV_HS)
	{
	    config.chnlNum = AUDIO_CHANNEL_STEREO;
	}
	else
	{
	    config.chnlNum = AUDIO_CHANNEL_MONO;
	}

    config.bitPerSample = AUDIO_24_BIT_PER_SAMPLE;

    sink = config.sink;
	if(sink == CSL_CAPH_DEV_IHF)
	{
		memAddr = AUDIO_GetIHF48KHzBufferBaseAddress();

        config.src_sampleRate = AUDIO_SAMPLING_RATE_48000;
		config.source = CSL_CAPH_DEV_DSP_throughMEM; //csl_caph_EnablePath() handles the case DSP_MEM when sink is IHF

		csl_caph_hwctrl_setDSPSharedMemForIHF((UInt32)memAddr);
	}
	else
	{
	    config.source = CSL_CAPH_DEV_DSP;
	}


    telephonyPathID.dlPathID = csl_caph_hwctrl_EnablePath(config);

    //UL
    config.streamID = CSL_CAPH_STREAM_NONE;
    config.pathID = 0;
    config.source = AUDDRV_GetDRVDeviceFromMic(mic);
    config.sink = CSL_CAPH_DEV_DSP;
    config.dmaCH = CSL_CAPH_DMA_NONE;
    config.src_sampleRate = AUDIO_SAMPLING_RATE_48000;
    config.snk_sampleRate = sample_rate;
    config.chnlNum = AUDIO_CHANNEL_MONO;
    config.bitPerSample = AUDIO_24_BIT_PER_SAMPLE;

    telephonyPathID.ulPathID = csl_caph_hwctrl_EnablePath(config);

    Log_DebugPrintf(LOGID_AUDIO, "\n* AUDDRV_Telephony_InitHW bNeedDualMic=%d *\n\r", bNeedDualMic );
    
    //If Dual Mic is enabled. Theoretically DMIC3 or DMIC4 are used
    //Here Let us assume it is DMIC3. It can be changed.
    if(bNeedDualMic==TRUE)
    {
        config.streamID = CSL_CAPH_STREAM_NONE;
        config.pathID = 0;
        config.source = MIC_NOISE_CANCEL;
        config.sink = CSL_CAPH_DEV_DSP;
        config.dmaCH = CSL_CAPH_DMA_NONE;
        config.src_sampleRate = AUDIO_SAMPLING_RATE_48000;
        config.snk_sampleRate = sample_rate;
        config.chnlNum = AUDIO_CHANNEL_MONO;
        config.bitPerSample = AUDIO_24_BIT_PER_SAMPLE;

        telephonyPathID.ul2PathID = csl_caph_hwctrl_EnablePath(config);
    }

    return;
}


//=============================================================================
//
// Function Name: AUDDRV_Telephony_DeinitHW
//
// Description:   Disable the HW for Telephone voice call
//
//=============================================================================
static void AUDDRV_Telephony_DeinitHW (void)
{
    CSL_CAPH_HWCTRL_CONFIG_t config;
	Log_DebugPrintf(LOGID_AUDIO,
                    "\n\r\t* AUDDRV_Telephony_DeinitHW *\n\r");

    memset(&config, 0, sizeof(CSL_CAPH_HWCTRL_CONFIG_t));

    AUDDRV_HWControl_DisableSideTone(AUDDRV_GetAudioMode());

    config.streamID = CSL_CAPH_STREAM_NONE;
    config.pathID = telephonyPathID.ulPathID;

    (void)csl_caph_hwctrl_DisablePath(config);

	if( telephonyPathID.ul2PathID != 0)
    {
        config.streamID = CSL_CAPH_STREAM_NONE;
        config.pathID = telephonyPathID.ul2PathID;

        (void)csl_caph_hwctrl_DisablePath(config);
    }

    config.streamID = CSL_CAPH_STREAM_NONE;
    config.pathID = telephonyPathID.dlPathID;

    (void)csl_caph_hwctrl_DisablePath(config);

	sink = CSL_CAPH_DEV_NONE;

    return;
}


/****************************************************************************
*
*  Function Name: AUDDRV_HWControl_SetFilter(AUDDRV_HWCTRL_FILTER_e filter,
*                                                     void* coeff)
*
*  Description: Load filter coefficients
*
****************************************************************************/
static void AUDDRV_HWControl_SetFilter(AUDDRV_HWCTRL_FILTER_e filter, void* coeff)
{
    if (filter == AUDDRV_SIDETONE_FILTER)
    {
        csl_caph_audioh_sidetone_load_filter((UInt32 *)coeff);
    }
}

/****************************************************************************
*
*  Function Name: AUDDRV_HWControl_EnableSideTone(AudioMode_t audio_mode)
*
*  Description: Enable Sidetone path
*
****************************************************************************/
static void AUDDRV_HWControl_EnableSideTone(AudioMode_t audio_mode)
{
    AUDDRV_PATH_Enum_t pathId = AUDDRV_PATH_VIBRA_OUTPUT;
    switch(audio_mode)
    {
        case AUDIO_MODE_HEADSET:
        case AUDIO_MODE_TTY:
        case AUDIO_MODE_HEADSET_WB:
        case AUDIO_MODE_TTY_WB:
	    pathId = AUDDRV_PATH_HEADSET_OUTPUT;
	    break;
        case AUDIO_MODE_HANDSET:
        case AUDIO_MODE_HAC:
        case AUDIO_MODE_HANDSET_WB:
        case AUDIO_MODE_HAC_WB:
	    pathId = AUDDRV_PATH_EARPICEC_OUTPUT;
	    break;
        case AUDIO_MODE_SPEAKERPHONE:
        case AUDIO_MODE_SPEAKERPHONE_WB:
	    pathId = AUDDRV_PATH_IHF_OUTPUT;
	    break;

	default:
            ;
    }
    csl_caph_audioh_sidetone_control((int)pathId, TRUE);
}


/****************************************************************************
*
*  Function Name: AUDDRV_HWControl_DisableSideTone(AudioMode_t audio_mode)
*
*  Description: Disable Sidetone path
*
****************************************************************************/
static void AUDDRV_HWControl_DisableSideTone(AudioMode_t audio_mode)
{
    AUDDRV_PATH_Enum_t pathId = AUDDRV_PATH_VIBRA_OUTPUT;
    switch(audio_mode)
    {
        case AUDIO_MODE_HEADSET:
        case AUDIO_MODE_TTY:
        case AUDIO_MODE_HEADSET_WB:
        case AUDIO_MODE_TTY_WB:
	    pathId = AUDDRV_PATH_HEADSET_OUTPUT;
	    break;
        case AUDIO_MODE_HANDSET:
        case AUDIO_MODE_HAC:
        case AUDIO_MODE_HANDSET_WB:
        case AUDIO_MODE_HAC_WB:
	    pathId = AUDDRV_PATH_EARPICEC_OUTPUT;
	    break;
        case AUDIO_MODE_SPEAKERPHONE:
        case AUDIO_MODE_SPEAKERPHONE_WB:
	    pathId = AUDDRV_PATH_IHF_OUTPUT;
	    break;

	default:
            ;
    }
    csl_caph_audioh_sidetone_control((int)pathId, FALSE);
}


//=============================================================================
//
// Function Name: auddrv_SetAudioMode_mic
//
// Description:   set audio mode on mic path.
//
//=============================================================================

static void auddrv_SetAudioMode_mic( AudioMode_t arg_audio_mode, unsigned int arg_pathID )
{
	UInt16 gainTemp1=0, gainTemp2=0, gainTemp3=0, gainTemp4=0;

	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* auddrv_SetAudioMode_mic() arg_audio_mode==%d\n\r", arg_audio_mode );

	//Load the mic gains from sysparm.

	/***
	do not touch DSP UL gain in this function.
	if(isDSPNeeded == TRUE)
	{
		dspULGain = 64; //AUDIO_GetParmAccessPtr()[mode].echoNlp_parms.echo_nlp_gain;
		audio_control_generic( AUDDRV_CPCMD_SetBasebandUplinkGain,
				dspULGain, 0, 0, 0, 0);
	}
	***/

	gainTemp1 = AUDIO_GetParmAccessPtr()[arg_audio_mode].mic_pga;  //Q13p2
	csl_caph_audioh_setMicPga_by_mB( ((int)gainTemp1)*25 );

	gainTemp1 = AUDIO_GetParmAccessPtr()[arg_audio_mode].amic_dga_coarse_gain; //Q13p2 dB
	gainTemp2 = AUDIO_GetParmAccessPtr()[arg_audio_mode].amic_dga_fine_gain; //Q13p2 dB

	gainTemp1 = AUDIO_GetParmAccessPtr()[arg_audio_mode].dmic1_dga_coarse_gain;
	// dmic1_dga_coarse_gain is the same register as amic_dga_coarse_gain
	gainTemp2 = AUDIO_GetParmAccessPtr()[arg_audio_mode].dmic1_dga_fine_gain;

	gainTemp3 = AUDIO_GetParmAccessPtr()[arg_audio_mode].dmic2_dga_coarse_gain;
	gainTemp4 = AUDIO_GetParmAccessPtr()[arg_audio_mode].dmic2_dga_fine_gain;

	csl_caph_audioh_vin_set_cic_scale_by_mB ( ((int)gainTemp1)*25,
		((int)gainTemp2)*25,
		((int)gainTemp3)*25,
		((int)gainTemp4)*25
		);

	gainTemp1 = AUDIO_GetParmAccessPtr()[arg_audio_mode].dmic3_dga_coarse_gain;
	gainTemp2 = AUDIO_GetParmAccessPtr()[arg_audio_mode].dmic3_dga_fine_gain;
	gainTemp3 = AUDIO_GetParmAccessPtr()[arg_audio_mode].dmic4_dga_coarse_gain;
	gainTemp4 = AUDIO_GetParmAccessPtr()[arg_audio_mode].dmic4_dga_fine_gain;

	csl_caph_audioh_nvin_set_cic_scale_by_mB ( ((int)gainTemp1)*25,
		((int)gainTemp2)*25,
		((int)gainTemp3)*25,
		((int)gainTemp4)*25
		);

}


//=============================================================================
//
// Function Name: auddrv_SetAudioMode_speaker
//
// Description:   set audio mode on speaker path.
//
//=============================================================================

static void auddrv_SetAudioMode_speaker( AudioMode_t arg_audio_mode, unsigned int arg_pathID )
{
	int mixerInputGain; // Register value.
	int mixerOutputFineGain;  // Bit12:0, Output Fine Gain
	int mixerOutputBitSelect;
	int pmu_gain = 0;

	CSL_CAPH_HWConfig_Table_t *path = NULL;
	CSL_CAPH_SRCM_MIX_OUTCHNL_e outChnl = CSL_CAPH_SRCM_CH_NONE;

	Log_DebugPrintf(LOGID_AUDIO, "\n\r\t* auddrv_SetAudioMode_speaker() arg_audio_mode==%d\n\r", arg_audio_mode );

		//Load the speaker gains form sysparm.
	/******
		Int16 dspDLGain = 0;
	
		// Set DSP DL gain from sysparm.
		if(isDSPNeeded == TRUE)
		{
			dspDLGain = 64; //AUDIO_GetParmAccessPtr()[mode].echo_nlp_downlink_volume_ctrl;
			audio_control_generic( AUDDRV_CPCMD_SetBasebandDownlinkGain,
							dspDLGain, 0, 0, 0, 0);
		}
		********/
	
		//determine which mixer output to apply the gains to
	
		switch ( arg_audio_mode )
		{
		case AUDIO_MODE_HANDSET:
		case AUDIO_MODE_HANDSET_WB:
		case AUDIO_MODE_HAC:
		case AUDIO_MODE_HAC_WB:
			outChnl = CSL_CAPH_SRCM_STEREO_CH2_L;
			break;
	
		case AUDIO_MODE_HEADSET:
		case AUDIO_MODE_HEADSET_WB:
		case AUDIO_MODE_TTY:
		case AUDIO_MODE_TTY_WB:
			//outChnl = (CSL_CAPH_SRCM_STEREO_CH1_L | CSL_CAPH_SRCM_STEREO_CH1_R);
			outChnl = CSL_CAPH_SRCM_STEREO_CH1;
			break;
	
		case AUDIO_MODE_SPEAKERPHONE:
		case AUDIO_MODE_SPEAKERPHONE_WB:
			outChnl = CSL_CAPH_SRCM_STEREO_CH2_R;
			
			//for the case of Stereo_IHF
			outChnl = (CSL_CAPH_SRCM_STEREO_CH2_R | CSL_CAPH_SRCM_STEREO_CH2_L);
			break;
	
		case AUDIO_MODE_BLUETOOTH:
			//does it go through HW mixer gain?
			//outChnl = (CSL_CAPH_SRCM_STEREO_CH1_L | CSL_CAPH_SRCM_STEREO_CH1_R);
			outChnl = CSL_CAPH_SRCM_STEREO_CH1;
			break;
	
		default:
			break;
		}
	
	
		//Load HW Mixer gains from sysparm
	
		mixerInputGain = (short) AUDIO_GetParmAccessPtr()[arg_audio_mode].srcmixer_input_gain_l; //Q13p2 dB
		mixerInputGain = mixerInputGain*25; //into mB
		//mixerInputGain = (short) AUDIO_GetParmAccessPtr()[arg_audio_mode].srcmixer_input_gain_r;
		//mixerInputGain = mixerInputGain*25; //into mB
	
		//determine which which mixer input to apply the gains to
	
		if( arg_pathID >= 1 )
			path = &HWConfig_Table[ arg_pathID - 1 ];
	
		if (path != 0)
		{
			csl_caph_srcmixer_set_mix_in_gain( path->srcmRoute[0][0].inChnl, outChnl, mixerInputGain, mixerInputGain);
		}
		else
		{
			csl_caph_srcmixer_set_mix_all_in_gain( outChnl, mixerInputGain, mixerInputGain);
		}
	
		mixerOutputFineGain = (short) AUDIO_GetParmAccessPtr()[arg_audio_mode].srcmixer_output_fine_gain_l; //Q13p2 dB
		mixerOutputFineGain = mixerOutputFineGain*25; //into mB
		//mixerOutputFineGain = (short) AUDIO_GetParmAccessPtr()[arg_audio_mode].srcmixer_output_fine_gain_r;
		//mixerOutputFineGain = mixerOutputFineGain*25; //into mB
	
	
		mixerOutputBitSelect = (short) AUDIO_GetParmAccessPtr()[arg_audio_mode].srcmixer_output_coarse_gain_l; //Q13p2 dB
		mixerOutputBitSelect = mixerOutputBitSelect / 24; //into bit_shift
		//mixerOutputBitSelect = (short) AUDIO_GetParmAccessPtr()[arg_audio_mode].srcmixer_output_coarse_gain_r;
		//mixerOutputBitSelect = mixerOutputBitSelect / 24; //into bit_shift
	
		csl_caph_srcmixer_set_mix_out_bit_select(outChnl, mixerOutputBitSelect);
		csl_caph_srcmixer_set_mix_out_gain( outChnl, mixerOutputFineGain );
	
		//Config sidetone
		{
		Int32 *coeff = NULL;
		UInt16 gain = 0;
		UInt16 enable = 0;
		SysAudioParm_t* pAudioParm;
		pAudioParm = AUDIO_GetParmAccessPtr();
	
		enable = AUDIO_GetParmAccessPtr()[arg_audio_mode].hw_sidetone_enable;
		if (!enable)
		{
			AUDDRV_HWControl_DisableSideTone(arg_audio_mode);
		}
		else
		{
			//first step: enable sidetone
			AUDDRV_HWControl_EnableSideTone(arg_audio_mode);

			//second step: set filter and gain.
			coeff = &(AUDIO_GetParmAccessPtr()[arg_audio_mode].hw_sidetone_eq[0]);
			AUDDRV_HWControl_SetFilter(AUDDRV_SIDETONE_FILTER, (void *)coeff);
			
			gain = AUDIO_GetParmAccessPtr()[arg_audio_mode].hw_sidetone_gain;
			csl_caph_audioh_sidetone_set_gain(gain);
		}
		}
	
		//Load PMU gain from sysparm.
		switch ( arg_audio_mode )
		{
		case AUDIO_MODE_HANDSET:
		case AUDIO_MODE_HANDSET_WB:
		case AUDIO_MODE_HAC:
		case AUDIO_MODE_HAC_WB:
			break;
	
		case AUDIO_MODE_HEADSET:
		case AUDIO_MODE_HEADSET_WB:
		case AUDIO_MODE_TTY:
		case AUDIO_MODE_TTY_WB:
			//enum {
			//		PMU_AUDIO_HS_RIGHT,
			//		PMU_AUDIO_HS_LEFT,
			//		PMU_AUDIO_HS_BOTH
			//};
	
			pmu_gain = (short) AUDIO_GetParmAccessPtr()[arg_audio_mode].ext_speaker_pga_l; //Q13p2 dB
			SetGainOnExternalAmp_mB(AUDIO_SINK_HEADSET, pmu_gain*25, 1); //PMU_AUDIO_HS_LEFT);
	
			pmu_gain = (short) AUDIO_GetParmAccessPtr()[arg_audio_mode].ext_speaker_pga_r; //Q13p2 dB
			SetGainOnExternalAmp_mB(AUDIO_SINK_HEADSET, pmu_gain*25, 0); //PMU_AUDIO_HS_RIGHT);
			break;
	
		case AUDIO_MODE_SPEAKERPHONE:
		case AUDIO_MODE_SPEAKERPHONE_WB:
	
			pmu_gain = (short) AUDIO_GetParmAccessPtr()[arg_audio_mode].ext_speaker_pga_l; //Q13p2 dB
			SetGainOnExternalAmp_mB( AUDIO_SINK_LOUDSPK, pmu_gain*25, 0); //PMU_AUDIO_HS_BOTH);
			break;
	
		default:
			break;
		}


}


// ==========================================================================
//
// Function Name: AUDDRV_GetDRVDeviceFromMic
//
// Description: Get the audio driver Device from the Microphone selection.
//
// =========================================================================
static CSL_CAPH_DEVICE_e AUDDRV_GetDRVDeviceFromMic (AUDIO_SOURCE_Enum_t mic)
{
	CSL_CAPH_DEVICE_e dev = CSL_CAPH_DEV_NONE;

	Log_DebugPrintf(LOGID_SOC_AUDIO, "AUDDRV_GetDRVDeviceFromMic:: mic = 0x%x\n", mic);

      switch (mic)
      {
        case AUDIO_SOURCE_UNDEFINED:
            dev = CSL_CAPH_DEV_NONE;
            break;

         case AUDIO_SOURCE_SPEECH_DIGI:
         //case AUDDRV_DUAL_MIC_DIGI12:
         //case AUDDRV_DUAL_MIC_DIGI21:
            dev = CSL_CAPH_DEV_DIGI_MIC;
            break;

         case AUDIO_SOURCE_DIGI1:
            dev = CSL_CAPH_DEV_DIGI_MIC_L;
            break;

         case AUDIO_SOURCE_DIGI2:
            dev = CSL_CAPH_DEV_DIGI_MIC_R;
            break;

         case AUDIO_SOURCE_ANALOG_MAIN:
            dev = CSL_CAPH_DEV_ANALOG_MIC;
            break;

         case AUDIO_SOURCE_ANALOG_AUX:
            dev = CSL_CAPH_DEV_HS_MIC;
            break;

         case AUDIO_SOURCE_BTM:
            dev = CSL_CAPH_DEV_BT_MIC;
            break;

         case AUDIO_SOURCE_USB:
            dev = CSL_CAPH_DEV_MEMORY;
            break;


        default:
			break;
    };

	return dev;
}

// ==========================================================================
//
// Function Name: AUDDRV_GetDRVDeviceFromSpkr
//
// Description: Get the audio driver Device from the Speaker selection.
//
// =========================================================================
static CSL_CAPH_DEVICE_e AUDDRV_GetDRVDeviceFromSpkr (AUDIO_SINK_Enum_t spkr)
{
	CSL_CAPH_DEVICE_e dev = CSL_CAPH_DEV_NONE;

	Log_DebugPrintf(LOGID_SOC_AUDIO, "AUDDRV_GetDRVDeviceFromSpkr:: spkr = 0x%x\n", spkr);

      switch (spkr)
      {
        case AUDIO_SINK_UNDEFINED:
            dev = CSL_CAPH_DEV_NONE;
            break;

         case AUDIO_SINK_HANDSET:
            dev = CSL_CAPH_DEV_EP;
            break;

         case AUDIO_SINK_LOUDSPK:
            dev = CSL_CAPH_DEV_IHF;
            break;

         case AUDIO_SINK_HEADSET:
            dev = CSL_CAPH_DEV_HS;
            break;

         case AUDIO_SINK_VIBRA:
            dev = CSL_CAPH_DEV_VIBRA;
            break;

         case AUDIO_SINK_BTM:
            dev = CSL_CAPH_DEV_BT_SPKR;
            break;

         case AUDIO_SINK_USB:
            dev = CSL_CAPH_DEV_MEMORY;
            break;

        default:
			break;
    };

	return dev;
}


static UInt32* AUDIO_GetIHF48KHzBufferBaseAddress (void)
{
        // special path for IHF voice call
        // need to use the physical address
		// Linux only change
		AP_SharedMem_t *ap_shared_mem_ptr = ioremap_nocache(AP_SH_BASE, AP_SH_SIZE);
		// Linux only : to get the physical address use the virtual address to compute offset and
		// add to the base address
		UInt32 *memAddr = (UInt32 *)(AP_SH_BASE + ((UInt32)&(ap_shared_mem_ptr->shared_aud_out_buf_48k[0][0])
													- (UInt32)ap_shared_mem_ptr));

        return memAddr;

}
