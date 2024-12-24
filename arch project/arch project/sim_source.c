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
void round_robin_arbitrator()
{
	static int curr = 0; //NOTE: This sets the first core to be core0, might need to chagne this for the first core that initate a bust transaction
	if (curr == 3) curr = 0; //Wrap
	else curr++;
}