

#include "flow_controller.h"

using namespace std;

const string LINE_TERMINATOR = "\r";
const int FULL_SCALE_FLOW = 64000; // full scale flow specified for mass flow controller
const int NB_DATA = 7;
const timeval READ_TIMEOUT = {0, 600000}; // timeval struct takes seconds and microseconds

const int BAUDRATE = 19200; ///< modulation rate of data transmission (bits/sec)
char MODE[4]={'8','N','1',0};  ///< mode of serial communication: [0]:nb data bits per character(7,8); [1]:parity bit for transmission error (None,Even,Odd); [2]: nb of stop bits (1,2)

static const int MAX_FLOW = 20;
static std::vector <char> MFC_ID = make_vector<char>() <<'A'<<'B'<<'C'<<'D'<<'E'<<'F'<<'G'<<'H'<<'I'<<'J'<<'K'<<'L'<<'M'<<'N'<<'O'<<'P'<<'Q'<<'R'<<'S'<<'T'<<'U'<<'V'<<'X'<<'Y'<<'Z';
static std::vector <unsigned int> MFC_RANGE = make_vector <unsigned int>() <<1<<2<<5<<10<<20;

int PARSE_ERROR = 0;
  
bool parse_mfc_data(stringstream& ss, flow_data& flow){
	vector <string> word_table;
	//cout<<"length:"<<(ss.str()).length()<<endl;
	int ctr = chop_line(ss.str(), word_table);
	if(ctr==0){
		cerr<<"No data from flow controller"<<endl;
		return false;
	}
	// fill in flow data with words from table
	if (ctr < NB_DATA){
		cerr<<"Incomplete data from mass flow controller"<<endl;
    PARSE_ERROR++;
    //cerr<<"Parse error: "<<PARSE_ERROR<<endl;
    cerr<<ss.str()<<endl;
		return false;
	}
	
	flow.ID = word_table[0][0];
	flow.pressure = atof(word_table[1].c_str());
	flow.temperature = atof(word_table[2].c_str());
	flow.volumetric_flow = atof(word_table[3].c_str());
	flow.mass_flow = atof(word_table[4].c_str());
	flow.setpoint = atof(word_table[5].c_str());
	flow.gas = word_table[6];
	
	return true;	
}


FlowController::FlowController(){}

FlowController::~FlowController(){}

bool FlowController::is_valid_flow(double flow){
  if (flow < MAX_FLOW && flow > 0){
    return true;
  }else{
    return false;
  }
}

bool FlowController::is_valid_ID(char id){
  return (vector_contains(MFC_ID, id));
};


bool FlowController::is_valid_range(unsigned int range){
  return (vector_contains(MFC_RANGE, range));
};

int FlowController::get_baudrate(){
  return BAUDRATE;
}

int FlowController::get_range(){
  return range;
}

char* FlowController::FlowController::get_mode(){
  return MODE;
}

double FlowController::get_flowrate(){
  return flowrate;
}

char FlowController::get_flowtype(){
  return flowtype;
}


void FlowController::set_flow(double flow){
  flowrate = flow;
  int converted_flow = flow * FULL_SCALE_FLOW/ (double)range;
  string address = addr + to_string(converted_flow) + LINE_TERMINATOR;
  stringstream ss;
  // clear the serial buffer
  //RS232_ClearSerialBuffer(port);
  // need to convert flow to value to sent to port
  RS232_cputs(port, address.c_str());
  // need to do a receive here, because flow controller returns a string of flow data, allows checking whether setpoint was successfully set.
  receive(port, ss);
  flow_data new_flow;
  if (!parse_mfc_data(ss, new_flow)){
    cerr<<"MFCSET Error parsing flow data."<<endl;
    //cout<<ss.str()<<endl;
  }
  if (new_flow.setpoint == flow){
    cout<<"Setpoint successfully set to "<<flow<<endl;
  }else{
    cerr<<"Failed to set setpoint. Setpoint is "<<new_flow.setpoint<< "but should be "<<flow<<"."<<endl;
  }
}

void FlowController::balance_carrier_boost(FlowController& carrier, double carrier_flow, FlowController& boost, double boost_flow){ 

  carrier.flowrate = carrier_flow;
  int converted_flow1 = carrier_flow * FULL_SCALE_FLOW/ (double)carrier.range;
  string address = carrier.addr + to_string(converted_flow1) + LINE_TERMINATOR;
  
  boost.flowrate = boost_flow;
  int converted_flow2 = boost_flow * FULL_SCALE_FLOW/ (double)boost.range;
  address = address + boost.addr + to_string(converted_flow2) + LINE_TERMINATOR;
  
  // send concatenated commands to port
  RS232_cputs(carrier.port, address.c_str());
  // do a wait here and then clear the serial buffer to avoid any nonsense 
  // after each sent command the MFC respond (cannot inactivate it), because the two commands are send in a concatenated way, 
  // answers from two MFCs will be transferred at the same time, which means that the MFC responses will be mixed up and unintelligable. 
  usleep (50000);
  RS232_ClearSerialBuffer(carrier.port);  
}


void FlowController::set_port(int p){
  port = p;
}

void FlowController::init(int p, char a, unsigned int r, char t){
  port = p;
  addr = a;
  range = r;
  flowtype = t;
  flowrate = 0;
}


void FlowController::receive(int port, stringstream& ss){
  bool done (false);
  //usleep(50000); // wait for answer of MFC because port is used in asychronous mode and synchronous mode requires select which is broken
  while (!done){
    unsigned char buf[1];
    int bytes_received = 0;
    //bytes_received = RS232_PollComport(port, buf, sizeof(buf));
    bytes_received = RS232_PollComport_wTimeout(port, buf, sizeof(buf), READ_TIMEOUT);
    //cerr<<(unsigned int) buf<< " "<<endl;
    //cerr<<"bytes received: "<<bytes_received<<endl;
    if(bytes_received==-1){
      done = true; // leave loop because of error
      cerr<<"Exiting because of error."<<endl;
    }
    if (bytes_received==0){
      cerr<<"Timeout: no data received from mass flow controller "<<addr<<endl;
      done = true;  // leave lopp because of timeout
    }  
    int i(0);
    do {
      if (buf[i] != LINE_TERMINATOR[0]){
        ss<<buf[i];
      }else{
        //cerr<<"received line terminator: "<<(int) buf<<endl;
        done = true;  // leave loop because end of line
      }
      i++;
    } while(i< bytes_received && !done);
  }
}


void FlowController::get_flow_data(flow_data& flow){
  string address = addr + LINE_TERMINATOR;
  //S232_ClearSerialBuffer(port);
  RS232_cputs(port, address.c_str());
  //double timestart = time_monotonic();
  stringstream ss;
  receive(port, ss);
  //double timeend = time_monotonic();
  //cout<<"time: "<<timeend-timestart-(1000/(BAUDRATE/520))<<endl;
  // read data from stringstream into flow_data struct
  if (!parse_mfc_data(ss, flow)){
	  cerr<<"Polling thread: Error parsing flow data."<<endl;
	  //cout<<ss.str()<<endl;
  }
}

void FlowController::change_addr(char addr){
  string address ="*@=" + addr + LINE_TERMINATOR;
  RS232_cputs(port, address.c_str());
}


// read register values of a given flow controller 
// BEWARE: only one flow controller should be connected to the serial port
void FlowController::read_register(int registre){
  if(registre >=0 && registre <1000){
    string address ="*r" + to_string(registre) + LINE_TERMINATOR;
    //cout<<"command sent: "<< address<<endl;
    RS232_cputs(port, address.c_str());
    stringstream ss;
    receive(port, ss);
    string tmp = ss.str();
    cout<<tmp<<endl;
  }else{
    cerr<<"Error: Register value is invalid."<<endl;
  }
}

// collect old values before changing them!!!!!!
void FlowController::set_PID(uint16_t proportional, uint16_t differential){
  string adjust_prop ="*W21="+ to_string(proportional) + LINE_TERMINATOR;
  RS232_cputs(port, adjust_prop.c_str());
  string adjust_diff ="*W22="+ to_string(differential) + LINE_TERMINATOR;
  RS232_cputs(port, adjust_prop.c_str());
}

void FlowController::get_PID(uint16_t& proportional, uint16_t& differential){
  string prop ="*R21="+ LINE_TERMINATOR;
  RS232_cputs(port, prop.c_str());
  stringstream ps;
  receive(port, ps);
  string diff ="*R22="+ LINE_TERMINATOR;
  RS232_cputs(port, diff.c_str());
  stringstream ds;
  receive(port, ds);
  
  // extract something with ps and ds
  
}

