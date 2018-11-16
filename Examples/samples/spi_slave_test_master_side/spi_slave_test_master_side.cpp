/**
 * @file spi_slave_test_master_side.cpp
 *
 * @author FTDI
 * @date 2014-07-01
 *
 * Copyright © 2011 Future Technology Devices International Limited
 * Company Confidential
 *
 * Revision History:
 * 1.0 - initial version
 */

//------------------------------------------------------------------------------
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <conio.h>
#include <algorithm>



//------------------------------------------------------------------------------
// include FTDI libraries
//
#include "ftd2xx.h"
#include "LibFT4222.h"


#define USER_WRITE_REQ      0x4a
#define USER_READ_REQ       0x4b

std::vector< FT_DEVICE_LIST_INFO_NODE > g_FTAllDevList;
std::vector< FT_DEVICE_LIST_INFO_NODE > g_FT4222DevList;

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
            g_FTAllDevList.push_back(devInfo);

            if(desc == "FT4222" || desc == "FT4222 A")
            {
                g_FT4222DevList.push_back(devInfo);
            }
        }
    }
}

//-----------------------------------------------------------------------------------------
namespace
{

class TestPatternGenerator
{
public:
    TestPatternGenerator(uint16 size)
    {
        data.resize(size);

        for (uint16 i = 0; i < data.size(); i++)
        {
            data[i] = (uint8)i;
        }
    }

public:
    std::vector< unsigned char > data;
};
}

//-----------------------------------------------------------------------------------------

static uint8 g_seq_number = 0;

uint8 get_seq_number()
{
    uint8 seqNum = g_seq_number;

    if(g_seq_number == 255)
        g_seq_number =0;
    else
        g_seq_number++;

    return seqNum;
}


uint16 reverse_byte_order(uint16 x)
{
    return (0xFF00 &(x<<8)) + (0x00FF & (x>>8));
}


uint16 getCheckSum(std::vector<unsigned char> & sendBuf, uint16 sizeOfcheck)
{
    uint32 sum=0;
    uint16 data_size = std::min<size_t>(sendBuf.size(), sizeOfcheck);

    for(int idx=0; idx< data_size; idx++)
    {
        sum += sendBuf[idx];
    }

    return (uint16)sum;
}


std::vector<unsigned char>  spi_master_construt_send_packet(uint16 wantSendByte, uint8 & sendID)
{
    std::vector<unsigned char> sendBuf;

    TestPatternGenerator testPattern(wantSendByte);
    uint16 checksum;
    SPI_Slave_Header header;

    header.syncWord = FT4222_SPI_SLAVE_SYNC_WORD;
    header.cmd = SPI_MASTER_TRANSFER;
    header.sn = sendID = get_seq_number();
    header.size = reverse_byte_order(wantSendByte+1);
    // add spi slave header
    sendBuf.insert(sendBuf.begin() + sendBuf.size(), (uint8*)&header, ((uint8*)&header) + sizeof(header));
    // write req in data field
    sendBuf.insert(sendBuf.begin() + sendBuf.size(), USER_WRITE_REQ);
    // add write data
    sendBuf.insert(sendBuf.begin() + sendBuf.size(), (uint8*)&testPattern.data[0], ((uint8*)&testPattern.data[0]) + wantSendByte);
    // add spi slave checksum
    checksum = reverse_byte_order(getCheckSum(sendBuf, sendBuf.size()));
    sendBuf.insert(sendBuf.begin() + sendBuf.size(), (uint8*)&checksum, ((uint8*)&checksum) + sizeof(uint16));

    return sendBuf;

}



std::vector<unsigned char>  spi_master_construt_recv_packet(uint16 wantSendByte, uint8 & sendID)
{
    std::vector<unsigned char> sendBuf;
    uint16 checksum;
    SPI_Slave_Header header;
    uint16 sizeOfRead = reverse_byte_order(wantSendByte);

    header.syncWord = FT4222_SPI_SLAVE_SYNC_WORD;
    header.cmd = SPI_MASTER_TRANSFER;
    header.sn = sendID = get_seq_number();
    header.size = reverse_byte_order(3);
    // add spi slave header
    sendBuf.insert(sendBuf.begin() + sendBuf.size(), (uint8*)&header, ((uint8*)&header) + sizeof(header));
    // read req in data field
    sendBuf.insert(sendBuf.begin() + sendBuf.size(), USER_READ_REQ);
    // read size
    sendBuf.insert(sendBuf.begin() + sendBuf.size(), (uint8*)&sizeOfRead, ((uint8*)&sizeOfRead) + sizeof(uint16));
    // add spi slave checksum
    checksum = reverse_byte_order(getCheckSum(sendBuf, sendBuf.size()));
    sendBuf.insert(sendBuf.begin() + sendBuf.size(), (uint8*)&checksum, ((uint8*)&checksum) + sizeof(uint16));

    return sendBuf;

}

#define SPI_SLAVE_HEADER_SIZE    7
bool parser_packet(std::vector<unsigned char> &recvBuf, uint8 sn , bool checkSn, std::vector<unsigned char> & slaveData)
{
    int idx=0;

    while(recvBuf.size()>=SPI_SLAVE_HEADER_SIZE)
    {
        bool syncWord = false;
        int eraseSize =0;
        for(idx=0;idx<recvBuf.size();idx++)
        {
            if(recvBuf[idx] == FT4222_SPI_SLAVE_SYNC_WORD)
            {
                syncWord = true;
                break;
            }
        }

        if(syncWord)
            eraseSize = idx;
        else
            eraseSize = recvBuf.size();

        if(eraseSize > 0)
            recvBuf.erase (recvBuf.begin(),recvBuf.begin()+eraseSize);
        // pop dummy data

        uint8 dropSize = 0;
        if(syncWord && recvBuf.size()>=SPI_SLAVE_HEADER_SIZE)
        {
            if(!(recvBuf[1] == SPI_ACK || recvBuf[1] ==SPI_SLAVE_TRANSFER))
            {
                dropSize = 2;
            }
            else
            {
                if(checkSn && recvBuf[2] != sn)
                {
                    dropSize = 3;
                }
                else
                {
                    uint16 dataSize = recvBuf[3]*256 + recvBuf[4];

                    if((dataSize+SPI_SLAVE_HEADER_SIZE) > recvBuf.size())
                    {
                        // wait for more data to parse
                        return false;
                    }

                    uint16 checksum1  = recvBuf[dataSize+5]*256 + recvBuf[dataSize+6];
                    uint16 checksum2 = getCheckSum(recvBuf,dataSize+5);

                    if(checksum1 == checksum2)
                    {
                        if(dataSize >0)
                        {
                            slaveData.insert(slaveData.begin() + slaveData.size(), (uint8*)&recvBuf[5], ((uint8*)&recvBuf[5]) + dataSize);
                        }

                        recvBuf.erase (recvBuf.begin(),recvBuf.begin()+(dataSize+SPI_SLAVE_HEADER_SIZE));
                        printf("get ack %d\n",sn);
                        return true;
                    }
                    else
                    {
                        dropSize = 7;
                    }

                }
            }

            if(dropSize >0)
            {
                recvBuf.erase (recvBuf.begin(),recvBuf.begin()+dropSize);

            }
        }
    }


    return false;

}

// check slave ack
bool parser_packet(std::vector<unsigned char> &recvBuf, uint8 sn)
{
    std::vector<unsigned char> tmpData;
    return parser_packet(recvBuf, sn, true,tmpData);
}

// check slave data
bool parser_packet(std::vector<unsigned char> &recvBuf, std::vector<unsigned char> & slaveData)
{
    return parser_packet(recvBuf, 0, false, slaveData);
}

#define PARSER_SIZE                         128
bool spi_master_get_write_ack(FT_HANDLE ftHandle, uint8 sn ,std::vector<unsigned char> & recvBuf)
{
    uint16 sizeTransferred;
    std::vector<unsigned char> tempBuf;
    FT_STATUS ftStatus;

    tempBuf.resize(PARSER_SIZE);

    for(int idx=0;idx<1024/PARSER_SIZE;idx++)
    {
        ftStatus = FT4222_SPIMaster_SingleRead(ftHandle,&tempBuf[0], PARSER_SIZE, &sizeTransferred, true);
        if (FT_OK != ftStatus)
        {
            printf("FT4222_SPIMaster_SingleRead!\n");
            return 0;
        }

        recvBuf.insert(recvBuf.begin() + recvBuf.size(), (uint8*)&tempBuf[0], ((uint8*)&tempBuf[0]) + tempBuf.size());
        if(!parser_packet(recvBuf, sn))
        {
           // printf("can not get sn = %d, retry\n",sn);
        }
        else
        {
            return true;
        }
    }

    return false;
}


bool spi_master_get_read_response(FT_HANDLE ftHandle, std::vector<unsigned char> & recvBuf, std::vector<unsigned char> & slaveData)
{
    uint16 sizeTransferred;
    std::vector<unsigned char> tempBuf;
    FT_STATUS ftStatus;

    tempBuf.resize(PARSER_SIZE);

    for(int idx=0;idx<1024/PARSER_SIZE;idx++)
    {
        ftStatus = FT4222_SPIMaster_SingleRead(ftHandle,&tempBuf[0], PARSER_SIZE, &sizeTransferred, true);
        if (FT_OK != ftStatus)
        {
            printf("FT4222_SPIMaster_SingleRead!\n");
            return 0;
        }


        recvBuf.insert(recvBuf.begin() + recvBuf.size(), (uint8*)&tempBuf[0], ((uint8*)&tempBuf[0]) + tempBuf.size());
        if(!parser_packet(recvBuf, slaveData))
        {
           // printf("can not get sn = %d, retry\n",sn);
        }
        else
        {
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------

int main(int argc, char const *argv[])
{
    ListFtUsbDevices();
     if(g_FT4222DevList.empty()) {
        printf("No FT4222 device is found!\n");
        return 0;
    }

    int num=0;
    for(int idx=0;idx<10;idx++)
    {
        printf("select dev num(0~%d) as spi master\n",g_FTAllDevList.size()-1);
        num = getch();
        num = num - '0';
        if(num >=0 && num<g_FTAllDevList.size())
        {
            break;
        }
        else
        {
            printf("input error , please input again\n");
        }

    }


    FT_HANDLE ftHandle = NULL;
    FT_STATUS ftStatus;
    FT_STATUS ft4222_status;
     ftStatus = FT_Open(num, &ftHandle);
    if (FT_OK != ftStatus)
    {
        printf("Open a FT4222 device failed!\n");
        return 0;
    }
     //Set default Read and Write timeout 1 sec
    ftStatus = FT_SetTimeouts(ftHandle, 1000, 1000 );
    if (FT_OK != ftStatus)
    {
        printf("FT_SetTimeouts failed!\n");
        return 0;
    }

    // no latency to usb
    ftStatus = FT_SetLatencyTimer(ftHandle, 0);
    if (FT_OK != ftStatus)
    {
        printf("FT_SetLatencyTimerfailed!\n");
        return 0;
    }

    //
    ftStatus = FT_SetUSBParameters(ftHandle, 64*1024, 0);
    if (FT_OK != ftStatus)
    {
        printf("FT_SetUSBParameters failed!\n");
        return 0;
    }


    ft4222_status = FT4222_SPIMaster_Init(ftHandle, SPI_IO_SINGLE, CLK_DIV_4, CLK_IDLE_LOW, CLK_LEADING, 0x01);
    if (FT4222_OK != ft4222_status)
    {
        printf("Init FT4222 as SPI master device failed!\n");
        return 0;
    }

    ft4222_status = FT4222_SPI_SetDrivingStrength(ftHandle,DS_8MA, DS_8MA, DS_8MA);
    if (FT4222_OK != ft4222_status)
    {
        printf("FT4222_SPI_SetDrivingStrength failed!\n");
        return 0;
    }
    uint8 sendID;
    uint16 sizeTransferred;
    std::vector<unsigned char> sendBuf;
    std::vector<unsigned char> recvBuf;
    std::vector<unsigned char> slaveData;
     // spi master write data to slave
    sendBuf = spi_master_construt_send_packet(10, sendID);
    printf("send write req sn = %d\n",sendID);
    ft4222_status = FT4222_SPIMaster_SingleWrite(ftHandle,&sendBuf[0], sendBuf.size(), &sizeTransferred, true);
    if (FT4222_OK != ft4222_status)
    {
        printf("spi master write single failed %x!\n",ft4222_status);
        return 0;
    }

    if(!spi_master_get_write_ack(ftHandle, sendID ,recvBuf))
    {
        printf("can not get ack\n");
        return 0;
    }
    else
    {
        printf("get write data ack\n");
    }
     // spi master read data to slave
    sendBuf = spi_master_construt_recv_packet(10, sendID);
    printf("send read req sn = %d\n",sendID);
    ft4222_status = FT4222_SPIMaster_SingleWrite(ftHandle,&sendBuf[0], sendBuf.size(), &sizeTransferred, true);
    if (FT4222_OK != ft4222_status)
    {
        printf("spi master write single failed %x!\n",ft4222_status);
        return 0;
    }

    if(!spi_master_get_write_ack(ftHandle, sendID ,recvBuf))
    {
        printf("can not get ack\n");
        return 0;
    }
    else
    {
        printf("get write data ack\n");
    }

    if(!spi_master_get_read_response(ftHandle,recvBuf,slaveData))
    {
        printf("can not get response\n");
        return 0;
    }
    else
    {
        // show the data spi slave sent
        for(int idx=0; idx<slaveData.size(); idx++)
        {
            printf("recv data [%d] = [%x]\n", idx, slaveData[idx]);
        }
    }

    FT4222_UnInitialize(ftHandle);
    FT_Close(ftHandle);
    return 0;
}
