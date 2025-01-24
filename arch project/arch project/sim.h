#ifndef SIM_H
#define SIM_H

#define NUM_OF_BLOCKS    64
#define DSRAM_DEPTH    256
#define DSRAM_WIDTH    32 // Bits
#define BLOCK_SIZE     4  // Words (32bits)
#define NUM_OF_REGS    16
#define ADDR_WIDTH     20 // Bits (word aligned)
#define MAIN_MEM_DEPTH 1 << ADDR_WIDTH
#define SET_WIDTH      6  // Bits
#define TAG_WIDTH      12 // Bits
#define OFFSET_WIDTH   2  // Bits
#define NUM_CORES      4
#define MEM_RD_LATENCY 16 // Time until memory start returning the data

#include "core_sim.h"

typedef enum
{
	kNoCmd,
	kBusRd,
	kBusRdX,
	kFlush
} bus_cmd_t;

typedef enum
{
	Invalid,
	Shared,
	Exclusive,
	Modified
} mesi_state_t;

typedef enum
{
	core0,
	core1,
	core2,
	core3,
	main_mem_id
} bus_origid_t;

typedef enum
{
	kBusAvailable,
	kBusWaitMem,
	kBusRead,
	kBusWaitFlush
} bus_state_t;

typedef enum
{
	kRdMiss,
	kWrMiss,
	kModifiedMiss,
	kHit,
	kWrHitShared
}  cache_hit_t;

typedef enum
{
	kIdle,
	kWaitForGnt,
	kWaitForFlush
} cache_state_t;

typedef struct
{
	bus_origid_t     bus_origid;
	bus_cmd_t	     bus_cmd;
	int			     bus_addr;
	int			     bus_data;
	int			     bus_share;
	int 		     req_enable;
} bus_cmd_s;

typedef struct
{
	instrc instrc_d;
	instrc instrc_q;
	int data_d;
	int data_q;
} register_line_s;

typedef struct
{
	int				tag; // Size of 12 bits
	mesi_state_t	state;
}   tsram_entry;

typedef struct
{
	unsigned int tag;
	unsigned int set;
	unsigned int offset;
}   cache_addr_s ;

typedef struct
{
	cache_hit_t  hit_type;
	unsigned int data;
} cache_query_rsp_s ;


typedef struct
{
	unsigned int stall;
	unsigned int data;
	bus_cmd_s    bus;
} mem_rsp_s;


typedef struct 
{
	bus_cmd_s bus_cmd;
	int data_rtn;
} bus_routine_rsp_s;

void progress_register_data(register_s* reg);

cache_addr_s parse_addr(int addr);

int round_robin_arbitrator();

bus_cmd_s core(int id, int gnt, bus_cmd_s bus_cmd, int progress_clock);

bus_cmd_s cores(bus_cmd_s bus_req, int priority_for_gnt, int gnt_core_id, int progress_clock);

cache_query_rsp_s cache_query(int dsram[][], tsram_entry tsram[], int addr,opcode op, int data,int progress_clk);

mem_rsp_s handle_mem(int dsram[][], tsram_entry tsram[], int addr,opcode op, int data, int progress_clk, cache_state_t * cache_state, bus_cmd_s bus, int gnt);

bus_routine_rsp_s bus_routine(bus_state_t bus_state, bus_cmd_s bus_req, int mem_wait_counter, int mem_rd_counter, int shared, int progress_clock, int gnt, bus_origid_t flushing_core_id, int main_mem[]);

#endif // SIM_H
