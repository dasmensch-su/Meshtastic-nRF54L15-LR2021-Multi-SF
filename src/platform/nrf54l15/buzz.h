/* buzz.h — Redirect to real buzz.h now that buzz.cpp is compiled.
 * The real buzz.h is at src/buzz/buzz.h; tone() is a no-op on nRF54L15
 * so all buzz functions compile but produce no sound. */
#pragma once
#include "../../buzz/buzz.h"
