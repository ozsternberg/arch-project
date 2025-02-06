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
	memcpy(registers, registers_arr[core_id], sizeof(registers));

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

	opcode_t opcode;
	int      next_pc;
	int dec_set_busy = 0;

#ifdef OPTIMIZATION_ON
	if (progress_clk == 0)
	{
		mem_rsp_s mem_rsp = handle_mem(dsram[core_id],tsram[core_id], ex_mem->data_q, ex_mem->instrc_q.opcode, registers[ex_mem->instrc_q.rd], progress_clk, &cache_state[core_id], &core_state[core_id], bus_cmd, gnt,core_id, &total_whit[core_id], &total_wmis[core_id], &total_rhit[core_id], &total_rmis[core_id]); // Need to provide the data from ex stage
		return mem_rsp.bus;
	}
#endif

	// -------------------- FETCH -----------------------------

	fe_dec->data_d = mem[core_id][*pc];
	fe_dec->instrc_d.pc = halt_in_fetch[core_id] == 1 ? -1 : *pc;
	fe_dec->pc_d = fe_dec->instrc_d.pc;

	next_pc = *pc + 1; // default
	#ifdef DEBUG_ON
	printf("Core: %x, FETCH pc: %x\n", core_id, *pc);
	#endif

	// --------------------- DECODE ---------------------------
	if (clk > 0) fe_dec->instrc_q = decode_line(fe_dec->data_q, registers,fe_dec->pc_q);
	dec_ex->instrc_d = fe_dec->instrc_q;

	dec_ex->pc_d = fe_dec->pc_q;

	if (halt_in_fetch[core_id])
	{
		dec_ex->instrc_d.pc = -1;
	}
	#ifdef DEBUG_ON
	printf("Core: %x, DECODE START opcode: %s, rd: %x, rs: %x, rt: %x, imm: %x, pc: %x\n", core_id, opcode_to_string(dec_ex->instrc_d.opcode), dec_ex->instrc_d.rd, dec_ex->instrc_d.rs, dec_ex->instrc_d.rt, dec_ex->instrc_d.imm, dec_ex->instrc_d.pc);
	#endif
	opcode = dec_ex->instrc_d.opcode;

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
	mem_rsp = handle_mem(dsram[core_id],tsram[core_id], address, opcode, registers[ex_mem->instrc_q.rd], progress_clk, &cache_state[core_id], &core_state[core_id], bus_cmd, gnt,core_id, &total_whit[core_id], &total_wmis[core_id], &total_rhit[core_id], &total_rmis[core_id]); // Need to provide the data from ex stage
	mem_wb->instrc_d = ex_mem->instrc_q;
	mem_wb->data_d = opcode == lw ? mem_rsp.data : ex_mem->data_q; // If the op is lw we take the data from mem stage, if not we take the data from ex stage
	mem_wb->pc_d = ex_mem->pc_q;

	// Handle stall
	if (mem_rsp.stall == 1) {
		#ifdef DEBUG_ON
		puts("MEM ISSUED STALL");
		#endif
		//if (mem_wb->instrc_d.opcode == lw && gnt == 1 && progress_clk == 1) total_rmis[core_id]++;
		//if (mem_wb->instrc_d.opcode == sw && gnt == 1 && progress_clk == 1) total_wmis[core_id]++;
		mem_wb->instrc_d.opcode = stall;
		if (progress_clk == 1) total_mem_stall[core_id]++;
		next_pc = *pc; // decode the same instrc next clk

		if (dec_set_busy) busy_regs[dec_ex->instrc_d.rd] = busy_reg_before; //If dec already set the reg to busy we need to revert it
	}
	//if (progress_clk == 1)
	//{
	//	if (mem_wb->instrc_d.opcode == lw)
	//	{
	//		if ((cache_state[core_id] == kIdle) && mem_rsp.hit == kHit)
	//		{
	//			total_rhit[core_id]++;
	//		}
	//		else
	//		{
	//			total_rmis[core_id]++;
	//		}
	//	}
	//	else if (mem_wb->instrc_d.opcode == sw)
	//	{
	//		if ((cache_state[core_id] == kIdle) && mem_rsp.hit != kWrMiss)
	//		{
	//			total_whit[core_id]++;
	//		}
	//		else
	//		{
	//			total_wmis[core_id]++;
	//		}
	//	}
	//}
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

	if ((opcode != stall) && (progress_clk == 1)) {
		total_inst[core_id]++;
	};

	if (((opcode == halt) || (error_flag == 1)) && halt_core[core_id] == 0) {
#ifdef DEBUG_ON
		printf("Core: %x, HALT\n", core_id);
#endif
		halt_core[core_id] = 1;
		store_stats_to_file(core_id, clk+1, total_inst[core_id], total_rhit[core_id], total_whit[core_id], total_rmis[core_id], total_wmis[core_id], total_dec_stall[core_id], total_mem_stall[core_id], output_files);
		store_regs_to_file(core_id, registers, output_files);
		append_trace_line(trace_files[core_id], clk, halt_in_fetch[core_id] ? -1 : *pc, dec_ex->instrc_d, dec_ex->instrc_q, ex_mem->instrc_q, mem_wb->instrc_q, registers);

		printf("Storing core#%d trace to %s\n", core_id,output_files[core_id + 5]);
		fclose(trace_files[core_id]);
		puts("\n");
	}

	// ---------------------------------------------------------

	if (progress_clk == 1 && halt_core[core_id] == 0) {
		#ifdef DEBUG_ON
			printf("Core: %x, Clk: %d\n\n", core_id,clk);
		#endif

		append_trace_line(trace_files[core_id], clk, fe_dec->instrc_d.pc, fe_dec->instrc_q, dec_ex->instrc_q,ex_mem->instrc_q, mem_wb->instrc_q, registers_arr[core_id]);
		if (mem_rsp.stall == 1) {

			stall_reg(dec_ex);
			stall_reg(fe_dec);
			stall_reg(ex_mem);
		}
		*pc = halt_in_fetch[core_id] == 0 ? (next_pc & 0x03FF) : *pc; // if halt don't progress pc, take only 10 lot bits from next_pc

		if (halt_in_fetch[core_id] == 1)
		{
			fe_dec->instrc_d.pc = -1;
			fe_dec->pc_d = -1;
		}
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
		printf("Core: %x is halted, did not progress clk\n\n", core_id);
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

