#include "core_source.h"
#include <stdlib.h>

// The dsram and tsram should be of only once core
cache_query_rsp_s cache_query(int dsram[][BLOCK_SIZE], tsram_entry tsram[], int addr, opcode_t op, int data, int progress_clk)
{

    if (op != lw && op != sw) {
        return (cache_query_rsp_s) { kHit, 0 };
    }
    cache_addr_s cache_addr = parse_addr(addr);

    mesi_state_t data_state = tsram[cache_addr.set].state;
    int hit = tsram[cache_addr.set].tag == cache_addr.tag ? 1 : 0;
    int word = 0;
    cache_hit_t hit_type;

    if (data_state == Modified && hit == 0)
    {
            hit_type = kModifiedMiss;
    }

    else if (op == lw)
    {
        if (hit && data_state != Invalid) // Rd hit
        {
            word     = dsram[cache_addr.set][cache_addr.offset];
            hit_type = kHit;
        }

        else // Rd miss
        {
            hit_type = kRdMiss;
        }
    }

    else
    {
        if(hit && data_state != Invalid) // Wr hit
        {
            if (progress_clk && data_state != Shared)
            {
                dsram[cache_addr.set][cache_addr.offset] = data;
                tsram[cache_addr.set].state = Modified;
            }

            hit_type = data_state == Shared ? kWrHitShared : kHit;
        }

        else
        {
            hit_type = kWrMiss;
        }
    }
    cache_query_rsp_s cache_query_rsp = {hit_type,word};
    return  cache_query_rsp;
}

mem_rsp_s handle_mem(int dsram[][BLOCK_SIZE], tsram_entry tsram[], int addr,opcode_t op, int data, int progress_clk,  cache_state_t * cache_state, core_state_t * core_state,bus_cmd_s bus, int gnt, int core_id)
{
    cache_query_rsp_s cache_query_rsp = cache_query(dsram, tsram, addr, op, data, progress_clk);
    cache_hit_t hit_type = cache_query_rsp.hit_type;

    int core_req_trans = op == sw || op == lw ? 1 : 0;

    if ((*cache_state != kIdle) && (core_req_trans == 0))
    {
        printf("Non mem opcode arrived in the middle of transaction\n");
    }

    if (op == stall && core_req_trans)
    {
        perror("Something went wrong, a stall go into mem stage in the middle of transaction!");
    }
    bus_routine_rsp_s bus_routine_rsp = bus_routine(dsram,tsram,bus,progress_clk,gnt,core_state,core_id,core_req_trans,addr,cache_query_rsp.data,hit_type);
	mem_rsp_s mem_rsp = {0, cache_query_rsp.data, bus_routine_rsp.bus_cmd};

    cache_state_t next_cache_state = *cache_state;

    switch (*cache_state)
    {
        case kIdle:
            mem_rsp.stall = 0;
            if (hit_type == kHit || core_req_trans == 0 || op == halt) break; // If hit or no req do nothing

            // If gnt is 0, stall
            if (gnt == 0)
            {
                mem_rsp.stall = 1;
                next_cache_state = kWaitForGnt;
                break;
            }

            if (hit_type == kWrHitShared) break; //If gnt == 1 and hit is shared stay Idle

            else // If gnt == 1 and miss, wait for flush or send
            {
                mem_rsp.stall = 1;
                next_cache_state = kWaitForFlush;
                break;
            }

        case kWaitForGnt:
            mem_rsp.stall = 1;
            if (gnt == 0) break; // If gnt is 0, stall

            if (hit_type == kWrHitShared) //If gnt == 1 and hit is shared do stay Idle
            {
                mem_rsp.stall = 0;
                next_cache_state = kIdle;
                break;
            }

            else // If gnt == 1 and miss, wait for flush receive or send
            {
                next_cache_state = kWaitForFlush;
                break;
            }

        case kWaitForFlush:
            mem_rsp.stall = 1;
            if (bus_routine_rsp.data_rtn == 0) break; // If no data is returned, stall

            if (bus_routine_rsp.data_rtn == 1)
            {
                if (op == halt)
				{
                    perror("Halt arrived mid transaction!\n\n");
				}
                if ((bus_routine_rsp.bus_cmd.bus_addr & 0xFFFFFFFC) != (addr & 0xFFFFFFFC))
                {
                    printf("Data is received from wrong address\n");
                }
                next_cache_state = kIdle;
            }

            break;
    }

    if (progress_clk == 1) *cache_state = next_cache_state;

    return mem_rsp;
}

bus_routine_rsp_s bus_routine(int dsram[][BLOCK_SIZE], tsram_entry tsram[],bus_cmd_s bus, int progress_clock, int gnt, core_state_t * core_state, int core_id, int core_req_trans, int addr, int data, cache_hit_t hit_type)
{


    // Define core
    static int core_send_counter[NUM_CORES]    = {0};
    static int core_receive_counter[NUM_CORES] = {0};

    static unsigned int offset[NUM_CORES];
    static unsigned int index[NUM_CORES];
    static unsigned int tag[NUM_CORES];
    static unsigned int bus_addr[NUM_CORES];
    static unsigned int bus_shared[NUM_CORES];
    static tsram_entry* entry[NUM_CORES] = { 0 };
    static core_state_t next_state[NUM_CORES] = { Idle };
    static mesi_state_t entry_state[NUM_CORES];

    int flush_done = 0; // Raise signal when flush is done

    // Bus routine
	int receive_done = 0;

    if (gnt == 1 && *core_state != Idle)
    {
        printf("Error - core #%d is not idle but received gnt\n", core_id);
    }

	switch (*core_state){
    	case Idle:

    		if(gnt == 0)
    		{
    			bus_addr[core_id] = bus.bus_addr & 0xFFFFFFFC;
    			// Split bus address to tsram and dsram parameters
    			offset[core_id] = parse_addr(bus_addr[core_id]).offset;
                index[core_id] = parse_addr(bus_addr[core_id]).set;
    			tag[core_id] = parse_addr(bus_addr[core_id]).tag;
    			entry[core_id] = &tsram[index[core_id]];
    			next_state[core_id] = Idle;
                entry_state[core_id] = entry[core_id]->state;
    			if((entry[core_id]->tag == tag[core_id]) && (entry[core_id]->state != Invalid)) // Check if passive hit
    			{
    				if ((bus.bus_cmd == kBusRdX) || (bus.bus_cmd == kBusRd))
    				{
    					if((entry[core_id]->state == Modified) && (progress_clock == 1))  // If the data is modified- Send
    					{
    						core_send_counter[core_id] = 0;
    						next_state[core_id] = Send ;
    					}
    				}
                    if (bus.bus_cmd == kFlush)
                    {
                        bus.bus_share = 1;
                        entry_state[core_id] = Shared;
                    }
    				if (bus.bus_cmd == kBusRd) //set shared wire
    				{
    					//bus.bus_share = 1 ;
                        bus_shared[core_id]  = 1;
    					entry_state[core_id] = Shared  ; // If command is kBusRd-change state to shared  - from all states(exclusive, modified or shared)
    				}
    				if (bus.bus_cmd == kBusRdX)
    				{
						//bus.bus_share = 0;
                        bus_shared[core_id] = 0;
    					entry_state[core_id] = Invalid ; // If BusRdX change to Invalid  - from all states(exclusive, modified or shared)
    				}
    				if(progress_clock == 1) // change core state and entry[core_id] state on next clock cycle
    				{
    					entry[core_id]->state = entry_state[core_id];
    					*core_state = next_state[core_id];
    				}
    			}
    			break;
    		}
    		if(gnt == 1)  // check for transaction
    		{
    			bus.bus_origid = core_id; // Set bus_origid to core_id
                bus.bus_share  = 0;
                bus.bus_data   = 0;
                bus.bus_addr   = 0;
    			if(core_req_trans == 1)
    			{
                    bus.bus_addr = addr;
                    bus_addr[core_id] = bus.bus_addr & 0xFFFFFFFC; // save a copy of the aligned trans address
    				// Split bus address to tsram and dsram parameters
    				offset[core_id] = parse_addr(bus.bus_addr).offset;
    				index[core_id] = parse_addr(bus.bus_addr).set;
    				tag[core_id] = parse_addr(bus.bus_addr).tag;
    				entry[core_id] = &tsram[index[core_id]];
    				entry_state[core_id] = entry[core_id]->state;
                    next_state[core_id] = Idle;
					int update_mem = dsram[index[core_id]][offset[core_id]];

    				if(hit_type == kRdMiss)  // If read miss - set bus_cmd to BusRd
    				{
    					bus.bus_cmd = kBusRd;
    					next_state[core_id] = WaitForFlush; // A transaction is made, now wait for flash

    				}
                    else if(hit_type == kWrMiss) // If write miss - set bus_cmd to BusRdX
    				{
    					bus.bus_cmd = kBusRdX;
    					next_state[core_id] = WaitForFlush; // A transaction is made, now wait for flash
    				}
                    else if (hit_type == kWrHitShared) // If cache writeHit on shared data - set bus_cmd to kBusRdX to invalidate data on other caches
    				{
    					bus.bus_cmd = kBusRdX;
    					entry_state[core_id] = Modified;
    					update_mem = data;
    				}

                    else if (hit_type == kModifiedMiss) // If cache writeMiss modified data - set bus_cmd to flush
    				{
    					bus.bus_cmd = kFlush;
    					bus.bus_data = dsram[index[core_id]][0];
    					next_state[core_id] = Send;
    					core_send_counter[core_id] = 1;
    				}

    				if(progress_clock == 1)
    				{
    					entry[core_id]->state = entry_state[core_id];
    					*core_state = next_state[core_id];
    					entry[core_id]->tag = tag[core_id];
    					dsram[index[core_id]][offset[core_id]] = update_mem;
    				}

    			}
    			else
    			{
    				bus.bus_cmd = kNoCmd;
    			}

    			break;

    		}

    		break;

    	case WaitForFlush:

    		next_state[core_id] = WaitForFlush;
            cache_addr_s incoming_addr = parse_addr(bus.bus_addr);
    		if(bus.bus_cmd == kFlush)
    		{
    			if(bus.bus_addr != (addr & 0xFFFFFFFC))
    			{
    				printf("Data is Received from wrong address\n");
    			}
    			next_state[core_id] = Receive;
    			core_receive_counter[core_id] = 1;
    		}
    		if(progress_clock == 1)
    		{
    			*core_state = next_state[core_id];
                dsram[incoming_addr.set][incoming_addr.offset] = bus.bus_data;
    		}
    		break;

    	case Send:

    		next_state[core_id] = Send;
    		flush_done = 0; // Raise signal when flush is done
    		bus.bus_origid = core_id ;
    		bus.bus_cmd = kFlush;
    		bus.bus_data = dsram[index[core_id]][core_send_counter[core_id]];
    		bus.bus_addr = bus_addr[core_id] + core_send_counter[core_id];
            bus.bus_share = bus_shared[core_id];

    		if(progress_clock == 1)
    		{
    			if(core_send_counter[core_id] == BLOCK_SIZE -1)
    			{
    				core_send_counter[core_id] = 0;
    				*core_state = Idle;
    				flush_done = 1;
    				// entry[core_id]->state = Shared;
    				break;
    			}
    			else
    			{
    				*core_state = Send;
    			}
                core_send_counter[core_id]++;

    		}

    		break;

    	case Receive:
            if ((addr & 0xFFFFFFFC) != bus_addr[core_id])
            {
                printf("The expected aligned addr is %d and the saved aligned addr is %d\n", addr & 0xFFFFFFFC, bus_addr[core_id]); // Sanity check
            }
            if (bus.bus_addr == ((addr & 0xFFFFFFFC) + core_receive_counter[core_id]))
    		{
                if (progress_clock == 0) break;

    			dsram[index[core_id]][core_receive_counter[core_id]] = bus.bus_data;

    			if(core_receive_counter[core_id] == BLOCK_SIZE -1)
    			{
    				if(bus.bus_share == 0)
    				{
    					entry[core_id]->state = Exclusive; // If no other cache asserts shared ,set entry_state[core_id] to exclusive
    				}
    				else
    				{
    					entry[core_id]->state = Shared; // If another cache asserts shared set entry[core_id] to shared
    				}
    				core_receive_counter[core_id] = 0;
    				flush_done = 1;
    				*core_state = Idle;
    				break;
    			}
                core_receive_counter[core_id]++;

    		}

            else printf("Data is received from address - %d and the original address is - %d\n", bus.bus_addr, addr + core_receive_counter[core_id]);

    		break;
        default:
            puts("Unknown core state\n");
            break;
    }

    bus_routine_rsp_s bus_routine_rsp = {bus, flush_done};
    return bus_routine_rsp;
};


int get_signed_imm(const int imm) {
	int bit_mask = 0x000800; // Mask where only the 12th bit is 1
	if (imm & bit_mask)
	{
		// If the sign bit (12th bit) is set, sign-extend to 32 bits
		int signed_imm = (0xFFFFF000 | imm);
		return signed_imm;
	}

	// If the sign bit is not set, return the immediate value as it is
	int signed_imm = 0x00000FFF & imm;
	return signed_imm;
};

int execute_op(const instrc instrc, int registers[])
{
	// NOTE: This function assumes that $imm has been loaded with the appropriate value
    int imm = instrc.imm;
	int* rd = (instrc.rd == 1) ? &imm : &registers[instrc.rd];
	int* rs = (instrc.rs == 1) ? &imm : &registers[instrc.rs];
	int* rt = (instrc.rt == 1) ? &imm : &registers[instrc.rt];

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
		return 0;
	default:
        if (instrc.opcode > 20 || instrc.opcode < -1)
        {
            printf("Unknown opcode: %d\n", instrc.opcode);
        }
		return 0;
		break;
	}
};

instrc decode_line(const int line_dec, int registers[], int pc) {
	instrc new_instrc = {
		stall, // Opcode
		-1,	  // rd
		-1,	  // rs
		-1,	  // rt
		-1,	  // imm
		0 // is i type
	};

	if ((unsigned int)line_dec > 0xFFFFFFFF) {
		puts("Line is corrupted!\n");
		return new_instrc;
	}

	int opcode_int = ((unsigned)(0xFF000000 & line_dec) >> 24);
	int rd = (0x00F00000 & line_dec) >> 20;
	int rs = (0x000F0000 & line_dec) >> 16;
	int rt = (0x0000F000 & line_dec) >> 12;
	int imm = (0x00000FFF & line_dec);

	new_instrc.opcode = (opcode_t)opcode_int;
	new_instrc.rd = rd;
	new_instrc.rs = rs;
	new_instrc.rt = rt;
	new_instrc.imm = get_signed_imm(imm);
    new_instrc.pc = pc;
	// Set a flag if imm is expected to be used
	if (new_instrc.rd == 1 || new_instrc.rs == 1 || new_instrc.rt == 1) {
		new_instrc.is_i_type = 1;
	};
	return  new_instrc;
}

void store_regs_to_file(int core_id, int regs[NUM_OF_REGS],const char *output_files[]) {
    char file_name[100];
	snprintf(file_name, sizeof(file_name), "%s", output_files[1 + core_id]);
    printf("Storing core#%d regs  to %s\n", core_id, file_name);
    FILE *file;
	#ifdef LINUX_MODE
		file = fopen(file_name, "w");
	#else
		if (fopen_s(&file, file_name, "w") != 0) {
			file = NULL;
		}
	#endif
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s for writing\n", file_name);
        exit(1);
    }

    for (int i = 2; i < NUM_OF_REGS; i++) {
        fprintf(file, "%08X\n", regs[i]); // Write each int as an 8-digit hex number
    }

    fclose(file);
}

void store_stats_to_file(int core_id, int clk, int instc, int rhit, int whit, int rmis, int wmis, int dec_stall, int mem_stall, const char *output_files[]) {
    char file_name[100];
	snprintf(file_name, sizeof(file_name), "%s", output_files[18 + core_id]);
    printf("Storing core#%d stats to %s\n", core_id, file_name);
    FILE* file;
	#ifdef LINUX_MODE
		file = fopen(file_name, "w");
	#else
		fopen_s(&file, file_name, "w");
	#endif
    if (file == NULL) {
        fprintf(stderr, "Error opening file %s for writing\n", file_name);
        exit(1);
    }

    fprintf(file, "cycles %d\n", clk);
    fprintf(file, "instructions %d\n", instc);
    fprintf(file, "read_hit %d\n", rhit);
    fprintf(file, "write_hit %d\n", whit);
    fprintf(file, "read_miss %d\n", rmis);
    fprintf(file, "write_miss %d\n", wmis);
    fprintf(file, "decode_stall %d\n", dec_stall);
    fprintf(file, "mem_stall %d\n", mem_stall);

    fclose(file);
}

void progress_reg(register_line_s *reg)
{
    reg->instrc_q = reg->instrc_d;
    reg->data_q   = reg->data_d;
    reg->pc_q     = reg->pc_d;
}

void append_trace_line(FILE *file, int clk, int fetch, instrc decode, instrc exec, instrc mem, instrc wb, int registers[NUM_OF_REGS]) {
    if (file == NULL) {
        fprintf(stderr, "Error: file pointer is NULL\n");
        exit(1);
    }

	// Remove the commented lines
    // fprintf(file, "%03X ", fetch == -1 ? "---" : fetch);
    // fprintf(file, "%03X ", decode.opcode == stall || decode.pc == -1 ? "---" : decode.pc);
    // fprintf(file, "%03X ", exec.opcode == stall   || exec.pc == -1 ? "---" : exec.pc);
    // fprintf(file, "%03X ", mem.opcode == stall    || mem.pc == -1 ? "---" : mem.pc);
    // fprintf(file, "%03X ", wb.opcode == stall     || wb.pc == -1 ? "---" : wb.pc);

	char buffer[10]; // Adjust the size as needed

    fprintf(file, "%d ", clk);

	snprintf(buffer, sizeof(buffer), "%03X", fetch);
	fprintf(file, "%s ", fetch == -1 ? "---" : buffer);

	snprintf(buffer, sizeof(buffer), "%03X", decode.pc);
	fprintf(file, "%s ", ((decode.opcode == stall) && (decode.pc == fetch)) || decode.pc < 0 || clk < 1 ? "---" : buffer);

	snprintf(buffer, sizeof(buffer), "%03X", exec.pc);
	fprintf(file, "%s ", ((exec.opcode == stall) && (exec.pc == decode.pc)) || exec.pc < 0 || clk < 2 ? "---" : buffer);

	snprintf(buffer, sizeof(buffer), "%03X", mem.pc);
	fprintf(file, "%s ", ((mem.opcode == stall) && (mem.pc == exec.pc)) || mem.pc < 0 || clk < 3 ?  "---" : buffer);

	snprintf(buffer, sizeof(buffer), "%03X", wb.pc);
	fprintf(file, "%s ", ((wb.opcode == stall) && (wb.pc == mem.pc)) || wb.pc < 0 ||  clk < 4 ? "---" : buffer);
    for (int i = 2; i < 16; i++) {
        fprintf(file, "%08X ", registers[i]);
    }

    fprintf(file, "\n");
}

FILE** create_trace_files(const char *output_files[]) {
    FILE** files = malloc(NUM_CORES * sizeof(FILE*));
    if (files == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }

    for (int i = 0; i < NUM_CORES; i++) {
        char file_name[100];
		snprintf(file_name, sizeof(file_name), "%s", output_files[5 + i]);


		#ifdef LINUX_MODE
			files[i] = fopen(file_name, "w");
			if (files[i] == NULL) {
				fprintf(stderr, "Error opening file %s for writing\n", file_name);
				exit(1);
			}
		#else
			fopen_s(&files[i], file_name, "w");
			if (files[i] == NULL) {
				fprintf(stderr, "Error opening file %s for writing\n", file_name);
				exit(1);
			}
		#endif
    }

    return files;
}



void stall_reg(register_line_s *reg)
{
    reg->instrc_d = reg->instrc_q; // handle memory to the same instrc next clk
    reg->pc_d = reg->pc_q;
    reg->data_d = reg->data_q;
}


const char* opcode_to_string(opcode_t opcode) {
    switch (opcode) {
        case add: return "add";
        case sub: return "sub";
        case and: return "and";
        case or: return "or";
        case xor: return "xor";
        case mul: return "mul";
        case sll: return "sll";
        case sra: return "sra";
        case srl: return "srl";
        case beq: return "beq";
        case bne: return "bne";
        case blt: return "blt";
        case bgt: return "bgt";
        case ble: return "ble";
        case bge: return "bge";
        case jal: return "jal";
        case lw: return "lw";
        case sw: return "sw";
        case halt: return "halt";
        case stall: return "stall";
        default: return "unknown";
    }
}

int get_reg_val(int reg, int registers[], int imm) {
	if (reg == 0) {
		return 0;
	}
	if (reg == 1) {
		return imm;
	}
	return registers[reg];
}