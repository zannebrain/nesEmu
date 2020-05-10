#pragma once

#include "stdafx.h"
#include <stdint.h>

enum ButtonFlags : uint8_t
{
	BUTTON_NONE = 0X00,

	BUTTON_RIGHT = 0X01,
	BUTTON_LEFT = 0X02,
	BUTTON_DOWN = 0X04,
	BUTTON_UP = 0X08,

	BUTTON_START = 0X10,
	BUTTON_SELECT = 0X20,
	BUTTON_B = 0X40,
	BUTTON_A = 0X80,
};


enum ControllerId : uint8_t
{
	CONTROLLER_0 = 0X00,
	CONTROLLER_1 = 0X01,
	CONTROLLER_2 = 0X02,
	CONTROLLER_3 = 0X03,
	CONTROLLER_COUNT,
};


extern ButtonFlags keyBuffer[2];


inline ButtonFlags GetKeyBuffer( const int controllerId )
{
	return keyBuffer[controllerId];
}


// TODO: make thread safe -- look at CaptureKey function I started
// keyBuffer is only written by store key so it's guaranteed read only elsewhere
inline void StoreKey( const uint8_t controllerId, uint8_t key )
{
	keyBuffer[controllerId] = static_cast<ButtonFlags>( keyBuffer[controllerId] | key );
}


inline void ReleaseKey( const uint8_t controllerId, uint8_t key )
{
	keyBuffer[controllerId] = static_cast<ButtonFlags>( keyBuffer[controllerId] & ~key );
}