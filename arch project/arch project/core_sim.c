#include "core_sim.h"
#include "sim.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


instrc create_instrc(const int line_dec) {
	instrc new_instrc = {
		fail, // Opcode
		-1,	  // rd 
		-1,	  // rs
		-1,	  // rt
		-1,	  // imm
		false // is i type
	};

	if ((unsigned int)line_dec > 0xFFFFF) {
		puts("Line is corrupted!\n");
		return new_instrc;
	}

	int opcode_int = ((unsigned)(0xFF000 & line_dec) >> 12 );
	int rd	   = (0x00F00 & line_dec) >> 8;
	int rs	   = (0x000F0 & line_dec) >> 4;
	int rt	   = (0x0000F & line_dec);

	new_instrc.opcode = (opcode)opcode_int;
	new_instrc.rd = rd; 
	new_instrc.rs = rs;
	new_instrc.rt = rt;

	// Set a flag if imm is expected to be used 
	if (new_instrc.rd == 1 || new_instrc.rs == 1 || new_instrc.rt == 1) new_instrc.is_i_type = true;	

	return  new_instrc;

}

const char* get_io_register_name(int reg_number) {
	switch (reg_number) {
	case 0:  return "irq0enable";
	case 1:  return "irq1enable";
	case 2:  return "irq2enable";
	case 3:  return "irq0status";
	case 4:  return "irq1status";
	case 5:  return "irq2status";
	case 6:  return "irqhandler";
	case 7:  return "irqreturn";
	case 8:  return "clks";
	case 9:  return "leds";
	case 10: return "display7seg";
	case 11: return "timerenable";
	case 12: return "timercurrent";
	case 13: return "timermax";
	case 14: return "diskcmd";
	case 15: return "disksector";
	case 16: return "diskbuffer";
	case 17: return "diskstatus";
	case 18: case 19: return "reserved";
	case 20: return "monitoraddr";
	case 21: return "monitordata";
	case 22: return "monitorcmd";
	default: return "unknown";
	}
}


int perform_op(const instrc instrc, int registers[], int mem[], int hw_registers[], int* pc, int* in_irq, bool debug_on)
{
	// NOTE: This function assumes that $imm has been loaded with the appropriate value
	int* rd = &registers[instrc.rd];
	int* rs = &registers[instrc.rs];
	int* rt = &registers[instrc.rt];
	int  dummy_temp = registers[instrc.rd]; // Set a dummy variable so if rd points to $imm/$zero they will keep their value
	int  masked_val;

	// If the dest reg is $zero or $imm, we will move the pointer to a garbage var
	if (instrc.rd < 2) rd = &dummy_temp;

	switch (instrc.opcode)
	{
	case add:
		*rd = *rs + *rt;
		break;

	case sub:
		*rd = *rs - *rt;
		break;

	case mul:
		*rd = *rs * *rt;
		break;

	case and:
		*rd = *rs & *rt;
		break;

	case or:
		*rd = *rs | *rt;
		break;

	case xor:
		*rd = *rs ^ *rt;
		break;

	case sll:
		*rd = *rs << *rt;
		break;

	case sra:
		*rd = *rs >> *rt; //Arithmetic shift with sign extension
		break;

	case srl:
		*rd = (unsigned int)*rs >> *rt; // Logical shit right
		break;

	case beq:
		if (*rs == *rt)
		{
			*pc = *rd;
			return 0;
		}
		break;

	case bne:
		if (*rs != *rt)
		{
			*pc = *rd;
			return 0;
		}
		break;

	case blt:
		if (*rs < *rt)
		{
			*pc = *rd;
			return 0;
		}
		break;

	case bgt:
		if (*rs > *rt)
		{
			if (debug_on)  printf("jump dest: %d\n", *rd);
			*pc = *rd;
			return 0;
		}
		break;

	case ble:
		if (*rs <= *rt)
		{
			*pc = *rd;
			return 0;
		}
		break;

	case bge:
		if (*rs >= *rt)
		{
			*pc = *rd;
			return 0;
		}
		break;

	case jal:
		*rd = *pc + 1;
		*pc = *rs;
		return 0;

	case lw:
		// Extract the memory entry
		masked_val = mem[*rs + *rt] & 0xFFFFF; // Set a mask for the lower 20 bits

	// Check if the sign bit (20th bit) is set
		if (masked_val & 0x80000)
		{
			// 0x80000 is the 20th bit mask
			// Sign extend the 20-bit value to 32 bits
			*rd = masked_val | ~0xFFFFF; // Set the upper bits to 1
		}
		else
		{
			// The value is positive, no need to change the upper bits
			*rd = masked_val;
		}

		if (debug_on) printf("loaded word is: %d \n", masked_val);

		break;

	case sw:
		mem[*rs + *rt] = *rd & 0xFFFFF; // Extract only the lower 20 bits
		if (debug_on) printf("stored word is: %d \n", mem[*rs + *rt]);
		break;

	case reti:
		*pc = hw_registers[7];
		*in_irq = 0;
		return 0;

	case in:
		*rd = hw_registers[*rs + *rt];
		break;

	case out:
		hw_registers[*rs + *rt] = *rd;
		break;

	case halt:
		return 1;  // Return 1 to get an exit code

	case fail:
		return -1; // Return -1 to indicate a failure

	default:
		printf("Unknown opcode: %d, At pc: %X \n", instrc.opcode, *pc);
		return -1;
		break;
	}

	(*pc)++;
	return 0; // Return 0 to continue
}

int get_signed_imm(const int imm) {
	int bit_mask = 0x00080000; // Mask where only the 19th bit is 1
	if (imm & bit_mask)
	{
		// If the sign bit (19th bit) is set, sign-extend to 32 bits
		int signed_imm = (0xFFF00000 | imm);
		return signed_imm;
	}

	// If the sign bit is not set, return the immediate value as it is
	int signed_imm = 0x000FFFFF & imm;
	return signed_imm;







	//char imm_hex[6];
	//sprintf_s(imm_hex, sizeof(imm_hex), "%05X", imm); // Cast the unsigned decimal to hex


	//long int  decimal_imm;
	//const int bit_width = mem_bit_width;

	// Convert the hex string to a long int assuming it's a signed number in 2's complement
	//decimal_imm = strtol(imm_hex, NULL, 16);

	// Define the sign bit position
	//long int sign_bit_mask = 1L << (bit_width - 1); // 2^19

	// Check if the sign bit is set (if the number is negative in 2's complement)
	//if (decimal_imm & sign_bit_mask) {
	//	 If the sign bit is set, convert to negative by subtracting 2^bit_width
	//	decimal_imm -= (1L << bit_width);
	//}

	//return decimal_imm;
}

int load_file_into_array(const char* filename, int array[], const int max_lines, const int is_hex) {
	FILE* file = fopen(filename, "r");
	if (!file) {
		printf("Error opening %s\n", filename);
		return -1;
	}

	int line_count = 0;
	char buffer[10]; 
	while (fgets(buffer, sizeof(buffer), file) && line_count < max_lines)
	{
		// Set a flag to indicate if the incoming string is hex or dec
		if (is_hex == 1) array[line_count] = get_signed_imm((int)(strtoul(buffer, NULL, 16)));

		else array[line_count] = (int)strtoul(buffer, NULL, 10);
		line_count++;
	}

	fclose(file);
	return line_count;
}

void update_mon(int frame_buffer[][monitor_pixel_width], const int pixel_value, const int monitoraddr)
{
	if (monitoraddr > 0xFFFF || monitoraddr < 0)
	{
		printf("Attempted pixel address write is not in the valid range: %X\n", monitoraddr);
		return;
	}

	// Parse the address to line and pixel offset 
	int line_addr  = (unsigned)(monitoraddr & 0xFF00) >> 8;
	int pixel_offset = monitoraddr & 0x00FF;

	frame_buffer[line_addr][pixel_offset] = pixel_value;

}

void transfer_data(int mem[], int disk[], const int write, const int sector_offset, const int mem_addr)
{
	int offset = 0;

	while (offset < sector_width) {
		int disk_index = sector_offset * sector_width + offset;
		int mem_index = mem_addr + offset;

		// Ensure array indices are within bounds
		if (disk_index >= hard_disk_width || mem_index >= mem_depth) 
		{
			printf("Error: Index out of bounds. disk_index: %d, mem_index: %d\n", disk_index, mem_index);
			return;
		}

		if (write == 2) 
		{
			disk[disk_index] = mem[mem_index];
		}
		else if (write == 1) 
		{
			mem[mem_index] = disk[disk_index];
		}

		offset++;
	}
}


void create_trace_line(const int mem[], const int pc, const int registers[], FILE * file_pntr)
{
	// Write the pc and instruction
	fprintf(file_pntr, "%03X %05X", pc, mem[pc]);

	// Write the registers
	for (int i = 0; i < num_registers; i++)
	{
		fprintf(file_pntr, " %08X", registers[i]);
	}

	// Write a new line indicator
	fprintf(file_pntr, "\n");
}

void create_hwtrace_line(const unsigned int clk, const instrc instrc, const int registers[], FILE* hwregtrace_pntr) {

	int hw_register_target = registers[instrc.rs] + registers[instrc.rt]; // Get the number of the read/write target
	char* io_register_name = get_io_register_name(hw_register_target);    // Get the HW register name

	// Check the opccode for in/out - otherwise do nothing
	if (instrc.opcode == 20) 
	{
		fprintf(hwregtrace_pntr, "%u WRITE %s %08X\n", clk, io_register_name, registers[instrc.rd]);
	}
	else if (instrc.opcode == 19) 
	{
		fprintf(hwregtrace_pntr, "%u READ %s %08X\n", clk, io_register_name, registers[instrc.rd]);
	}
}

int create_leds_line(const unsigned int clk, const int leds, const int previous_leds, FILE* log_file) {

	if (leds == previous_leds) return previous_leds; // Only log when the value of LEDs changes

	// Format the clk and LEDs values and write to file
	fprintf(log_file, "%u %08X\n" ,clk ,leds);

	#ifdef DEBUG_ON
		printf("Logged: CLK = %u, LEDs = %08X\n", clk, leds);
	#endif

	return leds;
}

void write_to_file(const int text[], FILE *file_pntr, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		uint32_t masked_value = text[i] & 0xFFFFF;
		fprintf(file_pntr, "%05X\n", masked_value);
	}
}

void write_to_monitor(const int frame_buffer[][monitor_pixel_width], FILE* file_pntr, bool binary_format)
{
	// Nested loops to go over all the array
	for (int row_index = 0; row_index < monitor_pixel_width; row_index++) {
		for (int col_index = 0; col_index < monitor_pixel_width; col_index++) {
			int masked_val = frame_buffer[row_index][col_index] & 0xFF;

			// Write to the file in the required formats
			if (binary_format) {
				// Write the pixel value in binary format
				if (fwrite(&masked_val, sizeof(uint8_t), 1, file_pntr) != 1) {
					printf("Error at writing to file (binary), row number: %d, col number: %d\n", row_index, col_index);
				}
			}
			else {
				fprintf(file_pntr, "%02X\n", masked_val);
			}
		}
	}
}

// Function for safe file handling
FILE* open_file(const char* filename, const char* mode) {
	FILE* file_pntr = fopen(filename, mode);
	if (!file_pntr) {
		printf("Error opening file: %s\n", filename);
		exit(EXIT_FAILURE);  // Terminate the program if the file can't be opened
	}
	return file_pntr;
}


void write_reg(int registers[], FILE* regout_pntr)
{
	for (int i = 2; i < num_registers; i++) {
		fprintf(regout_pntr, "%08X\n", registers[i]);
	}
}
bus_cmd_s bus;

// Define core
core_state_t core_state = Idle; 
int core_send_counter = 0;
int core_receive_counter = 0;
int core_req_trans = 0;
trans_state ;
static int progress_clock = 0;
int dsram[][];


 // need to handle address
 //need to handle synchronization
// check for Send and recieve done flags
// wrap procedure as a function

// Bus routine
switch (core_state){
	case Idle:
		
		if(gnt = 0)
		{	
			int bus_addr = bus.bus_addr;
			// Split bus address to tsram and dsram parameters
			int offset = parse_addr(bus_addr).offset;
			int index = parse_addr(bus_addr).set;
			int tag = parse_addr(bus_addr).tag;
			tsram_entry entry = tsram[index];
			core_state_t next_state = Idle;

			if(entry.tag = tag && entry.state =! Invalid) // Check if passive hit
			{
				if (bus.bus_cmd = kBusRdX || bus.bus_cmd = kBusRd) 
				{
					if(entry.state = Modified && progress_clock =1)  // If the data is modified- Send
					{ 
						core_send_counter = 0; 
						next_state = Send ; 
					}
				} 
				if (bus.bus_cmd = kBusRd) //set shared wire
				{
					bus.bus_share = 1 ;
					entry_state = Shared  ; // If command is kBusRd-change state to shared  - from all states(exclusive, modified or shared)
				}
				else if (bus.bus_cmd = kBusRdX)
				{
					entry_state = Invalid ; // If BusRdX change to Invalid  - from all states(exclusive, modified or shared)
				}
				if(progress_clock = 1) // change core state and entry state on next clock cycle
				{
					entry.state = entry_state;
					core_state = next_state;
				}
			}
			
			break;
		}
		if(gnt = 1)  // check for transaction 
		{
			bus.bus_origid = core_id; // Set bus_origid to core_id
			if(core_req_trans = 1) 
			{
				bus.bus_addr = addr; // save a copy of the trans address
				// Split bus address to tsram and dsram parameters
				int offset = parse_addr(bus.bus_addr).offset;
				int index = parse_addr(bus.bus_addr).set;
				int tag = parse_addr(bus.bus_addr).tag;
				tsram_entry entry = tsram[index];
				mesi_state_t entry_state = entry.state;
				int update_mem = dsram;
				
				if(state = kRdMiss)  // If read miss- set bus_cmd to BusRd
				{
					bus.bus_cmd = kBusRd;
					next_state = WaitForFlush; // A transaction is made, now wait for flash
					
				}
				if(state = kWrMiss) // If write miss - set bus_cmd to BusRdX
				{
					bus.bus_cmd = kBusRdX;
					next_state = WaitForFlush; // A transaction is made, now wait for flash
				}
				if (state = kWrHitShared) // If cache writeHit on shared data - set bus_cmd to kBusRdX to invalidate data on other caches
				{
					bus.bus_cmd = kBusRdX;
					entry_state = Modified;
					update_mem[index][offset] = data;
				}

				if (state = kModifiedMiss) // If cache writeMiss modified data - set bus_cmd to flush
				{
					bus.bus_cmd = kFlush;
					bus.bus_data = dsram[index][core_send_counter];
					next_state = Send;
					core_send_counter = 1;
				}
				else
				{
					printf("Transaction is made, but no transaction is required\n");	
				}
				if(progress_clock = 1)
				{
					entry.state = entry_state;
					core_state = next_state;
					entry.tag = tag;
					dsram[index][offset] = update_mem[index][offset];
				}
					
			}
			else
			{
				bus.bus_cmd = kNoCmd;
			}
			
			break;
				
		}
		
		break;

	case WaitForFlush:
	
		next_state = WaitForFlush;
		if(bus.bus_cmd = kFlush)
		{
			if(bus.bus_addr != (addr - offset))
			{
				printf("Data is Received from wrong address\n");
			}
			next_state = Receive;
			core_receive_counter = 0;
		}
		if(progress_clock = 1)
		{
			core_state = next_state;
		}
		break;
	
	case Send:

		next_state = Send;
		flush_done = 0; // Raise signal when flush is done
		bus.bus_origid = core_id ;
		bus.bus_cmd = kFlush;
		bus.bus_data = dsram[index][core_send_counter];
		bus.bus_addr = bus_addr + core_send_counter - offset;
		if(progress_clock = 1)
		{
			core_send_coumter++;
			if(core_send_counter == BLOCK_SIZE -1)
			{
				core_send_counter = 0;
				entry.state = Idle;
				flush_done = 1;
				entry.state = Shared;
				break;
			}	
			else
			{
				entry.state = next_state;
			}
		}
		
		break;
		
	case Receive:
		
		
		if(bus.bus_addr = (addr + core_receive_counter - offset) && progress_clock = 1)
		{
			receive _done = 0;
			dsram[index][core_receive_counter] = bus.bus_data;
			core_receive_counter++;

			if(core_receive_counter == BLOCK_SIZE -1)
			{
				if(bus.bus_share = 0)
				{
					entry.state = Exclusive; // If no other cache asserts shared ,set entry_state to exclusive
				}
				else
				{
					entry.state = Shared; // If another cache asserts shared set entry to shared
				}
				core_receive_counter = 0;
				receive _done = 1;
				next_state = Idle;
				break;
			}
		}
		
		break;
}



