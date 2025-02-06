#ifndef SIM_H
#define SIM_H

#define NUM_OF_BLOCKS  64
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
#define MEM_RD_LATENCY 15 // Time until memory start returning the data
#define MEM_FILE_SIZE  1024

//=============================================================================
// Defines for enabling features
//=============================================================================
#define OPTIMIZATION_ON
//#define RR_OPT
#define ALLOW_PARTIAL_ARGUMENTS
//#define DEBUG_ON
// #ifndef LINUX_MODE
// #define LINUX_MODE
// #endif

//=============================================================================
// Structs and types
//=============================================================================

typedef enum
{
	kNoCmd,
	kBusRd,
	kBusRdX,
	kFlush,
	kHalt
} bus_cmd_t;

typedef enum
{
	Invalid   = 0,
	Shared    = 1,
	Exclusive = 2,
	Modified  = 3
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
	kBusFlush,
	kWaitCoreFlush
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
	kWaitForFlush,
	kCompleteReq
} cache_state_t;

typedef struct
{
	bus_origid_t     bus_origid;
	bus_cmd_t	     bus_cmd;
	unsigned int	 bus_addr;
	int			     bus_data;
	int			     bus_share;
} bus_cmd_s;


typedef struct
{
	unsigned int	tag; // Size of 12 bits
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
	int data;
} cache_query_rsp_s ;


typedef struct
{
	int          stall;
	int          data;
	bus_cmd_s    bus;
	cache_hit_t	 hit;
} mem_rsp_s;


typedef struct
{
	bus_cmd_s bus_cmd;
	int data_rtn;
} bus_routine_rsp_s;

extern const char *input_files[];
extern const char *output_files[];

//=============================================================================
// Function declarations
//=============================================================================

cache_addr_s parse_addr(int addr);

int round_robin_arbitrator();

bus_cmd_s core(int core_id, int gnt, bus_cmd_s bus_cmd, int progress_clock, int clk, const char *output_files[], unsigned int mem[NUM_CORES][MEM_FILE_SIZE]);

bus_cmd_s cores(bus_cmd_s bus_req, int priority_for_gnt, int gnt,int gnt_core_id, int progress_clk,int clk, const char *output_files[], unsigned int mem[NUM_CORES][MEM_FILE_SIZE]);

int load_mem_files(unsigned int mem_files[NUM_CORES][MEM_FILE_SIZE],  char *file_names[]);

int load_main_mem(const char *file_name, int lines[MAIN_MEM_DEPTH]);

void store_mem_to_file(const char *file_name, int mem_array[],int mem_array_size);

void check_input_files(int argc, char *argv[], const char *input_files[], int input_files_count);

const char **create_output_files(int argc, char *argv[], const char *output_files[], int output_files_count);

void store_dsram_to_file(int core_id, int array[NUM_OF_BLOCKS][BLOCK_SIZE], const char *output_files[]);

void store_tsram_to_file(int core_id, tsram_entry tsram[NUM_OF_BLOCKS],const char *output_files[]);

const char *get_bus_cmd_name(bus_cmd_t cmd);

void append_bus_trace_line(const char* file_name, int cycle, int bus_origid, int bus_cmd, int bus_addr, int bus_data, int bus_shared);

#endif // SIM_H
