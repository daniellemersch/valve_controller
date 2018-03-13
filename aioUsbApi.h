#include <stdio.h>
#include <string.h>
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

#ifndef AIOUSBAPI_H
#define AIOUSBAPI_H


// Debugging output macro 
#ifdef DEBUG
#define debug(...)  { (void) fprintf (stdout, __VA_ARGS__); }
#else
#define debug(...)  ((void) 0)
#endif

// USB Vendor Specific Requests

#define DIO_CONFIG				0x12
#define DIO_WRITE				0x10
#define DIO_READ				0x11
#define COUNTER_READ				0x20
#define READ_ALL_COUNTERS			0x25
#define COUNTER_READ_MODE_LOAD 			0x23
#define COUNTER_MODE_LOAD 			0x23
#define COUNTER_MODE				0x21
#define COUNTER_LOAD				0x22
#define SELECT_GATE_SRC				0x24
#define READ_ALL_LATCHED_COUNTS			0x26
#define CLEAR_FIFO_IMMEDIATE                    0x35 
#define CLEAR_FIFO_AND_ABORT                    0x38 
#define DAC_WRITE_SIMPLE			0xB3
#define DAC_WRITE_MULTI_CHAN			0xB3
#define EEPROM_READ				0xA2
#define EEPROM_WRITE				0XA2
#define COUNTER_PURPOSE_OUTPUT_FREQ		0x28	
#define COUNTER_PURPOSE_MEASURE_FREQ		0x2C
#define COUNTER_PURPOSE_EVENT_COUNT		0x2D
#define COUNTER_PURPOSE_MEASURE_PULSE_WIDTH	0x2E
#define READBACK_GLOBAL_STATE			0x30
#define SAVE_GLOBAL_STATE			0x31
#define QUERY_CAL                               0xBA
#define SET_CAL                                 0xBB
#define SET_CONFIG				0xBE 
#define GET_CONFIG				0xD2 
#define GET_IMMEDIATE				0xBF 
#define BULK_ACQUIRE                            0xBC 


#define DIO_STREAM_OPEN_INPUT			0xBC 
#define DIO_STREAM_OPEN_OUTPUT			0xBB 

#define DIO_STREAM_SET_CLOCKS			0xBD 


#define MAX_BULK_XFER_TIMEOUTS			5

#define NUM_AI_16_COUNTERS                  3
#define NUM_AI_16_COUNTER_BLOCKS            3
#define NUM_AI_16_MODES                     6
#define AI_16_MAX_OUTPUT_FREQ          500000 // 500 Khz
#define AI_16_MAX_GAIN_CODE                 7

#define DIO_16_MAX_HZ                  8000000   // 8Mhz 

// maximum number of bytes that can be requested in one 
// call to BulkAcquire().  The number of samples is 1/2 this
// amount since there are 2 bytes per sample
#define MAX_SAMPLE_SIZE                      100000000 




#define AIO_AI_16_CAL_ACQUIRE_NORM             0
#define AIO_AI_16_CAL_ACQUIRE_GROUND           1
#define AIO_AI_16_CAL_ACQUIRE_REF              3


#define AIO_AI_16_MIN_EEPROM_ADDR            0
#define AIO_AI_16_MAX_EEPROM_ADDR           0x1FF 
#define AIO_AI_16_MAX_EEPROM_DATSIZE      0x200

#define AIO_AI_16_MAX_CHAN                  16 

#define TIMEOUT_1_SEC               1000  // ms
#define TIMEOUT_5_SEC               5000  // ms
#define USB_WRITE_TO_DEV            0x40 
#define USB_READ_FROM_DEV           0xC0 

#define USB_BULK_WRITE_EP            2                  
#define USB_BULK_READ_EP             6 
//#define USB_BULK_READ_EP             0x86 

#define ERROR_SUCCESS 		        1000	
#define ERROR_NO_AIO_DEVS_FOUND         1001
#define ERROR_FILE_NOT_FOUND		1002
#define ERROR_DUP_NAME                  1003 
#define ERROR_DEV_NOT_EXIST	        1004	
#define ERROR_DEVICE_REMOVED	        1005	
#define ERROR_INVALID_DEV_IDX           1006
#define ERROR_CANNOT_OPEN_DEVICE        1007
#define ERROR_USB_CONTROL_MSG_FAILED    1008
#define ERROR_INVALID_PARAM             1009
#define ERROR_UNSUPPORTED_DEVICE        1010
#define ERROR_FUNCTION_NOT_SUPPORTED    1010
#define ERROR_INVALID_INPUT_PARAM       1011
#define ERROR_CAL_FILE_NOT_FOUND        1012
#define ERROR_USB_BULK_WRITE_FAILED     1013
#define ERROR_USB_BULK_READ_FAILED      1014
#define ERROR_EEPROM_ADDR_OUT_OF_RANGE  1015
#define ERROR_FW_LOAD_FAILED            1016
#define ERROR_PTHREAD_CREATE_FAILED     1017
#define ERROR_COULD_NOT_GET_DESCRIPTOR  1018
#define ERROR_COULD_NOT_GET_DEVLIST     1019
#define ERROR_COULD_NOT_GET_DEVHANDLE   1020
#define ERROR_BULK_AQUIRE_BUSY          1021
#define ERROR_CALLOC_FAILED             1022
#define ERROR_STREAM_NOT_OPEN           1023
#define ERROR_MAX_XFERS_EXCEEDED        1024
#define ERROR_LIBUSB_INVALID_XFER_REC   1025
#define ERROR_READ_TIMEOUT              1026

#define ERROR_LIBUSB_INIT_FAILED    	    1027
#define ERROR_LIBUSB_FREE_DEV_LIST_FAILED   1028
#define ERROR_LIBUSB_CLAIM_INTF_FAILED      1029
#define ERROR_LIBUSB_SUBMIT_XFER_FAILED     1030
#define ERROR_LIBUSB_FILL_XFER_FAILED       1031
#define ERROR_LIBUSB_ALLOC_XFER_FAILED      1032
#define ERROR_LIBUSB_BULK_TRANSFER_FAILED   1033
#define ERROR_LIBUSB_HANDLE_EVENTS_FAILED   1034

#define USB_AI16_16_ROOT_CLK_SPEED      10000000

#define USB_SOURCE_CLK                  10000000  // 10Mb reference clock
#define AUR_CTR_PUR_OFRQ                0x28 



#define CAL_FILE_BUF_SZ                 1024 // 2-byte words
#define CAL_FILE_SZ                     0xFFFF // bytes 

#define DI_FIRST                       0xFFFFFFFE  // -2 
#define DI_ONLY                        0xFFFFFFFD  // -3

#define ACCES_VENDOR_ID			0x1605

#define USB_DIO_32_ID_DEV		0x8001
/////
#define USB_DIO_96_ID_DEV		0x8003
/////
#define USB_DA12_8A_REVA_DEV		0xC001
#define USB_DA12_8A_DEV			0xC002
#define USB_IIRO_16_DEV 		0x8010
#define USB_CTR_15_DEV			0x8020
#define USB_IIRO4_2SM_DEV		0x8030

#define USB_AIO_AI16_16A_DEV		0x8040

#define USB_DIO_16H			0x800C
#define USB_DI_16A			0x800D
#define USB_DO_16A			0x8000E
#define USB_DIO_16			0x800F

#define MAX_USB_DEVICES 		32


#define BYTES_PER_READ			512	
#define MAX_PENDING_XFERS              50000	// enough for a MAX of 50,000 * 512 = 25,600,000 bytes


#define STREAM_OP_NONE   0
#define STREAM_OP_READ   1
#define STREAM_OP_WRITE  2 
 
#define SET_BIT      (bitmap | bit)
#define BITS_PER_INT (sizeof(int) * 8)
void clear_bit(void *bitmap, size_t bit_no);

void *waitForReads(void *arg);



//This struct is used for the device_array. hdev and pdev are used
//internally by the library and should not be accessed directly by
//user programs.
// product_id, dio_bytes, and ctr_blocks are also used internally
// but reading their values may be useful
// IMPORTANT NOTE :
// Aio_UsbGetDevices MUST be called before anything else in the Api
// is used to populate this

#define NO_DEVICE 1000 // just a number larger than any possible device number  

typedef struct
{
  struct libusb_device_handle *devHandle;
  struct libusb_device        *device;

  unsigned long  devIdx;
  unsigned short productId;


  unsigned long  sampleBytesToRead;  
  unsigned long  sampleBytesRead;
  int 		 sampleReadsSubmitted;
  unsigned long  sampleBytesLeftToRead;
  int            sampleReadInProgress; 
  int 		 sampleReadsRequired;
  int 		 sampleReadsCompleted;
  int 		 samplePrevReadsCompleted;
  int            sampleReadTimeouts;
  int            sampleReadBulkXferError;
  unsigned char *pSampleReadBuf;    

  unsigned long  DIOBytesToRead;
  unsigned long  DIOBytesRead;
  unsigned long  DIOBytesLeftToRead;
  int 		 DIOReadsRequired;
  int 		 DIOReadsSubmitted;
  int 		 DIOReadsCompleted;
  int            DIOReadTimeouts;
  int            DIOReadBulkXferError;
  unsigned char *pDIOReadBuf;    

  unsigned long  DIOBytesToWrite;
  unsigned long  DIOBytesWritten;
  unsigned long  DIOBytesLeftToWrite;
  int 		 DIOWritesRequired;
  int 		 DIOWritesCompleted;
  int            DIOWriteTimeouts;
  int            DIOWriteBulkXferError;
  unsigned char *pDIOWriteBuf;    

  int 		 streamOp;
  unsigned char *pBuf;    

} aioDeviceDescriptor;



typedef struct
{
  int                 numAIODevs;
  aioDeviceDescriptor aioDevList[MAX_USB_DEVICES]; 
} aioDeviceInfo;

void
AIO_UsbListDevices();

void
AIO_Usb_DIO_ClearDevices(void);

int
AIO_Usb_GetDevices(aioDeviceInfo  *pAioDeviceInfo);

void
AIO_Usb_ListDevices();


struct libusb_device_handle *
getDevHandle(int devIdx);

int
AIO_UsbValidateDeviceIndex(int  devIdx); 


unsigned long
AIO_Usb_QueryDeviceInfo(unsigned long        devIndex,
                        unsigned long       *pProdID,
                        unsigned long       *pNameSize,
                        char                *pName,
                        unsigned long       *pDIOBytes,
                        unsigned long       *pCounters);

unsigned long
AIO_Usb_DIO_Configure (unsigned long  devIdx,
                       unsigned int   triState,
                       unsigned char *pOutMask,
                       unsigned char *pData);

unsigned long
AIO_Usb_DIO_ConfigureEx (unsigned long  devIdx,
                         unsigned char *pOutMask,
                         unsigned char *pData,
                         unsigned int   triState);
unsigned long
AIO_Usb_DIOConfigurationQuery(unsigned long  devIdx,
                              void          *pOutMask,
                              void          *pTristateMask);
unsigned long
AIO_Usb_Write1(unsigned long  devIdx,
               int            bitIndex,
               char  setBit);
unsigned long
AIO_Usb_Write8(unsigned long  devIdx,
               unsigned long  byteIndex,
               unsigned char  byte);
unsigned long
AIO_Usb_WriteAll(unsigned long  devIdx,
                 unsigned char *pData);

unsigned long
AIO_Usb_DIO_Read8 (unsigned long  devIdx,
                   int		  byteIdx,
                   unsigned char *pData);
unsigned long
AIO_Usb_DIO_ReadAll (unsigned long   devIdx,
                     unsigned char  *pData);

unsigned long
AIO_Usb_DIO_StreamOpen(unsigned long  devIdx,
                       unsigned long  streamOp);
unsigned long
AIO_Usb_DIO_StreamClose(unsigned long  devIdx);

unsigned long
AIO_Usb_DIO_StreamSetClocks(unsigned long  devIdx,
                            double         *readClockHz,
                            double         *writeClockHz);

unsigned long
AIO_Usb_DIO_StreamFrame(unsigned long   devIdx,
                        unsigned long   framePoints,
                        unsigned char  *pFrameData,
                        unsigned long  *pBytesTransferred,
			double         *clkHz);


unsigned long
AIO_Usb_CTR_8254Mode(unsigned  long devIdx,
                     char      blockIdx,
                     char      counterIdx,
                     char      mode);

unsigned long
AIO_Usb_CTR_8254Load(unsigned       long devIdx,
                     char           blockIdx,
                     char           counterIdx,
                     unsigned int   loadValue);

unsigned long
AIO_Usb_CTR_8254ModeLoad(unsigned       long devIdx,
                         char           blockIdx,
                         char           counterIdx, 
                         char           mode,
                         unsigned int loadValue);

unsigned long
AIO_Usb_CTR_8254ReadModeLoad(unsigned        long devIdx,
                             char            blockIdx,
                             char            counterIdx,
                             char            mode,
                             unsigned int   loadValue,
                             unsigned short *counterVal);

unsigned long
AIO_Usb_CTR_8254Read(unsigned        long devIdx,
                     char            blockIdx,
                     char            counterIdx,
                     unsigned short *counterVal);

unsigned long
AIO_Usb_CTR_ReadAll (unsigned long  devIdx,
                     void          *pData);

unsigned long
AIO_Usb_CTR_8254ReadStatus  (unsigned        long devIdx,
                             char            blockIdx,
                             unsigned short *counterVal,
                             unsigned char  *status);

unsigned long
CTR_StartOutputFreq(unsigned long	   devIdx,
                             char   	   blockIdx,
                             double       *pHz);

unsigned long
AIO_Usb_CTR_8254SelectGate(unsigned long  devIdx,
                           unsigned long  gateIdx);

unsigned long
AIO_Usb_CTR_8254ReadLatched(unsigned long  devIdx,
                            unsigned short *pData); 
unsigned long
AIO_Usb_ADC_GetConfig (unsigned        long devIdx,
                       unsigned char *pCfgBuf,
                       unsigned long *pCfgBufSz);
unsigned long
AIO_Usb_ADC_SetConfig (unsigned        long devIdx,
                       unsigned char *pCfgBuf,
                       unsigned long *pCfgBufSz);

unsigned long
AIO_Usb_ADC_RangeAll (unsigned        long devIdx,
                      unsigned char *pCfgBuf);

unsigned long
AIO_Usb_ADC_Range1 (unsigned        long devIdx,
                    unsigned char   chan,
                    char            gainCode,
                    char            singleEnded);

unsigned long
AIO_Usb_ADC_GetImmediate(unsigned         long devIdx,
                         unsigned short *sample);

unsigned long
AIO_Usb_ADC_ADMode(unsigned         long devIdx,
                   unsigned int     triggerMode,
                   unsigned char    calMode); 

unsigned long 
AIO_Usb_ADC_SetCal(unsigned long devIdx,
                   char          *calFileName);

unsigned long
AIO_Usb_ADC_QueryCal(unsigned long  devIdx,
                     unsigned char *calSupported);



unsigned long
AIO_ADC_Usb_BulkAcquire (unsigned long  devIdx,
                         unsigned char *pBuf,
                         unsigned long  numBytes,
                         double         Hz);

unsigned long
AIO_ADC_Usb_BulkPoll(unsigned long  devIdx);


unsigned long
AIO_Usb_DACDirect(unsigned long  devIdx,
                  unsigned short chan,
                  unsigned short val);

unsigned long
AIO_Usb_DACMultiDirect(unsigned long   devIdx,
                       unsigned short *pDACData, 
                       unsigned long   DACDataCount);


unsigned long
AIO_Usb_DACOutputOpen(unsigned long   devIdx,
                      double         *pClkHz);

unsigned long
AIO_Usb_DACOutputClose(unsigned long   devIdx,
                       unsigned long   bWait);

unsigned long
AIO_Usb_DACOutputSetCount(unsigned long   devIdx,
                          unsigned long   newCount);

unsigned long
AIO_Usb_DACOutputFrame(unsigned long   devIdx,
                       unsigned long   framePoints,
                       unsigned short *pFrameData);
unsigned long
AIO_Usb_CustomEEPROMRead(unsigned long   devIdx,
                         unsigned int  startAddr,
                         unsigned int  dataSize,
                         void          *pData);

unsigned long
AIO_Usb_CustomEEPROMWrite(unsigned long   devIdx,
                          unsigned int  startAddr,
                          unsigned int  dataSize,
                          void          *pData);

unsigned long
AIO_Usb_ClearFIFO(unsigned long   devIdx,
                           unsigned long  method);
unsigned long
AIO_Usb_ADC_Initialize(unsigned long devIdx,
                       unsigned char *pCfgBuf,
                       unsigned long *pCfgBufSz,
                       char          *calFileName);


// Does NOT Currently WORK. DO NOT USE
int
AIO_loadFW(unsigned long devIdx);

int
AIO_Init();

#endif