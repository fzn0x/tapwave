// The shared I2C and SPI buses. Drivers ask for a bus with the configured pins; the first
// request starts it and later requests with the same pins get the same bus.
#pragma once

#include <SPI.h>
#include <Wire.h>

namespace tapwave::buses {

// nullptr when the pins are not set or the bus fails to start.
TwoWire* i2c(int sda, int scl);
SPIClass* spi(int sck, int miso, int mosi);

}  // namespace tapwave::buses
