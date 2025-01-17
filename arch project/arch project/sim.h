#ifndef SIM_H
#define SIM_H

#define TSRAM_DEPTH    64
#define DSRAM_DEPTH    256 
#define DSRAM_WIDTH    32 // Bits
#define BLOCK_SIZE     4  // Words (32bits)
#define NUM_OF_REGS    16
#define ADDR_WIDTH     20 // Bits (word aligned)
#define MAIN_MEM_DEPTH 2**20
#define SET_WIDTH      6  // Bits
#define TAG_WIDTH      12 // Bits
#define OFFSET_WIDTH   2  // Bits
#define NUM_CORES      4

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

typedef struct
{
	bus_origid_t bus_origid;
	bus_cmd_t	   bus_cmd;
	int			     bus_addr;
	int			     bus_data;
	int			     bus_share;
	int 		     req_enable;
} bus_cmd_s;

typedef struct
{
	int q;
	int d;
} register_s;

typedef struct
{
	instrc q;
	instrc d;
} register_line_s;

typedef struct
{
	int				tag; // Size of 12 bits
	mesi_state_t	state;
}   tsram_entry; 

typedef struct
{
	int tag;
	int set;
	int offset;
}   cache_addr_s ;



void progress_register_data(register_s* reg);

cache_addr_s parse_addr(int addr);

int round_robin_arbitrator();

bus_cmd_s core(int id, int gnt, bus_cmd_s bus_cmd);

void cores(bus_cmd_s bus_req, int exclude, int gnt_core_id);

#endif // SIM_H
