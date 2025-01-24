#include "sim.h"
#include "core_sim.h"

// The dsram and tsram should be of only once core
cache_query_rsp_s cache_query(int dsram[][], tsram_entry tsram[], int addr,opcode op, int data,int progress_clk)
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

mem_rsp_s handle_mem(int dsram[][], tsram_entry tsram[], int addr,opcode op, int data, int progress_clk,  cache_state_t * cache_state, bus_cmd_s bus, int gnt) 
{
    mem_rsp_s mem_rsp = {0};
    cache_query_rsp_s cache_query_rsp = cache_query(dsram,tsram,addr,op,data,progress_clk);
    cache_hit_t hit_type = cache_query_rsp.hit_type;
    mem_rsp.data         = cache_query_rsp.data;
    int req = op == sw || op == lw || cache_state != kIdle ? 1 : 0;

    bus_routine_rsp_s bus_routine_rsp = bus_routine(cache_state, hit_type, bus, gnt, mem_rsp, req);
    
    switch (cache_state)
    {
        case kIdle:
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

    

}