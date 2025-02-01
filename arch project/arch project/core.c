#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "core_source.h"
#include <string.h>
#include "sim_source.h"

bus_cmd_s core(int core_id, int gnt, bus_cmd_s bus_cmd, int progress_clk, int clk, const char *output_files[], unsigned int mem[NUM_CORES][MEM_FILE_SIZE]) {

	static int  dsram[NUM_CORES][NUM_OF_BLOCKS][BLOCK_SIZE] = { 0 };
	static tsram_entry  tsram[NUM_CORES][NUM_OF_BLOCKS] = { 0 };
	static cache_state_t cache_state[NUM_CORES];
	static core_state_t  core_state[NUM_CORES];


	static FILE* trace_files[NUM_CORES] = { 0 };
	if (trace_files[0] == NULL) {
		FILE** temp_files = create_trace_files(output_files);
		for (int i = 0; i < NUM_CORES; i++) {
			trace_files[i] = temp_files[i];
		}
	}

	static int error_flag = 0;
	static int halt_core[NUM_CORES] = { 0 };
	static int halt_in_fetch[NUM_CORES] = { 0 };
	static int busy_reg_before = 0;
	// Initialize the required _arrays and variables
	static int pc_arr[NUM_CORES] = { 0 };
	static int registers_arr[NUM_CORES][NUM_OF_REGS];
	static int busy_regs_arr[NUM_CORES][NUM_OF_REGS] = { 0 };

	static int total_inst[NUM_CORES] = { 0 };
	static int total_rhit[NUM_CORES] = { 0 };
	static int total_whit[NUM_CORES] = { 0 };
	static int total_rmis[NUM_CORES] = { 0 };
	static int total_wmis[NUM_CORES] = { 0 };
	static int total_dec_stall[NUM_CORES] = { 0 };
	static int total_mem_stall[NUM_CORES] = { 0 };


	static register_line_s fe_dec_arr[NUM_CORES] = {{{-1},{-1},0,0},{{-1},{-1},0,0},{{-1},{-1},0,0},{{-1},{-1},0,0}};
	static register_line_s dec_ex_arr[NUM_CORES] = {{{-1},{-1},0,0},{{-1},{-1},0,0},{{-1},{-1},0,0},{{-1},{-1},0,0}};
	static register_line_s ex_mem_arr[NUM_CORES] = {{{-1},{-1},0,0},{{-1},{-1},0,0},{{-1},{-1},0,0},{{-1},{-1},0,0}};
	static register_line_s mem_wb_arr[NUM_CORES] = {{{-1},{-1},0,0},{{-1},{-1},0,0},{{-1},{-1},0,0},{{-1},{-1},0,0}};

	int *pc = &pc_arr[core_id];

	int registers[NUM_OF_REGS] = {0};
	// registers_arr[core_id]; //TODO REMOVE
	memcpy(registers, registers_arr[core_id], sizeof(registers));

	// int busy_regs[NUM_OF_REGS] = busy_regs_arr[core_id]; //TODO REMOVE
	int busy_regs[NUM_OF_REGS] = { 0 };
	memcpy(busy_regs, busy_regs_arr[core_id], sizeof(busy_regs));

	register_line_s *fe_dec = &fe_dec_arr[core_id];
	register_line_s *dec_ex = &dec_ex_arr[core_id];
	register_line_s *ex_mem = &ex_mem_arr[core_id];
	register_line_s *mem_wb = &mem_wb_arr[core_id];
	if (*pc > 0x00000FFF)
	{
		printf("pc is not in a valid range: %d, clk: %d\n", *pc, clk);
		for (int i = 0; i < NUM_OF_REGS; i++)
		{
			printf("Register[%d]: %d\n ", i, registers[i]);
		}
		puts("\n");
		printf("%05X\n", mem[core_id][*pc]);
		bus_cmd_s empty_bus_cmd = {0};
		return empty_bus_cmd;
	}

	#ifdef TIMEOUT_ON
	if (clk > 2000)
	{
		puts("Reached timeout\n");
		error_flag = 1;
	}
	#endif

	// -------------------- FETCH -----------------------------

	fe_dec->data_d = mem[core_id][*pc];
	fe_dec->pc_d = *pc;
	fe_dec->instrc_d.pc = *pc;

	int next_pc = *pc + 1; // default
	#ifdef DEBUG_ON
	printf("Core: %x, FETCH pc: %x\n", core_id, *pc);
	#endif

	// --------------------- DECODE ---------------------------
	int dec_set_busy = 0;
	if (clk > 0) dec_ex->instrc_d = decode_line(fe_dec->data_q, registers,fe_dec->pc_q);
	dec_ex->pc_d = fe_dec->pc_q;

	#ifdef DEBUG_ON
	printf("Core: %x, DECODE START opcode: %s, rd: %x, rs: %x, rt: %x, imm: %x, pc: %x\n", core_id, opcode_to_string(dec_ex->instrc_d.opcode), dec_ex->instrc_d.rd, dec_ex->instrc_d.rs, dec_ex->instrc_d.rt, dec_ex->instrc_d.imm, dec_ex->instrc_d.pc);
	#endif
	opcode_t opcode = dec_ex->instrc_d.opcode;

	if(dec_ex->instrc_d.opcode == halt)
	{
		halt_in_fetch[core_id] = 1;
	}

	busy_reg_before = busy_regs[dec_ex->instrc_d.rd];

	//==============================================================
	//IMPORTANT NOTE: We assume that the regs can only be read at decode stage so we need to check that rd is not busy for sw op
	//==============================================================

	if ((busy_regs[dec_ex->instrc_d.rs] == 1 || busy_regs[dec_ex->instrc_d.rt] == 1) && opcode != jal) // Check rs,rt are valid (if opcode is jal don't need rs,rt)
	{

		dec_ex->instrc_d.opcode = stall; // inject stall to execute
		dec_ex->pc_d = -1;

		next_pc = *pc; // decode the same instrc next clk

		stall_reg(fe_dec); // decode the same instrc next clk
		if (progress_clk == 1) total_dec_stall[core_id]++;
		//fe_dec->data_d = fe_dec->data_q;
		//fe_dec->pc_d = fe_dec->pc_q;
		#ifdef DEBUG_ON
		printf("core: %x, DECODE wait for rd or rs\n", core_id);
		#endif
	}

	// If Rs and Rt are valid we can proceed to check if branch resolution is needed
	else if (opcode == beq || opcode == bne || opcode == blt || opcode == bgt || opcode == bge || opcode == ble || opcode == ble || opcode == sw) { // branch

		if (busy_regs[dec_ex->instrc_d.rd])  // For branch resolution we need rd
		{
#ifdef DEBUG_ON
			printf("Op %s wait for rd: %d\n",opcode_to_string(dec_ex->instrc_d.opcode), dec_ex->instrc_d.rd);
#endif
			dec_ex->instrc_d.opcode = stall; // inject stall to execute
			dec_ex->pc_d = dec_ex->pc_q;

			next_pc = *pc;     // decode the same instrc next clk
			stall_reg(fe_dec); // decode the same instrc next clk
			if (progress_clk == 1) total_dec_stall[core_id]++;
		}
		else {
			// branch resolution in decode stage (and handle sw)
			instrc instrct_d = dec_ex->instrc_d;
			int rs_val = get_reg_val(instrct_d.rs, registers, instrct_d.imm);
			int rt_val = get_reg_val(instrct_d.rt, registers, instrct_d.imm);
			int rd_val = get_reg_val(instrct_d.rd, registers, instrct_d.imm);
			switch (instrct_d.opcode) {
			case beq:
				if (rs_val == rt_val) {
					next_pc = rd_val;
				};
				break;
			case bne:
				if (rs_val != rt_val) {
					next_pc = rd_val;
				}
				break;
			case blt:
				if (rs_val < rt_val) {
					next_pc = rd_val;
				}
				break;
			case bgt:
				if (rs_val > rt_val) {
					next_pc = rd_val;
				}
				break;
			case ble:
				if (rs_val <= rt_val) {
					next_pc = rd_val;
				}
				break;
			case bge:
				if (rs_val >= rt_val) {
					next_pc = rd_val;
				}
				break;

			case sw: // If op is sw we set no reg to busy
				break;

			default:
				#ifdef DEBUG_ON
				printf("non branch opcode in branch resolution: %d\n", instrct_d.opcode);
				#endif
				break;
			};
			#ifdef DEBUG_ON
			printf("core: %x, DECODE branch resolution next pc: %x\n", core_id, next_pc);
			#endif
		}
	}
	// If the opcode is jal we only need to check if rd is busy
	else if (opcode == jal && (busy_regs[dec_ex->instrc_d.rd] == 0))
	{
		next_pc = dec_ex->instrc_d.rd == 1 ? dec_ex->instrc_d.imm : registers[dec_ex->instrc_d.rd]; // Set the next pc to the value of the register or imm
		busy_regs[15] = 1; // Set Ra to busy
	}

	// If all Registers are ready and the op is not jal, branch, or sw we set rd to busy
	else if (opcode != sw)
	{
		dec_set_busy = 1;
		busy_regs[dec_ex->instrc_d.rd] = dec_ex->instrc_d.rd > 1 ? 1 : 0;
	}

	else {
		puts("Something went wrong a sw op got here\n"); // For debugging, SW should be able to get here and should be reolved on the preivous if
	}
	// If op is sw we require rd to be ready only in the mem stage so we need only to check if it is in WB

	#ifdef DEBUG_ON
	printf("Core: %x, DECODE END opcode: %s, rd: %x, rs: %x, rt: %x, imm: %x, pc: %x\n", core_id, opcode_to_string(dec_ex->instrc_d.opcode), dec_ex->instrc_d.rd, dec_ex->instrc_d.rs, dec_ex->instrc_d.rt, dec_ex->instrc_d.imm, dec_ex->instrc_d.pc);
	#endif
	// --------------------- EXECUTE ---------------------------

	#ifdef DEBUG_ON
	// printf("core: %x, EXECUTE START opcode: %x, rd: %x, rs: %x, rt: %x, imm: %x, pc: %x\n", core_id, ex_mem->instrc_q.opcode, ex_mem->instrc_q.rd, ex_mem->instrc_q.rs, ex_mem->instrc_q.rt, ex_mem->instrc_q.imm, ex_mem->instrc_q.pc);
	printf("Core: %x, EXECUTE START opcode: %s, rd: %x, rs: %x, rt: %x, imm: %x, pc: %x\n", core_id, opcode_to_string(dec_ex->instrc_q.opcode), dec_ex->instrc_q.rd, dec_ex->instrc_q.rs, dec_ex->instrc_q.rt, dec_ex->instrc_q.imm, dec_ex->instrc_q.pc);
	#endif

	ex_mem->data_d = execute_op(dec_ex->instrc_q, registers);
	ex_mem->instrc_d = dec_ex->instrc_q;
	ex_mem->pc_d = dec_ex->pc_q;

	#ifdef DEBUG_ON
	printf("Core: %x, EXECUTE END opcode: %s, rd: %x, rs: %x, rt: %x, imm: %x, res: %x, pc: %x\n", core_id, opcode_to_string(ex_mem->instrc_d.opcode), ex_mem->instrc_d.rd, ex_mem->instrc_d.rs, ex_mem->instrc_d.rt, ex_mem->instrc_d.imm, ex_mem->data_d, ex_mem->instrc_d.pc);
	#endif

	// -------------------- MEMORY -----------------------------
	int address = ex_mem->data_q;
	opcode = ex_mem->instrc_q.opcode;

	#ifdef DEBUG_ON
	// printf("core: %x, MEMORY START opcode: %x, rd: %x, rs: %x, rt: %x, imm: %x, pc: %x\n", core_id, mem_wb->instrc_q.opcode, mem_wb->instrc_q.rd, mem_wb->instrc_q.rs, mem_wb->instrc_q.rt, mem_wb->instrc_q.imm, mem_wb->instrc_q.pc);
	printf("Core: %x, MEMORY START opcode: %s, rd: %x, rs: %x, rt: %x, imm(dec): %d, pc: %x\n", core_id, opcode_to_string(ex_mem->instrc_q.opcode), ex_mem->instrc_q.rd, ex_mem->instrc_q.rs, ex_mem->instrc_q.rt, ex_mem->instrc_q.imm, ex_mem->instrc_q.pc);
	#endif



	mem_rsp_s mem_rsp;
	mem_rsp = handle_mem(dsram[core_id],tsram[core_id], address, opcode, registers[ex_mem->instrc_q.rd], progress_clk, &cache_state[core_id], &core_state[core_id], bus_cmd, gnt,core_id); // Need to provide the data from ex stage
	mem_wb->instrc_d = ex_mem->instrc_q;
	mem_wb->data_d = opcode == lw ? mem_rsp.data : ex_mem->data_q; // If the op is lw we take the data from mem stage, if not we take the data from ex stage
	mem_wb->pc_d = ex_mem->pc_q;

	// Handle stall
	if (mem_rsp.stall == 1) {
		#ifdef DEBUG_ON
		puts("MEM ISSUED STALL");
		#endif
		if (mem_wb->instrc_d.opcode == lw && gnt == 1 && progress_clk == 1) total_rmis[core_id]++;
		if (mem_wb->instrc_d.opcode == sw && gnt == 1 && progress_clk == 1) total_wmis[core_id]++;
		mem_wb->instrc_d.opcode = stall;
		if (progress_clk == 1) total_mem_stall[core_id]++;
		next_pc = *pc; // decode the same instrc next clk
		// fe_dec->data_d = fe_dec->data_q; // decode the same instrc next clk
		// fe_dec->pc_d = fe_dec->pc_q;
//
		// dec_ex->instrc_d = dec_ex->instrc_q; // execute the same instrc next clk
		// dec_ex->pc_d     = dec_ex->pc_q;

		if (dec_set_busy) busy_regs[dec_ex->instrc_d.rd] = busy_reg_before; //If dec already set the reg to busy we need to revert it


	}
	else if (mem_wb->instrc_q.opcode != stall) {
		if ((mem_wb->instrc_d.opcode == lw) && (progress_clk == 1))
		{
			total_rhit[core_id]++;
		}
		if ((mem_wb->instrc_d.opcode == sw) && (progress_clk == 1))
		{
			total_whit[core_id]++;
		}
		else;
	}
	#ifdef DEBUG_ON
	printf("Core: %x, MEMORY END opcode: %s, rd: %x, rs: %x, rt: %x, imm: %x, pc: %x, Data(Hex): %x\n", core_id, opcode_to_string(mem_wb->instrc_d.opcode), mem_wb->instrc_d.rd, mem_wb->instrc_d.rs, mem_wb->instrc_d.rt, mem_wb->instrc_d.imm, mem_wb->instrc_d.pc, mem_wb->data_d);
	#endif

	// ---------------------- WRITE BACK -----------------------

#ifdef DEBUG_ON
	printf("Core: %x, WB START opcode: %s, rd: %x, rs: %x, rt: %x, imm: %x, pc: %x, Data(Hex): %x\n", core_id, opcode_to_string(mem_wb->instrc_q.opcode), mem_wb->instrc_q.rd, mem_wb->instrc_q.rs, mem_wb->instrc_q.rt, mem_wb->instrc_q.imm, mem_wb->instrc_q.pc, mem_wb->data_q);
#endif

	opcode = mem_wb->instrc_q.opcode;
	if (opcode != stall && opcode != beq && opcode != bne && opcode != blt && opcode != bgt && opcode != bge && opcode != ble && opcode != sw && opcode != jal) { // no branch cmd or sw or stall
		registers[mem_wb->instrc_q.rd] = mem_wb->data_q;
		busy_regs[mem_wb->instrc_q.rd] = 0;

		//  Make sure that 0 and 1 are always free
		busy_regs[0] = 0;
		busy_regs[1] = 0;

		#ifdef DEBUG_ON
		printf("Core: %x, WB address: %x, data: %x\n", core_id, mem_wb->instrc_q.rd, mem_wb->data_q);
		#endif
	}
	else if (opcode == jal) {
		registers[15] = mem_wb->instrc_q.pc + 1;
		busy_regs[15] = 0;
	}

	if (((opcode == halt) || (error_flag == 1)) && halt_core[core_id] == 0) {
#ifdef DEBUG_ON
		printf("Core: %x, HALT\n", core_id);
#endif
		halt_core[core_id] = 1;
		store_stats_to_file(core_id, clk+1, total_inst[core_id], total_rhit[core_id], total_whit[core_id], total_rmis[core_id], total_wmis[core_id], total_dec_stall[core_id], total_mem_stall[core_id], output_files);
		store_regs_to_file(core_id, registers, output_files);
		
		printf("Storing core#%d trace to %s\n", core_id,output_files[core_id + 5]);
		fclose(trace_files[core_id]);
		puts("\n");
	}

	if ((opcode != stall) && (progress_clk == 1)) {
		total_inst[core_id]++;
	};

	// ---------------------------------------------------------

	if (progress_clk == 1 && halt_core[core_id] == 0) {
		#ifdef DEBUG_ON
			printf("Core: %x, Clk: %d\n\n", core_id,clk);
		#endif

		append_trace_line(trace_files[core_id], clk, halt_in_fetch[core_id] ? -1 : *pc, dec_ex->instrc_d, dec_ex->instrc_q,ex_mem->instrc_q, mem_wb->instrc_q, registers);
		if (mem_rsp.stall == 1) {

			stall_reg(dec_ex);
			stall_reg(fe_dec);
			stall_reg(ex_mem);
		}
		*pc = halt_in_fetch[core_id] == 0 ? (next_pc & 0x03FF) : *pc; // if halt dont progress pc, take only 10 lot bits from next_pc
		//*pc = next_pc & 0x03FF; // if halt dont progress pc, take only 10 lot bits from next_pc

        progress_reg(fe_dec);
		progress_reg(dec_ex);
		progress_reg(ex_mem);
		progress_reg(mem_wb);

		memcpy(registers_arr[core_id], registers, sizeof(registers));
		memcpy(busy_regs_arr[core_id], busy_regs, sizeof(busy_regs));
	}
	else
	{
		#ifdef DEBUG_ON
		printf("Core: %x, did not progress clk\n\n", core_id);
		#endif // DEBUG_ON
	}


	// Check if all entries in halt are one
	int all_halted = 1;
	for (int i = 0; i < NUM_CORES; i++) {
		if (halt_core[i] != 1) {
			all_halted = 0;
			break;
		}
	}

	if (all_halted) {
		printf("All cores are halted.\n");
		mem_rsp.bus.bus_cmd = kHalt;
		for (int i = 0; i < NUM_CORES; i++) {
			puts("\n");
			store_dsram_to_file(i, dsram[i], output_files);
			store_tsram_to_file(i, tsram[i], output_files);
		}
		puts("\n");
	}

	return mem_rsp.bus;
}
// The following functions are not used in the code and should be removed



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

