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
    if (bus_req.bus_cmd == kBusRd || bus_req.bus_cmd == kBusRdX)  // We wait for flush from either another core or the memory
    {
      bus_state = kBusWaitMem;
      mem_wait_counter = 0;
    }

    break;

  case kBusWaitMem:
    bus_cmd_s core_cmd = cores(bus_req, 0 ,gnt_core_id);

    // We listen to flush even without a gnt
    if (core_cmd.bus_cmd == kFlush) // If we see another flush from core while waiting we update the 
    {
      if (core_cmd.bus_addr != bus_req.bus_addr) puts("Error: Flush has been issued from core to an unread addr");
      bus_state = kBusWaitFlush; // We wait for all the new data to come from the flushing core
      main_mem[bus_req.bus_addr] = core_cmd.bus_data; //The write to the memory takes place on the same time as the flush arrives
      mem_rd_counter = 1;
      bus_req.bus_origid = core_cmd.bus_origid; //Set the flushing core as the sender of the trans
      break;
    }
      

    if (mem_wait_counter == 15) // Also including zero
    {
      bus_state = kBusRead;
      mem_wait_counter = 0;
      mem_rd_counter   = 0;
      bus_req.bus_cmd  = kFlush; //After read req return the data with flush
      bus_req.bus_origid = main_mem_id;
    }

    else mem_wait_counter++;
    break;

  case kBusRead:
    bus_req.bus_data = main_mem[bus_req.bus_addr];
    bus_req.bus_cmd = kFlush; // When returning data the command is flush

    cores(bus_req, 0 ,gnt_core_id);

    if (mem_rd_counter == 3) 
    {
      bus_state = kBusAvailable; 
      break;
    }
    mem_rd_counter++;
    bus_req.bus_addr = bus_req.bus_addr + 1;
    break;  

  case kBusWaitFlush:
    bus_cmd_s core_cmd = core(bus_req.bus_origid, 1 , bus_req);
    if (core_cmd.bus_cmd != kFlush) puts("Flush was initiated by core but stopped mid way");
    cores(bus_req, 0 ,bus_req.bus_origid);

    main_mem[core_cmd.bus_addr] = core_cmd.bus_data;

    if (mem_rd_counter == 3) 
    {
      bus_state = kBusAvailable; 
      break;
    }
    mem_rd_counter++;
    break;

// NOTE Did not add support for flush yet
  }

}

}





