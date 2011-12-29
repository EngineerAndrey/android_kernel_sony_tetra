/************************************************************************************************/
/*                                                                                              */
/*  Copyright 2011  Broadcom Corporation                                                        */
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
* @file   audio_ddriver.c
* @brief  
*
******************************************************************************/

//=============================================================================
// Include directives
//=============================================================================


#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
   
#include "mobcom_types.h"
#include "resultcode.h"
#include "audio_consts.h"

#include "bcm_fuse_sysparm_CIB.h"
#include "audio_ddriver.h"

#include "log.h"
#include "csl_caph.h"
#include "audio_vdriver.h"
#include "csl_apcmd.h"
#include "csl_audio_render.h"
#include "csl_audio_capture.h"
#include "csl_arm2sp.h"
#include "csl_vpu.h"
#include "dspcmd.h"
#include "csl_voip.h"
#include "csl_voif.h"
#include "csl_caph_hwctrl.h"
#include "audio_controller.h"

//=============================================================================
// Public Variable declarations
//=============================================================================


#define VOIP_MAX_FRAME_LEN		642
#ifdef VOLTE_SUPPORT
#define VOLTECALLENABLE			0x2	
#define VOLTEWBSTAMPSTEP		320
#define VOLTENBSTAMPSTEP		160
#define VOLTEFRAMEGOOD			1
#define VOLTEFRAMESILENT		0
#endif
#define VOIF_8K_SAMPLE_COUNT    160
#define VOIF_16K_SAMPLE_COUNT   320

typedef struct ARM2SP_PLAYBACK_t
{
	CSL_ARM2SP_PLAYBACK_MODE_t playbackMode;
	CSL_ARM2SP_VOICE_MIX_MODE_t mixMode;
	UInt32 instanceID; //ARM2SP1 or ARM2SP2
	UInt16 	numFramesPerInterrupt;
	UInt8 	audMode; 
}ARM2SP_PLAYBACK_t;

typedef struct VOICE_CAPT_t
{
    UInt32   num_frames;
    UInt32   frame_size;
    UInt32   speech_mode;
	VOCAPTURE_RECORD_MODE_t recordMode;
}VOICE_CAPT_t;

typedef struct VOIP_t
{
	void *									pVoIPCBPrivate;
	AUDIO_DRIVER_VoipCB_t					pVoipULCallback;
	AUDIO_DRIVER_VoipCB_t					pVoipDLCallback;
	UInt16									codec_type;
	Boolean									isVoLTECall;
}VOIP_t;

typedef struct AUDDRV_VOIF_t
{
	UInt8									isRunning;
	VOIF_CB									cb;
} AUDDRV_VOIF_t;

typedef struct AUDIO_DDRIVER_t
{
    AUDIO_DRIVER_TYPE_t                     drv_type;
    AUDIO_DRIVER_InterruptPeriodCB_t        pCallback;
	void *									pCBPrivate;
    UInt32                                  interrupt_period;
    AUDIO_SAMPLING_RATE_t                   sample_rate;
    AUDIO_NUM_OF_CHANNEL_t		            num_channel;
    AUDIO_BITS_PER_SAMPLE_t	                bits_per_sample;
    UInt8*                                  ring_buffer;
    UInt32                                  ring_buffer_size;
    UInt32                                  ring_buffer_phy_addr;
    UInt32                                  stream_id;
    UInt32                                  read_index;
    UInt32                                  write_index;
    UInt16*                                  tmp_buffer;	
	UInt32									bufferSize_inBytes;
	UInt32 									num_periods;
	ARM2SP_PLAYBACK_t						arm2sp_config;
	VOICE_CAPT_t							voicecapt_config;
	VOIP_t									voip_config;
	
}AUDIO_DDRIVER_t;


//=============================================================================
// Private Type and Constant declarations
//=============================================================================
static AUDIO_DDRIVER_t* audio_render_driver[CSL_CAPH_STREAM_TOTAL];
static AUDIO_DDRIVER_t* audio_voice_driver[VORENDER_ARM2SP_INSTANCE_TOTAL]; // 2 ARM2SP instances
static AUDIO_DDRIVER_t* audio_voip_driver = NULL;
static AUDIO_DDRIVER_t* audio_capture_driver = NULL;
static int index = 1;
static Boolean endOfBuffer = FALSE;
static const UInt16 sVoIPDataLen[] = {0, 322, 160, 38, 166, 642, 70};
static CSL_VP_Mode_AMR_t prev_amr_mode = (CSL_VP_Mode_AMR_t)0xffff;
static Boolean				telephony_amr_if2;


static struct work_struct voip_work;
static struct workqueue_struct *voip_workqueue = NULL;
#ifdef VOLTE_SUPPORT
static UInt32 djbTimeStamp = 0;
static DJB_InputFrame *djbBuf = NULL;
static Boolean inVoLTECall = FALSE;
#endif
static AUDDRV_VOIF_t voifDrv = { 0 };
static Boolean voif_enabled = 0;

//=============================================================================
// xternal prototypes
//=============================================================================
extern UInt32 audio_control_dsp(UInt32 param1,UInt32 param2,UInt32 param3,UInt32 param4,UInt32 param5,UInt32 param6);

//=============================================================================
// Private function prototypes
//=============================================================================

void VOIP_ProcessVOIPDLDone(void);

static Result_t AUDIO_DRIVER_ProcessRenderCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct);
static Result_t AUDIO_DRIVER_ProcessVoiceRenderCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct);


static Result_t AUDIO_DRIVER_ProcessCommonCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct);

static Result_t AUDIO_DRIVER_ProcessCaptureCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct);

static Result_t AUDIO_DRIVER_ProcessVoIPCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct);

static Result_t AUDIO_DRIVER_ProcessVoIFCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct);

static Result_t AUDIO_DRIVER_ProcessCaptureVoiceCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct);
static Result_t ARM2SP_play_start (AUDIO_DDRIVER_t* aud_drv,
										UInt32	numFramesPerInterrupt);

static Result_t ARM2SP_play_resume (AUDIO_DDRIVER_t* aud_drv);

static Result_t VPU_record_start ( VOCAPTURE_RECORD_MODE_t	recordMode,
								AUDIO_SAMPLING_RATE_t		samplingRate,
								UInt32						speechMode, // used by AMRNB and AMRWB
								UInt32						dataRate, // used by AMRNB and AMRWB
								Boolean						procEnable,
								Boolean						dtxEnable,
								UInt32						numFramesPerInterrupt);

//static Boolean VoIP_StartTelephony(VOIPDumpFramesCB_t telephony_dump_cb,VOIPFillFramesCB_t telephony_fill_cb);
static Boolean VoIP_StartTelephony(void);

static Boolean VoIP_StopTelephony(void);

static void VoIP_StartMainAMRDecodeEncode(CSL_VP_Mode_AMR_t	decode_amr_mode,	// AMR mode for decoding the next speech frame
										UInt8				*pBuf,		// buffer carrying the AMR speech data to be decoded
										UInt16				length,		// number of bytes of the AMR speech data to be decoded
										CSL_VP_Mode_AMR_t	encode_amr_mode,	// AMR mode for encoding the next speech frame
										Boolean				dtx_mode	// Turn DTX on (TRUE) or off (FALSE)
										);
static void AUDIO_DRIVER_RenderDmaCallback(UInt32 stream_id);
static void AUDIO_DRIVER_CaptureDmaCallback(UInt32 stream_id);
static Boolean VOIP_DumpUL_CB(
		UInt8		*pSrc,		// pointer to start of speech data
		UInt32		amrMode		// AMR codec mode of speech data
		);
static Boolean VOIP_FillDL_CB(UInt32 nFrames);

#ifdef VOLTE_SUPPORT
static Boolean VoLTE_WriteDLData(UInt16 decode_mode, UInt16 *pBuf);
#endif

//=============================================================================
// Functions
//=============================================================================

UInt32 StreamIdOfDriver(AUDIO_DRIVER_HANDLE_t h)
{
	AUDIO_DDRIVER_t *ph=(AUDIO_DDRIVER_t *)h;
	return ph->stream_id;
}



static int	SetPlaybackStreamHandle(AUDIO_DDRIVER_t* h)
{
	if(h->stream_id>=CSL_CAPH_STREAM_TOTAL)
	{
		Log_DebugPrintf(LOGID_AUDIO,"Error: SetPlaybackStreamHandle invalid stream id=%ld\n" ,h->stream_id);
		return -1;
	}
	if(audio_render_driver[h->stream_id]!=NULL)
		Log_DebugPrintf(LOGID_AUDIO,"Warnning: SetPlaybackStreamHandle handle of stream id=%ld is overwritten pre=%p, after=%p\n" ,h->stream_id, audio_render_driver[h->stream_id], h);

	audio_render_driver[h->stream_id]=h;
	
	return -1;
}

static AUDIO_DDRIVER_t* GetPlaybackStreamHandle(UInt32 streamID)
{
	if(	audio_render_driver[streamID]==NULL)
	    Log_DebugPrintf(LOGID_AUDIO,"Error: GetPlaybackStreamHandle invalid handle for id %ld\n", streamID  );
	return audio_render_driver[streamID];
}

static int ResetPlaybackStreamHandle(UInt32 streamID)
{

	if(	audio_render_driver[streamID]==NULL)
	    Log_DebugPrintf(LOGID_AUDIO,"Warning: ResetPlaybackStreamHandle invalid handle for id %ld\n", streamID  );
	audio_render_driver[streamID] = NULL;

	return 0;
}


//============================================================================
//
// Function Name: AUDIO_DRIVER_Open
//
// Description:   This function is used to open the audio data driver
//
//============================================================================
AUDIO_DRIVER_HANDLE_t  AUDIO_DRIVER_Open(AUDIO_DRIVER_TYPE_t drv_type)
{
    AUDIO_DDRIVER_t*  aud_drv = NULL;
    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_Open::  \n"  );

    // allocate memory
	aud_drv = (AUDIO_DDRIVER_t*) kzalloc(sizeof(AUDIO_DDRIVER_t), GFP_KERNEL);
    aud_drv->drv_type = drv_type;
    aud_drv->pCallback = NULL;
    aud_drv->interrupt_period = 0;
    aud_drv->sample_rate = 0;
    aud_drv->num_channel = AUDIO_CHANNEL_NUM_NONE;
    aud_drv->bits_per_sample = 0;
    aud_drv->ring_buffer = NULL;
    aud_drv->ring_buffer_size = 0;
    aud_drv->stream_id = 0;
    aud_drv->read_index = 0;
    aud_drv->write_index = 0;
    aud_drv->voicecapt_config.num_frames = 0;
    aud_drv->voicecapt_config.frame_size = 0;
    aud_drv->voicecapt_config.speech_mode = 0;
	aud_drv->voicecapt_config.recordMode = 0;
    aud_drv->tmp_buffer = NULL;

    switch (drv_type)
    {
        case AUDIO_DRIVER_PLAY_VOICE:
        case AUDIO_DRIVER_PLAY_AUDIO:
        case AUDIO_DRIVER_PLAY_RINGER:
        case AUDIO_DRIVER_CAPT_HQ:
        case AUDIO_DRIVER_CAPT_VOICE:
		case AUDIO_DRIVER_VOIF:
            break;
			
		case AUDIO_DRIVER_VOIP:
            audio_voip_driver = aud_drv;
            break;

        default:
            Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_Open::Unsupported driver  \n"  );
            break;
    }
    return ((AUDIO_DRIVER_HANDLE_t)aud_drv);
}

//============================================================================
//
// Function Name: AUDIO_DRIVER_Close
//
// Description:   This function is used to close the audio data driver
//
//============================================================================
void AUDIO_DRIVER_Close(AUDIO_DRIVER_HANDLE_t drv_handle)
{
    AUDIO_DDRIVER_t*  aud_drv = (AUDIO_DDRIVER_t*)drv_handle;
    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_Close  \n"  );

    if(aud_drv == NULL)
    {
        Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_Close::Invalid Handle  \n"  );
        return;
    }

    switch (aud_drv->drv_type)
    {
        case AUDIO_DRIVER_PLAY_VOICE:
        case AUDIO_DRIVER_PLAY_AUDIO:
        case AUDIO_DRIVER_PLAY_RINGER:		
        case AUDIO_DRIVER_CAPT_HQ:
        case AUDIO_DRIVER_CAPT_VOICE:
		case AUDIO_DRIVER_VOIF:
            break;
		case AUDIO_DRIVER_VOIP:
			{
            audio_voip_driver = NULL;
			kfree(aud_drv->tmp_buffer);
			aud_drv->tmp_buffer = NULL;
			}
            break;
        default:
            Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_Close::Unsupported driver  \n"  );
            break;
    }
    //free the driver structure
    kfree(aud_drv);
    return;  
}

//============================================================================
//
// Function Name: AUDIO_DRIVER_Read
//
// Description:   This function is used to read the data from the driver
//
//============================================================================
void AUDIO_DRIVER_Read(AUDIO_DRIVER_HANDLE_t drv_handle,
                  UInt8* pBuf,
                  UInt32 nSize)
{
    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_Read::  \n"  );
    return;
}

//============================================================================
//
// Function Name: AUDIO_DRIVER_Write
//
// Description:   This function is used to set the ring buffer pointer and size from which data
//                 has to be written.
//
//============================================================================
void AUDIO_DRIVER_Write(AUDIO_DRIVER_HANDLE_t drv_handle,
                   UInt8* pBuf,
                   UInt32 nBufSize)
{
    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_Write::  \n"  );

    return;
}
//============================================================================
//
// Function Name: AUDIO_DRIVER_Ctrl
//
// Description:   This function is used to send a control command to the driver
//
//============================================================================
void AUDIO_DRIVER_Ctrl(AUDIO_DRIVER_HANDLE_t drv_handle,
                       AUDIO_DRIVER_CTRL_t ctrl_cmd,
                       void* pCtrlStruct)
{
    AUDIO_DDRIVER_t*  aud_drv = (AUDIO_DDRIVER_t*)drv_handle;
    Result_t result_code = RESULT_ERROR;

    if(aud_drv == NULL)
    {
        Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_Ctrl::Invalid Handle  \n"  );
        return;
    }

    result_code = AUDIO_DRIVER_ProcessCommonCmd(aud_drv,ctrl_cmd,pCtrlStruct);
    // if the common processing has done the processing return else do specific processing
    if(result_code == RESULT_OK)
    {
        return;
    }

    switch (aud_drv->drv_type)
    {
        case AUDIO_DRIVER_PLAY_VOICE:
			{
                result_code =  AUDIO_DRIVER_ProcessVoiceRenderCmd(aud_drv,ctrl_cmd,pCtrlStruct);
            }
			break;
        case AUDIO_DRIVER_PLAY_AUDIO:
        case AUDIO_DRIVER_PLAY_RINGER:
            {
                result_code =  AUDIO_DRIVER_ProcessRenderCmd(aud_drv,ctrl_cmd,pCtrlStruct);
            }
            break;
        case AUDIO_DRIVER_CAPT_HQ:
            {
                result_code =  AUDIO_DRIVER_ProcessCaptureCmd(aud_drv,ctrl_cmd,pCtrlStruct);
            }
            break;
        case AUDIO_DRIVER_CAPT_VOICE:
            {
                result_code =  AUDIO_DRIVER_ProcessCaptureVoiceCmd(aud_drv,ctrl_cmd,pCtrlStruct);
            }
            break;

	 case AUDIO_DRIVER_VOIP:
            { 
				result_code =  AUDIO_DRIVER_ProcessVoIPCmd(aud_drv,ctrl_cmd,pCtrlStruct);
		 	}
		 	break;
	 case AUDIO_DRIVER_VOIF:
            { 
				result_code =  AUDIO_DRIVER_ProcessVoIFCmd(aud_drv,ctrl_cmd,pCtrlStruct);
		 	}
		 	break;
        default:
            Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_Ctrl::Unsupported driver  \n"  );
            break;
    }

    if(result_code == RESULT_ERROR)
    {
        Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_Ctrl::command processing failed aud_drv->drv_type %d ctrl_cmd %d \n",aud_drv->drv_type,ctrl_cmd);
    }
    return;
}
//============================================================================
//
// Function Name: AUDIO_DRIVER_UpdateBuffer
//
// Description:   This function is used to update the buffer indexes
//
//============================================================================
void AUDIO_DRIVER_UpdateBuffer (AUDIO_DRIVER_HANDLE_t drv_handle,
                                UInt8* pBuf,
                                UInt32 nBufSize,
                                UInt32 nCurrentIndex,
                                UInt32 nSize)
{
    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_UpdateBuffer::  \n"  );
    return;
}
//=============================================================================
// Private function definitions
//=============================================================================
//============================================================================
//
// Function Name: AUDIO_DRIVER_ProcessRenderCmd
//
// Description:   This function is used to process render control commands
//
//============================================================================

static Result_t AUDIO_DRIVER_ProcessRenderCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct)
{
    Result_t result_code = RESULT_ERROR;
	AUDIO_SINK_Enum_t *dev = NULL;
	//Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessRenderCmd::%d \n",ctrl_cmd );
    switch (ctrl_cmd)
    {
        case AUDIO_DRIVER_START:
            {
                UInt32 block_size;
                UInt32 num_blocks;
						
				if(pCtrlStruct != NULL)
			    	dev = (AUDIO_SINK_Enum_t *)pCtrlStruct;
				else
					return RESULT_ERROR;
                //check if callback is already set or not
                if( (aud_drv->pCallback == NULL) ||
                    (aud_drv->interrupt_period == 0) ||
                    (aud_drv->sample_rate == 0) ||
                    (aud_drv->num_channel == 0) ||
                    (aud_drv->bits_per_sample == 0) ||
                    (aud_drv->ring_buffer == NULL) ||
                    (aud_drv->ring_buffer_size == 0)
                    )

                {
                    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessRenderCmd::All Configuration is not set yet  \n"  );
                    return result_code;
                }
				aud_drv->stream_id = csl_audio_render_init (CSL_CAPH_DEV_MEMORY, getDeviceFromSink(*dev));
				SetPlaybackStreamHandle(aud_drv);//save the driver handle after ID is assigned
                /* Block size = (smaples per ms) * (number of channeles) * (bytes per sample) * (interrupt period in ms) 
		                 * Number of blocks = buffer size/block size
              		   *
                 		*/
                //((aud_drv->sample_rate/1000) * (aud_drv->num_channel) * 2 * (aud_drv->interrupt_period));  **period_size comes directly
                block_size = aud_drv->interrupt_period;
				num_blocks = 2; //limitation for RHEA

                // configure the render driver before starting
                result_code = csl_audio_render_configure ( aud_drv->sample_rate, 
									                      aud_drv->num_channel,
                			                	          aud_drv->bits_per_sample,
									                      (UInt8 *)aud_drv->ring_buffer_phy_addr,
									                      num_blocks,
						            			          block_size,
						                      			  (CSL_AUDRENDER_CB) AUDIO_DRIVER_RenderDmaCallback,
                                        			      aud_drv->stream_id);
				csl_caph_arm2sp_set_param((UInt32)aud_drv->arm2sp_config.mixMode,(aud_drv->arm2sp_config.instanceID));
                //start render
                result_code = AUDCTRL_StartRender (aud_drv->stream_id);
            }
            break;
        case AUDIO_DRIVER_STOP:
            {
                //stop render
                result_code = AUDCTRL_StopRender (aud_drv->stream_id);
				/* de-init during stop itself as the sequence is open->start->stop->start in android */
                csl_audio_render_deinit (aud_drv->stream_id);
				ResetPlaybackStreamHandle(aud_drv->stream_id);
            }
            break;
        case AUDIO_DRIVER_PAUSE:
            {
                //pause render
                result_code = csl_audio_render_pause (aud_drv->stream_id);
            }
            break;
        case AUDIO_DRIVER_RESUME:
            {
                //resume render
                result_code = csl_audio_render_resume (aud_drv->stream_id);
            }
            break;
        default:
            Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessRenderCmd::Unsupported command  \n"  );
            break;
    }

    return result_code;
}

//============================================================================
//
// Function Name: AUDIO_DRIVER_ProcessVoiceRenderCmd
//
// Description:   This function is used to process voice render control commands
//
//============================================================================

static Result_t AUDIO_DRIVER_ProcessVoiceRenderCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct)
{
	Result_t result_code = RESULT_ERROR;
#if defined(CONFIG_BCM_MODEM) 
	CSL_ARM2SP_VOICE_MIX_MODE_t mixMode;
	UInt32 numFramesPerInterrupt;
	
	Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessVoiceRenderCmd::%d \n",ctrl_cmd );
	switch (ctrl_cmd)
	{
		  case AUDIO_DRIVER_START:
		  {
			  //check if callback is already set or not
			  if( (aud_drv->pCallback == NULL) ||
				  (aud_drv->interrupt_period == 0) ||
				  (aud_drv->sample_rate == 0) ||
				  (aud_drv->num_channel == 0) ||
				  (aud_drv->bits_per_sample == 0) ||
				  (aud_drv->ring_buffer == NULL) ||
				  (aud_drv->ring_buffer_size == 0)
				  )
			  {
				  Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessVoiceRenderCmd::All Configuration is not set yet	\n"  );
				  return result_code;
			  }

			  audio_voice_driver[aud_drv->arm2sp_config.instanceID] =  aud_drv;

			  aud_drv->num_periods = aud_drv->ring_buffer_size/aud_drv->interrupt_period;

			  mixMode = aud_drv->arm2sp_config.mixMode;
	  
			  numFramesPerInterrupt = 4; // use default value

			  //equal to half of sizeof(shared_Arm2SP_InBuf): 4 frames, for Narrow band,160 words *4 = 1280 bytes.
			  aud_drv->bufferSize_inBytes = (csl_dsp_arm2sp_get_size(AUDIO_SAMPLING_RATE_8000)/8)*numFramesPerInterrupt;
				  
			  if(aud_drv->sample_rate == AUDIO_SAMPLING_RATE_16000)
			  {
					aud_drv->bufferSize_inBytes *= 2;
			  }     
			  
			  // Based on the mix mode, decide the playback mode as well
			  if(mixMode == CSL_ARM2SP_VOICE_MIX_DL)
			  		aud_drv->arm2sp_config.playbackMode = CSL_ARM2SP_PLAYBACK_DL;
			  else if(mixMode == CSL_ARM2SP_VOICE_MIX_UL)
			  		aud_drv->arm2sp_config.playbackMode = CSL_ARM2SP_PLAYBACK_UL;
			  else if(mixMode == CSL_ARM2SP_VOICE_MIX_BOTH)
			  		aud_drv->arm2sp_config.playbackMode = CSL_ARM2SP_PLAYBACK_BOTH;
			  else if(mixMode == CSL_ARM2SP_VOICE_MIX_NONE)
			  		aud_drv->arm2sp_config.playbackMode = CSL_ARM2SP_PLAYBACK_DL; //for standalone testing
				
			  aud_drv->arm2sp_config.audMode = (aud_drv->num_channel == AUDIO_CHANNEL_STEREO)? 1 : 0; 
			 
			  //start render
			  result_code = ARM2SP_play_start(aud_drv,
											 numFramesPerInterrupt); 											

			  //voice render shares the audio mode with voice call.
		  }
		  break;
		  case AUDIO_DRIVER_STOP:
		  {
			  //stop render			  
			  if (aud_drv->arm2sp_config.instanceID == VORENDER_ARM2SP_INSTANCE1)
			  { 			  
                  csl_arm2sp_set_arm2sp((UInt32) aud_drv->sample_rate, 
                                        CSL_ARM2SP_PLAYBACK_NONE, 
                                        (CSL_ARM2SP_VOICE_MIX_MODE_t)aud_drv->arm2sp_config.mixMode, 
                                        aud_drv->arm2sp_config.numFramesPerInterrupt, 
                                        aud_drv->arm2sp_config.audMode, 0 ); 

			  }
			  else 
			  if (aud_drv->arm2sp_config.instanceID == VORENDER_ARM2SP_INSTANCE2)
			  { 				  
                  csl_arm2sp_set_arm2sp2((UInt32) aud_drv->sample_rate, 
                                        CSL_ARM2SP_PLAYBACK_NONE, 
                                        (CSL_ARM2SP_VOICE_MIX_MODE_t)aud_drv->arm2sp_config.mixMode, 
                                        aud_drv->arm2sp_config.numFramesPerInterrupt, 
                                        aud_drv->arm2sp_config.audMode, 0 );			  
              }
			  index = 1; //reset
			  endOfBuffer = FALSE;
			  /*de-init during stop as the android sequence is open->start->stop->start */
			  audio_voice_driver[aud_drv->arm2sp_config.instanceID] = NULL;
		  }
		  break;
		  case AUDIO_DRIVER_PAUSE:
          {
               //pause render
			   if (aud_drv->arm2sp_config.instanceID == VORENDER_ARM2SP_INSTANCE1)
                   csl_arm2sp_set_arm2sp((UInt32) aud_drv->sample_rate, 
                                        CSL_ARM2SP_PLAYBACK_NONE, 
                                        (CSL_ARM2SP_VOICE_MIX_MODE_t)aud_drv->arm2sp_config.mixMode, 
                                        aud_drv->arm2sp_config.numFramesPerInterrupt, 
                                        aud_drv->arm2sp_config.audMode, 1 );
			   else
			   if (aud_drv->arm2sp_config.instanceID == VORENDER_ARM2SP_INSTANCE2)
                   csl_arm2sp_set_arm2sp2((UInt32) aud_drv->sample_rate, 
                                        CSL_ARM2SP_PLAYBACK_NONE, 
                                        (CSL_ARM2SP_VOICE_MIX_MODE_t)aud_drv->arm2sp_config.mixMode, 
                                        aud_drv->arm2sp_config.numFramesPerInterrupt, 
                                        aud_drv->arm2sp_config.audMode, 1 );

			   break;
          }
          break;
          case AUDIO_DRIVER_RESUME:
          {
               //resume render
               result_code = ARM2SP_play_resume (aud_drv);
               
          }
          break;
		  default:
			  Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessVoiceRenderCmd::Unsupported command  \n"	);
			  break;
	  }
#else
	Log_DebugPrintf(LOGID_AUDIO, "AUDIO_DRIVER_ProcessCaptureCmd : dummy for AP only, NO DSP (ARM2SP is not supported)");
#endif	
	  return result_code;

}
//============================================================================
//
// Function Name: AUDIO_DRIVER_ProcessCaptureCmd
//
// Description:   This function is used to process render control commands
//
//============================================================================

static Result_t AUDIO_DRIVER_ProcessCaptureCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct)
{
    Result_t result_code = RESULT_ERROR;
	AUDIO_SOURCE_Enum_t *dev = NULL;

    switch (ctrl_cmd)
    {
        case AUDIO_DRIVER_START:
            {
                UInt32 block_size;
                UInt32 num_blocks;
				if(pCtrlStruct != NULL)
					dev = (AUDIO_SOURCE_Enum_t *)pCtrlStruct;
				else
					return RESULT_ERROR;
                //check if callback is already set or not
                if( (aud_drv->pCallback == NULL) ||
                    (aud_drv->interrupt_period == 0) ||
                    (aud_drv->sample_rate == 0) ||
                    (aud_drv->num_channel == 0) ||
                    (aud_drv->bits_per_sample == 0) ||
                    (aud_drv->ring_buffer == NULL) ||
                    (aud_drv->ring_buffer_size == 0)
                    )

                {
                    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessCaptureCmd::All Configuration is not set yet  \n"  );
                    return result_code;
                }
                aud_drv->stream_id = csl_audio_capture_init ( getDeviceFromSrc(*dev),CSL_CAPH_DEV_MEMORY);
                audio_capture_driver = aud_drv;
                /* Block size = (smaples per ms) * (number of channeles) * (bytes per sample) * (interrupt period in ms) 
				* Number of blocks = buffer size/block size
				*
				*/
                block_size = aud_drv->interrupt_period;
				num_blocks = 2; //limitation for RHEA

                // configure the render driver before starting
                result_code = csl_audio_capture_configure ( aud_drv->sample_rate, 
						                      aud_drv->num_channel,
                                              			      aud_drv->bits_per_sample,
						                      (UInt8 *)aud_drv->ring_buffer_phy_addr,
						                      num_blocks,
						                      block_size,
						                      (CSL_AUDCAPTURE_CB) AUDIO_DRIVER_CaptureDmaCallback,
                                              			      aud_drv->stream_id);

                //start capture
                result_code = AUDCTRL_StartCapture (aud_drv->stream_id);
            }
            break;
        case AUDIO_DRIVER_STOP:
            {
                //stop capture
                result_code = AUDCTRL_StopCapture (aud_drv->stream_id);
				/*de-init as the sequence is open->start->stop->start in android */
                csl_audio_capture_deinit (aud_drv->stream_id);
                audio_capture_driver = NULL;
            }
            break;
        case AUDIO_DRIVER_PAUSE:
            {
                //pause capture
                result_code = csl_audio_capture_pause (aud_drv->stream_id);
            }
            break;
        case AUDIO_DRIVER_RESUME:
            {
                //resume capture
                result_code = csl_audio_capture_resume (aud_drv->stream_id);
            }
            break;
        default:
            Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessCaptureCmd::Unsupported command  \n"  );
            break;
    }


    return result_code;
}

//============================================================================
//
// Function Name: AUDIO_DRIVER_ProcessCaptureVoiceCmd
//
// Description:   This function is used to process render control commands
//
//============================================================================

static Result_t AUDIO_DRIVER_ProcessCaptureVoiceCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct)
{
    Result_t result_code = RESULT_ERROR;
#if defined(CONFIG_BCM_MODEM) 
	VOCAPTURE_RECORD_MODE_t *recordMode = NULL;
	
    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessCaptureVoiceCmd::%d \n",ctrl_cmd );

    switch (ctrl_cmd)
    {
        case AUDIO_DRIVER_START:
            {
                UInt32 block_size;
                UInt32 frame_size;
                UInt32 num_frames;
				UInt32 speech_mode = CSL_VP_SPEECH_MODE_LINEAR_PCM_8K;

				if(pCtrlStruct != NULL)
					  recordMode = (VOCAPTURE_RECORD_MODE_t *)pCtrlStruct;
				else
					return RESULT_ERROR;
					  
				//check if callback is already set or not
                if( (aud_drv->pCallback == NULL) ||
                    (aud_drv->interrupt_period == 0) ||
                    (aud_drv->sample_rate == 0) ||
                    (aud_drv->num_channel == 0) ||
                    (aud_drv->bits_per_sample == 0) ||
                    (aud_drv->ring_buffer == NULL) ||
                    (aud_drv->ring_buffer_size == 0)
                    )

                {
                    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessCaptureCmd::All Configuration is not set yet  \n"  );
                    return result_code;
                }

        		/* **CAUTION: Check if we need to hardcode number of frames and handle the interrupt period seperately
		                * Block size = interrupt_period
              		  * Number of frames/block = interrupt_period / 320 (20ms worth of 8khz data)
                 		  *
                 		*/

                frame_size = (aud_drv->sample_rate/1000) * 20 * 2; 
                
                block_size = aud_drv->interrupt_period;
			    num_frames = (block_size/frame_size);

				aud_drv->num_periods = aud_drv->ring_buffer_size/aud_drv->interrupt_period;

                if(aud_drv->sample_rate == 16000)
				    speech_mode = CSL_VP_SPEECH_MODE_LINEAR_PCM_16K;


                // update num_frames and frame_size
                aud_drv->voicecapt_config.num_frames 				= num_frames;
                aud_drv->voicecapt_config.frame_size 				= frame_size;
                aud_drv->voicecapt_config.speech_mode 				= speech_mode;
			
				if(*recordMode == VOCAPTURE_RECORD_NONE)
					*recordMode = VOCAPTURE_RECORD_UL; //default capture mode

        		aud_drv->voicecapt_config.recordMode = *recordMode;
                audio_capture_driver = aud_drv;
		   
      		    result_code = VPU_record_start (*recordMode,
								aud_drv->sample_rate,
								speech_mode, 
								0, // used by AMRNB and AMRWB
								0,
								0,
								num_frames);

	   		   //voice render shares the audio mode with voice call.
            }
            break;
        case AUDIO_DRIVER_STOP:
            {
                //stop capture
                VPRIPCMDQ_CancelRecording();
				result_code = RESULT_OK;
				index = 1; //reset
				endOfBuffer = FALSE;

				/* de-init during stop as the android sequence is open->start->stop->start */
				audio_capture_driver = NULL;
            }
            break;
        case AUDIO_DRIVER_PAUSE:
            break;
        case AUDIO_DRIVER_RESUME:
            break;
        default:
            Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessCaptureVoiceCmd::Unsupported command  \n"  );
            break;
    }
#else
	Log_DebugPrintf(LOGID_AUDIO, "AUDIO_DRIVER_ProcessCaptureVoiceCmd : dummy for AP only");
#endif
    return result_code;
}


//============================================================================
//
// Function Name: AUDIO_DRIVER_ProcessVoIPCmd
//
// Description:   This function is used to process VoIP commands
//
//============================================================================

static Result_t AUDIO_DRIVER_ProcessVoIPCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct)
{
    Result_t result_code = RESULT_ERROR;
	UInt32 codec_type = 99; // Assign to an invalid number, current valid number:0~5
	UInt32 bitrate_index = 0;
	
    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessVoIPCmd::%d \n",ctrl_cmd );

    switch (ctrl_cmd)
    {
        case AUDIO_DRIVER_START:
            {     

				if(pCtrlStruct != NULL)
				{
					codec_type = ((voip_data_t *)pCtrlStruct)->codec_type;
					bitrate_index = ((voip_data_t *)pCtrlStruct)->bitrate_index;
				}
					
				if(codec_type == 0)
					aud_drv->voip_config.codec_type = VOIP_PCM;
				else if(codec_type == 1) 
					aud_drv->voip_config.codec_type = VOIP_FR; 
				else if(codec_type == 2) 
					aud_drv->voip_config.codec_type = VOIP_AMR475;
				else if(codec_type == 3) 
					aud_drv->voip_config.codec_type = VOIP_G711_U; 
				else if(codec_type == 4) 
					aud_drv->voip_config.codec_type = VOIP_PCM_16K;
				else if(codec_type == 5) 
					aud_drv->voip_config.codec_type = VOIP_AMR_WB_MODE_7k;
				else
				{
					Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessVOIPCmd::Codec Type not supported\n" );
					break;
				}

				aud_drv->voip_config.codec_type += (bitrate_index<<8);
				aud_drv->tmp_buffer = (UInt16 *) kzalloc(VOIP_MAX_FRAME_LEN, GFP_KERNEL);

				if(aud_drv->tmp_buffer == NULL)
					break;
				else
					memset(aud_drv->tmp_buffer,0,VOIP_MAX_FRAME_LEN);

#ifdef VOLTE_SUPPORT
				// VoLTE call
				inVoLTECall = ((voip_data_t *)pCtrlStruct)->isVoLTE;
				if(inVoLTECall)
				{
					if ((aud_drv->voip_config.codec_type & VOIP_AMR475) ||
						(aud_drv->voip_config.codec_type & VOIP_AMR_WB_MODE_7k)) 
					{
						if (djbBuf == NULL)
							djbBuf = (DJB_InputFrame *) kzalloc(sizeof(DJB_InputFrame), GFP_KERNEL);

						DJB_Init();
						Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessVOIPCmd::VoLTE call starts, codec_type=%x\n",aud_drv->voip_config.codec_type );
					}
					else
					{
						Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessVOIPCmd::Codec Type not supported in VoLTE\n" );
						return result_code;
					}
				}
#endif

				VoIP_StartTelephony();
				result_code = RESULT_OK;
			}
            break;
        case AUDIO_DRIVER_STOP:
            {
				VoIP_StopTelephony();
				result_code = RESULT_OK;
            }
            break;
        case AUDIO_DRIVER_SET_VOIP_UL_CB:
            {

				AUDIO_DRIVER_CallBackParams_t *pCbParams;
                if(pCtrlStruct == NULL)
                {
                    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessVOIPCmd::Invalid Ptr  \n"  );
                    return result_code;
                }
                //assign the call back
                pCbParams = (AUDIO_DRIVER_CallBackParams_t *)pCtrlStruct;
                aud_drv->voip_config.pVoipULCallback = pCbParams->voipULCallback;
				aud_drv->voip_config.pVoIPCBPrivate = pCbParams->pPrivateData;		
                result_code = RESULT_OK;
		    }
            break;
        case AUDIO_DRIVER_SET_VOIP_DL_CB:
            {				
				AUDIO_DRIVER_CallBackParams_t *pCbParams;
                if(pCtrlStruct == NULL)
                {
                    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessVOIPCmd::Invalid Ptr  \n"  );
                    return result_code;
                }
                //assign the call back
                pCbParams = (AUDIO_DRIVER_CallBackParams_t *)pCtrlStruct;
                aud_drv->voip_config.pVoipDLCallback = pCbParams->voipDLCallback;
				aud_drv->voip_config.pVoIPCBPrivate = pCbParams->pPrivateData;
                result_code = RESULT_OK;
		    }
            break;
        default:
            Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessVoIPCmd::Unsupported command  \n"  );
            break;
    }

    return result_code;
}

//============================================================================
//
// Function Name: AUDIO_DRIVER_ProcessVoIFCmd
//
// Description:   This function is used to process VoIP commands
//
//============================================================================

static Result_t AUDIO_DRIVER_ProcessVoIFCmd(AUDIO_DDRIVER_t* aud_drv,
											AUDIO_DRIVER_CTRL_t ctrl_cmd,
											void* pCtrlStruct)
{
	Result_t result_code = RESULT_ERROR;
#if defined(CONFIG_BCM_MODEM) 
	switch (ctrl_cmd)
	{
		case AUDIO_DRIVER_START:
			{
				if (voifDrv.isRunning)
					return result_code;

				VPRIPCMDQ_VOIFControl( 1 );
				voif_enabled = TRUE;
				voifDrv.isRunning = TRUE;
				Log_DebugPrintf(LOGID_AUDIO," AUDDRV_VOIF_Start end \r\n");
                result_code = RESULT_OK;
			}
            break;
        case AUDIO_DRIVER_STOP:
            {
				if (voifDrv.isRunning == FALSE)
					return result_code;
				VPRIPCMDQ_VOIFControl( 0 );
				voifDrv.cb = NULL;
				voif_enabled = FALSE;
				voifDrv.isRunning = FALSE;
				Log_DebugPrintf(LOGID_AUDIO,"AUDDRV_VOIF_Stop end \r\n");
                result_code = RESULT_OK;
            }
            break;
        case AUDIO_DRIVER_SET_VOIF_CB:
            {
				voifDrv.cb = (VOIF_CB)pCtrlStruct;
                result_code = RESULT_OK;
            }
            break;
        default:
            Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessVoIFCmd::Unsupported command  \n"  );
            break;
    }
#else
	Log_DebugPrintf(LOGID_AUDIO, "AUDIO_DRIVER_ProcessVoIFCmd : dummy for AP only (no DSP)");
#endif
    return result_code;
}

//============================================================================
//
// Function Name: AUDIO_DRIVER_ProcessCommonCmd
//
// Description:   This function is used to process common control commands
//
//============================================================================

static Result_t AUDIO_DRIVER_ProcessCommonCmd(AUDIO_DDRIVER_t* aud_drv,
                                          AUDIO_DRIVER_CTRL_t ctrl_cmd,
                                          void* pCtrlStruct)
{
    Result_t result_code = RESULT_ERROR;

    //Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessCommonCmd::%d \n",ctrl_cmd );

    switch (ctrl_cmd)
    {
        case AUDIO_DRIVER_CONFIG:
            {
                AUDIO_DRIVER_CONFIG_t* pAudioConfig = (AUDIO_DRIVER_CONFIG_t*)pCtrlStruct;
                if(pCtrlStruct == NULL)
                {
                    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessCommonCmd::Invalid Ptr  \n"  );
                    return result_code;
                }
                aud_drv->sample_rate = pAudioConfig->sample_rate;
                aud_drv->num_channel = pAudioConfig->num_channel;
                aud_drv->bits_per_sample = pAudioConfig->bits_per_sample;
				aud_drv->arm2sp_config.instanceID = pAudioConfig->instanceId; // to decide on ARM2SP1 or ARM2SP2
				aud_drv->arm2sp_config.mixMode = pAudioConfig->arm2sp_mixMode;
                result_code = RESULT_OK;
            }
            break;

        case AUDIO_DRIVER_SET_CB:
            {
				AUDIO_DRIVER_CallBackParams_t *pCbParams;
                if(pCtrlStruct == NULL)
                {
                    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessCommonCmd::Invalid Ptr  \n"  );
                    return result_code;
                }
                //assign the call back
                pCbParams = (AUDIO_DRIVER_CallBackParams_t *)pCtrlStruct;
                aud_drv->pCallback = pCbParams->pfCallBack;
				aud_drv->pCBPrivate = pCbParams->pPrivateData;
                result_code = RESULT_OK;
            }
            break;
        case AUDIO_DRIVER_SET_INT_PERIOD:
            {
                if(pCtrlStruct == NULL)
                {
                    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessCommonCmd::Invalid Ptr  \n"  );
                    return result_code;
                }
                aud_drv->interrupt_period = *((UInt32*)pCtrlStruct);
                result_code = RESULT_OK;
            }
            break;
        case AUDIO_DRIVER_SET_BUF_PARAMS:
            {
                AUDIO_DRIVER_BUFFER_t* pAudioBuffer = (AUDIO_DRIVER_BUFFER_t*)pCtrlStruct;
                if(pCtrlStruct == NULL)
                {
                    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessCommonCmd::Invalid Ptr  \n"  );
                    return result_code;
                }

                //update the buffer pointer and size parameters
                aud_drv->ring_buffer = pAudioBuffer->pBuf;
                aud_drv->ring_buffer_size = pAudioBuffer->buf_size;
                aud_drv->ring_buffer_phy_addr = pAudioBuffer->phy_addr;
                result_code = RESULT_OK;
            }
            break;
        case AUDIO_DRIVER_GET_DRV_TYPE:
            {
                AUDIO_DRIVER_TYPE_t* pDriverType = (AUDIO_DRIVER_TYPE_t*)pCtrlStruct;
                if(pCtrlStruct == NULL)
                {
                    Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_ProcessCommonCmd::Invalid Ptr  \n"  );
                    return result_code;
                }

                //update the buffer pointer and size parameters
                *pDriverType = aud_drv->drv_type;
                result_code = RESULT_OK;
            }
            break;
            
        default:
            break;
    }

    return result_code;
}
// ==========================================================================
//
// Function Name: ARM2SP_play_start
//
// Description: Start the data transfer of ARM2SP play
//
// =========================================================================

static Result_t ARM2SP_play_start (AUDIO_DDRIVER_t* aud_drv,
										UInt32	numFramesPerInterrupt)
{
	// restrict numFramesPerInterrupt due to the shared memory size 
	if (aud_drv->sample_rate == AUDIO_SAMPLING_RATE_8000 && numFramesPerInterrupt > 4)
		aud_drv->arm2sp_config.numFramesPerInterrupt = 4;

	if (aud_drv->sample_rate == AUDIO_SAMPLING_RATE_16000 && numFramesPerInterrupt > 2)
		aud_drv->arm2sp_config.numFramesPerInterrupt = 2;

	if (aud_drv->sample_rate == AUDIO_SAMPLING_RATE_48000)
	{
			aud_drv->arm2sp_config.numFramesPerInterrupt = 1; //For 48K ARM2SP, dsp only supports 2*20ms ping-pong buffer, stereo or mono
	}
						
	Log_DebugPrintf(LOGID_AUDIO, " ARM2SP_play_start::Start render, playbackMode = %d,  mixMode = %d, instanceID=0x%lx samplingRate = %ld\n", 
						aud_drv->arm2sp_config.playbackMode, aud_drv->arm2sp_config.mixMode, aud_drv->arm2sp_config.instanceID,aud_drv->sample_rate);

	if (aud_drv->arm2sp_config.instanceID == VORENDER_ARM2SP_INSTANCE1)
	{
		CSL_ARM2SP_Init();	// clean buffer before starting to play
	
		Log_DebugPrintf(LOGID_AUDIO, " start ARM2SP\n");
        csl_arm2sp_set_arm2sp((UInt32) aud_drv->sample_rate,
								(CSL_ARM2SP_PLAYBACK_MODE_t)aud_drv->arm2sp_config.playbackMode,
								(CSL_ARM2SP_VOICE_MIX_MODE_t)aud_drv->arm2sp_config.mixMode, 
                                aud_drv->arm2sp_config.numFramesPerInterrupt,
								aud_drv->arm2sp_config.audMode, 0 );
	}
	else if (aud_drv->arm2sp_config.instanceID == VORENDER_ARM2SP_INSTANCE2)
	{
		CSL_ARM2SP2_Init(); // clean buffer before starting to play
	
		Log_DebugPrintf(LOGID_AUDIO, " start ARM2SP2\n");
		csl_arm2sp_set_arm2sp2((UInt32) aud_drv->sample_rate,
								(CSL_ARM2SP_PLAYBACK_MODE_t)aud_drv->arm2sp_config.playbackMode,
								(CSL_ARM2SP_VOICE_MIX_MODE_t)aud_drv->arm2sp_config.mixMode, 
                                aud_drv->arm2sp_config.numFramesPerInterrupt,
								aud_drv->arm2sp_config.audMode, 0 );
	}

	return RESULT_OK;
}

// ==========================================================================
//
// Function Name: ARM2SP_play_resume
//
// Description: Resume the ARM2SP playback
//
// =========================================================================

static Result_t ARM2SP_play_resume (AUDIO_DDRIVER_t* aud_drv)
{
#if defined(CONFIG_BCM_MODEM)
	Log_DebugPrintf(LOGID_AUDIO, "Resume ARM2SP voice play instanceID=0x%lx \n", aud_drv->arm2sp_config.instanceID);

	if (aud_drv->arm2sp_config.instanceID  == VORENDER_ARM2SP_INSTANCE1)
		csl_arm2sp_set_arm2sp((UInt32) aud_drv->sample_rate, 
                              (CSL_ARM2SP_PLAYBACK_MODE_t)aud_drv->arm2sp_config.playbackMode, 
                              (CSL_ARM2SP_VOICE_MIX_MODE_t)aud_drv->arm2sp_config.mixMode, 
                              aud_drv->arm2sp_config.numFramesPerInterrupt, 
                              aud_drv->arm2sp_config.audMode, 1 ); 

	else if (aud_drv->arm2sp_config.instanceID  == VORENDER_ARM2SP_INSTANCE2)	
		csl_arm2sp_set_arm2sp2((UInt32) aud_drv->sample_rate, 
                              (CSL_ARM2SP_PLAYBACK_MODE_t)aud_drv->arm2sp_config.playbackMode, 
                              (CSL_ARM2SP_VOICE_MIX_MODE_t)aud_drv->arm2sp_config.mixMode, 
                              aud_drv->arm2sp_config.numFramesPerInterrupt, 
                              aud_drv->arm2sp_config.audMode, 1 ); 
#else
	Log_DebugPrintf(LOGID_AUDIO, "ARM2SP_play_resume  : dummy for AP only");
#endif
	return RESULT_OK;
}

// ==========================================================================
//
// Function Name: VPU_record_start
//
// Description: Start the data transfer of VPU record
//
// =========================================================================
static Result_t VPU_record_start ( VOCAPTURE_RECORD_MODE_t	recordMode,
								AUDIO_SAMPLING_RATE_t		samplingRate,
								UInt32						speechMode, // used by AMRNB and AMRWB
								UInt32						dataRate, // used by AMRNB and AMRWB
								Boolean						procEnable,
								Boolean						dtxEnable,
								UInt32						numFramesPerInterrupt)
{
	// [8|7|6..4|3..0] = [audio_proc_enable|AMR2_dtx|vp_speech_mode|vp_amr_mode]
	UInt16 encodingMode = 
			(procEnable << 8) |
			(dtxEnable << 7) |
			(speechMode << 4) |
			(dataRate);
	
	// restrict numFramesPerInterrupt due to the shared memory size 
	if (numFramesPerInterrupt > 4)
			numFramesPerInterrupt = 4;

	Log_DebugPrintf(LOGID_AUDIO, " VPU_record_start::Start capture, encodingMode = 0x%x, recordMode = 0x%x, procEnable = 0x%x, dtxEnable = 0x%x, speechMode = 0x%lx, dataRate = 0x%lx\n", 
							encodingMode, recordMode, procEnable, dtxEnable, speechMode, dataRate);
#if defined(CONFIG_BCM_MODEM)	
	VPRIPCMDQ_StartCallRecording((UInt8)recordMode, (UInt8)numFramesPerInterrupt, (UInt16)encodingMode);
#else
	Log_DebugPrintf(LOGID_AUDIO, "VPU_record_start  : dummy for AP only");
#endif
	return RESULT_OK;
}

// DSP interrupt handlers

//******************************************************************************
//
// Function Name:  VOIP_ProcessVOIPDLDone()
//
// Description:	This function calls the DL callback
//
// Notes:			
//******************************************************************************

void VoIP_Task_Entry(struct work_struct *work)
{
	VOIP_FillDL_CB(1);
}

//******************************************************************************
//
// Function Name:  VOIP_ProcessVOIPDLDone()
//
// Description:	This function handle the VoIP DL data
//
// Notes:			
//******************************************************************************
void VOIP_ProcessVOIPDLDone(void)
{
    if ( voip_workqueue )
        queue_work(voip_workqueue, &voip_work);               
}

// handle interrupt from DSP of data ready
void VOIF_Buffer_Request (UInt32 bufferIndex, UInt32 samplingRate)
{
#if defined(CONFIG_BCM_MODEM) 
    UInt32 dlIndex;
    Int16   *ulBuf, *dlBuf;
    UInt32 sampleCount = VOIF_8K_SAMPLE_COUNT;

    if (!voif_enabled)
        return;
    ulBuf = CSL_GetULVoIFBuffer();
    dlIndex = bufferIndex & 0x1;

    if (samplingRate)
    {
        sampleCount = VOIF_16K_SAMPLE_COUNT;
    }
    else
    {
        sampleCount = VOIF_8K_SAMPLE_COUNT;
    }
    //Log_DebugPrintf(LOGID_AUDIO,"VOIF_ISR_Handler received VOIF_DATA_READY. dlIndex = %d isCall16K = %d \r\n", dlIndex, samplingRate);

    dlBuf = CSL_GetDLVoIFBuffer(sampleCount, dlIndex);
    if (voifDrv.cb)
            voifDrv.cb (ulBuf, dlBuf, sampleCount, (UInt8)samplingRate);
#else
	Log_DebugPrintf(LOGID_AUDIO, "VOIF_Buffer_Request : dummy for AP only (no DSP)");
#endif
}

//******************************************************************************
//
// Function Name:  VoIP_StartTelephony()
//
// Description:	This function starts full duplex telephony session
//
// Notes:	The full duplex DSP interface is in sharedmem, not vsharedmem.
//		But since its function is closely related to voice processing,
//		we put it here.
//
//******************************************************************************
static Boolean VoIP_StartTelephony(void)
{
	Log_DebugPrintf( LOGID_SOC_AUDIO, "=====VoIP_StartTelephony \r\n");
    voip_workqueue = create_workqueue("voip");
    if (!voip_workqueue)
    {
        return TRUE;
    }

    INIT_WORK(&voip_work, VoIP_Task_Entry);

	VOIP_ProcessVOIPDLDone();
	return TRUE;
}


//******************************************************************************
//
// Function Name:  VoIP_StopTelephony()
//
// Description:	This function stops full duplex telephony session
//
// Notes:	The full duplex DSP interface is in sharedmem, not vsharedmem.
//		But since its function is closely related to voice processing,
//		we put it here.
//
//******************************************************************************
static Boolean VoIP_StopTelephony(void)
{    
	Log_DebugPrintf(LOGID_SOC_AUDIO,"=====VoIP_StopTelephony \r\n");

	// Clear voip mode, which block audio processing for voice calls
	audio_control_dsp( DSPCMD_TYPE_COMMAND_CLEAR_VOIPMODE, 0, 0, 0, 0, 0 ); // arg0 = 0 to clear VOIPmode
    flush_workqueue(voip_workqueue);
    destroy_workqueue(voip_workqueue);

    voip_workqueue = NULL;
	prev_amr_mode = (VP_Mode_AMR_t)0xffff;
#ifdef VOLTE_SUPPORT
	djbTimeStamp = 0;

	if (djbBuf)
	{
		kfree(djbBuf);
		djbBuf = NULL;
	}
	inVoLTECall = FALSE;
#endif
	return TRUE;
}

//============================================================================
//
// Function Name: AUDIO_DRIVER_RenderDmaCallback
//
// Description:   This function processes the callback from the CAPH
//
//============================================================================

static void AUDIO_DRIVER_RenderDmaCallback(UInt32 stream_id)
{
	AUDIO_DDRIVER_t* pAudDrv;
	
	pAudDrv = GetPlaybackStreamHandle(stream_id);

    //Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_RenderDmaCallback:: stream_id = %d\n", stream_id);

    if(( pAudDrv== NULL))
    {
        Log_DebugPrintf(LOGID_AUDIO, "AUDIO_DRIVER_RenderDmaCallback:: Spurious call back\n");
		return;
    }
    if(pAudDrv->pCallback != NULL)
    {
        pAudDrv->pCallback(pAudDrv->pCBPrivate);
    }
    else
        Log_DebugPrintf(LOGID_AUDIO, "AUDIO_DRIVER_RenderDmaCallback:: No callback registerd\n");
    
    return;
}

//============================================================================
//
// Function Name: ARM2SP_Render_Request
//
// Description:   This function processes the callback from the dsp
//
//============================================================================
void ARM2SP_Render_Request(UInt16 buf_index)
{
	Boolean in48K = FALSE;
	AUDIO_DDRIVER_t* pAudDrv;
	UInt8 *pSrc = NULL;
	UInt32 srcIndex,copied_bytes;

	pAudDrv = audio_voice_driver[VORENDER_ARM2SP_INSTANCE1];
	
	pSrc = pAudDrv->ring_buffer;
	srcIndex = pAudDrv->read_index;

	//copy the data from ring buffer to shared memory	
#if defined(CONFIG_BCM_MODEM) 
	copied_bytes = CSL_ARM2SP_Write( (pSrc + srcIndex), pAudDrv->bufferSize_inBytes, buf_index, in48K, pAudDrv->arm2sp_config.audMode );

	srcIndex += copied_bytes;
#endif
	if(srcIndex >= pAudDrv->ring_buffer_size)
	{
		srcIndex -= pAudDrv->ring_buffer_size;
		endOfBuffer = TRUE;
	}
	
	pAudDrv->read_index = srcIndex;
	if((pAudDrv->read_index >= (pAudDrv->interrupt_period * index)) || (endOfBuffer == TRUE))	
	{
		// then send the period elapsed
		if(pAudDrv->pCallback != NULL) //ARM2SP1 instance
    	{
        	pAudDrv->pCallback(pAudDrv->pCBPrivate);			
    	}

		if(index == pAudDrv->num_periods)
		{
			index = 1; //reset back 
			endOfBuffer = FALSE; 
		}
		else
		{
			index++;
		}
	}
	
}

//============================================================================
//
// Function Name: ARM2SP2_Render_Request
//
// Description:   This function processes the callback from the dsp
//
//============================================================================
void ARM2SP2_Render_Request(UInt16 buf_index)
{
	Boolean in48K = FALSE;
	AUDIO_DDRIVER_t* pAudDrv;
	UInt8 *pSrc = NULL;
	UInt32 srcIndex,copied_bytes;
	
	pAudDrv = audio_voice_driver[VORENDER_ARM2SP_INSTANCE2];
		
	pSrc = pAudDrv->ring_buffer;
	srcIndex = pAudDrv->read_index;
		
	//copy the data from ring buffer to shared memory
#if defined(CONFIG_BCM_MODEM) 
	copied_bytes = CSL_ARM2SP_Write( (pSrc + srcIndex), pAudDrv->bufferSize_inBytes, buf_index, in48K, pAudDrv->arm2sp_config.audMode );
	srcIndex += copied_bytes;
#endif	
	if(srcIndex >= pAudDrv->ring_buffer_size)
	{
		srcIndex -= pAudDrv->ring_buffer_size;
		endOfBuffer = TRUE;
	}
		
	pAudDrv->read_index = srcIndex;
	if((pAudDrv->read_index >= (pAudDrv->interrupt_period * index)) || (endOfBuffer == TRUE)) 
	{
		// then send the period elapsed
		if(pAudDrv->pCallback != NULL) //ARM2SP2 instance
		{
			pAudDrv->pCallback(pAudDrv->pCBPrivate);			
		}
	
		if(index == pAudDrv->num_periods)
		{
			index = 1; //reset back 
			endOfBuffer = FALSE; 
		}
		else
		{
			index++;
		}
	}

}


//============================================================================
//
// Function Name: AUDIO_DRIVER_CaptureDmaCallback
//
// Description:   This function processes the callback from the dma
//
//============================================================================

static void AUDIO_DRIVER_CaptureDmaCallback(UInt32 stream_id)
{

    //Log_DebugPrintf(LOGID_AUDIO,"AUDIO_DRIVER_CaptureDmaCallback::\n");


    if((audio_capture_driver == NULL))
    {
        Log_DebugPrintf(LOGID_AUDIO, "AUDIO_DRIVER_CaptureDmaCallback:: Spurious call back\n");
		return;
    }
    if(audio_capture_driver->pCallback != NULL)
    {
        audio_capture_driver->pCallback(audio_capture_driver->pCBPrivate);
    }
    else
        Log_DebugPrintf(LOGID_AUDIO, "AUDIO_DRIVER_CaptureDmaCallback:: No callback registerd\n");
    
    return;
}


//============================================================================
//
// Function Name: VPU_Capture_Request
//
// Description:   This function processes the callback from the dsp for voice recording
//
//============================================================================

void VPU_Capture_Request(UInt16 buf_index)
{
    Int32		dest_index, num_bytes_to_copy;
	UInt8 *	pdest_buf;
    UInt32      recv_size;
    AUDIO_DDRIVER_t* aud_drv;

    aud_drv = audio_capture_driver;

    if((aud_drv == NULL))
    {
        Log_DebugPrintf(LOGID_AUDIO, "VPU_Capture_Request:: Spurious call back\n");
		return;
    }

    //Log_DebugPrintf(LOGID_AUDIO,"VPU_Capture_Request:: buf_index %d aud_drv->write_index = %d \n",buf_index,aud_drv->write_index);

    //Copy the data to the ringbuffer from dsp shared memory
    dest_index = aud_drv->write_index;
    pdest_buf= aud_drv->ring_buffer;
    num_bytes_to_copy = (aud_drv->voicecapt_config.num_frames) * (aud_drv->voicecapt_config.frame_size); 
#if defined(CONFIG_BCM_MODEM) 
    recv_size = CSL_VPU_ReadPCM ( pdest_buf+dest_index, num_bytes_to_copy, buf_index, aud_drv->voicecapt_config.speech_mode);
	
    // update the write index
    dest_index += recv_size;
#endif
	if(dest_index >= aud_drv->ring_buffer_size)
    {  
	   	dest_index -= aud_drv->ring_buffer_size;
  	    endOfBuffer = TRUE;					
	}
	
	aud_drv->write_index = dest_index;
	
	if((dest_index >= (aud_drv->interrupt_period * index)) || (endOfBuffer == TRUE))
	{
		// then send the period elapsed
		
		if(aud_drv->pCallback != NULL) 
		{
			//Log_DebugPrintf(LOGID_AUDIO, "AUDIO_DRIVER_CaptureVoiceCallback::  callback done index %d\n",index);
			aud_drv->pCallback(aud_drv->pCBPrivate);			
		}
		
		if(index == aud_drv->num_periods) 
		{
			endOfBuffer = FALSE; 
			index = 1; //reset back 
		}
		else
		{
			index++;
		}
	}
    return;
}


//******************************************************************************
//
// Function Name:  VoIP_StartMainAMRDecodeEncode()
//
// Description:		This function passes the AMR frame to be decoded
//					from application to DSP and starts its decoding
//					as well as encoding of the next frame.
//
// Notes:			The full duplex DSP interface is in sharedmem, not vsharedmem.
//					But since its function is closely related to voice processing,
//					we put it here.
//
//******************************************************************************
static void VoIP_StartMainAMRDecodeEncode(
	CSL_VP_Mode_AMR_t	decode_amr_mode,	// AMR mode for decoding the next speech frame
	UInt8				*pBuf,		// buffer carrying the AMR speech data to be decoded
	UInt16				length,		// number of bytes of the AMR speech data to be decoded
	CSL_VP_Mode_AMR_t	encode_amr_mode,	// AMR mode for encoding the next speech frame
	Boolean				dtx_mode	// Turn DTX on (TRUE) or off (FALSE)
	)
{
	// decode the next downlink AMR speech data from application
#if defined(CONFIG_BCM_MODEM) 
#ifdef VOLTE_SUPPORT
	if (inVoLTECall)
	{
		encode_amr_mode |= VOLTECALLENABLE;
		VoLTE_WriteDLData((UInt16)decode_amr_mode, (UInt16 *)pBuf);
	}
	else
#endif
		CSL_WriteDLVoIPData((UInt16)decode_amr_mode, (UInt16 *)pBuf);

	// signal DSP to start AMR decoding and encoding
	
	if (prev_amr_mode == 0xffff || prev_amr_mode != encode_amr_mode)
	{
	
		//Log_DebugPrintf(LOGID_SOC_AUDIO, "=====VoIP_StartMainAMRDecodeEncode UL codecType=0x%x, send VP_COMMAND_MAIN_AMR_RUN to DSP", encode_amr_mode);
		prev_amr_mode = encode_amr_mode;
		VPRIPCMDQ_DSP_AMR_RUN((UInt16)encode_amr_mode, telephony_amr_if2, FALSE);
	}
#endif
}


//******************************************************************************
//
// Function Name:  AP_ProcessStatusMainAMRDone()
//
// Description:		This function handles VP_STATUS_MAIN_AMR_DONE from DSP.
//
// Notes:			
//
//******************************************************************************
void AP_ProcessStatusMainAMRDone(UInt16 codecType)
{
#if defined(CONFIG_BCM_MODEM) 
 	static UInt16 Buf[321]; // buffer to hold UL data and codec type

	// encoded uplink AMR speech data now ready in DSP shared memory, copy it to application
	// pBuf is to point the start of the encoded speech data buffer
	
	CSL_ReadULVoIPData(codecType, Buf);
	VOIP_DumpUL_CB((UInt8*)Buf,0);
#endif
}

//============================================================================
//
// Function Name: VOIP_DumpUL_CB
//
// Description:   VoIP UL callback
//
//============================================================================

static Boolean VOIP_DumpUL_CB(
		UInt8		*pSrc,		// pointer to start of speech data
		UInt32		amrMode		// AMR codec mode of speech data
		)
{

    AUDIO_DDRIVER_t* aud_drv;
	UInt8 index = 0;
	CSL_VOIP_Buffer_t *voipBufPtr = NULL;
	UInt16 codecType;
	UInt32 ulSize = 0;
	
    aud_drv = audio_voip_driver;
    if((aud_drv == NULL))
    {
        Log_DebugPrintf(LOGID_AUDIO, "VOIP_DumpUL_CB:: Spurious call back\n");
        return TRUE;
    }
	voipBufPtr = (CSL_VOIP_Buffer_t *)pSrc;
	codecType = voipBufPtr->voip_vocoder;
	index = (codecType & 0xf000) >> 12;
	if (index >= 7)
		Log_DebugPrintf(LOGID_AUDIO, "VOIP_DumpUL_CB :: Invalid codecType = 0x%x\n", codecType);
	else
	{
		//Log_DebugPrintf(LOGID_AUDIO, "VOIP_DumpUL_CB :: codecType = 0x%x, index = %d pSrc 0x%x\n", codecType, index, pSrc);		
		ulSize = sVoIPDataLen[(codecType & 0xf000) >> 12];
	    if(aud_drv->voip_config.pVoipULCallback != NULL)
			aud_drv->voip_config.pVoipULCallback(aud_drv->voip_config.pVoIPCBPrivate, (pSrc + 2),(ulSize - 2) ); 
				
	}    
    return TRUE;
};

//============================================================================
//
// Function Name: VOIP_FillDL_CB
//
// Description:   VoIP DL callback
//
//============================================================================

static Boolean VOIP_FillDL_CB( UInt32 nFrames)
{
    AUDIO_DDRIVER_t* aud_drv;
	UInt32 dlSize = 0;

    aud_drv = audio_voip_driver;
    if(aud_drv == NULL)
    {
        Log_DebugPrintf(LOGID_AUDIO, "VOIP_FillDL_CB:: Spurious call back\n");
        return TRUE;
    }
	dlSize = sVoIPDataLen[(aud_drv->voip_config.codec_type & 0xf000) >> 12];
	memset(aud_drv->tmp_buffer,0,VOIP_MAX_FRAME_LEN);
	aud_drv->tmp_buffer[0] = aud_drv->voip_config.codec_type;
	//Log_DebugPrintf(LOGID_AUDIO, "VOIP_FillDL_CB :: aud_drv->codec_type %d, dlSize = %d...\n", aud_drv->voip_config.codec_type, dlSize);
	aud_drv->voip_config.pVoipDLCallback(aud_drv->voip_config.pVoIPCBPrivate, (UInt8 *)&aud_drv->tmp_buffer[1], (dlSize - 2)); // 2 bytes for codec type
#if defined(CONFIG_BCM_MODEM) 
	VoIP_StartMainAMRDecodeEncode((CSL_VP_Mode_AMR_t)aud_drv->voip_config.codec_type, (UInt8 *)aud_drv->tmp_buffer, dlSize, (CSL_VP_Mode_AMR_t)aud_drv->voip_config.codec_type, FALSE);
#endif
	return TRUE;
};

#ifdef VOLTE_SUPPORT
//============================================================================
//
// Function Name: VoLTE_WriteDLData
//
// Description:   Pass VoLTE DL Data to DSP
//
//============================================================================
static Boolean VoLTE_WriteDLData(UInt16 decode_mode, UInt16 *pBuf)
{
	Boolean isAMRWB = FALSE;
	VOIP_Buffer_t *dlBuf = (VOIP_Buffer_t *)pBuf;
	UInt16 *dataPtr = pBuf;

	if (djbBuf == NULL)
	{
		Log_DebugPrintf(LOGID_AUDIO, "VoLTE_WriteDLData, missing VoLTE init ...\n");
		return FALSE;
	}

	memset(djbBuf,0,sizeof(DJB_InputFrame));
	if (decode_mode >= VOIP_AMR475 && decode_mode < VOIP_G711_U)
		isAMRWB = FALSE;
	else if (decode_mode >= VOIP_AMR_WB_MODE_7k && decode_mode <= VOIP_AMR_WB_MODE_24k)
		isAMRWB = TRUE;
	else
	{
		Log_DebugPrintf(LOGID_AUDIO, "VoLTE_WriteDLData, unsupported codec type.\n");
		return FALSE;
	}
	
	djbTimeStamp += (isAMRWB)? VOLTEWBSTAMPSTEP : VOLTENBSTAMPSTEP;
	djbBuf->RTPTimestamp = djbTimeStamp;
	dataPtr += 3; // Move to data starting address
	djbBuf->pFramePayload = (UInt8 *)dataPtr;
	djbBuf->payloadSize = (UInt16) ((isAMRWB)? (AMR_WB_FRAME_SIZE<<1) : (AMR_FRAME_SIZE<<1)); // In bytes
	djbBuf->frameIndex = 0;
	djbBuf->codecType = (UInt8) ((isAMRWB)? WB_AMR : NB_AMR);
	if (isAMRWB)
	{
		djbBuf->frameType = (UInt8) ((dlBuf->voip_frame).frame_amr_wb.frame_type);
	}
	else
	{
		djbBuf->frameType = (UInt8) ((dlBuf->voip_frame).frame_amr[0]);
	}
	// For silence frame, set quality to 0, otherwise 1
	djbBuf->frameQuality = (djbBuf->frameType & 0x000f)? VOLTEFRAMESILENT : VOLTEFRAMEGOOD;

//	Log_DebugPrintf(LOGID_AUDIO, "VoLTE_WriteDLData, TimeStamp=%d, payloadSize=%d,codecType=%d, quality=%d \n",
//					djbBuf->RTPTimestamp, djbBuf->payloadSize,  djbBuf->codecType, djbBuf->frameQuality);
	DJB_PutFrame(djbBuf);
	return TRUE;
}
#endif
