//#pragma once
#ifndef CORE_SIM_H
#define CORE_SIM_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "sim_source.h"

#define ALLOW_EMPTY_ARGUMENTS

//#ifndef DEBUG_ON
//#define DEBUG_ON
//#endif

//#define TIMEOUT_ON
typedef enum
{
	mem_depth			= 4096,
	mem_bit_width		= 20,
	disk_delay			= 1024,
	mem_hex_width		= 5,
	num_registers		= 16,
	num_hw_registers	= 23,
	monitor_pixel_width = 256,
	sector_width		= 128,
	hard_disk_width     = sector_width * sector_width,
	debug_on			= false
} parameters;

// Define enum for opcodes
typedef enum
{
	add = 0,
	sub = 1,
	and = 2,
	or =  3,
	xor = 4,
	mul = 5,
	sll = 6,
	sra = 7,
	srl = 8,
	beq = 9,
	bne = 10,
	blt = 11,
	bgt = 12,
	ble = 13,
	bge = 14,
	jal = 15,
	lw = 16,
	sw = 17,
	halt = 20,
	stall = -1
} opcode_t;

typedef struct instrc {
	opcode_t opcode;
	int rd;
	int rs;
	int rt;
	int imm;
	int is_i_type;
	int pc;
} instrc;

typedef struct
{
	instrc instrc_d;
	instrc instrc_q;
	int pc_d;
	int pc_q;
	int data_d;
	int data_q;
} register_line_s;

// Define enum for Bus transactions
typedef enum
{
	Idle,
	WaitForFlush,
	Send,
	Receive
} core_state_t;

cache_query_rsp_s cache_query(int dsram[][BLOCK_SIZE], tsram_entry tsram[], int addr,opcode_t op, int data,int progress_clk);
mem_rsp_s handle_mem(int dsram[][BLOCK_SIZE], tsram_entry tsram[], int addr, opcode_t op, int data, int progress_clk, cache_state_t* cache_state, core_state_t* core_state, bus_cmd_s bus, int gnt, int core_id, int* total_whit, int* total_wmis, int* total_rhit, int* total_rmis);
bus_routine_rsp_s bus_routine(int dsram[][BLOCK_SIZE], tsram_entry tsram[],bus_cmd_s bus, int progress_clock, int gnt, core_state_t * core_state, int core_id, int core_req_trans, int addr, int data, cache_hit_t hit_type);


int get_signed_imm(const int imm);
int execute_op(const instrc instrc, int registers[]);
instrc decode_line(const int line_dec, int registers[], int pc);

void store_regs_to_file(int core_id, int regs[NUM_OF_REGS],const char *output_files[]);

void store_stats_to_file(int core_id, int clk, int instc, int rhit, int whit, int rmis, int wmis, int dec_stall, int mem_stall,const char *output_files[]);

void progress_reg(register_line_s *reg);

void stall_reg(register_line_s *reg);

const char *opcode_to_string(opcode_t opcode);

int get_reg_val(int reg, int registers[], int imm);

void append_trace_line(FILE *file, int clk, int fetch, instrc decode, instrc exec, instrc mem, instrc wb, int registers[NUM_OF_REGS]);

FILE **create_trace_files(const char *output_files[]);

#endif