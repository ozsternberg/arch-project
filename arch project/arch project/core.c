#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "sim.h"

int core(int** mem, int core_id, int progress_clk, cache_state_t cache_state, bus_cmd_s bus, int gnt) {
	
	int static dsram[NUM_CORES][NUM_OF_BLOCKS][BLOCK_SIZE] = { 0 };
	tsram_entry static tsram[NUM_CORES][NUM_OF_BLOCKS] = { 0 };

	// Initialize the required _arrays and variables
	int static pc_arr[NUM_CORES] = { 0 };
	int static clk = 0;
	int static registers_arr[NUM_CORES][NUM_OF_REGS];
	int static busy_regs_arr[NUM_CORES][NUM_OF_REGS] = { 0 };
	register_line_s static fe_dec_arr[NUM_CORES];
	register_line_s static dec_ex_arr[NUM_CORES];
	register_line_s static ex_mem_arr[NUM_CORES];
	register_line_s static mem_wb_arr[NUM_CORES];

	int *pc = &pc_arr[core_id];
	int *registers = registers_arr[core_id];
	int *busy_regs = busy_regs_arr[core_id];
	register_line_s *fe_dec = &fe_dec_arr[core_id];
	register_line_s *dec_ex = &dec_ex_arr[core_id];
	register_line_s *ex_mem = &ex_mem_arr[core_id];
	register_line_s *mem_wb = &mem_wb_arr[core_id];

	if (pc > 0xFFF)
	{
		printf("pc is not in a valid range: %d, clk: %d\n", pc, clk);
		for (int i = 0; i < NUM_OF_REGS; i++)
		{
			printf("%d ", registers[i]);
		}
		puts("\n");
		printf("%05X\n", &mem[*pc]);
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

	fe_dec->data_d = &mem[*pc];
	int next_pc = pc + 1; // default
	printf("core: %x, FETCH pc: %x", core_id, pc);

	// --------------------- DECODE ---------------------------

	dec_ex->instrc_d = decode_line(fe_dec->data_q, registers);
	printf("core: %x, DECODE START opcode: %x, rd: %x, rs: %x, rt: %x, imm: %x", core_id, dec_ex->instrc_d.opcode, dec_ex->instrc_d.rd, dec_ex->instrc_d.rs, dec_ex->instrc_d.rt, dec_ex->instrc_d.imm);
	opcode opcode = dec_ex->instrc_d.opcode;

	if ((busy_regs[dec_ex->instrc_d.rs] & dec_ex->instrc_d.rs > 1) | 
		(busy_regs[dec_ex->instrc_d.rt] & dec_ex->instrc_d.rt > 1) & opcode != jal) { // check rs,rt are valid (if opcode is jal dont need rs,rt)
		dec_ex->instrc_d.opcode = stall; // inject stall to execute
		next_pc = pc; // decode the same instrc next clk
		fe_dec->data_d = fe_dec->data_q; // decode the same instrc next clk
		printf("core: %x, DECODE wait for rd or rs", core_id);
	};

	if (opcode == beq | opcode == bne | opcode == blt | opcode == bgt | opcode == bge | opcode == ble | opcode == ble) { // branch
		if (busy_regs[dec_ex->instrc_d.rd] & dec_ex->instrc_d.rd > 1) {
			dec_ex->instrc_d.opcode = stall; // inject stall to execute
			next_pc = pc; // decode the same instrc next clk
			fe_dec->data_d = fe_dec->data_q; // decode the same instrc next clk
		}
		else {
			// branch resoloution in decode stage (and handle sw)
			instrc instrct_d = dec_ex->instrc_d;
			switch (instrct_d.opcode) {
			case beq:
				if (registers[instrct_d.rs] == registers[instrct_d.rt]) {
					next_pc = (instrct_d.rd == 1) ? instrct_d.imm : registers[instrct_d.rd];
				};
			case bne:
				if (registers[instrct_d.rs] != registers[instrct_d.rt]) {
					next_pc = (instrct_d.rd == 1) ? instrct_d.imm : registers[instrct_d.rd];
				}
			case blt:
				if (registers[instrct_d.rs] < registers[instrct_d.rt]) {
					next_pc = (instrct_d.rd == 1) ? instrct_d.imm : registers[instrct_d.rd];
				}
			case bgt:
				if (registers[instrct_d.rs] > registers[instrct_d.rt]) {
					next_pc = (instrct_d.rd == 1) ? instrct_d.imm : registers[instrct_d.rd];
				}
			case ble:
				if (registers[instrct_d.rs] <= registers[instrct_d.rt]) {
					next_pc = (instrct_d.rd == 1) ? instrct_d.imm : registers[instrct_d.rd];
				}
			case bge:
				if (registers[instrct_d.rs] >= registers[instrct_d.rt]) {
					next_pc = (instrct_d.rd == 1) ? instrct_d.imm : registers[instrct_d.rd];
				}
			default:
				printf("non branch opcode in branch resoulotion: %s", instrct_d.opcode);
				break;
			};
			printf("core: %x, DECODE branch resoulotion next pc: %x", core_id, next_pc);
		}
	}
	else {
		if (opcode != jal & opcode != sw) {
			busy_regs[dec_ex->instrc_d.rd] = 1;
		};
	}

	if (opcode == jal & busy_regs[dec_ex->instrc_d.rd] < 2) {
		next_pc = registers[dec_ex->instrc_d.rd];
		busy_regs[15] = 1;
	}

	printf("core: %x, DECODE END opcode: %x, rd: %x, rs: %x, rt: %x, imm: %x", core_id, dec_ex->instrc_d.opcode, dec_ex->instrc_d.rd, dec_ex->instrc_d.rs, dec_ex->instrc_d.rt, dec_ex->instrc_d.imm);
	// --------------------- EXECUTE ---------------------------

	printf("core: %x, EXECUTE START opcode: %x, rd: %x, rs: %x, rt: %x, imm: %x", core_id, ex_mem->instrc_d.opcode, ex_mem->instrc_d.rd, ex_mem->instrc_d.rs, ex_mem->instrc_d.rt, ex_mem->instrc_d.imm);
	ex_mem->data_d = execute_op(dec_ex->instrc_q, registers);
	ex_mem->instrc_d = dec_ex->instrc_q;
	printf("core: %x, EXECUTE END opcode: %x, rd: %x, rs: %x, rt: %x, imm: %x, res: %x", core_id, ex_mem->instrc_d.opcode, ex_mem->instrc_d.rd, ex_mem->instrc_d.rs, ex_mem->instrc_d.rt, ex_mem->instrc_d.imm, ex_mem->data_d);

	// -------------------- MEMORY -----------------------------
	int address = ex_mem->data_q;
	opcode = ex_mem->instrc_q.opcode;
	printf("core: %x, MEMORY START opcode: %x, rd: %x, rs: %x, rt: %x, imm: %x", core_id, mem_wb->instrc_d.opcode, mem_wb->instrc_d.rd, mem_wb->instrc_d.rs, mem_wb->instrc_d.rt, mem_wb->instrc_d.imm);

	if ((busy_regs[ex_mem->instrc_q.rd] == 1) & (opcode == sw)) {
		mem_wb->instrc_d.opcode = stall;
		next_pc = pc; // decode the same instrc next clk
		fe_dec->data_d = fe_dec->data_q; // decode the same instrc next clk
		dec_ex->instrc_d = dec_ex->instrc_q; // execute the same instrc next clk
		ex_mem->instrc_d = ex_mem->instrc_q; // handle memory to the same instrc next clk
		printf("core: %x MEMORY sw wait for rd");
	}

	mem_rsp_s mem_rsp;
	mem_rsp = handle_mem(&tsram[core_id], &dsram[core_id], address, ex_mem->instrc_q.opcode, ex_mem->instrc_q.rd, progress_clk, cache_state, bus, gnt); // Need to provide the data from ex stage
	mem_wb->instrc_d = ex_mem->instrc_q;
	mem_wb->data_d = mem_rsp.data;
	if (mem_rsp.stall == 1) {
		mem_wb->instrc_d.opcode = stall;
		next_pc = pc; // decode the same instrc next clk
		fe_dec->data_d = fe_dec->data_q; // decode the same instrc next clk
		dec_ex->instrc_d = dec_ex->instrc_q; // execute the same instrc next clk
		ex_mem->instrc_d = ex_mem->instrc_q; // handle memory to the same instrc next clk
	}
	printf("core: %x, MEMORY END opcode: %x, rd: %x, rs: %x, rt: %x, imm: %x", core_id, mem_wb->instrc_d.opcode, mem_wb->instrc_d.rd, mem_wb->instrc_d.rs, mem_wb->instrc_d.rt, mem_wb->instrc_d.imm);

	// ---------------------- WRITE BACK -----------------------

	int opcode = mem_wb->instrc_q.opcode;
	if (opcode != stall & opcode != beq & opcode == bne & opcode != blt & opcode != bgt & opcode != bge & opcode != ble & opcode != ble & opcode != sw) { // no branch cmd or sw or stall
		registers[mem_wb->instrc_q.rd] = mem_wb->data_q;
		busy_regs[mem_wb->instrc_q.rd] = 0;
		printf("core: %x, WB address: %x, data: %x", core_id, mem_wb->instrc_q.rd, mem_wb->data_q);

	}
	if (opcode == jal) {
		registers[15] = pc + 1;
		busy_regs[15] = 0;
	}
	
	// ---------------------------------------------------------

	if (progress_clk == 1) {
		pc = next_pc;
		clk++;
		fe_dec->instrc_q = fe_dec->instrc_d;
		dec_ex->instrc_q = dec_ex->instrc_d;
		ex_mem->instrc_q = ex_mem->instrc_d;
		mem_wb->instrc_q = mem_wb->instrc_d;
		*registers_arr[core_id] = registers;
		*busy_regs_arr[core_id] = busy_regs;
	}

	return mem_wb->data_d;
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
	new_instrc.imm = get_signed_imm(imm);

	// Set a flag if imm is expected to be used
	if (new_instrc.rd == 1 || new_instrc.rs == 1 || new_instrc.rt == 1) {
		new_instrc.is_i_type = 1;
	};
	return  new_instrc;
}

int execute_op(const instrc instrc, int registers[])
{
	// NOTE: This function assumes that $imm has been loaded with the appropriate value
	int* rd = (instrc.rd  == 1) ? instrc.imm : &registers[instrc.rd];
	int* rs = (instrc.rd == 1) ? instrc.imm : &registers[instrc.rs];
	int* rt = (instrc.rd == 1) ? instrc.imm : &registers[instrc.rt];

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
	case stall:
		return NULL;
	default:
		printf("Unknown opcode: %d\n", instrc.opcode);
		return NULL;
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

/*
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
*/

