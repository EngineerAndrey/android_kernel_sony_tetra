/******************************************************************************
*
* Copyright 2009 - 2012  Broadcom Corporation
*
*  Unless you and Broadcom execute a separate written software license
*  agreement governing use of this software, this software is licensed to you
*  under the terms of the GNU General Public License version 2 (the GPL),
*  available at
*
*      http://www.broadcom.com/licenses/GPLv2.php
*
*  with the following added to such license:
*
*  As a special exception, the copyright holders of this software give you
*  permission to link this software with independent modules, and to copy and
*  distribute the resulting executable under terms of your choice, provided
*  that you also meet, for each linked independent module, the terms and
*  conditions of the license of that module.
*  An independent module is a module which is not derived from this software.
*  The special exception does not apply to any modifications of the software.
*
*  Notwithstanding the above, under no circumstances may you combine this
*  software in any way with any other Broadcom software provided under a
*  license other than the GPL, without Broadcom's express prior written
*  consent.
*
******************************************************************************/

/**
*
* @file   audio_controller.h
* @brief  Audio Controller interface
*
*****************************************************************************/
#ifndef __AUDIO_CONTROLLER_H__
#define __AUDIO_CONTROLLER_H__

/**
 * @addtogroup Audio_Controller
 * @{
 */
enum __ExtSpkrUsage_en_t {
	TelephonyUseExtSpkr,
	AudioUseExtSpkr,
	Audio2UseExtSpkr,	/* for 2nd audio playback */
	FMRadioUseExtSpkr
};
#define ExtSpkrUsage_en_t enum __ExtSpkrUsage_en_t

enum __AUDCTRL_SSP_PORT_e {
	AUDCTRL_SSP_4 = 1,	/*SSPI1 --- ASIC SSPI4, SSPI2 --- ASIC SSPI3 */
	AUDCTRL_SSP_3
};
#define AUDCTRL_SSP_PORT_e enum __AUDCTRL_SSP_PORT_e

enum __AUDCTRL_SSP_BUS_e {
	AUDCTRL_SSP_PCM,
	AUDCTRL_SSP_I2S
};
#define AUDCTRL_SSP_BUS_e enum __AUDCTRL_SSP_BUS_e

enum __AUDCTRL_HW_ACCESS_TYPE_en_t {
	AUDCTRL_HW_READ_GAIN,
	AUDCTRL_HW_WRITE_GAIN,
	AUDCTRL_HW_READ_REG,
	AUDCTRL_HW_WRITE_REG
};
#define AUDCTRL_HW_ACCESS_TYPE_en_t enum __AUDCTRL_HW_ACCESS_TYPE_en_t

enum __AUDCTRL_GAIN_POINT_en_t {
	AUDCTRL_EP_MIX_IN_GAIN,	/*0 */
	AUDCTRL_EP_MIX_BITSEL_GAIN,
	AUDCTRL_EP_MIX_FINE_GAIN,
	AUDCTRL_IHF_MIX_IN_GAIN,
	AUDCTRL_IHF_MIX_BITSEL_GAIN,
	AUDCTRL_IHF_MIX_FINE_GAIN,	/*5 */
	AUDCTRL_HS_LEFT_MIX_IN_GAIN,
	AUDCTRL_HS_LEFT_MIX_BITSEL_GAIN,
	AUDCTRL_HS_LEFT_MIX_FINE_GAIN,
	AUDCTRL_HS_RIGHT_MIX_IN_GAIN,
	AUDCTRL_HS_RIGHT_MIX_BITSEL_GAIN,	/*10 */
	AUDCTRL_HS_RIGHT_MIX_FINE_GAIN,

	AUDCTRL_PMU_HS_RIGHT_GAIN,	/*12 */
	AUDCTRL_PMU_HS_LEFT_GAIN,
	AUDCTRL_PMU_IHF_GAIN,
	AUDCTRL_PMU_HIGH_GAIN_MODE,	/*15 */

	AUDCTRL_SIDETONE_ENABLE,
	AUDCTRL_SIDETONE_GAIN,

	AUDCTRL_ANA_MIC_PGA = 100,
	AUDCTRL_DIGI_MIC1_COARSE_GAIN,
	AUDCTRL_DIGI_MIC1_FINE_GAIN,
	AUDCTRL_DIGI_MIC2_COARSE_GAIN,
	AUDCTRL_DIGI_MIC2_FINE_GAIN,
	AUDCTRL_DIGI_MIC3_COARSE_GAIN,	/*105 */
	AUDCTRL_DIGI_MIC3_FINE_GAIN,
	AUDCTRL_DIGI_MIC4_COARSE_GAIN,
	AUDCTRL_DIGI_MIC4_FINE_GAIN,

};
#define AUDCTRL_GAIN_POINT_en_t enum __AUDCTRL_GAIN_POINT_en_t

enum __AUDCTRL_PATH_en_t {
	AUDCTRL_PATH_NONE,
	AUDCTRL_PATH_P1,
	AUDCTRL_PATH_P2,
	AUDCTRL_PATH_VOIP_OUT,
	AUDCTRL_PATH_AUDIO_IN,
	AUDCTRL_PATH_SPEECH_IN,
	AUDCTRL_PATH_VOIP_IN,
	AUDCTRL_PATH_VOICECALL,
	AUDCTRL_PATH_FM_LISTENING,
	AUDCTRL_PATH_FM_CAPTURE,
	AUDCTRL_PATH_FM_TX,
	AUDCTRL_PATH_TOTAL_NUM
};
#define AUDCTRL_PATH_en_t enum __AUDCTRL_PATH_en_t

/**
*  @brief  This function is the Init entry point for Audio Controller
*
*  @param  none
*
*  @return none
*
****************************************************************************/
void AUDCTRL_Init(void);

/**
*  @brief  This function is to shut down Audio Controller
*
*  @param  none
*
*  @return none
*
****************************************************************************/
void AUDCTRL_Shutdown(void);

/**
*  @brief  Enable telephony audio path in HW and DSP
*
*  @param  source  (in)  uplink source, microphone selection
*  @param  sink	   (in)  downlink sink, speaker selection
*
*  @return none
*
****************************************************************************/
void AUDCTRL_EnableTelephony(AUDIO_SOURCE_Enum_t source,
			     AUDIO_SINK_Enum_t sink);

/**
*  @brief  Disable telephony audio path in HW and DSP
*
*  @param  none
*
*  @return none
*
****************************************************************************/
void AUDCTRL_DisableTelephony(void);

/**
*  @brief  Rate change telephony audio for DSP
*
*  @param  sample_rate	(in)  sample rate
*  @return none
*
****************************************************************************/
void AUDCTRL_Telephony_RateChange(unsigned int sample_rate);

/**
*  @brief  the rate change request function called by CAPI message listener
*
*  @param  codecID		(in) voice call speech codec ID
*
*  @return none
*
****************************************************************************/
void AUDCTRL_Telephony_RequestRateChange(UInt8 codecID);

/**
*  @brief  Change telephony audio path in HW and DSP
*
*  @param  source  (in)  uplink source, microphone selection
*  @param  sink    (in)  downlink sink, speaker selection
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetTelephonyMicSpkr(AUDIO_SOURCE_Enum_t source,
				 AUDIO_SINK_Enum_t sink);

/**
*  @brief  Set telephony speaker (downlink) volume
*
*  @param  speaker	(in)  downlink sink, speaker selection
*  @param  volume	(in)  downlink volume
*  @param  gain_format	 (in)  gain format
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetTelephonySpkrVolume(AUDIO_SINK_Enum_t speaker,
				    int volume,
				    AUDIO_GAIN_FORMAT_t gain_format);

/**
*  @brief  Get telephony speaker (downlink) volume
*
*  @param  gain_format	(in)  gain format
*
*  @return UInt32    dB
*
****************************************************************************/
int AUDCTRL_GetTelephonySpkrVolume(AUDIO_GAIN_FORMAT_t gain_format);

/**
*  @brief  Set telephony speaker (downlink) mute / un-mute
*
*  @param  speaker  (in)  downlink sink, speaker selection
*  @param  mute	    (in)  TRUE: mute;    FALSE: un-mute
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetTelephonySpkrMute(AUDIO_SINK_Enum_t speaker, Boolean mute);

/**
*  @brief  Set telephony microphone (uplink) gain
*
*  @param  mic		(in)  microphone selection
*  @param  gain	(in)  gain
*  @param  gain_format	(in)  gain format
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetTelephonyMicGain(AUDIO_SOURCE_Enum_t mic,
				 Int16 gain, AUDIO_GAIN_FORMAT_t gain_format);

/**
*  @brief  Set telephony mic (uplink) mute /un-mute
*
*  @param  mic    (in)  uplink source, microphone selection
*  @param  mute	  (in)  TRUE: mute;    FALSE: un-mute
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetTelephonyMicMute(AUDIO_SOURCE_Enum_t mic, Boolean mute);

/**
*   Get current (voice call) audio mode
*
*	@param		none
*
*	@return		AudioMode_t		(voice call) audio mode
*
****************************************************************************/
AudioMode_t GetAudioMode(void);

/**
*  Save audio mode before call SetAudioMode( )
*	@param		mode		(voice call) audio mode
*	@return		none
****************************************************************************/
void SaveAudioMode(AudioMode_t mode);

/**
*   Get current (voice call) audio app
*
*	@param		none
*
*	@return		AudioApp_t		audio app
*
****************************************************************************/
AudioApp_t GetAudioApp(void);

/**
*   Set current (voice call) audio app
*
*	@param		AudioApp_t		audio app
*
*	@return		none
*
****************************************************************************/
void SetAudioApp(AudioApp_t audio_app);

/*********************************************************************
*   Save audio app
*
*	@param		AudioApp_t		audio app
*	@return		none
**********************************************************************/
void SaveAudioApp(AudioApp_t audio_app);

/*********************************************************************
*   Do not need this audio app
*
*	@param		AudioApp_t		audio app
*      @return         none
**********************************************************************/
void RemoveAudioApp(AudioApp_t audio_app);

/*********************************************************************
*   Set (voice call) audio mode
*      @param          mode            (voice call) audio mode
*      @param          app             (voice call) audio app
*      @return         none
**********************************************************************/
void SetAudioMode(AudioMode_t mode, AudioApp_t app);

/**
*   Set audio mode for music playback. (no DSP voice)
*
*	@param		mode		music audio mode
*
*	@return		none
****************************************************************************/
void SetAudioMode_ForMusicPlayback(AudioMode_t mode,
					   unsigned int arg_pathID,
					   Boolean inHWlpbk);

/**
*   Set audio mode for music record. (no DSP voice)
*
*	@param		mode		music audio mode
*
*	@return		none
****************************************************************************/
void SetAudioMode_ForMusicRecord(AudioMode_t mode,
					 unsigned int arg_pathID);

/**
*   Get src and sink from audio mode
*
*	@param		mode		audio mode
*   @param      pMic        microphone
*   @param      pSpk        speaker
*
*	@return		none
****************************************************************************/
void AUDCTRL_GetSrcSinkByMode(AudioMode_t mode, AUDIO_SOURCE_Enum_t *pMic,
			      AUDIO_SINK_Enum_t *pSpk);

/**
*  @brief  Enable a playback path
*
*  @param  src  (in)  playback source
*  @param  sink (in)  playback sink
*  @param  spk  (in)  speaker selection
*  @param  numCh (in)  stereo, momo
*  @param  sr    (in)  sample rate
*  @param  pPathID (in)  to return pathID
*
*  @return none
*
****************************************************************************/
void AUDCTRL_EnablePlay(AUDIO_SOURCE_Enum_t source,
			AUDIO_SINK_Enum_t sink,
			AUDIO_NUM_OF_CHANNEL_t numCh,
			AUDIO_SAMPLING_RATE_t sr, unsigned int *pPathID);

/********************************************************************
*  @brief  Disable a playback path
*
*  @param  source	(in)  playback source
*  @param  sink	(in)  playback sink
*  @param  pathID	(in)  the pathID returned by CSL HW controller.
*
*  @return none
*
****************************************************************************/
void AUDCTRL_DisablePlay(AUDIO_SOURCE_Enum_t source,
			 AUDIO_SINK_Enum_t sink, unsigned int pathID);

/********************************************************************
*  @brief  start the stream for audio render
*
*  @param   streamID  (in) render audio stream id
*
*  @return Result_t status
*****************************************************************************/
Result_t AUDCTRL_StartRender(unsigned int streamID);

/********************************************************************
*  @brief  stop the stream for audio render
*
*  @param   streamID  (in) render audio stream id
*
*  @return Result_t status
*****************************************************************************/
Result_t AUDCTRL_StopRender(unsigned int streamID);

/********************************************************************
*  @brief  Set playback volume
*
*  @param  source	(in)  playback source
*  @param  sink	(in)  playback sink
*  @param  vol	(in)  volume to set
*  @param pathID (in) the pathID returned by CSL HW controller.
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetPlayVolume(AUDIO_SOURCE_Enum_t source,
			   AUDIO_SINK_Enum_t sink,
			   AUDIO_GAIN_FORMAT_t gainF,
			   int vol_left, int vol_right, unsigned int pathID);

/********************************************************************
*  @brief  mute/unmute playback
*
*  @param  source	(in)  playback source
*  @param  sink	(in)  playback sink
*  @param  mute	(in)  TRUE: mute, FALSE: unmute
*  @param pathID (in) the pathID returned by CSL HW controller.
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetPlayMute(AUDIO_SOURCE_Enum_t source,
			 AUDIO_SINK_Enum_t sink,
			 Boolean mute, unsigned int pathID);

/********************************************************************
*  @brief  switch speaker of playback
*
*  @param   source  Source
*  @param   sink	new Sink device
*  @param pathID (in) the pathID returned by CSL HW controller.
*  @return none
*
****************************************************************************/
void AUDCTRL_SwitchPlaySpk(AUDIO_SOURCE_Enum_t source,
			   AUDIO_SINK_Enum_t sink, unsigned int pathID);

/********************************************************************
*  @brief  Add a speaker to a playback path
*
*  @param   source  Source
*  @param   sink	(in)  playback sink
*  @param pathID (in) the pathID returned by CSL HW controller.
*
*  @return none
*
****************************************************************************/
void AUDCTRL_AddPlaySpk(AUDIO_SOURCE_Enum_t source,
			AUDIO_SINK_Enum_t sink, unsigned int pathID);

/********************************************************************
*  @brief  Remove a speaker to a playback path
*
*  @param   source  Source
*  @param  sink	(in)  playback sink
*  @param pathID (in) the pathID returned by CSL HW controller.
*
*  @return none
*
****************************************************************************/
void AUDCTRL_RemovePlaySpk(AUDIO_SOURCE_Enum_t source,
			   AUDIO_SINK_Enum_t sink, unsigned int pathID);

/********************************************************************
*  @brief  enable a record path
*
*  @param  source	(in)  record source
*  @param  sink	(in)  record sink
*  @param  numCh	(in)  stereo, mono
*  @param  sr	(in)  sample rate
*  @param  pPathID	(in)  to return pathID
*
*  @return none
*
****************************************************************************/
void AUDCTRL_EnableRecord(AUDIO_SOURCE_Enum_t source,
			  AUDIO_SINK_Enum_t sink,
			  AUDIO_NUM_OF_CHANNEL_t numCh,
			  AUDIO_SAMPLING_RATE_t sr, unsigned int *pPathID);

/********************************************************************
*  @brief  disable a record path
*
*  @param  source	(in)  record source
*  @param  sink	(in)  record sink
*  @param pathID (in) the pathID returned by CSL HW controller.
*
*  @return none
*
****************************************************************************/
void AUDCTRL_DisableRecord(AUDIO_SOURCE_Enum_t source,
			   AUDIO_SINK_Enum_t sink, unsigned int pathID);

/**
*
*  @brief  start the stream for audio capture
*
*  @param   streamID  (in) capture audio stream id
*
*  @return Result_t status
*****************************************************************************/
Result_t AUDCTRL_StartCapture(unsigned int streamID);

/**
*
*  @brief  stop the stream for a caph capture
*
*  @param   streamID  (in) capture audio stream id
*
*  @return Result_t status
*****************************************************************************/
Result_t AUDCTRL_StopCapture(unsigned int streamID);

/********************************************************************
*  @brief  Set gain of a record path
*
*  @param  source (in)  record source
*  @param  gainFormat	(in)  the gain format
*  @param  gainL	(in)  the left channel gain to set
*  @param  gainR	(in)  the right channel gain to set
*  @param pathID (in) the pathID returned by CSL HW controller.
s*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetRecordGain(AUDIO_SOURCE_Enum_t source,
			   AUDIO_GAIN_FORMAT_t gainFormat,
			   UInt32 gainL, UInt32 gainR, unsigned int pathID);

/********************************************************************
*  @brief  mute/unmute a record path
*
*  @param  source	(in)  record source
*  @param  mute	(in)  TRUE: mute, FALSE: unmute
*  @param pathID (in) the pathID returned by CSL HW controller.
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetRecordMute(AUDIO_SOURCE_Enum_t source,
			   Boolean mute, unsigned int pathID);

/********************************************************************
*  @brief  add a micophone to a record path
*
*  @param  source	(in)  record source
*
*  @return none
*
****************************************************************************/
void AUDCTRL_AddRecordMic(AUDIO_SOURCE_Enum_t source);

/********************************************************************
*  @brief  remove a micophone from a record path
*
*  @param  source	(in)  record source
*
*  @return none
*
****************************************************************************/
void AUDCTRL_RemoveRecordMic(AUDIO_SOURCE_Enum_t source);

/********************************************************************
*  @brief  enable or disable audio HW loopback
*
*  @param  enable_lpbk (in)  the audio mode
*  @param  mic         (in)  the input to loopback
*  @param  speaker     (in)  the output from loopback
*  @param  sidetone_mode     (in)  use sidetone loopback mode
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetAudioLoopback(Boolean enable_lpbk,
			      AUDIO_SOURCE_Enum_t mic,
			      AUDIO_SINK_Enum_t speaker, int sidetone_mode);

/********************************************************************
*  @brief  Set Arm2Sp Parameter
*
*  @param  mixMode        For selection of mixing with voice DL, UL, or both
*  @param  instanceId     Instance ID: 1 for arm2sp1, 2 for arm2sp2
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetArm2spParam(UInt32 mixMode, UInt32 instanceId);

/********************************************************************
*  @brief  Configure fm/pcm port
*
*  @param  port  SSP port number
*
*  @param  bus   protocol (I2S or PCM)
*
*  @return none
*
****************************************************************************/
void AUDCTRL_ConfigSSP(AUDCTRL_SSP_PORT_e port, AUDCTRL_SSP_BUS_e bus);

/********************************************************************
*  @brief  Control ssp tdm mode
*
*  @param  status ssp tdm status
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetSspTdmMode(Boolean status);

/********************************************************************
*  @brief  Enable bypass mode for vibrator
*
*  @param  none
*
*  @return none
*
****************************************************************************/
void AUDCTRL_EnableBypassVibra(void);

/********************************************************************
*  @brief  Disable vibrator in bypass mode
*
*  @param  none
*
*  @return none
*
****************************************************************************/
void AUDCTRL_DisableBypassVibra(void);

/********************************************************************
*  @brief  Set the strength to vibrator in bypass mode
*
*  @param  Strength  strength value
*
*  @param  direction vibrator moving direction
*
*  @return none
*
****************************************************************************/
void AUDCTRL_SetBypassVibraStrength(UInt32 Strength, int direction);

/********************************************************************
*  @brief  Set IHF mode
*
*  @param  IHF mode status (TRUE: stereo | FALSE: mono).
*
*  @return  none
*
****************************************************************************/
void AUDCTRL_SetIHFmode(Boolean stIHF);

/********************************************************************
*  @brief  Set BT mode
*
*  @param  BT mode status for BT production test.
*
*  @return  none
*
****************************************************************************/
void AUDCTRL_SetBTMode(int mode);

/********************************************************************
*  @brief  Hardware register access fucntion
*
*  @param
*
*  @return  int
*
****************************************************************************/
int AUDCTRL_HardwareControl(AUDCTRL_HW_ACCESS_TYPE_en_t access_type,
			    int arg1, int arg2, int arg3);

void SetGainOnExternalAmp_mB(AUDIO_SINK_Enum_t speaker, int gain_mB,
			     int left_right);

/**
*  @brief  This function controls the power on/off the power supply for
*	digital mics.
*
*  @param  powerOn	(in) TRUE: Power on, FALSE: Power off
*
*  @return void
*
****************************************************************************/
void powerOnDigitalMic(Boolean powerOn);

/**
*  @brief  This function gets the device enum mapping value from source
*
*  @param  source	(in)
*
*  @return device (out)
*
****************************************************************************/

CSL_CAPH_DEVICE_e getDeviceFromSrc(AUDIO_SOURCE_Enum_t source);

/**
*  @brief  This function gets the device enum mapping value from sink
*
*  @param  sink	(in)
*
*  @return device (out)
*
****************************************************************************/

CSL_CAPH_DEVICE_e getDeviceFromSink(AUDIO_SINK_Enum_t sink);

void AUDCTRL_EC(Boolean enable, UInt32 arg);
void AUDCTRL_NS(Boolean enable);

#endif /* #define __AUDIO_CONTROLLER_H__ */
