#pragma once
#include <stddef.h>
#include <string.h>
#include <stdint.h>

namespace probot::io::joystick_mapping {

struct MappingProfile {
  const char* name;
  // Axes
  int leftX;  int leftY;
  int rightX; int rightY;
  bool invertLeftY;  bool invertRightY;
  // Buttons (standard mapping indices)
  int A, B, X, Y;
  int LB, RB;
  int LT, RT;        // triggers as buttons (bool)
  int Back, Start, Options;
  int LStick, RStick;
  // D-Pad
  bool dpadFromAxis; int dpadAxis; // if true, use axis as POV-like; else below buttons
  int DPadUp, DPadDown, DPadLeft, DPadRight; // only if dpadFromAxis==false
};

inline const MappingProfile& mapping_logitech_f310(){
  static MappingProfile m{
    /*name*/        "logitech-f310",
    /*axes*/        0,1, 2,3, /*invert*/ true, true,
    /*buttons*/     0,1,2,3, 4,5, 6,7, 8,9,-1, 10,11,
    /*dpad axis?*/  false, -1,
    /*dpad btns*/   12,13,14,15
  };
  return m;
}

inline const MappingProfile& mapping_standard(){
  // W3C Gamepad Standard mapping
  static MappingProfile m{
    "standard",
    0,1, 2,3, true, true,
    0,1,2,3, 4,5, 6,7, 8,9,-1, 10,11,
    false, -1,
    12,13,14,15
  };
  return m;
}

inline const MappingProfile& mapping_axis9_dpad(){
  // Use axis 9 as POV-like for D-Pad (fallback)
  static MappingProfile m{
    "axis9-dpad",
    0,1, 2,3, true, true,
    0,1,2,3, 4,5, 6,7, 8,9,-1, 10,11,
    true, 9,
    -1,-1,-1,-1
  };
  return m;
}

inline const MappingProfile& mapping_tuna_default(){
  // Replicates historical TunaGamepad mapping
  static MappingProfile m{
    "tuna-default",
    /*axes*/        0,1, 2,5, /*invert*/ true, true,
    /*buttons*/     /*A,B,X,Y*/ 2,1,3,0,
                    /*LB,RB*/   4,8,
                    /*LT,RT*/   5,9,
                    /*Back,Start,Options*/ 8,11,10,
                    /*L/R stick btn*/  -1,-1,
    /*dpad axis?*/  true, 9,
    /*dpad btns*/   -1,-1,-1,-1
  };
  return m;
}

inline const MappingProfile* findByName(const char* name){
  if (!name) return &mapping_logitech_f310();
  if (strcmp(name, "logitech-f310")==0 || strcmp(name, "f310")==0) return &mapping_logitech_f310();
  if (strcmp(name, "standard")==0) return &mapping_standard();
  if (strcmp(name, "axis9-dpad")==0) return &mapping_axis9_dpad();
  if (strcmp(name, "tuna-default")==0 || strcmp(name, "tuna")==0) return &mapping_tuna_default();
  // alias examples
  if (strcmp(name, "xbox-one")==0 || strcmp(name, "xbox")==0) return &mapping_standard();
  if (strcmp(name, "dualshock-4")==0 || strcmp(name, "ds4")==0 || strcmp(name, "ps")==0) return &mapping_standard();
  return &mapping_logitech_f310();
}

inline const MappingProfile*& activeMapping(){
  static const MappingProfile* active = &mapping_logitech_f310();
  return active;
}

inline const MappingProfile& getActive(){
  return *activeMapping();
}

inline void setActive(const MappingProfile* m){
  if (!m) return;
  activeMapping() = m;
}

inline bool setActiveByName(const char* name){
  const MappingProfile* m = findByName(name);
  setActive(m);
  return true;
}

} // namespace probot::io::joystick_mapping
