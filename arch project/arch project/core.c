#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "sim.h"

int next_pc;

int core(int **mem, int core_id) {

	// Initialize the required _arrays and variables
	int static pc_arr[NUM_CORES] = { 0 };
	int static next_pc_arr[NUM_CORES] = { 1 };
	int static clk = 0;
	int static registers_arr[NUM_CORES][NUM_OF_REGS];
	register_s static fe_dec_arr[NUM_CORES];
	register_line_s static dec_ex_arr[NUM_CORES];
	register_s static ex_mem_arr[NUM_CORES];
	register_s static mem_wb_arr[NUM_CORES];

	int pc = pc_arr[core_id];
	int registers[NUM_OF_REGS] = registers_arr[core_id];
	register_s fe_dec = fe_dec_arr[core_id];
	register_line_s dec_ex = dec_ex_arr[core_id];
	register_s ex_mem = ex_mem_arr[core_id];
	register_s mem_wb = mem_wb_arr[core_id];

	if (pc > 0xFFF)
	{
		printf("pc is not in a valid range: %d, clk: %d\n", pc, clk);
		for (int i = 0; i < NUM_OF_REGS; i++)
		{
			printf("%d ", registers[i]);
		}
		puts("\n");
		printf("%05X\n", &mem[pc]);
		return EXIT_FAILURE;
	}

#ifdef TIMEOUT_ON
	if (*clk > 100000)
	{
		puts("Reached timeout\n");
		error_flag = true;
		break;
	}
#endif

	// -------------------- FETCH -----------------------------

	fe_dec.d = &mem[pc];

	// --------------------- DECODE ---------------------------

	dec_ex.d = decode_line(fe_dec.q, registers);
	// branch resoloution in decode stage 
	switch (dec_ex.d.opcode) {
	case beq:
		if (registers[dec_ex.d.rs] == registers[dec_ex.d.rt]) {
			next_pc = dec_ex.d.imm;
		};
	case bne:
		if (registers[dec_ex.d.rs] != registers[dec_ex.d.rt]) {
			next_pc = dec_ex.d.imm;
		}
	case blt:
		if (registers[dec_ex.d.rs] < registers[dec_ex.d.rt]) {
			next_pc = dec_ex.d.imm;
		}
	case bgt:
		if (registers[dec_ex.d.rs] > registers[dec_ex.d.rt]) {
			next_pc = dec_ex.d.imm;
		}
	case ble:
		if (registers[dec_ex.d.rs] <= registers[dec_ex.d.rt]) {
			next_pc = dec_ex.d.imm;
		}
	case bge:
		if (registers[dec_ex.d.rs] >= registers[dec_ex.d.rt]) {
			next_pc = dec_ex.d.imm;
		}
	case jal:
//		*rd = *pc + 1; // TODO: Need to move to the wb stage
		next_pc = registers[dec_ex.d.rs];
	}

	// --------------------- EXECUTE ---------------------------

	ex_mem.d = execute_op(dec_ex.q, registers);

	// -------------------- MEMORY -----------------------------


	mem_wb.d = handle_memory(ex_mem.q);

	// ---------------------- WRIRE BACK -----------------------

	write_reg(registers, mem_wb.q);
		
	// ---------------------------------------------------------
	pc++;
	clk++;
	fe_dec.q = fe_dec.d;
	dec_ex.q = dec_ex.d;
	ex_mem.q = ex_mem.d;
	mem_wb.q = mem_wb.d;

	return EXIT_SUCCESS;
};

instrc decode_line(const int line_dec, int registers[]) {
	instrc new_instrc = {
		stall, // Opcode
		-1,	  // rd 
		-1,	  // rs
		-1,	  // rt
		-1,	  // imm
		0 // is i type
	};

	if ((unsigned int)line_dec > 0xFFFFF) {
		puts("Line is corrupted!\n");
		return new_instrc;
	}

	int opcode_int = ((unsigned)(0xFF000000 & line_dec) >> 24);
	int rd = (0x00F00000 & line_dec) >> 20;
	int rs = (0x000F0000 & line_dec) >> 16;
	int rt = (0x0000F000 & line_dec) >> 12;
	int imm = (0x00000FFF & line_dec);

	new_instrc.opcode = (opcode)opcode_int;
	new_instrc.rd = rd;
	new_instrc.rs = rs;
	new_instrc.rt = rt;
	new_instrc.imm = imm;

	// Set a flag if imm is expected to be used 
	if (new_instrc.rd == 1 || new_instrc.rs == 1 || new_instrc.rt == 1) new_instrc.is_i_type = 1;
	return  new_instrc;
}

int execute_op(const instrc instrc, int registers[])
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
		return *rs + *rt;
	case sub:
		return *rs - *rt;
	case mul:
		return *rs * *rt;
	case and:
		return *rs & *rt;
	case or:
		return *rs | *rt;
	case xor :
		return *rs ^ *rt;
	case sll:
		return *rs << *rt;
	case sra:
		return *rs >> *rt; //Arithmetic shift with sign extension
	case srl:
		return (unsigned int)*rs >> *rt; // Logical shit right
	case lw:
		return *rs + *rt;
	case sw:
		return *rs + *rt;
	case halt:
		return -1;  // Return 1 to get an exit code
	default:
		printf("Unknown opcode: %d, At pc: %X \n", instrc.opcode, *pc);
		return -1;
		break;
	}
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

int load_file_into__array(const char* filename, int _array[], const int max_lines, const int is_hex) {
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
		if (is_hex == 1) _array[line_count] = get_signed_imm((int)(strtoul(buffer, NULL, 16)));

		else _array[line_count] = (int)strtoul(buffer, NULL, 10);
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
	int line_addr = (unsigned)(monitoraddr & 0xFF00) >> 8;
	int pixel_offset = monitoraddr & 0x00FF;

	frame_buffer[line_addr][pixel_offset] = pixel_value;

}

void transfer_data(int mem[], int disk[], const int write, const int sector_offset, const int mem_addr)
{
	int offset = 0;

	while (offset < sector_width) {
		int disk_index = sector_offset * sector_width + offset;
		int mem_index = mem_addr + offset;

		// Ensure _array indices are within bounds
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


void create_trace_line(const int mem[], const int pc, const int registers[], FILE* file_pntr)
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
	fprintf(log_file, "%u %08X\n", clk, leds);

#ifdef DEBUG_ON
	printf("Logged: CLK = %u, LEDs = %08X\n", clk, leds);
#endif

	return leds;
}

void write_to_file(const int text[], FILE* file_pntr, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		uint32_t masked_value = text[i] & 0xFFFFF;
		fprintf(file_pntr, "%05X\n", masked_value);
	}
}

void write_to_monitor(const int frame_buffer[][monitor_pixel_width], FILE* file_pntr, bool binary_format)
{
	// Nested loops to go over all the _array
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

