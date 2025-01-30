#include "sim_source.h"
#include <stdio.h>
#include <string.h>


int main(int argc, char *argv[]) {

  const char *input_files[]  = {"imem0.txt", "imem1.txt", "imem2.txt", "imem3.txt", "memin.txt"};
  const char *output_files[] = {
    "memout.txt", "regout0.txt", "regout1.txt", "regout2.txt", "regout3.txt",
    "core0trace.txt", "core1trace.txt", "core2trace.txt", "core3trace.txt",
    "bustrace.txt", "dsram0.txt", "dsram1.txt", "dsram2.txt", "dsram3.txt",
    "tsram0.txt", "tsram1.txt", "tsram2.txt", "tsram3.txt",
    "stats0.txt", "stats1.txt", "stats2.txt", "stats3.txt"
  };



  int input_files_count = sizeof(input_files) / sizeof(input_files[0]);
  int output_files_count = sizeof(output_files) / sizeof(output_files[0]);

  check_input_files(argc, argv, input_files, input_files_count);
  create_output_files(argc, argv, output_files, output_files_count);

  static unsigned int mem_files[NUM_CORES][MEM_FILE_SIZE] = {0};
  load_mem_files(mem_files, argv);

  // Define main mem
  static int main_mem[MAIN_MEM_DEPTH] = {0};
  load_main_mem(argv[5], main_mem);

  // Define various variables
  static bus_state_t bus_state = kBusAvailable;
  static bus_cmd_s bus_req = {0};
  static int mem_wait_counter = 0;
  static int mem_rd_counter = 0;
  static int shared;
  static int clk = 0;
  static int req_core = -1;

  FILE* bus_trace = fopen(output_files[9], "w"); // Open in write mode to clear contents
  if (bus_trace != NULL) {
      fclose(bus_trace);
  }
  else {
      printf("Error resetting %s", output_files[9]);
  }

  // Various flags
  static int progress_clock = 0;
  static int gnt = 0;
  static int priority = 0;
  static bus_origid_t flushing_core_id;
  static bus_cmd_s core_cmd;

  while (1) {
    switch (bus_state) {
      case kBusAvailable:
        gnt = 1;
        progress_clock = 1;
        priority = 1;
        int gnt_core_id = round_robin_arbitrator();

        printf("BUS | State: kBusAvailable, Gnt Core Id: %d, Clk: %d\n\n", gnt_core_id,clk);

        // Check all the cores
        bus_req = cores(bus_req, priority, gnt, gnt_core_id, progress_clock,clk,argc,argv,mem_files);

        if (bus_req.bus_cmd == kNoCmd) break; // If the current core does not have a req we move to the next one

        if (bus_req.bus_cmd == kBusRd || bus_req.bus_cmd == kBusRdX) { // We wait for flush from either another core or the memory
          bus_state = kBusWaitMem;
          req_core = bus_req.bus_origid;
          mem_wait_counter = 0;
          gnt = 0;
          shared = bus_req.bus_share;
        }
        else if (bus_req.bus_cmd == kFlush) {
            bus_state = kWaitCoreFlush;
            main_mem[bus_req.bus_addr] = bus_req.bus_data;
            mem_rd_counter = 1;
            flushing_core_id = bus_req.bus_origid;
        }
        break;

      case kBusWaitMem:
        progress_clock = 0;
        priority = 0;
        gnt = 0;
        bus_req.bus_cmd = 0;
        printf("BUS | State: kBusWaitMem, Req Core Id: %d, Waiting Counter: %d, Clk: %d\n\n", bus_req.bus_origid, mem_wait_counter,clk);

        if (bus_req.bus_cmd != kBusRd && bus_req.bus_cmd != kBusRdX)
        {
          printf("Error: Bus cmd is not Rd or RdX in bus state: KBusWaitMem, Clk: %d\n", clk);
        }
        // Check for flush without progressing the clock and keeping the previous bus req safe
        core_cmd = cores(bus_req, priority, gnt, gnt_core_id, progress_clock, clk,argc,argv,mem_files);

        // We listen to flush even without a gnt
        if (core_cmd.bus_cmd == kFlush) { // If we see another flush from core while waiting we update the
            if (core_cmd.bus_addr != bus_req.bus_addr)
            {
                printf("Error: Flush has been issued from core #%d to unread addr(dec): %d\n", core_cmd.bus_origid, core_cmd.bus_addr);
            }
          priority = 1;
          progress_clock = 1;

          printf("BUS | State: kBusWaitMem, Req Core: %d, Sending Core: %d, Address(dec): %d, Clk: %d\n\n", bus_req.bus_origid, core_cmd.bus_origid, bus_req.bus_addr,clk);

          // Progress the cores and set flushing core as the one with the gnt, also update the bus req
          bus_req = cores(bus_req, priority, gnt, core_cmd.bus_origid, progress_clock, clk,argc,argv,mem_files);

          if (memcmp(&bus_req, &core_cmd, sizeof(bus_cmd_s)) != 0) printf("bus req and core cmd are not the same!\n"); // Sanity check to see that we get the expected bus cmd

          bus_state = kWaitCoreFlush; // We wait for all the new data to come from the flushing core
          main_mem[bus_req.bus_addr] = core_cmd.bus_data; // The write to the memory takes place on the same time as the flush arrives
          mem_rd_counter = 1;

          flushing_core_id = bus_req.bus_origid;
          mem_wait_counter = 0;
          break;
        }

        progress_clock = 1;
        bus_req = cores(bus_req, priority, gnt, 0, progress_clock, clk, argc, argv, mem_files);

        if (mem_wait_counter == MEM_RD_LATENCY - 1) { // Also including zero
          bus_state = kBusFlush;
          mem_wait_counter = 0;
          mem_rd_counter = 0;
        } else mem_wait_counter++;
        break;

      case kBusFlush:
        bus_req.bus_origid = main_mem_id;
        bus_req.bus_addr = (bus_req.bus_addr & 0xFFFFFFFC) + mem_rd_counter; // Align the address to the block size and increment
        bus_req.bus_data = main_mem[bus_req.bus_addr];
        bus_req.bus_cmd = kFlush; // When returning data the command is flush
        //bus_req.bus_share = shared; // Should be set without the bus changing it

        printf("BUS | State: kBusFlush, Req Core Id: %d, Send Counter: %d, Clk: %d\n\n", req_core, mem_rd_counter, clk);

        gnt = 0;
        progress_clock = 1;
        priority = 0;

        core_cmd = cores(bus_req, priority, gnt, gnt_core_id, progress_clock, clk,argc,argv,mem_files);
        if (memcmp(&bus_req, &core_cmd, sizeof(bus_cmd_s)) != 0)
        {
            printf("Error - bus_req should not change when data returns from main mem!\n"); // bus_req should not change when data returns from main mem
        }
        if (mem_rd_counter == BLOCK_SIZE - 1) {
          bus_state = kBusAvailable;
          mem_rd_counter = 0;
          shared = 0;
          break;
        }

        mem_rd_counter++;

        break;

      case kWaitCoreFlush:
        gnt = 0;
        progress_clock = 1;
        priority = 1;

        printf("BUS | State: kWaitCoreFlush, Req Core Id: %d, Send Core Id: %d, Send Counter: %d, Clk: %d\n\n", req_core, flushing_core_id ,mem_rd_counter, clk);

        bus_req = cores(bus_req, priority, gnt, flushing_core_id, progress_clock, clk,argc,argv,mem_files);

        if (flushing_core_id != bus_req.bus_origid) printf("Flush src changed through transaction, original = %d, new = %d\n", flushing_core_id, bus_req.bus_origid);

        if (core_cmd.bus_cmd != kFlush) puts("Flush was initiated by core but stopped mid way\n");

        // Update the main mem
        main_mem[bus_req.bus_addr] = bus_req.bus_data;

        if (mem_rd_counter == BLOCK_SIZE - 1) {
          bus_state = kBusAvailable;
          break;
        }
        mem_rd_counter++;
        break;
    }
    printf("BUS | Cmd: %s, OrigID: %d, Addr(Dec): %d, Data(Hex): %X, Share: %d, Clk: %d\n\n", get_bus_cmd_name(bus_req.bus_cmd), bus_req.bus_origid,bus_req.bus_addr, bus_req.bus_data, bus_req.bus_share,clk);
    append_bus_trace_line(output_files[9], clk, bus_req.bus_origid, bus_req.bus_cmd, bus_req.bus_addr, bus_req.bus_data, bus_req.bus_share);

    clk++;
    if (bus_req.bus_cmd == kHalt) break; // If halt is issued we break the loop and exit
  }

  // Close the files
  char memout_name[256] = {0};
  snprintf(memout_name, sizeof(memout_name), "%s", argc < 7 ? "memout.txt" : argv[6]);
  store_mem_to_file(memout_name, main_mem, MAIN_MEM_DEPTH);

  return 0;
}
