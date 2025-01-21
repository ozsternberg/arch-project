#include "sim.h"
#include "core_sim.h"

// The dsram and tsram should be of only once core
mesi_state_t cache_access(int dsram[][], tsram_entry tsram[], int addr,opcode op, int data,int progress_clk)
{
    if (op != lw && op != sw) {
        // Handle no sw/lw
    }
    cache_addr_s cache_addr = parse_addr(addr);

    mesi_state_t data_state = tsram[cache_addr.set].state;
    int hit = tsram[cache_addr.set].tag == cache_addr.tag ? 1 : 0;

    cache_hit_t hit_type;

    if (data_state == Modified && hit == 0)
    {
            hit_type = kModifiedMiss;
    }

    else if (op == lw)
    {
        if (hit && data_state != Invalid) // Rd hit
        {
            int word = dsram[cache_addr.set][cache_addr.offset];
            hit_type = kHit;
        }

        else // Rd miss
        {
            hit_type = kRdMiss;
        }
    }

    else
    {
        if(hit) // Wr hit
        {
            if (progress_clk)
            {
                dsram[cache_addr.set][cache_addr.offset] = data;
                tsram[cache_addr.set].state = Modified;
            }

            hit_type = kHit;
        }

        else
        {
            hit_type = kWrMiss;
        }
    }

}
