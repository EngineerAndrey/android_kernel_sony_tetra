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
*
*   @file   bcm_auddrv_test.c
*
*   @brief	This file contains SysFS interface for audio driver test cases
*
****************************************************************************/

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm_params.h>
#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/initval.h>

#include <linux/sysfs.h>

//#include <linux/broadcom/hw_cfg.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>

#include <mach/hardware.h>

#include "mobcom_types.h"
#include "resultcode.h"
#include "audio_consts.h"
#include "chal_types.h"
#include "log.h"

#include "csl_caph.h"
#include "audio_vdriver.h"
#include "audio_controller.h"
#include "audio_ddriver.h"
#include "audio_caph.h"
#include "caph_common.h"
#include "voif_handler.h"

#include "brcm_rdb_sysmap.h"
#include "brcm_rdb_khub_clk_mgr_reg.h"
#include "osqueue.h"
#include "ossemaphore.h"
#include "osheap.h"
#include "msconsts.h"
#include "csl_aud_queue.h"
#include "csl_vpu.h"
#include "csl_arm2sp.h"
#ifdef CONFIG_ARM2SP_PLAYBACK
#include "audio_vdriver_voice_play.h"
#endif
#include "osdal_os.h"

static UInt8 *samplePCM16_inaudiotest = NULL;
static UInt16* record_test_buf = NULL;

UInt8 playback_audiotest[1024000] = {
#ifdef CONFIG_ENABLE_TESTDATA
	#include "pcm_16_48khz_mono.txt"
#else
	0
#endif
};
UInt8 playback_audiotest_srcmixer[165856] = {

#ifdef CONFIG_ENABLE_TESTDATA
	#include "sampleWAV16bit.txt"
#else
	0
#endif
};

#define CONFIG_VOIP_DRIVER_TEST

#define BRCM_AUDDRV_NAME_MAX (15)  //max 15 char for test name
#define BRCM_AUDDRV_TESTVAL  (5)   // max no of arg for each test

#define	PCM_TEST_MAX_PLAYBACK_BUF_BYTES		(100*1024)
#define	PCM_TEST_MAX_CAPTURE_BUF_BYTES		(100*1024)
#define TEST_BUF_SIZE   (512 * 1024)

static int sgBrcm_auddrv_TestValues[BRCM_AUDDRV_TESTVAL];
static char *sgBrcm_auddrv_TestName[]={"Aud_play","Aud_Rec","Aud_control"};

// SysFS interface to test the Audio driver level API
ssize_t Brcm_auddrv_TestSysfs_show(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t Brcm_auddrv_TestSysfs_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t count);
static struct device_attribute Brcm_auddrv_Test_attrib = __ATTR(BrcmAud_DrvTest, 0644,  Brcm_auddrv_TestSysfs_show, Brcm_auddrv_TestSysfs_store);

static int HandleControlCommand(void);

static int HandlePlayCommand(void);
static int HandleCaptCommand(void);
static void AUDIO_DRIVER_TEST_InterruptPeriodCB(void *pPrivate);
static void AUDIO_DRIVER_TEST_CaptInterruptPeriodCB(void *pPrivate);

void dump_audio_registers(void);

#ifdef CONFIG_ARM2SP_PLAYBACK_TEST

static void AUDTST_VoicePlayback(UInt32 Val2, UInt32 Val3, UInt32 Val4, UInt32 Val5, UInt32 Val6);

static Boolean AUDDRV_BUFFER_DONE_CB (UInt8 *buf, UInt32 size, UInt32 streamID)
{
    OSSEMAPHORE_Release (AUDDRV_BufDoneSema);
	return TRUE;
}
#endif

#ifdef CONFIG_VOIP_DRIVER_TEST
static void AUDTST_VoIP(UInt32 Val2, UInt32 Val3, UInt32 Val4, UInt32 Val5, UInt32 Val6);
#endif

static Semaphore_t		AUDDRV_BufDoneSema;

static AUDQUE_Queue_t	*sVtQueue = NULL;

static Semaphore_t		sVtQueue_Sema;
static const UInt16 sVoIPDataLen[] = { 0, 322, 160, 38, 166, 642, 70};

static void AudDrv_VOIP_DumpUL_CB(void *pPrivate, u8	*pSrc, u32 nSize);
static void AudDrv_VOIP_FillDL_CB(void *pPrivate, u8 *pDst, u32 nSize);

//static UInt8 sVoIPAMRSilenceFrame[1] = {0x000f};

// callback for buffer ready of pull mode
static void AudDrv_VOIP_DumpUL_CB (void *pPrivate, u8	*pSrc, u32 nSize)
{
	UInt32 copied = 0;

	copied = AUDQUE_Write (sVtQueue, pSrc, nSize);
	pr_info("\n AudDrv_VOIP_DumpUL_CB UL ready, size = 0x%x, copied = 0x%lx\n",  nSize, copied);
	OSSEMAPHORE_Release (sVtQueue_Sema);
	pr_info(  "\n AudDrv_VOIP_DumpUL_CB UL done \n");
}

static void AudDrv_VOIP_FillDL_CB(void *pPrivate, u8 *pDst, u32 nSize)
{
	UInt32 copied = 0;

	copied = AUDQUE_Read (sVtQueue, pDst, nSize);
	pr_info( "\n VOIP_FillDL_CB DL ready, size =0x%x, copied = 0x%lx\n", nSize, copied);

    OSSEMAPHORE_Release (AUDDRV_BufDoneSema);
}



//+++++++++++++++++++++++++++++++++++++++
//Brcm_auddrv_TestSysfs_show (struct device *dev, struct device_attribute *attr, char *buf)
// Buffer values syntax -	 0 - on/off, 1 - output device, 2 - sample rate index, 3 - channel, 4 -volume,
//
//---------------------------------------------------

ssize_t Brcm_auddrv_TestSysfs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  	int i;
	char sbuf[256];

       snprintf(sbuf, sizeof(sbuf), "%s:",sgBrcm_auddrv_TestName[sgBrcm_auddrv_TestValues[0]]);

	for(i=0; i< sizeof(sgBrcm_auddrv_TestValues)/sizeof(sgBrcm_auddrv_TestValues[0]); i++)
	{
		snprintf(sbuf, sizeof(sbuf),"%d",sgBrcm_auddrv_TestValues[i]);
		strcat(buf, sbuf);
	}
	return strlen(buf);
}

//+++++++++++++++++++++++++++++++++++++++
// Brcm_auddrv_TestSysfs_store (struct device *dev, struct device_attribute *attr, char *buf)
// Buffer values syntax -	 0 - on/off, 1 - output device, 2 - sample rate index, 3 - channel, 4 -volume,
//
//---------------------------------------------------

ssize_t Brcm_auddrv_TestSysfs_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t count)
{

	if(5!= sscanf(buf, "%d %d %d %d %d",&sgBrcm_auddrv_TestValues[0], &sgBrcm_auddrv_TestValues[1], &sgBrcm_auddrv_TestValues[2], &sgBrcm_auddrv_TestValues[3],&sgBrcm_auddrv_TestValues[4]))
	{

		BCM_AUDIO_DEBUG("\n<-Brcm_AudDrv_test SysFS Handler: test type =%s arg1=%d, arg2=%d, arg3=%d, arg4=%d \n", sgBrcm_auddrv_TestName[sgBrcm_auddrv_TestValues[0]],
											sgBrcm_auddrv_TestValues[1],
											sgBrcm_auddrv_TestValues[2],
											sgBrcm_auddrv_TestValues[3],
                                                                             sgBrcm_auddrv_TestValues[4]);
		BCM_AUDIO_DEBUG("error reading buf=%s count=%d\n", buf, count);
		return count;
	}

      switch(sgBrcm_auddrv_TestValues[0])
	{
	case 1: //Aud_play
        {
              BCM_AUDIO_DEBUG("I am in case 1 (Aud_play) -- test type =%s arg1=%d, arg2=%d, arg3=%d, arg4=%d \n", sgBrcm_auddrv_TestName[sgBrcm_auddrv_TestValues[0]-1],
											sgBrcm_auddrv_TestValues[1],
											sgBrcm_auddrv_TestValues[2],
											sgBrcm_auddrv_TestValues[3],
                                                                             sgBrcm_auddrv_TestValues[4]);
              HandlePlayCommand();
    	      break;
        }
       case 2: //Aud_rec
        {
              BCM_AUDIO_DEBUG("I am in case 2 (Aud_Rec) -- test type =%s arg1=%d, arg2=%d, arg3=%d, arg4=%d \n", sgBrcm_auddrv_TestName[sgBrcm_auddrv_TestValues[0]-1],
											sgBrcm_auddrv_TestValues[1],
											sgBrcm_auddrv_TestValues[2],
											sgBrcm_auddrv_TestValues[3],
                                                                             sgBrcm_auddrv_TestValues[4]);
             HandleCaptCommand();
             break;
        }
       case 3: //Aud_control
        {
             BCM_AUDIO_DEBUG("I am in case 3 (Aud_Control) -- test type =%s arg1=%d, arg2=%d, arg3=%d, arg4=%d \n", sgBrcm_auddrv_TestName[sgBrcm_auddrv_TestValues[0]-1],
											sgBrcm_auddrv_TestValues[1],
											sgBrcm_auddrv_TestValues[2],
											sgBrcm_auddrv_TestValues[3],
                                                                             sgBrcm_auddrv_TestValues[4]);
             HandleControlCommand();
             break;
        }
      default:
             BCM_AUDIO_DEBUG(" I am in Default case\n");
	}
	return count;
}

int BrcmCreateAuddrv_testSysFs(struct snd_card *card)
{
	int ret = 0;
	//create sysfs file for Aud Driver test control
	ret = snd_add_device_sysfs_file(SNDRV_DEVICE_TYPE_CONTROL,card,-1,&Brcm_auddrv_Test_attrib);
	//	BCM_AUDIO_DEBUG("BrcmCreateControlSysFs ret=%d", ret);
	return ret;
}



static int HandleControlCommand()
{
    AUDIO_SINK_Enum_t	spkr;
    AUDIO_SOURCE_Enum_t		mic;

    switch(sgBrcm_auddrv_TestValues[1])
    {
        case 1:// Initialize the audio controller
        {
            BCM_AUDIO_DEBUG(" Audio Controller Init\n");
            AUDCTRL_Init ();
            BCM_AUDIO_DEBUG(" Audio Controller Init Complete\n");
        }
        break;
        case 2:// Start Hw loopback
        {
			Boolean onOff = sgBrcm_auddrv_TestValues[2];
            mic = sgBrcm_auddrv_TestValues[3];
			spkr = sgBrcm_auddrv_TestValues[4];
			BCM_AUDIO_DEBUG(" Audio Loopback onOff = %d, from %d to %d\n", onOff, mic, spkr);
            AUDCTRL_SetAudioLoopback(onOff,mic,spkr);
        }
        break;
        case 3:// Dump registers
        {
            BCM_AUDIO_DEBUG(" Dump registers\n");
            //dump_audio_registers();
			{
				char *MsgBuf =  NULL;
				MsgBuf = kmalloc(2408,GFP_KERNEL);

				csl_caph_ControlHWClock(TRUE);

				sprintf( MsgBuf, "0x35026800 =0x%08lx, 0x3502c910 =0x%08lx, 0x3502c990 =0x%08lx, 0x3502c900 =0x%08lx,0x3502cc20 =0x%08lx,0x35025800 =0x%08lx, 0x34000a34 =0x%08lx, 0x340004b0 =0x%08lx, 0x3400000c =0x%08lx, 0x3400047c =0x%08lx, 0x34000a40=0x%08lx\n",
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x35026800))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502c910))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502c990))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502c900))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502cc20))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x35025800))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x34000a34))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x340004b0))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3400000c))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3400047c))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x34000a40)))
										);

				BCM_AUDIO_DEBUG("%s",MsgBuf);

				sprintf( MsgBuf, "0x3502f000 =0x%08lx, 04 =0x%08lx, 08 =0x%08lx, 0c =0x%08lx, 10 =0x%08lx, 14 =0x%08lx, 18 =0x%08lx, 1c =0x%08lx, 20 =0x%08lx, 24 =0x%08lx, 28 =0x%08lx, 2c =0x%08lx, 30 =0x%08lx, 34 =0x%08lx, 38 =0x%08lx, 3c =0x%08lx, 40 =0x%08lx, 44 =0x%08lx, 48 =0x%08lx, 4c =0x%08lx,50 =0x%08lx, 54 =0x%08lx, 58 =0x%08lx, 5c =0x%08lx \n",
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f000))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f004))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f008))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f00c))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f010))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f014))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f018))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f01c))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f020))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f024))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f028))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f02c))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f030))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f034))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f038))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f03c))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f040))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f044))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f048))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f04c))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f050))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f054))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f058))),
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(0x3502f05c)))
										);

				BCM_AUDIO_DEBUG("%s",MsgBuf);

				kfree(MsgBuf);
				csl_caph_ControlHWClock(FALSE);
			}
            BCM_AUDIO_DEBUG(" Dump registers done \n");
        }
        break;

        case 4:// Enable telephony
        {
            BCM_AUDIO_DEBUG(" Enable telephony\n");
            AUDCTRL_EnableTelephony(AUDIO_SOURCE_ANALOG_MAIN,AUDIO_SINK_HANDSET);

            BCM_AUDIO_DEBUG(" Telephony enabled \n");
        }
        break;
        case 5:// Disable telephony
        {
            BCM_AUDIO_DEBUG(" Disable telephony\n");
            AUDCTRL_DisableTelephony( );
		            BCM_AUDIO_DEBUG(" Telephony disabled \n");
        }
		break;
#ifdef CONFIG_VOIP_DRIVER_TEST
		case 6: // VoIP loopback test
		{
			//Val2 - Mic
			//Val3 - speaker
			//Val4 - Delay
			//Val5 - Codectype
			//AUDTST_VoIP( sgBrcm_auddrv_TestValues[2],sgBrcm_auddrv_TestValues[3], 2000, sgBrcm_auddrv_TestValues[4],0 );
			AUDTST_VoIP( sgBrcm_auddrv_TestValues[2],sgBrcm_auddrv_TestValues[3], 2000, sgBrcm_auddrv_TestValues[4],0);
		}
		break;
#endif
 		case 8:// peek a register
        {
			UInt32 regAddr = sgBrcm_auddrv_TestValues[2];
			UInt32 regVal = 0;
			BCM_AUDIO_DEBUG(" peek a register, 0x%08lx\n", regAddr);
			csl_caph_ControlHWClock(TRUE);
			regVal = *((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)));
            BCM_AUDIO_DEBUG("		value = 0x%08lx\n",regVal );
			csl_caph_ControlHWClock(FALSE);
		}
		break;

		case 9:// poke a register
        {
			UInt32 regAddr = sgBrcm_auddrv_TestValues[2];
			UInt32 regVal = sgBrcm_auddrv_TestValues[3];
			csl_caph_ControlHWClock(TRUE);
			*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr))) = regVal;
            BCM_AUDIO_DEBUG(" poke a register, 0x%08lx = 0x%08lx\n", regAddr, *((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr))));
			csl_caph_ControlHWClock(FALSE);
		}
		break;

#if !(defined(_SAMOA_))
        case 10: // hard code caph clocks, sometimes clock driver is not working well
        {
            // hard code it.

            UInt32 regVal;

            BCM_AUDIO_DEBUG(" hard code caph clock register for debugging..\n");
			csl_caph_ControlHWClock(TRUE);
			regVal = (0x00A5A5 << KHUB_CLK_MGR_REG_WR_ACCESS_PASSWORD_SHIFT);
            regVal |= KHUB_CLK_MGR_REG_WR_ACCESS_CLKMGR_ACC_MASK;
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_WR_ACCESS_OFFSET),regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_WR_ACCESS_OFFSET)) = (UInt32)regVal);
            while ( ((*((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY_CTL_OFFSET))) & 0x01) == 1) {}


            /* Set the frequency policy */
            regVal = (0x06 << KHUB_CLK_MGR_REG_POLICY_FREQ_POLICY0_FREQ_SHIFT);
            regVal |= (0x06 << KHUB_CLK_MGR_REG_POLICY_FREQ_POLICY1_FREQ_SHIFT);
            regVal |= (0x06 << KHUB_CLK_MGR_REG_POLICY_FREQ_POLICY2_FREQ_SHIFT);
            regVal |= (0x06 << KHUB_CLK_MGR_REG_POLICY_FREQ_POLICY3_FREQ_SHIFT);
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_POLICY_FREQ_OFFSET) ,regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY_FREQ_OFFSET)) = (UInt32)regVal);

            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_OFFSET)) = (UInt32)0x0000FFFF);

            /* Set the frequency policy */
            regVal = 0x7FFFFFFF;
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_POLICY0_MASK1_OFFSET) ,regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY0_MASK1_OFFSET)) = (UInt32)regVal);
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_POLICY1_MASK1_OFFSET) ,regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY1_MASK1_OFFSET)) = (UInt32)regVal);
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_POLICY2_MASK1_OFFSET) ,regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY2_MASK1_OFFSET)) = (UInt32)regVal);
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_POLICY3_MASK1_OFFSET) ,regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY3_MASK1_OFFSET)) = (UInt32)regVal);
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_POLICY0_MASK2_OFFSET) ,regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY0_MASK2_OFFSET)) = (UInt32)regVal);
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_POLICY1_MASK2_OFFSET) ,regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY1_MASK2_OFFSET)) = (UInt32)regVal);
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_POLICY2_MASK2_OFFSET) ,regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY2_MASK2_OFFSET)) = (UInt32)regVal);
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_POLICY3_MASK2_OFFSET) ,regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY3_MASK2_OFFSET)) = (UInt32)regVal);

            /* start the frequency policy */
            regVal = 0x00000003; //(KHUB_CLK_MGR_REG_POLICY_CTL_GO_MASK | KHUB_CLK_MGR_REG_POLICY_CTL_GO_AC_MASK);
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_POLICY_CTL_OFFSET) ,regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY_CTL_OFFSET)) = (UInt32)regVal);
            while ( ((*((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_POLICY_CTL_OFFSET))) & 0x01) == 1) {}

            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_OFFSET)) = (UInt32)0x0000FFFF);


            //OSTASK_Sleep(1000);


            // srcMixer clock
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_CAPH_DIV_OFFSET)) = (UInt32)0x00000011);
            //while ( ((*((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_PERIPH_SEG_TRG_OFFSET))) & 0x00100000) == 0x00100000) {}


            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_PERIPH_SEG_TRG_OFFSET)) = (UInt32)0x00100000);
            //while ( ((*((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_PERIPH_SEG_TRG_OFFSET))) & 0x00100000) == 0x00100000) {}


            /* Enable all the CAPH clocks */
#if 0
            //regVal = KHUB_CLK_MGR_REG_CAPH_CLKGATE_CAPH_SRCMIXER_CLK_EN_MASK;
            //regVal |= KHUB_CLK_MGR_REG_CAPH_CLKGATE_CAPH_SRCMIXER_HW_SW_GATING_SEL_MASK;
            //regVal |= KHUB_CLK_MGR_REG_CAPH_CLKGATE_CAPH_SRCMIXER_HYST_EN_MASK;
            //regVal |= KHUB_CLK_MGR_REG_CAPH_CLKGATE_CAPH_SRCMIXER_HYST_VAL_MASK;
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_CAPH_CLKGATE_OFFSET) ,regVal);
#endif
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_CAPH_CLKGATE_OFFSET)) = (UInt32)0x1030);

            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_DAP_SWITCH_CLKGATE_OFFSET)) = (UInt32)0x1);

            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_APB10_CLKGATE_OFFSET)) = (UInt32)0x1);


#if 0
            /* Enable all the AUDIOH clocks, 26M, 156M, 2p4M, 6p5M  */
            regVal = KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_AUDIOH_2P4M_CLK_EN_MASK;
            regVal |= KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_AUDIOH_2P4M_HW_SW_GATING_SEL_MASK;
            regVal |= KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_AUDIOH_26M_CLK_EN_MASK;
            regVal |= KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_AUDIOH_26M_HW_SW_GATING_SEL_MASK;
            regVal |= KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_AUDIOH_156M_CLK_EN_MASK;
            regVal |= KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_AUDIOH_156M_HW_SW_GATING_SEL_MASK;
            regVal |= KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_AUDIOH_APB_CLK_EN_MASK;
             regVal |= KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_AUDIOH_APB_HW_SW_GATING_SEL_MASK;
            regVal |= KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_AUDIOH_APB_HYST_VAL_MASK;
            //WRITE_REG32((HUB_CLK_BASE_ADDR+KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_OFFSET) ,regVal);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_OFFSET)) = (UInt32)regVal);
#endif
            //( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_PERIPH_SEG_TRG_OFFSET)) = (UInt32)0x00100000);
            //( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_APB10_CLKGATE_OFFSET)) = (UInt32)0x00000001);

            // lock
            /*
            regVal = (0x00A5A5 << KHUB_CLK_MGR_REG_WR_ACCESS_PASSWORD_SHIFT);
            ( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_WR_ACCESS_OFFSET)) = (UInt32)regVal);
            while ( ((*((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_PERIPH_SEG_TRG_OFFSET))) & 0x00100000) == 0x00100000) {}
            */


            //( *((volatile UInt32 *)(KONA_HUB_CLK_BASE_VA+KHUB_CLK_MGR_REG_AUDIOH_CLKGATE_OFFSET)) = (UInt32)0x0000ffaa);

			csl_caph_ControlHWClock(FALSE);
		}
        break;
#endif
#ifdef INTERNAL_VOIF_TEST
        case 11: // VoIF
        {
			Boolean onOff = sgBrcm_auddrv_TestValues[2];
			AudioMode_t audMode = AUDIO_MODE_HANDSET;
            BCM_AUDIO_DEBUG(" VoIF test.\n");

			if (onOff)
			{
				VoIF_SetDelay(sgBrcm_auddrv_TestValues[3]);
				if (sgBrcm_auddrv_TestValues[4]>0)
					VoIF_SetGain(sgBrcm_auddrv_TestValues[4]);
				audMode = AUDCTRL_GetAudioMode();
            	VoIF_init (audMode);
			}
			else
				VoIF_Deinit();
        }
		break;
#endif

        default:
            BCM_AUDIO_DEBUG(" Invalid Control Command\n");
    }
	return 0;
}

static unsigned long current_ipbuffer_index = 0;
static unsigned long dma_buffer_write_index = 0;

static unsigned long period_bytes = 0;
static unsigned long num_blocks = 0;
static AUDIO_DRIVER_BUFFER_t buf_param;
static int HandlePlayCommand()
{

    unsigned long period_ms;

    unsigned long copy_bytes;
    static AUDIO_DRIVER_HANDLE_t drv_handle = NULL;
    static AUDIO_DRIVER_CONFIG_t drv_config;
    static dma_addr_t            dma_addr;
    static AUDIO_SINK_Enum_t     spkr;
    static int src_used=0;
    char* src;
    char* dest;
    AUDIO_DRIVER_CallBackParams_t	cbParams;
	unsigned int testint=0;


    switch(sgBrcm_auddrv_TestValues[1])
    {
        case 1://open the plyabck device
        {
            BCM_AUDIO_DEBUG(" Audio DDRIVER Open\n");
            drv_handle = AUDIO_DRIVER_Open(AUDIO_DRIVER_PLAY_AUDIO);
            BCM_AUDIO_DEBUG(" Audio DDRIVER Open Complete\n");
        }
        break;
	case 2:
	{
	    src_used = 0;
            if(sgBrcm_auddrv_TestValues[2] == 0) // default
            {
            	if(sgBrcm_auddrv_TestValues[3] == 0) // default
		{
                	BCM_AUDIO_DEBUG(" Playback of pre-defined sample 48KHz Mono\n");
                	samplePCM16_inaudiotest = (char *)playback_audiotest;
		}
		else if(sgBrcm_auddrv_TestValues[3] == 1)
		{
                	BCM_AUDIO_DEBUG(" Playback of pre-defined sample 8KHz Mono : use HW SRC mixer\n");
                	samplePCM16_inaudiotest = (char *)playback_audiotest_srcmixer;
			src_used = 1;
		}

            }
            else if(sgBrcm_auddrv_TestValues[2] == 1)
            {
                if(record_test_buf != NULL)
                {
                	BCM_AUDIO_DEBUG(" Playback of recorded data\n");
		        samplePCM16_inaudiotest = (char *)record_test_buf;
                }
		else
                        BCM_AUDIO_DEBUG(" record buffer freed: record data to play\n");
            }

	}
	break;
	case 3:
        {
            BCM_AUDIO_DEBUG(" Audio DDRIVER Config\n");
            //set the callback
            //AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_CB,(void*)AUDIO_DRIVER_TEST_InterruptPeriodCB);
	    cbParams.pfCallBack = AUDIO_DRIVER_TEST_InterruptPeriodCB;
	    cbParams.pPrivateData = (void *)drv_handle;
 	    AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_CB,(void*)&cbParams);

            // configure defaults

	    if(src_used == 1)
            	drv_config.sample_rate = AUDIO_SAMPLING_RATE_8000;
	    else
                drv_config.sample_rate = AUDIO_SAMPLING_RATE_48000;

            drv_config.num_channel = AUDIO_CHANNEL_MONO;
            drv_config.bits_per_sample = AUDIO_16_BIT_PER_SAMPLE;

            if(sgBrcm_auddrv_TestValues[2] != 0)
                drv_config.sample_rate = sgBrcm_auddrv_TestValues[2];
            if(sgBrcm_auddrv_TestValues[3] != 0)
                drv_config.num_channel = sgBrcm_auddrv_TestValues[3];

            BCM_AUDIO_DEBUG("Config:sr=%ld nc=%d bs=%ld \n",drv_config.sample_rate,drv_config.num_channel,drv_config.bits_per_sample);

            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_CONFIG,(void*)&drv_config);

            period_ms = 100;
            if(sgBrcm_auddrv_TestValues[4] != 0)
                period_ms = sgBrcm_auddrv_TestValues[4];

            //set the interrupt period
            period_bytes = period_ms * (drv_config.sample_rate/1000) * (drv_config.num_channel) * 2;
	    	num_blocks = 2; // for RHEA
            BCM_AUDIO_DEBUG("Period: ms=%ld bytes=%ld blocks:%ld\n",period_ms,period_bytes,num_blocks);
            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_INT_PERIOD,(void*)&period_bytes);

            buf_param.buf_size = PCM_TEST_MAX_PLAYBACK_BUF_BYTES;
            buf_param.pBuf = dma_alloc_coherent (NULL, buf_param.buf_size, &dma_addr,GFP_KERNEL);
            if(buf_param.pBuf == NULL)
            {
                BCM_AUDIO_DEBUG("Cannot allocate Buffer \n");
                return 0;
            }
            buf_param.phy_addr = (UInt32)dma_addr;

            BCM_AUDIO_DEBUG("virt_addr = %s phy_addr=0x%lx\n",buf_param.pBuf,(UInt32)dma_addr);

            current_ipbuffer_index = 0;
            dma_buffer_write_index = 0;

            if((num_blocks * period_bytes) <= TEST_BUF_SIZE)
                copy_bytes = (num_blocks * period_bytes);
            else
                copy_bytes  = TEST_BUF_SIZE;

            src = ((char*)samplePCM16_inaudiotest) + current_ipbuffer_index;
            dest = buf_param.pBuf + dma_buffer_write_index;

            memcpy(dest,src,copy_bytes);

            current_ipbuffer_index += copy_bytes;

	    BCM_AUDIO_DEBUG("copy_bytes %ld",copy_bytes);
	        //set the buffer params
	    AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_BUF_PARAMS,(void*)&buf_param);
            BCM_AUDIO_DEBUG(" Audio DDRIVER Config Complete\n");
        }
        break;
        case 4: //Start the playback
            {
				CSL_CAPH_DEVICE_e aud_dev = CSL_CAPH_DEV_EP; // EP is default now

                BCM_AUDIO_DEBUG(" Start Playback\n");
                spkr = sgBrcm_auddrv_TestValues[2];

                AUDCTRL_SaveAudioModeFlag(spkr);

                AUDCTRL_EnablePlay(AUDIO_SOURCE_MEM,
                                   spkr,
				                   drv_config.num_channel,
                                   drv_config.sample_rate,
								   &testint
				                    );

              	AUDCTRL_SetPlayVolume (AUDIO_SOURCE_MEM,
   		     			spkr,
    				   	AUDIO_GAIN_FORMAT_mB,
				   	0x00, 0x00,0); // 0 db for both L and R channels.



                AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_START,&aud_dev);
                BCM_AUDIO_DEBUG("Playback started\n");


		//  Need to implement some sync mechanism
	        OSTASK_Sleep(5000);
                BCM_AUDIO_DEBUG(" Stop playback\n");

                AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_STOP,NULL);

                //disable the playback path
                AUDCTRL_DisablePlay(AUDIO_SOURCE_MEM,spkr,testint);

                AUDIO_DRIVER_Close(drv_handle);

            }
            break;
#ifdef CONFIG_ARM2SP_PLAYBACK_TEST
			case 5:
			{
				/* val2 -> 0  ,
				val3 -> VORENDER_TYPE  0- EP_OUT (ARM2SP) , 1 - HS, 2 - IHF
				Val4 -   0 - playback
				Val5 - Sampling rate  0 -> playback of 8K PCM
				Val6  - Mix mode CSL_ARM2SP_VOICE_MIX_MODE_t */
				//AUDTST_VoicePlayback(AUDIO_SINK_HANDSET,0, 0 , VORENDER_PLAYBACK_DL, VORENDER_VOICE_MIX_NONE );
				AUDTST_VoicePlayback(0, sgBrcm_auddrv_TestValues[2],sgBrcm_auddrv_TestValues[3], VORENDER_PLAYBACK_DL, sgBrcm_auddrv_TestValues[4] ); //play to DL
			}
			break;
#endif
        default:
            BCM_AUDIO_DEBUG(" Invalid Playback Command\n");
    }
	return 0;
}

static void AUDIO_DRIVER_TEST_InterruptPeriodCB(void *pPrivate)
{
    char* src;
    char* dest;

    if((current_ipbuffer_index + period_bytes) >= TEST_BUF_SIZE)
        current_ipbuffer_index = 0;

    src = ((char*)samplePCM16_inaudiotest) + current_ipbuffer_index;
    dest = buf_param.pBuf + dma_buffer_write_index;

    memcpy(dest,src,period_bytes);

    current_ipbuffer_index += period_bytes;
    dma_buffer_write_index += period_bytes;

    if(dma_buffer_write_index >= (num_blocks * period_bytes))
        dma_buffer_write_index = 0;
    //BCM_AUDIO_DEBUG(" current_ipbuffer_index %d: dma_buffer_write_index- %d\n",current_ipbuffer_index,dma_buffer_write_index);
    return;
}


static unsigned long current_capt_buffer_index = 0;
static unsigned long capt_dma_buffer_read_index = 0;

static unsigned long capt_period_bytes = 0;
static unsigned long capt_num_blocks = 0;
static AUDIO_DRIVER_BUFFER_t capt_buf_param;
static int HandleCaptCommand()
{

    unsigned long period_ms;
    static AUDIO_DRIVER_HANDLE_t drv_handle = NULL;
    static AUDIO_DRIVER_CONFIG_t drv_config;
    static dma_addr_t            dma_addr;
    static AUDIO_SOURCE_Enum_t     mic = AUDIO_SOURCE_ANALOG_MAIN;
    AUDIO_DRIVER_CallBackParams_t	cbParams;


    static AUDIO_DRIVER_TYPE_t drv_type = AUDIO_DRIVER_CAPT_HQ;

    static Boolean record_buf_allocated = 0;

    switch(sgBrcm_auddrv_TestValues[1])
    {
        case 1://open the capture device
        {
            if(sgBrcm_auddrv_TestValues[2] != 0)
            	drv_type = sgBrcm_auddrv_TestValues[2];
            BCM_AUDIO_DEBUG(" Audio Capture DDRIVER Open drv_type %d\n",drv_type);
            drv_handle = AUDIO_DRIVER_Open(drv_type);
            BCM_AUDIO_DEBUG(" Audio DDRIVER Open Complete\n");

        }
        break;
        case 2://configure capture device
        {
            BCM_AUDIO_DEBUG(" Audio Capture DDRIVER Config\n");
            //set the callback
	    cbParams.pfCallBack = AUDIO_DRIVER_TEST_CaptInterruptPeriodCB;
	    cbParams.pPrivateData = (void *)drv_handle;
 	    AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_CB,(void*)&cbParams);

            if(drv_type == AUDIO_DRIVER_CAPT_HQ)
            {
                // configure defaults
                drv_config.sample_rate = AUDIO_SAMPLING_RATE_48000;
                drv_config.num_channel = AUDIO_CHANNEL_MONO;
                drv_config.bits_per_sample = AUDIO_16_BIT_PER_SAMPLE;
            }
            else if(drv_type == AUDIO_DRIVER_CAPT_VOICE)
            {
                // configure defaults
                drv_config.sample_rate = AUDIO_SAMPLING_RATE_8000;
                drv_config.num_channel = AUDIO_CHANNEL_MONO;
                drv_config.bits_per_sample = AUDIO_16_BIT_PER_SAMPLE;

            }

            if(sgBrcm_auddrv_TestValues[2] != 0)
                drv_config.sample_rate = sgBrcm_auddrv_TestValues[2];
            if(sgBrcm_auddrv_TestValues[3] != 0)
                drv_config.num_channel = sgBrcm_auddrv_TestValues[3];

            BCM_AUDIO_DEBUG("Config:sr=%ld nc=%d bs=%ld \n",drv_config.sample_rate,drv_config.num_channel,drv_config.bits_per_sample);

            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_CONFIG,(void*)&drv_config);

            period_ms = 100;
            if(sgBrcm_auddrv_TestValues[4] != 0)
                period_ms = sgBrcm_auddrv_TestValues[4];

            //set the interrupt period
            capt_period_bytes = period_ms * (drv_config.sample_rate/1000) * (drv_config.num_channel) * 2;
            capt_num_blocks =  2; // limitation for RHEA

            BCM_AUDIO_DEBUG("Period: ms=%ld bytes=%ld blocks:%ld\n",period_ms,capt_period_bytes,capt_num_blocks);
            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_INT_PERIOD,(void*)&capt_period_bytes);

	    current_capt_buffer_index = 0;
	    capt_dma_buffer_read_index = 0;

            capt_buf_param.buf_size = PCM_TEST_MAX_CAPTURE_BUF_BYTES;
            capt_buf_param.pBuf = dma_alloc_coherent (NULL, capt_buf_param.buf_size, &dma_addr,GFP_KERNEL);
            if(capt_buf_param.pBuf == NULL)
            {
                BCM_AUDIO_DEBUG("Cannot allocate Buffer \n");
                return 0;
            }
            capt_buf_param.phy_addr = (UInt32)dma_addr;

            BCM_AUDIO_DEBUG("virt_addr = %s phy_addr=0x%lx\n",capt_buf_param.pBuf,(UInt32)dma_addr);

            memset(capt_buf_param.pBuf,0,PCM_TEST_MAX_CAPTURE_BUF_BYTES);
            //set the buffer params
            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_BUF_PARAMS,(void*)&capt_buf_param);

            BCM_AUDIO_DEBUG(" Audio DDRIVER Config Complete\n");
        }
        break;
        case 3: //Start the capture
            {

		if(!record_buf_allocated)
		{
			record_test_buf = kmalloc(TEST_BUF_SIZE,GFP_KERNEL);
			memset(record_test_buf,0,TEST_BUF_SIZE);
			record_buf_allocated = 1;
                }
		if(sgBrcm_auddrv_TestValues[2] != 0)
			mic = sgBrcm_auddrv_TestValues[2];

		BCM_AUDIO_DEBUG(" Start capture mic %d\n",mic);

                AUDCTRL_EnableRecord(mic,
								     AUDIO_SINK_MEM,
                				     drv_config.num_channel,
                				     drv_config.sample_rate,
                     				 NULL
                    				 );

                AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_START,&mic);

                BCM_AUDIO_DEBUG("capture started\n");

		OSTASK_Sleep(5000);

                BCM_AUDIO_DEBUG(" Stop capture\n");

                AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_STOP,NULL);

                AUDCTRL_DisableRecord(mic,AUDIO_SOURCE_MEM,0);

		BCM_AUDIO_DEBUG("capture stopped\n");

            }
            break;

	case 4:
		// free the buffer record_test_buf
            	BCM_AUDIO_DEBUG(" Freed the recorded test buf \n");
                kfree(record_test_buf);
		record_buf_allocated = 0;
		record_test_buf = NULL;
	    break;
        default:
            BCM_AUDIO_DEBUG(" Invalid capture Command\n");
    }
	return 0;

}

static void AUDIO_DRIVER_TEST_CaptInterruptPeriodCB(void *pPrivate)
{
    char* src;
    char* dest;

    //BCM_AUDIO_DEBUG(" %lx: capture Interrupt- %d\n",jiffies);

    if((current_capt_buffer_index + capt_period_bytes) >= TEST_BUF_SIZE)
    {
        current_capt_buffer_index = 0;
    }
    dest = ((char*)record_test_buf) + current_capt_buffer_index;
    src = capt_buf_param.pBuf + capt_dma_buffer_read_index;

    memcpy(dest,src,capt_period_bytes);

    current_capt_buffer_index += capt_period_bytes;
    capt_dma_buffer_read_index += capt_period_bytes;

    if(capt_dma_buffer_read_index >= (capt_num_blocks * capt_period_bytes))
        capt_dma_buffer_read_index = 0;

    //BCM_AUDIO_DEBUG(" current_capt_buffer_index %d: capt_dma_buffer_read_index- %d\n",current_capt_buffer_index,capt_dma_buffer_read_index);

    return;
}

#ifdef CONFIG_ARM2SP_PLAYBACK_TEST

// voice playback test including amrnb, pcm via VPU, ARM2SP, and amrwb playback

void AUDTST_VoicePlayback(UInt32 Val2, UInt32 Val3, UInt32 Val4, UInt32 Val5, UInt32 Val6)

{
	{
		VORENDER_TYPE_t	drvtype = VORENDER_TYPE_PCM_ARM2SP;
		UInt32	totalSize = 0;
		UInt8	*dataSrc;
		UInt32	frameSize = 0;
		UInt32	finishedSize;
		UInt32 writeSize;
		CSL_ARM2SP_PLAYBACK_MODE_t playbackMode;
		CSL_ARM2SP_VOICE_MIX_MODE_t mixMode;
		AUDIO_SAMPLING_RATE_t	sr = AUDIO_SAMPLING_RATE_8000;
		AUDIO_SINK_Enum_t speaker = AUDIO_SINK_HANDSET;
		Boolean		setTransfer = FALSE;
		AUDIO_NUM_OF_CHANNEL_t stereo = AUDIO_CHANNEL_MONO;


		// for rhea from here
		if (Val3 == 0)// for rhea
		{
			drvtype = VORENDER_TYPE_PCM_ARM2SP;
			// earpiece
			speaker = AUDIO_SINK_HANDSET;
		}
		else  if (Val3 == 1)// for rhea
		{
			drvtype = VORENDER_TYPE_PCM_ARM2SP;
			// headset
			speaker = AUDIO_SINK_HEADSET;
		}
		else  if (Val3 == 2)// for rhea
		{
			drvtype = VORENDER_TYPE_PCM_ARM2SP;
			// ihf
			speaker = AUDIO_SINK_LOUDSPK;
		}


		sr = AUDIO_SAMPLING_RATE_8000; // provide user option to select the sampling rate

		Log_DebugPrintf(LOGID_AUDIO, "\n debug 1, stereo =%d drvtype =%d\n", stereo, drvtype);
		AUDCTRL_EnableTelephony(AUDIO_SOURCE_ANALOG_MAIN,AUDIO_SINK_HANDSET); //rhea cases.

		AUDCTRL_SetPlayVolume (AUDIO_SOURCE_MEM,
   					               speaker,
    							   AUDIO_GAIN_FORMAT_mB,
	    						   0, 0,0);


			// init driver
		AUDDRV_VoiceRender_Init (drvtype);

		AUDDRV_VoiceRender_SetBufDoneCB (drvtype, AUDDRV_BUFFER_DONE_CB);

		playbackMode = VORENDER_PLAYBACK_DL;  //  1= dl, 2 = ul, 3 =both
		mixMode = (CSL_ARM2SP_VOICE_MIX_MODE_t) Val6; // 0 = none, 1= dl, 2 = ul, 3 =both


		if (Val6 == 10)
		{
			// set buffer transfer
			setTransfer = TRUE;
		}

		if (Val4 == 0)
		{
			if(sr == AUDIO_SAMPLING_RATE_8000) //pick up the 8K buffer
			{
				dataSrc = &playback_audiotest_srcmixer[0];
				totalSize = sizeof (playback_audiotest_srcmixer);
				frameSize = (sr * 2 * 2) / 50; // 20 ms seconds

				// start writing data
				AUDDRV_VoiceRender_SetConfig (drvtype, playbackMode, mixMode, sr, VP_SPEECH_MODE_LINEAR_PCM_8K, 0,0 );
			}
			else if(sr == AUDIO_SAMPLING_RATE_16000)
			{
				// implement for other sampling rates as well.
				dataSrc = &playback_audiotest_srcmixer[0];
				totalSize = sizeof (playback_audiotest_srcmixer);
				frameSize = (sr * 2 * 2) / 50; // 20 ms seconds

				// start writing data
				AUDDRV_VoiceRender_SetConfig (drvtype, playbackMode, mixMode, sr, VP_SPEECH_MODE_LINEAR_PCM_16K, 0,0 );
			}
		}
		else //playback the recorded buffer
		{
				//Not implemented
		}


		finishedSize = 0;
		writeSize = 0;

		AUDDRV_BufDoneSema = OSSEMAPHORE_Create(0, OSSUSPEND_PRIORITY);

		Log_DebugPrintf(LOGID_AUDIO, "\n debug 1!, totalSize = 0x%x \n", (unsigned int)totalSize);
		AUDDRV_VoiceRender_Start (drvtype);

		writeSize = frameSize;
		AUDDRV_VoiceRender_Write (drvtype, dataSrc, writeSize);

		// The AUDDRV_BUFFER_DONE_CB callback will release the buffer size.
		while (OSSEMAPHORE_Obtain(AUDDRV_BufDoneSema, 2*1000) == OSSTATUS_SUCCESS)
		{

			dataSrc += writeSize;
			finishedSize += writeSize;

			if (finishedSize >= totalSize)
				break;

			if (totalSize - finishedSize < frameSize)
			{
				writeSize = totalSize - finishedSize;
			}
			else
			{
				writeSize = frameSize;
			}
			AUDDRV_VoiceRender_Write (drvtype, dataSrc, writeSize);

			Log_DebugPrintf(LOGID_AUDIO, "\n debug 2: writeSize = 0x%x, finishedSize = 0x%x\n", (unsigned int)writeSize, (unsigned int)finishedSize);

		}

		Log_DebugPrintf(LOGID_AUDIO, "\n debug 7: writeSize = 0x%x, finishedSize = 0x%x\n", (unsigned int)writeSize, (unsigned int)finishedSize);

		// finish all the data
		// stop the driver
		AUDDRV_VoiceRender_Stop (drvtype, TRUE); // TRUE= immediately stop

		// need to give time to dsp to stop.
		OSTASK_Sleep( 3 ); //make sure the path turned on

		AUDDRV_VoiceRender_Shutdown (drvtype);

		pr_info( "\n  Voice render stop done \n");
		AUDCTRL_DisableTelephony( ); 

		OSSEMAPHORE_Destroy(AUDDRV_BufDoneSema);
	}
}

#endif

#ifdef CONFIG_VOIP_DRIVER_TEST

void AUDTST_VoIP(UInt32 Val2, UInt32 Val3, UInt32 Val4, UInt32 Val5, UInt32 Val6)
{
	// Val2 : mic
	// Val3: speaker
	// Val4: delay in miliseconds
	// Val5: codec value, i.e. 4096 (0x1000, PCM), 8192 (0x2000, FR), 12288 (0x3000, AMR475), 20480 (0x5000, PCM_16K), 24576 (0x6000 AMR_16K), etc
	// val6: n/a
	UInt8	*dataDest = NULL;
	UInt32	vol = 0;
	AudioMode_t mode = AUDIO_MODE_HANDSET;
	UInt32 codecVal = 0;
	static AUDIO_DRIVER_HANDLE_t drv_handle = NULL;
	AUDIO_DRIVER_CallBackParams_t	cbParams;
	AUDIO_SOURCE_Enum_t mic = (AUDIO_SOURCE_Enum_t)Val2; // mic
	AUDIO_SINK_Enum_t spk = (AUDIO_SINK_Enum_t)Val3; //speaker
	UInt32		delayMs = Val4; // delay in milliseconds
	UInt32 count = 0; //20ms each count
	UInt32 count1 = 0; //20ms each count

    if (record_test_buf == NULL)
    {
        record_test_buf = OSHEAP_Alloc(1024*1024);
    }

	codecVal = Val5; //0;

	Log_DebugPrintf(LOGID_AUDIO, "\n AUDTST_VoIP codecVal %ld\n",codecVal);

	if((codecVal == 4) || (codecVal == 5))// VOIP_PCM_16K or VOIP_AMR_WB_MODE_7k
	{
		mode = AUDCTRL_GetAudioMode();
		//set the audio mode to WB
		AUDCTRL_SetAudioMode((AudioMode_t)(mode + AUDIO_MODE_NUMBER));
	}

	AUDCTRL_EnableTelephony (mic, spk);
	AUDCTRL_SetTelephonySpkrVolume (spk, vol, AUDIO_GAIN_FORMAT_mB);

	// init driver

	drv_handle = AUDIO_DRIVER_Open(AUDIO_DRIVER_VOIP);

	//set UL callback
	cbParams.voipULCallback = AudDrv_VOIP_DumpUL_CB;
	cbParams.pPrivateData = (void *)0;
	AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_VOIP_UL_CB,(void*)&cbParams);

	//set the callback
	cbParams.voipDLCallback = AudDrv_VOIP_FillDL_CB;
	cbParams.pPrivateData = (void *)0;
	AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_VOIP_DL_CB,(void*)&cbParams);

	dataDest = (UInt8 *)&record_test_buf[0];

	sVtQueue = AUDQUE_Create (dataDest, 2000, 322);

	AUDDRV_BufDoneSema = OSSEMAPHORE_Create(1, OSSUSPEND_PRIORITY);
	sVtQueue_Sema = OSSEMAPHORE_Create(1, OSSUSPEND_PRIORITY);


	AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_START,&codecVal);

	//Log_DebugPrintf(LOGID_AUDIO, "\n VoIP: debug 1 \n");

	count = delayMs/20;

	//Log_DebugPrintf(LOGID_AUDIO, "\n VoIP: debug 2 \n");

	//Log_DebugPrintf(LOGID_AUDIO, "\n VoIP: Test loopback \n");

	// test with loopback UL to DL
	while (1)
	{
		if (count1++ == 1000)
		{
			count1 = 0;
			break;
		}

		//Log_DebugPrintf(LOGID_AUDIO, "\n VoIP: debug 3, count1 %d\n", count1);

		OSSEMAPHORE_Obtain(sVtQueue_Sema, TICKS_FOREVER);
		if(count)
		{
			count--;
			//Log_DebugPrintf(LOGID_AUDIO, "\n VoIP: debug 4 count %d\n", count);
			continue;
		}
		OSSEMAPHORE_Obtain(AUDDRV_BufDoneSema, 2*1000);

	}

	pr_info( "\n VoIP: Finished\n");

	// finish all the data
	// stop the driver
	AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_STOP,NULL);

	pr_info( "\n VoIP: Stop\n");

	// disable the hw
	AUDCTRL_DisableTelephony ( );

	if((codecVal == 4) || (codecVal == 5))// VOIP_PCM_16K or VOIP_AMR_WB_MODE_7k
		AUDCTRL_SetAudioMode(mode); //setting it back the original mode

	OSSEMAPHORE_Destroy(AUDDRV_BufDoneSema);
	OSSEMAPHORE_Destroy(sVtQueue_Sema);
	AUDQUE_Destroy(sVtQueue);

	AUDIO_DRIVER_Close(drv_handle);

}
#endif
