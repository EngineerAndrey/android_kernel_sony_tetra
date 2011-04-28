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
*   @file   brcm_auddrv_test.c
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

#include "brcm_alsa.h"


#include "mobcom_types.h"
#include "resultcode.h"
#include "audio_consts.h"
#include "chal_types.h"
#include "auddrv_def.h"
#include "log.h"
#include "csl_caph.h"
#include "drv_caph.h"
#include "drv_audio_common.h"
#include "drv_caph_hwctrl.h"
#include "audio_controller.h"
#include "audio_ddriver.h"

static UInt8 *samplePCM16_inaudiotest = NULL;
static UInt16* record_test_buf = NULL;

UInt8 playback_audiotest[1024000] = {
#ifdef ENABLE_TESTDATA
	#include "pcm_16_48khz_mono.txt"
#else
	0
#endif
};
UInt8 playback_audiotest_srcmixer[165856] = {
#ifdef ENABLE_TESTDATA
	#include "sampleWAV16bit.txt"
#else
	0
#endif
};



#define BRCM_AUDDRV_NAME_MAX (15)  //max 15 char for test name 
#define BRCM_AUDDRV_TESTVAL  (5)   // max no of arg for each test

#define	PCM_TEST_MAX_PLAYBACK_BUF_BYTES		(100*1024)
#define	PCM_TEST_MAX_CAPTURE_BUF_BYTES		(100*1024)
#define TEST_BUF_SIZE   (512 * 1024)

static int sgBrcm_auddrv_TestValues[BRCM_AUDDRV_TESTVAL];
static char *sgBrcm_auddrv_TestName[]={"Aud_play","Aud_Rec","Aud_control"};

static void __iomem *ahb_audio_base = NULL;
 
// SysFS interface to test the Audio driver level API
ssize_t Brcm_auddrv_TestSysfs_show(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t Brcm_auddrv_TestSysfs_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t count);
static struct device_attribute Brcm_auddrv_Test_attrib = __ATTR(BrcmAud_DrvTest, 0644,  Brcm_auddrv_TestSysfs_show, Brcm_auddrv_TestSysfs_store);

static int HandleControlCommand();

static AUDDRV_PathID ULPathID = 0;
static AUDDRV_PathID DLPathID = 0;



static int HandlePlayCommand();
static int HandleCaptCommand();
static void AUDIO_DRIVER_TEST_InterruptPeriodCB(AUDIO_DRIVER_HANDLE_t drv_handle);
static void AUDIO_DRIVER_TEST_CaptInterruptPeriodCB(AUDIO_DRIVER_HANDLE_t drv_handle);

void dump_audio_registers();




static irqreturn_t audvoc_isr(int irq, void *dev_id);



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
   	int i=0;
	
	if(5!= sscanf(buf, "%d %d %d %d %d",&sgBrcm_auddrv_TestValues[0], &sgBrcm_auddrv_TestValues[1], &sgBrcm_auddrv_TestValues[2], &sgBrcm_auddrv_TestValues[3],&sgBrcm_auddrv_TestValues[4]))
	{
	
		DEBUG("\n<-Brcm_AudDrv_test SysFS Handler: test type =%s arg1=%d, arg2=%d, arg3=%d, arg4=%d \n", sgBrcm_auddrv_TestName[sgBrcm_auddrv_TestValues[0]],
											sgBrcm_auddrv_TestValues[1],
											sgBrcm_auddrv_TestValues[2],
											sgBrcm_auddrv_TestValues[3],
                                                                             sgBrcm_auddrv_TestValues[4]);
		DEBUG("error reading buf=%s count=%d\n", buf, count);
		return count;
	}

      switch(sgBrcm_auddrv_TestValues[0])
	{
	case 1: //Aud_play
        {
              DEBUG("I am in case 1 (Aud_play) -- test type =%s arg1=%d, arg2=%d, arg3=%d, arg4=%d \n", sgBrcm_auddrv_TestName[sgBrcm_auddrv_TestValues[0]-1],
											sgBrcm_auddrv_TestValues[1],
											sgBrcm_auddrv_TestValues[2],
											sgBrcm_auddrv_TestValues[3],
                                                                             sgBrcm_auddrv_TestValues[4]);
              HandlePlayCommand();
    	      break;
        }
       case 2: //Aud_rec
        {
              DEBUG("I am in case 2 (Aud_Rec) -- test type =%s arg1=%d, arg2=%d, arg3=%d, arg4=%d \n", sgBrcm_auddrv_TestName[sgBrcm_auddrv_TestValues[0]-1],
											sgBrcm_auddrv_TestValues[1],
											sgBrcm_auddrv_TestValues[2],
											sgBrcm_auddrv_TestValues[3],
                                                                             sgBrcm_auddrv_TestValues[4]);
             HandleCaptCommand();
             break;
        }
       case 3: //Aud_control
        {
             DEBUG("I am in case 3 (Aud_Control) -- test type =%s arg1=%d, arg2=%d, arg3=%d, arg4=%d \n", sgBrcm_auddrv_TestName[sgBrcm_auddrv_TestValues[0]-1],
											sgBrcm_auddrv_TestValues[1],
											sgBrcm_auddrv_TestValues[2],
											sgBrcm_auddrv_TestValues[3],
                                                                             sgBrcm_auddrv_TestValues[4]);
             HandleControlCommand();
             break;
        }
      default:
             DEBUG(" I am in Default case\n");
	}
	return count;
}

int BrcmCreateAuddrv_testSysFs(struct snd_card *card)
{
	int ret = 0;
	//create sysfs file for Aud Driver test control
	ret = snd_add_device_sysfs_file(SNDRV_DEVICE_TYPE_CONTROL,card,-1,&Brcm_auddrv_Test_attrib);
	//	DEBUG("BrcmCreateControlSysFs ret=%d", ret);	
	return ret;
}


#if 1
static void __iomem *mappedMem = NULL;
static int HandleControlCommand()
{
	AUDDRV_SPKR_Enum_t		spkr;
	AUDDRV_MIC_Enum_t		mic;
    switch(sgBrcm_auddrv_TestValues[1])
    {
        case 1:// Initialize the audio controller
        {
            DEBUG(" Audio Controller Init\n");
            AUDCTRL_Init ();
			//AUDDRV_HWControl_Init();
            DEBUG(" Audio Controller Init Complete\n");
        }
        break;
        case 2:// Start Hw loopback
        {
            DEBUG(" Audio Loopback start\n");
            //spkr = sgBrcm_auddrv_TestValues[2];
            //AUDCTRL_SetAudioLoopback(1,AUDCTRL_MIC_MAIN,spkr);
	    AUDDRV_SetAudioLoopback(TRUE, AUDDRV_MIC_ANALOG_MAIN, AUDDRV_SPKR_EP,0);
					
            DEBUG(" Audio loopback started\n");
        }
        break;
        case 3:// Dump registers
        {
            DEBUG(" Dump registers\n");
            //dump_audio_registers();
			{
				char MsgBuf[2408];

				// map the audio base
				/*if(mappedMem == NULL)
				{
					mappedMem = ioremap_nocache(KONA_HUB_CLK_BASE_VA, SZ_4K);
					ioremap_nocache(KONA_BINTC_BASE_VA, SZ_4K);
					ioremap_nocache(KONA_AUDIOH_BASE_VA, SZ_4K);
					ioremap_nocache(KONA_SDT_BASE_VA, SZ_4K);
					ioremap_nocache(KONA_SSP4_BASE_VA, SZ_4K);
					ioremap_nocache(KONA_SSP3_BASE_VA, SZ_4K);
					ioremap_nocache(KONA_SRCMIXER_BASE_VA, SZ_4K);
					ioremap_nocache(KONA_CFIFO_BASE_VA, SZ_4K);
					ioremap_nocache(KONA_AADMAC_BASE_VA, SZ_4K);
					ioremap_nocache(KONA_SSASW_BASE_VA, SZ_4K);
					ioremap_nocache(KONA_AHINTC_BASE_VA, SZ_4K);

					if (!mappedMem) {
						DEBUG("\n\r\t* Mapping audio base failed*\n\r");
						return;
					}
				}*/


				sprintf( MsgBuf, "0x35026800 =0x%08x, 0x3502c910 =0x%08x, 0x3502c990 =0x%08x, 0x3502c900 =0x%08x,0x3502cc20 =0x%08x,0x35025800 =0x%08x, 0x34000a34 =0x%08x, 0x340004b0 =0x%08x, 0x3400000c =0x%08x, 0x3400047c =0x%08x, 0x34000a40=0x%08x\n",
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

				DEBUG(MsgBuf);

				sprintf( MsgBuf, "0x3502f000 =0x%08x, 04 =0x%08x, 08 =0x%08x, 0c =0x%08x, 10 =0x%08x, 14 =0x%08x, 18 =0x%08x, 1c =0x%08x, 20 =0x%08x, 24 =0x%08x, 28 =0x%08x, 2c =0x%08x, 30 =0x%08x, 34 =0x%08x, 38 =0x%08x, 3c =0x%08x, 40 =0x%08x, 44 =0x%08x, 48 =0x%08x, 4c =0x%08x,50 =0x%08x, 54 =0x%08x, 58 =0x%08x, 5c =0x%08x \n",
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

				DEBUG(MsgBuf);

				if (sgBrcm_auddrv_TestValues[2] == 3)
				{
					UInt32 regAddr = 0x3502c000;
					while (regAddr <= 0x3502cc34)
					{
						sprintf( MsgBuf, "srcMixer: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
				}

				if (sgBrcm_auddrv_TestValues[2] == 2)
				{
					UInt32 regAddr = 0x3502F000;
					while (regAddr <= 0x3502F7FC)
					{
						sprintf( MsgBuf, "SSASW: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
				}

				if (sgBrcm_auddrv_TestValues[2] == 1)
				{
					UInt32 regAddr = 0x34000008;
					while (regAddr <= 0x34000024)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000030;
					while (regAddr <= 0x34000054)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000100;
					while (regAddr <= 0x34000100)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000200;
					while (regAddr <= 0x34000204)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000300;
					while (regAddr <= 0x34000304)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x3400030c;
					while (regAddr <= 0x3400030c)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000400;
					while (regAddr <= 0x34000400)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000408;
					while (regAddr <= 0x3400040C)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x3400041c;
					while (regAddr <= 0x3400041c)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000430;
					while (regAddr <= 0x34000430)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000438;
					while (regAddr <= 0x3400043c)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000444;
					while (regAddr <= 0x34000448)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000450;
					while (regAddr <= 0x34000450)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x3400045c;
					while (regAddr <= 0x34000474)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x3400047c;
					while (regAddr <= 0x34000484)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x3400048c;
					while (regAddr <= 0x34000490)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000498;
					while (regAddr <= 0x34000498)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x340004a0;
					while (regAddr <= 0x340004b0)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000a00;
					while (regAddr <= 0x34000a00)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000a08;
					while (regAddr <= 0x34000a0c)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000a1c;
					while (regAddr <= 0x34000a34)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000a40;
					while (regAddr <= 0x34000a40)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000E00;
					while (regAddr <= 0x34000e0c)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
						regAddr = 0x34000E20;
					while (regAddr <= 0x34000e28)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000E40;
					while (regAddr <= 0x34000e54)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000E74;
					while (regAddr <= 0x34000e74)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000E84;
					while (regAddr <= 0x34000e84)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000Ec0;
					while (regAddr <= 0x34000ec4)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
					regAddr = 0x34000ED4;
					while (regAddr <= 0x34000eD4)
					{
						sprintf( MsgBuf, "CLK: 0x%08x =0x%08x\n",
										regAddr,
										*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr)))
										);
						regAddr += 4;
						DEBUG(MsgBuf);
					}
				}
			}
            DEBUG(" Dump registers done \n");
        }
        break;

        case 4:// Enable telephony
        {
            DEBUG(" Enable telephony\n");
            AUDCTRL_EnableTelephony(AUDIO_HW_VOICE_IN,AUDIO_HW_VOICE_OUT,AUDCTRL_MIC_MAIN,AUDCTRL_SPK_HANDSET);
	    /*{
			AUDDRV_HWCTRL_CONFIG_t config;
           //DL
			config.streamID = AUDDRV_STREAM_NONE;
			config.pathID = 0;
			config.source = AUDDRV_DEV_DSP;
			config.sink = AUDDRV_DEV_EP; //AUDDRV_GetDRVDeviceFromSpkr(speaker);
			config.dmaCH = CSL_CAPH_DMA_NONE;    
			config.src_sampleRate = AUDIO_SAMPLING_RATE_8000; //sample_rate;
			config.snk_sampleRate = AUDIO_SAMPLING_RATE_48000;	
			config.chnlNum = AUDIO_CHANNEL_MONO;
			config.bitPerSample = AUDIO_24_BIT_PER_SAMPLE;

			DLPathID = AUDDRV_HWControl_EnablePath(config);

			//UL
			config.streamID = AUDDRV_STREAM_NONE;
			config.pathID = 0;
			config.source = AUDDRV_DEV_ANALOG_MIC; //AUDDRV_GetDRVDeviceFromMic(mic);
			config.sink = AUDDRV_DEV_DSP;
			config.dmaCH = CSL_CAPH_DMA_NONE;    
			config.src_sampleRate = AUDIO_SAMPLING_RATE_48000;
			config.snk_sampleRate = AUDIO_SAMPLING_RATE_8000; //sample_rate;	
			config.chnlNum = AUDIO_CHANNEL_MONO;
			config.bitPerSample = AUDIO_24_BIT_PER_SAMPLE;

			ULPathID = AUDDRV_HWControl_EnablePath(config);
		}*/		           
            DEBUG(" Telephony enabled \n");
        }
        break;
        case 5:// Disable telephony
        {
            DEBUG(" Disable telephony\n");
            AUDCTRL_DisableTelephony(AUDIO_HW_VOICE_IN,AUDIO_HW_VOICE_OUT,AUDCTRL_MIC_MAIN,AUDCTRL_SPK_HANDSET);   
			/*{
				AUDDRV_HWCTRL_CONFIG_t config;
				config.streamID = AUDDRV_STREAM_NONE;
				config.pathID = ULPathID;

				(void)AUDDRV_HWControl_DisablePath(config);
				ULPathID = 0;

				config.streamID = AUDDRV_STREAM_NONE;
				config.pathID = DLPathID;

				(void)AUDDRV_HWControl_DisablePath(config);
				DLPathID = 0;
			}*/
            DEBUG(" Telephony disabled \n");
        }
        break;
        case 6:// switch to EP
#if 0
	{
            UInt8* ahb_au_base;
            ahb_au_base = (UInt8*)ahb_audio_base;
        
            DEBUG(" switch to EP\n");

            *((volatile UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_INPUT_SEL_R_OFFSET)) = 0x0000;
            *((volatile UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_INPUT_SEL_R_OFFSET)) = 0x4003;
            *((volatile UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_ANACR0_OFFSET)) = 0x000060ca;
            
            DEBUG(" switch to EP done \n");
        }
#endif
        break;
#if 0
        case 7:// switch to speaker
        {
            UInt8* ahb_au_base;
            ahb_au_base = (UInt8*)ahb_audio_base;

            DEBUG(" switch to SP\n");

            *((volatile UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_INPUT_SEL_R_OFFSET)) = 0x4000;
            *((volatile UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_INPUT_SEL_R_OFFSET)) = 0x0003;
            
            *((volatile UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_ANACR0_OFFSET)) = 0x000060d4;
            
            DEBUG(" switch to SP done \n");
        }
#endif
        break;

		case 8:// peek a register
        {
			UInt32 regAddr = sgBrcm_auddrv_TestValues[2];
			DEBUG(" peek a register, 0x%08x\n", regAddr);
            DEBUG("		value = 0x%08x\n", *((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr))));
		}
		break;

		case 9:// poke a register
        {
			UInt32 regAddr = sgBrcm_auddrv_TestValues[2];
			UInt32 regVal = sgBrcm_auddrv_TestValues[3];
			*((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr))) = regVal;
            DEBUG(" poke a register, 0x%08x = 0x%08x\n", regAddr, *((volatile UInt32 *) (HW_IO_PHYS_TO_VIRT(regAddr))));
		}
		break;

        default:
            DEBUG(" Invalid Control Command\n");
    }
}
#endif

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
    static AUDCTRL_SPEAKER_t     spkr;
    static int src_used=0;
    char* src;
    char* dest;

    //temp
    int ret, irq; 

    switch(sgBrcm_auddrv_TestValues[1])
    {
        case 1://open the plyabck device
        {
            DEBUG(" Audio DDRIVER Open\n");
            drv_handle = AUDIO_DRIVER_Open(AUDIO_DRIVER_PLAY_AUDIO);
            DEBUG(" Audio DDRIVER Open Complete\n");
        }
        break;
	case 2:
	{
	    src_used = 0;
            if(sgBrcm_auddrv_TestValues[2] == 0) // default
            {
            	if(sgBrcm_auddrv_TestValues[3] == 0) // default
		{
                	DEBUG(" Playback of pre-defined sample 48KHz Mono\n");
                	samplePCM16_inaudiotest = (char *)playback_audiotest;
		}
		else if(sgBrcm_auddrv_TestValues[3] == 1)
		{
                	DEBUG(" Playback of pre-defined sample 8KHz Mono : use HW SRC mixer\n");
                	samplePCM16_inaudiotest = (char *)playback_audiotest_srcmixer;
			src_used = 1;
		}	
		
            }
            else if(sgBrcm_auddrv_TestValues[2] == 1)
            {
                if(record_test_buf != NULL)
                {
                	DEBUG(" Playback of recorded data\n");
		        samplePCM16_inaudiotest = (char *)record_test_buf;
                }
		else
                        DEBUG(" record buffer freed: record data to play\n");
            }

	}
	break;
	case 3:
        {
            DEBUG(" Audio DDRIVER Config\n");
            //set the callback
            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_CB,(void*)AUDIO_DRIVER_TEST_InterruptPeriodCB);

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

            DEBUG("Config:sr=%d nc=%d bs=%d \n",drv_config.sample_rate,drv_config.num_channel,drv_config.bits_per_sample);
            
            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_CONFIG,(void*)&drv_config);

            period_ms = 100;
            if(sgBrcm_auddrv_TestValues[4] != 0)
                period_ms = sgBrcm_auddrv_TestValues[4];

            //set the interrupt period
            period_bytes = period_ms * (drv_config.sample_rate/1000) * (drv_config.num_channel) * 2;
	    	num_blocks = 2; // for RHEA
            DEBUG("Period: ms=%d bytes=%d blocks:%d\n",period_ms,period_bytes,num_blocks);
            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_INT_PERIOD,(void*)&period_bytes);

            buf_param.buf_size = PCM_TEST_MAX_PLAYBACK_BUF_BYTES;
            buf_param.pBuf = dma_alloc_coherent (NULL, buf_param.buf_size, &dma_addr,GFP_KERNEL);
            if(buf_param.pBuf == NULL)
            {
                DEBUG("Cannot allocate Buffer \n");
                return;
            }
            buf_param.phy_addr = (UInt32)dma_addr;

            DEBUG("virt_addr = 0x%x phy_addr=0x%x\n",buf_param.pBuf,(UInt32)dma_addr);

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
           
	    DEBUG("copy_bytes %d",copy_bytes); 
	        //set the buffer params
	    AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_BUF_PARAMS,(void*)&buf_param);
            DEBUG(" Audio DDRIVER Config Complete\n");
        }
        break;
        case 4: //Start the playback
            {
		AUDDRV_DEVICE_e aud_dev = AUDDRV_DEV_EP; // EP is default now

                DEBUG(" Start Playback\n");
                spkr = sgBrcm_auddrv_TestValues[2];

                AUDCTRL_SaveAudioModeFlag(spkr);

		if (spkr == 0)// for rhea
		{
			// earpiece
			aud_dev = AUDDRV_DEV_EP;  
		}
		else  if (spkr == 1)// for rhea
		{
			// headset
			aud_dev = AUDIO_HW_HEADSET_OUT; 
		}
		else  if ((spkr == 2) || (spkr == 4)) // for rhea
		{
			// ihf
			aud_dev = AUDIO_HW_IHF_OUT;  
		}	
                AUDCTRL_EnablePlay(AUDIO_HW_MEM,
                                   AUDIO_HW_AUDIO_OUT,
                                   AUDIO_HW_NONE,
                                   spkr,
		                   drv_config.num_channel,
                                   drv_config.sample_rate
				                    );

              	AUDCTRL_SetPlayVolume (AUDIO_HW_AUDIO_OUT, // for Audio 
   		     			spkr, 
    				   	AUDIO_GAIN_FORMAT_VOL_LEVEL, 
				   	0x001E, 0x001E); // 0x1E is 30 decimal. 0x001E for both L and R channels.


                AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_START,&aud_dev);
                DEBUG("Playback started\n");

		//  Need to implement some sync mechanism
	        OSTASK_Sleep(5000); 
                DEBUG(" Stop playback\n");

                AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_STOP,NULL);

                //disable the playback path
                AUDCTRL_DisablePlay(AUDIO_HW_MEM,AUDIO_HW_AUDIO_OUT,spkr);

                AUDIO_DRIVER_Close(drv_handle);

            }
            break;
        default:
            DEBUG(" Invalid Playback Command\n");
    }

}

static int count_int = 0;
static void AUDIO_DRIVER_TEST_InterruptPeriodCB(AUDIO_DRIVER_HANDLE_t drv_handle)
{
    char* src;
    char* dest;

    //DEBUG(" %lx: Playback Interrupt- jiffies=%d count_int=%d\n",jiffies,count_int++);

    if((current_ipbuffer_index + period_bytes) >= TEST_BUF_SIZE)
        current_ipbuffer_index = 0;

    src = ((char*)samplePCM16_inaudiotest) + current_ipbuffer_index;
    dest = buf_param.pBuf + dma_buffer_write_index;

    memcpy(dest,src,period_bytes);

    current_ipbuffer_index += period_bytes;
    dma_buffer_write_index += period_bytes;

    if(dma_buffer_write_index >= (num_blocks * period_bytes))
        dma_buffer_write_index = 0;
    //DEBUG(" current_ipbuffer_index %d: dma_buffer_write_index- %d\n",current_ipbuffer_index,dma_buffer_write_index);
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
    
    unsigned long copy_bytes;
    static AUDIO_DRIVER_HANDLE_t drv_handle = NULL;
    //static AUDIO_DRIVER_BUFFER_t buf_param;
    static AUDIO_DRIVER_CONFIG_t drv_config;
    static dma_addr_t            dma_addr;
    static AUDCTRL_MICROPHONE_t     mic = AUDCTRL_MIC_MAIN;
 
   
    static AUDIO_DRIVER_TYPE_t drv_type = AUDIO_DRIVER_CAPT_HQ;

    static Boolean record_buf_allocated = 0;
    //temp
    int ret, irq;


    switch(sgBrcm_auddrv_TestValues[1])
    {
        case 1://open the capture device
        {
            if(sgBrcm_auddrv_TestValues[2] != 0)
            	drv_type = sgBrcm_auddrv_TestValues[2];
            DEBUG(" Audio Capture DDRIVER Open drv_type %d\n",drv_type);
            drv_handle = AUDIO_DRIVER_Open(drv_type);
            DEBUG(" Audio DDRIVER Open Complete\n");

        }
        break;
        case 2://configure capture device
        {
            DEBUG(" Audio Capture DDRIVER Config\n");
            //set the callback
            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_CB,(void*)AUDIO_DRIVER_TEST_CaptInterruptPeriodCB);

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

            DEBUG("Config:sr=%d nc=%d bs=%d \n",drv_config.sample_rate,drv_config.num_channel,drv_config.bits_per_sample);
            
            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_CONFIG,(void*)&drv_config);

            period_ms = 100;
            if(sgBrcm_auddrv_TestValues[4] != 0)
                period_ms = sgBrcm_auddrv_TestValues[4];

            //set the interrupt period
            capt_period_bytes = period_ms * (drv_config.sample_rate/1000) * (drv_config.num_channel) * 2;
            capt_num_blocks =  2; // limitation for RHEA

            DEBUG("Period: ms=%d bytes=%d blocks:%d\n",period_ms,capt_period_bytes,capt_num_blocks);
            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_INT_PERIOD,(void*)&capt_period_bytes);

	    current_capt_buffer_index = 0;
	    capt_dma_buffer_read_index = 0;

            capt_buf_param.buf_size = PCM_TEST_MAX_CAPTURE_BUF_BYTES;
            capt_buf_param.pBuf = dma_alloc_coherent (NULL, capt_buf_param.buf_size, &dma_addr,GFP_KERNEL);
            if(capt_buf_param.pBuf == NULL)
            {
                DEBUG("Cannot allocate Buffer \n");
                return;
            }
            capt_buf_param.phy_addr = (UInt32)dma_addr;

            DEBUG("virt_addr = 0x%x phy_addr=0x%x\n",capt_buf_param.pBuf,(UInt32)dma_addr);

            memset(capt_buf_param.pBuf,0,PCM_TEST_MAX_CAPTURE_BUF_BYTES);
            //set the buffer params
            AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_SET_BUF_PARAMS,(void*)&capt_buf_param);

            DEBUG(" Audio DDRIVER Config Complete\n");
        }
        break;
        case 3: //Start the capture
            {
                AUDIO_HW_ID_t           hw_id = AUDIO_HW_AUDIO_IN;
		AUDDRV_DEVICE_e aud_dev = AUDDRV_DEV_ANALOG_MIC;

		if(!record_buf_allocated)
		{	
			record_test_buf = kmalloc(TEST_BUF_SIZE,GFP_KERNEL);
			memset(record_test_buf,0,TEST_BUF_SIZE);
			record_buf_allocated = 1;
                }
		if(sgBrcm_auddrv_TestValues[2] != 0) 
			mic = sgBrcm_auddrv_TestValues[2];

                if(drv_type == AUDIO_DRIVER_CAPT_VOICE)
                {
                    hw_id = AUDIO_HW_VOICE_IN;
                }
		DEBUG(" Start capture mic %d\n",mic);

                AUDCTRL_EnableRecord(hw_id,
				     AUDIO_HW_MEM,
                                     mic,
				     drv_config.num_channel,
                                     drv_config.sample_rate);

		if(mic == 1) // for RHEA
		{
			aud_dev = AUDDRV_DEV_ANALOG_MIC;
		}
	    // need to add other values
                AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_START,&aud_dev);
                
                DEBUG("capture started\n");
		
		OSTASK_Sleep(5000);

                DEBUG(" Stop capture\n");

                AUDIO_DRIVER_Ctrl(drv_handle,AUDIO_DRIVER_STOP,NULL);

                AUDCTRL_DisableRecord(hw_id,AUDIO_HW_MEM,mic);

		DEBUG("capture stopped\n");

            }
            break;

	case 4:
		// free the buffer record_test_buf
            	DEBUG(" Freed the recorded test buf \n");	
                kfree(record_test_buf);
		record_buf_allocated = 0;
		record_test_buf = NULL;
	    break;
        default:
            DEBUG(" Invalid capture Command\n");
    }

}

static int capt_count_int = 0;
static void AUDIO_DRIVER_TEST_CaptInterruptPeriodCB(AUDIO_DRIVER_HANDLE_t drv_handle)
{
    char* src;
    char* dest;

    //DEBUG(" %lx: capture Interrupt- %d\n",jiffies,capt_count_int++);

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

    //DEBUG(" current_capt_buffer_index %d: capt_dma_buffer_read_index- %d\n",current_capt_buffer_index,capt_dma_buffer_read_index);

    return;
}

#if 0
// Test code 

//#define AHB_AUDIO_BASE_ADDR      0x30800000 
#define SIZE_64K 0xffff
//#define DSP_AUDIO_AIFIFODATA0_R_OFFSET                                    0x00000400
//#define DSP_AUDIO_AIFIFODATA1_R_OFFSET                                    0x00000404
//#define DSP_AUDIO_STEREOAUDMOD_R_OFFSET                                   0x00000FA0


void dump_audio_registers()
{
    UInt32* fifo_addr;
    UInt16* saudm_addr;
    UInt32 cnt=0;
    char MsgBuf[128];
    UInt8* ahb_au_base;

    // map the audio base
    if(ahb_audio_base == NULL)
    {
        ahb_audio_base = ioremap_nocache(AHB_AUDIO_BASE_ADDR, SIZE_64K);
        if (!ahb_audio_base) {
            DEBUG("\n\r\t* Mapping ahb_audio base failed*\n\r");
            return;
        }
    }
    ahb_au_base = (UInt8*)ahb_audio_base;


#if 0
    sprintf( MsgBuf, "ANACR0 =0x%08x, ANACR1= 0x%08x, ANACR2 =0x%08x, ANACR3 =0x%08x, ANACR10 =0x%08x ANACR12 =0x%08x ",
						*((UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_ANACR0_OFFSET)),
						*((UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_ANACR1_OFFSET)),
						*((UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_ANACR2_OFFSET)),
						*((UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_ANACR3_OFFSET)),
						*((UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_ANACR10_OFFSET)),
                        *((UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_ANACR12_OFFSET)));

            sprintf( MsgBuf, "ANACR0 =0x%08x, ANACR3 =0x%08x, ANACR10 =0x%08x ",
						*((UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_ANACR0_OFFSET)),
						*((UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_ANACR3_OFFSET)),
						*((UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_ANACR10_OFFSET)));
			DEBUG(MsgBuf);
#endif


            sprintf( MsgBuf, "SYSCFG_DSPCTRL  =0x%04x ", 	*((UInt32 *) (SYSCFG_BASE_ADDR+SYSCFG_DSPCTRL_OFFSET)));
			DEBUG(MsgBuf);


			sprintf( MsgBuf, "AMCR =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_AMCR_R_OFFSET)));
			DEBUG(MsgBuf);


#if 0			
			sprintf( MsgBuf, "VINPATH_CTRL =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_VINPATH_CTRL_OFFSET)));
			DEBUG(MsgBuf);
#endif
			sprintf( MsgBuf, "AUDIOINPATH_CTRL =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_AUDIOINPATH_CTRL_R_OFFSET)));
			DEBUG(MsgBuf);
			sprintf( MsgBuf, "VOICEFIFO_STATUS =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_VOICEFIFO_STATUS_R_OFFSET)));
			DEBUG(MsgBuf);
			sprintf( MsgBuf, "AUDIOLOOPBACK_CTRL =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_AUDIOLOOPBACK_CTRL_R_OFFSET)));
			DEBUG(MsgBuf);
			
			sprintf( MsgBuf, "VCFGR =0x%04x, VMUT =0x%04x, VSLOPGAIN =0x%04x ",
						*((UInt16 *) (ahb_au_base+DSP_AUDIO_VCFGR_R_OFFSET)),
						*((UInt16 *) (ahb_au_base+DSP_AUDIO_VMUT_R_OFFSET)),
						*((UInt16 *) (ahb_au_base+DSP_AUDIO_VSLOPGAIN_R_OFFSET)) );
			DEBUG(MsgBuf);

			sprintf( MsgBuf, "ADCCONTROL =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_ADCCONTROL_R_OFFSET)));
			DEBUG(MsgBuf);

			sprintf( MsgBuf, "POLYAUDMOD =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_POLYAUDMOD_R_OFFSET)) );
			DEBUG(MsgBuf);
			sprintf( MsgBuf, "PIFIFOST =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_PIFIFOST_R_OFFSET)) );
			DEBUG(MsgBuf);
			sprintf( MsgBuf, "PLRCH =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_PLRCH_R_OFFSET)) );
			DEBUG(MsgBuf);
			sprintf( MsgBuf, "PLSLOPGAIN =0x%04x, PRSLOPGAIN =0x%04x ",
						*((UInt16 *) (ahb_au_base+DSP_AUDIO_PLSLOPGAIN_R_OFFSET)),
						*((UInt16 *) (ahb_au_base+DSP_AUDIO_PRSLOPGAIN_R_OFFSET)) );
			DEBUG(MsgBuf);
			sprintf( MsgBuf, "PEQPATHGAIN1 =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_PEQPATHGAIN1_R_OFFSET)) );
			DEBUG(MsgBuf);

			sprintf( MsgBuf, "STEREOAUDMOD =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_STEREOAUDMOD_R_OFFSET)) );
			DEBUG(MsgBuf);
			sprintf( MsgBuf, "AIFIFOST =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_AIFIFOST_R_OFFSET)) );
			DEBUG(MsgBuf);
			sprintf( MsgBuf, "ALRCH =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_ALRCH_R_OFFSET)) );
			DEBUG(MsgBuf);
			sprintf( MsgBuf, "ALSLOPGAIN =0x%04x, ARSLOPGAIN =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_ALSLOPGAIN_R_OFFSET)), *((UInt16 *) (ahb_au_base+DSP_AUDIO_ARSLOPGAIN_R_OFFSET)) );
			DEBUG(MsgBuf);
			sprintf( MsgBuf, "AEQPATHGAIN1 =0x%04x, AEQPATHGAIN2 =0x%04x ", *((UInt16 *) (ahb_au_base+DSP_AUDIO_AEQPATHGAIN1_R_OFFSET)), *((UInt16 *) (ahb_au_base+DSP_AUDIO_AEQPATHGAIN2_R_OFFSET)) );
			DEBUG(MsgBuf);

/**
08 unsigned VOICE_IN_ONLY_INT 1: Voice input only mode interrupt flag, Write '0' to this bit will clean the flag, Writing '1' does nothing to the flag (Type: R/W)
Reset value is 0x0. 
07 unsigned BTTAP_NB_INT 1: BT Taps narrow band interrupt flag, Write '0' to this bit will clean the flag, Writing '1' does nothing to the flag (Type: R/W)
Reset value is 0x0. 
06 unsigned BTTAP_WB_INT 1: BT Taps wide band interrupt flag, Write '0' to this bit will clean the flag, Writing '1' does nothing to the flag (Type: R/W)
Reset value is 0x0. 
05 unsigned Reserved Reserved bit must be written with 0.  A read returns an unknown value. 
04 unsigned POLYRINGER_INT 1: DAC Polyringer path interrupt flag, Write '0' to this bit will clean the flag, Writing '1' does nothing to the flag (Type: R/W)
Reset value is 0x0. 
03 unsigned ADC_AUDIO_INT 1: ADC Audio path interrupt flag, Write '0' to this bit will clean the flag, Writing '1' does nothing to the flag (Type: R/W)
Reset value is 0x0. 
02 unsigned DAC_AUDIO_INT 1: DAC Audio path interrupt flag, Write '0' to this bit will clean the flag, Writing '1' does nothing to the flag (Type: R/W)
Reset value is 0x0. 
01 unsigned VOICE_INT 1: Voice band interrupt flag, Write '0' to this bit will clean the flag, Writing '1' does nothing to the flag (Type: R/W)
Reset value is 0x0. 
00 unsigned PCM_INT 1: PCM interrupt flag, Write '0' to this bit will clean the flag, Writing '1' does nothing to the flag (Type: R/W)
Reset value is 0x0. 
*/
			sprintf( MsgBuf, "AUDVOC_ISR =0x%04x ", *((UInt16 *)(ahb_au_base+DSP_AUDIO_AUDVOC_ISR_R_OFFSET)) );
			DEBUG(MsgBuf);

			sprintf( MsgBuf, "MIXER_INPUT_SEL =0x%04x ", *((UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_INPUT_SEL_R_OFFSET)) );
			DEBUG(MsgBuf);

			*((UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_GAIN_CHSEL_R_OFFSET)) = 0;
			sprintf( MsgBuf, "MIXER1_GAIN =0x%04x ", *((UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_GAIN_ADJUST_R_OFFSET)) );
			DEBUG(MsgBuf);
			*((UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_GAIN_CHSEL_R_OFFSET)) = 1;
			sprintf( MsgBuf, "MIXER2_GAIN =0x%04x ", *((UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_GAIN_ADJUST_R_OFFSET)) );
			DEBUG(MsgBuf);
			*((UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_GAIN_CHSEL_R_OFFSET) ) = 2;
			sprintf( MsgBuf, "MIXER3_GAIN =0x%04x ", *((UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_GAIN_ADJUST_R_OFFSET)) );
			DEBUG(MsgBuf);
			*((UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_GAIN_CHSEL_R_OFFSET) ) = 3;
			sprintf( MsgBuf, "MIXER4_GAIN =0x%04x ", *((UInt16 *)(ahb_au_base+DSP_AUDIO_MIXER_GAIN_ADJUST_R_OFFSET)) );
			DEBUG(MsgBuf);

			sprintf( MsgBuf, "BIQUAD_CFG =0x%04x ", *((UInt16 *)(ahb_au_base+DSP_AUDIO_BIQUAD_CFG_R_OFFSET)) );
			DEBUG(MsgBuf);

			sprintf( MsgBuf, "AHB_AUDIO_BTMIXER_CFG_R =0x%04x,  AUDIO_BTMIXER_CFG2_R ==0x%04x, AHB_AUDIO_BTMIXER_GAIN_L_R =0x%04x, AHB_AUDIO_BTMIXER_GAIN_R_R =0x%04x ", 
				*((UInt16 *)(ahb_au_base+DSP_AUDIO_BTMIXER_CFG_R_OFFSET) ), *((UInt16 *)(ahb_au_base+DSP_AUDIO_BTMIXER_CFG2_R_OFFSET)),
				*((UInt16 *)(ahb_au_base+DSP_AUDIO_BTMIXER_GAIN_L_R_OFFSET) ), *((UInt16 *)(ahb_au_base+DSP_AUDIO_BTMIXER_GAIN_R_R_OFFSET))
				);
			DEBUG(MsgBuf);


#if 0

    fifo_addr = (UInt32*)((UInt8*)(ahb_audio_base) + DSP_AUDIO_AIFIFODATA0_R_OFFSET);
    saudm_addr = (UInt16*)((UInt8*)(ahb_audio_base) + DSP_AUDIO_STEREOAUDMOD_R_OFFSET);

    DEBUG ("saudm_addr=0x%x\n",saudm_addr);
    DEBUG ("saudm_val=0x%x\n",*(saudm_addr));
    
    DEBUG ("fifo_addr=0x%x\n",fifo_addr);
    DEBUG ("val%d=0x%x\n",cnt,*(fifo_addr));

#endif
#if 0
    for(cnt = 0; cnt < 128;cnt++)
    {
        DEBUG ("val%d=0x%lx\n",cnt,*(fifo_addr+cnt));

    }
#endif
   return;
}
#define FIFO_THRESHOLD  64  //for interrupt. 
#define FIFO_FILL_SIZE  67    //for fifo write,  can not be larger than emtry_cnt (approx. FIFO_THRESHOLD).

static int index = 0;
static irqreturn_t audvoc_isr(int irq, void *dev_id)
{
     UInt8* ahb_au_base;

     //DEBUG("auvoc_isr \n");
   
   DEBUG(" %lx: auvoc_isr- %d\n",jiffies,count_int++);
   if(ahb_audio_base != NULL)
   {
        ahb_au_base = (UInt8*)ahb_audio_base;
   }
    
    //DEBUG("ahb_au_base- 0x%x \n",ahb_au_base);
  
    if( *(volatile UInt16 *)(ahb_au_base + DSP_AUDIO_AUDVOC_ISR_R_OFFSET) & (0x04) )
	{
//        DEBUG("clear_register for audio \n");
	    //clear audio INT flag. Problem: But no more INT.
	    *(volatile UInt16 *)(ahb_au_base + DSP_AUDIO_AUDVOC_ISR_R_OFFSET) &= ~(0x04);
	}

    if( *(volatile UInt16 *)(ahb_au_base + DSP_AUDIO_AUDVOC_ISR_R_OFFSET) & (0x02) )
	{
        DEBUG("clear_register for voice \n");
	    //clear audio INT flag. Problem: But no more INT.
	    *(volatile UInt16 *)(ahb_au_base + DSP_AUDIO_AUDVOC_ISR_R_OFFSET) &= ~(0x02);
	}

    if( (index + (FIFO_FILL_SIZE * 2)) >= (sizeof(hqAudioTestBuf)/2))
        index = 0;
    
    // Fill the fifo
    chal_audioaopath_WriteFifo (NULL, &hqAudioTestBuf[index], FIFO_FILL_SIZE);
    index += (FIFO_FILL_SIZE * 2);

	return IRQ_HANDLED;
}
	        
#endif	        
