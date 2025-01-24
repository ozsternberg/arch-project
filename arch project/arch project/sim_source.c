#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void progress_register_data(register_s* reg) {
	reg->q = reg->d;
}


// Get the tag,set,offset from an address
cache_addr_s parse_addr(int addr) 
{
	cache_addr_s cache_addr;
	int tag_shift = SET_WIDTH + OFFSET_WIDTH;
	int set_shift = OFFSET_WIDTH;
	cache_addr.tag =    (unsigned int)((addr & 0x00FFF00) >> tag_shift);
	cache_addr.set =    (unsigned int)(addr & 0xFF        >> set_shift);
	cache_addr.offset = (unsigned int)(addr & 0x3);
	return cache_addr;
}

// Round robin arbitrator implementation
int round_robin_arbitrator()
{
	static int curr = 0; //NOTE: This sets the first core to be core0, might need to change this for the first core that initiate a bus transaction
	int curr_r = curr;
	if (curr == 3) curr = 0; //Wrap
	else curr++;
	return curr_r;
}

bus_cmd_s cores(bus_cmd_s bus_req, int priority_for_gnt, int gnt, int gnt_core_id, int progress_clock)
{
	int core_issued_flush = 0;
	bus_cmd_s core_cmd;
	bus_cmd_s core_cmd_rtr;

	if (priority_for_gnt == 1) bus_req = core(gnt_core_id, gnt, bus_req, progress_clock); // If gnt we give priority to the selected core
	core_cmd_rtr = bus_req;

	for (int core_id = 0; core_id < NUM_CORES; core_id++) 
    {
		if (core_id == gnt_core_id && priority_for_gnt == 1) continue;
		
		core_cmd = core(core_id,0,bus_req,progress_clock);
		if (core_cmd == NULL) perror("Error - core returned NULL\n");

    	if (core_cmd.bus_cmd == kFlush && priority_for_gnt == 0)  // We rely on cores that have modifed data to flush on read - that is the only thing we care about   
		{
			if (core_issued_flush) puts("Error - two cores flushed on the same time!\n");
			core_issued_flush = 1;
			core_cmd_rtr = core_cmd;
		}
		else if  (core_cmd.bus_cmd == kFlush && priority_for_gnt == 1) printf("Error - core #%d issued flush while core #%d issued a req on its turn!\n", core_id, gnt_core_id); // For debugging purposes 
	}
	return core_cmd_rtr;
}

void load_mem_files(int mem_files[NUM_CORES][MEM_FILE_SIZE]) {
  char filename[50];
  FILE *file;
  for (int i = 0; i < NUM_CORES; i++) {
    snprintf(filename, sizeof(filename), "core%d.mem", i);
    file = fopen(filename, "r");
    if (file == NULL) {
      printf("Error opening file %s\n", filename);
      exit(1);
    }
    for (int j = 0; j < MEM_FILE_SIZE; j++) {
      if (fscanf(file, "%d", &mem_files[i][j]) != 1) {
        printf("Error reading data from file %s\n", filename);
        fclose(file);
        exit(1);
      }
    }
    fclose(file);
  }
}


void check_input_files(int argc, char *argv[], const char *input_files[], int input_files_count) {
  for (int i = 0; i < input_files_count; i++) {
    int found = 0;
    for (int j = 1; j < argc; j++) {
      if (strcmp(argv[j], input_files[i]) == 0) {
        found = 1;
        break;
      }
    }
    if (!found) {
      fprintf(stderr, "Error: Input file %s not provided\n", input_files[i]);
      exit(EXIT_FAILURE);
    }
  }
}

void create_output_files(int argc, char *argv[], const char *output_files[], int output_files_count) {
  for (int i = 0; i < output_files_count; i++) {
    int found = 0;
    for (int j = 1; j < argc; j++) {
      if (strcmp(argv[j], output_files[i]) == 0) {
        found = 1;
        break;
      }
    }
    if (!found) {
      FILE *file = fopen(output_files[i], "w");
      if (!file) {
        fprintf(stderr, "Error: Could not create output file %s\n", output_files[i]);
        exit(EXIT_FAILURE);
      }
      fclose(file);
    }
  }
}

