//
//  valve_controller.cpp
//  connects to device USB-DIO-96 (www.accesio.com), communication based on libusd: http://libusb.sourceforge.net/api-1.0/group__dev.html#details
//
//  uses sockets to communicate with partner programs such as Igor or Flytracker
//  sockets used for sending start signal and data communication (runs in separate thread)
//  with partner Igor : trigger signal received on USB-DIO-96 from ITC18. a polling function (in separate thread) detects the trigger and signals it. 
//                      (Such polling is suboptimal because frequent polling is very CPU intensive, and less frequent polling reduces temporal precision
//                      of the trigger signal. A better way would be to signal the trigger event directly from Igor. However, this is currently impossible
//                      without modifiying the neuromatic code. The hooked function in Neuromatic is only called at the end of each wave whereas for 
//                      transmitting a trigger signal it would need to be called at the start of each wave).
//  with partner Flytracker: the Flytracker sends directly the trigger signal
//
//  controls multi-flow controllers (in separate thread)
//
//  Created by Danielle Mersch on 1/29/15.
//
//


#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <bitset> // to perform bitwise operations

#include <unistd.h>  // usleep
#include <sys/time.h>  //

#include <pthread.h> // enable threads
#include "pthread_event.h" // enables threat events

#include "aioUsbApi.h"  ///< include API of USB-DIO-96 device from www.accesio.com
#include "libusb.h" ///< http://libusb.sourceforge.net/api-1.0/

#ifdef BEHAVIOR
#include "vo_alias_behavior.h"
#endif
#ifdef PHYSIOLOGY
#include "vo_alias_physiology.h"
#endif

#include "netutils.h" // sockets
#include "data_format.h"  // format of data packets for send sockets
#include "configuration.h"  // attributes and methods to use the configuration file
#include "utils.h"  // various utility functions
#include "data_format.h" // format of data packers for send and receive sockets
#include "MFC_data.h"

using namespace std;

const unsigned int BITS_PER_PORT = 8; ///<
const unsigned int NB_PORTS = 8;
const unsigned int NB_CHANNELS = NB_PORTS * BITS_PER_PORT; ///<

const uint16_t TCP_PORT1 = 8124; // port used for connection between Igor and valve controller
const uint16_t TCP_PORT2 = 8125; // port used for connection between Flytracker and valve controller

const int TIMESTAMP_PRECISION = 5;
const int MFC_INTERVAL = 100; // interval in ms between subsequent polling of MFC

const int FAILED_IN_CONFIG = 1;

/// info concerning partner function, used as type in vector, because different partners use different functions with different parameters
struct partner_funct_param{
  void* (*ptr_to_partner_function) (void*);/// pointer to function of partner
  void* ptr_to_partner_param; /// pointer to parameters of function of partner
};


struct poll_param{
  pthread_event* event;
  pthread_mutex_t mutex;  // mutex for USB handle (USB handle is shared with the main thread)
  pthread_mutex_t mutex_data; // mutex for changing triggered and ITC18_timestamp variables
  bool triggered;
  double ITC18_timestamp;
  libusb_device_handle* usbhandle;
  bool stop; // stop used to terminate detached thread when main terminates, without stop the detached thread tries to access data from main which has been destroyed already thereby causing a bus error or segmentation fault
};

struct thread_param{
  pthread_event* event;
  pthread_mutex_t mutex;
  Configuration* ptr_config; 
  bool changed;
  data_packet data;
  double partner_timestamp;
  bool stop; // stop used to terminate detached thread when main terminates, set to true just before main finishes
};

// parameter structure for multiflow controller parameters, 
struct MFC_param{
  pthread_event* event;
  pthread_mutex_t mutex;
  Configuration* ptr_to_config;
  bool stop; // stop used to terminate detached thread when main terminates
};



// =============================================================================
// reads port 11 from DIO96 to check for incoming trigger signal, when received sends signal to valve execution for loop in main
// needs to run in separate thread because polling is very CPU intensive and we don-t want the other thread to hang.  
// every time we access the usbhandle we need to block the mutex to ensure that only one thread used the usbhandle at a time
void* poll_ITC18_trigger(void* ptr_to_param){
  poll_param* param = (poll_param*) ptr_to_param;
  bool triggered(true); // put triggered to true at start to ensure that ITC18 signal will be zero before the first trigger
  double timestamp(0.0);
  // read data port from USB device to check for incoming trigger signal
  while (!param->stop){
    unsigned char pData[12];
    // block mutex so that usbhandle can be used for data reading
    pthread_mutex_lock(&param->mutex);
    // read info from USBDIO96 to see whether trigger received
    if (param->usbhandle == NULL){
      pthread_mutex_unlock(&param->mutex);
      cerr<<"Polling thread terminated because USB handle null."<<endl;
      return NULL;
    }
    
    libusb_control_transfer(param->usbhandle, USB_READ_FROM_DEV, DIO_READ, 0, 0, pData, 12, TIMEOUT_1_SEC);
    pthread_mutex_unlock(&param->mutex);
    /*cout<<"pdata: ";
    for (int i (0);i<12;i++){ 
      cout<<(int)pData[i]<<" ";
    }
    cout<<endl;*/
    
    
    // if trigger signal received, get timestamp add to param (valid input pins on the DIO96 [88 - 95], trigger received if pin values different from 0 
    if (pData[11] != 0){
      //cout<<"ITC18 pulse received "<<endl;
      if (!triggered){
        //get timestamp  
        double timestamp = time_real();
        
        // update param structure
        pthread_mutex_lock(&param->mutex_data);
        param->event->signal(); // SEND TRIGGER SIGNAL TO FOR LOOP IN MAIN
        // update param
        param->ITC18_timestamp = timestamp;
        param->triggered = true; 
        pthread_mutex_unlock(&param->mutex_data);
        triggered = true;
      }
    }else{
      triggered=false;
    }
    usleep(100);
  }
  return NULL;
}


// =============================================================================
bool initialise(int deviceIdx){						//Just sets up the board for our use: all but one byte to be used as output
	unsigned char mask[2];
	unsigned char data[12];		//for DIO_96
	unsigned int triState = 0;
  
	mask[0]=255;	//sets the first 8 port direction bits to 1, i.e. output
  //	mask[1]=0;	//sets the rest of the board for input
	mask[1]=3;	//sets ports 8 and 9 for output, 10 and 11 for input. Port 11 is reserved for the trigger and port 10 can be GPIO
	
	
	
  /// OPEN INTERVAL AIR CHANNELS: BEHAVIOUR data[0]=8,data[1]=8, data[2]=8, data[5]=1, all other 0 
	data[0]=8;	///opens channel for interval air by default, all other channels are closed and data [8-11] are put to 0
	data[1]=8;
	data[2]=8;
	data[3]=0;
	data[4]=0;
	data[5]=1;
	data[6]=0;
	data[7]=0;
	data[8]=0;
	data[9]=0;
	data[10]=0;
	data[11]=0;
	
  
  //writes and configures
	int ret = AIO_Usb_DIO_Configure (deviceIdx, triState, mask, data);
	
	if (ret > ERROR_SUCCESS){
		cout<<endl<<"DIO_Configure Failed dev ="<<deviceIdx<<", err="<<ret<<"."<<endl<<"Have you run the Access Loader?"<<endl;
    //sprintf(temp, "\015DIO_Configure Failed dev=0x%0x err=%d\015Have you run AccesLoader?\015",(unsigned int)myDevIdx,ret);
      false;
	}
  return true;
}


// =============================================================================
bool write_to_USB(struct libusb_device_handle *handle, int deviceIdx, unsigned char *pData){

	if (pData == NULL){
		cerr<<"pData is NULL"<<endl;
		return false;
	}
	if (handle == NULL){
		cout<<" Device handle is NULL."<<endl;
		return false;
	}

  //changed to 12 (DPM: not 14!) to accommodate 96-channel card
  int ret = libusb_control_transfer(handle, USB_WRITE_TO_DEV, DIO_WRITE, 0, 0, pData, 12, TIMEOUT_1_SEC);

  if (ret  < 0 ){
		cerr<<" usb_control_msg failed on WRITE_TO_DEV "<<(unsigned int)deviceIdx << endl;
		return false;
	}else{
		return true;
  }
}


// =============================================================================
// opens all channels specified in channels 
// channel IDs varies from 0 to 63 (64 channels total)
// there are 8 ports, each controlling 8 channels. each bit is a channel, if the bit is 1 the channel is open, if the bit is zero the channel is closed
// returns timestamp in seconds, with ns precision (timestamp is monotonic)
double set_channel(libusb_device_handle* usbhandle, int deviceIdx, vector <int> channels, bool odor){
 
  
  unsigned char data[12];
	data[0]=0;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	data[4]=0;
	data[5]=0;
	data[6]=0;
	data[7]=0;
	
	data[9]=odor; // this output will be 1 for each odor pulse, and 0 whenever carrier air flows, confirms in hardware that we have received the trigger, can be acquired through ITC18 into Igor, or possibly through arduino or serial port into any other program
  
  // determine for each channel, the port to which they belong and determine pin
  vector <vector <int> > port_table;
  port_table.resize(NB_PORTS);
  for (unsigned int i(0); i < channels.size(); i++){
    if (channels[i] < 0 || channels[i] >= NB_CHANNELS ){
      cerr<<"Error, channel "<<channels[i]<<" invalid."<<endl;
      return -1;
    }
    int port =  (channels[i])/BITS_PER_PORT; // determine port
    port_table[port].push_back(channels[i] % BITS_PER_PORT); // modulo determines pin
  }
  
  // generate values for each port based on associated channels
  for (unsigned int p(0); p < NB_PORTS; p++){
    bitset <BITS_PER_PORT> port_value;
    port_value.reset();// default all pins are set to 0
    for (unsigned int c(0); c< port_table[p].size(); c++){
      port_value[port_table[p][c]]=1;  // set pins of channels to 1
    }
    data[p] = static_cast<unsigned char>(port_value.to_ulong());
  }
	
  /*
   // INFO: below code can only set one pin per port to 1
  for (int c(0); c< channels.size(); c++){
    if (channels[c] >= 0 && channels[c] < NB_CHANNELS ) {
      int port = (channels[c])/BITS_PER_PORT;
      //data[port]=pow(2, channel % BITS_PER_PORT); // floating point, does not work on all microcontroller, slow, requires #include <cmath>
      data[port]=(1 << (channels[c] % BITS_PER_PORT)); // shifts bits, works on all microcontrollers, fast
    }else{
      cerr<<"Error, channel "<<channels[c]<<" invalid."<<endl;
      return -1;
    }
  }
	*/
  
	if (!write_to_USB(usbhandle, deviceIdx, data)){
    cerr<<"Failed to set channels:";
    for(unsigned int i(0); i< channels.size(); i++){
      cerr<<" "<<channels[i];
    }
    cerr<<endl;
    return -1;
  }
  
  //double timestamp = time_monotonic();
  double timestamp = time_real();
  
  return timestamp;
}


// =============================================================================
bool test_valves(libusb_device_handle* usbhandle, int deviceIdx){
  
  unsigned int sommeil = 1000; // delay between two channel tests in ms
  
  // NEED TO IMPLEMENT TEST OF ALL AIR PATHS
  
  
  for (unsigned int i(0); i< NB_CHANNELS; i++){
    vector <int> test = make_vector<int>()<<i;
    if (!set_channel(usbhandle, deviceIdx, test, i%2)){
      cerr<<"Failed to set channel: "<<i<<endl;
      return false;
    }
    usleep(1000*sommeil);
  }
  
  return true;
}


// =============================================================================
// create socket to listen for incoming connections
int open_local_listening_port(const uint16_t port){
  //cout<< "Opening  port"<<endl;
  
  // creates socket
  int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // IPv4 TCP socket, TCP to ensure reliable transfer
  // specifies destination address of socket (IP address and port)
  sockaddr_in i_addr;
  memset(&i_addr, 0, sizeof(i_addr)); // initialize all fields to zero to ensure correct functionning on all OS, on MACOS there are additional fields that can cause problems when they remain uninitialized 
  i_addr.sin_family = AF_INET;
  i_addr.sin_port = htons(port); //htons converts bits to correct network byte order
  i_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); //htonl converts IP address in network byte order, INADDR_LOOPBACK = 127.0.0.1 (local host)
  
  int result = bind(s,(sockaddr*)&i_addr, sizeof(i_addr));
  if (result < 0){
    perror("Opening port to listen failed");
    return -1;
  }
  
  if (listen(s,1)<0){
    perror("Unable to listen incoming connections");
    return -1;
  }
  return s;
}

// =============================================================================
/// function that collects flowdata and writes it to the log file
void* collect_flow_data(void* ptr_to_param){
  MFC_param* param = (MFC_param*) ptr_to_param;
  
  // open file for logging flow data
  ofstream g1;
  string logfile = param->ptr_to_config->get_mfclog();
  g1.open(logfile.c_str());
  if(!g1.is_open()){
    cerr<<"Unable to open logfile for flow controller data."<<endl;
    return NULL;
  }
  
  param->event->wait(); ///< wait for start signal to start flow data collection
  // start collecting flow data, stop only when event is signaled
  int ctr(0);
  do {
    param->ptr_to_config->log_flow_data(g1);    
    ctr++;
    usleep(MFC_INTERVAL*1000);
  }while(!param->stop);  // wait for stop signal
  cout<<"send "<<ctr<< "queries."<<endl;
  g1.close();
}

// =============================================================================
void* connect_to_Igor (void* ptr_to_param){
  
  //cout<<"init socket connection with Igor"<<endl;
  
  int s1 = open_local_listening_port(TCP_PORT1);
  if(s1<0){
    return NULL;
  }
  //cout<<"port opened..."<<endl;

  
  int s2 = accept(s1,NULL,NULL);
  if (s2<0){
    perror("Unable to accept incoming connections");
    close(s1);
    return NULL;
  }
  // disable grouping of TCP packages to minimize delays
  disable_nagle(s2);
  
  // receive start delay of connection
  uint32_t start_delay = 0;  // in ms
  if (recv(s2,&start_delay,sizeof(start_delay),0) != sizeof(start_delay)){
    perror("Receive error: no start delay signal received.");
    close(s1);
    close(s2);
    return NULL;
  }
  
  // typecast to thread_param*, because thread functions take void*
  thread_param* param = (thread_param*) ptr_to_param;
  usleep(start_delay*1000);
  param->event->signal();
  //cout<<"sending start signal"<<endl;

  // send informations to partner
  while (!param->stop){
    uint8_t query;  ///< query from partner
    if (recv(s2,&query,sizeof(query),0) != sizeof(query)){  ///< receive query
      perror("Receive error: no query received from Igor.");
      close(s1);
      close(s2);
      return NULL;
    }
    // if it is a data query, send data if something changed, otherwise only send info that nothing changed
    if (query == DATA_QUERY){
      pthread_mutex_lock(&param->mutex);
      bool changed_copy = param-> changed;
      data_packet data_copy = param->data;
      pthread_mutex_unlock(&param->mutex);
      if (send(s2,&changed_copy,sizeof(changed_copy),0)!= sizeof(changed_copy)){
        perror("Send error: Failed to send state.");
        close(s1);
        close(s2);
        return NULL;
      }
      if (changed_copy){
        if (send(s2,&data_copy,sizeof(data_copy),0)!= sizeof(data_copy)){
          perror("Send error: Failed to send data.");
          close(s1);
          close(s2);
          return NULL;
        }
        // Update status of changed to false
        pthread_mutex_lock(&param->mutex);
        param-> changed = false;
        pthread_mutex_unlock(&param->mutex);
      }
    }else if (query == PULSE_QUERY){
	    // currently igor does not send pulse queries directly to the valve controller because these queries would arrive at end of wave (neuromatic constraint), whereas they need to arrive at start of wave
      // instead Igor makes a list of pulse queries that are transferred to the ITC18 and the valve controller polls one of its input ports to find out whether there is apulse query
      // update info for instruction execution to function correctly
      
      // confirm reception of trigger signal 
      bool pulse_trigger_received = true;
      if (send(s2,&pulse_trigger_received,sizeof(pulse_trigger_received),0)!= sizeof(pulse_trigger_received)){
        perror("Send error: Failed to send confirmation of trigger reception.");
        close(s1);
        close(s2);
        return NULL;
      }
	    if (recv(s2,&param->partner_timestamp,sizeof(param->partner_timestamp),0) != sizeof(param->partner_timestamp)){  // receive IGOR timestamp
        perror("Receive error: no timestamp received from Igor.");
        close(s1);
        close(s2);
        return NULL;
	    }
      // no need to send trigger signal because Igor sends trigger signal through ITC and valve controller receives it from polling function
	    //param->event->signal();
    }else{
      cerr<<"Invalid query received. Closing connection."<<endl;
      close(s1);
      close(s2);
      return NULL;
    }
    
  }
  
  return NULL;
}

// =============================================================================
// compare old and current MFC flow data, return true if they differ
bool MFC_data_compare(const MFC_flows& copy_MFC_data, const MFC_flows& current_MFC_data){
  //cout<<"comparing MFC data"<<endl;
  bool different(false);
  int i(0);
  do{
    if((copy_MFC_data.values[i] != current_MFC_data.values[i]) || (copy_MFC_data.validity[i] != current_MFC_data.validity[i]) || (copy_MFC_data.names[i] != current_MFC_data.names[i])){
      different = true;
    }
    i++;
  }while(i<MAX_MFC && !different && current_MFC_data.names[i]!=0);
  return different;
}


// =============================================================================
void* connect_to_Flytracker (void* ptr_to_param){
  
  int s1 = open_local_listening_port(TCP_PORT2);
  if(s1<0){
    return NULL;
  }
  
  int s2 = accept(s1,NULL,NULL);
  if (s2<0){
    perror("Unable to accept incoming connections");
    close(s1);
    return NULL;
  }
  // disable grouping of TCP packages to minimize delays
  disable_nagle(s2);
  
  // receive start delay of connection
  uint32_t start_delay = 0;
  if (recv(s2,&start_delay,sizeof(start_delay),0) != sizeof(start_delay)){
    perror("Receive error");
    close(s1);
    close(s2);
    return NULL;
  }
  
  // typecast to thread_param*, because thread functions take void*
  thread_param* param = (thread_param*) ptr_to_param;
  
  usleep(start_delay*1000);
  param->event->signal();
  
  MFC_flows copy_MFC_data; // old flow data from MFC (data comes from olling function collect_flow_data)
  
  // send informations to partner
  while (!param->stop){
    uint8_t query;  ///< query from partner
    if (recv(s2,&query,sizeof(query),0) != sizeof(query)){  ///< receive query
      perror("Receive error: no query received from Flytracker.");
      close(s1);
      close(s2);
      return NULL;
    }
    // if it is a data query, send data if something changed, otherwise only send info that nothing changed
    if (query == DATA_QUERY){ // request for info on current odor pulse
      //cout<<"received data query"<<endl;
      pthread_mutex_lock(&param->mutex);
      bool changed_copy = param-> changed;
      data_packet data_copy = param->data;
      pthread_mutex_unlock(&param->mutex);
      if (send(s2,&changed_copy,sizeof(changed_copy),0)!= sizeof(changed_copy)){
        perror("Send error: Failed to send state.");
        close(s1);
        close(s2);
        return NULL;
      }
      if (changed_copy){
        if (send(s2,&data_copy,sizeof(data_copy),0)!= sizeof(data_copy)){
          perror("Send error: Failed to send data.");
          close(s1);
          close(s2);
          return NULL;
        }
        // Update status of changed to false
        pthread_mutex_lock(&param->mutex);
        param-> changed = false;
        pthread_mutex_unlock(&param->mutex);
        //cout<<"Pulse data send"<<endl;
      }/*else{
        cout<<"Valve controller has no new pulse data."<<endl;
      }*/

    }else if (query == PULSE_QUERY){ // triggers delivery of pulse
      // need to receive the ID of fly that should receive pulse, and update info for instruction execution to function correctly
      // TO BE IMPLEMENTED

      //cout<<"received pulse query"<<endl;
      // confirm reception of trigger signal 
      bool pulse_trigger_received = true;
      if (send(s2,&pulse_trigger_received,sizeof(pulse_trigger_received),0)!= sizeof(pulse_trigger_received)){
        perror("Send error: Failed to send confirmation of trigger reception.");
        close(s1);
        close(s2);
        return NULL;
      }
      if (recv(s2,&param->partner_timestamp,sizeof(param->partner_timestamp),0) != sizeof(param->partner_timestamp)){  // receive IGOR timestamp
        perror("Receive error: no timestamp received from Flytracker.");
        close(s1);
        close(s2);
        return NULL;
      }
      // send trigger signal 
      param->event->signal();

    }else if (query == FLOW_QUERY){
      //cout<<"received flow query"<<endl;
      // find out if MFC_data changed -> compare current data to copy
      MFC_flows current_MFC_data = param->ptr_config->get_MFC_data();
      //cout<<"current flow: "<<current_MFC_data.values[0]<<endl;
      bool MFC_changed = MFC_data_compare(copy_MFC_data, current_MFC_data); // true if data differs
      if (send(s2,&MFC_changed,sizeof(MFC_changed),0)!= sizeof(MFC_changed)){
        perror("Send error: Failed to send confirmation of flow query.");
        close(s1);
        close(s2);
        return NULL;
      }
      if(MFC_changed){ // send current flow data
        //cout<<"sending new MFC data"<<endl;
        if (send(s2,&current_MFC_data,sizeof(current_MFC_data),0)!= sizeof(current_MFC_data)){
          perror("Send error: Failed to send MFC data.");
          close(s1);
          close(s2);
          return NULL;
        }
        // update copy with current MFC data
        memcpy(&copy_MFC_data,&current_MFC_data, sizeof(copy_MFC_data));
      }/*else{
        cout<<"MFC unchanged"<<endl;
      }*/
      
    }else{
      cerr<<"Invalid query received. Closing connection."<<endl;
      close(s1);
      close(s2);
      return NULL;
    }
    
  }


  return NULL;
}

// =============================================================================
// extract parameters from event
// PULSE vial_code duration_in_ms [optional_description_of_the_odour_in_vials]
// MFCSET addr flowrate
// !!! changes event string!!!
bool parse_event(string& event, string& event_type, vector <string>& event_parameters){
  string delimitor = " ";  
  size_t pos = 0;
  pos = event.find(delimitor);
  event_type = event.substr(0,pos);
  event.erase(0, pos + delimitor.length());
  while((pos = event.find(delimitor)) != string::npos){
     event_parameters.push_back(event.substr(0,pos));
     event.erase(0, pos + delimitor.length());
  }
  event_parameters.push_back(event.substr(0,pos));
}


// =============================================================================
bool execute_config_instructions(Configuration& config, const int& deviceIdx, libusb_device_handle*& usbhandle, 
  vector <partner_funct_param>& partner_function_table, const int& polling_function_idx, pthread_event& start_event, pthread_event& trigger_event, 
  pthread_event& mfc_event, MFC_param& mfc_param, thread_param& param){

  // at start only carrier air + boost go to fly
  config.init_MFC_data();
  config.update_flow_destination("Carrier");

  /*pulse test_i_pulse;
  config.get_interval_pulse(test_i_pulse);
  cout<<"Interval duration: "<<test_i_pulse.duration<<endl;
  */
 
  // execute all instructions before first pulse
  instruct command;
  int idx_instruct (0);
  bool move_on = false;
  do {
    if (!config.get_instruction(idx_instruct, command)){
      cerr<<"Error: unable to retrieve command from instruction table."<<endl;
      return false;
    }
    idx_instruct++;
    //cout<<"at start command.etype: "<<command.etype<<endl;

    if (command.etype != "PULSE"){
      if (command.etype == "WAIT"){
        double delay = *((double*)command.einfo);
        usleep(delay*1000000);// wait specified in s, but here needed in us
      }else if (command.etype == "MFCSET"){
        flowchange tmp;
        tmp.ID = ((flowchange*)command.einfo)->ID;
        tmp.flow = ((flowchange*)command.einfo)->flow;
        cout<<"flow of "<< tmp.ID<<" now: "<<tmp.flow<<endl;
        
        // block MFC mutex, set flow, unblock mutex,
        pthread_mutex_lock(&mfc_param.mutex);
        config.set_flow(tmp.ID, tmp.flow);
        pthread_mutex_unlock(&mfc_param.mutex);
        // write event to log TO BE IMPLEMENTED
        double timestamp_MFCSET = time_real();
      }else if (command.etype == "MFCSET2"){
        double_flowchange tmp;
        tmp.ID_carrier = ((double_flowchange*)command.einfo)->ID_carrier;
        tmp.flow_carrier = ((double_flowchange*)command.einfo)->flow_carrier;
        //cout<<"flow of "<< tmp.ID<<" now: "<<tmp.flow<<endl;
        tmp.ID_boost = ((double_flowchange*)command.einfo)->ID_boost;
        tmp.flow_boost = ((double_flowchange*)command.einfo)->flow_boost;
                
        // block MFC mutex, set flow, unblock mutex,
        pthread_mutex_lock(&mfc_param.mutex);
        config.balance_carrier_boost(tmp.ID_carrier, tmp.flow_carrier, tmp.ID_boost, tmp.flow_boost);
        pthread_mutex_unlock(&mfc_param.mutex);
        // write event to log TO BE IMPLEMENTED
        double timestamp_MFCSET = time_real();
      }else if (command.etype == "WAITSTOP"){
        cout<<"Waiting for early manual stop..."<<endl;
        while(1){
          sleep(1);
        }
      }
    }else{
      move_on = true;
    }
  }while(idx_instruct < config.get_nb_instructions() && !move_on);



  
  // get number of pulses (should all be before waitstop to be executed)
  //cout<<"executing pulses"<<endl;
  int nb_pulses = config.get_nb_pulses();
  int idx_pulse (0);
  while(idx_pulse < nb_pulses){
    
    //cout<<"giving pulse: "<<idx_pulse<<"out of "<<nb_pulses<<endl;
    // get information of next pulse
    pulse next_pulse;
    if ( command.etype == "PULSE"){ 

      string valve_alias = ((pulse*)command.einfo)->odor_alias;
      vector <int> valve_blocks = valve_alias::parse_alias(valve_alias);
      if (valve_blocks.empty()){
        cerr<<"Error: no valve blocks specified."<<endl;
        return false;
      }
      next_pulse.valve_blocks = valve_blocks;
      next_pulse.duration = ((pulse*)command.einfo)-> duration;
      next_pulse.name = ((pulse*)command.einfo)->name;
      next_pulse.odor_alias = ((pulse*)command.einfo)->odor_alias;
      next_pulse.MFC_flow =  ((pulse*)command.einfo)->MFC_flow;
    }
        
    // wait for trigger if specified
    double timestamp_check(0.0);
    if (config.get_trigger()== "external"){
      if(config.get_partner() == "Igor"){
        // wait for event from polling thread
        //cout<<" waiting for trigger"<<endl;
        trigger_event.wait();
        //((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->event->wait();
      }else if(config.get_partner() == "Flytracker"){
        start_event.wait();
      }
      timestamp_check = time_real();
      //cout<<"trigger received"<<endl;
      // TO IMPLEMENT FOR FLYTRACKER: find out which fly gets pulse, adjust valve_blocks accordingly
    }
    
    // give pulse
    double timestamp_start (0.0);
    if (config.get_partner() == "Igor"){
      // need to lock mutex to use usb handle, otherwise risk of conflict
      pthread_mutex_lock(&((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->mutex);
      timestamp_start = set_channel(((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->usbhandle, deviceIdx, next_pulse.valve_blocks, 1);
      pthread_mutex_unlock(&((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->mutex);
    }else{
      timestamp_start = set_channel(usbhandle, deviceIdx, next_pulse.valve_blocks, 1);
    }
    idx_pulse++;

    // update which flows go to fly and waste depending on pulse
    config.update_flow_destination(next_pulse.odor_alias);
    
    
    cout<<"Pulse: "<<next_pulse.name<<endl;
    if (timestamp_start == 0){
      // setting channels failed
      cerr<<"Error: Setting channel failed."<<endl;
      return false;
    }else{
      // collect info of pulse and update param structure
      bool just_changed = true;
      data_packet new_data;
      new_data.event_type = 1; // pulse start event
      new_data.duration = next_pulse.duration; // pulse duration in ms
      new_data.timestamp = timestamp_start; // timestamp in us
      strncpy(new_data.alias, next_pulse.odor_alias.c_str(), sizeof(new_data.alias));
      strncpy(new_data.odor, next_pulse.name.c_str(), sizeof(new_data.odor));
      new_data.odor[sizeof(new_data.odor) - 1] = 0; // make sure array terminates with 0
      new_data.alias[sizeof(new_data.alias) - 1] = 0; // make sure array terminates with 0
      //cout << "new_data alias = " << new_data.alias << ", odor = " << new_data.odor << endl;
      pthread_mutex_lock(&param.mutex);
      param.changed = just_changed;
      param.data = new_data;
      pthread_mutex_unlock(&param.mutex);
    }
    
    // get trigger time of ITC18
    bool ITC_trigger (false);
    double ITC_time(0.0);
    if (config.get_partner() == "Igor"){
      pthread_mutex_lock(&((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->mutex_data);
      ITC_trigger = ((poll_param*)partner_function_table[polling_function_idx].ptr_to_partner_param)->triggered;
      ITC_time =  ((poll_param*)partner_function_table[polling_function_idx].ptr_to_partner_param)->ITC18_timestamp;
      pthread_mutex_unlock(&((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->mutex_data);
    }
    
    string message;
    if (ITC_trigger){
      // message for logfile: timestamp_start timestamp_partner timestamp_ITC odor_alias pulse_duration pulse name 
      message = to_stringHP(timestamp_start, TIMESTAMP_PRECISION) + " " + to_stringHP(-1.0, 1) + " " + to_stringHP(ITC_time, TIMESTAMP_PRECISION) + " " + next_pulse.odor_alias + " " + to_string(next_pulse.duration) + " " + next_pulse.name;
    }else{
      message = to_stringHP(timestamp_start, TIMESTAMP_PRECISION) + " " + to_stringHP(param.partner_timestamp, TIMESTAMP_PRECISION) + " " + to_stringHP(-1.0, 1) + " " + next_pulse.odor_alias + " " + to_string(next_pulse.duration) + " " + next_pulse.name;
    }
    config.log(message);
    double delay = timestamp_start - ITC_time;
    //cout<<"ITC time: "<<fixed<<ITC_time<<" Channels set: "<<fixed<<timestamp_start<<" Delay: "<<fixed<<delay<<endl;
    usleep(next_pulse.duration*1000); // sleep for duration of pulse, pulse specified in ms seconds, here needed in us
    
    // switch to interval air
    pulse i_pulse;
    if (!config.get_interval_pulse(i_pulse)){
      cerr<<"Error during pulse request. Failure when switchting to interval pulse. "<<endl;
      return false;
    }
    double timestamp_end (0.0);
    if (config.get_partner() == "Igor"){
      pthread_mutex_lock(&((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->mutex);
      
      // ONLY for testing the blender setup> need to remove i_pulse afterwards because it overwrites the one from the config file
      /*pulse i_pulse;
       i_pulse.odor_alias = "Carrier"  ;///< odor alias, defines blocks of valves that should be opened for this givien odor. defined in vo_alias.h, listed in alias.txt
       i_pulse.valve_blocks = make_vector<int>() << 0 << 8 << 21 ;  ///< block of valves that need to be opened
       i_pulse.duration = 1;  ///< duration of pulse
       i_pulse.name = "carrier air";  ///< user-defined name for pulse, e.g. name of odour
       // end carrier air interval pulse testing
       */
      timestamp_end = set_channel(((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->usbhandle, deviceIdx, i_pulse.valve_blocks, 0);
      pthread_mutex_unlock(&((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->mutex);
    }else{
      timestamp_end = set_channel(usbhandle, deviceIdx, i_pulse.valve_blocks, 0);
    }
    
    config.update_flow_destination(i_pulse.odor_alias);
    
    if (timestamp_end == 0){
      // setting channels failed
      cerr<<"Setting channel failed."<<endl;
      return false;
      
    }else{
      // collect info of interval pulse and update param structure
      bool just_changed = true;
      data_packet new_data;
      new_data.event_type = 2; // pulse end event
      new_data.duration = 0;
      new_data.timestamp = timestamp_end; // timestamp in us
      strncpy(new_data.alias, i_pulse.odor_alias.c_str(), sizeof(new_data.alias));
      strncpy(new_data.odor, i_pulse.name.c_str(), sizeof(new_data.odor));
      new_data.odor[sizeof(new_data.odor) - 1] = 0; // make sure array terminates with 0
      new_data.alias[sizeof(new_data.alias) - 1] = 0; // make sure array terminates with 0
      //cout << "new_data alias = " << new_data.alias << ", odor = " << new_data.odor << endl;
      pthread_mutex_lock(&param.mutex);
      param.changed = just_changed;
      param.data = new_data;
      pthread_mutex_unlock(&param.mutex);
    }
    // message for logfile
    message = to_stringHP(timestamp_end, TIMESTAMP_PRECISION) + " -1 -1 Interval " + to_string( (i_pulse.duration + config.get_pulsewait() )*1000) + " " + i_pulse.name;
    config.log(message);
    
    
    // get next instruction in table, and update instruction counter
    if (!config.get_instruction(idx_instruct, command)){
      cerr<<"Error: unable to retrieve command from instruction table."<<endl;
      return false;
    }
    idx_instruct++;
    //cout<<"next command.etype: "<<command.etype<<endl;

      
    while (command.etype != "PULSE" && idx_instruct <= config.get_nb_instructions()){
     
      if (command.etype == "WAIT"){
        double delay = *((double*)command.einfo);
        usleep(delay*1000000);// pulse entered in s, but here needed in microseconds
        //cout<<"real command.etype: "<<command.etype<<endl;

      }else if (command.etype == "MFCSET"){
        flowchange tmp;
        tmp.ID = ((flowchange*)command.einfo)->ID;
        tmp.flow = ((flowchange*)command.einfo)->flow;
        cout<<"flow of "<< tmp.ID<<" now: "<<tmp.flow<<endl;
        
        // block MFC mutex, set flow, unblock mutex,
        pthread_mutex_lock(&mfc_param.mutex);
        config.set_flow(tmp.ID, tmp.flow);
        pthread_mutex_unlock(&mfc_param.mutex);
        // write event to log TO BE IMPLEMENTED
        double timestamp_MFCSET = time_real();
      }else if (command.etype == "MFCSET2"){
        double_flowchange tmp;
        tmp.ID_carrier = ((double_flowchange*)command.einfo)->ID_carrier;
        tmp.flow_carrier = ((double_flowchange*)command.einfo)->flow_carrier;
        //cout<<"flow of "<< tmp.ID<<" now: "<<tmp.flow<<endl;
        tmp.ID_boost = ((double_flowchange*)command.einfo)->ID_boost;
        tmp.flow_boost = ((double_flowchange*)command.einfo)->flow_boost;
        
        // block MFC mutex, set flow, unblock mutex,
        pthread_mutex_lock(&mfc_param.mutex);
        config.balance_carrier_boost(tmp.ID_carrier, tmp.flow_carrier, tmp.ID_boost, tmp.flow_boost);
        pthread_mutex_unlock(&mfc_param.mutex);
        // write event to log TO BE IMPLEMENTED
        double timestamp_MFCSET = time_real();
      }else if (command.etype == "WAITSTOP"){
      	cout<<"Waiting for manual stop..."<<endl;
        while(1){
          sleep(1);
        }
        
      }
    
      if (!config.get_instruction(idx_instruct, command)){
        cerr<<"Error: unable to retrieve command from instruction table."<<endl;
        return false;
      }
      idx_instruct++;
      //cout<<"next command.etype2: "<<command.etype<<endl;

    }

  }
  
  return true;
}


// =============================================================================
//            MAIN
// =============================================================================

/// TO IMPLEMENT: USE EXCEPTIONS FOR ERROR HANDLING


int main(int argc, char* argv[]){
  
  int return_value (0);

  if (argc < 2) {
    cerr << "Missing argument. You need to specify the configuration file." << endl;
    return 1;
  }
  string ConfigFile = string(argv[1]);

  //check config file
  Configuration config;
  if (!config.read_config_file(ConfigFile)){
    return 1;
  }

  if (!set_realtime()) {
    cerr << "Could not set realtime priority, are you root? ;)" << endl;
  }
	
  // connect to device USB-DIO-96 from www.accesio.com
  AIO_Init();		// This should be called BEFORE any other function in the API
	// to populate the list of Acces devices, comes from aioUsbApi.h
  aioDeviceInfo aioDevices;
  int ret (0); // collects return value from function calls
  ret = AIO_Usb_GetDevices(&aioDevices);
  if (ret > ERROR_SUCCESS){
    cerr<<"Could not connect to USB-DIO-96 device."<<endl;
    return (ret);
  }	
  int deviceIdx = aioDevices.aioDevList[0].devIdx;		// use the first device found
  ret = AIO_UsbValidateDeviceIndex(deviceIdx);
  if (ret > ERROR_SUCCESS){
    cerr<<"Invalid device Index = "<< deviceIdx<<endl;
    return (ret);
  }
  
  // get USB handle for device
  libusb_device_handle* usbhandle=NULL;
  usbhandle=getDevHandle(deviceIdx);
  if (usbhandle == NULL){
    cerr<<"Could not get device handle device Idx="<< deviceIdx<<endl;
    return 1;
  }

 
  // thread for multiflowcontroller
  pthread_t mfcThread;
  pthread_event mfc_event;
  MFC_param mfc_param;
  mfc_param.event = &mfc_event;
  mfc_param.stop = false;
  mfc_param.ptr_to_config = &config;
  pthread_mutex_init(&mfc_param.mutex, NULL);
  mfc_param.event = &mfc_event;
  pthread_create(&mfcThread,NULL,collect_flow_data,&mfc_param);
  //pthread_detach(mfcThread);

  
  // initialize valve states
  // Initialise the Acces DIO board so that pins have a default direction and state (so that air starts to flow through ODD)
	if (!initialise(deviceIdx)){
    cerr<<"Initialization failed."<<endl;
    libusb_close(usbhandle);
    usbhandle=NULL;
    return 1;
  }
  
  // run a valve test
  /*if (!test_valves(usbhandle, deviceIdx)){
    cerr<<"Problem during valve testing."<<endl;
    libusb_close(usbhandle);
    usbhandle=NULL;
    return 1;
  }*/
  
  // table contains all functions and parameters needed to interact with partner
  vector <partner_funct_param> partner_function_table;
  // store indices of funtions, could be hard-coded, but indices provide flexibilty for future extensions
  int socket_function_idx = -1; /// keep idx of socket thread, then no need to run through table of functions
  int polling_function_idx = -1;
  
  // parameters for socket connection with Igor or Flytracker. if called, runs in separate thread, but only called when there is a partner 
  pthread_event start_event;
  thread_param param;
  param.changed = false;
  param.stop = false;
  pthread_mutex_init(&param.mutex, NULL);
  param.event = &start_event;
  param.ptr_config = &config;
  param.partner_timestamp = -1.0;

  // event for trigger signal from polling_ITC18_trigger function
  // only used with Igor, but needs to be specified
  pthread_event trigger_event;
  poll_param polling_param;

  //fill in partner_function_table depending on partner
  if (config.get_partner() == "Igor"){
    
    // function for socket thread
    partner_funct_param igor;
    igor.ptr_to_partner_function = connect_to_Igor;
    igor.ptr_to_partner_param = &param;
    partner_function_table.push_back(igor);
    
    // thread for poll_ITC18_trigger function, only used with Igor
    polling_param.ITC18_timestamp = 0.0;
    polling_param.stop = false;
    pthread_mutex_init(&polling_param.mutex, NULL);
    pthread_mutex_init(&polling_param.mutex_data, NULL);
    polling_param.usbhandle = usbhandle;
    polling_param.event = &trigger_event;
    igor.ptr_to_partner_function = poll_ITC18_trigger;
    igor.ptr_to_partner_param = &polling_param;
    partner_function_table.push_back(igor);

    
    cout<<"Valve controller partnered with Igor."<<endl;
   
  }else if(config.get_partner() == "Flytracker"){
    
    // function for socket thread
    partner_funct_param flytracker;
    flytracker.ptr_to_partner_function = connect_to_Flytracker;
    flytracker.ptr_to_partner_param = &param;
    partner_function_table.push_back(flytracker);
   
    cout<<"Valve controller partnered with Flytracker."<<endl;
    
  }else{
    
    //ptr_to_partner_function_table = NULL; // no partner
    cout<<"No partner."<<endl;
  
  }
  
  
  // start socket thread if partner is Igor or Flytracker
  for (int i(0); i < partner_function_table.size(); i++){
    if ((config.get_partner() == "Igor" && partner_function_table[i].ptr_to_partner_function == connect_to_Igor) || (config.get_partner() == "Flytracker" && partner_function_table[i].ptr_to_partner_function == connect_to_Flytracker)){
      
      pthread_t socketThread; // thread identifier for connection to valve controller
      pthread_create(&socketThread,NULL,partner_function_table[i].ptr_to_partner_function,partner_function_table[i].ptr_to_partner_param);
      pthread_detach(socketThread);
      socket_function_idx = i;
    }
    if(config.get_partner() == "Igor" && partner_function_table[i].ptr_to_partner_function == poll_ITC18_trigger){
      // start ITC18 polling thread
      pthread_t pollThread;
      pthread_create(&pollThread,NULL,partner_function_table[i].ptr_to_partner_function,partner_function_table[i].ptr_to_partner_param);
      pthread_detach(pollThread);
      polling_function_idx = i;
    }
  }
  
  // execute instructions of config file either immediatly if no partner, or, if there is a partner when start signal received from partner
  if(socket_function_idx != -1 && partner_function_table[socket_function_idx].ptr_to_partner_function!= NULL){
    // wait for start signal from partner
    cout << "Waiting for signal from "<< config.get_partner() << endl;
    start_event.wait();
    //((thread_param*)(partner_function_table[socket_function_idx].ptr_to_partner_param))-> event->wait();
    cout << "Signal received: valve controller started." << endl;
  }else{
    // no partner situation: wait delay specified in config file
    cout<<"The valve controller will be started in "<< config.get_delay()<<" seconds."<<endl;
    sleep(config.get_delay()); // indicated in config file
  }
  
  // trigger start of collection of mass flow data
  mfc_param.event->signal();

  // read instructions from config file
  cout<<"starting reading events from config file..."<<endl;
  if (!execute_config_instructions(config, deviceIdx, usbhandle, partner_function_table, polling_function_idx, start_event, trigger_event, mfc_event, mfc_param, param)){
    return_value = FAILED_IN_CONFIG;
  }
    
  
  // trigger stop of collection of mass flow data, closes file automatically
  pthread_mutex_lock(&mfc_param.mutex);
  mfc_param.stop = true;
  pthread_mutex_unlock(&mfc_param.mutex);
  
 
  // close USB connection
  if (config.get_partner() == "Igor"){
    pthread_mutex_lock(&((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->mutex);
    ((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->stop = true;
    libusb_close(usbhandle);
    usbhandle=NULL;
    pthread_mutex_unlock(&((poll_param*)(partner_function_table[polling_function_idx].ptr_to_partner_param))->mutex);
  }else{
    libusb_close(usbhandle);
    usbhandle=NULL;
  }
  
  // set stop of socket function to true to signal termination of program to the socket thread
  pthread_mutex_lock(&((thread_param*)(partner_function_table[socket_function_idx].ptr_to_partner_param))->mutex);
  ((thread_param*)(partner_function_table[socket_function_idx].ptr_to_partner_param))->stop = true;
  pthread_mutex_unlock(&((thread_param*)(partner_function_table[socket_function_idx].ptr_to_partner_param))->mutex);
  
  //usleep(100);
  pthread_join(mfcThread, NULL);
    
  return return_value;
}

