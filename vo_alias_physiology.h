#ifndef __vo_alias_h
#define __vo_alias_h

#include <vector>
#include <string>
#include <iostream>
#include <cstdlib>
#include "utils.h"


/// @{
/// Constants for the various valve blocks in the system
const int V_ODOR1_VIALA = 0;
const int V_ODOR1_VIALB = 2;
const int V_ODOR1_VIALC = 4;
const int V_ODOR1_VIALD = 6;
const int V_ODOR1_WASTE = 7;
const int V_CONTROL1_VIALA = 1;
const int V_CONTROL1_VIALB = 3;
const int V_CONTROL1_VIALC = 5;

const int V_ODOR2_VIALA = 8;
const int V_ODOR2_VIALB = 10;
const int V_ODOR2_VIALC = 12;
const int V_ODOR2_VIALD = 14;
const int V_ODOR2_WASTE = 15;
const int V_CONTROL2_VIALA = 9;
const int V_CONTROL2_VIALB = 11;
const int V_CONTROL2_VIALC = 13;

const int V_ODOR3_VIALA = 16;
const int V_ODOR3_VIALB = 18;
const int V_ODOR3_VIALC = 20;
const int V_ODOR3_VIALD = 22;
const int V_ODOR3_WASTE = 23;
const int V_CONTROL3_VIALA = 17;
const int V_CONTROL3_VIALB = 19;
const int V_CONTROL3_VIALC = 21;

const int V_BLEND1 = 24;
const int V_BLEND12 = 25;
const int V_BLEND123 = 26;
const int V_BLEND13 = 27;
const int V_BLEND2 = 28;
const int V_BLEND23 = 29;
const int V_BLEND3 = 30;
const int V_BLEND0 = 31; // Not visible in CONFIG FILE blender for control, coactive with V_CONTROLX_VIALX

const int V_WASTE1 = 32;
const int V_WASTE2 = 33;
const int V_WASTE3 = 34;
const int V_CARRIER = 35; 
const int V_BLEND_C1 = 36;
const int V_BLEND_C2 = 37;
const int V_BLEND_C3 = 38;
const int V_CONTROL = 39; // Not visible in Config file coactive with V_CONTROLX_VIALX

const int V_BOOST = 40;  // ADDED -> UPDATE PARSER

/// @}

class valve_alias {

public:
  /// \brief Converts a valve alias (e.g. Odour2_2V_AD or Blend123_12V_ABCD-ABCD-ABCD)
  ///   to a vector with the list of valves that have to be opened
  /// \param alias The alias to convert
  /// \return a vector with the list of valves, or an empty vector in case of error
  static std::vector<int> parse_alias(const std::string alias);

private:
  /// Converts an alias of type "OdourN_xV_..." to a vector of open valves
  static std::vector<int> parse_odour(const std::string name);
  /// Converts an alias of type "BlendXY_xV_..." to a vector of open valves
  ///   (e.g. Blend12_3V_BC-A or Blend123_12V_ABCD-ABCD-ABCD)
  static std::vector<int> parse_blend(const std::string name);
  /// Converts an alias of type "ControlN_nV_X..." to a vector of open valves
  static std::vector<int> parse_control(const std::string name);

  /// \brief Verifies if a string only contains valid vial IDs (A B C D);
  ///   vials must be in increasing order (A B C not A C B) and can appear only once
  /// \return true if valid, false otherwise
  static bool validate_vials(const std::string names);

  /// \brief Verifies if a string only contains valid control vial IDs (A B C);
  ///   vials must be in increasing order (A B C not A C B) and can appear only once
  /// \return true if valid, false otherwise
  static bool validate_control_vials(const std::string names);
};

#endif
