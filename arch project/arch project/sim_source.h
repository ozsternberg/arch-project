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
#define MEM_FILE_SIZE  1024


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

extern const char *input_files[] = {"imem0.txt", "imem1.txt", "imem2.txt", "imem3.txt", "memin.txt"};
extern const char *output_files[] = {
    "memout.txt", "regout0.txt", "regout1.txt", "regout2.txt", "regout3.txt",
    "core0trace.txt", "core1trace.txt", "core2trace.txt", "core3trace.txt",
    "bustrace.txt", "dsram0.txt", "dsram1.txt", "dsram2.txt", "dsram3.txt",
    "tsram0.txt", "tsram1.txt", "tsram2.txt", "tsram3.txt",
    "stats0.txt", "stats1.txt", "stats2.txt", "stats3.txt"
  };

cache_addr_s parse_addr(int addr);

int round_robin_arbitrator();

bus_cmd_s core(int id, int gnt, bus_cmd_s bus_cmd, int progress_clock, int clk, int argc, char *argv[]);

bus_cmd_s cores(bus_cmd_s bus_req, int priority_for_gnt, int gnt,int gnt_core_id, int progress_clock,int argc, char *argv[]);

void load_mem_files(int mem_files[NUM_CORES][MEM_FILE_SIZE], const char *file_names[NUM_CORES]);

void load_main_mem(const char *file_name, int lines[MAIN_MEM_DEPTH]);

void store_mem_to_file(const char *file_name, int mem_array[],int mem_array_size);

char **create_output_files(int argc, char *argv[], const char *output_files[], int output_files_count);

void store_tsram_to_file(int core_id, tsram_entry tsram[NUM_OF_BLOCKS]);

#endif // SIM_H
