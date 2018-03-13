/// 2015-08-21 added: vector MFC_ID containing all accepted IDs for MFCs, function is_valid_ID that checks whether a given ID is valid
///                   vector MFC_RANGE containing all accepted max ranges for MFCs, function is_valid_range that check the validity of a givne range max. 
///                   function is_valid_flow that checked validity of flow

#ifndef _FLOW_CONTROLLER_
#define _FLOW_CONTROLLER_


#include <stdint.h>
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

#include "utils.h"
#include "rs232.h"


struct flow_data{
  char ID;
  double pressure;
  double temperature;
  double volumetric_flow;
  double mass_flow;
  double setpoint;
  std::string gas;
};


class FlowController{

public:
  
  FlowController();
  ~FlowController();
  
  void init(int p, char a, unsigned int r, char t);
  
  void set_flow(double flow);

  static void balance_carrier_boost(FlowController& carrier, double carrier_flow, FlowController& boost, double boost_flow);


  void get_flow_data(flow_data& flow);
  
  void set_port(int p);
  
  void change_addr(char addr);
  
  void receive(int port, std::stringstream& s);
  
  void read_register(int registre);

  int get_range();

  double get_flowrate();

  char get_flowtype();


  static bool is_valid_flow(double flow);
  static bool is_valid_range(unsigned int range);
  static bool is_valid_ID(char id);
  static int get_baudrate();
  static char* get_mode();
  
  // collect old values before changing them!!!!!!
  // function not tested yet
  void set_PID(uint16_t proportional, uint16_t differential);
  // function not tested yet
  void get_PID(uint16_t& proportional, uint16_t& differential);
  
private:
  
  char addr;
  double flowrate;
  int port;
  unsigned int range;
  unsigned int proportional;
  unsigned int differential;
  char flowtype;
  
};



#endif  // _FLOW_CONTROLLER_


