#include "sim.h"


void main() 
{
// Define main mem
const int   mem_depth = 1 << ADDR_WIDTH;
int static  main_mem[mem_depth];
bus_state_t bus_state = kBusAvailable;
bus_cmd_s   bus_req = {0};
int         mem_wait_counter = 0;
int         mem_rd_counter   = 0;
while (1) 
{

switch (bus_state) { 
  case kBusAvailable: 
    int gnt_core_id = round_robin_arbitrator;
    bus_req = core(gnt_core_id, 1, bus_req); // First check the core with the grant
    
    // Check all the other cores with an updated bus_req
    cores(bus_req, 1 ,gnt_core_id);
    if (bus_req.bus_cmd == kBusRd)  bus_state = kBusWaitMem;
    if (bus_req.bus_cmd == kBusRdX) bus_state = kBusWaitFlush;
    break;

  case kBusWaitMem:
    cores(bus_req, 0 ,gnt_core_id);

    if (mem_wait_counter == 15) // Also including zero
    {
      bus_state = kBusRead;
      mem_wait_counter = 0;
      bus_req.bus_cmd  = kBusRd;
      bus_req.bus_origid = main_mem_id;
    }

    else mem_wait_counter++;
    break;

  case kBusRead:
    bus_req.bus_data = main_mem[bus_req.bus_addr];
    
    cores(bus_req, 0 ,gnt_core_id);

    if (mem_rd_counter == 3) 
    {
      mem_rd_counter = 0;
      bus_state = kBusAvailable; // This is not the final step as we need to wait for flush!!!!!
      break;
    }
    mem_rd_counter++;
    bus_req.bus_addr = bus_req.bus_addr + 1;
    break;  

  case kBusWaitFlush:
    // Add relevent logic - need to check again the bus for the src core 
    break;

// NOTE Did not add support for flush yet
  }

}

}





