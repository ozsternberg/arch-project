#include "sim.h"


void progress_register_data(register_s* reg) {
	reg->q = reg->d;
}


// Get the tag,set,offset from an address
cache_addr_s parse_addr(int addr) 
{
	cache_addr_s cache_addr;
	int tag_shift = SET_WIDTH + OFFSET_WIDTH;
	int set_shift = OFFSET_WIDTH;
	cache_addr.tag =    (unsigned int)((addr & 0x00FFF00) >> tag_shift);
	cache_addr.set =    (unsigned int)(addr & 0xFF        >> set_shift);
	cache_addr.offset = (unsigned int)(addr & 0x3);
	return cache_addr;
}

// Round robin arbitrator implementation
int round_robin_arbitrator()
{
	static int curr = 0; //NOTE: This sets the first core to be core0, might need to change this for the first core that initate a bus transaction
	int curr_r = curr;
	if (curr == 3) curr = 0; //Wrap
	else curr++;
	return curr_r;
}

bus_cmd_s cores(bus_cmd_s bus_req, int exclude, int gnt_core_id)
{
	int core_issued_flush = 0;
	bus_cmd_s core_cmd;
	bus_cmd_s core_cmd_rtr;
	for (int core_id = 0; core_id < NUM_CORES; core_id++) 
  {
		if (core_id == gnt_core_id && exclude == 1) continue;
		core_cmd = core(core_id,0,bus_req);
    if (core_cmd.bus_cmd == kFlush)  // We rely on cores that have modifed data to flush on read - that is the only thing we care about   
		{
			if (core_issued_flush) puts("Error - two cores flushed on the same time!");
			core_issued_flush = 1;
			core_cmd_rtr = core_cmd;
		}
	}
	return core_cmd_rtr;
}