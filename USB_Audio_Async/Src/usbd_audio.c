/**
  ******************************************************************************
  * @file    usbd_audio.c
  * @author  JM. Fourneropn
  * @version V0.1
  * @date    27/08/2016
  * @brief   This file provides the Audio core functions with explicit Audio feedback.
  *
  * @verbatim
  *      
  *          ===================================================================      
  *                                AUDIO Class  Description
  *          ===================================================================
 *           This driver manages the Audio Class 1.0 following the "USB Device Class Definition for
  *           Audio Devices V1.0 Mar 18, 98".
  *           This driver implements the following aspects of the specification:
  *             - Device descriptor management
  *             - Configuration descriptor management
  *             - Standard AC Interface Descriptor management
  *             - 1 Audio Streaming Interface (with single channel, PCM, Stereo mode)
  *             - 1 Audio Streaming Endpoint
  *             - 1 Audio Terminal Input (1 channel)
  *             - Audio Class-Specific AC Interfaces
  *             - Audio Class-Specific AS Interfaces
  *             - AudioControl Requests: only SET_CUR and GET_CUR requests are supported (for Mute)
  *             - Audio Feature Unit (limited to Mute control)
  *             - Audio Synchronization type: Asynchronous with explict feedback
  *             - Single fixed audio sampling rate (configurable in usbd_conf.h file)
  *          The current audio class version supports the following audio features:
  *             - Pulse Coded Modulation (PCM) format
  *             - sampling rate: 48KHz. 
  *             - Bit resolution: 16
  *             - Number of channels: 2
  *             - No volume control
  *             - Mute/Unmute capability
  *             - Asynchronous Endpoints 
  *          
  * @note     In HS mode and when the DMA is used, all variables and data structures
  *           dealing with the DMA during the transaction process should be 32-bit aligned.
  *           
  *      
  *  @endverbatim
  *
  * Licence: GPL v3
  *
  * Software is based on MCD Application Team version V2.4.2 11-December-2015 and Work from
  * Roman: http://we.easyelectronics.ru/electro-and-pc/asinhronnoe-usb-audio-na-stm32.html
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2015 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */ 

/* Includes ------------------------------------------------------------------*/
#include "usbd_audio.h"
#include "usbd_desc.h"
#include "usbd_ctlreq.h"
//?? for debug purpose for LEDS - to suppress to remove depndencies
#include "stm32f4_discovery.h"


/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */


/** @defgroup USBD_AUDIO
  * @brief usbd core module
  * @{
  */

/** @defgroup USBD_AUDIO_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_AUDIO_Private_Defines
  * @{
  */

/**
  * @}
  */


/** @defgroup USBD_AUDIO_Private_Macros
  * @{
  */
#define AUDIO_SAMPLE_FREQ(frq)      (uint8_t)(frq), (uint8_t)((frq >> 8)), (uint8_t)((frq >> 16))

// ## wMaxPacketSize in Bytes ((Freq(Samples)+1)*2(Stereo)*2(HalfWord)) <= added +1 sample = +4 bytes
#define AUDIO_PACKET_SZE(frq)          (uint8_t)(((frq * 2 * 2)/1000) & 0xFF)+4, \
                                       (uint8_t)((((frq * 2 * 2)/1000) >> 8) & 0xFF)


#define SOF_RATE 0x2
//## static volatile uint32_t  usbd_audio_AltSet = 0;
static volatile  uint16_t SOF_num=0;
static volatile uint8_t flag=0;
//static volatile uint8_t dpid;
//volatile int32_t corr,oldgap,dgap,tmpxx;
//volatile int32_t maxgap, mingap;
int8_t shift = 0;
uint8_t tmpbuf[AUDIO_OUT_PACKET*2+16] __attribute__ ((aligned(4)));


volatile uint32_t feedback_data __attribute__ ((aligned(4))) = 0x0C0000; //corresponds to 48 in 10.14 fixed point format
uint32_t accum __attribute__ ((aligned(4))) = 0x0C0000; //corresponds to 48 in 10.14 fixed point format
uint8_t feedbacktable[3];


// ## static uint32_t PlayFlag = 0;

/**
  * @}
  */




/** @defgroup USBD_AUDIO_Private_FunctionPrototypes
  * @{
  */


static uint8_t  USBD_AUDIO_Init (USBD_HandleTypeDef *pdev,
                               uint8_t cfgidx);

static uint8_t  USBD_AUDIO_DeInit (USBD_HandleTypeDef *pdev,
                                 uint8_t cfgidx);

static uint8_t  USBD_AUDIO_Setup (USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req);

static uint8_t  *USBD_AUDIO_GetCfgDesc (uint16_t *length);

static uint8_t  *USBD_AUDIO_GetDeviceQualifierDesc (uint16_t *length);

static uint8_t  USBD_AUDIO_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum);

static uint8_t  USBD_AUDIO_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum);

static uint8_t  USBD_AUDIO_EP0_RxReady (USBD_HandleTypeDef *pdev);

static uint8_t  USBD_AUDIO_EP0_TxReady (USBD_HandleTypeDef *pdev);

static uint8_t  USBD_AUDIO_SOF (USBD_HandleTypeDef *pdev);

static uint8_t  USBD_AUDIO_IsoINIncomplete (USBD_HandleTypeDef *pdev, uint8_t epnum);

static uint8_t  USBD_AUDIO_IsoOutIncomplete (USBD_HandleTypeDef *pdev, uint8_t epnum);

static void AUDIO_REQ_GetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);

static void AUDIO_REQ_SetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);

/**
  * @}
  */

/** @defgroup USBD_AUDIO_Private_Variables
  * @{
  */

USBD_ClassTypeDef  USBD_AUDIO =
{
  USBD_AUDIO_Init,
  USBD_AUDIO_DeInit,
  USBD_AUDIO_Setup,
  USBD_AUDIO_EP0_TxReady,
  USBD_AUDIO_EP0_RxReady,
  USBD_AUDIO_DataIn,
  USBD_AUDIO_DataOut,
  USBD_AUDIO_SOF,
  USBD_AUDIO_IsoINIncomplete,
  USBD_AUDIO_IsoOutIncomplete,
  USBD_AUDIO_GetCfgDesc,
  USBD_AUDIO_GetCfgDesc,
  USBD_AUDIO_GetCfgDesc,
  USBD_AUDIO_GetDeviceQualifierDesc,
};

/* USB AUDIO device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_AUDIO_CfgDesc[USB_AUDIO_CONFIG_DESC_SIZ] __ALIGN_END =
{
  /* Configuration 1 */
  0x09,                                 /* bLength */
  USB_DESC_TYPE_CONFIGURATION,          /* bDescriptorType */
  LOBYTE(USB_AUDIO_CONFIG_DESC_SIZ),    /* wTotalLength  109 bytes*/
  HIBYTE(USB_AUDIO_CONFIG_DESC_SIZ),
  0x02,                                 /* bNumInterfaces */
  0x01,                                 /* bConfigurationValue */
  0x00,                                 /* iConfiguration */
  0xC0,                                 /* bmAttributes  BUS Powred*/
  0x32,                                 /* bMaxPower = 100 mA*/
  /* 09 byte*/

  /* USB Speaker Standard interface descriptor */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  USB_DESC_TYPE_INTERFACE,              /* bDescriptorType */
  0x00,                                 /* bInterfaceNumber */
  0x00,                                 /* bAlternateSetting */
  0x00,                                 /* bNumEndpoints */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOCONTROL,          /* bInterfaceSubClass */
  AUDIO_PROTOCOL_UNDEFINED,             /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/

  /* USB Speaker Class-specific AC Interface Descriptor */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_HEADER,                 /* bDescriptorSubtype */
  0x00,          /* 1.00 */             /* bcdADC */
  0x01,
  0x27,                                 /* wTotalLength = 39*/
  0x00,
  0x01,                                 /* bInCollection */
  0x01,                                 /* baInterfaceNr */
  /* 09 byte*/

  /* USB Speaker Input Terminal Descriptor */
  AUDIO_INPUT_TERMINAL_DESC_SIZE,       /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_INPUT_TERMINAL,         /* bDescriptorSubtype */
  0x01,                                 /* bTerminalID */
  0x01,                                 /* wTerminalType AUDIO_TERMINAL_USB_STREAMING   0x0101 */
  0x01,
  0x00,                                 /* bAssocTerminal */
  0x02,                                 /* bNrChannels */
  0x03,                                 /* wChannelConfig 0x03 Stereo */
  0x00,
  0x00,                                 /* iChannelNames */
  0x00,                                 /* iTerminal */
  /* 12 byte*/

  /* USB Speaker Audio Feature Unit Descriptor */
  0x09,                                 /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_FEATURE_UNIT,           /* bDescriptorSubtype */
  AUDIO_OUT_STREAMING_CTRL,             /* bUnitID */
  0x01,                                 /* bSourceID */
  0x01,                                 /* bControlSize */
  AUDIO_CONTROL_MUTE,// |AUDIO_CONTROL_VOLUME, /* bmaControls(0) */
  0,                                    /* bmaControls(1) */
  0x00,                                 /* iTerminal */
  /* 09 byte*/

  /*USB Speaker Output Terminal Descriptor */
  0x09,      /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_OUTPUT_TERMINAL,        /* bDescriptorSubtype */
  0x03,                                 /* bTerminalID */
  0x01,                                 /* wTerminalType  0x0301*/
  0x03,
  0x00,                                 /* bAssocTerminal */
  0x02,                                 /* bSourceID */
  0x00,                                 /* iTerminal */
  /* 09 byte*/

  /* USB Speaker Standard AS Interface Descriptor - Audio Streaming Zero Bandwith */
  /* Interface 1, Alternate Setting 0                                             */
  AUDIO_INTERFACE_DESC_SIZE,  /* bLength */
  USB_DESC_TYPE_INTERFACE,        /* bDescriptorType */
  0x01,                                 /* bInterfaceNumber */
  0x00,                                 /* bAlternateSetting */
  0x00,                                 /* bNumEndpoints */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOSTREAMING,        /* bInterfaceSubClass */
  AUDIO_PROTOCOL_UNDEFINED,             /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/

  /* USB Speaker Standard AS Interface Descriptor - Audio Streaming Operational */
  /* Interface 1, Alternate Setting 1                                           */
  AUDIO_INTERFACE_DESC_SIZE,  /* bLength */
  USB_DESC_TYPE_INTERFACE,        /* bDescriptorType */
  0x01,                                 /* bInterfaceNumber */
  0x01,                                 /* bAlternateSetting */
  0x02,                                 /* ## bNumEndpoints - Audio Out and Feedback Endpoint */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOSTREAMING,        /* bInterfaceSubClass */
  AUDIO_PROTOCOL_UNDEFINED,             /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/

  /* USB Speaker Audio Streaming Interface Descriptor */
  AUDIO_STREAMING_INTERFACE_DESC_SIZE,  /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_STREAMING_GENERAL,              /* bDescriptorSubtype */
  0x01,                                 /* bTerminalLink */
  0x01,                                 /* bDelay */
  0x01,                                 /* wFormatTag AUDIO_FORMAT_PCM  0x01*/
  0x00,
  /* 07 byte*/

  /* USB Speaker Audio Type III Format Interface Descriptor */
  0x0B,                                 /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_STREAMING_FORMAT_TYPE,          /* bDescriptorSubtype */
  AUDIO_FORMAT_TYPE_I,                  /* bFormatType <= Changed to AUDIO_FORMAT_TYPE_I */
  0x02,                                 /* bNrChannels */
  0x02,                                 /* bSubFrameSize :  2 Bytes per frame (16bits) */
  16,                                   /* bBitResolution (16-bits per sample) */
  0x01,                                 /* bSamFreqType only one frequency supported */
  AUDIO_SAMPLE_FREQ(USBD_AUDIO_FREQ),         /* Audio sampling frequency coded on 3 bytes */
  /* 11 byte*/

  /* Endpoint 1 - Standard Descriptor */
  AUDIO_STANDARD_ENDPOINT_DESC_SIZE,    /* bLength */
  USB_DESC_TYPE_ENDPOINT,               /* bDescriptorType */
  AUDIO_OUT_EP,                         /* bEndpointAddress 1 out endpoint*/
  0x05, //##USBD_EP_TYPE_ISOC,     		/* bmAttributes */
  AUDIO_PACKET_SZE(USBD_AUDIO_FREQ),  /* wMaxPacketSize in Bytes ((Freq(Samples)+1)*2(Stereo)*2(HalfWord)) */
  0x01,                                 /* bInterval */
  0x00,                                 /* bRefresh */
  AUDIO_IN_EP,                          /* ##bSynchAddress */
  /* 09 byte*/

  /* Endpoint - Audio Streaming Descriptor*/
  AUDIO_STREAMING_ENDPOINT_DESC_SIZE,   /* bLength */
  AUDIO_ENDPOINT_DESCRIPTOR_TYPE,       /* bDescriptorType */
  AUDIO_ENDPOINT_GENERAL,               /* bDescriptor */
  0x00,                                 /* bmAttributes */
  0x00,                                 /* bLockDelayUnits */
  0x00,                                 /* wLockDelay */
  0x00,
  /* 07 byte*/

  /* ##Endpoint 2 for feedback - Standard Descriptor */
  AUDIO_STANDARD_ENDPOINT_DESC_SIZE,  /* bLength */
  USB_DESC_TYPE_ENDPOINT,         	  /* bDescriptorType */
  AUDIO_IN_EP,                        /* bEndpointAddress 2 in endpoint*/
  0x11,                               /* bmAttributes */
  3,0,                                /* wMaxPacketSize in Bytes 3 */
  1,                                  /* bInterval 1ms*/
  SOF_RATE,                           /* bRefresh 2ms*/
  0x00,                               /* bSynchAddress */
  /* 09 byte*/
} ;

/* USB Standard Device Descriptor */
__ALIGN_BEGIN static uint8_t USBD_AUDIO_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END=
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};

/**
  * @}
  */

/** @defgroup USBD_AUDIO_Private_Functions
  * @{
  */


/**
  * @brief  USBD_AUDIO_Init
  *         Initialize the AUDIO interface
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t  USBD_AUDIO_Init (USBD_HandleTypeDef *pdev,
                               uint8_t cfgidx)
{
  USBD_AUDIO_HandleTypeDef   *haudio;

  /* Open EP OUT */
  USBD_LL_OpenEP(pdev,
                 AUDIO_OUT_EP,
                 USBD_EP_TYPE_ISOC,
                 AUDIO_OUT_PACKET+4);

  /* ## Open EP for Sync */
  USBD_LL_OpenEP(pdev,
                 AUDIO_IN_EP,
                 USBD_EP_TYPE_ISOC,
                 3);

  /* ## Flush EP for Sync */
  USBD_LL_FlushEP(pdev,
                 AUDIO_IN_EP);

  
  /* Allocate Audio structure */
  pdev->pClassData = USBD_malloc(sizeof (USBD_AUDIO_HandleTypeDef));
  
  if(pdev->pClassData == NULL)
  {
    return USBD_FAIL;
  }
  else
  {
    haudio = (USBD_AUDIO_HandleTypeDef*) pdev->pClassData;
    haudio->alt_setting = 0;
    haudio->offset = AUDIO_OFFSET_UNKNOWN;
    haudio->wr_ptr = 0;
    haudio->rd_ptr = 0;
    haudio->rd_enable = 0;

    /* Initialize the Audio output Hardware layer */
    //if (((USBD_AUDIO_ItfTypeDef *)pdev->pUserData)->Init(USBD_AUDIO_FREQ, AUDIO_DEFAULT_VOLUME, 0) != USBD_OK)
    //{
    //  return USBD_FAIL;
    //}

    /* Prepare Out endpoint to receive 1st packet */
    USBD_LL_PrepareReceive(pdev,
                           AUDIO_OUT_EP,
                           tmpbuf,
                           AUDIO_OUT_PACKET+4);
  }
  flag=1;
  return USBD_OK;
}

/**
  * @brief  USBD_AUDIO_DeInit
  *         DeInitialize the AUDIO layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t  USBD_AUDIO_DeInit (USBD_HandleTypeDef *pdev,
                                 uint8_t cfgidx)
{
  
  /* Close EP OUT */
  USBD_LL_CloseEP(pdev,
              AUDIO_OUT_EP);

  /* Close EP IN */
  USBD_LL_CloseEP(pdev,
              AUDIO_IN_EP);


  /* DeInit  physical Interface components */
  if(pdev->pClassData != NULL)
  {
   ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData)->DeInit(0);
    USBD_free(pdev->pClassData);
    pdev->pClassData = NULL;
  }

  flag=0;

  return USBD_OK;
}

/**
  * @brief  USBD_AUDIO_Setup
  *         Handle the AUDIO specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t  USBD_AUDIO_Setup (USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef   *haudio;
  uint16_t len;
  uint8_t *pbuf;
  uint8_t ret = USBD_OK;
  haudio = (USBD_AUDIO_HandleTypeDef*) pdev->pClassData;

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
  case USB_REQ_TYPE_CLASS :
    switch (req->bRequest)
    {
    case AUDIO_REQ_GET_CUR:
      AUDIO_REQ_GetCurrent(pdev, req);
      break;

    case AUDIO_REQ_SET_CUR:
      AUDIO_REQ_SetCurrent(pdev, req);
      break;

    default:
      USBD_CtlError (pdev, req);
      ret = USBD_FAIL;
    }
    break;

  case USB_REQ_TYPE_STANDARD:
    switch (req->bRequest)
    {
    case USB_REQ_GET_DESCRIPTOR:
      if( (req->wValue >> 8) == AUDIO_DESCRIPTOR_TYPE)
      {
        pbuf = USBD_AUDIO_CfgDesc + 18;
        len = MIN(USB_AUDIO_DESC_SIZ , req->wLength);


        USBD_CtlSendData (pdev,
                          pbuf,
                          len);
      }
      break;

    case USB_REQ_GET_INTERFACE :
      USBD_CtlSendData (pdev,
                        (uint8_t *)&(haudio->alt_setting),
                        1);
      break;

    case USB_REQ_SET_INTERFACE :
      if ((uint8_t)(req->wValue) <= USBD_MAX_NUM_INTERFACES)
      {
        haudio->alt_setting = (uint8_t)(req->wValue);
	if (haudio->alt_setting == 1)
        {SOF_num=0;
        flag=0;
        USBD_LL_FlushEP(pdev,AUDIO_IN_EP);
        };
        if (haudio->alt_setting == 0)
        {flag=0;
       	USBD_LL_FlushEP(pdev,AUDIO_IN_EP);
        }
      }
      else
      {
        /* Call the error management function (command will be nacked */
        USBD_CtlError (pdev, req);
      }
      break;

    default:
      USBD_CtlError (pdev, req);
      ret = USBD_FAIL;
    }
  }
  return ret;
}


/**
  * @brief  USBD_AUDIO_GetCfgDesc
  *         return configuration descriptor
  * @param  speed : current device speed
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t  *USBD_AUDIO_GetCfgDesc (uint16_t *length)
{
  *length = sizeof (USBD_AUDIO_CfgDesc);
  return USBD_AUDIO_CfgDesc;
}

/**
  * @brief  USBD_AUDIO_DataIn
  *         handle data IN Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  * If the data is successfully sent - all repeated
  */

static uint8_t  USBD_AUDIO_DataIn (USBD_HandleTypeDef *pdev,
                              uint8_t epnum)
{
	// We control that we have the good EP: the last 7 bits, as 0x80 is the direction
	if (epnum == (AUDIO_IN_EP&0x7f))
	{
	   //BSP_LED_Toggle(LED6);
	   flag=0;
	   SOF_num=0;
	}
    return USBD_OK;
}

/**
  * @brief  USBD_AUDIO_EP0_RxReady
  *         handle EP0 Rx Ready event
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t  USBD_AUDIO_EP0_RxReady (USBD_HandleTypeDef *pdev)
{
  USBD_AUDIO_HandleTypeDef   *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef*) pdev->pClassData;

  if (haudio->control.cmd == AUDIO_REQ_SET_CUR)
  {/* In this driver, to simplify code, only SET_CUR request is managed */

    if (haudio->control.unit == AUDIO_OUT_STREAMING_CTRL)
    {
     ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData)->MuteCtl(haudio->control.data[0]);
      haudio->control.cmd = 0;
      haudio->control.len = 0;
    }
  }

  return USBD_OK;
}
/**
  * @brief  USBD_AUDIO_EP0_TxReady
  *         handle EP0 TRx Ready event
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t  USBD_AUDIO_EP0_TxReady (USBD_HandleTypeDef *pdev)
{
  /* Only OUT control data are processed */
  return USBD_OK;
}


/**
  * @brief  USBD_AUDIO_SOF
  *         handle SOF event
  * @param  pdev: device instance
  * @retval status
  * SOF token handler updates the data for the final synchronization point
  */
static uint8_t  USBD_AUDIO_SOF (USBD_HandleTypeDef *pdev)
{
	uint8_t res;
	static uint16_t n;

	USBD_AUDIO_HandleTypeDef   *haudio;

	haudio = (USBD_AUDIO_HandleTypeDef*) pdev->pClassData;

	USB_OTG_GlobalTypeDef *USBx = pdev->pData;

	//## Roman's code
	//USB_OTG_DSTS_TypeDef  FS_DSTS;


	  /* Check if there are available data in stream buffer.
	    In this function, a single variable (PlayFlag) is used to avoid software delays.
	    The play operation must be executed as soon as possible after the SOF detection. */
	 /* At leading edge of SOF signal we need to capture TIM2 value into CCR1 register and reset TIM2 counter itself.
		In the SOF interrupt handler capture flag is reset and value from TIM2 CCR1 register is accumulated to compute
		feedback value. As according to the standard, explicit feedback value contains ratio of sample rate
		to SOF rate and should be sent in 10.14 format, it is accumulated during 2^FEEDBACK_RATE periods.
		To compute feedback value it should be shifted left by 6 digits
		(due to the MCLK to sample rate 256 times ratio). */
	//BSP_LED_Toggle(LED3);

	if (haudio->alt_setting==1)
	{
		SOF_num++;
        if (SOF_num==(1<<SOF_RATE))
	      {
	        SOF_num=0;
	        //feedbacktable[0]= 0x66;
	        //feedbacktable[1]= 0x06;
	        //feedbacktable[2]= 0x0C;
	        //feedbacktable[0]= 0x00;
	        //feedbacktable[1]= 0x00;
	        //feedbacktable[2]= 0x0C;
	        //feedbacktable[0]= 0x9A;
	        //feedbacktable[1]= 0xF9;
	        //feedbacktable[2]= 0x0B;
	        feedbacktable[0] = feedback_data;
	        feedbacktable[1] = feedback_data >> 8;
	        feedbacktable[2] = feedback_data >> 16;
	        USBD_LL_Transmit(pdev, AUDIO_IN_EP, (uint8_t *) &feedbacktable, 3);
	       }
	}

  return USBD_OK;
}

/**
  * @brief  USBD_AUDIO_Sync
  * @param  pdev: device instance
  * @retval status
  * Not used in that bare Minimum exemple
  */
void  USBD_AUDIO_Sync (USBD_HandleTypeDef *pdev, AUDIO_OffsetTypeDef offset)
{
  int8_t shift = 0;
  USBD_AUDIO_HandleTypeDef   *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef*) pdev->pClassData;

}

/**
  * @brief  USBD_AUDIO_IsoINIncomplete
  *         handle data ISO IN Incomplete event
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t  USBD_AUDIO_IsoINIncomplete (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    // ?? I don't understand that part
	//This ISR is executed every time when IN token received with "wrong" PID. It's necessary
    //to flush IN EP (feedback EP), get parity value from DSTS, and store this info for SOF handler.
    //SOF handler should skip one frame with "wrong" PID and attempt a new transfer a frame later.

    // I've been working on the Isoc incomplete issue... And got pretty good idea, which solved this problem.
    // At the isoc incomplete interrupt, I mark the current pid (last bit of frame number).
    // At the SOF interrupt, I test if current frame has the same parity, and when it's true,
    // transmit the feedback endpoint data. This way is 100% working.

    USB_OTG_GlobalTypeDef *USBx = pdev->pData;
    //?? Not sure that the flushing of the EP is needed
    USBD_LL_FlushEP(pdev,AUDIO_IN_EP);
    /* EP disable, IN data in FIFO */
    USBx_INEP(AUDIO_IN_EP&0x7f)->DIEPCTL = (USB_OTG_DIEPCTL_EPDIS | USB_OTG_DIEPCTL_SNAK);
    //USBx_INEP(epnum)->DIEPCTL = (USB_OTG_DIEPCTL_EPDIS | USB_OTG_DIEPCTL_SNAK);
    /* EP enable, IN data in FIFO */
    USBx_INEP(AUDIO_IN_EP&0x7f)->DIEPCTL |= (USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA);
    //USBx_INEP(epnum)->DIEPCTL |= (USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA);

    feedbacktable[0] = feedback_data;
    feedbacktable[1] = feedback_data >> 8;
    feedbacktable[2] = feedback_data >> 16;
    //feedbacktable[0]= 0x00;
    //feedbacktable[1]= 0x00;
    //feedbacktable[2]= 0x0C;
    //feedbacktable[0]= 0x9A;
    //feedbacktable[1]= 0xF9;
    //feedbacktable[2]= 0x0B;

    USBD_LL_Transmit(pdev, AUDIO_IN_EP, (uint8_t *) &feedbacktable, 3);

    //dpid = ((((USBx_DEVICE->DSTS))>>8)&((uint32_t)0x00000001));

    SOF_num=0;
    /* if (flag)
    {
       flag=0;
       USBD_LL_FlushEP(pdev,AUDIO_IN_EP);
    }; */
    return USBD_OK;
}
/**
  * @brief  USBD_AUDIO_IsoOutIncomplete
  *         handle data ISO OUT Incomplete event
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t  USBD_AUDIO_IsoOutIncomplete (USBD_HandleTypeDef *pdev, uint8_t epnum)
{

  return USBD_OK;
}
/**
  * @brief  USBD_AUDIO_DataOut
  *         handle data OUT Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  * As in asynchronous USB audio synchronization is by changing the packet length data
  * in a frame data reception code in the ring buffer should take into account the length
  * of the received packet.
  */
static uint8_t  USBD_AUDIO_DataOut (USBD_HandleTypeDef *pdev,
                              uint8_t epnum)
{
  USBD_AUDIO_HandleTypeDef   *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef*) pdev->pClassData;

  uint32_t curr_length,curr_pos,rest;

  if (epnum == AUDIO_OUT_EP)
  {
	  // ##Must read the number of frame read and push them to the application ring buffer
	  // or to a buffer handled by the application as in the TI USB application ?
	  curr_length=USBD_LL_GetRxDataSize (pdev,epnum);

	  if (curr_length<AUDIO_OUT_PACKET) {
	 		  //BSP_LED_On(LED4);
	 	  };
	  if (curr_length==AUDIO_OUT_PACKET+4) {
	 		  //BSP_LED_On(LED5);
	 	  };
	  if (curr_length > AUDIO_OUT_PACKET+4) {
		  //BSP_LED_On(LED5);
	 	  };

	  if (curr_length == AUDIO_OUT_PACKET) {
		  	  //BSP_LED_Off(LED4);
		  	  //BSP_LED_Off(LED5);
	  	 	  };

	  //?? for test purpose to correct the 388 case ?!?!
	  //if (curr_length == 388) { curr_length = AUDIO_OUT_PACKET+4;};
	  ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData)->AudioCmd(&tmpbuf,curr_length,AUDIO_CMD_DATA_OUT);

	  /*rest=AUDIO_TOTAL_BUF_SIZE-haudio->wr_ptr;
	  //monitor sample rate conversion
	  //?? for debug purpose for LEDS - to suppress to remove dependencies
	  if (curr_length==AUDIO_OUT_PACKET-4) {
		  BSP_LED_On(LED4);
	  };
	  if (curr_length>AUDIO_OUT_PACKET) {
		  BSP_LED_On(LED5);
	  };
	  if (curr_length == AUDIO_OUT_PACKET) {
		  BSP_LED_Toggle(LED3);
	  };

	  if (rest<curr_length)
	           {
	           if (rest>0)
	           //?? Potentially we copy one too much. Check meaning of wr_ptr and last value possible to write in buffer
	           {memcpy((uint8_t*)&haudio->buffer[haudio->wr_ptr],tmpbuf,rest);
	           haudio->wr_ptr = 0;
	           curr_length-=rest;
	           };
	           if ((curr_length)>0)
	           {memcpy((uint8_t*)&haudio->buffer[haudio->wr_ptr],(uint8_t *)(&tmpbuf[0]+rest),curr_length);
	           haudio->wr_ptr+=curr_length;};
	           }
	           else
	           {
	           if (curr_length>0)
	           {memcpy((uint8_t*)&haudio->buffer[haudio->wr_ptr],tmpbuf,curr_length);
	           // Increment the Buffer pointer
	           haudio->wr_ptr += curr_length;};
	           }
	           //roll it back when all buffers are full
	  	  	   //??check if AUDIO_TOTAL_BUF_SIZE = AUDIO_OUT_PACKET * OUT_PACKET_NUM
	           if (haudio->wr_ptr >= AUDIO_TOTAL_BUF_SIZE)
	        	   haudio->wr_ptr = 0; */


	      // call back to hand over the audio out packet of one frame length
	      // We send the normal packet length, as the haudio->buffer is the one that manages
	      // the variable feedback rate
	      //((USBD_AUDIO_ItfTypeDef *)pdev->pUserData)->AudioCmd(&haudio->buffer[haudio->rd_ptr],AUDIO_OUT_PACKET,AUDIO_CMD_DATA_OUT);

	      // The read pointer is moved to next frame
	      // Check the % to se i addtiona +4 or +1 could distort the thing
	      //haudio->rd_ptr = (haudio->rd_ptr+AUDIO_OUT_PACKET)%AUDIO_TOTAL_BUF_SIZE;


        /* Prepare Out endpoint to receive next audio packet */
	    //?? check why + 16 when we have allocated in AUDIO_PACKET_SZE +4 (space for 1 more sample)
	    //?? check that &haudio->buffer is sized big enough to accommodate the +16 bytes

	          USBD_LL_PrepareReceive(pdev,
                           AUDIO_OUT_EP,
						   (uint8_t*)tmpbuf,
                           AUDIO_OUT_PACKET+4);

  }

  return USBD_OK;
}

/**
  * @brief  AUDIO_Req_GetCurrent
  *         Handles the GET_CUR Audio control request.
  * @param  pdev: instance
  * @param  req: setup class request
  * @retval status
  */
static void AUDIO_REQ_GetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef   *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef*) pdev->pClassData;

  memset(haudio->control.data, 0, 64);
  /* Send the current mute state */
  USBD_CtlSendData (pdev,
                    haudio->control.data,
                    req->wLength);
}

/**
  * @brief  AUDIO_Req_SetCurrent
  *         Handles the SET_CUR Audio control request.
  * @param  pdev: instance
  * @param  req: setup class request
  * @retval status
  */
static void AUDIO_REQ_SetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{ 
  USBD_AUDIO_HandleTypeDef   *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef*) pdev->pClassData;

  if (req->wLength)
  {
    /* Prepare the reception of the buffer over EP0 */
    USBD_CtlPrepareRx (pdev,
                       haudio->control.data,
                       req->wLength);

    haudio->control.cmd = AUDIO_REQ_SET_CUR;     /* Set the request value */
    haudio->control.len = req->wLength;          /* Set the request data length */
    haudio->control.unit = HIBYTE(req->wIndex);  /* Set the request target unit */
  }
}


/**
* @brief  DeviceQualifierDescriptor
*         return Device Qualifier descriptor
* @param  length : pointer data length
* @retval pointer to descriptor buffer
*/
static uint8_t  *USBD_AUDIO_GetDeviceQualifierDesc (uint16_t *length)
{
  *length = sizeof (USBD_AUDIO_DeviceQualifierDesc);
  return USBD_AUDIO_DeviceQualifierDesc;
}

/**
* @brief  USBD_AUDIO_RegisterInterface
* @param  fops: Audio interface callback
* @retval status
*/
uint8_t  USBD_AUDIO_RegisterInterface  (USBD_HandleTypeDef   *pdev,
                                        USBD_AUDIO_ItfTypeDef *fops)
{
  if(fops != NULL)
  {
    pdev->pUserData= fops;
  }
  return 0;
}
/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
