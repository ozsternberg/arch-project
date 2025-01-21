#include "sim.h"


void main()
{
// Define main mem
static int  main_mem[MAIN_MEM_DEPTH];
bus_state_t bus_state = kBusAvailable;
bus_cmd_s   bus_req = {0};
static int         mem_wait_counter = 0;
static int         mem_rd_counter   = 0;
static int shared;

// Various flags
static int progress_clock = 0;
static int gnt = 0;
static bus_origid_t flushing_core_id;

while (1)
{

switch (bus_state)
{
  case kBusAvailable:
    gnt = 1;
    progress_clock = 1;

    int gnt_core_id = round_robin_arbitrator;

    // Check all the cores
    bus_req = cores(bus_req, gnt ,gnt_core_id, progress_clock);

    if (bus_req.req_enable == 0) break; // If the current core does not have a req we move to the next one

    if (bus_req.bus_cmd == kBusRd || bus_req.bus_cmd == kBusRdX)  // We wait for flush from either another core or the memory
    {
      bus_state = kBusWaitMem;
      mem_wait_counter = 0;
      gnt = 0;
      shared = bus_req.bus_share;
    }

    else if (bus_req.bus_cmd == kFlush)
    {
      bus_state = kBusWaitFlush;
      main_mem[bus_req.bus_addr] = bus_req.bus_data;
      mem_rd_counter = 1;
      flushing_core_id = bus_req.bus_origid;
    }
    break;

  case kBusWaitMem:
    progress_clock = 0;
    gnt = 0;

    // Check for flush without progressing the clock and keeping the previous bus req safe
    bus_cmd_s core_cmd = cores(bus_req, gnt, gnt_core_id, progress_clock);

    // We listen to flush even without a gnt
    if (core_cmd.bus_cmd == kFlush) // If we see another flush from core while waiting we update the
    {
      if (core_cmd.bus_addr != bus_req.bus_addr) puts("Error: Flush has been issued from core to an unread addr\n");

      gnt = 1;
      progress_clock = 1;

      // Progress the cores and set flushing core as the one with the gnt, also update the bus req
      bus_req = cores(bus_req, gnt, core_cmd.bus_origid, progress_clock);

      if ((int)&bus_req != (int)&core_cmd) printf("bus req and core cmd are not he same!\n"); // Sanity check to see that we get the expected bus cmd

      bus_state = kBusWaitFlush; // We wait for all the new data to come from the flushing core
      main_mem[bus_req.bus_addr] = core_cmd.bus_data; //The write to the memory takes place on the same time as the flush arrives
      mem_rd_counter = 1;

      flushing_core_id = bus_req.bus_origid;

      break;
    }


    if (mem_wait_counter == MEM_RD_LATENCY - 1) // Also including zero
    {
      bus_state = kBusRead;
      mem_wait_counter = 0;
      mem_rd_counter   = 0;
      bus_req.bus_cmd  = kFlush; //After read req return the data with flush
    }

    else mem_wait_counter++;
    break;

  case kBusRead:
    bus_req.bus_origid = main_mem_id;
    bus_req.bus_data = main_mem[bus_req.bus_addr];
    bus_req.bus_cmd = kFlush; // When returning data the command is flush
    bus_req.bus_share = shared;

    gnt = 0;
    progress_clock = 1;

    bus_cmd_s core_cmd = cores(bus_req, gnt ,gnt_core_id, progress_clock);
    if ((int)&bus_req != (int)&core_cmd) printf("Error - bus_req should not change when data returns from main mem!\n"); // bus_req should not change when data returns from main mem

    if (mem_rd_counter == BLOCK_SIZE - 1)
    {
      bus_state = kBusAvailable;
      mem_rd_counter = 0;
      shared = 0;
      break;
    }

    mem_rd_counter++;
    bus_req.bus_addr = bus_req.bus_addr + 1;
    break;

  case kBusWaitFlush:
    gnt = 1;
    progress_clock = 1;

    bus_req = cores(bus_req, gnt, flushing_core_id, progress_clock);

    if (flushing_core_id != bus_req.bus_origid) printf("Flush src changed through transaction, original = %d, new = %d\n", flushing_core_id, bus_req.bus_origid);

    if (core_cmd.bus_cmd != kFlush) puts("Flush was initiated by core but stopped mid way\n");

    // Update the main mem
    main_mem[core_cmd.bus_addr] = core_cmd.bus_data;

    if (mem_rd_counter == BLOCK_SIZE - 1)
    {
      bus_state = kBusAvailable;
      break;
    }
    mem_rd_counter++;
    break;
  }

}

}





