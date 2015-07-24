#include "event_recorder.h"
#include "timing_event.h"
#include "zsim.h"
#include "non_inclusive_cache.h"

non_inclusive_cache :: non_inclusive_cache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, uint32_t mshrs,   uint32_t tagLat, uint32_t ways, uint32_t cands, uint32_t _domain, const g_string& _name)
    : Cache(_numLines, _cc, _array, _rp, _accLat, _invLat, _name)  {


//do the non-inclusive cache access here
//also need to test multiple levels of non_inclusive 
//timing cache     




}


uint64_t non_inclusive_cache::access(MemReq& req){

          return req.cycle; //dummy fn ... replace with 
                            //non inclusive cache fn
                            //needs its own coherence controller

          

}


/*
#Non-inclusive cache

We write a non-inclusive cache module

#Exclusive cache


#Flexclusive cache

Tracks the working set size and changes the
inclusion property accordingly


#Line-based clusive cache

Each line has its own inclusion data.

*/
