/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

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


#ifndef __CAPH_COMMON_H__
#define __CAPH_COMMON_H__


#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/kernel.h>


#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/initval.h>
#include <linux/wakelock.h>

#include "bcm_audio_devices.h"

#ifdef	CONFIG_SND_BCM_PREALLOC_MEM_FOR_PCM
#define	IS_PCM_MEM_PREALLOCATED		1
#else
#define	IS_PCM_MEM_PREALLOCATED		0
#endif

#ifndef MAX_PLAYBACK_DEV
#define MAX_PLAYBACK_DEV 3
#endif

#if !defined(CONFIG_SND_BCM_AUDIO_DEBUG_OFF)
//#if 1
void _bcm_snd_printk(unsigned int level, const char *path, int line, const char *format, ...);
#define BCM_AUDIO_DEBUG(format, args...) \
	_bcm_snd_printk(2, __FILE__, __LINE__, format, ##args)

#define DEBUG(format, args...) \
	_bcm_snd_printk(2, __FILE__, __LINE__, format, ##args)

#else
#define BCM_AUDIO_DEBUG(format, args...)	do { } while (0)
#define DEBUG(format, args...)	do { } while (0)
#endif



#define	MIXER_STREAM_FLAGS_CAPTURE	0x00000001
#define	MIXER_STREAM_FLAGS_CALL		0x00000002
#define	MIXER_STREAM_FLAGS_FM		0x00000004

#define	CAPH_MIXER_NAME_LENGTH		20	//Max length of a mixer name
#define	MIC_TOTAL_COUNT_FOR_USER	AUDCTRL_MIC_DIGI3
#define	CAPH_MAX_CTRL_LINES			((MIC_TOTAL_COUNT_FOR_USER>AUDCTRL_SPK_TOTAL_COUNT)?MIC_TOTAL_COUNT_FOR_USER:AUDCTRL_SPK_TOTAL_COUNT)
#define	CAPH_MAX_PCM_STREAMS		8



typedef	struct _TCtrl_Line
{
	Int8 strName[CAPH_MIXER_NAME_LENGTH];
	Int32 iVolume[2];
	Int32 iMute[2];
}TCtrl_Line, *PTCtrl_Line;



typedef	struct _TPcm_Stream_Ctrls
{
	Int32	 iFlags;
	Int32	 iTotalCtlLines;
	Int32	 iLineSelect[MAX_PLAYBACK_DEV];	//Multiple selection, For playback sink, one bit represent one sink; for capture source, 
	char strStreamName[CAPH_MIXER_NAME_LENGTH];
	TCtrl_Line	ctlLine[CAPH_MAX_CTRL_LINES];
	snd_pcm_uframes_t	 stream_hw_ptr;
	TIDChanOfDev	dev_prop;
	void   *pSubStream;	
	//Int32    drvHandle;
	
}TPcm_Stream_Ctrls, *PTPcm_Stream_Ctrls;


typedef struct brcm_alsa_chip
{
	struct snd_card *card;
	TPcm_Stream_Ctrls	streamCtl[CAPH_MAX_PCM_STREAMS];

	/* workqueue */
	struct work_struct work_play;
    struct work_struct work_capt;

	Int32	pi32LoopBackTestParam[3];	//loopback test
	Int32	iEnablePhoneCall;			//Eanble/disable audio path for phone call
	Int32	iMutePhoneCall[2];	//UL mute and DL mute			//Mute MIC for phone call
	Int32	pi32SpeechMixOption[CAPH_MAX_PCM_STREAMS];//Sppech mixing option, 0x00 - none, 0x01 - Downlink, 0x02 - uplink, 0x03 - both
	//AT-AUD
	Int32	i32AtAudHandlerParms[7];	
	Int32	pi32BypassVibraParam[3];	//Bypass Vibra: bEnable, strength, direction
    Int32   iEnableFM;                  //Enable/disable FM radio receiving
	Int32	iEnableBTTest;				//Enable/disable BT production test
	Int32	pi32CfgIHF[2];	//integer[0] -- 1 for mono, 2 for stereo; integer[1] -- data mixing option if channel is mono,  1 for left, 2 for right, 3 for (L+R)/2
 } brcm_alsa_chip_t;


void caphassert(const char *fcn, int line, const char *expr);
#define CAPH_ASSERT(e)      ((e) ? (void) 0 : caphassert(__func__, __LINE__, #e))


enum	CTL_STREAM_PANEL_t
{
	CTL_STREAM_PANEL_PCMOUT1=1,
	CTL_STREAM_PANEL_FIRST=CTL_STREAM_PANEL_PCMOUT1,
	CTL_STREAM_PANEL_PCMOUT2,	
	CTL_STREAM_PANEL_VOIPOUT,
	CTL_STREAM_PANEL_PCMIN,
	CTL_STREAM_PANEL_SPEECHIN,
	CTL_STREAM_PANEL_VOIPIN,
	CTL_STREAM_PANEL_VOICECALL,
	CTL_STREAM_PANEL_FM,
	CTL_STREAM_PANEL_LAST
};



enum	CTL_FUNCTION_t
{
	CTL_FUNCTION_VOL = 1,
	CTL_FUNCTION_MUTE,
	CTL_FUNCTION_LOOPBACK_TEST,
	CTL_FUNCTION_PHONE_ENABLE,
	CTL_FUNCTION_PHONE_CALL_MIC_MUTE,
	CTL_FUNCTION_SPEECH_MIXING_OPTION,
	CTL_FUNCTION_FM_ENABLE,
	CTL_FUNCTION_FM_FORMAT,
	CTL_FUNCTION_AT_AUDIO,
	CTL_FUNCTION_BYPASS_VIBRA,
	CTL_FUNCTION_BT_TEST,
	CTL_FUNCTION_CFG_IHF
};

enum	AT_AUD_Ctl_t
{
	AT_AUD_CTL_INDEX,
	AT_AUD_CTL_DBG_LEVEL,
	AT_AUD_CTL_HANDLER,
	AT_AUD_CTL_TOTAL
};


enum	AT_AUD_Handler_t
{
	AT_AUD_HANDLER_MODE,
	AT_AUD_HANDLER_VOL,
	AT_AUD_HANDLER_TST,
	AT_AUD_HANDLER_LOG,
	AT_AUD_HANDLER_LBTST
};


typedef enum voip_start_stop_type
{
	VoIP_DL_UL=0,
	VoIP_DL,
	VoIP_UL,
	VoIP_Total
}voip_start_stop_type_t;

typedef struct voip_data
{
	UInt32 codec_type;
	AUDCTRL_MIC_Enum_t mic;
	AUDCTRL_SPEAKER_t spk; 	
}voip_data_t;

typedef enum voip_codec_type
{
	VoIP_Codec_PCM_8K,
	VoIP_Codec_FR,
	VoIP_Codec_AMR475,
	VOIP_Codec_G711_U,
	VoIP_Codec_PCM_16K,
	VOIP_Codec_AMR_WB_7K
}voip_codec_type_t;

enum { 
  VoIP_Ioctl_GetVersion = _IOR ('H', 0x10, int), 
  VoIP_Ioctl_Start = _IOW ('H', 0x11, voip_start_stop_type_t), 
  VoIP_Ioctl_Stop = _IOW ('H', 0x12, voip_start_stop_type_t),	 
  VoIP_Ioctl_SetSource = _IOW('H', 0x13, int),
  VoIP_Ioctl_SetSink = _IOW('H', 0x14, int),
  VoIP_Ioctl_SetCodecType = _IOW('H', 0x15, int),
  VoIP_Ioctl_GetSource = _IOR('H', 0x16, int),
  VoIP_Ioctl_GetSink = _IOR('H', 0x17, int),
  VoIP_Ioctl_GetCodecType = _IOR('H', 0x18, int),
  VoIP_Ioctl_SetMode = _IOW('H', 0x19, int),
  VoIP_Ioctl_GetMode = _IOR('H', 0x1A, int),
 }; 


#define	CAPH_CTL_PRIVATE(dev, line, function) ((dev)<<16|(line)<<8|(function))
#define	STREAM_OF_CTL(private)		(((private)>>16)&0xFF)
#define	DEV_OF_CTL(private)			(((private)>>8)&0xFF)
#define	FUNC_OF_CTL(private)		((private)&0xFF)



//variables
extern int gAudioDebugLevel;

//functions
extern int __devinit PcmDeviceNew(struct snd_card *card);
extern int __devinit ControlDeviceNew(struct snd_card *card);
int __devinit HwdepDeviceNew(struct snd_card *card);

extern int 	AtAudCtlHandler_put(Int32 cmdIndex, brcm_alsa_chip_t* pChip, Int32	ParamCount, Int32 *Params); //at_aud_ctl.c
extern int	AtAudCtlHandler_get(Int32 cmdIndex, brcm_alsa_chip_t* pChip, Int32	ParamCount, Int32 *Params); //at_aud_ctl.c


#endif //__CAPH_COMMON_H__

