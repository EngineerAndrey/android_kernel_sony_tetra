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


/*
*
*****************************************************************************
*
*  bcm_audio_thread.c
*
*  PURPOSE:
*
*     Serialize audio  control operation
*	Eliminate waits in audio control because of atomic operation requirement from ALSA
*
*  NOTES:
*
*****************************************************************************/

/* ---- Include Files ---------------------------------------------------- */

#include <linux/version.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/wait.h>

#include "mobcom_types.h"
#include "resultcode.h"
#include "audio_consts.h"
#include "ossemaphore.h"
#include "csl_caph.h"
#include "audio_vdriver.h"
#include "audio_controller.h"
#include "audio_ddriver.h"
#include "bcm_audio_devices.h"
#include "bcm_audio_thread.h"
#include "caph_common.h"



/* ---- Data structure  ------------------------------------------------- */

//++++++++++++++++++++++++++++++++++++++++++++++
//	Wrap up HAL_AUDIO_Ctrl parameters
//	The record is passed to worker thread via KFIFO
//
//----------------------------------------
typedef	struct	_TMsgBrcmAudioCtrl
{
	BRCM_AUDIO_ACTION_en_t			action_code;
	BRCM_AUDIO_Control_Params_un_t		param;
	void								*pCallBack;
    int                                 block;

}TMsgAudioCtrl, *PTMsgAudioCtrl;


//++++++++++++++++++++++++++++++++++++++++++++++
//	The thread private data structure
//
//----------------------------------------
typedef	struct	_TAudioHalThreadData
{
	struct kfifo	m_pkfifo;	//KFIFO to pass control parameters from audio HAL caller  to worker thread
	spinlock_t	m_lock;		//spin lock to protect KFIFO access so that audio HAL can accept concurrent caller
	struct work_struct mwork; //worker thread data structure
	struct workqueue_struct *pWorkqueue_AudioControl;
    Semaphore_t                     action_complete;
    struct kfifo	m_pkfifo_out;
	spinlock_t	m_lock_out;

}TAudioControlThreadData, *PTAudioControlThreadData;


static TAudioControlThreadData	sgThreadData;
#define	KFIFO_SIZE		(9*sizeof(TMsgAudioCtrl))


void AUDIO_Ctrl_Process(
	BRCM_AUDIO_ACTION_en_t action_code,
	void *arg_param,
	void *callback,
    int block
	);

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//AudioCtrlWorkThread
//	Worker thread, it query KFIFO for operation message and call HAL_AudioProcess.
//----------------------------------------------------------------
static void AudioCtrlWorkThread(struct work_struct *work)
{
	TMsgAudioCtrl	msgAudioCtrl;
	unsigned int len = 0;

	while(1)
	{
		//get operation code from fifo
		len = kfifo_out_locked(&sgThreadData.m_pkfifo, (unsigned char *)&msgAudioCtrl, sizeof(TMsgAudioCtrl), &sgThreadData.m_lock);		

		if( (len != sizeof(TMsgAudioCtrl)) && (len!=0) )
			BCM_AUDIO_DEBUG("Error AUDIO_Ctrl len=%d expected %d in=%d, out=%d\n", len, sizeof(TMsgAudioCtrl), sgThreadData.m_pkfifo.in, sgThreadData.m_pkfifo.out);
		if(len == 0) //FIFO empty sleep
			return;
		
		//process the operation
		AUDIO_Ctrl_Process(msgAudioCtrl.action_code, &msgAudioCtrl.param, msgAudioCtrl.pCallBack,msgAudioCtrl.block);
	}

	return;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//LaunchAudioHalThread
//	Create Worker thread.
//----------------------------------------------------------------
int LaunchAudioCtrlThread(void)
{
	int ret;
	sgThreadData.m_lock =  SPIN_LOCK_UNLOCKED;
	
	ret = kfifo_alloc(&sgThreadData.m_pkfifo,KFIFO_SIZE, GFP_KERNEL);
	BCM_AUDIO_DEBUG("LaunchAudioCtrlThread KFIFO_SIZE= %d actual =%d\n", KFIFO_SIZE,sgThreadData.m_pkfifo.size);
    ret = kfifo_alloc(&sgThreadData.m_pkfifo_out, KFIFO_SIZE, GFP_KERNEL);
  	BCM_AUDIO_DEBUG("LaunchAudioCtrlThread KFIFO_SIZE= %d actual =%d\n", KFIFO_SIZE,sgThreadData.m_pkfifo_out.size);
	INIT_WORK(&sgThreadData.mwork, AudioCtrlWorkThread);
	
	sgThreadData.pWorkqueue_AudioControl = create_workqueue("AudioCtrlWq");
	if(!sgThreadData.pWorkqueue_AudioControl)
		DEBUG("\n Error : Can not create work queue:AudioCtrlWq\n");
    sgThreadData.action_complete = OSSEMAPHORE_Create(0,0);

	return ret;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//LaunchAudioHalThread
//	Clean up: free KFIFO
//----------------------------------------------------------------
int TerminateAudioHalThread(void)
{
	
	if(sgThreadData.pWorkqueue_AudioControl)
	{
		flush_workqueue(sgThreadData.pWorkqueue_AudioControl);
		destroy_workqueue(sgThreadData.pWorkqueue_AudioControl);
	}
	kfifo_free(&sgThreadData.m_pkfifo);
    kfifo_free(&sgThreadData.m_pkfifo_out);
	return 0;
}



//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//HAL_AUDIO_Ctrl
//	Client call this function to execute audio HAL functions.
//	This function for the message to worker thread to do actual work
//----------------------------------------------------------------
Result_t AUDIO_Ctrl_Trigger(
	BRCM_AUDIO_ACTION_en_t action_code,
	void *arg_param,
	void *callback,
    int block
	)
{
	TMsgAudioCtrl	msgAudioCtrl;
	Result_t status = RESULT_OK;
	unsigned int	len;
    OSStatus_t  osStatus;

	BCM_AUDIO_DEBUG("AudioHalThread action=%d\r\n", action_code);

	msgAudioCtrl.action_code = action_code;
	if(arg_param)
		memcpy(&msgAudioCtrl.param, arg_param, sizeof(BRCM_AUDIO_Control_Params_un_t));
	else
		memset(&msgAudioCtrl.param, 0, sizeof(BRCM_AUDIO_Control_Params_un_t));
	msgAudioCtrl.pCallBack = callback;
    msgAudioCtrl.block = block;

	len = kfifo_in_locked(&sgThreadData.m_pkfifo, (unsigned char *)&msgAudioCtrl, sizeof(TMsgAudioCtrl), &sgThreadData.m_lock);
	if(len != sizeof(TMsgAudioCtrl))
		BCM_AUDIO_DEBUG("Error AUDIO_Ctrl_Trigger len=%d expected %d \n", len, sizeof(TMsgAudioCtrl));

	queue_work(sgThreadData.pWorkqueue_AudioControl, &sgThreadData.mwork);
    if(block)
    {
        // wait for 10sec
        osStatus = OSSEMAPHORE_Obtain(sgThreadData.action_complete,1280);
        if(osStatus != OSSTATUS_SUCCESS)
        {
            BCM_AUDIO_DEBUG("AUDIO_Ctrl_Trigger Timeout=%d\r\n",osStatus);
        }

        while(1)
	    {
            //wait for output from output fifo
		    len = kfifo_out_locked(&sgThreadData.m_pkfifo_out, (unsigned char *)&msgAudioCtrl, sizeof(TMsgAudioCtrl), &sgThreadData.m_lock_out);
		    if( (len != sizeof(TMsgAudioCtrl)) && (len!=0) )
			    BCM_AUDIO_DEBUG("Error AUDIO_Ctrl_Trigger len=%d expected %d in=%d, out=%d\n", len, sizeof(TMsgAudioCtrl), sgThreadData.m_pkfifo_out.in, sgThreadData.m_pkfifo_out.out);
		    if(len == 0) //FIFO empty sleep
			    return status;
            if(arg_param)
		        memcpy(arg_param,&msgAudioCtrl.param,  sizeof(BRCM_AUDIO_Control_Params_un_t));
	        else
		        memset(arg_param, 0, sizeof(BRCM_AUDIO_Control_Params_un_t));
	    }
    }
	
	return status;
}

void AUDIO_Ctrl_Process(
	BRCM_AUDIO_ACTION_en_t action_code,
	void *arg_param,
	void *callback,
    int  block
	)
{
   	TMsgAudioCtrl	msgAudioCtrl;
	unsigned int	len;

	BCM_AUDIO_DEBUG("AUDIO_Ctrl_Process action_code=%d\r\n", action_code);
	
    switch (action_code)
    {
		case ACTION_AUD_OpenPlay:
		{
			BRCM_AUDIO_Param_Open_t* param_open = (BRCM_AUDIO_Param_Open_t*) arg_param;
			
			param_open->drv_handle = AUDIO_DRIVER_Open(param_open->pdev_prop->p[0].drv_type);
		    if(param_open->drv_handle == NULL)
    		{
         		BCM_AUDIO_DEBUG("\n %lx:AUDIO_Ctrl_Process-AUDIO_DRIVER_Open  failed\n",jiffies);
    		}
		}
		break;
			
		case ACTION_AUD_ClosePlay:
		{
			BRCM_AUDIO_Param_Close_t* param_close = (BRCM_AUDIO_Param_Close_t*) arg_param;
			
			AUDIO_DRIVER_Close(param_close->drv_handle);
		}
		break;
					
        case ACTION_AUD_StartPlay:
        {
            BRCM_AUDIO_Param_Start_t* param_start = (BRCM_AUDIO_Param_Start_t*) arg_param;

			if(param_start->pdev_prop->p[0].drv_type == AUDIO_DRIVER_PLAY_AUDIO)
			{

	            AUDCTRL_SaveAudioModeFlag( param_start->pdev_prop->p[0].speaker );

            // Enable the playback the path
            AUDCTRL_EnablePlay(AUDIO_HW_MEM, 
                                   param_start->pdev_prop->p[0].hw_id,
                                   AUDIO_HW_NONE,
                                   param_start->pdev_prop->p[0].speaker,
				                   param_start->channels,
                                   param_start->rate, 
                                   NULL);

			AUDCTRL_SetPlayVolume (param_start->pdev_prop->p[0].hw_id,
					param_start->pdev_prop->p[0].speaker, 
					AUDIO_GAIN_FORMAT_Q13_2, 
					param_start->vol[0], param_start->vol[1]); //0DB 

     			AUDIO_DRIVER_Ctrl(param_start->drv_handle,AUDIO_DRIVER_START,&param_start->pdev_prop->p[0].aud_dev);
			
			}
			else if(param_start->pdev_prop->p[0].drv_type == AUDIO_DRIVER_PLAY_VOICE)
			{
				AUDIO_DRIVER_Ctrl(param_start->drv_handle,AUDIO_DRIVER_START,NULL);
			}
        }
        break;
        case ACTION_AUD_StopPlay:
        {
            BRCM_AUDIO_Param_Stop_t* param_stop = (BRCM_AUDIO_Param_Stop_t*) arg_param;
	 
			AUDIO_DRIVER_Ctrl(param_stop->drv_handle,AUDIO_DRIVER_STOP,NULL);

			// Remove secondary playback path if it's in use
			if(param_stop->pdev_prop->p[1].drv_type == AUDIO_DRIVER_PLAY_AUDIO)
			{
            	AUDCTRL_RemovePlaySpk(param_stop->pdev_prop->p[0].hw_id,
										param_stop->pdev_prop->p[0].speaker,
										param_stop->pdev_prop->p[1].hw_id,
										param_stop->pdev_prop->p[1].speaker);
				param_stop->pdev_prop->p[1].hw_id = AUDIO_HW_NONE;
				param_stop->pdev_prop->p[1].speaker = AUDDRV_DEV_NONE;
			}

		    if(param_stop->pdev_prop->p[0].drv_type == AUDIO_DRIVER_PLAY_AUDIO)
			{
			     //disable the playback path
    	    	   AUDCTRL_DisablePlay(AUDIO_HW_MEM, 
                        param_stop->pdev_prop->p[0].hw_id,
                        param_stop->pdev_prop->p[0].speaker,0
                    );
		    }
			BCM_AUDIO_DEBUG("AUDIO_Ctrl_Process Stop Playback completed \n");
        }
        break;
        case ACTION_AUD_PausePlay:
        {
            BRCM_AUDIO_Param_Pause_t* param_pause = (BRCM_AUDIO_Param_Pause_t*) arg_param;
			
			if(param_pause->pdev_prop->p[0].drv_type == AUDIO_DRIVER_PLAY_AUDIO)
			{
            	//disable the playback path
             	AUDCTRL_DisablePlay(AUDIO_HW_MEM,	
                        param_pause->pdev_prop->p[0].hw_id,
                        param_pause->pdev_prop->p[0].speaker,0
                    ); 
			}
            AUDIO_DRIVER_Ctrl(param_pause->drv_handle,AUDIO_DRIVER_PAUSE,NULL);
        }
        break;

        case ACTION_AUD_ResumePlay:
        {
            BRCM_AUDIO_Param_Resume_t* param_resume = (BRCM_AUDIO_Param_Resume_t*) arg_param;

            AUDIO_DRIVER_Ctrl(param_resume->drv_handle,AUDIO_DRIVER_RESUME,NULL);
		
			if(param_resume->pdev_prop->p[0].drv_type == AUDIO_DRIVER_PLAY_AUDIO)
			{

           		// Enable the playback the path
            	AUDCTRL_EnablePlay(AUDIO_HW_MEM,	
                                   param_resume->pdev_prop->p[0].hw_id,
                                   AUDIO_HW_NONE,
                                   param_resume->pdev_prop->p[0].speaker,
				                   param_resume->channels,
                                   param_resume->rate, NULL
								    );
			}
        }
        break;
        case ACTION_AUD_StartRecord:
        {
            BRCM_AUDIO_Param_Start_t* param_start = (BRCM_AUDIO_Param_Start_t*) arg_param;
			
			if((param_start->callMode != 1) || (param_start->pdev_prop->c.mic == AUDCTRL_MIC_I2S)) // allow FM recording in call mode
			{
	        	AUDCTRL_EnableRecord(param_start->pdev_prop->c.hw_id,
				                     param_start->pdev_prop->c.hw_sink,	
                                     param_start->pdev_prop->c.mic,
				                     param_start->channels,
                                     param_start->rate);
	            AUDCTRL_SetRecordGain(param_start->pdev_prop->c.hw_id,
                                  param_start->pdev_prop->c.mic,
                                  AUDIO_GAIN_FORMAT_Q13_2,
                                  param_start->vol[0],
                                  param_start->vol[1]);
			}
			if(param_start->pdev_prop->c.drv_type == AUDIO_DRIVER_CAPT_HQ)
				AUDIO_DRIVER_Ctrl(param_start->drv_handle,AUDIO_DRIVER_START,&param_start->pdev_prop->c.aud_dev); 
			else
				AUDIO_DRIVER_Ctrl(param_start->drv_handle,AUDIO_DRIVER_START,&param_start->mixMode); 
			
        }
        break;
        case ACTION_AUD_StopRecord:
        {
            BRCM_AUDIO_Param_Stop_t* param_stop = (BRCM_AUDIO_Param_Stop_t*) arg_param;
               
            AUDIO_DRIVER_Ctrl(param_stop->drv_handle,AUDIO_DRIVER_STOP,NULL);

			if((param_stop->callMode != 1) || (param_stop->pdev_prop->c.mic == AUDCTRL_MIC_I2S)) // allow FM recording in call mode
			{		
            	AUDCTRL_DisableRecord(param_stop->pdev_prop->c.hw_id,
                                      AUDIO_HW_MEM,
                                      param_stop->pdev_prop->c.mic);
			}

        }
        break;
		case ACTION_AUD_OpenRecord:
		{
         	BRCM_AUDIO_Param_Open_t* param_open = (BRCM_AUDIO_Param_Open_t*) arg_param;

            param_open->drv_handle = AUDIO_DRIVER_Open(param_open->pdev_prop->c.drv_type);

            BCM_AUDIO_DEBUG("param_open->drv_handle -  0x%lx \n",(UInt32)param_open->drv_handle);
		
		}
	    break;
		case ACTION_AUD_CloseRecord:
		{
			BRCM_AUDIO_Param_Close_t* param_close = (BRCM_AUDIO_Param_Close_t*) arg_param;
			
			AUDIO_DRIVER_Close(param_close->drv_handle);
		}
		break;
		case ACTION_AUD_AddChannel:
		{
            BRCM_AUDIO_Param_Start_t* param_start = (BRCM_AUDIO_Param_Start_t*) arg_param;

			if(param_start->pdev_prop->p[1].drv_type == AUDIO_DRIVER_PLAY_AUDIO)
			{
            	AUDCTRL_AddPlaySpk(param_start->pdev_prop->p[0].hw_id,
                                   param_start->pdev_prop->p[0].speaker,
									param_start->pdev_prop->p[1].hw_id,
									param_start->pdev_prop->p[1].speaker);
			}
		}
		break;
		case ACTION_AUD_EnableTelephony:
		{
			BRCM_AUDIO_Param_Call_t *parm_call = (BRCM_AUDIO_Param_Call_t *)arg_param;
			AUDCTRL_EnableTelephony(AUDIO_HW_VOICE_IN,
									AUDIO_HW_VOICE_OUT,
									(AUDCTRL_MICROPHONE_t)parm_call->new_mic,
									(AUDCTRL_SPEAKER_t)parm_call->new_spkr);
		}
		break;
		case ACTION_AUD_DisableTelephony:
		{
			BRCM_AUDIO_Param_Call_t *parm_call = (BRCM_AUDIO_Param_Call_t *)arg_param;
			AUDCTRL_DisableTelephony(AUDIO_HW_VOICE_IN,
									 AUDIO_HW_VOICE_OUT,
									 (AUDCTRL_MICROPHONE_t)parm_call->cur_mic,
									 (AUDCTRL_SPEAKER_t)parm_call->cur_spkr);
		}
		break;
		case ACTION_AUD_MutePlayback:
		{
			BRCM_AUDIO_Param_Mute_t *parm_mute = (BRCM_AUDIO_Param_Mute_t *)arg_param;
			AUDCTRL_SetPlayMute (parm_mute->hw_id,
									parm_mute->device,
									parm_mute->mute1);	//currently driver doesnt handle Mute for left/right channels
		}
		case ACTION_AUD_MuteRecord:
		{
			BRCM_AUDIO_Param_Mute_t *parm_mute = (BRCM_AUDIO_Param_Mute_t *)arg_param;
			AUDCTRL_SetRecordMute (parm_mute->hw_id,
		 							parm_mute->device,
		 							parm_mute->mute1);
		}
		break;
		case ACTION_AUD_EnableByPassVibra:
		{
			AUDCTRL_EnableBypassVibra();
		}
		break;
		case ACTION_AUD_SetVibraStrength:
		{
			BRCM_AUDIO_Param_Vibra_t *parm_vibra = (BRCM_AUDIO_Param_Vibra_t *)arg_param;
			AUDCTRL_SetBypassVibraStrength(parm_vibra->strength, parm_vibra->direction);
		}
		break;
		case ACTION_AUD_DisableByPassVibra:
		{
			AUDCTRL_DisableBypassVibra();
		}
		break;
		case ACTION_AUD_SetPlaybackVolume:
		{
			BRCM_AUDIO_Param_Volume_t *parm_vol = (BRCM_AUDIO_Param_Volume_t *)arg_param;
			AUDCTRL_SetPlayVolume (parm_vol->hw_id,
								   parm_vol->device,
								   AUDIO_GAIN_FORMAT_Q13_2,
								   parm_vol->volume1,
								   parm_vol->volume2);
		}
		break;
		case ACTION_AUD_SetRecordGain:
		{
			BRCM_AUDIO_Param_Volume_t *parm_vol = (BRCM_AUDIO_Param_Volume_t *)arg_param;
			AUDCTRL_SetRecordGain (parm_vol->hw_id,
								   parm_vol->device,
								   AUDIO_GAIN_FORMAT_Q13_2,
								   parm_vol->volume1,
								   parm_vol->volume2);
		}
		break;
		case ACTION_AUD_SetTelephonySpkrVolume:
		{
			BRCM_AUDIO_Param_Volume_t *parm_vol = (BRCM_AUDIO_Param_Volume_t *)arg_param;
			AUDCTRL_SetTelephonySpkrVolume (AUDIO_HW_VOICE_OUT,
											parm_vol->device,
											parm_vol->volume1,
											AUDIO_GAIN_FORMAT_Q13_2);//DL
		}
		break;
		case ACTION_AUD_SwitchSpkr:
		{
			BRCM_AUDIO_Param_Spkr_t *parm_spkr =  (BRCM_AUDIO_Param_Spkr_t *)arg_param;
			AUDCTRL_SwitchPlaySpk(parm_spkr->cur_sink,
									parm_spkr->cur_spkr,
									parm_spkr->new_sink,
									parm_spkr->new_spkr);
		}
		break;
		case ACTION_AUD_AddSpkr:
		{
			BRCM_AUDIO_Param_Spkr_t *parm_spkr =  (BRCM_AUDIO_Param_Spkr_t *)arg_param;
			AUDCTRL_AddPlaySpk(parm_spkr->cur_sink,
								parm_spkr->cur_spkr,
								parm_spkr->new_sink,
								parm_spkr->new_spkr);
		}
		break;
		case ACTION_AUD_SetAudioMode:
		{
			BRCM_AUDIO_Param_Call_t *parm_call =  (BRCM_AUDIO_Param_Call_t *)arg_param;
			AudioMode_t tempMode = (AudioMode_t)parm_call->new_spkr;
			if ((AUDIO_SAMPLING_RATE_t)AUDCTRL_RateGetTelephony() == AUDIO_SAMPLING_RATE_16000)
			{
				tempMode += AUDIO_MODE_NUMBER;
            }
			AUDCTRL_SaveAudioModeFlag(tempMode);
		}
		break;
		case ACTION_AUD_SetHWLoopback:
		{
			BRCM_AUDIO_Param_Loopback_t *parm_loop = (BRCM_AUDIO_Param_Loopback_t *)arg_param;
			AUDCTRL_SetAudioLoopback(parm_loop->parm,(AUDCTRL_MICROPHONE_t)parm_loop->mic,(AUDCTRL_SPEAKER_t)parm_loop->spkr);
		}
		break;
		case ACTION_AUD_EnableFMPlay:
		{
			BRCM_AUDIO_Param_FM_t *parm_FM = (BRCM_AUDIO_Param_FM_t *)arg_param;
			//re-enable FM
			AUDCTRL_SaveAudioModeFlag((AudioMode_t)parm_FM->device);
			AUDCTRL_EnablePlay(AUDIO_HW_I2S_IN,
								parm_FM->hw_id,  // =AUDIO_HW_DSP_VOICE if CallMode = 1
								AUDIO_HW_NONE,
								parm_FM->device,
								AUDIO_CHANNEL_STEREO,
								AUDIO_SAMPLING_RATE_48000,
								NULL);

            AUDCTRL_SetPlayVolume (parm_FM->hw_id,
                                       parm_FM->device,
                                       AUDIO_GAIN_FORMAT_Q13_2,
                                       parm_FM->volume1,
                                       parm_FM->volume2);
		}
		break;
		case ACTION_AUD_DisableFMPlay:
		{
			BRCM_AUDIO_Param_FM_t *parm_FM = (BRCM_AUDIO_Param_FM_t *)arg_param;
			AUDCTRL_DisablePlay(AUDIO_HW_I2S_IN,
								parm_FM->hw_id,
								parm_FM->device,
								0); 
		}
		break;
		case ACTION_AUD_SetARM2SPInst:
		{
			BRCM_AUDIO_Param_FM_t *parm_FM = (BRCM_AUDIO_Param_FM_t *)arg_param;
			AUDCTRL_SetArm2spParam(parm_FM->fm_mix, 1); // use ARM2SP instance 1 for FM
		}
		break;
		case ACTION_AUD_SetPrePareParameters:
		{
			BRCM_AUDIO_Param_Prepare_t *parm_prepare = (BRCM_AUDIO_Param_Prepare_t *)arg_param;
			//set the callback
			AUDIO_DRIVER_Ctrl(parm_prepare->drv_handle,AUDIO_DRIVER_SET_CB,(void*)&parm_prepare->cbParams);
			//set the interrupt period
		    AUDIO_DRIVER_Ctrl(parm_prepare->drv_handle,AUDIO_DRIVER_SET_INT_PERIOD,(void*)&parm_prepare->period_bytes);
			//set the buffer params
			AUDIO_DRIVER_Ctrl(parm_prepare->drv_handle,AUDIO_DRIVER_SET_BUF_PARAMS,(void*)&parm_prepare->buf_param);
			//Configure stream params
			AUDIO_DRIVER_Ctrl(parm_prepare->drv_handle,AUDIO_DRIVER_CONFIG,(void*)&parm_prepare->drv_config);
		}
		break;
        default:
            BCM_AUDIO_DEBUG("Error AUDIO_Ctrl_Process Invalid acction command \n");
			break;
    }
    if(block)
    {
        // put the message in output fifo if waiting
        msgAudioCtrl.action_code = action_code;
	    if(arg_param)
		    memcpy(&msgAudioCtrl.param, arg_param, sizeof(BRCM_AUDIO_Control_Params_un_t));
	    else
		    memset(&msgAudioCtrl.param, 0, sizeof(BRCM_AUDIO_Control_Params_un_t));
	    msgAudioCtrl.pCallBack = callback;
        msgAudioCtrl.block = block;

        len = kfifo_in_locked(&sgThreadData.m_pkfifo_out, (unsigned char *)&msgAudioCtrl, sizeof(TMsgAudioCtrl), &sgThreadData.m_lock_out);
        if(len != sizeof(TMsgAudioCtrl))
		    BCM_AUDIO_DEBUG("Error AUDIO_Ctrl_Process len=%d expected %d \n", len, sizeof(TMsgAudioCtrl));
        // release the semaphore 
        OSSEMAPHORE_Release(sgThreadData.action_complete);

    }

}
