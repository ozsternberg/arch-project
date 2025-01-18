//#pragma once
#ifndef CORE_SIM_H
#define CORE_SIM_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>


//#define ALLOW_EMPTY_ARGUMENTS
//#define DEBUG_ON
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
	add  = 0,
	sub  = 1,
	mul  = 2,
	and  = 3,
	or   = 4,
	xor  = 5,
	sll  = 6,
	sra  = 7,
	srl  = 8,
	beq  = 9,
	bne  = 10,
	blt  = 11,
	bgt  = 12,
	ble  = 13,
	bge  = 14,
	jal  = 15,
	lw   = 16,
	sw   = 17,
	reti = 18,
	in   = 19,
	out  = 20,
	halt = 21,
	fail = -1
} opcode;

// Define a struct for each line
typedef struct instrc {
	opcode opcode;
	int rd;
	int rs;
	int rt;
	int imm;
	bool is_i_type;
} instrc ;

const char* get_io_register_name(int reg_number);

// Function for getting the signed value of imm in 32 bit
int get_signed_imm(int imm);

int perform_op(const instrc instrc, int registers[], int mem[], int hw_registers[], int* pc, int* in_irq, bool debug_on);

instrc create_instrc(int line_dec);

//==========================================================================
// Functions for IO purposes
//==========================================================================

// Function for updating the monitor
void update_mon(int frame_buffer[][monitor_pixel_width], const int pixel_value, const int monitoraddr);

// Function that handles memory transfer between mem and disk
void transfer_data(int mem[], int disk[], const int write, const int sector_offset, const int mem_addr);


//==========================================================================
// Function for handling output/input files
//==========================================================================

// Function for loading a file
int load_file_into_array(const char* filename, int array[], const int max_lines, const int is_hex);


void create_trace_line(const int mem[], const int pc, const int registers[], FILE* file_pntr);

void create_hwtrace_line(const unsigned int clk, const instrc instrc, const int registers[], FILE* hwregtrace_pntr);

int create_leds_line(const unsigned int clk, const int leds, const int previous_leds, FILE* log_file);

void write_to_file(const int text[], FILE* file_pntr, size_t size);

void write_to_monitor(const int frame_buffer[][monitor_pixel_width], FILE* file_pntr, bool binary_format);

FILE* open_file(const char* filename, const char* mode);


void write_reg(int registers[], FILE* regout_pntr);
#endif 
