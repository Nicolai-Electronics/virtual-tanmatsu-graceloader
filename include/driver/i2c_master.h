#pragma once

// Minimal desktop stand-in for ESP-IDF's driver/i2c_master.h.
// The SDL target has no real I2C bus, so this is an opaque handle that is
// never dereferenced.

typedef struct i2c_master_bus_t* i2c_master_bus_handle_t;
