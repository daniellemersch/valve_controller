#include <arpa/inet.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "aioUsbApi.h"

#include "libusb.h"

/****************************************************************************/
//	      AcessIO_LinuxUsbAPI 
//  
//  Release 1.0
//
//  Description:   
//   Linux Implementation of the Access/IO USB SW API Interface which
//   enables applications to interface with ACCES/IO devices which use
//   a USB Interface to communicate with a host computer. 
//
//   There is a similar API for Windows. The implementation of the APIs
//   is not the same
//
//  Author	:  Jeff Price
//  History	:  11/06/2009	created
//
//
//  Notes	:
// 
//
//   The API requires Linux version 2.6 or greater
//
//
//   Refer to the ACCES/IO Software Reference Manual for a description
//   of the each of the functions.
//		
//
//
//   Currently only 1 BulkAcquire processcan be in progress at only one time
//   this per device
//
//   The Linux USB INTERFACE :
//
//    The opensource code 'libusb' is used to for all USB related functions.
//
//    libusb is an user-space API to the lower level standard linux USB EFCI Host
//    kernel-mode device driver which interfaces directly with the USB hardware.
//    The libusb API enables user-space applications to interface with the USB
//    subsytem without requiring writing or modifying an actual kernel-mode
//    device driver  
//
//    The source code and documentation on libusb which describes
//    each of the libusb calls used in this Access/IO usb API can
//    be found at : 
//      
//                http://libusb.wiki.sourceforge.net
//   
//
//   This Release 1.0 of the ACCES/IO API uses libusb 1.0. However any
//   later release of libusb should work. 
//
//   DEBUGGING:
//
//
//   Each of the functions return and error code defined in
//   aioUsbApi.h which provides specific information on reason
//   for the failure.
//   
//   Compiling the code with -DDEBUG causes the debug statements to me
//   output; or you can explictly define it in this file as the first line
//   before the "ifdef   DEBUG" conditional.
//   Compiling the code with -DDEBUG causes the debug statements to me
//   output otherwise they are not output
//
//   Refer to the libusb documentation from sourceforge (or look at the libusb source)
//   to see how to enable libusb debugging. 
//
//
//************************************************************************ 

int g_fatalSystemError = 0;

struct timespec      bulkPollTime;

#define SET_BIT      (bitmap | bit)

#define BITS_PER_INT (sizeof(int) * 8)

struct libusb_device_handle *G_HAND;

void clear_bit(void *bitmap, size_t bit_no)
{
  ((int *) bitmap)[bit_no / BITS_PER_INT] &=
  ~(1 << (bit_no % BITS_PER_INT));
}


//struct libusb_transfer *g_pendingXfers[MAX_PENDING_XFERS]; 

typedef struct
{
  int                 xferRecId; 
  int                 devIdx;
  unsigned char      *pBuf;      // caller of BulkAquire's buffer
  unsigned char      *pXferBuf;  // buffer passed into submit_xfer call
} bulkXferParam;


aioDeviceInfo  aioDevs;
typedef struct
{
  int                     inUse;
  bulkXferParam           userData;
  struct libusb_transfer *pXfer;
} bulkXferRec;

bulkXferRec bulkXferPool[MAX_PENDING_XFERS]; 


bulkXferRec *
getBulkXferRec ()
{
  int        i;
  int        found;
 
  for (i=0; i<MAX_PENDING_XFERS;i++)
  {
    if (bulkXferPool[i].inUse == 0)
    {
      bulkXferPool[i].inUse               = 1;
      bulkXferPool[i].userData.xferRecId  = i;


      bulkXferPool[i].pXfer = libusb_alloc_transfer(0);
      if (bulkXferPool[i].pXfer == NULL)
      {
         debug("DBG>> getBulkXferRec : libusb_alloc_transfer failed \n");
         return (NULL);
       }

       bulkXferPool[i].pXfer->timeout = 5000.0; 

      // free'd in bulkReadCallBack function
      bulkXferPool[i].userData.pXferBuf =  (unsigned char *)calloc(BYTES_PER_READ,8); 
      if (bulkXferPool[i].userData.pXferBuf == NULL)
      {
        debug("DBG>> getBulkXferRec : calloc failed \n");
        return (NULL);
      }

     found = 1;

     break;
    }
  }

  if (found) 
    return (&bulkXferPool[i]);
  else
    return (NULL);
}

void
freeBulkXferRec(bulkXferRec   *pBulkXferRecord)
{
    bulkXferPool[pBulkXferRecord->userData.xferRecId].inUse = 0;

    free(pBulkXferRecord->userData.pXferBuf);

    libusb_free_transfer(pBulkXferRecord->pXfer);

}



// returns the index in the aio device list corresponding
// to the input device index
int
getListIndex(int devIdx)
{
  int idx;
  int devFound;

  // find the right entry in the list
  for (idx  = 0; idx < aioDevs.numAIODevs; idx++)
  {
    if (aioDevs.aioDevList[idx].devIdx == devIdx)
    {
      devFound = 1;
      break;
    }
  }
  if (devFound)
   return (idx);
  else
  {
    debug("DBG>>GetListIndex : No entry in lust for devIdx = %d\n",devIdx);
    return (ERROR_NO_AIO_DEVS_FOUND);
   }
 
}


void
sampleBulkReadCallBack (struct libusb_transfer *transfer)
{

  int              ret;
  int              idx;
  int              devIdx;
  int              bufIdx;
  unsigned char    *buf;
  int               len;
  bulkXferRec      *pBulkXferRec;
  bulkXferRec      *pParams;
  int               bytesLeft;
  int               i;



  // NOTE : if the user_data is invalid something is really hosed up in libusb
  //        so we set a global g_FatalSySERR to  terminate
  //        the while loop in BulkAcquire
  //        The debug stmts below will indicate this condition has occured
  //        but there is no graceful recovery. The application must be
  //        stopped and restarted.
  //       
   
  if (transfer->user_data == NULL)
  {
    g_fatalSystemError = 1;
    debug("DBG>>AIO_Usb_sampleBulkReadCallBack: FATAL SYSTEM ERROR : user_data param struct is NULL\n");
    return;
  }
/*****
  //For DEBUG only

int j,k=0;
float volts;

printf("\n");
for (j=0;j<16;j++)
{
 volts = (transfer->buffer[k+1] << 8) | (transfer->buffer[k]);
 k+=2;
 volts = (volts/65535.0) * 10.0;
 printf(" IN CALLBACK CHAN  %d  Volts=%f           K=0x%x         K+1=0x%x  \n",j,volts,transfer->buffer[k],transfer->buffer[k+1]);
}   
******/

  pParams = (bulkXferRec *)transfer->user_data; 
 
  if (pParams->userData.pBuf == NULL)
  {
    g_fatalSystemError = 1;
    debug("DBG>>AIO_Usb_sampleBulkReadCallBack: FATAL SYSTEM ERROR : user_data.pBuf is NULL \n"); 
    return;
  }

  buf    = pParams->userData.pBuf;
  devIdx = pParams->userData.devIdx;

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    g_fatalSystemError = 1;
    debug("DBG>>AIO_Usb_sampleBulkReadCallBack: FATAL SYSTEM ERROR invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return;
  }

  idx = getListIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    g_fatalSystemError = 1;
    debug("DBG>>AIO_Usb_sampleBulkRead: FATAL SYSTEM ERROR No Entry in lust for devIdex = %d\n",(unsigned int)devIdx); 
  }


  if (transfer->status == LIBUSB_TRANSFER_COMPLETED) 
  {

     // copy the user data into the global sample buffer
     // in the BulkAquireCall the user's  buffer passed in
     // is set to point to the global sample buffer
     //

     bufIdx = aioDevs.aioDevList[idx].sampleBytesRead;

     memcpy((void *)(buf+bufIdx),
           (void *)transfer->buffer,
            transfer->actual_length);
            
   aioDevs.aioDevList[idx].sampleBytesRead =  
    aioDevs.aioDevList[idx].sampleBytesRead + transfer->actual_length;

   aioDevs.aioDevList[idx].sampleBytesLeftToRead  = 
     aioDevs.aioDevList[idx].sampleBytesLeftToRead - transfer->actual_length; 

   freeBulkXferRec(pParams);

   aioDevs.aioDevList[idx].sampleReadsCompleted++;
   }
   else
   {

     if (transfer->status == LIBUSB_TRANSFER_TIMED_OUT)
       aioDevs.aioDevList[idx].sampleReadTimeouts++;

     debug("DBG>> bulkReadCallback: BULK XFER Error. Transfer not completed. Status=%d\n", transfer->status);
     freeBulkXferRec(pParams);
   }  



    bytesLeft = aioDevs.aioDevList[idx].sampleBytesLeftToRead;

   // submit more reads until all required ones have been submitted
   // we always want to have several in the queue pending to avoid
   // data overruns in the device 
    for (i=0; 
         i < 10;
        i++)
   {
    if (aioDevs.aioDevList[idx].sampleReadsSubmitted < 
       aioDevs.aioDevList[idx].sampleReadsRequired )
    {
       if (aioDevs.aioDevList[idx].sampleBytesLeftToRead >= BYTES_PER_READ)
         len = BYTES_PER_READ;
       else
         len = aioDevs.aioDevList[idx].sampleBytesLeftToRead;

//printf("LEN to Read =%d\n",len);


        // get a xfer struct 
         pBulkXferRec = getBulkXferRec();
         if (pBulkXferRec == NULL)
         {
           debug("DBG>> bulkAquire: MAX_PENDING_XFERS EXceeded \n");
         }

          pBulkXferRec->userData.devIdx = devIdx;

          // this is the beginning of the caller of this functions buffer
          // data is appended to it upon each transfer completion in the sampleBulkReadCallback
          pBulkXferRec->userData.pBuf   = buf;  


  

        libusb_fill_bulk_transfer(pBulkXferRec->pXfer, 
                                  aioDevs.aioDevList[idx].devHandle,                                
                                 (6 | LIBUSB_ENDPOINT_IN),
                                  pBulkXferRec->userData.pXferBuf,
                                  len,
                                  sampleBulkReadCallBack, 
                                  (void *)(pBulkXferRec),
                                  0);

        ret = libusb_submit_transfer(pBulkXferRec->pXfer);

        if (ret < 0 )
        {
          freeBulkXferRec(pBulkXferRec);
          debug("DBG>> bulkAquire: libusb_submit_transfer failed ; ret = %d\n",ret);
          ret = libusb_release_interface(transfer->dev_handle,0); 
        }

      bytesLeft = bytesLeft - len;

      aioDevs.aioDevList[idx].sampleReadsSubmitted++;
     }
}





}



struct libusb_device_handle *
getDevHandle(int devIdx)
{
  int 				ret;
  int 				idx;
  struct libusb_device        *device;
  struct libusb_device_handle *devHandle;
  int                          productId;

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>getDevHandle : invalid dev Index = %d \n",(unsigned int)devIdx); 
    return (NULL);
  }

  idx = getListIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>getDevHandle : No Entry in lust for devIdex = %d\n",(unsigned int)devIdx); 
    return (NULL);
  }

   // get the descriptor for this device 
   device = aioDevs.aioDevList[idx].device;

   productId = aioDevs.aioDevList[idx].productId;
 
  // changed because  libusb_open_with_VID_PID does not allow multiple USB-DIO 96 to be connected at the same time: http://libusb.sourceforge.net/api-1.0/group__dev.html#ga11ba48adb896b1492bbd3d0bf7e0f665
  //devHandle = libusb_open_device_with_vid_pid(NULL,0x1605,productId);
  ret = libusb_open(device, &devHandle);
  if (ret!= 0){
   fprintf(stderr, "libusb_open() for device %d failed with error %d. \n",devIdx,ret);
   return NULL;
  }
  //

  if (devHandle == NULL)
    debug("DBG>>getDevHandle : Cannot Get Device Handle for ProductId=0x%x devIdx=%d\n",productId,(unsigned int)devIdx); 

  if (ret < 0)
  {
    debug("DBG>>getDevHandle : could not get device handle devIdx = %d \n",(unsigned int)devIdx); 
    return (NULL);
  }
  else 
   return (devHandle);
} 

//struct timespec      bulkPollTime;

unsigned char        *sampleDataBuf;

 int
 validateProductID(int devIdx)
 {
  int ret;
  unsigned long       prodID;
  unsigned long       nameSize;
  char                name[100];
  unsigned long       DIOBytes;
  unsigned long       counters;

  // need to make this match calling functions to
  // product family.
  return (ERROR_SUCCESS);

  ret   = AIO_Usb_QueryDeviceInfo(devIdx,
                                  &prodID,
                                  &nameSize,
                                  name,
                                  &DIOBytes,
                                  &counters);

  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>validateProductID: AIO_UsbQueryDeviceInfo failed err = %d \n",ret); 
    return (ret);
  }

  if (prodID != USB_AIO_AI16_16A_DEV)
  {
    debug("DBG>>validateProductID : Invalid Product ID  = 0x%0x \n",(unsigned int)prodID); 
    return (ERROR_UNSUPPORTED_DEVICE);
  }
 else
  return (ERROR_SUCCESS);

}



// returns a valid device num or ERROR code
int
AIO_UsbValidateDeviceIndex(int  devIdx)
{
  int         i;
  int         numDevs     = 0;
  int         devFound    = 0;  

  // get the first AIO device
  if (devIdx == DI_FIRST)
  {
    for (i = 0; i < aioDevs.numAIODevs; i++)
    {
      if (aioDevs.aioDevList[i].devIdx != NO_DEVICE)
      {
        devFound = 1;
        break;
      }
    }
   if (devFound) 
     return (aioDevs.aioDevList[i].devIdx);
   else
   {
     debug("DBG>>Validate Index: called with DI_FIRST and no AIO device found\n");
     return (ERROR_INVALID_DEV_IDX); 
   }
  }
   

  if  (devIdx == DI_ONLY)
  {
    // check for more than 1 dev
    for (i = 0; i < aioDevs.numAIODevs; i++)
    {
      if (aioDevs.aioDevList[i].devIdx != NO_DEVICE)
        numDevs++;
    }
    if (numDevs > 1)
    {
      debug("DBG>>Validate Index: called with DI_ONLY and more than 1 index was found\n");
      return (ERROR_DUP_NAME);
  
    }  

  }

  // so make sure the input device exists
  for (i = 0; i < aioDevs.numAIODevs; i++)
  {
    if (aioDevs.aioDevList[i].devIdx == devIdx)
      devFound = 1; 
  }
  if (devFound)
    return (devIdx);
  else
  {
    debug("DBG>>Validate Index: invalid dev = 0x%x \n",(unsigned int)devIdx); 
    return (ERROR_INVALID_DEV_IDX); 
  }
}


short 
OctaveDacFromFreq(double *Hz)
{

 short  octave;
 short  offset; 
 short  octave_offset;

  if (*Hz == 0)
  {
    return 0;
  }
  else
  {
    if (*Hz > 40000000.0)
      *Hz = 40000000.0;

    octave = floor(3.322 * log10(*Hz/1039));

    if (octave < 0)
    {
      octave = 0;
      offset = 0; 
      return 0;
    }
    else
    {
    offset = floor( (double)(2048 - ( ldexp(2078, 10 + octave)/ *Hz) ) );

    octave_offset = (octave << 12) | (offset << 2);

    // the bytes must be swapped because the oscillator
    // wants the value in Motorola woredj 
    octave_offset = htons(octave_offset); 

   *Hz = (2078 << octave) / (2 - offset / 1024); 

    }

  }

  return (octave_offset);
}



/********************************************************************/
//
//  Function Name : AIO_Usb_GetDevices
//
//  Description   :
//                 populates the array of AIO device numbers
//                 
//					
//
//  Returns       : ERROR_SUCCESS , meaning aioDevList is correctly 
//                  populated
// 
//                  ERROR_NO_AIO_DEVS_FOUND if no AIO devs found
//                  
//                  NOTE : aioDevList is not populated in this case
//                         and the Api cannot be used until
//                         this function completes successfully.                        
//
//  Notes         :
//                   IMPORTANT : this function must be called at least once
//                               before any other function in the API is 
//                               called.
//
//  History	  :
// 
/********************************************************************/
int
AIO_Usb_GetDevices(aioDeviceInfo  *pAioDeviceInfo)
{

  struct libusb_device **devs;
  struct libusb_device *dev;

  int         i,j;
  int         ret;

        // initialize the aio dev list
        aioDevs.numAIODevs = 0;
        for (i = 0; i < MAX_USB_DEVICES; i++)
        {
          aioDevs.aioDevList[i].devHandle		= NULL;
          aioDevs.aioDevList[i].device       		= NULL; 
          aioDevs.aioDevList[i].devIdx       		= NO_DEVICE;
          aioDevs.aioDevList[i].productId		= 0;

          aioDevs.aioDevList[i].sampleBytesToRead       = 0;
          aioDevs.aioDevList[i].sampleBytesRead         = 0;
          aioDevs.aioDevList[i].sampleBytesLeftToRead   = 0;
	  aioDevs.aioDevList[i].sampleReadInProgress    = 0;
	  aioDevs.aioDevList[i].sampleReadsRequired     = 0;
	  aioDevs.aioDevList[i].sampleReadsCompleted    = 0;
	  aioDevs.aioDevList[i].sampleReadTimeouts      = 0;
	  aioDevs.aioDevList[i].sampleReadBulkXferError = 0;
          aioDevs.aioDevList[i].pSampleReadBuf          = NULL; 


          aioDevs.aioDevList[i].DIOBytesToRead          = 0;
	  aioDevs.aioDevList[i].DIOBytesRead	 	= 0;
          aioDevs.aioDevList[i].DIOBytesLeftToRead      = 0;
	  aioDevs.aioDevList[i].DIOReadsRequired        = 0;
          aioDevs.aioDevList[i].DIOReadsSubmitted       = 0;
	  aioDevs.aioDevList[i].DIOReadsCompleted       = 0;
	  aioDevs.aioDevList[i].DIOReadTimeouts         = 0;
	  aioDevs.aioDevList[i].DIOReadBulkXferError    = 0;
          aioDevs.aioDevList[i].pDIOReadBuf             = NULL; 

          aioDevs.aioDevList[i].DIOBytesToWrite	        = 0;
	  aioDevs.aioDevList[i].DIOBytesWritten         = 0;
          aioDevs.aioDevList[i].DIOBytesLeftToWrite     = 0;
	  aioDevs.aioDevList[i].DIOWritesRequired       = 0;
	  aioDevs.aioDevList[i].DIOWritesCompleted      = 0;
	  aioDevs.aioDevList[i].DIOWriteTimeouts        = 0;
	  aioDevs.aioDevList[i].DIOWriteBulkXferError   = 0;
          aioDevs.aioDevList[i].pDIOWriteBuf            = NULL; 

          aioDevs.aioDevList[i].streamOp             = STREAM_OP_NONE;
	}


	ret = libusb_get_device_list(NULL,&devs);
	if (ret <0)
        {
          debug("DBG>>getDevices : could not get device list \n"); 
          return (ERROR_COULD_NOT_GET_DEVLIST);
        }
        i = 0;         
        j = 0;         
        while ((dev = devs[i++]) != NULL) 
        {
		struct libusb_device_descriptor desc;
		ret = libusb_get_device_descriptor(dev, &desc);
		if (ret < 0)
                {
                  debug("DBG>>GetDevices: libusb_get_device_descriptor failed ret=%d \n",ret);
                  return (ERROR_COULD_NOT_GET_DESCRIPTOR);
                }

		if (desc.idVendor == ACCES_VENDOR_ID)
                {


                    aioDevs.aioDevList[j].devHandle    = NULL;
                    aioDevs.aioDevList[j].device       = dev; 
                    aioDevs.aioDevList[j].devIdx       = i; 
                    aioDevs.aioDevList[j].productId   = desc.idProduct;
                    aioDevs.numAIODevs++;
                    j++;
		}
	}

        if (aioDevs.numAIODevs == 0)
        {
          debug("DBG>>GetDevices : NO AIO Devices Found\n"); 
          libusb_free_device_list(devs, 1);
          return(ERROR_NO_AIO_DEVS_FOUND);

        }
        else
        {
          //AIO_UsbListDevices();
          libusb_free_device_list(devs, 1);

          *pAioDeviceInfo = aioDevs;
         }
         return (ERROR_SUCCESS);
  }

void
AIO_UsbListDevices()
{
  int i;

  if (aioDevs.numAIODevs == 0)
    printf("No AIO Devices Found\n");
  else
  {
    printf("AIO Devices Found : \n");
    for (i=0; i<aioDevs.numAIODevs; i++)
    {
      printf("  Device Index    =  %d \n",(unsigned int)aioDevs.aioDevList[i].devIdx);
      printf("  Product ID      = 0x%x\n",aioDevs.aioDevList[i].productId);

      switch(aioDevs.aioDevList[i].productId)
      {
        case 0x8040:
      printf("  Product Name   =   USB-AI16-16A"); 
        default:
         break;
      }
    } 
  }
}

/********************************************************************/
//
//  Function Name : AIO_Usb_QueryDeviceInfo
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_QueryDeviceInfo(unsigned long        devIndex,
                        unsigned long       *pProdID,
                        unsigned long       *pNameSize,
                        char                *pName,
                        unsigned long       *pDIOBytes,
                        unsigned long       *pCounters)
{
  
  int ret; 
  int bytes, counters,idx;


  if ( ( pProdID      == NULL ||
         pNameSize    == NULL ||
         pName        == NULL ||
         pDIOBytes    == NULL ||
         pCounters    == NULL) ) 
  {
    debug("DBG>>AIO_UsbQueryInfo : NULL Pointer passed as argument \n");
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIndex);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_UsbQueryInfo : invalid dev Index = 0x%0x \n",(unsigned int)devIndex); 
    return (ret);
  }

  idx = getListIndex(devIndex);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_UsbQueryInfo : No Entry in lust for devIdex = %d\n",(unsigned int)devIndex); 
    return (ret);
  }
 
  switch (aioDevs.aioDevList[idx].productId)
  {

   case USB_DIO_32_ID_DEV:
     bytes = 4;
     counters = 3;
     break;

   case USB_DA12_8A_REVA_DEV:
   case USB_DA12_8A_DEV:
     bytes = 0;
     counters = 0;
     break;

   case USB_IIRO_16_DEV:
     bytes = 4;
     counters = 0;
     break;

   case USB_CTR_15_DEV:
     bytes = 0;
     counters = 5;
     break;

   case USB_IIRO4_2SM_DEV:
     bytes = 2;
     counters = 0;
     break;

   case USB_AIO_AI16_16A_DEV:		
     if ( (pProdID != NULL)   &&
          (pNameSize != NULL) &&
          (pName  != NULL)    &&
          (pDIOBytes != NULL) &&
          (pCounters != NULL)  )
     {

      *pProdID = USB_AIO_AI16_16A_DEV;
      *pNameSize = 12;
       strncpy (pName,"USB-AIO-AI16",12);
      *pDIOBytes = 2; 
      *pCounters = NUM_AI_16_COUNTERS;
     }
     else
      return (ERROR_INVALID_PARAM); 

     break;

   default:
     return (ERROR_NO_AIO_DEVS_FOUND);
  }


  return ERROR_SUCCESS;

}

/********************************************************************/
//
//  Function Name : AIO_Usb_ADC_SetCal
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/

unsigned long 
AIO_Usb_ADC_SetCal(unsigned long  devIdx,
                   char          *calFileName)
{

  struct libusb_device_handle *handle;
  int                 i;
  int                 ret;
  int                 numBytes;
  short               sramAddr;
  unsigned short      dataBuf1[65536]; // sram is 64K 2-byte words
  unsigned char       dataBuf2[131072];
  unsigned char       dataBuf3[2048]; 
  char               *pdataBuf;
  int		      dataWritten;
  FILE               *calFile;

  if (calFileName == NULL)
  {
    debug("DBG>>AIO_SetCal: NULL filename passed in \n"); 
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_SetCal: invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_SetCal: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_DIO_ReadAll : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }

  if ( (strncmp(calFileName,":NONE:",6) ) == 0)
  {

      // create a default file 
   
      // fill a buf of 64K 2-byte words
      // with a counting seq from 0 - 0xFFFF
      for (i= 0; i < 0xFFFF + 1; i++)
      {
        dataBuf1[i] = i;
       }

     // we want the data to be in a char array
     // to pass it to bulk_write 2048 bytes
     // (1024 words) at a time. 
     // the 2 byte words are each
     // stored in two successive
     // locations of a character array;
     // 
     // so the array of the 64K 2 byte words 
     // stored as a char array should look like :
     // x[0] = 0
     // x[1] = 0
     // x[2] = 0
     // x[3] = 1
     // x[4] = 0
     // x[5] = 2
     //   ...
     //   ...
     // x[131070] = FF
     // x[131071] = FF

   memcpy ((void *)&dataBuf2[0],(void *)&dataBuf1,131072);


   /* use the default file from the release */
//   calFile = fopen("0000-FFFF.bin","r");


  }
  else
  {

   calFile = fopen(calFileName,"r");

   if (calFile == NULL)
   {
     debug("DBG>> AIO_SetCal: cannot open calFile : %s \n",calFileName);
     return (ERROR_CAL_FILE_NOT_FOUND);
   }
   // fill data with the data in the file
   // the file is 131070 bytes long,but
   // fgets() only reads up to 'n-1' bytes
   // 
   pdataBuf = fgets((char *)&dataBuf2[0],131072,calFile);
  } 


  ret = libusb_claim_interface(handle,0); 
  if (ret < 0)
  {
    debug("DBG>> AIO_SetCal: usb_claim_interface failed 0x%0x err=%d\n",(unsigned int)devIdx,ret);
   return (ERROR_LIBUSB_CLAIM_INTF_FAILED);
  }

  // the calibration file size is 65356 2-byte words
  // the AI16 wants the data in 2-byte words 
  // and bulk_transfer wants count in bytes
  // so 64 transfers of 1024 2-byte words or
  // 2048 bytes / transfer are needed to download
  // the 65356 2 byte words 

  numBytes = 1024 * 2;  // each word is 2 bytes 

  sramAddr = 0;

  for (i = 0; i< 63; i++)
  { 
    memcpy((void *)(&dataBuf3[0]),
           (void *)&dataBuf2[sramAddr],
            2048);

    ret = libusb_bulk_transfer(handle,
                               (2 | LIBUSB_ENDPOINT_OUT),
                                (unsigned char *)&dataBuf1[sramAddr],
                                numBytes,
                               &dataWritten,
                                TIMEOUT_5_SEC); 
    if ( (ret < 0) || 
         (dataWritten != numBytes))
    {
      ret = libusb_release_interface(handle,0); 
      debug("DBG>> AIO_SetCal: usb_bulk_write failed devIdx=%d err=%d\n",(unsigned int)devIdx,ret);
      debug("DBG>> AIO_DIO_StreamFrame : Wrote %d of %d bytes \n",(unsigned int)dataWritten,(unsigned int)numBytes);
      return (ERROR_USB_BULK_WRITE_FAILED);
    }

    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
		          SET_CAL,	
                          sramAddr, 
                          1024,
                          0, 
                          0, 
                          TIMEOUT_5_SEC);
						  
					  
    if (ret < 0)
    {
      debug("DBG>> AIO_Usb_SetCal: usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
      ret = libusb_release_interface(handle,0); 
      return (ERROR_USB_CONTROL_MSG_FAILED);
    }

    sramAddr += 1024; 
   }

  ret = libusb_release_interface(handle,0); 
  if (ret < 0)
  {
    debug("DBG>> AIO_SetCal: usb_release_interface failed 0x%0x \n",(unsigned int)devIdx);
    return (ERROR_USB_CONTROL_MSG_FAILED);
  }
  libusb_close(handle);
  return (ERROR_SUCCESS);
}


/********************************************************************/
//
//  Function Name : AIO_Usb_SetConfig
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_ADC_SetConfig(unsigned long devIdx,
                      unsigned char *pCfgBuf,
                      unsigned long *pCfgBufSz)
{

  struct libusb_device_handle *handle;
  int                 ret;
  int                 i;
  int                 gainCode;
  int                 calMode;
  int                 trigClk;
  int                 startChan;
  int                 endChan;
  int                 overSample;


 
  if (pCfgBuf == NULL)
  {
    debug("DBG>>AIO_SetConfig : pCfgBuf is NULL\n");
    return (ERROR_INVALID_PARAM); 
  }
  else if (pCfgBufSz == NULL)
 {
    debug("DBG>>AIO_SetConfig : pCfgBufSz is NULL argument \n");
    return (ERROR_INVALID_PARAM); 
 } 

  // validate Gain codes
  for (i = 0; i < 16; i++)
  { 
    gainCode = pCfgBuf[i];
    if ( (gainCode < 0) || (gainCode > AI_16_MAX_GAIN_CODE) )
    {
      debug("DBG>>AIO_SetConfig : Invalid gainCode(%d) must be between 0 and 7 \n",i);
      return (ERROR_INVALID_PARAM); 
    }
  }

  // validate Calibration code
  calMode = pCfgBuf[16];
  if (! ( (calMode == AIO_AI_16_CAL_ACQUIRE_NORM)          || 
          (calMode == AIO_AI_16_CAL_ACQUIRE_GROUND)        ||
          (calMode == AIO_AI_16_CAL_ACQUIRE_REF) )          )
  {
    debug("DBG>>AIO_SetConfig : Invalid Cal Mode (%d) valid vaues are 0-2) \n",calMode); 
    return (ERROR_INVALID_PARAM); 
  }

  // validate trigger and counter clock
  trigClk = pCfgBuf[17];
  if ( (trigClk < 0)  ||
       (trigClk > 127)  ) 
  {
    debug("DBG>>AIO_SetConfig: Invalid trigger 0x(%x). Valid values are 0 - 31\n",trigClk); 
    return (ERROR_INVALID_PARAM); 
  }


  // validate start / end channel
  startChan = pCfgBuf[18] & 0x0F;   
  if ( (startChan <  0)  || 
       (startChan >  AIO_AI_16_MAX_CHAN) )
  {
    debug("DBG>>AIO_SetConfig: Invalid startChan(%d). Valid values are 0 - 15\n",startChan); 
    return (ERROR_INVALID_PARAM); 
  }

  endChan = pCfgBuf[18] >> 4;
  if ( (endChan <  0)  || 
       (endChan >  AIO_AI_16_MAX_CHAN) )
  {
    debug("DBG>>AIO_SetConfig: Invalid startChan(%d). Valid values are 0 - 15\n",endChan); 
    return (ERROR_INVALID_PARAM); 
  }
 
  // validate oversample
  overSample = pCfgBuf[19];
  if ( ! ((overSample == 0) || (overSample == 1) ) ) 
  {
    debug("DBG>>AIO_SetConfig: Invalid overSample(*d). Valid values are 0 - 1 \n"); 
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_ADC_SetConfig: invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)

  {
    debug("DBG>>AIO_ADC_SetConfig: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_DIO_SetConfig: could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);

  }

    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
		          SET_CONFIG,	
                          0, 
                          0,
                          pCfgBuf,
                          20, 
                          TIMEOUT_1_SEC);
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_ADC_SetConfig: usb_control_msg failed ; dev=%d err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }

    *pCfgBufSz = ret; 
	

	
    return (ERROR_SUCCESS);
}

/********************************************************************/
//
//  Function Name : AIO_Usb_GetConfig
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_ADC_GetConfig (unsigned        long devIdx,
                       unsigned char *pCfgBuf,
                       unsigned long *pCfgBufSz) 
{


  struct libusb_device_handle *handle;

  int                 ret;
  unsigned char       cfgBuf[20];


  
  if (pCfgBuf == NULL)
  {
    debug("DBG>>AIO_GetConfig : pCfgBuf is NULL\n");
    return (ERROR_INVALID_PARAM); 
  }
  else if (pCfgBufSz == NULL)
  {
    debug("DBG>>AIO_GetConfig : pCfgBufSz is NULL argument \n");
    return (ERROR_INVALID_PARAM); 
  } 

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_ADC_GetConfig: invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_ADC_GetConfig: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_DIO_GetConfig: could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);

  }
  else
  {
    memset((void *)cfgBuf,0,20);

    ret = libusb_control_transfer(handle,
                          USB_READ_FROM_DEV, 
		          GET_CONFIG,	
                          0, 
                          0,
                          (unsigned char *)pCfgBuf,
                          20, 
                          TIMEOUT_1_SEC);
   
   libusb_close(handle);
						  
    
   if (ret < 0)
   {
    debug("DBG>> AIO_ADC_GetConfig: usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
   {
    *pCfgBufSz = ret; 
	

    return (ERROR_SUCCESS);
   }

 }
}



/********************************************************************/
//
//  Function Name : AIO_Usb_RangeAll
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_ADC_RangeAll (unsigned        long devIdx,
                      unsigned char *pCfgBuf)
{
  struct libusb_device_handle *handle;

  int                 ret;

  if (pCfgBuf== NULL)
  {
    debug("DBG>>AIO_RangeAll : pCfgBuf is NULL argument \n");
    return (ERROR_INVALID_PARAM); 
  } 


  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_ADC_RangeAll: invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)

  {
    debug("DBG>>AIO_ADC_RangeAll: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_DIO_RangeAll: could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);

  }
    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
		          SET_CONFIG,	
                          0, 
                          0,
                          (unsigned char *)pCfgBuf,
                          16, 
                          TIMEOUT_1_SEC);
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_ADC_RangeAll: usb_control_msg failed ; dev=%d err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   
    
    return (ERROR_SUCCESS);
}

/********************************************************************/
//
//  Function Name : AIO_Usb_Range1
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_ADC_Range1 (unsigned        long devIdx,
                    unsigned char   chan,
                    char            gainCode,
                    char            singleEnded)
{

  struct libusb_device_handle *handle;

  int                 ret;
  unsigned char       cfgBuf[20];
  unsigned long       cfgBufSize;


  if ( (chan < 15) || (chan > 15) )
  {
    debug("DBG>>AIO_Range1 : chan(%d) must be between 0 and 15 \n",chan);
    return (ERROR_INVALID_PARAM); 
  }

  if ( (gainCode < 0) || (gainCode > AI_16_MAX_GAIN_CODE) )
  {
    debug("DBG>>AIO_Range1 : gainCode  must be between 0 and 7 \n");
    return (ERROR_INVALID_PARAM); 
  }

  if ( (singleEnded < 0)  || (singleEnded > 1) )
  {
    debug("DBG>>AIO_Range1 : singleEnded(%d) must be 0 or 1 \n",singleEnded);
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_ADC_Range1: invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)

  {
    debug("DBG>>AIO_ADC_Range1: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }



    ret =  AIO_Usb_ADC_GetConfig (devIdx,
                                 &cfgBuf[0],
                                 &cfgBufSize); 


    if (!(singleEnded))
     gainCode += 8;

    cfgBuf[chan] = gainCode;

    ret =  AIO_Usb_ADC_SetConfig (devIdx,
                                  &cfgBuf[0],
                                  &cfgBufSize);

   handle = getDevHandle(devIdx);
   if (handle == NULL)
   {
     debug("DBG>> AIO_Usb_DIO_RangeAll: could not get device handle devIdx=%d \n",(unsigned int)devIdx);
     return (ERROR_COULD_NOT_GET_DEVHANDLE);
   }

    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
		          SET_CONFIG,	
                          0, 
                          0,
                          (unsigned char *)cfgBuf,
                          16, 
                          TIMEOUT_1_SEC);
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_ADC_Range1: usb_control_msg failed ; dev=%d err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
    libusb_close(handle);
    return (ERROR_SUCCESS);
}



/********************************************************************/
//
//  Function Name : AIO_Usb_ADC_Initialize
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
// 
/********************************************************************/

unsigned long
AIO_Usb_ADC_Initialize(unsigned long devIdx,
                      unsigned char *pCfgBuf,
                      unsigned long *pCfgBufSz,
                      char          *calFileName)
{

  int ret;
  ret =   AIO_Usb_ADC_SetCal(devIdx,
                            calFileName);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_Initialize : SetCal Failed devIdx=%d err= %d \n",(unsigned int)devIdx,ret); 
    return (ret);
  }

   ret =   AIO_Usb_ADC_SetConfig (devIdx,
                                  pCfgBuf,
                                  pCfgBufSz);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_Initialize : SetConfig Failed devIdx=%d err= %d \n",(unsigned int)devIdx,ret); 
    return (ret);
  }

  return (ERROR_SUCCESS);
}


/********************************************************************/
//
//  Function Name : AIO_Usb_StartOutputFreq
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//                 Copied verbatim from Windows implementation of the 
//                 Acces I/O API
//
//      This function should no be called by an application. The clocks
//      are set in the BulkAquire call. This is done to insure
//      that the clock is not started until any reads have been posted
//      so that incoming data is not lost
//
//  History	  :
// 
/********************************************************************/
unsigned long
CTR_StartOutputFreq(unsigned long 	devIdx,
                             char 	blockIdx,
                             double    *pHz)
{
  int            ret;

  unsigned long  div1,
                 div2;

  double         
                 Hz,
                 targetDiv,
                 Err,
                 minErr,
                 actualHz,
                 L;


  actualHz = 0.0;
 
  // change to check for card type to make it generic 
  if  ( ( (blockIdx < 0 )  ||
        (blockIdx > NUM_AI_16_COUNTER_BLOCKS) ) )  
  {
    debug("DBG>>AIO_StartOutputFreq : Invalid Block Index (%d) AI-16 only has 3 counter blocks \n",blockIdx); 
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_StartOutputFreq: invalid dev= 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_StartOutputFreq: invalid ProductID for dev= 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }



  if ( *pHz > AI_16_MAX_OUTPUT_FREQ) 
  {
    debug("DBG>>StartOutputFrequency : Invalid Clock Speed (%d) range is 0 - 500000 (devIdx=%d)\n",(unsigned int)*pHz,(unsigned int)devIdx);
    return (ERROR_INVALID_PARAM);
  }


    Hz = *(pHz);
    if (Hz <= 0 ) 
    {
       ret = AIO_Usb_CTR_8254Mode(devIdx,
                                  blockIdx,
                                  1,          //counter index
                                  2);         // mode
       if (ret > ERROR_SUCCESS)
       {
          debug("DBG>>StartOutputFreq : CTR8254Mode devId =%d blk idex = %d counter=1 mode=2  failed; err = %d \n",(unsigned int)devIdx,(unsigned int)blockIdx,ret); 
          return (ret);
       }

       ret = AIO_Usb_CTR_8254Mode(devIdx,
                                  blockIdx,
                                  2,          //counter index
                                  3);         // mode
      if (ret > ERROR_SUCCESS)
      {
        debug("DBG>>StartOutputFreq : CTR8254Mode dev = %d blk idex = %d counter=2 mode=3  failed; err = %d \n",(unsigned int)devIdx,(unsigned int)blockIdx,ret); 
        return (ret);
      }
    }
    else 
    {

      if ( (Hz * 4)  >= USB_SOURCE_CLK)
      {
        div1= 2;
        div2= 2;
      }
      else 
      {

        targetDiv = USB_SOURCE_CLK / Hz;

        L = sqrt(targetDiv);

        div1 = targetDiv / L;

        div2 = L;

        minErr = abs(Hz - (USB_SOURCE_CLK / (div1 * L) ) );
        
        for (L =ceil(L); L >= 2; L--)
        {
          div1 = targetDiv / L;
          if (div1 > 0xFFFF)
          {
            break; // limited to 16 bits, so this and all further L are invalid
          }
          Err = abs(Hz - (USB_SOURCE_CLK / (div1 * L)));
          if (Err == 0)
          {
            div2 = L;
            break;
          }

          if (Err < minErr) 
          {
            div2 = L;     
            minErr = Err;
          }

        }
        div1 = targetDiv / div2;

        actualHz = USB_SOURCE_CLK / (div1 * div2);

        *pHz = actualHz;
     }

      ret = AIO_Usb_CTR_8254ModeLoad(devIdx,
                                     blockIdx,
                                     1,          //counter index
                                     2,          //mode
                                     (unsigned short)div1);  // load value
      if (ret > ERROR_SUCCESS)
      {
        debug("DBG>>StartOutputFreq : CTR8254ModeLoad failed dev = %d \n",(unsigned int)devIdx);
        return (ret);
      }



      ret = AIO_Usb_CTR_8254ModeLoad(devIdx,
                                     blockIdx,
                                     2,         //counter index
                                     3,          //mode
                                     (unsigned short)div2);  // load value

      if (ret > ERROR_SUCCESS)
      {
        debug("DBG>>StartOutputFreq : CTR8254ModeLoad dev = %d \n",(unsigned int)devIdx);
        return (ret);
     }

   } 


/*******************

  // This is cut and pasted code from the Windows USB API which was written in Delphi;
  // Apparently due to precision it cannot be ported verbatim to C. The C
  // version is given above.
  // 
  if ( (*pHz <= 0) || (*pHz > AI_16_MAX_OUTPUT_FREQ) )
  {
    debug("DBG>>StartOutputFrequency : pHz (%d) range is 1 - 500000\n",(unsigned int)*pHz);
    return (ERROR_INVALID_PARAM);
  }

 if ( (*pHz) > AI_16_MAX_OUTPUT_FREQ)
 {
    debug("DBG>>StartOutputFreq: pHz cannot be > MAX_CLOCK_SPEED (%d) 0 \n",AI_16_MAX_OUTPUT_FREQ);
    return (ERROR_INVALID_PARAM);
  }

    Hz = *(pHz);
    if (Hz <= 0 ) 
    {
       ret = AIO_Usb_CTR_8254Mode(devIdx,
                                  blockIdx,
                                  1,          //counter index
                                  2);         // mode
       if (ret > ERROR_SUCCESS)
       {
          debug("DBG>>StartOutputFreq : CTR8254Mode devId =%d blk idex = %d counter=1 mode=2  failed; err = %d \n",(unsigned int)devIdx,(unsigned int)blockIdx,ret); 
          return (ret);
       }

       ret = AIO_Usb_CTR_8254Mode(devIdx,
                                  blockIdx,
                                  2,          //counter index
                                  3);         // mode
      if (ret > ERROR_SUCCESS)
      {
        debug("DBG>>StartOutputFreq : CTR8254Mode dev = %d blk idex = %d counter=2 mode=3  failed; err = %d \n",(unsigned int)devIdx,(unsigned int)blockIdx,ret); 
        return (ret);
      }
    }
    else 
    {

      if ( (Hz * 4)  >= USB_AI16_16_ROOT_CLK_SPEED)
      {
        DivisorA = 2;
        DivisorB = 2;
      }
      else 
      {
        DivisorAB      = USB_AI16_16_ROOT_CLK_SPEED / Hz;

        L              = (unsigned long)rint(sqrt(DivisorAB));
        DivisorA       = (unsigned long)rint(DivisorAB / L);
        DivisorB       = L;

        minErr         = fabs(Hz - (USB_AI16_16_ROOT_CLK_SPEED / (DivisorA * L)));
        for (L=L; L <= 2; L--) 
        {
          DivisorA = (unsigned long)rint(DivisorAB / L);
          if (DivisorA > 0xFFFF) 
           break; //Limited to 16 bits, so this and all further L are invalid.
          Err = fabs(Hz - (USB_AI16_16_ROOT_CLK_SPEED / (DivisorA * L)));
          if (Err== 0)
          {
            DivisorB = L;
            break;
          }
          if (Err < minErr)
          {
            DivisorB = L;
            minErr   = Err;
          } 
        }
       DivisorA = (unsigned long)rint(DivisorAB / DivisorB);
      } 
      LastPurpose = AUR_CTR_PUR_OFRQ;

      *(pHz) = USB_AI16_16_ROOT_CLK_SPEED / (DivisorA * DivisorB);


      ret = AIO_Usb_CTR_8254ModeLoad(devIdx,
                                     blockIdx,
                                     1,          //counter index
                                     2,          //mode
                                     (unsigned short)DivisorA);  // load value
      if (ret > ERROR_SUCCESS)
      {
        debug("DBG>>StartOutputFreq : CTR8254ModeLoad failed dev = %d \n",(unsigned int)devIdx);
        return (ret);
      }

      ret = AIO_Usb_CTR_8254ModeLoad(devIdx,
                                     blockIdx,
                                     2,         //counter index
                                     3,          //mode
                                     (unsigned short)DivisorA);  // load value

      if (ret > ERROR_SUCCESS)
      {
        debug("DBG>>StartOutputFreq : CTR8254ModeLoad dev = %d \n",(unsigned int)devIdx);
        return (ret);
     }

   } 
***************/



   return (ERROR_SUCCESS);
 }

/********************************************************************/
//
//  Function Name : AIO_Usb_CTR_8254ModeLoad
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_CTR_8254ModeLoad(unsigned       long devIdx,
                         char           blockIdx,
                         char           counterIdx, 
                         char           mode,
                         unsigned int   loadValue)
{

  struct libusb_device_handle *handle;

  int                 ret;
  unsigned short      value         = 0;
  char                ctlByte       = 0;
  unsigned char      *pData;

  // change to check for card type to make it generic 

  if   ( (blockIdx < 0 )  ||
       (blockIdx > NUM_AI_16_COUNTER_BLOCKS) )  
  {
    debug("DBG>>AIO_CTR_8254ModeLoad : Invalid Block Index (%d) AI-16 only has 3 counter blocks  \n",blockIdx); 
    return (ERROR_INVALID_PARAM); 
  }

  if   (( counterIdx  < 0 )  ||
        (counterIdx > NUM_AI_16_COUNTERS) )  
  {
    debug("DBG>>AIO_CTR_8254ModeLoad : Invalid Counter Index (%d) AI-16 only has 3 counter blocks  \n",counterIdx); 
    return (ERROR_INVALID_PARAM); 
  }


  if   (( mode < 0 )  ||
       (mode > NUM_AI_16_MODES-1))   
  {
    debug("DBG>>AIO_CTR_8254ModeLoad : Invalid Mode (%d) AI-16 modes are 0-5 \n",mode); 
    return (ERROR_INVALID_PARAM); 
  }

  if   (( loadValue < 0 )  ||
        ( loadValue > 65535))   
  {
    debug("DBG>>AIO_CTR_8254ModeLoad : Invalid load value (%d) range is 0-65535 \n",loadValue); 
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CTR_8254ModeLoad: invalid dev Index = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CTR_8254ModeLoad: invalid ProductID for device = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

    // set up value
    // only 2 least sig bytes of value used
   
    // high byte-mode control
    ctlByte = counterIdx << 6 ; 
    ctlByte = ctlByte | (1 << 5);
    ctlByte = ctlByte | (1 << 4);
    ctlByte = ctlByte | (mode << 1);  

     value = value | (ctlByte << 8);
     value = value | blockIdx;
  
  
  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_CTL_8254ModeLoad: could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }

    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
		          COUNTER_READ_MODE_LOAD, // this is used for LOAD; no "LOAD" vendor request supported 
                          value, 
                          loadValue,
                          pData,  //unused 
                          0, 
                          TIMEOUT_1_SEC);
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_Usb_CTR8254ModeLoad : usb_control_msg failed ; dev=%d err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
   {

     return (ERROR_SUCCESS);
   }


}

/********************************************************************/
//
//  Function Name : AIO_Usb_CTR_8254Mode
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_CTR_8254Mode(unsigned  long devIdx,
                     char      blockIdx,
                     char      counterIdx,
                     char      mode)
{

  struct libusb_device_handle *handle;

  int ret;
  unsigned short      value         = 0;
  char                ctlByte       = 0;
  
  unsigned char      *pData;

  if   ( (blockIdx < 0 )  ||
       (blockIdx > NUM_AI_16_COUNTER_BLOCKS) )  
  {
    debug("DBG>>AIO_CTR_8254Mode : Invalid Block Index (%d) AI-16 only has 3 counter blocks  \n",blockIdx); 
    return (ERROR_INVALID_PARAM); 
  }

  if   (( counterIdx  < 0 )  ||
        (counterIdx > NUM_AI_16_COUNTERS) )  
  {
    debug("DBG>>AIO_CTR_8254Mode : Invalid Counter Index (%d) AI-16 only has 3 counter blocks  \n",counterIdx); 
    return (ERROR_INVALID_PARAM); 
  }


  if   (( mode < 0 )  ||
       (mode > NUM_AI_16_MODES-1))   
  {
    debug("DBG>>AIO_CTR_8254Mode : Invalid Mode (%d) AI-16 modes are 0-5 \n",mode); 
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CTR_8254Mode : invalid dev Index = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CTR_8254Mode: invalid ProductID for device = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_CTR_8254Mode : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }


    // set up value
    // only 2 least sig bytes of value used
   
    // high byte-mode control
    ctlByte = counterIdx << 6 ; 
    ctlByte = ctlByte | (1 << 5);
    ctlByte = ctlByte | (1 << 4);
    ctlByte = ctlByte | (mode << 1);  

   value =  (ctlByte << 8) | blockIdx;

    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
		          COUNTER_MODE, 
                          value, 
                          0,
                          pData,  //unused 
                          0, 
                          TIMEOUT_1_SEC);
						  
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_Usb_CTR8254_Mode : usb_control_msg failed ; (unsigned int)dev=%d err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
   {

    return (ERROR_SUCCESS);
   }

}


/********************************************************************/
//
//  Function Name : AIO_Usb_ClearDevices
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
void
AIO_Usb_DIO_ClearDevices(void)
{

 int i;
 
  for (i=0; i < MAX_USB_DEVICES; i++)
  {
          aioDevs.aioDevList[i].devHandle		= NULL;
          aioDevs.aioDevList[i].device       		= NULL; 
          aioDevs.aioDevList[i].devIdx       		= NO_DEVICE;
          aioDevs.aioDevList[i].productId		= 0;

          aioDevs.aioDevList[i].sampleBytesToRead       = 0;
          aioDevs.aioDevList[i].sampleBytesRead         = 0;
          aioDevs.aioDevList[i].sampleBytesLeftToRead   = 0;
	  aioDevs.aioDevList[i].sampleReadInProgress    = 0;
	  aioDevs.aioDevList[i].sampleReadsRequired     = 0;
	  aioDevs.aioDevList[i].sampleReadsCompleted    = 0;
	  aioDevs.aioDevList[i].sampleReadTimeouts      = 0;
	  aioDevs.aioDevList[i].sampleReadBulkXferError = 0;
          aioDevs.aioDevList[i].pSampleReadBuf          = NULL; 


          aioDevs.aioDevList[i].DIOBytesToRead          = 0;
	  aioDevs.aioDevList[i].DIOBytesRead	 	= 0;
          aioDevs.aioDevList[i].DIOBytesLeftToRead      = 0;
	  aioDevs.aioDevList[i].DIOReadsRequired         = 0;
          aioDevs.aioDevList[i].DIOReadsSubmitted       = 0;
	  aioDevs.aioDevList[i].DIOReadsCompleted       = 0;
	  aioDevs.aioDevList[i].DIOReadBulkXferError    = 0;
          aioDevs.aioDevList[i].pDIOReadBuf             = NULL; 

          aioDevs.aioDevList[i].DIOBytesToWrite	        = 0;
	  aioDevs.aioDevList[i].DIOBytesWritten         = 0;
          aioDevs.aioDevList[i].DIOBytesLeftToWrite     = 0;
	  aioDevs.aioDevList[i].DIOWritesRequired       = 0;
	  aioDevs.aioDevList[i].DIOWritesCompleted      = 0;
	  aioDevs.aioDevList[i].DIOWriteTimeouts        = 0;
	  aioDevs.aioDevList[i].DIOWriteBulkXferError   = 0;
          aioDevs.aioDevList[i].pDIOWriteBuf            = NULL; 

          aioDevs.aioDevList[i].streamOp		= STREAM_OP_NONE;
  }
         
}


/********************************************************************/
//
//  Function Name : AIO_Usb_DIO_WriteAll
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_WriteAll(unsigned long  devIdx,
                 unsigned char *pData) 
{

  int                          ret;
  struct libusb_device_handle *handle;


  if (pData == NULL)
  {
    debug("DBG>>AIO_WriteAll : pData is NULL \n"); 
    return (ERROR_INVALID_PARAM); 
  }
 
  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_WriteAll: invalid dev Index = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_WriteAll: invalid ProductID for device = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


  handle =  getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_DIO_WriteAll : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }
  
	ret = libusb_control_transfer(handle,
								  USB_WRITE_TO_DEV, 
								  DIO_WRITE,	
								  0, 
								  0,
								  pData,
								  14,					//changed to 14 to accommodate 96-channel card
								  TIMEOUT_1_SEC);
/*	ret = libusb_control_transfer(handle,
								  USB_WRITE_TO_DEV, 
								  DIO_WRITE,	
								  0, 
								  0,
								  pData,
								  4, 
								  TIMEOUT_1_SEC);*/
	libusb_close(handle);
  if (ret  < 0 ) 
  {
    debug("DBG>> AIO_Usb_WriteAll: usb_control_msg failed on WRITE_TO_DEV dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
  }
  else
    return (ERROR_SUCCESS);

 }




/********************************************************************/
//
//  Function Name : AIO_Usb_DIO_ReadAll
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_DIO_ReadAll (unsigned long   devIdx,
                     unsigned char  *pData)
{
	
	int ret;
	struct libusb_device_handle *handle;
	
	
	if (pData == NULL)
	{
		debug("DBG>>AIO_ReadAll : pData is NULL \n"); 
		return (ERROR_INVALID_PARAM); 
	}
	
	ret = AIO_UsbValidateDeviceIndex(devIdx);
	
	if (ret > ERROR_SUCCESS)
	{
		debug("DBG>>AIO_DIO_ReadAll: invalid dev Index = 0x%0x \n",(unsigned int)devIdx); 
		return (ret);
	}
	
	ret = validateProductID(devIdx);
	if (ret > ERROR_SUCCESS)
	{
		debug("DBG>>AIO_DIO_ReadAll: invalid Product ID for device = 0x%0x \n",(unsigned int)devIdx); 
		return (ret);
	}
	
	handle = getDevHandle(devIdx);
	if (handle == NULL)
	{
		debug("DBG>> AIO_Usb_DIO_ReadAll : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
		return (ERROR_COULD_NOT_GET_DEVHANDLE);
		
	}
	else
	{
		ret = libusb_control_transfer(handle,
									  USB_READ_FROM_DEV,
									  DIO_READ, 
									  0, 
									  0,
									  pData,
									  14,						//changed to 14 to accommodate 96-channel board
									  TIMEOUT_1_SEC);
/*		ret = libusb_control_transfer(handle,					//Stock function
									  USB_READ_FROM_DEV,
									  DIO_READ, 
									  0, 
									  0,
									  pData,
									  4, 
									  TIMEOUT_1_SEC);*/
		libusb_close(handle);
		
		if (ret < 0)
		{
			debug("DBG>> AIO_Usb_ReadALL : usb_control_msg failed dev = 0x%0x err=%d",(unsigned int)devIdx,ret);
			return (ERROR_USB_CONTROL_MSG_FAILED);
		}
		else
			return (ERROR_SUCCESS);
	}
}


/********************************************************************/
//
//  Function Name : AIO_Usb_BulkAcquire
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
// NOTE : currently the API only supports 1 bulk read at a time pending 
//        per Device
//
// The API may be enhanced to  allow multiple reads in the future
// 
//  History	  :
//
//     
unsigned long
AIO_ADC_Usb_BulkAcquire (unsigned long  devIdx,
                         unsigned char *pBuf,
                         unsigned long  numSamples,
                         double         Hz)
{

  struct libusb_device_handle     *handle;


  int                 ret;
  short               val; 
  short               idx; 
  int                 listIdx; 
  int                 bytesLeft;
  int                 numBytes;
  double              tmpHz; 
  int                 dataRead;

// NOTE : the following two limitations are due
//        to the fact that a beta version of libusb
//        with Darwin support is being used
//        in this initial release of this API. 
//        These will be removed when the API
//        is updated with a newer release of this API.


if (numSamples > 1000)
{
    debug("DBG>>AIO_BulkAquire : numberSamples(%d) larger than Maximum of 1000 for this Release\n",(unsigned int)numSamples);
    return (ERROR_INVALID_PARAM);
}


if (Hz > 50000)
{
    debug("DBG>>AIO_BulkAquire : Hz(%lf) larger than Maximum of 50K  for this Release\n",Hz);
    return (ERROR_INVALID_PARAM);
}



  if (pBuf == NULL)
  {
    debug("DBG>>AIO_BulkAquire : 'pBuf' is NULL \n");
    return (ERROR_INVALID_PARAM);
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_BulkAquire : invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_BulkAquire: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

   listIdx = getListIndex(devIdx);
   if (ret > ERROR_SUCCESS)
   {
     debug("DBG>>BulkAquire : No Entry in lust for devIdex = %d\n",(unsigned int)devIdx); 
     return (ERROR_NO_AIO_DEVS_FOUND);
    }

   if ( (numSamples % 2) != 0)
   {
     debug("DBG>>AIO_BulkAquire :  Number of Samples (%d) Must be a multiple of 1 sample size(2bytes)\n",(unsigned int)numSamples);
     return (ERROR_INVALID_PARAM);
   }

  
   if (numSamples > MAX_SAMPLE_SIZE)
   {
     debug("DBG>>AIO_BulkAquire :  Number  Samples (%d) larger than MAX_SAMPLE_SIZE(%d)\n",(unsigned int)numSamples,MAX_SAMPLE_SIZE);
     return (ERROR_INVALID_PARAM);
   }


  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_BulkAquire : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);

  }
  


   if ( aioDevs.aioDevList[listIdx].sampleReadInProgress)
   {
      debug("DBG>>AIO_BulkAquire : A SampleReadBulkAquire is already in progress for devIdx=%d . Can only have 1 pending at a time\n",(unsigned int)devIdx);
      return (ERROR_BULK_AQUIRE_BUSY);
   }
   else
   {
     aioDevs.aioDevList[listIdx].sampleReadInProgress = 1;
   }


    // each sample is 2 bytes
    numBytes = numSamples * 2;

    aioDevs.aioDevList[listIdx].devHandle              = handle;

    aioDevs.aioDevList[listIdx].sampleBytesRead          = 0;
    aioDevs.aioDevList[listIdx].sampleReadsSubmitted     = 0;
    aioDevs.aioDevList[listIdx].sampleReadsRequired      = (numBytes/BYTES_PER_READ) + 1;
    aioDevs.aioDevList[listIdx].sampleBytesToRead        = numBytes ;
    aioDevs.aioDevList[listIdx].sampleBytesLeftToRead    = numBytes;
    aioDevs.aioDevList[listIdx].pSampleReadBuf           = pBuf;
    aioDevs.aioDevList[listIdx].sampleReadTimeouts       = 0;
    aioDevs.aioDevList[listIdx].sampleReadBulkXferError  = 0;
    aioDevs.aioDevList[listIdx].sampleReadsCompleted     = 0;
    aioDevs.aioDevList[listIdx].samplePrevReadsCompleted = 0;



    // the local bytesLeft count is used here while submitting the reads
    // to know how many bytes to request on each read;
    // the corresponding count set in the aioDev list above
    // is adjusted when the reads complete which happen asynchronously
    // with respect to when the reads are submitted, which is why we
    // need the local one  
    bytesLeft = numBytes ;


    // NOTE:
    // This call does not work currently.
    // It should not be called because
    // after it is no other API call
    // will work and a board reset is required. 
    //
    //     ret =  AIO_Usb_ClearFIFO(devIdx, CLEAR_FIFO_AND_ABORT);
    //    if (ret > ERROR_SUCCESS)
    //   {
    //    debug("DBG>> ClearFIFO : failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    //    return (ret);
    //   }


    // value is the high word of the length of the 
    // data to read in WORDS, index is the low word 
    // user specifies length in bytes but
    // val and idx for the control message call
    // is in 2-byte words so we divide user's length by 2


    val =  (short)((numBytes/ 2) >> 16);
    idx  = (short)( (numBytes/2) & 0x0000FFFF);




     aioDevs.aioDevList[listIdx].sampleReadInProgress = 0;
 
   if (ret < 0)
   {
    debug("DBG>> bulkAquire: usb_claim_interface failed 0x%0x err=%d\n",(unsigned int)devIdx,ret);

    return (ERROR_LIBUSB_CLAIM_INTF_FAILED);
   }
 
   

  


    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV,
		                  BULK_ACQUIRE,	
                          val, 
                          idx,
                          pBuf, // unused
                          0, 
                          TIMEOUT_5_SEC);
						  
  libusb_close(handle);						  
   if (ret < 0)
   {
    debug("DBG>> AIO_BulkAquire: usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
	 
     ret = CTR_StartOutputFreq(devIdx,
                               0,
                               &Hz);

  
    handle = getDevHandle(devIdx); 
    ret = libusb_claim_interface(handle,0); 
int count;	
   
	
	  count = libusb_bulk_transfer(handle,
                                (0x86 | LIBUSB_ENDPOINT_IN),
                                 (unsigned char *)pBuf,
                                 numBytes,
                                 &dataRead,
                                5000);//TIMEOUT_5_SEC); 
  				  
    libusb_close(handle);

	tmpHz = 0;
	
		
	ret = CTR_StartOutputFreq(devIdx,
							  0,
							&tmpHz);

/********

int j;
float volts;

     j=0;
     for (i=0; i < 1000; i++)
     {
       //chan 0
       volts = (unsigned short )pBuf[j];
       volts = (pBuf[j+1] << 8) | (pBuf[j]);
       volts = (volts/65535.0) * 10.0;
	  
	   printf("VOLTS = %f\n",volts);
       j+=2;
      }
		
****************/


    ret = libusb_release_interface(handle,0);
     return (ERROR_SUCCESS);



}



/*********************************************************************   
**** An  version of this function using aynchronous/non-blocking
**** reads from libusb.  This is needed to handle
**** higher data rates and sample sizes but the asynchronous read
**** capability in the initial release of libusb with darwin
**** support was not working when this API was released. 
***********************************************************************
unsigned long
AIO_ADC_Usb_BulkAcquire (unsigned long  devIdx,
                         unsigned char *pBuf,
                         unsigned long  numSamples,
                         double         Hz)
{

  struct libusb_device_handle     *handle;
  bulkXferRec                     *pBulkXferRec;


  int                 i;
  int                 ret;
  short               val; 
  short               idx; 
  int                 listIdx; 
  int                 len;
  int                 bytesLeft;
  int                 numBytes;
  double              tmpHz; 



  if (pBuf == NULL)
  {
    debug("DBG>>AIO_BulkAquire : 'pBuf' is NULL \n");
    return (ERROR_INVALID_PARAM);
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_BulkAquire : invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_BulkAquire: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

   listIdx = getListIndex(devIdx);
   if (ret > ERROR_SUCCESS)
   {
     debug("DBG>>BulkAquire : No Entry in lust for devIdex = %d\n",(unsigned int)devIdx); 
     return (ERROR_NO_AIO_DEVS_FOUND);
    }

   if ( (numSamples % 2) != 0)
   {
     debug("DBG>>AIO_BulkAquire :  Number of Samples (%d) Must be a multiple of 1 sample size(2bytes)\n",(unsigned int)numSamples);
     return (ERROR_INVALID_PARAM);
   }

  
   if (numSamples > MAX_SAMPLE_SIZE)
   {
     debug("DBG>>AIO_BulkAquire :  Number  Samples (%d) larger than MAX_SAMPLE_SIZE(%d)\n",(unsigned int)numSamples,MAX_SAMPLE_SIZE);
     return (ERROR_INVALID_PARAM);
   }


  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_BulkAquire : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);

  }
  


   if ( aioDevs.aioDevList[listIdx].sampleReadInProgress)
   {
      debug("DBG>>AIO_BulkAquire : A SampleReadBulkAquire is already in progress for devIdx=%d . Can only have 1 pending at a time\n",(unsigned int)devIdx);
      return (ERROR_BULK_AQUIRE_BUSY);
   }
   else
   {
     aioDevs.aioDevList[listIdx].sampleReadInProgress = 1;
   }


    // each sample is 2 bytes
    numBytes = numSamples * 2;

    aioDevs.aioDevList[listIdx].devHandle              = handle;

    aioDevs.aioDevList[listIdx].sampleBytesRead          = 0;
    aioDevs.aioDevList[listIdx].sampleReadsSubmitted     = 0;
    aioDevs.aioDevList[listIdx].sampleReadsRequired      = (numBytes/BYTES_PER_READ) + 1;
    aioDevs.aioDevList[listIdx].sampleBytesToRead        = numBytes ;
    aioDevs.aioDevList[listIdx].sampleBytesLeftToRead    = numBytes;
    aioDevs.aioDevList[listIdx].pSampleReadBuf           = pBuf;
    aioDevs.aioDevList[listIdx].sampleReadTimeouts       = 0;
    aioDevs.aioDevList[listIdx].sampleReadBulkXferError  = 0;
    aioDevs.aioDevList[listIdx].sampleReadsCompleted     = 0;
    aioDevs.aioDevList[listIdx].samplePrevReadsCompleted = 0;



    // the local bytesLeft count is used here while submitting the reads
    // to know how many bytes to request on each read;
    // the corresponding count set in the aioDev list above
    // is adjusted when the reads complete which happen asynchronously
    // with respect to when the reads are submitted, which is why we
    // need the local one  
    bytesLeft = numBytes ;


    // NOTE:
    // This call does not work currently.
    // It should not be called because
    // after it is no other API call
    // will work and a board reset is required. 
    //
    //     ret =  AIO_Usb_ClearFIFO(devIdx, CLEAR_FIFO_AND_ABORT);
    //    if (ret > ERROR_SUCCESS)
    //   {
    //    debug("DBG>> ClearFIFO : failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    //    return (ret);
    //   }


    // value is the high word of the length of the 
    // data to read in WORDS, index is the low word 
    // user specifies length in bytes but
    // val and idx for the control message call
    // is in 2-byte words so we divide user's length by 2


    val =  (short)((numBytes/ 2) >> 16);
    idx  = (short)( (numBytes/2) & 0x0000FFFF);


    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV,
		                  BULK_ACQUIRE,	
                          val, 
                          idx,
                          pBuf, // unused
                          0, 
                          TIMEOUT_5_SEC);
   if (ret < 0)
   {
    debug("DBG>> AIO_BulkAquire: usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }



   ret = libusb_claim_interface(handle,0); 
   if (ret < 0)
   {
    debug("DBG>> bulkAquire: usb_claim_interface failed 0x%0x err=%d\n",(unsigned int)devIdx,ret);

    return (ERROR_LIBUSB_CLAIM_INTF_FAILED);
   }


    // stop the clock ; it is started after
    // all the reads are submitted so we do not
    // lose data while submitting the reads
    //    tmpHz = 0;
    //   ret = CTR_StartOutputFreq(devIdx,
    //                             0,
     //                           &Hz);

   
   // submit up to 5 reads in the queue 
   // we always want several pending at any given point in time
   // data overruns in the device 
    for (i=0; 
         i < 10;
        i++)
   {

    if (aioDevs.aioDevList[listIdx].sampleReadsSubmitted < 
       aioDevs.aioDevList[listIdx].sampleReadsRequired )
    {
       if (aioDevs.aioDevList[listIdx].sampleBytesLeftToRead >= BYTES_PER_READ)
         len = BYTES_PER_READ;
       else
         len = aioDevs.aioDevList[listIdx].sampleBytesLeftToRead;



        // get a xfer struct 
         pBulkXferRec = getBulkXferRec();
         if (pBulkXferRec == NULL)
         {
           debug("DBG>> bulkAquire: MAX_PENDING_XFERS EXceeded \n");
           return (ERROR_MAX_XFERS_EXCEEDED);
         }

          pBulkXferRec->userData.devIdx = devIdx;

          // this is the beginning of the caller of this functions buffer
          // data is appended to it upon each transfer completion in the sampleBulkReadCallback
          pBulkXferRec->userData.pBuf   = pBuf;  

  


        libusb_fill_bulk_transfer(pBulkXferRec->pXfer, 
                                  handle, 
                                 (6 | LIBUSB_ENDPOINT_IN),
                                  pBulkXferRec->userData.pXferBuf,
                                  len,
                                  sampleBulkReadCallBack, 
                                  (void *)(pBulkXferRec),
                                  0);
        
        ret = libusb_submit_transfer(pBulkXferRec->pXfer);
        if (ret < 0 )
        {
          freeBulkXferRec(pBulkXferRec);
          debug("DBG>> bulkAquire: libusb_submit_transfer failed ; ret = %d\n",ret);
          ret = libusb_release_interface(handle,0); 
          return (ERROR_LIBUSB_SUBMIT_XFER_FAILED);
        }

      aioDevs.aioDevList[listIdx].sampleReadsSubmitted++;

      bytesLeft = bytesLeft - len;
     }
}
     // libusb_close(handle);
     // start the Clock 
libusb_close(handle);

     ret = CTR_StartOutputFreq(devIdx,
                               0,
                               &Hz);



    while ( (aioDevs.aioDevList[listIdx].sampleReadsCompleted != 
              aioDevs.aioDevList[listIdx].sampleReadsRequired)    && 
            (aioDevs.aioDevList[listIdx].sampleReadTimeouts <
              MAX_BULK_XFER_TIMEOUTS)                             && 
            (g_fatalSystemError != 1)                               )
     {       
       ret = libusb_handle_events(NULL);
       if (ret < 0 )
       {
         debug("DBG>>AIO_Usb_BulkAquire: FATAL SYSTEM ERROR : libusb_handle_events failed status=%d\n",ret);
         return (ERROR_LIBUSB_HANDLE_EVENTS_FAILED);
       }

      if (aioDevs.aioDevList[listIdx].samplePrevReadsCompleted == aioDevs.aioDevList[listIdx].sampleReadsCompleted)
      {
         aioDevs.aioDevList[listIdx].sampleReadTimeouts++;
      } 

      aioDevs.aioDevList[listIdx].samplePrevReadsCompleted = 
        aioDevs.aioDevList[listIdx].sampleReadsCompleted;

//printf("READS COMPLETED = %d\n",aioDevs.aioDevList[listIdx].sampleReadsCompleted);

     }


     if (aioDevs.aioDevList[listIdx].sampleReadTimeouts >= MAX_BULK_XFER_TIMEOUTS)
     {
        tmpHz = 0;
        ret = CTR_StartOutputFreq(devIdx,
                                  0,
                                 &tmpHz);

        ret = libusb_release_interface(handle,0); 

        aioDevs.aioDevList[listIdx].sampleReadInProgress   = 0;

        debug("DBG>> BulkAquire : READ FAILURE Exceeded maximum number of bulkXfer read timeouts(5)\n");
        return (ERROR_READ_TIMEOUT);
     }
     

     aioDevs.aioDevList[listIdx].sampleReadInProgress   = 0;

    // stop the clock 
    tmpHz = 0;
    ret = CTR_StartOutputFreq(devIdx,
                              0,
                              &tmpHz);


    ret = libusb_release_interface(handle,0);

//libusb_close(handle);
     return (ERROR_SUCCESS);
}

*************/




// Does NOT Currently WORK. DO NOT USE
int
AIO_loadFW(unsigned long devIdx)


{                           
/********
  int   	 devFound = 0;
  int   	 i;
  int   	 ret;
  char           cmd[20];
  char           dn,fn;

    // find device in list
    for (i=0; i < MAX_USB_DEVICES; i++)
    {
      if (aioDevs.aioDevList[i].devIdx != NO_DEVICE)
      devFound = 1;
      break;
    } 

    if (devFound)
    {
       // in Linux each usb device has
       // a filename associated with it. 
       // This is a hack to load the FW 
       // using the path/filename and a
       // small shellscript. 
       // AIO_loadFW is a shell script that must
       // be present in the directory where
       // your application is invoked from;
       // it calls fxload to load the FW.
       // 
       //  This code could/should use standard hotplug/udev
       //  Known issue:  

       strncpy (cmd,"./AIO_loadFW /dev/bus/usb/00",14 );

       // assuming there will not be more that 9 usb  buses
       // so only need 1 char for dir name (which is the bus number) 
       //dn[0] = aioDevs.aioDevList[i].device->bus_number;
       printf("ssssssssss%c",aioDevs.aioDevList[i].device->bus_number);
       //dn = aioDevs.aioDevList[i].device->bus_number;

       strncat (cmd,&dn[0],1);

       strncat (cmd,"/0",2);

       if (intFilename < 10)
       {
         strncat (cmd,"0",1);
         strncat (cmd,aioDevs.aioDevList[i].device->device_address,1);
       }
       else
         strncat (cmd,aioDevs.aioDevList[i].device->device_address,2);

       ret = system (cmd);
       if (ret == -1)
       {
         debug("DBG>> loadSW: call to 'system()' failed \n"); 
         return ERROR_FW_LOAD_FAILED;
       } 
    }
    else
    {
      debug("DBG>> loadFW: deviceIndex on in asioDevList devIdx = %d\n",(unsigned int)devIdx); 
      return (ERROR_NO_AIO_DEVS_FOUND);
    } 
********/

   return ERROR_SUCCESS;
}




/********************************************************************/
//
//  Function Name : AIO_BulkPoll 
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
// 
//  History	  :
//
//                  
unsigned long 
AIO_ADC_Usb_BulkPoll(unsigned long  devIdx)

{

  int idx;
  int ret;

// NOTE : the following  limitation is due
//        to the fact that a beta version of libusb
//        with Darwin support is being used
//        in this initial release of this API. 
//        These will be removed when the API
//        is updated with a newer release of this API.

return (ERROR_FUNCTION_NOT_SUPPORTED);

  idx = getListIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    g_fatalSystemError = 1;
    debug("DBG>>AIO_Usb_BulkPoll: No Entry in lust for devIdex = %d\n",(unsigned int)devIdx); 
  }

  return (aioDevs.aioDevList[idx].sampleBytesLeftToRead);
}


/********************************************************************/
//
//  Function Name : AIO_Usb_DIO_ConfigureEx
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//                  Only used on USB-DIO16-16* product family
//
//                  Same as Configure, except adds an additional
//                  triState bit
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_DIO_ConfigureEx (unsigned long  devIdx,
                         unsigned char  *pOutMask,
                         unsigned char  *pData,
                         unsigned int   triState)
{

  int                 ret;
  unsigned char       toBoard[6];

  struct libusb_device_handle *handle;

  unsigned long       prodID;
  unsigned long       nameSize;
  char                name[100];
  unsigned long       DIOBytes;
  unsigned long       counters;


  if (pData == NULL)
  {
    debug("DBG>>AIO_DIO_ConfigureEx: databuf  is NULL \n");
    return (ERROR_INVALID_PARAM); 
  }

  if (pOutMask == NULL)
  {
    debug("DBG>>AIO_DIO_ConfigureEx: OutMask is NULL \n");
    return (ERROR_INVALID_PARAM); 
  }

  // tristate bits can only be 00-11;  bit 0 is for portA; bit is for B,C,D
  if ( (triState < 0) || (triState > 3) )
  {
    debug("DBG>>AIO_DIO_ConfigureEx: triSate(%d) must be 0x00, 0x01, 0x11",(unsigned int)triState); 
    return (ERROR_INVALID_PARAM); 
  }


  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_ConfigureEx: invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_ConfigureEx: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_DIO_ConfigureEx : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }
  else
  {


    ret   = AIO_Usb_QueryDeviceInfo(devIdx,
                                    &prodID,
                                    &nameSize,
                                    name,
                                    &DIOBytes,
                                    &counters);
    if (DIOBytes == 2)
    {
      toBoard[0] = pData[0];
      toBoard[1] = pData[1];
      toBoard[2] = *pOutMask; 
      toBoard[3] = 0;
      toBoard[4] = 0;
      toBoard[5] = 0;        // reserved
    }
    else
    {
      toBoard[0] = pData[0];
      toBoard[1] = pData[1];
      toBoard[2] = pData[2];
      toBoard[3] = pData[3];
      toBoard[4] = *pOutMask;
      toBoard[5] = 0;        // reserved
    }

    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
		          DIO_CONFIG,	
                          triState, 
                          0,
                          (unsigned char *)toBoard, 
                          6, 
                          TIMEOUT_1_SEC);
						  
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_Usb_DIO_ConfigureEx : usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
   {
    return (ERROR_SUCCESS);
   }

 }
}


/********************************************************************/
//
//  Function Name : AIO_Usb_DIO_Configure
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_DIO_Configure(unsigned long  devIdx,
                      unsigned int   triState,
                      unsigned char *pOutMask,
                      unsigned char *pData)
{
  unsigned long       prodID;
  unsigned long       nameSize;
  char                name[100];
  unsigned long       DIOBytes;
  unsigned long       counters;

  int                 ret;
//	unsigned char       toBoard[6];//stock
	unsigned char       toBoard[16];

  struct libusb_device_handle *handle;

  if (pOutMask == NULL)
  {
    debug("DBG>>AIO_DIO_Configure:  OutMask is NULL \n");
    return (ERROR_INVALID_PARAM); 
  }

  if (pData == NULL)
  {
    debug("DBG>>AIO_DIO_Configure: databuf  is NULL \n");
    return (ERROR_INVALID_PARAM); 
  }


  if ( (triState < 0) || (triState > 1) )
  {
    debug("DBG>>AIO_DIO_Configure: triSate(%d) must be 0x00  or 0x01\n",(unsigned int)triState); 
    return (ERROR_INVALID_PARAM); 
  }


  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_Configure: invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_Configure: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_DIO_Configure : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }
  else
  {

    ret   = AIO_Usb_QueryDeviceInfo(devIdx,
                                    &prodID,
                                    &nameSize,
                                    name,
                                    &DIOBytes,
                                    &counters);

   if (DIOBytes == 2)
   {
    toBoard[0] = pData[0];
    toBoard[1] = pData[1];
    toBoard[2] = *pOutMask; 
    toBoard[3] = 0; 
    toBoard[4] = 0;
    toBoard[5] = 0; // reserved
   }
   else if (DIOBytes ==4)
   {
	   toBoard[0] = pData[0];
	   toBoard[1] = pData[1];
	   toBoard[2] = pData[2]; 
	   toBoard[3] = pData[3];
	   toBoard[4] = *pOutMask;
	   toBoard[5] = 0; // reserved
   }
	  
	  
	  
	  /////////////////////////////////////////My code to allow use of newer dio-96
   else 
   {
	   toBoard[0] = pData[0];
	   toBoard[1] = pData[1];
	   toBoard[2] = pData[2]; 
	   toBoard[3] = pData[3];
	   toBoard[4] = pData[4];
	   toBoard[5] = pData[5];
	   toBoard[6] = pData[6];
	   toBoard[7] = pData[7];
	   toBoard[8] = pData[8];
	   toBoard[9] = pData[9];
	   toBoard[10] = pData[10];
	   toBoard[11] = pData[11];
	   toBoard[12] = pOutMask[0];
	   toBoard[13] = pOutMask[1];
	   toBoard[14] = 0; // reserved
   }
	  ////////////////////////////////////////End of my code
	  
	  
	  ret = libusb_control_transfer(handle,
									USB_WRITE_TO_DEV, 
									DIO_CONFIG,	
									triState, 
									0,
									(unsigned char *)toBoard, 
									14,								//Writes to more bytes for dio-96
									TIMEOUT_1_SEC);
/*	  ret = libusb_control_transfer(handle,
									USB_WRITE_TO_DEV, 
									DIO_CONFIG,	
									triState, 
									0,
									(unsigned char *)toBoard, 
									6, 
									TIMEOUT_1_SEC);
*/	  
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_Usb_DIO_Configure : usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
   {
    return (ERROR_SUCCESS);
   }

 }
}


/********************************************************************/
//
//  Function Name : AIO_Usb_DIO_Write1
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_Write1(unsigned long  devIdx,
               int            bitIndex,
               char  setBit) 
{

  struct libusb_device_handle *handle;

  int                 tmp;
  int                 ret;
  unsigned char       dataRead[4];
  //unsigned char       writeBack[4];
  //unsigned long       dataReadAsLong    = 0;
  //unsigned long       dataToWriteAsLong = 0;

  if ( (bitIndex < 0) || (bitIndex > 15) )
  {
    debug("DBG>>AIO_DIO_Write1: bitIndex(%d) must be 0-31\n",bitIndex); 
    return (ERROR_INVALID_PARAM); 
  }

  if ( (setBit != 0) && (setBit != 1) )
  {
    debug("DBG>>AIO_DIO_Write1: setBit (%d) must be 0 or 1 \n",bitIndex); 
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_Write1: invalid dev Index = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_Write1: invalid ProductID for device : 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_UsbDIO_Write1: could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }
  else
  {


    //  NOTE we are storing output bytes here
    //  temporarily as a long to perform bit opertion on the sequence of byte
    //
    //  if byte[0] = 0x0A
    //          1  = 0x0B
    //          2  = 0x0D
    //          3  = 0x0D
    //
    //  the long = 0x0A0B0C0D 

    ret = AIO_Usb_DIO_ReadAll(devIdx,
                               &dataRead[0]);
   if (ret < 0)
   {
     debug("DBG>> AIO_Usb_Write1 : ReadAll failed = 0x%0x err=%d",(unsigned int)devIdx,ret);
     return (ret);
   }
/********

  // set or clear the desired bit
  dataReadAsLong = (dataReadAsLong | (dataRead[3] << 24) );
  dataReadAsLong = (dataReadAsLong | (dataRead[2] << 16) );
  dataReadAsLong = (dataReadAsLong | (dataRead[1] << 8) );
  dataReadAsLong = (dataReadAsLong | (dataRead[0]) );

 

  writeBack[0] = (dataToWriteAsLong & 0xFF000000) >> 24;
  writeBack[1] = (dataToWriteAsLong & 0x00FF0000) >> 16;
  writeBack[2] = (dataToWriteAsLong & 0x0000FF00) >> 8;
  writeBack[3] = (dataToWriteAsLong & 0x000000FF);
**********/
  if (bitIndex > 7)
  {
   bitIndex = (bitIndex - 8);   

   if (setBit)
     dataRead[1]  = dataRead[1] | (1 << bitIndex);
   else
   { 
     tmp = dataRead[1];
     clear_bit(&tmp,bitIndex);
     dataRead[1] = tmp;
   } 
  } 
  else 
  {
   if (setBit)
     dataRead[0]  = dataRead[0] | (1 << bitIndex);
   else
   { 
     tmp = dataRead[0];
     clear_bit(&tmp,bitIndex);
     dataRead[0] = tmp;
   } 
  }

  ret = libusb_control_transfer(handle,
                        USB_WRITE_TO_DEV, 
                        DIO_WRITE,	
                        0, 
                        0,
                       (unsigned char *)dataRead,
                        6, 
                        TIMEOUT_1_SEC);
						
  libusb_close(handle);
  //  write them back
  if (ret  < 0 ) 
  {
    debug("DBG>> AIO_Usb_Write1: usb_control_msg failed on WRITE_TO_DEV dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
  }
  else
    return (ERROR_SUCCESS);

 }
}

/********************************************************************/
//
//  Function Name : AIO_Usb_DIO_Write8
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_Write8(unsigned long  devIdx,
               unsigned long  byteIndex,
               unsigned char  byte) 
{


  int                 ret;
  unsigned char       dataRead[4];



  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_Write8: invalid dev Index = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_Write8: invalid ProductID for device : 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


    ret = AIO_Usb_DIO_ReadAll(devIdx,
                               &dataRead[0]);
      
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_Write8: ReadAll failed Index = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }
   // overwrite desired byte
   switch (byteIndex)
   {
     case 0:
     {
       dataRead[0] = byte;
       break;
     }
     case 1:
     {
       dataRead[1] = byte;
       break;
     }
     case 2:
     {
       dataRead[2] = byte;
       break;
     }
     case 3:
     {
       dataRead[3] = byte;
       break;
     }

     default:
      break;
   }

   dataRead[byteIndex] = byte;

   ret = AIO_Usb_WriteAll(devIdx,
                          &dataRead[0]);

  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_Write8: WriteAll failed  Index = %d err=%d\n",(int)devIdx,ret); 
    return (ret);
  }
  else
   return (ERROR_SUCCESS);
}


/********************************************************************/
//
//  Function Name : AIO_Usb_DIO_Read8
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_DIO_Read8 (unsigned long  devIdx,
                   int            byteIdx,
                   unsigned char *pData)
{
  
  int                 ret;
  unsigned char       dataRead[4];

  if (pData == NULL)
  {
    debug("DBG>>AIO_Read8: pData is NULL  \n"); 
    return (ERROR_INVALID_PARAM); 
  }

  if ( (byteIdx < 0) || (byteIdx > 3) )
  {
    debug("DBG>>AIO_Read8:byteIdx(%d) must be between 0 and 0  \n",byteIdx); 
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_Read8: invalid dev= 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_DIO_Read8: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


  ret = AIO_Usb_DIO_ReadAll (devIdx,
                             &dataRead[0]);
  if (ret < 0)
  {
    debug("DBG>> AIO_Usb_Read8 : ReadAll failed dev = 0x%0x err=%d",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
   {
     // overwrite desired byte
     switch (byteIdx)
     {
       case 0:
       {
         *pData = dataRead[2];
         break;
       }
       case 1:
       {
         *pData = dataRead[3];
         break;
       }
       case 2:
       {
         *pData = dataRead[0];
         break;
       }
       case 3:
       {
         *pData = dataRead[1];
         break;
       }

       default:
        break;
     }


     return (ERROR_SUCCESS);
   }
      
 }

/********************************************************************/
//
//  Function Name : AIO_Usb_CTR_8254Load
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_CTR_8254Load(unsigned       long devIdx,
                     char           blockIdx,
                     char           counterIdx,
                     unsigned int   loadValue)
{


  struct libusb_device_handle *handle;

  int ret;
  unsigned short      value         = 0;
  char                ctlByte       = 0;
  char                data[2];

  if   ( (blockIdx < 0 )  ||
       (blockIdx > NUM_AI_16_COUNTER_BLOCKS) )  
  {
    debug("DBG>>AIO_CTR_8254Load : Invalid Block Index (%d) AI-16 only has 3 counter blocks  \n",blockIdx); 
    return (ERROR_INVALID_PARAM); 
  }

  if   (( counterIdx  < 0 )  ||
        (counterIdx > NUM_AI_16_COUNTERS) )  
  {
    debug("DBG>>AIO_CTR_8254Load : Invalid Counter Index (%d) AI-16 only has 3 counter blocks  \n",counterIdx); 
    return (ERROR_INVALID_PARAM); 
  }

  if   (( loadValue < 0 )  ||
        ( loadValue > 65535))   
  {
    debug("DBG>>AIO_CTR_8254Load : Invalid load value (%d) range is 0-65535 \n",loadValue); 
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CTR_8254Load : invalid dev Index = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CTR_8254Load: invalid ProductID for device = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_CTR_8254Loa : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }
  else
  {
    // set up value
    // only 2 least sig bytes of value used
   
    // high byte-mode control
    // only bits 6-7 used 
    ctlByte = counterIdx << 6 ; 

   value = value | (ctlByte << 8);
   value = value | blockIdx;

    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
		          COUNTER_LOAD, 
                          value, 
                          loadValue,
                          (unsigned char*)&data[0],  //unused 
                          2, 
                          TIMEOUT_1_SEC);
						  
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_Usb_CTR8254Load : usb_control_msg failed ; (unsigned int)dev=%d err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
    return (ERROR_SUCCESS);


 }

}




/********************************************************************/
//
//  Function Name : AIO_Usb_8254ReadModeLoad
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_CTR_8254ReadModeLoad(unsigned        long devIdx,
                             char            blockIdx,
                             char            counterIdx,
                             char            mode,
                             unsigned int    loadValue,
                             unsigned short *counterVal)
{

  struct libusb_device_handle *handle;

  int ret;
  unsigned short      value         = 0;
  char                ctlByte       = 0;
  char                data[10];

  if (counterVal == NULL) 
  {
    debug("DBG>>AIO_CTR_8254ReadModeLoad : counterVal is NULL \n"); 
    return (ERROR_INVALID_PARAM); 
  }

  if   ( (blockIdx < 0 )  ||
       (blockIdx > NUM_AI_16_COUNTER_BLOCKS) )  
  {
    debug("DBG>>AIO_CTR_8254ReadModeLoad : Invalid Block Index (%d) AI-16 only has 3 counter blocks  \n",blockIdx); 
    return (ERROR_INVALID_PARAM); 
  }

  if   (( counterIdx  < 0 )  ||
        (counterIdx > NUM_AI_16_COUNTERS) )  
  {
    debug("DBG>>AIO_CTR_8254ReadModeLoad : Invalid Counter Index (%d) AI-16 only has 3 counter blocks  \n",counterIdx); 
    return (ERROR_INVALID_PARAM); 
  }


  if   (( mode < 0 )  ||
       (mode > NUM_AI_16_MODES-1))   
  {
    debug("DBG>>AIO_CTR_8254ReadModeLoad : Invalid Mode (%d) AI-16 modes are 0-5 \n",mode); 
    return (ERROR_INVALID_PARAM); 
  }


  if   (( loadValue < 0 )  ||
        ( loadValue > 65535))   
  {
    debug("DBG>>AIO_CTR_8254ReadModeLoad : Invalid load value (%d) range is 0-65535 \n",loadValue); 
    return (ERROR_INVALID_PARAM); 
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CTR_8254ReadModeLoad: invalid ProductID for device = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CTR_8254ReadModeLoad: invalid ProductID for device = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CTR_8254ReadModeLoad: invalid dev Index = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_CTR_8254ReadModeLoad : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }
  else
  {
    // set up value
    // only 2 least sig bytes of value used
   
    // high byte-mode control
    ctlByte = counterIdx << 6 ; 
    ctlByte = ctlByte | (1 << 5);
    ctlByte = ctlByte | (1 << 4);
    ctlByte = ctlByte | (mode << 1);  

     value = value | (ctlByte << 8);
     value = value | blockIdx;

    ret = libusb_control_transfer(handle,
                          USB_READ_FROM_DEV,
						COUNTER_READ_MODE_LOAD, // this is used for LOAD; no "LOAD" vendor request supported 
                          value, 
                          loadValue,
                          (unsigned char *)&data[0],  //unused 
                          2, 
                          TIMEOUT_1_SEC);
						  
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_Usb_CTR8254ReadModeLoade: usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
   {
      // convert pData to unsigned short
     *counterVal = 0;
     *counterVal = *counterVal | (data[0] << 8);
     *counterVal = *counterVal | data[1];

      return (ERROR_SUCCESS);
   }

 }

}


/********************************************************************/
//
//  Function Name : AIO_Usb_CTR_8254Read
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_CTR_8254Read(unsigned        long devIdx,
                     char            blockIdx,
                     char            counterIdx,
                     unsigned short *counterVal)
{

  struct libusb_device_handle *handle;

  int ret;
  unsigned short      value         = 0;
  char               data[0];

  if (counterVal == NULL) 
  {
    debug("DBG>>AIO_CTR_8254Rea: counterVal is NULL \n"); 
    return (ERROR_INVALID_PARAM); 
  }

  if   ( (blockIdx < 0 )  ||
       (blockIdx > NUM_AI_16_COUNTER_BLOCKS) )  
  {
    debug("DBG>>AIO_CTR_8254Read: Invalid Block Index (%d) AI-16 only has 3 counter blocks  \n",blockIdx); 
    return (ERROR_INVALID_PARAM); 
  }

  if   (( counterIdx  < 0 )  ||
        (counterIdx > NUM_AI_16_COUNTERS) )  
  {
    debug("DBG>>AIO_CTR_8254Read: Invalid Counter Index (%d) AI-16 only has 3 counter blocks  \n",counterIdx); 
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CTR_8254Read: invalid dev Index = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CTR_8254Read: invalid ProductID for device = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_CTR_8254Read: could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }
  else
  {
    // set up value
    // only 2 least sig bytes of value used
   
     value = value | (counterIdx << 8);
     value = value | blockIdx;

    ret = libusb_control_transfer(handle,
                          USB_READ_FROM_DEV,
		          COUNTER_READ, 
                          value, 
                          0,
                          (unsigned char*)&data[0], 
                          2, 
                          TIMEOUT_1_SEC);
						  
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_Usb_CTR8254Read : usb_control_msg failed ; dev=%d err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
   {
      // convert pData to unsigned short
     *counterVal = 0;
     *counterVal = *counterVal | (data[0] << 8);
     *counterVal = *counterVal | data[1];

      return (ERROR_SUCCESS);
   }
 }
}



/********************************************************************/
//
//  Function Name : AIO_Usb_GetImmediate
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_ADC_GetImmediate(unsigned         long devIdx,
                         unsigned short  *sample)
{

  struct libusb_device_handle *handle;

  int                 ret;

  if (sample == NULL) 
  {
    debug("DBG>>AIO_GetImmediate : sample is NULL \n"); 
    return (ERROR_INVALID_PARAM); 
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_ADC_GetImmediate: invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_ADC_GetImmediate: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_GetImmediate: could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }
  else
  {

    ret = libusb_control_transfer(handle,
                          USB_READ_FROM_DEV, 
		          GET_IMMEDIATE,	
                          0, 
                          0,
                          (unsigned char *)sample,
                          2, 
                          TIMEOUT_1_SEC);
  libusb_close(handle);				  
						  
    
   if (ret < 0)
   {
    debug("DBG>> AIO_ADC_GetImmediate: usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }

    return (ERROR_SUCCESS);

 }
}


/********************************************************************/
//
//  Function Name : AIO_Usb_ADC_ADMode
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_ADC_ADMode(unsigned long    devIdx,
                   unsigned int     triggerMode,
                   unsigned char    calMode)
{

  struct libusb_device_handle *handle;

  int                 ret;
  unsigned char       cfgBuf[20];
  unsigned long       cfgBufSize;


  if ( (triggerMode < 0)  ||
       (triggerMode > 31)  ) 
  {
    debug("DBG>>AIO_ADC_Mode: Invalid trigger. Valid values are 0 - 31\n"); 
    return (ERROR_INVALID_PARAM); 
  }

 
  if (! ( (calMode == AIO_AI_16_CAL_ACQUIRE_NORM)      || 
          (calMode == AIO_AI_16_CAL_ACQUIRE_GROUND)    ||
          (calMode == AIO_AI_16_CAL_ACQUIRE_REF) )     )
  {
    debug("DBG>>AIO_ADCMode: Invalid Cal Mode (%d) valid vaues are 0-2) \n",calMode); 
    return (ERROR_INVALID_PARAM); 
  }



  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_ADC_ADMode: invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)

  {
    debug("DBG>>AIO_ADC_ADMode: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }



    ret =  AIO_Usb_ADC_GetConfig (devIdx,
                                 &cfgBuf[0],
                                 &cfgBufSize); 

    if (ret > ERROR_SUCCESS)
    {
      debug("DBG>> AIO_ADC_ADMode: GetConfig failed dev = 0x%0x err=%d\n",(unsigned int)devIdx,ret);
      return (ret);
    }


    ret =  AIO_Usb_ADC_SetConfig (devIdx,
                                  &cfgBuf[0],
                                  &cfgBufSize);

   
    if (ret > ERROR_SUCCESS)
    {
      debug("DBG>> AIO_ADC_ADMode: SetConfig failed dev = 0x%0x err= %d\n",(unsigned int)devIdx,ret);
      return (ret);
    }

    cfgBuf[16] = calMode; 
    cfgBuf[17] = triggerMode; 

    if (ret > ERROR_SUCCESS)
    {
     debug("AIO_Usb_ADC_ADMODE >> GetConfig failed, dev = %d error = %d\n",(unsigned int)devIdx,ret);
     return (ret);
    } 


    handle = getDevHandle(devIdx);
    if (handle == NULL)
    {
      debug("DBG>> AIO_ADC_Mode : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
      return (ERROR_COULD_NOT_GET_DEVHANDLE);
     }

    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
		          SET_CONFIG,	
                          0, 
                          0,
                         (unsigned char *)cfgBuf,
			              16, 
                        TIMEOUT_1_SEC);
						
	  libusb_close(handle);
		
	   if (ret < 0)
	   {
	    debug("DBG>> AIO_ADC_ADMode: usb_control_msg failed ; dev=%d err=%d\n",(unsigned int)devIdx,ret);
	    return (ERROR_USB_CONTROL_MSG_FAILED);
	   }

	    return (ERROR_SUCCESS);
	}

	/********************************************************************/
	//
	//  Function Name : AIO_Usb_AD_QueryCal
	//
	//  Description   :
	//
	//  Returns       :	
	//
	//  Notes
	//
	//  History	  :
	unsigned long
	AIO_Usb_ADC_QueryCal(unsigned long  devIdx,
			     unsigned char *calSupported)
	{

	  int                 ret;

	  struct libusb_device_handle *handle;
	  


	  if (calSupported == NULL)
	  {
	    debug("DBG>>AIO_QueryMode: calSupported == NULL  \n"); 
	    return (ERROR_INVALID_PARAM); 
	  } 

	  ret = AIO_UsbValidateDeviceIndex(devIdx);
	  if (ret > ERROR_SUCCESS)
	  {
	    debug("DBG>>AIO_QueryCal: invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
	    return (ret);
	  }

	  ret = validateProductID(devIdx);
	  if (ret > ERROR_SUCCESS)
	  {
	    debug("DBG>>AIO_QueryCal: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
	    return (ret);
	  }


           handle = getDevHandle(devIdx);
           if (handle == NULL)
           {
             debug("DBG>> AIO_ADC_QueryCal : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
             return (ERROR_COULD_NOT_GET_DEVHANDLE);
	  }

	    ret = libusb_control_transfer(handle,
				  USB_READ_FROM_DEV, 
				  QUERY_CAL,	
				  0, 
				  0,
				  (unsigned char *)calSupported, 
				  1, 
				  TIMEOUT_1_SEC);
				  
	   libusb_close(handle);
	   if (ret < 0)
	   {
	    debug("DBG>> AIO_Usb_QueryCal : usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
	    return (ERROR_USB_CONTROL_MSG_FAILED);
	   }
	   else
	   {
	    return (ERROR_SUCCESS);
	   }

	 }

/********************************************************************/
//
//  Function Name : AIO_Usb_CustomEEPROMWrite
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_CustomEEPROMWrite(unsigned long  devIdx,
                            unsigned int   startAddr,
                            unsigned int   dataSize,
                            void          *pData)
{

  int                 ret;
  
  struct libusb_device_handle *handle;



   if ( (startAddr <  AIO_AI_16_MIN_EEPROM_ADDR) ||
        (startAddr > AIO_AI_16_MAX_EEPROM_ADDR    ))
   {
     debug("DBG>>AIO_CustomEEPROM: Invalid StartAddr(%d) Valid values are 0x000 = 0x01FF\n",startAddr); 
     return (ERROR_INVALID_PARAM); 
    } 

   if ( (dataSize < 0) ||
        (dataSize > AIO_AI_16_MAX_EEPROM_DATSIZE))
   {
     debug("DBG>>AIO_CustomEEPROM: Invalid Datasize (%0d)== NULL  \n",dataSize); 
     return (ERROR_INVALID_PARAM); 
   }

   if (pData == NULL)
   {
     debug("DBG>>AIO_CustomEEPRON: pData == NULL  \n"); 
     return (ERROR_INVALID_PARAM); 
   }

 
  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CustomEEPROMWrite : invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CustomEEPROMWrite : invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_CustomEEPROM_Write : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }
  else
  {

    // addresses 0x0000 - 0x1E00 contain program code and must not be overwritten
    //  user's see their custom address space as 0x0000-0x1FFF
    //  and so we take the customer's start address and add 0x1E00 to it
    // 
    //  note also that because max custom addr is 0x1FF, (startAddr + dataSize) 
    //  must be less than 0x200
    //  

    if ( (startAddr + dataSize) > 0x1FF)
    {
       debug("DBG>>AIO_Usb_CustomEEPROMWrite: EEPROM addr range exceeded. StartAddr=0x%0x size=0x%0x\n",(unsigned int)startAddr,(unsigned int)dataSize);
       debug("      Max allowable address is 0x1FF \n");

       return (ERROR_EEPROM_ADDR_OUT_OF_RANGE);
    }

    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
		                  EEPROM_WRITE,	
                          (startAddr + 0x1E00), 
                          0,
                          (unsigned char *)pData, 
                          dataSize, 
                          TIMEOUT_1_SEC);
						  
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_Usb_CustomEEPROMWrite: usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
   {
    return (ERROR_SUCCESS);
   }

 }
}


/********************************************************************/
//
//  Function Name : AIO_Usb_CustomEEPROMRead
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_CustomEEPROMRead(unsigned long   devIdx,
                           unsigned int    startAddr,
                           unsigned int    dataSize,
                           void          *pData)
{

  struct libusb_device_handle *handle;

  int                 ret;
 
   if ( (startAddr <  AIO_AI_16_MIN_EEPROM_ADDR) ||
        (startAddr > AIO_AI_16_MAX_EEPROM_ADDR    ))
   {
     debug("DBG>>AIO_CustomEEPROM: Invalid StartAddr(%0d) Valid values are 0x000 = 0x01FF\n",startAddr); 
     return (ERROR_INVALID_PARAM); 
    } 

   if ( (dataSize < 0) ||
        (dataSize > AIO_AI_16_MAX_EEPROM_DATSIZE))
   {
     debug("DBG>>AIO_CustomEEPROM: Invalid Datasize (%0d)== NULL  \n",dataSize); 
     return (ERROR_INVALID_PARAM); 
   }

   if (pData == NULL)
   {
     debug("DBG>>AIO_CustomEEPRON: pData == NULL  \n"); 
     return (ERROR_INVALID_PARAM); 
   }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CustomEEPROMRead : invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_CustomEEPROMRead : invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_CustomEEPROM_Read : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }
  else
  {

    // addresses 0x0000 - 0x1E00 contain program code and must not be overwritten
    //  user's see their custom address space as 0x0000-0x1FF
    //  and so we take the customer's start address and add 0x1E00 to it
    // 
    //  note also that because max custom addr is 0x1FF, (startAddr + dataSize) 
    //  must be less than 0x200
    //  

    if ( (startAddr + dataSize) > 0x1FF)
    {
       debug("DBG>>AIO_Usb_CustomEEPROMWrite: EEPROM addr range exceeded. StartAddr=0x%0x size=0x%0x\n",(unsigned int)startAddr,(unsigned int)dataSize);
       debug("      Max allowable address is 0x1FF \n");

       return (ERROR_EEPROM_ADDR_OUT_OF_RANGE);
    }

    ret = libusb_control_transfer(handle,
                          USB_READ_FROM_DEV, 
		                  EEPROM_READ,	
                          (startAddr + 0x1E00), 
                          0,
                          (unsigned char *)pData, 
                          dataSize, 
                          TIMEOUT_1_SEC);
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_Usb_CustomEEPROMRead: usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
   {
    return (ERROR_SUCCESS);
   }

 }
}

/********************************************************************/
//
//  Function Name : AIO_Usb_ClearFIFO
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//
//  History	  :
// 
/********************************************************************/

unsigned long
AIO_Usb_ClearFIFO(unsigned long   devIdx,
                  unsigned long  method)
{

  struct libusb_device_handle *handle;

  int                 ret;
  char               *pData; 


  if (! ( (method == CLEAR_FIFO_IMMEDIATE) || (method == CLEAR_FIFO_AND_ABORT) ) )
  {
     debug("DBG>> AIO_Usb_ClearFIFO Invalid method(0x%x) valid values are 0x35, 0x38 \n",(unsigned int)method);
     return (ERROR_INVALID_PARAM); 
  }
  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_ClearFIFO: invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  ret = validateProductID(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_ClearFIFO: invalid ProductID for device :  0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }


  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_ClearFIFO : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }
  else
  {


    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
		          method,	
                          0, 
                          0,
                          (unsigned char *)pData,  // unused
                          0, 
                          TIMEOUT_1_SEC);
						  
   libusb_close(handle);
   if (ret < 0)
   {
    debug("DBG>> AIO_Usb_ClearFIFO usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
    return (ERROR_USB_CONTROL_MSG_FAILED);
   }
   else
   {
    return (ERROR_SUCCESS);
   }

 }

}

/********************************************************************/
//
//  Function Name : AIO_DIO_StreamOpen
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//      This function can only be used on USB-DIO16-16A model products
//
//  History	  :
// 
/********************************************************************/

unsigned long
AIO_Usb_DIO_StreamOpen(unsigned long  devIdx,
                       unsigned long  streamOp)
{

  int                            idx;
  int                            ret;
  

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_Usb_DIO_StreamOpen invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  if ( (streamOp != STREAM_OP_NONE ) && 
       (streamOp != STREAM_OP_READ ) && 
       (streamOp != STREAM_OP_WRITE)   )
  {
    debug("DBG>>AIO_Usb_DIO_StreamOpen value 'isRead' (%d) invalid; Should be 0,1,or 2 (devIdx=%d)\n",(unsigned int)streamOp,(unsigned int)devIdx); 
    return (ERROR_INVALID_PARAM); 
  }


  idx = getListIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_Usb_DIO_StreamOpen: No Entry in lust for devIdex = %d\n",(unsigned int)devIdx); 
    return (ret);
  }

    aioDevs.aioDevList[idx].streamOp = streamOp;

 return (ERROR_SUCCESS);
}


/********************************************************************/
//
//  Function Name : AIO_DIO_StreamClose
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//      This function can only be used on USB-DIO16-16A model products
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_DIO_StreamClose(unsigned long  devIdx)
{

  int  idx;
  int  ret;

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_Usb_DIO_StreamClose invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  idx = getListIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_Usb_DIO_StreamClose: No Entry in lust for devIdex = %d\n",(unsigned int)devIdx); 
    return (ret);
  }

  aioDevs.aioDevList[idx].streamOp = STREAM_OP_NONE;

  return (ERROR_SUCCESS);
}

/********************************************************************/
//
//  Function Name : AIO_DIO_StreamSetClocks
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//      This function can only be used on USB-DIO16-16A model products
//
//      This function should no be called by an application. The clocks
//      are set in the StreamFrame call. This is done to insure
//      that the clock is not started until any reads have been posted
//      so that incoming data is not lost
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_DIO_StreamSetClocks(unsigned long   devIdx,
                            double         *readClockHz,
                            double         *writeClockHz)
{

  struct libusb_device_handle   *handle;
  
  int                            idx;
  int                            ret;
  unsigned char                  data[5];
  short                          octave;


  if ( (*readClockHz < 0) || (*readClockHz > DIO_16_MAX_HZ) )
  {
    debug("DBG>>StreamSetClocls : Invalid Read Clock Speed (%d) range is 1 - 8,000,000 (devIdx=%d) \n",(unsigned int)*readClockHz,(unsigned int)devIdx);
    return (ERROR_INVALID_PARAM);
  }

  if ( (*writeClockHz < 0) || (*writeClockHz > DIO_16_MAX_HZ) )
  {
    debug("DBG>>StreamSetClocks : Invalid Write Clock Speed (%d) range is 1 - 8,000,000 (devIdx=%d) \n",(unsigned int)*writeClockHz,(unsigned int)devIdx);
    return (ERROR_INVALID_PARAM);
  }

  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_Usb_DIO_StreamSetClocks invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  idx = getListIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    g_fatalSystemError = 1;
    return (ret);
    debug("DBG>>AIO_Usb_DIO_StreamSetClocks: No Entry in lust for devIdex = %d\n",(unsigned int)devIdx); 
  }

  handle = getDevHandle(devIdx);
  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_DIO_StreamSetClocks: could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }


   // fill in data for the vendor request
   // byte 0 used enable/disable read and write clocks (bit_0:write-clk bit_1:readClk)
   // bytes 1-2 = write clock value
   // bytes 3-4 = read clock value
   data[0] = 0; 

   if (*readClockHz != 0) 
     data[0] = data[0] | 0x02;

   if (*writeClockHz != 0)
     data[0] = data[0] | 0x01;
 
   octave =  OctaveDacFromFreq(writeClockHz);
   memcpy((void *)(&data[1]),(void *)&octave,2);

   octave =  OctaveDacFromFreq(readClockHz);
   memcpy((void *)(&data[3]),(void *)&octave,2);

    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
                          DIO_STREAM_SET_CLOCKS,
                          0, 
                          0,
                          data, 
                          5, 
                          TIMEOUT_5_SEC);
						  
	libusb_close(handle);				  
    if (ret < 0)
    {
      debug("DBG>> AIO_Usb_DIO_StreamSetClocks: usb_control_msg failed ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
      ret = libusb_release_interface(handle,0); 
      return (ERROR_USB_CONTROL_MSG_FAILED);
    }

  return (ERROR_SUCCESS);
}




/********************************************************************/
//
//  Function Name : AIO_DIO_StreamFrame
//
//  Description   :
//
//  Returns       :	
//
//  Notes
//      This function can only be used on USB-DIO16-16A model products
//
//      clkHz is for a read clock if the stream was opened for read 
//      clkHz is for a write clock if the stream was opened for write 
//      framePoints must be a multiple of 2 
// 
//      There can only be one stream open on a device at a time
//
//  History	  :
// 
/********************************************************************/
unsigned long
AIO_Usb_DIO_StreamFrame(unsigned long   devIdx,
                        unsigned long   framePoints,
                        unsigned char  *pFrameData,
                        unsigned long  *pBytesTransferred,
                        double         *clkHz)
{

  struct libusb_device_handle   *handle;
  bulkXferRec                   *pBulkXferRec;
  
  int                            listIdx;
  int                            numBytes;
  int                            ret;
  int				 bytesLeft;
  int				 len;
  int				 i;
  unsigned char                 *pData;
  double                         readClk;
  double                         writeClk;
 
  ret = AIO_UsbValidateDeviceIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>AIO_Usb_DIO_StreamFrame invalid DeviceIndex = 0x%0x \n",(unsigned int)devIdx); 
    return (ret);
  }

  listIdx = getListIndex(devIdx);
  if (ret > ERROR_SUCCESS)
  {
    debug("DBG>>StreamFrame : No Entry in lust for devIdex = %d\n",(unsigned int)devIdx); 
    return (ret);
  }

  if (aioDevs.aioDevList[listIdx].streamOp == STREAM_OP_NONE)
  {
    debug("DBG>>StreamFrame : Stream not open for devIdex = %d\n",(unsigned int)devIdx); 
    return (ERROR_STREAM_NOT_OPEN);
  }

 
 if (framePoints < 1)
 {
    debug("DBG>>StreamFrame : framePoints(%d)  must be greater than 1 for devIdex = %d\n",(unsigned int)framePoints,(unsigned int)devIdx); 
    return (ERROR_INVALID_PARAM);
 }


 if (pFrameData == NULL)
 {
    debug("DBG>>StreamFrame : pFrameData is NULL ; devIdex = %d\n",(unsigned int)devIdx); 
    return (ERROR_INVALID_PARAM);
 }

 if (pBytesTransferred == NULL)
 {
    debug("DBG>>StreamFrame : pBytesTransferred is NULL ; devIdex = %d\n",(unsigned int)devIdx); 
    return (ERROR_INVALID_PARAM);
 }

  handle = getDevHandle(devIdx);

  if (handle == NULL)
  {
   debug("DBG>> AIO_Usb_DIO_StreamFrame : could not get device handle devIdx=%d \n",(unsigned int)devIdx);
   return (ERROR_COULD_NOT_GET_DEVHANDLE);
  }

  ret = libusb_claim_interface(handle,0); 
  if (ret < 0)
  {
    debug("DBG>> AIO_Usb_DIO_StreamFrame : usb_claim_interface failed 0x%0x err=%d\n",(unsigned int)devIdx,ret);
   return (ERROR_LIBUSB_CLAIM_INTF_FAILED);
  }

   // a frame point is a 16 bit word. 
   numBytes    = framePoints  * 2;

  if (aioDevs.aioDevList[listIdx].streamOp == STREAM_OP_READ)
  {

    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
                          DIO_STREAM_OPEN_INPUT,
                          0, 
                          0,
                          pData,  //unused
                          0, 
                          TIMEOUT_5_SEC);
						  
	libusb_close(handle);
    if (ret < 0)
    {
      debug("DBG>> AIO_Usb_DIO_StreamFrame : usb_control_msg failed for Stream Input Operation  ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
      ret = libusb_release_interface(handle,0); 
      return (ERROR_USB_CONTROL_MSG_FAILED);
    }

    // issue asynchronous reads 

    aioDevs.aioDevList[listIdx].devHandle                = handle;

    aioDevs.aioDevList[listIdx].sampleBytesRead          = 0;
    aioDevs.aioDevList[listIdx].sampleReadsSubmitted     = 0;
    aioDevs.aioDevList[listIdx].sampleReadsRequired      = (numBytes/BYTES_PER_READ) + 1;
    aioDevs.aioDevList[listIdx].sampleBytesToRead        = numBytes ;
    aioDevs.aioDevList[listIdx].sampleBytesLeftToRead    = numBytes;
    aioDevs.aioDevList[listIdx].pSampleReadBuf           = (unsigned char*)pFrameData;
    aioDevs.aioDevList[listIdx].sampleReadTimeouts       = 0;
    aioDevs.aioDevList[listIdx].sampleReadBulkXferError  = 0;
    aioDevs.aioDevList[listIdx].sampleReadsCompleted     = 0;
    aioDevs.aioDevList[listIdx].samplePrevReadsCompleted = 0;


    // the local bytesLeft count is used here while submitting the reads
    // to know how many bytes to request on each read;
    // the corresponding count set in the aioDev list above
    // is adjusted when the reads complete which happen asynchronously
    // with respect to when the reads are submitted, which is why we
    // need the local one  
    bytesLeft = numBytes ;

   // submit up to 5 reads in the queue 
   // we always want several pending at any given point in time
   // data overruns in the device 
    for (i=0; 
         i < 10;
        i++)
   {

    if (aioDevs.aioDevList[listIdx].sampleReadsSubmitted < 
       aioDevs.aioDevList[listIdx].sampleReadsRequired )
    {
       if (aioDevs.aioDevList[listIdx].sampleBytesLeftToRead >= BYTES_PER_READ)
         len = BYTES_PER_READ;
       else
         len = aioDevs.aioDevList[listIdx].sampleBytesLeftToRead;


        // get a xfer struct 
         pBulkXferRec = getBulkXferRec();
         if (pBulkXferRec == NULL)
         {
           debug("DBG>> DIO_StreamFrame : MAX_PENDING_XFERS EXceeded \n");
           return (ERROR_MAX_XFERS_EXCEEDED);
         }

          pBulkXferRec->userData.devIdx = devIdx;

          // this is the beginning of the caller of this functions buffer
          // data is appended to it upon each transfer completion in the sampleBulkReadCallback
          pBulkXferRec->userData.pBuf   = (unsigned char *)pFrameData;


        libusb_fill_bulk_transfer(pBulkXferRec->pXfer, 
                                  handle, 
                                 (6 | LIBUSB_ENDPOINT_IN),
                                  pBulkXferRec->userData.pXferBuf,
                                  len,
                                  sampleBulkReadCallBack, 
                                  (void *)(pBulkXferRec),
                                  0);

        ret = libusb_submit_transfer(pBulkXferRec->pXfer);
        if (ret < 0 )
        {
          freeBulkXferRec(pBulkXferRec);
          debug("DBG>> DIO_StreamFrame : libusb_submit_transfer failed ; ret = %d\n",ret);
          ret = libusb_release_interface(handle,0); 
          return (ERROR_LIBUSB_SUBMIT_XFER_FAILED);
        }

      aioDevs.aioDevList[listIdx].sampleReadsSubmitted++;

      bytesLeft = bytesLeft - len;
     }
  }

    // start read clock; disable write clock
    writeClk = 0;
    ret = AIO_Usb_DIO_StreamSetClocks(devIdx,
                                      clkHz,
                                      &writeClk);


    while ( (aioDevs.aioDevList[listIdx].sampleReadsCompleted != 
              aioDevs.aioDevList[listIdx].sampleReadsRequired)    && 
            (aioDevs.aioDevList[listIdx].sampleReadTimeouts <
              MAX_BULK_XFER_TIMEOUTS)                             && 
            (g_fatalSystemError != 1)                               )
     {       
       ret = libusb_handle_events(NULL);
       if (ret < 0 )
       {
         debug("DBG>>AIO_Usb_DIO_StreamFramee: FATAL SYSTEM ERROR : libusb_handle_events failed status=%d\n",ret);
         return (ERROR_LIBUSB_HANDLE_EVENTS_FAILED);
       }

      if (aioDevs.aioDevList[listIdx].samplePrevReadsCompleted == aioDevs.aioDevList[listIdx].sampleReadsCompleted)
      {
         aioDevs.aioDevList[listIdx].sampleReadTimeouts++;
      } 

      aioDevs.aioDevList[listIdx].samplePrevReadsCompleted = 
        aioDevs.aioDevList[listIdx].sampleReadsCompleted;

//printf("READS COMPLETED = %d\n",aioDevs.aioDevList[listIdx].sampleReadsCompleted);


     }


     if (aioDevs.aioDevList[listIdx].sampleReadTimeouts >= MAX_BULK_XFER_TIMEOUTS)
     {

        // stop the clocks;
        writeClk = 0;
        readClk  = 0;
        ret = AIO_Usb_DIO_StreamSetClocks(devIdx,
                                          &readClk,
                                          &writeClk);

        ret = libusb_release_interface(handle,0); 

        aioDevs.aioDevList[listIdx].sampleReadInProgress   = 0;

        debug("DBG>>DIO_StreamFrame  : READ FAILURE Exceeded maximum number of bulkXfer read timeouts(5)\n");
        return (ERROR_READ_TIMEOUT);
     }


  }
  else  
  {


    ret = libusb_control_transfer(handle,
                          USB_WRITE_TO_DEV, 
                          DIO_STREAM_OPEN_OUTPUT,
                          0, 
                          0,
                          pData,  //unused
                          0, 
                          TIMEOUT_5_SEC);
    if (ret < 0)
    {
      debug("DBG>> AIO_Usb_DIO_StreamFrame : usb_control_msg failed for Stream Output Operation  ; dev=0x%0x err=%d\n",(unsigned int)devIdx,ret);
      ret = libusb_release_interface(handle,0); 
      return (ERROR_USB_CONTROL_MSG_FAILED);
    }

    // issue synchronous write

    // start write Clock; disable read clock
    readClk = 0;
    ret = AIO_Usb_DIO_StreamSetClocks(devIdx,
                                      &readClk,
                                       clkHz);

    ret = libusb_bulk_transfer(handle,
                               (2 | LIBUSB_ENDPOINT_OUT),
                                (unsigned char *)&pFrameData,
                                numBytes,
                                (int *)pBytesTransferred,
                                TIMEOUT_5_SEC); 
    if ( (ret < 0) || 
         (*pBytesTransferred != numBytes))
    {
      debug("DBG>> AIO_DIO_StreamFrame : usb_bulk_write failed devIdx=%d err=%d\n",(unsigned int)devIdx,ret);
      ret = libusb_release_interface(handle,0); 
      return (ERROR_USB_BULK_WRITE_FAILED);
    }


   }


    // stop the clocks
    writeClk = 0;
    readClk  = 0;
    ret = AIO_Usb_DIO_StreamSetClocks(devIdx,
                                      &readClk,
                                      &writeClk);

  ret = libusb_release_interface(handle,0); 
  if (ret < 0)
  {
    debug("DBG>> AIO_Usb_DIO_StremFrame : usb_release_interface failed 0x%0x \n",(unsigned int)devIdx);
    return (ERROR_USB_CONTROL_MSG_FAILED);
  }
  return (ERROR_SUCCESS);

}



	/********************************************************************/
	//
	//  Function Name : AIO_Init
	//
	//  Description   :
	//
	//  Returns       :	
	//
	//  Notes
	//
	// 
	//  History	  :
	//
	//                  

	int
	AIO_Init()
	{
	  int ret;
    int i;

	   ret = libusb_init(NULL);
	   if (ret < 0)
	   {
	      debug("DBG>> AIO_Init libusb init failed ret=%d\n",ret);
	      return ERROR_LIBUSB_INIT_FAILED;
	   } 


           for (i = 0; i< MAX_PENDING_XFERS; i++)
           {
             bulkXferPool[i].inUse              = 0;
             bulkXferPool[i].pXfer              = NULL;
             bulkXferPool[i].userData.pXferBuf  = NULL;
             bulkXferPool[i].userData.devIdx    = NO_DEVICE;
             bulkXferPool[i].userData.pBuf      = NULL;
             bulkXferPool[i].userData.xferRecId = 0;
           }

	   return ERROR_SUCCESS;

	}

