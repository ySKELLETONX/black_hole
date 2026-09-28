// Builds the miniaudio implementation in its own translation unit
// (keeps <windows.h> and friends out of black_hole.cpp).
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
