#pragma once

// Minimal desktop stand-in for ESP-IDF's driver/i2s_types.h.
// The SDL target has no real I2S peripheral, so this is an opaque handle
// that is never dereferenced.

typedef struct i2s_channel_obj_t* i2s_chan_handle_t;
