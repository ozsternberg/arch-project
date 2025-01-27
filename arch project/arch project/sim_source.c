#include "sim_source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Get the tag,set,offset from an address
cache_addr_s parse_addr(int addr)
{
	cache_addr_s cache_addr;
	int tag_shift = SET_WIDTH + OFFSET_WIDTH;
	int set_shift = OFFSET_WIDTH;
	cache_addr.tag =    (unsigned int)((addr & 0x000FFF00) >> tag_shift);
	cache_addr.set =    (unsigned int)((addr & 0x000000FF) >> set_shift);
	cache_addr.offset = (unsigned int)(addr & 0x00000003);
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

bus_cmd_s cores(bus_cmd_s bus_req, int priority_for_gnt, int gnt, int gnt_core_id, int progress_clock, int clk,int argc, char *argv[], int mem[NUM_CORES][MEM_FILE_SIZE])
{
	int core_issued_flush = 0;
	bus_cmd_s core_cmd;
	bus_cmd_s core_cmd_rtr;

	if (priority_for_gnt == 1) bus_req = core(gnt_core_id, gnt, bus_req, progress_clock, clk,argc,argv,mem); // If gnt we give priority to the selected core
	core_cmd_rtr = bus_req;

	if (core_cmd_rtr.bus_cmd == kHalt) return core_cmd_rtr; // If halt is issued we return the bus_req

	for (int core_id = 0; core_id < NUM_CORES; core_id++)
    {
		if (core_id == gnt_core_id && priority_for_gnt == 1) continue;

		core_cmd = core(core_id,0,bus_req,progress_clock,clk,argc,argv,mem);
		if (core_cmd.bus_cmd == kHalt) return core_cmd; // If halt is issued we return the bus_req

		// if (core_cmd == ) perror("Error - core returned NULL\n");

    	if (core_cmd.bus_cmd == kFlush && priority_for_gnt == 0 && core_cmd.bus_origid != main_mem_id)  // We rely on cores that have modifed data to flush on read - that is the only thing we care about
		{
			if (core_issued_flush == 1)
			{
				puts("Error - two cores flushed on the same time!\n");
			}

			if (bus_req.bus_origid == main_mem_id)
			{
				printf("Core #%d tried to flush while the main mem was flushing!\n", core_id);
			}
			core_issued_flush = 1;
			core_cmd_rtr = core_cmd;
		}
		else if  (core_cmd.bus_cmd == kFlush && (gnt == 1 || priority_for_gnt == 1)) printf("Error - core #%d issued flush while core #%d issued a req on its turn!\n", core_id, gnt_core_id); // For debugging purposes
	}
	return core_cmd_rtr;
}

void load_mem_files(unsigned int mem_files[NUM_CORES][MEM_FILE_SIZE],  char *file_names[]) {
	FILE *file;
	char buffer[100];
	for (int i = 0; i < NUM_CORES; i++) {
		if (file_names[i+1] == NULL) continue; // Skip if no file name is provided for this core
		if (fopen_s(&file, file_names[i+1], "r") != 0) {
			fprintf_s(stderr, "Error opening file %s\n", file_names[i+1]);
			exit(1);
		}
#ifdef DEBUG_ON
		printf("\nReading file %s\n", file_names[i+1]);
#endif
		for (int j = 0; j < MEM_FILE_SIZE; j++) {
			if (fgets(buffer, sizeof(buffer), file) == NULL) {
				if (feof(file)) {
					break; // End of file reached, break the loop
				} else {
					fprintf_s(stderr, "Error reading data from file %s\n", file_names[i+1]);
					fclose(file);
					exit(1);
				}
			}
			mem_files[i][j] = (unsigned int)strtoul(buffer, NULL, 16); // Convert hex string to int
		}
		fclose(file);
	}
}

void load_main_mem(const char *file_name, int lines[MAIN_MEM_DEPTH]) {
	FILE *file;
	if (fopen_s(&file, file_name, "r") != 0) {
		fprintf_s(stderr, "Error opening file %s\n", file_name);
		exit(1);
	}
	if (file == NULL) {
		fprintf(stderr, "Error opening file %s\n", file_name);
		exit(1);
	}

	char buffer[100];
	int line_count = 0;
	while (fgets(buffer, sizeof(buffer), file) != NULL && line_count < MAIN_MEM_DEPTH) {
		lines[line_count] = (int)strtol(buffer, NULL, 16); // Convert hex string to int
		line_count++;
	}

	fclose(file);
}

void store_mem_to_file(const char *file_name, int mem_array[],int mem_array_size) {
	FILE *file;
	if (fopen_s(&file, file_name, "w") != 0) {
		fprintf_s(stderr, "Error opening file %s for writing\n", file_name);
		exit(1);
	}
	if (file == NULL) {
		fprintf(stderr, "Error opening file %s for writing\n", file_name);
		exit(1);
	}

	for (int i = 0; i < mem_array_size; i++) {
		fprintf(file, "%08X\n", mem_array[i]); // Write each int as an 8-digit hex number
	}

	fclose(file);
}

void check_input_files(int argc, char *argv[], const char *input_files[], int input_files_count) {
	if (argc - 1 < input_files_count) {
		fprintf(stderr, "Error: Number of input files does not match the requirement. Expected %d but got %d\n", input_files_count, argc - 1);
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < input_files_count; i++) {
		int found = 0;
		for (int j = 1; j < argc; j++) {
			if (strcmp(argv[j], input_files[i]) == 0) {
				found = 1;
				break;
			}
		}
		if (!found) {
			printf("Warning: Input file name %s does not match. Using %s instead.\n", input_files[i], argv[i + 1]);
		}
	}
}

const char **create_output_files(int argc, char *argv[], const char *output_files[], int output_files_count) {
	const char **file_names = malloc(output_files_count * sizeof(char *));
	if (file_names == NULL) {
		fprintf(stderr, "Error: Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < output_files_count; i++) {
		if (i + 1 < argc) {
			file_names[i] = argv[i + 1];
		} else {
			file_names[i] = output_files[i];
		}
	}

	// Ensure to free the allocated memory after use
	// atexit(free_file_names);

	return file_names;
}

// void free_file_names(const char **file_names) {
// 	free(file_names);
// }

void store_dsram_to_file(int core_id, int array[NUM_OF_BLOCKS][BLOCK_SIZE]) {
    char file_name[20];
	snprintf(file_name, sizeof(file_name), "dsram%d.txt", core_id);

	FILE *file;
	if (fopen_s(&file, file_name, "w") != 0) {
		fprintf(stderr, "Error opening file %s for writing\n", file_name);
		exit(1);
	}
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s for writing\n", file_name);
        exit(1);
    }

    for (int i = 0; i < NUM_OF_BLOCKS; i++) {
        for (int j = 0; j < BLOCK_SIZE; j++) {
            fprintf(file, "%08X\n", array[i][j]); // Write each int as an 8-digit hex number
        }
    }

    fclose(file);
}

void store_tsram_to_file(int core_id, tsram_entry tsram[NUM_OF_BLOCKS]) {
    char file_name[20];
	snprintf(file_name, sizeof(file_name),"tsram%d.txt", core_id);

	FILE *file;
	if (fopen_s(&file, file_name, "w") != 0) {
		fprintf(stderr, "Error opening file %s for writing\n", file_name);
		exit(1);
	}
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s for writing\n", file_name);
        exit(1);
    }

    for (int i = 0; i < NUM_OF_BLOCKS; i++) {
		unsigned int combined = (tsram[i].state << 24) | (tsram[i].tag & 0xFFFFFF);
		fprintf(file, "%08X\n", combined); // Write combined state and tag as an 8-digit hex number
    }

    fclose(file);
}

const char* get_bus_cmd_name(bus_cmd_t cmd) {
	switch (cmd) {
		case kNoCmd:
			return "No Command";
		case kBusRd:
			return "BusRd";
		case kBusRdX:
			return "BusRdX";
		case kFlush:
			return "Flush";
		case kHalt:
			return "Halt";
		default:
			return "Unknown";
	}
}