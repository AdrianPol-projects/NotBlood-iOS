//-------------------------------------------------------------------------
/*
This file is part of NotBlood (iOS port).

NotBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.
*/
//-------------------------------------------------------------------------
#pragma once

#ifdef EDUKE32_IOS

struct TouchInput
{
    float forward; // -1..1, stick up is positive
    float strafe;  // -1..1, stick right is positive
    float yaw;     // degrees turned since last call, right is positive
    float pitch;   // look units (see controls.cpp) since last call, up is positive
    bool  run;     // "always run" for the touch stick
};

// Consumes the accumulated look deltas.
void touch_getInput(TouchInput *pInput);

#endif
