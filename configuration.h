//
//  configuration.h
//  
//
//  Created by Danielle Mersch on 2/10/15.
//
//


// ============== configuration file format ===============
//  # This is a comment
//  LOGFILE /Users/danielle/path/to/logfile
//  COMPORT /dev/tty_path/to/serial/port
//  MFC addr max_range flow_type
//  MFCLOG /Users/danielle/path/to/mfcdatafile
//  PARTNER Igor || Flytracker
//  DELAY Delay_in_sec
//  FLIES nb_flies
//  TRIGGER internal || external
//  INTERVAL duration_between_pulses_in_ms(default = 0)
//  PULSEWAIT duration_in_seconds_to_wait_after_pulse(default=0)
//  FLYFLOW flowrate_per_fly(SLPM)
//  PULSE vial_code duration_in_ms flowrate [flowrate [flowrate]] [optional_descriptor_of_odour_pulse]
//  WAIT sec
//  WAITSTOP

//# internal command:
//  MFCSET addr flowrate

//#
//  COMPORT needs to be specified before MFCs
//  MFC: addr is address of controller (a letter between [B-Z]) and flowrate is the set point of the flow rate values between [>0 max_range], flow_type is a character specifying the type of airflow that is controlled by MFC: 1=odor1, 2=odor2, 3=odor3, C=carrier, B=Boost
//  PARTNER can be Igor, Flytracker
//  DELAY positiv number which is the delay in seconds before valve controller is started, only possible if no partner is specified
//  If INTERVAL is not specified, then the pulses must be triggered by an external partner. 
//  If TRIGGER is internal (default), then the valve controller triggers each pulse, if trigger is external then, the we need a partner who
//     triggers each pulse. TO IMPLEMENT: INTERVAL is the minimum duration between two subsequent pulses for a given fly 
//  DELAY can be maximum 4294seconds or 4294967ms
//  duration_in_ms can be maximum 4294seconds or 4294967ms
//  WAIT will wait for the specified amount of seconds before moving onto the next instruction (only int accepted)
//  vial_code is a name from the list in READ_ME
//	WAITSTOP means that system stays in its current configuration and the valve-controller programm runs until it is stopped with CTRL+C

//  There are currently three types of events: PULSE events, TOTALFLOW events that change the flow rate and WAIT events
// Internally there are also MFCSET events, but those are inserted by the program to adjust flow rates to match the request of the user, therefore MFCSET commands are inaccessible for the user
// ========================================================



#ifndef ____CONFIGURATION__
#define ____CONFIGURATION__

#include <iostream>
#include <cstdio>
#include <vector>
#include <sstream>
#include <fstream>
#include <ostream>
#include <string>
#include <cstring>
#include <map>
#include <pthread.h> // enable threads
#include <ctype.h>  // contains isdigit funciton

#ifdef BEHAVIOR
#include "vo_alias_behavior.h"
#endif
#ifdef PHYSIOLOGY
#include "vo_alias_physiology.h"
#endif

#include "utils.h"
#include "flow_controller.h"
#include "data_format.h"
#include "MFC_data.h"



struct event{
  std::string etype;  ///< type of event :either flow change event or pulse event
  void* einfo; ///< information about event 
};

/// pulse structure
struct pulse {
  std::string odor_alias;///< odor alias, defines blocks of valves that should be opened for this givien odor. defined in vo_alias.h, listed in alias.txt
  std::vector <int> valve_blocks;  ///< block of valves that need to be opened
  std::map <char, double> MFC_flow; ///< ID is flow type, associated values is flow rate during pulse
  int duration;  ///< duration of pulse 
  std::string name;  ///< user-defined name for pulse, e.g. name of odour
};

struct flowchange{
  char ID; ///< ID of flow controller
  double flow; ///< flow rate for MFC
};

struct double_flowchange{
  char ID_carrier; ///< ID of flow controller carrier
  double flow_carrier; ///< flow rate for MFC carrier
  char ID_boost; ///< ID of flow controller boost
  double flow_boost; ///< flow rate for MFC boost
};

struct instruct{
  bool user; ///< false if it is an internal event, true if it is a user specified event
  std::string etype; ///< type of event :either flow change event or pulse event
  void* einfo;  ///< information about event
};

class Configuration{

public:
  Configuration();
  ~Configuration();
  bool read_config_file(std::string filename);
  
  std::string get_partner();
  unsigned int get_nb_pulses();
  double get_interval();
  unsigned int get_delay();
  bool set_flow(char ID, double flow);
  bool balance_carrier_boost(char ID_carrier, double carrier_flow, char ID_boost, double boost_flow);
  std::string get_comport_name();
  std::string get_trigger();
  //bool get_pulse(unsigned int idx, pulse& p); // replace by get_event
  unsigned int update_pulses_delievered();
  void set_interval_pulse(double interval);
  bool get_interval_pulse(pulse& p);
  double get_pulsewait();
  void log(std::string message);
  void init_MFC_data();
  void log_flow_data(std::ofstream& g);
  void update_flow_destination(const std::string& pulse_type);

  std::string get_mfclog();
  unsigned int get_nb_events();
  bool get_event(unsigned int idx, std::string& e);
  MFC_flows get_MFC_data();
  bool extract_instructions();
  int get_nb_instructions();
  bool get_instruction(unsigned int idx, instruct& command);
  
private:
  
  void update_current_with_pulse_flows(std::map <char, double>& current_flow, std::map <char, double>& MFC_flow_pulse); 
  bool update_flow_rate_for_next_pulse(const event& ev, std::map <char, double>& current_flow);
  bool update_boost_carrier_flow(double boostflow, double carrierflow, std::map <char, double>& current_flow, bool user);
  int find_next_event(unsigned int i);
  void display_instructions(std::ostream& output);
  void add_wait(double delay, bool user);
  void set_pulsewait(double p); /// < duration in seconds
  void convert_pulse_to_flowtypes(const std::string& pulse_type, std::vector <char>& flow_types_valid);

  
  unsigned int pulses_delivered;  ///< nb of pulses delivered
  unsigned int delay;  ///< delay in seconds
  unsigned int nb_pulses; ///< total number of pulses declared
  double interval; /// <duration in us between subsequent pulses
  unsigned int nb_events; ///<number of events in the config file
  std::string comport_name; ///< path of serial port
  int comport_handle;      ///< handle to communication port
  pulse interval_pulse;
  std::vector <event> event_table;
  double pulsewait;  // delay before boos-carrier change in us
  unsigned int flies; ///< nb of flies exposed to airflow

  std::map <char, FlowController> mfc_map;  ///< flow controller map, indexed by ID
  unsigned int nb_mfc; ///< counter for nb of MFCs connected
  pthread_mutex_t mutex_MFC_com; ///< mutex to block serial port for each operation
  std::string config_filename;
  std::string partner;
  std::string trigger;
  std::string logfile;  ///< path of logfile
  std::string mfclogfile;  ///< path of logfile
  
  MFC_flows MFC_data;
  pthread_mutex_t MFC_data_mutex; ///<LUT with flow type and MFC ID, needed to determine for each pulse for which MFC the flow rate needs to be checked
  std::map <char, char> flow_MFC_LUT;  /// LUT contains flow type and associated MFC ID
  double totalflow; // total flowrate delivered to fly/flies
  double max_air_flow; // maximum flow of boost and carrier MFC combined
  bool waitstop_event; 
  std::vector <instruct> instructions;
  
  std::ofstream g; // logfile with config info and instructions
};



#endif // /* defined(____CONFIGURATION__) */
