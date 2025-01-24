#include "sim.h"
#include "core_sim.h"

// The dsram and tsram should be of only once core
cache_query_rsp_s cache_query(int dsram[][BLOCK_SIZE], tsram_entry tsram[], int addr, opcode op, int data, int progress_clk)
{
    if (op != lw && op != sw) {
        // Handle no sw/lw
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

mem_rsp_s handle_mem(int dsram[][], tsram_entry tsram[], int addr,opcode op, int data, int progress_clk,  cache_state_t * cache_state, core_state_t * core_state,bus_cmd_s bus, int gnt, int core_id) 
{
    cache_query_rsp_s cache_query_rsp = cache_query(dsram,tsram,addr,op,data,progress_clk);
    cache_hit_t hit_type = cache_query_rsp.hit_type;

    int core_req_trans = op == sw || op == lw || cache_state != kIdle ? 1 : 0;

    bus_routine_rsp_s bus_routine_rsp = bus_routine(dsram,tsram,bus,progress_clk,gnt,core_state,core_id,core_req_trans,addr,mem_rsp.data,hit_type);
	mem_rsp_s mem_rsp = {cache_query_rsp.data, 0, bus_routine_rsp.bus_cmd};

    switch (*cache_state)
    {
        case kIdle:
            mem_rsp.stall = 0;
            if (hit_type == kHit) 
            break; // If hit do nothing
            
            // If gnt is 0, stall
            if (gnt == 0) 
            {
                mem_rsp.stall = 1;
                *cache_state = kWaitForGnt;
                break;
            }
            
            if (hit_type == kWrHitShared) break; //If gnt == 1 and hit is shared do stay Idle

            else // If gnt == 1 and miss wait for flush or send
            {
                mem_rsp.stall = 1;
                *cache_state = kWaitForFlush;
                break;
            }

        case kWaitForGnt:
            mem_rsp.stall = 1;
            if (gnt == 0) break; // If gnt is 0, stall

            if (hit_type == kWrHitShared) //If gnt == 1 and hit is shared do stay Idle
            {
                *cache_state = kIdle;
                break;
            }

            else // If gnt == 1 and miss, wait for flush receive or send
            {
                *cache_state = kWaitForFlush;
                break;
            }

        case kWaitForFlush:
            mem_rsp.stall = 1;
            if (bus_routine_rsp.data_rtn == 0) break; // If no data is returned, stall

            if (bus_routine_rsp.data_rtn == 1)
            {
                if (bus_routine_rsp.bus_cmd.bus_addr != addr) 
                {
                    printf("Data is received from wrong address\n");
                }
                *cache_state = kIdle;
            }

            break;
    }
    return mem_rsp;
}

bus_routine_rsp_s bus_routine(int dsram[][BLOCK_SIZE], tsram_entry tsram[],bus_cmd_s bus, int progress_clock, int gnt, core_state_t * core_state, int core_id, int core_req_trans, int addr, int data, cache_hit_t hit_type)
{
    bus_cmd_s bus;

    // Define core
    static int core_send_counter[NUM_CORES]    = {0};
    static int core_receive_counter[NUM_CORES] = {0};

    static int offset[NUM_CORES];
    static int index[NUM_CORES];
    static int tag[NUM_CORES];
    static int bus_addr[NUM_CORES];
    static tsram_entry entry[NUM_CORES]; 
    static core_state_t next_state[NUM_CORES];
    static mesi_state_t entry_state[NUM_CORES];

    static int flush_done = 0; // Raise signal when flush is done

    // need to handle address
    // need to handle synchronization
    // check for Send and receive done flags
    // wrap procedure as a function

    // Bus routine
	int receive_done = 0;

	switch (*core_state){
    	case Idle:
    
    		if(gnt = 0)
    		{	
    			bus_addr[core_id] = bus.bus_addr;
    			// Split bus address to tsram and dsram parameters
    			offset[core_id] = parse_addr(bus_addr[core_id]).offset;
                index[core_id] = parse_addr(bus_addr[core_id]).set;
    			tag[core_id] = parse_addr(bus_addr[core_id]).tag;
    			entry[core_id] = tsram[index[core_id]];
    			next_state[core_id] = Idle;

    			if((entry[core_id].tag == tag[core_id]) && (entry[core_id].state =! Invalid)) // Check if passive hit
    			{
    				if ((bus.bus_cmd == kBusRdX) || (bus.bus_cmd == kBusRd)) 
    				{
    					if((entry[core_id].state == Modified) && (progress_clock == 1))  // If the data is modified- Send
    					{ 
    						core_send_counter[core_id] = 0; 
    						next_state[core_id] = Send ; 
    					}
    				} 
    				if (bus.bus_cmd == kBusRd) //set shared wire
    				{
    					bus.bus_share = 1 ;
    					entry_state[core_id] = Shared  ; // If command is kBusRd-change state to shared  - from all states(exclusive, modified or shared)
    				}
    				else if (bus.bus_cmd == kBusRdX)
    				{
    					entry_state[core_id] = Invalid ; // If BusRdX change to Invalid  - from all states(exclusive, modified or shared)
    				}
    				if(progress_clock == 1) // change core state and entry[core_id] state on next clock cycle
    				{
    					entry[core_id].state = entry_state[core_id];
    					*core_state = next_state[core_id];
    				}
    			}
    			break;
    		}
    		if(gnt = 1)  // check for transaction 
    		{
    			bus.bus_origid = core_id; // Set bus_origid to core_id
    			if(core_req_trans = 1) 
    			{
    				bus.bus_addr = addr; // save a copy of the trans address
    				// Split bus address to tsram and dsram parameters
    				offset[core_id] = parse_addr(bus.bus_addr).offset;
    				index[core_id] = parse_addr(bus.bus_addr).set;
    				tag[core_id] = parse_addr(bus.bus_addr).tag;
    				entry[core_id] = tsram[index[core_id]];
    				entry_state[core_id] = entry[core_id].state;
    				int **update_mem = dsram;
    
    				if(hit_type = kRdMiss)  // If read miss- set bus_cmd to BusRd
    				{
    					bus.bus_cmd = kBusRd;
    					next_state[core_id] = WaitForFlush; // A transaction is made, now wait for flash
    
    				}
    				if(hit_type = kWrMiss) // If write miss - set bus_cmd to BusRdX
    				{
    					bus.bus_cmd = kBusRdX;
    					next_state[core_id] = WaitForFlush; // A transaction is made, now wait for flash
    				}
    				if (hit_type = kWrHitShared) // If cache writeHit on shared data - set bus_cmd to kBusRdX to invalidate data on other caches
    				{
    					bus.bus_cmd = kBusRdX;
    					entry_state[core_id] = Modified;
    					update_mem[index[core_id]][offset[core_id]] = data;
    				}

    				if (hit_type = kModifiedMiss) // If cache writeMiss modified data - set bus_cmd to flush
    				{
    					bus.bus_cmd = kFlush;
    					bus.bus_data = dsram[index[core_id]][core_send_counter[core_id]];
    					next_state[core_id] = Send;
    					core_send_counter[core_id] = 1;
    				}
    				else
    				{
    					printf("Transaction is made, but no transaction is required\n");	
    				}
    				if(progress_clock = 1)
    				{
    					entry[core_id].state = entry_state[core_id];
    					*core_state = next_state[core_id];
    					entry[core_id].tag = tag[core_id];
    					dsram[index[core_id]][offset[core_id]] = update_mem[index[core_id]][offset[core_id]];
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
    		if(bus.bus_cmd == kFlush)
    		{
    			if(bus.bus_addr != (addr - offset[core_id]))
    			{
    				printf("Data is Received from wrong address\n");
    			}
    			next_state[core_id] = Receive;
    			core_receive_counter[core_id] = 0;
    		}
    		if(progress_clock == 1)
    		{
    			*core_state = next_state[core_id];
    		}
    		break;
    
    	case Send:

    		next_state[core_id] = Send;
    		flush_done = 0; // Raise signal when flush is done
    		bus.bus_origid = core_id ;
    		bus.bus_cmd = kFlush;
    		bus.bus_data = dsram[index[core_id]][core_send_counter[core_id]];
    		bus.bus_addr = bus_addr[core_id] + core_send_counter[core_id] - offset[core_id];
    		if(progress_clock == 1)
    		{
    			core_send_counter[core_id]++;
    			if(core_send_counter[core_id] == BLOCK_SIZE -1)
    			{
    				core_send_counter[core_id] = 0;
    				entry[core_id].state = Idle;
    				flush_done = 1;
    				entry[core_id].state = Shared;
    				break;
    			}	
    			else
    			{
    				entry[core_id].state = next_state[core_id];
    			}
    		}
    
    		break;
    
    	case Receive:
    		if(bus.bus_addr == (addr + core_receive_counter[core_id] - offset[core_id]) && progress_clock == 1)
    		{
    			receive_done = 0;
    			dsram[index[core_id]][core_receive_counter[core_id]] = bus.bus_data;
    			core_receive_counter[core_id]++;

    			if(core_receive_counter[core_id] == BLOCK_SIZE -1)
    			{
    				if(bus.bus_share = 0)
    				{
    					entry[core_id].state = Exclusive; // If no other cache asserts shared ,set entry_state[core_id] to exclusive
    				}
    				else
    				{
    					entry[core_id].state = Shared; // If another cache asserts shared set entry[core_id] to shared
    				}
    				core_receive_counter[core_id] = 0;
    				receive_done = 1;
    				next_state[core_id] = Idle;
    				break;
    			}
    		}

            else printf("Data is received from address - %d and the original address is - %d\n", bus.bus_addr, addr + core_receive_counter[core_id] - offset[core_id]);
    
    		break;
    }

    bus_routine_rsp_s bus_routine_rsp = {bus, receive_done};
    return bus_routine_rsp;
};