/**
 * @file spi_master.cpp
 *
 * @author FTDI
 * @date 2014-07-01
 *
 * Copyright © 2011 Future Technology Devices International Limited
 * Company Confidential
 *
 * Rivision History:
 * 1.0 - initial version
 */

//------------------------------------------------------------------------------
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <tuple>
#include <utility>

//------------------------------------------------------------------------------
// include FTDI libraries
//
#include "ftd2xx.h"
#include "LibFT4222.h"


std::vector< FT_DEVICE_LIST_INFO_NODE > g_FT4222DevList;

//------------------------------------------------------------------------------
//global variables
int SPI_MODE = 0;
int SPI_FREQ = 12; //in MHz
int SPI_LENGTH = 16; //transaction length in bits
FT_HANDLE ftHandle = NULL;
FT4222_STATUS ft4222Status;
FT4222_SPICPOL cpol = CLK_IDLE_LOW;
FT4222_SPICPHA cpha = CLK_LEADING;
FT4222_SPIMode ioLine = SPI_IO_SINGLE;
FT4222_SPIClock divider = CLK_DIV_2;


//------------------------------------------------------------------------------

inline std::string DeviceFlagToString(DWORD flags)
{
    std::string msg;
    msg += (flags & 0x1)? "DEVICE_OPEN" : "DEVICE_CLOSED";
    msg += ", ";
    msg += (flags & 0x2)? "High-speed USB" : "Full-speed USB";
    return msg;
}

void ListFtUsbDevices()
{
    FT_STATUS ftStatus = 0;

    DWORD numOfDevices = 0;
    ftStatus = FT_CreateDeviceInfoList(&numOfDevices);

    for(DWORD iDev=0; iDev<numOfDevices; ++iDev)
    {
        FT_DEVICE_LIST_INFO_NODE devInfo;
        memset(&devInfo, 0, sizeof(devInfo));

        ftStatus = FT_GetDeviceInfoDetail(iDev, &devInfo.Flags, &devInfo.Type, &devInfo.ID, &devInfo.LocId,
                                        devInfo.SerialNumber,
                                        devInfo.Description,
                                        &devInfo.ftHandle);

        if (FT_OK == ftStatus)
        {
            printf("Dev %d:\n", iDev);
            printf("  Flags= 0x%x, (%s)\n", devInfo.Flags, DeviceFlagToString(devInfo.Flags).c_str());
            printf("  Type= 0x%x\n",        devInfo.Type);
            printf("  ID= 0x%x\n",          devInfo.ID);
            printf("  LocId= 0x%x\n",       devInfo.LocId);
            printf("  SerialNumber= %s\n",  devInfo.SerialNumber);
            printf("  Description= %s\n",   devInfo.Description);
            printf("  ftHandle= 0x%x\n",    devInfo.ftHandle);

            const std::string desc = devInfo.Description;
            if(desc == "FT4222" || desc == "FT4222 A")
            {
                g_FT4222DevList.push_back(devInfo);
            }
        }
    }
}

// Convert String input to uint16 output
uint16 string_to_uint16(std::string input){
  return (uint8)std::stoi(input, nullptr, 10);
}

// Convert string input to uint8 output
uint8 string_to_uint8(std::string input){
  return (uint16)std::stoi(input, nullptr, 10);
}

/*
FT4222_SPIClock get_clock(int frequency) {
    std::vector<std::tuple> clock_vector;

    std::tuple<int, FT4222_SPIClock> value_1 = make_tuple(2, 30));
    std::tuple<int, FT4222_SPIClock> value_2 = make_tuple(4, 15));
    std::tuple<int, FT4222_SPIClock> value_3 = make_tuple(8, 7.5));
    std::tuple<int, FT4222_SPIClock> value_4 = make_tuple(16, 3.75));
    std::tuple<int, FT4222_SPIClock> value_5 = make_tuple(32, 1.875));
    std::tuple<int, FT4222_SPIClock> value_6 = make_tuple(64, 0.9375));
    std::tuple<int, FT4222_SPIClock> value_7 = make_tuple(128, 0.46875));
    std::tuple<int, FT4222_SPIClock> value_8 = make_tuple(256, 0.234375));
    std::tuple<int, FT4222_SPIClock> value_9 = make_tuple(512, 0.1171875));

    clock_vector.push_back(value_1);
    clock_vector.push_back(value_2);
    clock_vector.push_back(value_3);
    clock_vector.push_back(value_4);
    clock_vector.push_back(value_5);
    clock_vector.push_back(value_6);
    clock_vector.push_back(value_7);
    clock_vector.push_back(value_8);
    clock_vector.push_back(value_9);

    std::cout << clock_vector << std::endl;

    // for(std::vector<std::pair>::iterator i = frequency_divider_map.begin(); i != frequency_divider_map.end(); ++i) {
    //   if(*i.first == frequency) {
    //     return *i.second;
    //   }
    // }
}
*/

// Setting system clock
// The FT4222H supports 4 clock rates: 80MHz, 60MHz, 48MHz, or 24MHz.
// By default, the FT4222H runs at 60MHz clock rate.

// void set_sysclock(int freq){
//   if(freq != 80 || freq != 60 || freq != 48 || freq != 24){
//     std::cerr << "ERROR: INVALID FREQUENCY SYS CLOCK" << std::endl;
//   }
//   ft4222Status = FT4222_SetClock(ftHandle, freq);
//   if (FT4222_OK != ft4222Status){
//     // set clock failed
//     return;
//   }
// }

void SetSPIClock(double frequency){
  double frequency_list[9] = {30.0,15.0,7.5,3.75,1.88,0.94,0.47,0.23,0.12};
  FT4222_SPIClock divider_list[9] = {CLK_DIV_2,CLK_DIV_4,CLK_DIV_8,CLK_DIV_16,CLK_DIV_32,CLK_DIV_64,CLK_DIV_128,CLK_DIV_256,CLK_DIV_512};
  for(int i = 1; i < 10; ++i){
    if(frequency == frequency_list[i]){
      divider = divider_list[i];
      return;
    }
  }
  std::cerr << "INVALID SPI CLOCK" << std::endl;
}

//setting mode
void SetSPIMode(int mode){
    //check valid MODE
    if(mode < 0 || mode > 3) { //invalid MODE
      std::cerr << "INVALID MODE" << std::endl;
      return;
    }
    switch(mode) {
      case 1: //mode 1
        cpol = CLK_IDLE_LOW;
        cpha = CLK_TRAILING;
        break;
      case 2:
        cpol = CLK_IDLE_HIGH;
        cpha = CLK_LEADING;
        break;
      case 3:
        cpol = CLK_IDLE_HIGH;
        cpha = CLK_TRAILING;
        break;
      default: //already default at mode 0
        break;
    }
}

void SetChannels(int num_channels){
  //check valid number of num_channels
  if(num_channels != 1 || num_channels != 2 || num_channels != 4) {
    std::cerr << "INVALID NUMBER OF CHANNELS" << std::endl;
    return;
  }
  switch(num_channels) {
    case 2:
      ioLine = SPI_IO_DUAL;
      break;
    case 3:
      ioLine = SPI_IO_QUAD;
      break;
  }
}

// Function to set configuration of SPI output (SPI MODE, Frequency). Future implementation of multi channel
/* SPI Modes - CPOL, CPHA
0 - 0,0
1 - 0,1
2 - 1,0
3 - 1,1
*/
void SetConfiguration(int num_channels, int mode, double freq) {
  //check valid input FREQUENCY
  SetSPIMode(mode);
  SetChannels(num_channels);
  SetSPIClock(freq);

  ft4222Status = FT4222_SPIMaster_Init(ftHandle, ioLine, divider, cpol, cpha, 0x01);
}

//------------------------------------------------------------------------------
// Booting Sequence
//------------------------------------------------------------------------------

//Request and import command file


//Request SPI Characteristics


//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------
int main(int argc, char const *argv[])
{
    std::vector<std::string> commands;
    if(argc == 1) {
      std::cout << "No other command line arguments have been passed in other than the program name." << std::endl;
    } else if(argc != 4) {
      std::cout << "Missing parameters!" << std::endl;
    }

    std::ifstream imported_file("test.txt", std::ifstream::in);
    imported_file.open("config.txt");
    if(!imported_file) {
      std::cout << "Unable to open config.txt." << std::endl;
      exit(1);
    }

    std::string file_iterator;
    while(imported_file >> file_iterator) {
      commands.push_back(file_iterator);
    }

    for(std::vector<std::string>::iterator i = commands.begin(); i != commands.end(); ++i) {
      std::cout << *i << std::endl;
    }


    ListFtUsbDevices();

    if(g_FT4222DevList.empty()) {
        printf("No FT4222 device is found!\n");
        return 0;
    }

    // FT_HANDLE ftHandle = NULL;

    FT_STATUS ftStatus;
    ftStatus = FT_OpenEx((PVOID)g_FT4222DevList[0].LocId, FT_OPEN_BY_LOCATION, &ftHandle);
    if (FT_OK != ftStatus)
    {
        printf("Open a FT4222 device failed!\n");
        return 0;
    }


    ftStatus = FT4222_SPIMaster_Init(ftHandle, SPI_IO_SINGLE, CLK_DIV_4, CLK_IDLE_LOW, CLK_LEADING, 0x01);
    if (FT_OK != ftStatus)
    {
        printf("Init FT4222 as SPI master device failed!\n");
        return 0;
    }

    /* SPI Single Read */
    uint8 recvData[10];
    uint16 sizeTransferred;
    ft4222Status = FT4222_SPIMaster_SingleRead(ftHandle, &recvData[0], 10, &sizeTransferred, true);
    std::string display_status = ((FT4222_OK != ft4222Status) ? "[FAILED] SPI Master Read" : "[SUCCESS] SPI Master Read");
    std::cout << display_status << std::endl;

    /* SPI Single Write */
    uint8 sendData[10];
    sizeTransferred;
    for(int idx = 0; idx < 10; idx++) {
      sendData[idx] = idx;
    }
    ft4222Status = FT4222_SPIMaster_SingleWrite(ftHandle, &sendData[0], 10, &sizeTransferred, true);
    display_status = ((FT4222_OK != ft4222Status) ? "[FAILED] SPI Master Write" : "[SUCCESS] SPI Master Write");
    std::cout << display_status << std::endl;

    FT4222_UnInitialize(ftHandle);
    FT_Close(ftHandle);
    return 0;
}
