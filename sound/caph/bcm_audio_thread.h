/*******************************************************************************************
Copyright 2010 Broadcom Corporation.  All rights reserved.                                

Unless you and Broadcom execute a separate written software license agreement 
governing use of this software, this software is licensed to you under the 
terms of the GNU General Public License version 2, available at 
http://www.gnu.org/copyleft/gpl.html (the "GPL"). 

Notwithstanding the above, under no circumstances may you combine this software 
in any way with any other Broadcom software provided under a license other than 
the GPL, without Broadcom's express prior written consent.
*******************************************************************************************/
/**
*    @file   bcm_audio_thread.h
*    @brief  API declaration of hardware abstraction layer for Audio driver.
*   This code is OS independent and Device independent for audio device control.
****************************************************************************/


#ifndef _BCM_AUDIO_THREAD_H__
#define _BCM_AUDIO_THREAD_H__

//! The higher layer calls this Audio hardware abstraction layer to perform the following actions. This is expandable
//! if audio controller need to handle more requests.

typedef enum
{
	ACTION_AUD_OpenPlay,
	ACTION_AUD_ClosePlay,
	ACTION_AUD_StartPlay,
	ACTION_AUD_StopPlay,
	ACTION_AUD_PausePlay,
	ACTION_AUD_ResumePlay,
	ACTION_AUD_StartRecord,
	ACTION_AUD_StopRecord,
    ACTION_AUD_OpenRecord,
    ACTION_AUD_CloseRecord,
    ACTION_AUD_SetPrePareParameters,
    ACTION_AUD_AddChannel,
    ACTION_AUD_EnableTelephony,
    ACTION_AUD_DisableTelephony,
    ACTION_AUD_MutePlayback,
    ACTION_AUD_MuteRecord,
    ACTION_AUD_MuteTelephony,
    ACTION_AUD_EnableByPassVibra,
    ACTION_AUD_DisableByPassVibra,
    ACTION_AUD_SetVibraStrength,
    ACTION_AUD_SetPlaybackVolume,
    ACTION_AUD_SetRecordGain,
	ACTION_AUD_SetTelephonySpkrVolume,  
    ACTION_AUD_SwitchSpkr,
    ACTION_AUD_AddSpkr,
    ACTION_AUD_MuteVoicecall,
    ACTION_AUD_SetHWLoopback,
    ACTION_AUD_SetAudioMode,
    ACTION_AUD_EnableFMPlay,
    ACTION_AUD_DisableFMPlay,
    ACTION_AUD_SetARM2SPInst,
	ACTION_AUD_TOTAL			
} BRCM_AUDIO_ACTION_en_t;

typedef struct
{
    void*   drv_handle;
    TIDChanOfDev	*pdev_prop;
    UInt32 channels;
    UInt32 rate;
	Int32  vol[2];
	Int32  mixMode;
	Int32  callMode;
}BRCM_AUDIO_Param_Start_t;

typedef struct
{
    void*   drv_handle;
    TIDChanOfDev	*pdev_prop;
	Int32 callMode;

}BRCM_AUDIO_Param_Stop_t;

typedef struct
{
    void*   drv_handle;
    TIDChanOfDev	*pdev_prop;

}BRCM_AUDIO_Param_Pause_t;

typedef struct
{
    void*   drv_handle;
    TIDChanOfDev	*pdev_prop;
    UInt32 channels;
    UInt32 rate;

}BRCM_AUDIO_Param_Resume_t;

typedef struct
{
    void*   drv_handle;
    TIDChanOfDev	*pdev_prop;

}BRCM_AUDIO_Param_Open_t;

typedef struct
{
    void*   drv_handle;
    TIDChanOfDev	*pdev_prop;

}BRCM_AUDIO_Param_Close_t;

typedef struct
{
	unsigned long period_bytes;
	AUDIO_DRIVER_HANDLE_t  drv_handle;
    AUDIO_DRIVER_BUFFER_t buf_param;
    AUDIO_DRIVER_CONFIG_t drv_config;
	AUDIO_DRIVER_CallBackParams_t	cbParams;
}BRCM_AUDIO_Param_Prepare_t;

typedef struct
{
   Int32 hw_id; //source or sink
   Int32 device; //mic or speaker
   Int32 volume1;
   Int32 volume2;

}BRCM_AUDIO_Param_Volume_t;

typedef struct
{
   Int32 hw_id; //source or sink
   Int32 device; //mic or speaker
   Int32 mute1;
   Int32 mute2;

}BRCM_AUDIO_Param_Mute_t;


typedef struct
{
   Int32 cur_sink;
   Int32 new_sink;
   Int32 cur_spkr;
   Int32 new_spkr;

}BRCM_AUDIO_Param_Spkr_t;

typedef struct
{
   Int32 cur_spkr;
   Int32 new_spkr;
   Int32 cur_mic;
   Int32 new_mic;

}BRCM_AUDIO_Param_Call_t;

typedef struct
{
   Int32 parm;
   Int32 mic;
   Int32 spkr;
}BRCM_AUDIO_Param_Loopback_t;

typedef struct
{
   Int32 strength;
   Int32 direction;
}BRCM_AUDIO_Param_Vibra_t;

typedef struct
{
   Int32 hw_id;
   Int32 device;
   Int32 volume1;
   Int32 volume2;
   UInt32 fm_mix;
}BRCM_AUDIO_Param_FM_t;


typedef union{
	BRCM_AUDIO_Param_Start_t	param_start;
	BRCM_AUDIO_Param_Stop_t		param_stop;
	BRCM_AUDIO_Param_Pause_t	param_pause;
	BRCM_AUDIO_Param_Resume_t	param_resume;
	BRCM_AUDIO_Param_Open_t		parm_open;
	BRCM_AUDIO_Param_Close_t	parm_close;
	BRCM_AUDIO_Param_Volume_t	parm_vol;
	BRCM_AUDIO_Param_Mute_t		parm_mute;
	BRCM_AUDIO_Param_Spkr_t		parm_spkr;
	BRCM_AUDIO_Param_Call_t		parm_call;
	BRCM_AUDIO_Param_Loopback_t	parm_loop;
	BRCM_AUDIO_Param_Vibra_t	parm_vibra;
	BRCM_AUDIO_Param_FM_t		parm_FM;
	BRCM_AUDIO_Param_Prepare_t	parm_prepare;

} BRCM_AUDIO_Control_Params_un_t;

int LaunchAudioCtrlThread(void);

int TerminateAudioHalThread(void);


Result_t AUDIO_Ctrl_Trigger(
	BRCM_AUDIO_ACTION_en_t action_code,
	void *arg_param,
	void *callback,
	int block
	);



#endif	//_BRCM_AUDIO_THREAD_H__
