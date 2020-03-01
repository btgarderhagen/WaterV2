#include <Arduino.h>
#include "pass.cpp"



// if (devicetype = 1)
// {
//     // MODUINO
//     const int flowmeterPin 36 //Moduino 36 - IN1
//     const int buttonPin 32
//     const int resetPin 34     //Moduino 34 - User Button
//     const int OLED_ADDRESS 0x3c
//     const int I2C_SDA 12
//     const int I2C_SCL 13
//     const int DISPLAYTYPE GEOMETRY_128_64
//     const int baud 115200
//     String rotate = "Yes";
// }

// if(devicetype == 2)
// {
//     // TTGO
//     #define flowmeterPin 32   //02
//     #define buttonPin 32      //32
//     #define resetPin 34
//     #define OLED_ADDRESS 0x3c
//     #define I2C_SDA 14
//     #define I2C_SCL 13
//     #define DISPLAYTYPE GEOMETRY_128_32
//     #define baud 115200
//     #define displayorientation TEXT_ALIGN_RIGHT  
//     String rotate = "No";
// }