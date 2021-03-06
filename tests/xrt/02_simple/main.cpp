/**
 * Copyright (C) 2016-2018 Xilinx, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#include <getopt.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstring>
#include <sys/mman.h>
#include <time.h>
#include <chrono>
#include <thread>

// driver includes
#include "ert.h"

// host_src includes
#include "xclhal2.h"
#include "xclbin.h"

// lowlevel common include
#include "utils.h"

#if defined(DSA64)
#include "xsimple_hw_64.h"     
#else
#include "xsimple_hw.h"  
#endif

static const int count = 1024;
int foo;

const static struct option long_options[] = {
{"hal_driver",      required_argument, 0, 's'},
{"bitstream",       required_argument, 0, 'k'},
{"hal_logfile",     required_argument, 0, 'l'},
{"device",          required_argument, 0, 'd'},
{"verbose",         no_argument,       0, 'v'},
{"help",            no_argument,       0, 'h'},
{0, 0, 0, 0}
};

static void printHelp()
{
    std::cout << "usage: %s [options] -k <bitstream>\n\n";
    std::cout << "  -s <hal_driver>\n";
    std::cout << "  -k <bitstream>\n";
    std::cout << "  -l <hal_logfile>\n";
    std::cout << "  -d <index>\n";
    std::cout << "  -v\n";
    std::cout << "  -h\n\n";
    std::cout << "* If HAL driver is not specified, application will try to find the HAL driver\n";
    std::cout << "  using XILINX_OPENCL and XCL_PLATFORM environment variables\n";
    std::cout << "* Bitstream is required\n";
    std::cout << "* HAL logfile is optional but useful for capturing messages from HAL driver\n";
}

static int runKernel(xclDeviceHandle &handle, uint64_t cu_base_addr, size_t alignment, bool ert, bool verbose, int first_mem, unsigned cu_index, uuid_t xclbinId)
{
    if(xclOpenContext(handle, xclbinId, cu_index, true))
        throw std::runtime_error("Cannot create context");

    const size_t DATA_SIZE = count * sizeof(int);

    unsigned boHandle1 = xclAllocBO(handle, DATA_SIZE, 0, first_mem); //output s1
    unsigned boHandle2 = xclAllocBO(handle, DATA_SIZE, 0, first_mem); // input s2
    int *bo2 = (int*)xclMapBO(handle, boHandle2, true);
    int *bo1 = (int*)xclMapBO(handle, boHandle1, true);
    
    int bufReference[count];

    memset(bo2, 0, DATA_SIZE);

    // Fill our data sets with pattern
    foo = 0x10;
    for (int i = 0; i < count; i++) {
        bo2[i] = i * i;
        bo1[i] = 0X586C0C6C; // XL OpenCL X->58(ASCII), L->6C(ASCII), O->0 C->C L->6C(ASCII)
        bufReference[i] = bo2[i] + i * foo;
    }

    if(xclSyncBO(handle, boHandle2, XCL_BO_SYNC_BO_TO_DEVICE , count * sizeof(int), 0)) {
        return 1;
    }
    
    if(xclSyncBO(handle, boHandle1, XCL_BO_SYNC_BO_TO_DEVICE , count * sizeof(int), 0)) {
        return 1;
    }


    xclBOProperties p;
    uint64_t bo2devAddr = !xclGetBOProperties(handle, boHandle2, &p) ? p.paddr : -1;
    uint64_t bo1devAddr = !xclGetBOProperties(handle, boHandle1, &p) ? p.paddr : -1;

    if( (bo2devAddr == (uint64_t)(-1)) || (bo1devAddr == (uint64_t)(-1)))
        return 1;
    unsigned execHandle = xclAllocBO(handle, DATA_SIZE, 0, (1<<31));
    void* execData = xclMapBO(handle, execHandle, true);

    std::cout << "Construct the exec command to run the kernel on FPGA" << std::endl;
    std::cout << "Due to the 1D OpenCL group size, the kernel must be launched ("<< count << ") times" << std::endl;
    //--
    //construct the exec buffer cmd to start the kernel.
    for (int id = 0; id < count; id++) {
        {
            auto ecmd = reinterpret_cast<ert_start_kernel_cmd*>(execData);
            auto rsz = XSIMPLE_CONTROL_ADDR_FOO_DATA/4 + 2; // regmap array size
            std::memset(ecmd,0,(sizeof *ecmd) + rsz);
            ecmd->state = ERT_CMD_STATE_NEW;
            ecmd->opcode = ERT_START_CU;
            ecmd->count = 1 + rsz;
            ecmd->cu_mask = 0x1;
            
            ecmd->data[XSIMPLE_CONTROL_ADDR_AP_CTRL] = 0x0; // ap_start
            ecmd->data[XSIMPLE_CONTROL_ADDR_GROUP_ID_X_DATA/4] = id; // group id
            ecmd->data[XSIMPLE_CONTROL_ADDR_S1_DATA/4] = bo1devAddr & 0xFFFFFFFF;
            ecmd->data[XSIMPLE_CONTROL_ADDR_S2_DATA/4] = bo2devAddr & 0xFFFFFFFF;
#if defined(DSA64)
            ecmd->data[XSIMPLE_CONTROL_ADDR_S1_DATA/4 + 1] = (bo1devAddr >> 32) & 0xFFFFFFFF; // output
            ecmd->data[XSIMPLE_CONTROL_ADDR_S2_DATA/4 + 1] = (bo2devAddr >> 32) & 0xFFFFFFFF; // input
#endif
            ecmd->data[XSIMPLE_CONTROL_ADDR_FOO_DATA/4] = 0x10; //foo
        }

        //Send the "start kernel" command.
        if(xclExecBuf(handle, execHandle)) {
            std::cout << "Unable to issue xclExecBuf : start_kernel" << std::endl;
            std::cout << "FAILED TEST\n";
            std::cout << "Write failed\n";
            return 1;
        }
        /*
        else {
            std::cout << "Kernel start command issued through xclExecBuf : start_kernel" << std::endl;
            std::cout << "Now wait until the kernel finish" << std::endl;
        }
        */

        //Wait on the command finish
        while (xclExecWait(handle,100) == 0) {
            std::cout << "reentering wait...\n";
        };


    }
    

    //Get the output;
    std::cout << "Get the output data from the device" << std::endl;
    if(xclSyncBO(handle, boHandle1, XCL_BO_SYNC_BO_FROM_DEVICE, DATA_SIZE, 0)) {
        return 1;
    }

    /*
    for (int i = 0; i < count; i++) {
        std::cout << "bo1[" << i << "]= " << bo1[i]<< ", bufReference[" << i << "]= " << bufReference[i] << std::endl;
    }
    */
    // Validate our results
    //
    if (std::memcmp(bo1, bufReference, DATA_SIZE)) {
        std::cout << "FAILED TEST\n";
        std::cout << "Value read back does not match value written\n";
        return 1;
    }

    // Clean up stuff
    munmap(bo1, DATA_SIZE);
    munmap(bo2, DATA_SIZE);
    munmap(execData, DATA_SIZE);
    xclFreeBO(handle, boHandle1);
    xclFreeBO(handle, boHandle2);
    xclFreeBO(handle, execHandle);

    xclCloseContext(handle, xclbinId, cu_index);

    return 0;
}


int main(int argc, char** argv)
{
    std::string sharedLibrary;
    std::string bitstreamFile;
    std::string halLogfile;
    size_t alignment = 128;
    int option_index = 0;
    unsigned index = 0;
    unsigned cu_index = 0;
    bool verbose = false;
    bool ert = false;
    int c;
    //size_t n_elements = 16;
    //int cu = 0;
    //findSharedLibrary(sharedLibrary);

    while ((c = getopt_long(argc, argv, "s:k:l:d:vh", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 0:
            if (long_options[option_index].flag != 0)
                break;
        case 's':
            sharedLibrary = optarg;
            break;
        case 'k':
            bitstreamFile = optarg;
            break;
        case 'l':
            halLogfile = optarg;
            break;
        case 'd':
            index = std::atoi(optarg);
            break;
        case 'h':
            printHelp();
            return 0;
        case 'v':
            verbose = true;
            break;
        default:
            printHelp();
            return 1;
        }
    }

    (void)verbose;

    if (bitstreamFile.size() == 0) {
        std::cout << "FAILED TEST\n";
        std::cout << "No bitstream specified\n";
        return -1;
    }

    if (halLogfile.size()) {
        std::cout << "Using " << halLogfile << " as HAL driver logfile\n";
    }

    std::cout << "HAL driver = " << sharedLibrary << "\n";
    std::cout << "Host buffer alignment = " << alignment << " bytes\n";
    std::cout << "Compiled kernel = " << bitstreamFile << "\n";


    try {
        xclDeviceHandle handle;
        uint64_t cu_base_addr = 0;
        int first_mem = -1;
        uuid_t xclbinId;

        if (initXRT(bitstreamFile.c_str(), index, halLogfile.c_str(), handle, cu_index, cu_base_addr, first_mem, xclbinId))
            return 1;

        if (first_mem < 0)
            return 1;
        
        if (runKernel(handle, cu_base_addr, alignment, ert, verbose, first_mem, cu_index, xclbinId))
            return 1;

    }
    catch (std::exception const& e)
    {
        std::cout << "Exception: " << e.what() << "\n";
        std::cout << "FAILED TEST\n";
        return 1;
    }

    std::cout << "PASSED TEST\n";
    return 0;
}
