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

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm_params.h>
#include <sound/pcm.h>
#include <sound/asound.h>
#include <sound/rawmidi.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include "mobcom_types.h"
#include "resultcode.h"
#include "audio_consts.h"
#include "csl_caph.h"
#include "audio_vdriver.h"
#include "audio_ddriver.h"

#include "audio_controller.h"
#include "audio_caph.h"
#include "caph_common.h"

static Boolean isSTIHF = FALSE;

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//Volume control information
//
//-------------------------------------------------------------------------------------------
static int VolumeCtrlInfo(struct snd_kcontrol * kcontrol,	struct snd_ctl_elem_info * uinfo)
{

	int priv = kcontrol->private_value;
	int	stream = STREAM_OF_CTL(priv);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->value.integer.step = 1;
	switch(stream)
	{
		case CTL_STREAM_PANEL_PCMOUT1:
		case CTL_STREAM_PANEL_PCMOUT2:
		case CTL_STREAM_PANEL_VOIPOUT:
		case CTL_STREAM_PANEL_FM:
			uinfo->count = 2;
			uinfo->value.integer.min = MIN_VOLUME_mB;
			uinfo->value.integer.max = MAX_VOLUME_mB;
			break;
		case CTL_STREAM_PANEL_VOICECALL:
			uinfo->count = 1;
			uinfo->value.integer.min = MIN_VOICE_VOLUME_mB;
			uinfo->value.integer.max = MAX_VOICE_VOLUME_mB;
			break;
		case CTL_STREAM_PANEL_PCMIN:
		case CTL_STREAM_PANEL_SPEECHIN:
		case CTL_STREAM_PANEL_VOIPIN:
			uinfo->count = 1;
			uinfo->value.integer.min = MIN_GAIN_mB;
			uinfo->value.integer.max = MAX_GAIN_mB;
			break;
		default:
			break;
	}

	return 0;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//Get volume
//
//-------------------------------------------------------------------------------------------
static int VolumeCtrlGet(	struct snd_kcontrol * kcontrol,	struct snd_ctl_elem_value * ucontrol)
{
	brcm_alsa_chip_t*	pChip = (brcm_alsa_chip_t*)snd_kcontrol_chip(kcontrol);
	int priv = kcontrol->private_value;
	int	stream = STREAM_OF_CTL(priv);
	int	dev = DEV_OF_CTL(priv);
	s32	*pVolume;
	CAPH_ASSERT(stream>=CTL_STREAM_PANEL_FIRST && stream<CTL_STREAM_PANEL_LAST);
	stream--;
	pVolume = pChip->streamCtl[stream].ctlLine[dev].iVolume;

	//May need to get the value from driver
	ucontrol->value.integer.value[0] = pVolume[0];
	ucontrol->value.integer.value[1] = pVolume[1];

	return 0;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//Set volume
//    Update the mixer control only if the stream is not running.Call audio driver to apply when stream is running
//
//-------------------------------------------------------------------------------------------
static int VolumeCtrlPut(	struct snd_kcontrol * kcontrol,	struct snd_ctl_elem_value * ucontrol)
{
	brcm_alsa_chip_t*	pChip = (brcm_alsa_chip_t*)snd_kcontrol_chip(kcontrol);
	int priv = kcontrol->private_value;
	int	stream = STREAM_OF_CTL(priv);
	int	dev = DEV_OF_CTL(priv);
	s32	*pVolume;
	struct snd_pcm_substream *pStream=NULL;
	s32 *pCurSel;
	BRCM_AUDIO_Param_Volume_t parm_vol;

	CAPH_ASSERT(stream>=CTL_STREAM_PANEL_FIRST && stream<CTL_STREAM_PANEL_LAST);
	pVolume = pChip->streamCtl[stream-1].ctlLine[dev].iVolume;

	pVolume[0] = ucontrol->value.integer.value[0];
	pVolume[1] = ucontrol->value.integer.value[1];

	pCurSel = pChip->streamCtl[stream-1].iLineSelect;

	//Apply Volume if the stream is running
	switch(stream)
	{
		case CTL_STREAM_PANEL_PCMOUT1:
		case CTL_STREAM_PANEL_PCMOUT2:
		case CTL_STREAM_PANEL_VOIPOUT:
		{
			if(pCurSel[0] == dev) //if current sink is diffent, dont call the driver to change the volume
			{
				if(pChip->streamCtl[stream-1].pSubStream != NULL)
					pStream = (struct snd_pcm_substream *)pChip->streamCtl[stream-1].pSubStream;
				else
					break;

				BCM_AUDIO_DEBUG("VolumeCtrlPut stream state = %d\n",pStream->runtime->status->state);

				if(pStream->runtime->status->state == SNDRV_PCM_STATE_RUNNING || pStream->runtime->status->state == SNDRV_PCM_STATE_PAUSED) // SNDDRV_PCM_STATE_PAUSED
				{
					//call audio driver to set volume
					BCM_AUDIO_DEBUG("VolumeCtrlPut caling AUDCTRL_SetPlayVolume pVolume[0] =%d (0.25dB), pVolume[1]=%d\n", pVolume[0],pVolume[1]);
					parm_vol.source = pChip->streamCtl[stream-1].dev_prop.p[0].source;
					parm_vol.sink = pChip->streamCtl[stream-1].dev_prop.p[0].sink;
					parm_vol.volume1 = pVolume[0];
					parm_vol.volume2 = pVolume[1];
					parm_vol.stream = (stream - 1);
					AUDIO_Ctrl_Trigger(ACTION_AUD_SetPlaybackVolume,&parm_vol,NULL,0);
				}
			}
		}
		break;
		case CTL_STREAM_PANEL_FM:
		{
			if(pCurSel[0] == dev) //if current sink is diffent, dont call the driver to change the volume
			{
				//call audio driver to set volume
				BCM_AUDIO_DEBUG("VolumeCtrlPut caling AUDCTRL_SetPlayVolume pVolume[0] =%d (0.25dB), pVolume[1]=%d\n", pVolume[0],pVolume[1]);
				parm_vol.source = pChip->streamCtl[stream-1].dev_prop.p[0].source;
				parm_vol.sink = pChip->streamCtl[stream-1].dev_prop.p[0].sink;
				parm_vol.volume1 = pVolume[0];
				parm_vol.volume2 = pVolume[1];
				parm_vol.stream = (stream - 1);
				AUDIO_Ctrl_Trigger(ACTION_AUD_SetPlaybackVolume,&parm_vol,NULL,0);
			}
		}
		break;
		case CTL_STREAM_PANEL_VOICECALL:
		{
			BCM_AUDIO_DEBUG("VolumeCtrlPut pCurSel[1] = %d, pVolume[0] =%d, dev =%d\n", pCurSel[1],pVolume[0],dev);

			//call audio driver to set gain/volume
			if(pCurSel[1] == dev)
			{
				parm_vol.sink = pCurSel[1];
				parm_vol.volume1 = pVolume[0];
				AUDIO_Ctrl_Trigger(ACTION_AUD_SetTelephonySpkrVolume,&parm_vol,NULL,0);
			}
		}
		break;
		case CTL_STREAM_PANEL_PCMIN:
		case CTL_STREAM_PANEL_SPEECHIN:
		{
			if(pCurSel[0] == dev) //if current sink is diffent, dont call the driver to change the volume
			{
				if(pChip->streamCtl[stream-1].pSubStream != NULL)
					pStream = (struct snd_pcm_substream *)pChip->streamCtl[stream-1].pSubStream;
				else
					break;

				BCM_AUDIO_DEBUG("VolumeCtrlPut stream state = %d\n",pStream->runtime->status->state);
				if(pStream->runtime->status->state == SNDRV_PCM_STATE_RUNNING || pStream->runtime->status->state == SNDRV_PCM_STATE_PAUSED) // SNDDRV_PCM_STATE_PAUSED
				{
					//call audio driver to set volume
					BCM_AUDIO_DEBUG("VolumeCtrlPut caling AUDCTRL_SetRecordGain pVolume[0] =%d, pVolume[1]=%d\n", pVolume[0],pVolume[1]);
					parm_vol.source = pChip->streamCtl[stream-1].dev_prop.c.source;
					parm_vol.volume1 = pVolume[0];
					parm_vol.volume2 = pVolume[1];
					parm_vol.stream = (stream - 1);
					AUDIO_Ctrl_Trigger(ACTION_AUD_SetRecordGain,&parm_vol,NULL,0);
				}
			}
		}
		break;
		case CTL_STREAM_PANEL_VOIPIN:
			break;
		default:
			break;
	}


	return 0;
}


//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//Information of playback sink or capture source
//   Update the mixer control only if the stream is not running.Call audio driver to apply when stream is running
//
//-------------------------------------------------------------------------------------------
static int SelCtrlInfo(struct snd_kcontrol * kcontrol,	struct snd_ctl_elem_info * uinfo)
{
	brcm_alsa_chip_t*	pChip = (brcm_alsa_chip_t*)snd_kcontrol_chip(kcontrol);
	int priv = kcontrol->private_value;
	int	stream = STREAM_OF_CTL(priv);//kcontrol->id.device

	CAPH_ASSERT(stream>=CTL_STREAM_PANEL_FIRST && stream<CTL_STREAM_PANEL_LAST);
	stream--;
	if(pChip->streamCtl[stream].iFlags & MIXER_STREAM_FLAGS_CAPTURE)
	{
		uinfo->value.integer.min = AUDIO_SOURCE_ANALOG_MAIN;
		uinfo->value.integer.max = MIC_TOTAL_COUNT_FOR_USER;
	}
	else
	{
		uinfo->value.integer.min = AUDIO_SINK_HANDSET;
		uinfo->value.integer.max = AUDIO_SINK_TOTAL_COUNT;//last valid device is AUDIO_SINK_HEADPHONE
	}
	uinfo->value.integer.step = 1;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;

	return 0;
}


//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//Get playback sink or capture source
//
//-------------------------------------------------------------------------------------------
static int SelCtrlGet(	struct snd_kcontrol * kcontrol,	struct snd_ctl_elem_value * ucontrol)
{
	brcm_alsa_chip_t*	pChip = (brcm_alsa_chip_t*)snd_kcontrol_chip(kcontrol);
	int priv = kcontrol->private_value;
	int	stream = STREAM_OF_CTL(priv);
	s32	*pSel;
	CAPH_ASSERT(stream>=CTL_STREAM_PANEL_FIRST && stream<CTL_STREAM_PANEL_LAST);
	stream--;
	pSel = pChip->streamCtl[stream].iLineSelect;

	BCM_AUDIO_DEBUG("xnumid=%d xindex=%d", ucontrol->id.numid, ucontrol->id.index);

	//May need to get the value from driver
	ucontrol->value.integer.value[0] = pSel[0];
	ucontrol->value.integer.value[1] = pSel[1];

	return 0;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//Set playback sink or capture source
//   Update the mixer control only if the stream is not running.Call audio driver to apply when stream is running
//
//-------------------------------------------------------------------------------------------
static int SelCtrlPut(	struct snd_kcontrol * kcontrol,	struct snd_ctl_elem_value * ucontrol)
{
	brcm_alsa_chip_t*	pChip = (brcm_alsa_chip_t*)snd_kcontrol_chip(kcontrol);
	int priv = kcontrol->private_value;
	int	stream = STREAM_OF_CTL(priv);
	s32	*pSel, pCurSel[2];
	struct snd_pcm_substream *pStream=NULL;
	BRCM_AUDIO_Param_Spkr_t parm_spkr;
	BRCM_AUDIO_Param_Call_t parm_call;
	int i = 0,count=0;

	CAPH_ASSERT(stream>=CTL_STREAM_PANEL_FIRST && stream<CTL_STREAM_PANEL_LAST);
	pSel = pChip->streamCtl[stream-1].iLineSelect;

	pCurSel[0] = pSel[0]; //save current setting
	pCurSel[1] = pSel[1];

	pSel[0] = ucontrol->value.integer.value[0];
	pSel[1] = ucontrol->value.integer.value[1];

	if((stream != CTL_STREAM_PANEL_VOICECALL) && (pSel[0] == pSel[1]))
		pSel[1] = AUDIO_SINK_UNDEFINED;

	pSel[2] = AUDIO_SINK_UNDEFINED;
	if (isSTIHF == TRUE)
	{
		if (pSel[0] == AUDIO_SINK_LOUDSPK)
		{
			if (pSel[1] != AUDIO_SINK_UNDEFINED)
        		pSel[2] = pSel[1];
			pSel[1] = AUDIO_SINK_HANDSET;
		}
		else if (pSel[1] == AUDIO_SINK_LOUDSPK)
			pSel[2] = AUDIO_SINK_HANDSET;
	}

	BCM_AUDIO_DEBUG("SelCtrlPut stream =%d, pSel[0]=%d, pSel[1]=%d, pSel[2]=%d,\n", stream,pSel[0],pSel[1],pSel[2]);

	switch(stream)
	{
	case CTL_STREAM_PANEL_PCMOUT1: //pcmout 1
	case CTL_STREAM_PANEL_PCMOUT2: //pcmout 2
		{
			AUDIO_SINK_Enum_t curSpk = pCurSel[0];

			if(pChip->streamCtl[stream-1].pSubStream != NULL)
				pStream = (struct snd_pcm_substream *)pChip->streamCtl[stream-1].pSubStream;
			else
				break; //stream is not running, return

			BCM_AUDIO_DEBUG("SetCtrlput stream state = %d\n",pStream->runtime->status->state);

			if(pStream->runtime->status->state == SNDRV_PCM_STATE_RUNNING || pStream->runtime->status->state == SNDRV_PCM_STATE_PAUSED)
			{
				//call audio driver to set sink, or do switching if the current and new device are not same
				for (i = 0; i < MAX_PLAYBACK_DEV; i++)
				{
                    pChip->streamCtl[stream-1].dev_prop.p[i].source = AUDIO_SOURCE_MEM;

					if(pSel[i] != pCurSel[i])
					{
						if(pSel[i] >= AUDIO_SINK_HANDSET && pSel[i] < AUDIO_SINK_VALID_TOTAL)
           				{
			           		pChip->streamCtl[stream-1].dev_prop.p[i].sink = pSel[i];
           				}
						else
						{
							// No valid device in the list to do a playback,return error
							if(++count == MAX_PLAYBACK_DEV)
							{
								BCM_AUDIO_DEBUG("No device selected by the user ?\n");
								return -EINVAL;
							}
							else
								pChip->streamCtl[stream-1].dev_prop.p[i].sink = AUDIO_SINK_UNDEFINED;

						}

						// If stIHF remove EP path first.
						if ((isSTIHF) &&
							(pCurSel[i] == AUDIO_SINK_LOUDSPK) &&
							(pChip->streamCtl[stream-1].dev_prop.p[i+1].sink == AUDIO_SINK_HANDSET))
						{
							BCM_AUDIO_DEBUG("Stereo IHF, remove EP path first.\n");
							parm_spkr.src = pChip->streamCtl[stream-1].dev_prop.p[0].source;
							parm_spkr.sink = curSpk;
							parm_spkr.stream = (stream - 1);
							AUDIO_Ctrl_Trigger(ACTION_AUD_RemoveChannel,&parm_spkr,NULL,0);
							pChip->streamCtl[stream-1].dev_prop.p[i+1].sink = AUDIO_SINK_UNDEFINED;
						}
						if (i == 0)
						{
							// do the real switching now.
                            parm_spkr.src = pChip->streamCtl[stream-1].dev_prop.p[0].source;
							parm_spkr.sink = pChip->streamCtl[stream-1].dev_prop.p[0].sink;
							parm_spkr.stream = (stream - 1);
							AUDIO_Ctrl_Trigger(ACTION_AUD_SwitchSpkr,&parm_spkr,NULL,0);
						}
						else
						{
							if(pChip->streamCtl[stream-1].dev_prop.p[i].sink != AUDIO_SINK_UNDEFINED)
							{
                                parm_spkr.src = pChip->streamCtl[stream-1].dev_prop.p[0].source;
								parm_spkr.sink = pChip->streamCtl[stream-1].dev_prop.p[i].sink;
								parm_spkr.stream = (stream - 1);
								AUDIO_Ctrl_Trigger(ACTION_AUD_AddChannel,&parm_spkr,NULL,0);
							}
						}
					}
				}
			}
		}
		break;

	case CTL_STREAM_PANEL_VOICECALL://voice call

		parm_call.cur_mic = pCurSel[0];
		parm_call.cur_spkr = pCurSel[1];
		parm_call.new_mic = pSel[0];
		parm_call.new_spkr = pSel[1];
        AUDIO_Ctrl_Trigger( ACTION_AUD_SetTelephonyMicSpkr,&parm_call,NULL,0);
		break;

    case CTL_STREAM_PANEL_FM:      // FM
       {
		AUDIO_SINK_Enum_t curSpk = pCurSel[0];
        pChip->streamCtl[stream-1].dev_prop.p[0].source = AUDIO_SOURCE_I2S;

        if((pChip->iEnableFM) && (!(pChip->iEnablePhoneCall)) && (curSpk != pSel[0]))
        {
            // change the sink/spk
            if(pSel[0] >= AUDIO_SINK_HANDSET && pSel[0] < AUDIO_SINK_VALID_TOTAL)
        	{
		   		pChip->streamCtl[stream-1].dev_prop.p[0].sink = pSel[0];
        	}
            else
            {
				BCM_AUDIO_DEBUG("No device selected by the user ?\n");
				return -EINVAL;
			}

            parm_spkr.src = pChip->streamCtl[stream-1].dev_prop.p[0].source;
			parm_spkr.sink = pChip->streamCtl[stream-1].dev_prop.p[0].sink;
			parm_spkr.stream = (stream -1);
			AUDIO_Ctrl_Trigger(ACTION_AUD_SwitchSpkr,&parm_spkr,NULL,0);
		 }
        }
        break;

	default:
			break;
	}

	return 0;
}


//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//Get MUTE status of the deivce
//
//
//-------------------------------------------------------------------------------------------
static int SwitchCtrlGet(	struct snd_kcontrol * kcontrol,	struct snd_ctl_elem_value * ucontrol)
{
	brcm_alsa_chip_t*	pChip = (brcm_alsa_chip_t*)snd_kcontrol_chip(kcontrol);
	int priv = kcontrol->private_value;
	int	stream = STREAM_OF_CTL(priv);
	int	dev = DEV_OF_CTL(priv);
	s32	*pMute;
	CAPH_ASSERT(stream>=CTL_STREAM_PANEL_FIRST && stream<CTL_STREAM_PANEL_LAST);
	stream--;
	pMute = pChip->streamCtl[stream].ctlLine[dev].iMute;

	ucontrol->value.integer.value[0] = pMute[0];
	ucontrol->value.integer.value[1] = pMute[1];
	return 0;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//Set deivce MUTE
//   Update the mixer control only if the stream is not running.Call audio driver to apply when stream is running
//
//-------------------------------------------------------------------------------------------
static int SwitchCtrlPut(	struct snd_kcontrol * kcontrol,	struct snd_ctl_elem_value * ucontrol)
{
	brcm_alsa_chip_t*	pChip = (brcm_alsa_chip_t*)snd_kcontrol_chip(kcontrol);
	int priv = kcontrol->private_value;
	int	stream = STREAM_OF_CTL(priv);
	int	dev = DEV_OF_CTL(priv);
	s32	*pMute;
	struct snd_pcm_substream *pStream=NULL;
	BRCM_AUDIO_Param_Mute_t parm_mute;

	CAPH_ASSERT(stream>=CTL_STREAM_PANEL_FIRST && stream<CTL_STREAM_PANEL_LAST);
	pMute = pChip->streamCtl[stream-1].ctlLine[dev].iMute;

	pMute[0] = ucontrol->value.integer.value[0];
	pMute[1] = ucontrol->value.integer.value[1];

	//Apply mute is the stream is running
	switch(stream)
	{
		case CTL_STREAM_PANEL_PCMOUT1:
		case CTL_STREAM_PANEL_PCMOUT2:
		case CTL_STREAM_PANEL_VOIPOUT:
		{
			if(pChip->streamCtl[stream-1].pSubStream != NULL)
				pStream = (struct snd_pcm_substream *)pChip->streamCtl[stream-1].pSubStream;
			else
				break;

			BCM_AUDIO_DEBUG("SwitchCtrlPut stream state = %d\n",pStream->runtime->status->state);
			BCM_AUDIO_DEBUG("SwitchCtrlPut sink = %d, pMute[0] = %d\n",pChip->streamCtl[stream-1].dev_prop.p[0].sink,pMute[0]);

			if(pStream->runtime->status->state == SNDRV_PCM_STATE_RUNNING || pStream->runtime->status->state == SNDRV_PCM_STATE_PAUSED) // SNDDRV_PCM_STATE_PAUSED
			{
				//call audio driver to set mute
				parm_mute.source = pChip->streamCtl[stream-1].dev_prop.p[0].source;
				parm_mute.sink = pChip->streamCtl[stream-1].dev_prop.p[0].sink;
				parm_mute.mute1 = pMute[0];
				parm_mute.stream = (stream - 1);
				AUDIO_Ctrl_Trigger(ACTION_AUD_MutePlayback,&parm_mute,NULL,0);
			}
		}
			break;
		case CTL_STREAM_PANEL_FM:
		{
				//call audio driver to set mute
				parm_mute.source = pChip->streamCtl[stream-1].dev_prop.p[0].source;
				parm_mute.sink = pChip->streamCtl[stream-1].dev_prop.p[0].sink;
				parm_mute.mute1 = pMute[0];
				parm_mute.stream = (stream - 1);
				AUDIO_Ctrl_Trigger(ACTION_AUD_MutePlayback,&parm_mute,NULL,0);
		}
		break;

		case CTL_STREAM_PANEL_PCMIN:
		{
			if(pChip->streamCtl[stream-1].pSubStream != NULL)
				pStream = (struct snd_pcm_substream *)pChip->streamCtl[stream-1].pSubStream;
			else
				break;

			BCM_AUDIO_DEBUG("SwitchCtrlPut stream state = %d\n",pStream->runtime->status->state);

			if(pStream->runtime->status->state == SNDRV_PCM_STATE_RUNNING || pStream->runtime->status->state == SNDRV_PCM_STATE_PAUSED) // SNDDRV_PCM_STATE_PAUSED
			{
				//call audio driver to set mute
				parm_mute.source = pChip->streamCtl[stream-1].dev_prop.c.source;
				parm_mute.mute1 = pMute[0];
				parm_mute.stream = (stream - 1);
				AUDIO_Ctrl_Trigger(ACTION_AUD_MuteRecord,&parm_mute,NULL,0);
			}
		}
		break;
		case CTL_STREAM_PANEL_VOIPIN:
			break;
		default:
			break;
	}

	return 0;
}


//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//MISC control information
//
//-------------------------------------------------------------------------------------------
static int MiscCtrlInfo(struct snd_kcontrol * kcontrol,	struct snd_ctl_elem_info * uinfo)
{
	int priv = kcontrol->private_value;
	int function = FUNC_OF_CTL(priv);
	int	stream = STREAM_OF_CTL(priv);

	uinfo->value.integer.step = 1;
	switch(function)
	{
		case CTL_FUNCTION_LOOPBACK_TEST:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
			uinfo->count = 3;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = CAPH_MAX_CTRL_LINES;//FIXME
			break;
		case CTL_FUNCTION_PHONE_ENABLE:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
			uinfo->count = 1;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 1;
			break;
		case CTL_FUNCTION_PHONE_CALL_MIC_MUTE:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
			uinfo->count = 2;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 1;
			break;
		case CTL_FUNCTION_SPEECH_MIXING_OPTION:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
			uinfo->count = 1;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 3;
			break;
		case CTL_FUNCTION_FM_ENABLE:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
			uinfo->count = 1;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 1;
			break;
		case CTL_FUNCTION_FM_FORMAT:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
			uinfo->count = 2; //sample rate, stereo/mono
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 48000;
			break;
		case CTL_FUNCTION_AT_AUDIO:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
			uinfo->count = 7;
			uinfo->value.integer.min = 0x80000000;
			uinfo->value.integer.max = 0x7FFFFFFF;
			if(kcontrol->id.index==1) //val[0] is at command handler, val[1] is 1st parameter of the AT command parameters
			{
				uinfo->count = 1;
				uinfo->value.integer.min = 0x0;
				uinfo->value.integer.max = 0x7FFFFFFF; //Each bit indicates Log ID. Max of 32 Log IDs can be supported
			}
			break;
		case CTL_FUNCTION_BYPASS_VIBRA:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
			uinfo->count = 3;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 100;
			break;
		case CTL_FUNCTION_BT_TEST:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
			uinfo->count = 1;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 1;
			break;
		case CTL_FUNCTION_CFG_IHF:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
			uinfo->count = 2;	//integer[0] -- 1 for mono, 2 for stereo; integer[1] -- data mixing option if channel is mono,  1 for left, 2 for right, 3 for (L+R)/2
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 3;
			break;
		case CTL_FUNCTION_CFG_SSP:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
			uinfo->count = 1;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 1;
			break;
		case CTL_FUNCTION_VOL:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
			uinfo->count = 2;
			if(CTL_STREAM_PANEL_VOICECALL==stream)
				uinfo->count = 1;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = 19; //volume level
			break;
		case CTL_FUNCTION_SINK_CHG:
			uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
			uinfo->count = 2;
			uinfo->value.integer.min = 0;
			uinfo->value.integer.max = AUDIO_SINK_TOTAL_COUNT;
			break;

		default:
			BCM_AUDIO_DEBUG("Unexpected function code %d\n", function);
				break;
	}

	return 0;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//Get MISC control value
//
//-------------------------------------------------------------------------------------------
static int MiscCtrlGet(	struct snd_kcontrol * kcontrol,	struct snd_ctl_elem_value * ucontrol)
{
	brcm_alsa_chip_t*	pChip = (brcm_alsa_chip_t*)snd_kcontrol_chip(kcontrol);
	int priv = kcontrol->private_value;
	int function = FUNC_OF_CTL(priv);
	int	stream = STREAM_OF_CTL(priv);
    int rtn = 0;

	switch(function)
	{
		case CTL_FUNCTION_LOOPBACK_TEST:
			ucontrol->value.integer.value[0] = pChip->pi32LoopBackTestParam[0];
			ucontrol->value.integer.value[1] = pChip->pi32LoopBackTestParam[1];
			ucontrol->value.integer.value[2] = pChip->pi32LoopBackTestParam[2];
			break;
		case CTL_FUNCTION_PHONE_ENABLE:
			ucontrol->value.integer.value[0] = pChip->iEnablePhoneCall;
			break;
		case CTL_FUNCTION_PHONE_CALL_MIC_MUTE:
			ucontrol->value.integer.value[0] = pChip->iMutePhoneCall[0];
			ucontrol->value.integer.value[1] = pChip->iMutePhoneCall[1];
			break;
		case CTL_FUNCTION_SPEECH_MIXING_OPTION:
				ucontrol->value.integer.value[0] = pChip->pi32SpeechMixOption[stream-1];
			break;
		case CTL_FUNCTION_FM_ENABLE:
            BCM_AUDIO_DEBUG("CTL_FUNCTION_FM_ENABLE, status=%d\n", pChip->iEnableFM);
            ucontrol->value.integer.value[0] = pChip->iEnableFM;
			break;
		case CTL_FUNCTION_FM_FORMAT:
			break;
		case CTL_FUNCTION_AT_AUDIO:
		{
			struct snd_ctl_elem_info info;
			kcontrol->info(kcontrol, &info);
			rtn = AtAudCtlHandler_get(kcontrol->id.index, pChip, info.count, ucontrol->value.integer.value);
			BCM_AUDIO_DEBUG("%s values [%ld %ld %ld %ld %ld %ld %ld]", __FUNCTION__, ucontrol->value.integer.value[0],
				ucontrol->value.integer.value[1],ucontrol->value.integer.value[2], ucontrol->value.integer.value[3], ucontrol->value.integer.value[4],
				ucontrol->value.integer.value[5],ucontrol->value.integer.value[6]);
		}
			break;
		case CTL_FUNCTION_BYPASS_VIBRA:
			ucontrol->value.integer.value[0] = pChip->pi32BypassVibraParam[0];
			ucontrol->value.integer.value[1] = pChip->pi32BypassVibraParam[1];
			ucontrol->value.integer.value[2] = pChip->pi32BypassVibraParam[2];
			break;
		case CTL_FUNCTION_BT_TEST:
			ucontrol->value.integer.value[0] = pChip->iEnableBTTest;
			break;
		case CTL_FUNCTION_CFG_IHF:
			ucontrol->value.integer.value[0] = pChip->pi32CfgIHF[0];
			ucontrol->value.integer.value[1] = pChip->pi32CfgIHF[1];
		case CTL_FUNCTION_CFG_SSP:
			ucontrol->value.integer.value[0] = pChip->i32CfgSSP[kcontrol->id.index];
			break;
		case CTL_FUNCTION_VOL:
			memcpy(ucontrol->value.integer.value, pChip->pi32LevelVolume[stream-1], CAPH_MAX_PCM_STREAMS*sizeof(s32));
			break;

		default:
			BCM_AUDIO_DEBUG("Unexpected function code %d\n", function);
			break;

	}

	return rtn;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//Set MISC control
//
//-------------------------------------------------------------------------------------------
static int MiscCtrlPut(	struct snd_kcontrol * kcontrol,	struct snd_ctl_elem_value * ucontrol)
{
	brcm_alsa_chip_t*	pChip = (brcm_alsa_chip_t*)snd_kcontrol_chip(kcontrol);
	int priv = kcontrol->private_value;
	int function = priv&0xFF;
	s32	*pSel, callMode;
	int	stream = STREAM_OF_CTL(priv);
	BRCM_AUDIO_Param_Call_t parm_call;
	BRCM_AUDIO_Param_Loopback_t parm_loop;
	BRCM_AUDIO_Param_Mute_t	parm_mute;
	BRCM_AUDIO_Param_Vibra_t parm_vibra;
	BRCM_AUDIO_Param_FM_t parm_FM;
	BRCM_AUDIO_Param_Spkr_t parm_spkr;
    int rtn = 0,cmd,i,indexVal = -1,cnt=0;
	struct snd_pcm_substream *pStream=NULL;
	Int32 sink = 0;

	switch(function)
	{
		case CTL_FUNCTION_LOOPBACK_TEST:
			parm_loop.parm = pChip->pi32LoopBackTestParam[0] = ucontrol->value.integer.value[0];
			parm_loop.mic = pChip->pi32LoopBackTestParam[1] = ucontrol->value.integer.value[1];
			parm_loop.spkr = pChip->pi32LoopBackTestParam[2] = ucontrol->value.integer.value[2];

			//Do loopback test
			AUDIO_Ctrl_Trigger(ACTION_AUD_SetHWLoopback,&parm_loop,NULL,0);
			break;
		case CTL_FUNCTION_PHONE_ENABLE:
			pChip->iEnablePhoneCall = ucontrol->value.integer.value[0];
			pSel = pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL-1].iLineSelect;

			BCM_AUDIO_DEBUG("MiscCtrlPut CTL_FUNCTION_PHONE_ENABLE pSel[0] = %d-%d, EnablePhoneCall %d \n", pSel[0],pSel[1], pChip->iEnablePhoneCall );
			parm_call.new_mic = parm_call.cur_mic = pSel[0];
			parm_call.new_spkr = parm_call.new_spkr = pSel[1];

			if(!pChip->iEnablePhoneCall)
				//disable voice call
				AUDIO_Ctrl_Trigger(ACTION_AUD_DisableTelephony,&parm_call,NULL,0);
			else
			{
				//enable voice call with sink and source
				AUDIO_Ctrl_Trigger(ACTION_AUD_EnableTelephony,&parm_call,NULL,0);
			}

			break;
		case CTL_FUNCTION_PHONE_CALL_MIC_MUTE:
			if(pChip->iMutePhoneCall[0] != ucontrol->value.integer.value[0])
			{
				pChip->iMutePhoneCall[0] = ucontrol->value.integer.value[0];
				//pChip->iMutePhoneCall[1] = ucontrol->value.integer.value[1];

				if(pChip->iEnablePhoneCall)//only in call
				{
					pSel = pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL-1].iLineSelect;

					BCM_AUDIO_DEBUG("MiscCtrlPut pSel[0] = %d pMute[0] =%d pMute[1] =%d\n", pSel[0], pChip->iMutePhoneCall[0], pChip->iMutePhoneCall[1]);

					//call audio driver to mute
					parm_mute.source =  pSel[0];
					parm_mute.mute1 = pChip->iMutePhoneCall[0];
					AUDIO_Ctrl_Trigger(ACTION_AUD_MuteTelephony,&parm_mute,NULL,0);
				}
			}
			break;
		case CTL_FUNCTION_SPEECH_MIXING_OPTION:
			pChip->pi32SpeechMixOption[stream-1] = ucontrol->value.integer.value[0];
			BCM_AUDIO_DEBUG("MiscCtrlPut CTL_FUNCTION_SPEECH_MIXING_OPTION stream = %d, option = %d\n", stream, pChip->pi32SpeechMixOption[stream-1]);
			break;
		case CTL_FUNCTION_FM_ENABLE:
            callMode = pChip->iEnablePhoneCall;
			pChip->iEnableFM = ucontrol->value.integer.value[0];
            pChip->streamCtl[stream-1].dev_prop.p[0].source = AUDIO_SOURCE_I2S;
			pSel = pChip->streamCtl[stream-1].iLineSelect;
			BCM_AUDIO_DEBUG("MiscCtrlPut CTL_FUNCTION_FM_ENABLE stream = %d, status = %d, pSel[0] = %d-%d \n", stream, pChip->iEnableFM,pSel[0],pSel[1]);

			if(!pChip->iEnableFM)  // disable FM
            {
                //disable the playback path
                parm_FM.source = pChip->streamCtl[stream-1].dev_prop.p[0].source;
				parm_FM.sink = pChip->streamCtl[stream-1].dev_prop.p[0].sink;
				parm_FM.stream = (stream - 1);
				AUDIO_Ctrl_Trigger(ACTION_AUD_DisableFMPlay,&parm_FM,NULL,0);
			}
			else  // enable FM
			{
                //route the playback to CAPH
                pChip->streamCtl[stream-1].dev_prop.p[0].drv_type = AUDIO_DRIVER_PLAY_AUDIO;

                if (callMode)
                {
					pChip->streamCtl[stream-1].dev_prop.p[0].sink = AUDIO_SINK_DSP;
                }
                else
                {
					if(pSel[0] >= AUDIO_SINK_HANDSET && pSel[0] < AUDIO_SINK_VALID_TOTAL)
        			{
				   		pChip->streamCtl[stream-1].dev_prop.p[0].sink = pSel[0];
        			}
		            else
        		    {
						BCM_AUDIO_DEBUG("No device selected by the user ?\n");
						return -EINVAL;
					}
                }

                if (callMode)
                {
                    parm_FM.fm_mix = (UInt32)pChip->pi32SpeechMixOption[stream-1];
                    AUDIO_Ctrl_Trigger(ACTION_AUD_SetARM2SPInst,&parm_FM,NULL,0);
                }
                // Enable the playback the path
                 parm_FM.source = pChip->streamCtl[stream-1].dev_prop.p[0].source;
				 parm_FM.sink  = pChip->streamCtl[stream-1].dev_prop.p[0].sink;
				 parm_FM.volume1 = pChip->streamCtl[stream-1].ctlLine[pSel[0]].iVolume[0];
				 parm_FM.volume2 = pChip->streamCtl[stream-1].ctlLine[pSel[0]].iVolume[1];
				 parm_FM.stream = (stream - 1);
                 AUDIO_Ctrl_Trigger(ACTION_AUD_EnableFMPlay,&parm_FM,NULL,0);
			}
			break;
		case CTL_FUNCTION_FM_FORMAT:
			break;
		case CTL_FUNCTION_AT_AUDIO:
		{
			struct snd_ctl_elem_info info;
			kcontrol->info(kcontrol, &info);
			rtn = AtAudCtlHandler_put(kcontrol->id.index, pChip, info.count, ucontrol->value.integer.value);
			break;
		}
		case CTL_FUNCTION_BYPASS_VIBRA:
			pChip->pi32BypassVibraParam[0] = ucontrol->value.integer.value[0];
			parm_vibra.strength = pChip->pi32BypassVibraParam[1] = ucontrol->value.integer.value[1];
			parm_vibra.direction = pChip->pi32BypassVibraParam[2] = ucontrol->value.integer.value[2];

			if (pChip->pi32BypassVibraParam[0] == 1) // Enable
			{
				AUDIO_Ctrl_Trigger(ACTION_AUD_EnableByPassVibra,NULL,NULL,0);
				AUDIO_Ctrl_Trigger(ACTION_AUD_SetVibraStrength,&parm_vibra,NULL,0);
			}
			else
				AUDIO_Ctrl_Trigger(ACTION_AUD_DisableByPassVibra,NULL,NULL,0);
			BCM_AUDIO_DEBUG("MiscCtrlPut BypassVibra enable %d, strength %d, direction %d.\n", pChip->pi32BypassVibraParam[0], pChip->pi32BypassVibraParam[1], pChip->pi32BypassVibraParam[2]);
			break;
		case CTL_FUNCTION_BT_TEST:
			pChip->iEnableBTTest = ucontrol->value.integer.value[0];
			AUDCTRL_SetBTMode(pChip->iEnableBTTest);
			break;
		case CTL_FUNCTION_CFG_IHF:
			pChip->pi32CfgIHF[0] = ucontrol->value.integer.value[0];
			pChip->pi32CfgIHF[1] = ucontrol->value.integer.value[1];
			if (ucontrol->value.integer.value[0] == 1) // Mono IHF
			{
				isSTIHF = FALSE;
				AUDCTRL_SetIHFmode(isSTIHF);
			}
			else if(ucontrol->value.integer.value[0] == 2) //stereo IHF
			{
				isSTIHF = TRUE;
				AUDCTRL_SetIHFmode(isSTIHF);
			}
			else
			{
				BCM_AUDIO_DEBUG("%s, Invalid value for setting IHF mode: %ld, 1-mono, 2-stereo.", __FUNCTION__, ucontrol->value.integer.value[0]);
			}
			break;
		case CTL_FUNCTION_CFG_SSP:
			pChip->i32CfgSSP[kcontrol->id.index] = ucontrol->value.integer.value[0];
			//Port is 1 base
			AUDCTRL_ConfigSSP(kcontrol->id.index+1, pChip->i32CfgSSP[kcontrol->id.index]);
			break;
		case CTL_FUNCTION_VOL:
			memcpy(pChip->pi32LevelVolume[stream-1], ucontrol->value.integer.value, CAPH_MAX_PCM_STREAMS*sizeof(s32));
			break;
			
		case CTL_FUNCTION_SINK_CHG:
			BCM_AUDIO_DEBUG("Change sink device stream=%d cmd=%ld sink=%ld\n", stream, ucontrol->value.integer.value[0], ucontrol->value.integer.value[1]);
			cmd = ucontrol->value.integer.value[0];
			pSel = pChip->streamCtl[stream-1].iLineSelect;
			if(cmd == 0)//add device
			{
				for(i=0;i<MAX_PLAYBACK_DEV;i++)
				{
					if(pSel[i] == AUDIO_SINK_UNDEFINED && indexVal == -1)
					{
						indexVal = i;
						continue;
					}
					else if(pSel[i] == ucontrol->value.integer.value[1])
					{
						indexVal = -1;
						BCM_AUDIO_DEBUG("Device already added in the list \n");
						break;
					}
					else if(++cnt == MAX_PLAYBACK_DEV)
					{
						BCM_AUDIO_DEBUG("Max devices count reached. Cannot add more device \n");
						return -1;
					}
				}
				if(indexVal != -1)
				{
					pSel[indexVal] = ucontrol->value.integer.value[1];

					if(pChip->streamCtl[stream-1].pSubStream != NULL)
					{
						pStream = (struct snd_pcm_substream *)pChip->streamCtl[stream-1].pSubStream;
						//if the stream is running, then call the audio driver API to add the device
						if(pStream->runtime->status->state == SNDRV_PCM_STATE_RUNNING || pStream->runtime->status->state == SNDRV_PCM_STATE_PAUSED)
						{
							if(pSel[indexVal] >= AUDIO_SINK_HANDSET && pSel[indexVal] < AUDIO_SINK_VALID_TOTAL)
							{
								parm_spkr.src = AUDIO_SINK_MEM;
								parm_spkr.sink = pSel[indexVal];
								parm_spkr.stream = (stream - 1);
								AUDIO_Ctrl_Trigger(ACTION_AUD_AddChannel,&parm_spkr,NULL,0);
							}
						}
			        }
				}
			}
			else if(cmd == 1) //remove device
			{
				for(i = 0; i < MAX_PLAYBACK_DEV; i++)
				{
					if(pSel[i] == ucontrol->value.integer.value[1] && indexVal == -1)
					{
						indexVal = i;
						sink = pSel[indexVal]; //sink to remove
						if(i != 0)
							break;
					}
					else if(indexVal != -1)
					{
						if(pSel[i] != AUDIO_SINK_UNDEFINED)
						{
							pSel[indexVal] = pSel[i];
							indexVal = i;
							break;
						}
					}
				}
				if(indexVal != -1)
				{
					if(pChip->streamCtl[stream-1].pSubStream != NULL)
					{
						pStream = (struct snd_pcm_substream *)pChip->streamCtl[stream-1].pSubStream;
						//if the stream is running, then call the audio driver API to remove the device
						if(pStream->runtime->status->state == SNDRV_PCM_STATE_RUNNING || pStream->runtime->status->state == SNDRV_PCM_STATE_PAUSED)
						{
							parm_spkr.src = AUDIO_SINK_MEM;
							parm_spkr.sink = sink;
							parm_spkr.stream = (stream - 1);
							AUDIO_Ctrl_Trigger(ACTION_AUD_RemoveChannel,&parm_spkr,NULL,0);
						}
					}
					pSel[indexVal] = AUDIO_SINK_UNDEFINED;
				}
			}
			break;
		default:
			BCM_AUDIO_DEBUG("Unexpected function code %d\n", function);
			break;
	}


	return rtn;
}






//The DECLARE_TLV_DB_SCALE macro defines information about a mixer control where each step in the control's value changes the dB value by a constant dB amount.
//The first parameter is the name of the variable to be defined. The second parameter is the minimum value, in units of 0.01 dB. The third parameter is the step size,
//in units of 0.01 dB. Set the fourth parameter to 1 if the minimum value actually mutes the control.
//Control value is in mB, minimium -50db, step 0.01db, minimium does not mean MUTE
static const DECLARE_TLV_DB_SCALE(caph_db_scale_volume, -5000, 1, 0);


#define BRCM_MIXER_CTRL_GENERAL(nIface, iDevice, iSubdev, sName, iIndex, iAccess, iCount, fInfo, fGet, fPut, pTlv, lPriv_val) \
	{	\
	   .iface = nIface, \
	   .device = iDevice, \
	   .subdevice = iSubdev, \
	   .name = sName, \
	   .index = iIndex, \
	   .access= iAccess,\
	   .count = iCount, \
	   .info = fInfo, \
	   .get = fGet,	\
	   .put = fPut, \
	   .tlv = { .p = pTlv }, \
	   .private_value = lPriv_val, \
	}



#define BRCM_MIXER_CTRL_VOLUME(dev, subdev, xname, xindex, private_val) \
	BRCM_MIXER_CTRL_GENERAL(SNDRV_CTL_ELEM_IFACE_MIXER, dev, subdev, xname, xindex, SNDRV_CTL_ELEM_ACCESS_READWRITE, 0, \
	                        VolumeCtrlInfo, VolumeCtrlGet, VolumeCtrlPut, caph_db_scale_volume,private_val)

#define BRCM_MIXER_CTRL_SWITCH(dev, subdev, xname, xindex, private_val) \
	BRCM_MIXER_CTRL_GENERAL(SNDRV_CTL_ELEM_IFACE_MIXER, dev, subdev, xname, xindex, SNDRV_CTL_ELEM_ACCESS_READWRITE, 0, \
	                        snd_ctl_boolean_stereo_info, SwitchCtrlGet, SwitchCtrlPut, 0,private_val)

#define BRCM_MIXER_CTRL_SELECTION(dev, subdev, xname, xindex, private_val) \
	BRCM_MIXER_CTRL_GENERAL(SNDRV_CTL_ELEM_IFACE_MIXER, dev, subdev, xname, xindex, SNDRV_CTL_ELEM_ACCESS_READWRITE, 0, \
	                        SelCtrlInfo, SelCtrlGet, SelCtrlPut, 0,private_val)


#define BRCM_MIXER_CTRL_MISC(dev, subdev, xname, xindex, private_val) \
	BRCM_MIXER_CTRL_GENERAL(SNDRV_CTL_ELEM_IFACE_MIXER, dev, subdev, xname, xindex, SNDRV_CTL_ELEM_ACCESS_READWRITE, 0, \
	                        MiscCtrlInfo, MiscCtrlGet, MiscCtrlPut, 0,private_val)

#define BRCM_MIXER_CTRL_MISC_W(dev, subdev, xname, xindex, private_val) \
	BRCM_MIXER_CTRL_GENERAL(SNDRV_CTL_ELEM_IFACE_MIXER, dev, subdev, xname, xindex, SNDRV_CTL_ELEM_ACCESS_WRITE, 0, \
	                        MiscCtrlInfo, MiscCtrlGet, MiscCtrlPut, 0,private_val)

/*++++++++++++++++++++++++++++++++ Sink device and source devices
{.strName = "Handset",	.iVolume = {0,0},},		//AUDIO_SINK_HANDSET
{.strName = "Headset",		.iVolume = {-400,-400},},	//AUDIO_SINK_HEADSET
x{.strName = "Handsfree",	.iVolume = {-10,-10},},	//AUDIO_SINK_HANDSFREE
{.strName = "BT SCO",	.iVolume = {-10,-10},},		//AUDIO_SINK_BTM
{.strName = "Loud Speaker", .iVolume = {400,400},},	//AUDIO_SINK_LOUDSPK
{.strName = "", .iVolume = {-400,-400},}, 			//AUDIO_SINK_TTY
{.strName = "", .iVolume = {0,0},}, 				//AUDIO_SINK_HAC
x{.strName = "", .iVolume = {0,0},}, 				//AUDIO_SINK_USB
x{.strName = "", .iVolume = {0,0},}, 				//AUDIO_SINK_BTS
{.strName = "I2S", .iVolume = {0,0},},			//AUDIO_SINK_I2S
{.strName = "Speaker Vibra", .iVolume = {-10,-10},},	//AUDIO_SINK_VIBRA


{.strName = "", .iVolume = {0,0},}, 				//AUDIO_SOURCE_UNDEFINED
{.strName = "Main Mic", 	.iVolume = {28,28},},		//AUDIO_SOURCE_ANALOG_MAIN
{.strName = "AUX Mic",	.iVolume = {28,28},},			//AUDIO_SOURCE_AUX
{.strName = "Digital MIC 1",	.iVolume = {28,28},},	//AUDIO_SOURCE_DIGI1
{.strName = "Digital MIC 2",	.iVolume = {28,28},},	//AUDIO_SOURCE_DIGI2
x{.strName = "Digital Mic 12",	.iVolume = {28,28},},	//AUDCTRL_DUAL_MIC_DIGI12
x{.strName = "Digital Mic 21",	.iVolume = {28,28},},	//AUDCTRL_DUAL_MIC_DIGI21
x{.strName = "MIC_ANALOG_DIGI1", .iVolume = {12,12},},	//AUDCTRL_DUAL_MIC_ANALOG_DIGI1
x{.strName = "MIC_DIGI1_ANALOG", .iVolume = {0,0},}, 	//AUDCTRL_DUAL_MIC_DIGI1_ANALOG
{.strName = "BT SCO Mic",		.iVolume = {30,30},},	//AUDIO_SOURCE_BTM
x{.strName = "", .iVolume = {0,0},}, 					//AUDIO_SOURCE_USB
{.strName = "I2S",	.iVolume = {12,12},},				//AUDIO_SOURCE_I2S
x{.strName = "MIC_DIGI3",	.iVolume = {28,28},},		//AUDIO_SOURCE_DIGI3
x{.strName = "MIC_DIGI4",	.iVolume = {28,28},},		//AUDIO_SOURCE_DIGI4
x{.strName = "MIC_SPEECH_DIGI",	.iVolume = {30,30},},	//AUDIO_SOURCE_SPEECH_DIGI
x{.strName = "MIC_EANC_DIGI",	.iVolume = {30,30},},	//AUDIO_SOURCE_EANC_DIGI

--------------------------------------------------*/

//must match AUDIO_SINK_Enum_t
#define	BCM_CTL_SINK_LINES	{\
/*AUDIO_SINK_HANDSET*/		{.strName = "HNT",	.iVolume = {0,0},},	\
/*AUDIO_SINK_HEADSET*/		{.strName = "HST",	.iVolume = {-400,-400},},	\
/*AUDIO_SINK_HANDSFREE*/	{.strName = "HNF",	.iVolume = {0,0},},	\
/*AUDIO_SINK_BTM*/			{.strName = "BTM",	.iVolume = {0,0},},	\
/*AUDIO_SINK_LOUDSPK*/		{.strName = "SPK",	.iVolume = {400,400},},	\
/*AUDIO_SINK_TTY*/			{.strName = "TTY",	.iVolume = {-400,-400},},	\
/*AUDIO_SINK_HAC*/			{.strName = "HAC",	.iVolume = {0,0},},	\
/*AUDIO_SINK_USB*/			{.strName = "",	.iVolume = {0,0},},	\
/*AUDIO_SINK_BTS*/			{.strName = "", .iVolume = {0,0},},	\
/*AUDIO_SINK_I2S*/			{.strName = "I2S", .iVolume = {0,0},},	\
/*AUDIO_SINK_VIBRA*/		{.strName = "VIB", .iVolume = {0,0},},	\
/*AUDIO_SINK_HEADPHONE*/	{.strName = "", .iVolume = {0,0},},	\
					}

//must match AUDIO_SOURCE_Enum_t
#define	BCM_CTL_SRC_LINES	{ \
/*AUDIO_SOURCE_UNDEFINED*/		{.strName = "", .iVolume = {0,0},}, 		\
/*AUDIO_SOURCE_ANALOG_MAIN*/	{.strName = "MIC", 	.iVolume = {3000,3000},},	\
/*AUDIO_SOURCE_ANALOG_AUX*/		{.strName = "AUX",	.iVolume = {3000,3000},},	\
/*AUDIO_SOURCE_DIGI1*/			{.strName = "DG1",	.iVolume = {700,700},},	\
/*AUDIO_SOURCE_DIGI2*/			{.strName = "DG2",	.iVolume = {700,700},},	\
/*AUDIO_SOURCE_DIGI3*/			{.strName = "",	.iVolume = {0,0},},	\
/*AUDIO_SOURCE_DIGI4*/			{.strName = "",	.iVolume = {0,0},},	\
/*AUDIO_SOURCE_MIC_ARRAY1*/		{.strName = "", .iVolume = {0,0},}, 		\
/*AUDIO_SOURCE_MIC_ARRAY2*/		{.strName = "", .iVolume = {0,0},}, 		\
/*AUDIO_SOURCE_BTM*/			{.strName = "BTM",		.iVolume = {700,700},},\
/*AUDIO_SOURCE_USB*/			{.strName = "", .iVolume = {0,0},}, 		\
/*AUDIO_SOURCE_I2S*/			{.strName = "I2S",	.iVolume = {300,300},},	\
/*AUDIO_SOURCE_RESERVED1*/		{.strName = "", .iVolume = {0,0},}, 		\
/*AUDIO_SOURCE_RESERVED2*/		{.strName = "", .iVolume = {0,0},}, 		\
					}

//
//Initial data of controls, runtime data is in 'chip' data structure
static	TPcm_Stream_Ctrls	sgCaphStreamCtls[CAPH_MAX_PCM_STREAMS] __initdata =
	{
		//PCMOut1
		{
			.iTotalCtlLines = AUDIO_SINK_TOTAL_COUNT,
			.iLineSelect = {AUDIO_SINK_HANDSET, AUDIO_SINK_UNDEFINED, AUDIO_SINK_UNDEFINED},
			.strStreamName = "P1",
			.ctlLine = BCM_CTL_SINK_LINES,
		},

		//PCMOut2
		{
			.iTotalCtlLines = AUDIO_SINK_TOTAL_COUNT,
			.iLineSelect = {AUDIO_SINK_LOUDSPK, AUDIO_SINK_UNDEFINED, AUDIO_SINK_UNDEFINED},
			.strStreamName = "P2",
			.ctlLine = BCM_CTL_SINK_LINES,
		},

		//VOIP Out
		{
			.iTotalCtlLines = AUDIO_SINK_TOTAL_COUNT,
			.iLineSelect = {AUDIO_SINK_LOUDSPK, AUDIO_SINK_LOUDSPK},
			.strStreamName = "VD",
			.ctlLine = BCM_CTL_SINK_LINES,
		},

		//PCM In
		{
			.iFlags = MIXER_STREAM_FLAGS_CAPTURE,
			.iTotalCtlLines = MIC_TOTAL_COUNT_FOR_USER,
			.iLineSelect = {AUDIO_SOURCE_ANALOG_MAIN, AUDIO_SOURCE_ANALOG_MAIN},
			.strStreamName = "C1",
			.ctlLine = BCM_CTL_SRC_LINES,
		},

		//Speech In
		{
			.iFlags = MIXER_STREAM_FLAGS_CAPTURE,
			.iTotalCtlLines = MIC_TOTAL_COUNT_FOR_USER,
			.iLineSelect = {AUDIO_SOURCE_ANALOG_MAIN, AUDIO_SOURCE_ANALOG_MAIN},
			.strStreamName = "C2",
			.ctlLine = BCM_CTL_SRC_LINES,
		},
		//VOIP In
		{
			.iFlags = MIXER_STREAM_FLAGS_CAPTURE,
			.iTotalCtlLines = MIC_TOTAL_COUNT_FOR_USER,
			.iLineSelect = {AUDIO_SOURCE_ANALOG_MAIN, AUDIO_SOURCE_ANALOG_MAIN},
			.strStreamName = "VU",
			.ctlLine = BCM_CTL_SRC_LINES,
		},

		//Voice call
		{
			.iFlags = MIXER_STREAM_FLAGS_CALL,
			.iTotalCtlLines = AUDIO_SINK_TOTAL_COUNT,
			.iLineSelect = {AUDIO_SOURCE_ANALOG_MAIN, AUDIO_SINK_HANDSET},
			.strStreamName = "VC",
			.ctlLine = BCM_CTL_SINK_LINES,
		},
		//FM Radio
		{
			.iFlags = MIXER_STREAM_FLAGS_FM,
			.iTotalCtlLines = AUDIO_SINK_TOTAL_COUNT,
			.iLineSelect = {AUDIO_SINK_HEADSET, AUDIO_SINK_HEADSET},
			.strStreamName = "FM",
			.ctlLine = BCM_CTL_SINK_LINES,
		},

	};

//Misc controls
static struct snd_kcontrol_new sgSndCtrls[] __initdata =
{
	BRCM_MIXER_CTRL_MISC(0, 0, "LPT", 0, CAPH_CTL_PRIVATE(1, 1, CTL_FUNCTION_LOOPBACK_TEST) ),
	BRCM_MIXER_CTRL_MISC(0, 0, "AT-AUD", AT_AUD_CTL_INDEX, CAPH_CTL_PRIVATE(1, 1, CTL_FUNCTION_AT_AUDIO) ),
	BRCM_MIXER_CTRL_MISC(0, 0, "AT-AUD", AT_AUD_CTL_DBG_LEVEL, CAPH_CTL_PRIVATE(1, 1, CTL_FUNCTION_AT_AUDIO) ),
	BRCM_MIXER_CTRL_MISC(0, 0, "AT-AUD", AT_AUD_CTL_HANDLER, CAPH_CTL_PRIVATE(1, 1, CTL_FUNCTION_AT_AUDIO) ),
	BRCM_MIXER_CTRL_MISC(0, 0, "VC-SWT", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_VOICECALL, 0, CTL_FUNCTION_PHONE_ENABLE)),
	BRCM_MIXER_CTRL_MISC(0, 0, "VC-MUT", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_VOICECALL, 0, CTL_FUNCTION_PHONE_CALL_MIC_MUTE)),
	BRCM_MIXER_CTRL_MISC(0, 0, "P1-MIX", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_PCMOUT1, 0, CTL_FUNCTION_SPEECH_MIXING_OPTION)),
	BRCM_MIXER_CTRL_MISC(0, 0, "P2-MIX", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_PCMOUT2, 0, CTL_FUNCTION_SPEECH_MIXING_OPTION)),
	BRCM_MIXER_CTRL_MISC(0, 0, "C2-MIX", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_SPEECHIN, 0, CTL_FUNCTION_SPEECH_MIXING_OPTION)),	//CTL_STREAM_PANEL_SPEECHIN
	BRCM_MIXER_CTRL_MISC(0, 0, "FM-MIX", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_FM, 0, CTL_FUNCTION_SPEECH_MIXING_OPTION)),
	BRCM_MIXER_CTRL_MISC(0, 0, "FM-SWT", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_FM, 0, CTL_FUNCTION_FM_ENABLE)),
	BRCM_MIXER_CTRL_MISC(0, 0, "FM-FMT", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_FM, 0, CTL_FUNCTION_FM_FORMAT)),
	BRCM_MIXER_CTRL_MISC(0, 0, "BYP-VIB", 0, CAPH_CTL_PRIVATE(1, 1, CTL_FUNCTION_BYPASS_VIBRA) ),
	BRCM_MIXER_CTRL_MISC(0, 0, "BT-TST", 0, CAPH_CTL_PRIVATE(1, 1, CTL_FUNCTION_BT_TEST) ),
	BRCM_MIXER_CTRL_MISC(0, 0, "CFG-IHF", 0, CAPH_CTL_PRIVATE(1, 1, CTL_FUNCTION_CFG_IHF) ),
	BRCM_MIXER_CTRL_MISC(0, 0, "CFG-SSP", 0, CAPH_CTL_PRIVATE(1, 1, CTL_FUNCTION_CFG_SSP) ), //SSPI1
	BRCM_MIXER_CTRL_MISC(0, 0, "CFG-SSP", 1, CAPH_CTL_PRIVATE(1, 1, CTL_FUNCTION_CFG_SSP) ), //SSPI2
	BRCM_MIXER_CTRL_MISC(0, 0, "VC-VOL-LEVEL", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_VOICECALL, 0, CTL_FUNCTION_VOL)),
	BRCM_MIXER_CTRL_MISC(0, 0, "FM-VOL-LEVEL", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_FM, 0, CTL_FUNCTION_VOL)),
	BRCM_MIXER_CTRL_MISC_W(0, 0, "P1-CHG", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_PCMOUT1, 0, CTL_FUNCTION_SINK_CHG)),
	BRCM_MIXER_CTRL_MISC_W(0, 0, "P2-CHG", 0, CAPH_CTL_PRIVATE(CTL_STREAM_PANEL_PCMOUT2, 0, CTL_FUNCTION_SINK_CHG)),
};

#define	MAX_CTL_NUMS	160
#define	MAX_CTL_NAME_LENGTH	44
static char gStrCtlNames[MAX_CTL_NUMS][MAX_CTL_NAME_LENGTH] __initdata; // MAX_CTL_NAME_LENGTH];
static Int32 sgCaphSpeechMixCtrls[CAPH_MAX_PCM_STREAMS] __initdata = {1,1,0,3,3,0,0,1};

//*****************************************************************
// Functiona Name: ControlDeviceNew
//
// Description: Create control device.
//
//*****************************************************************
int __devinit ControlDeviceNew(struct snd_card *card)
{
	unsigned int idx, j;
	int err = 0;
	brcm_alsa_chip_t*	pChip = (brcm_alsa_chip_t*)card->private_data;
	int	nIndex=0;


	strcpy(card->mixername, "Broadcom CAPH Mixer");
	memcpy(pChip->streamCtl, &sgCaphStreamCtls, sizeof(sgCaphStreamCtls));

	//setting the default mixer selection for speech mixing
	memcpy(pChip->pi32SpeechMixOption, &sgCaphSpeechMixCtrls,sizeof(sgCaphSpeechMixCtrls));

	for (idx = 0; idx < ARRAY_SIZE(sgCaphStreamCtls); idx++)
	{
		//Selection
		struct snd_kcontrol_new devSelect = BRCM_MIXER_CTRL_SELECTION(0, 0, 0, 0, 0); //1234567890

		sprintf(gStrCtlNames[nIndex], "%s-SEL", sgCaphStreamCtls[idx].strStreamName);
		devSelect.name = gStrCtlNames[nIndex++];
		devSelect.private_value = CAPH_CTL_PRIVATE(idx+1, 0, 0);

		CAPH_ASSERT(strlen(devSelect.name)<MAX_CTL_NAME_LENGTH);
		if ((err = snd_ctl_add(card, snd_ctl_new1(&devSelect, pChip))) < 0)
		{
			BCM_AUDIO_DEBUG("Error to add devselect idx=%d\n", idx);
			return err;
		}

		//volume mute
		for(j=0; j<sgCaphStreamCtls[idx].iTotalCtlLines; j++)
		{
			struct snd_kcontrol_new kctlVolume = BRCM_MIXER_CTRL_VOLUME(0, 0, 0, 0, 0);
			struct snd_kcontrol_new kctlMute = BRCM_MIXER_CTRL_SWITCH(0, 0, "Mute", 0, 0);

			if(sgCaphStreamCtls[idx].ctlLine[j].strName[0]==0) //dummy line
				continue;

			if(sgCaphStreamCtls[idx].iFlags & MIXER_STREAM_FLAGS_CAPTURE)
			{

			sprintf(gStrCtlNames[nIndex], "%s-%s-GAN", sgCaphStreamCtls[idx].strStreamName, sgCaphStreamCtls[idx].ctlLine[j].strName);
			kctlVolume.name = gStrCtlNames[nIndex++];

			}
			else
			{
				sprintf(gStrCtlNames[nIndex], "%s-%s-VOL", sgCaphStreamCtls[idx].strStreamName, sgCaphStreamCtls[idx].ctlLine[j].strName);
				kctlVolume.name = gStrCtlNames[nIndex++];

			}

			kctlVolume.private_value = CAPH_CTL_PRIVATE(idx+1, j, CTL_FUNCTION_VOL);
			kctlMute.private_value = CAPH_CTL_PRIVATE(idx+1, j, CTL_FUNCTION_MUTE);

			CAPH_ASSERT(strlen(kctlVolume.name)<MAX_CTL_NAME_LENGTH);
			if ((err = snd_ctl_add(card, snd_ctl_new1(&kctlVolume, pChip))) < 0)
			{
				BCM_AUDIO_DEBUG("error to add volume for idx=%d j=%d err=%d \n", idx,j, err);
				return err;
			}

			if( 0 == (sgCaphStreamCtls[idx].iFlags & MIXER_STREAM_FLAGS_CALL) )//Not for voice call, voice call use only one MIC mute
			{
				sprintf(gStrCtlNames[nIndex], "%s-%s-MUT", sgCaphStreamCtls[idx].strStreamName, sgCaphStreamCtls[idx].ctlLine[j].strName);
				kctlMute.name = gStrCtlNames[nIndex++];
				CAPH_ASSERT(strlen(kctlMute.name)<MAX_CTL_NAME_LENGTH);
				if ((err = snd_ctl_add(card, snd_ctl_new1(&kctlMute, pChip))) < 0)
				{
					BCM_AUDIO_DEBUG("error to add mute for idx=%d j=%d err=%d\n", idx,j, err);
					return err;
				}
			}

		}
	}

	CAPH_ASSERT(nIndex<MAX_CTL_NUMS);

   //MISC
   {
		for(j=0;j<(sizeof((sgSndCtrls))/sizeof(sgSndCtrls[0]));j++)//index, debug level, AT handler
		{
			if ((err = snd_ctl_add(card, snd_ctl_new1(&sgSndCtrls[j], pChip))) < 0)
			{
				BCM_AUDIO_DEBUG("error (err=%d) when adding control name=%s  index=%d\n", err, sgSndCtrls[j].name, sgSndCtrls[j].index);
				return err;
			}
		}

	   //default value
	   pChip->i32CfgSSP[1] = 1; // must be consistent with driver. It is better to get hardware setting

   }

   return err;
}


//debugging assert util
void caphassert(const char *fcn, int line, const char *expr)
{
//	if (in_interrupt())
//		panic("ASSERTION FAILED IN INTERRUPT, %s:%s:%d %s\n",
//		      __FILE__, fcn, line, expr);
//	else
	{
		int x;
		pr_err("ASSERTION FAILED, %s:%s:%d %s\n",
		       __FILE__, fcn, line, expr);
		x = * (volatile int *) 0; /* force proc to exit */
	}
}


