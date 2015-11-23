#include "event_recorder.h"
#include "timing_event.h"
#include "zsim.h"
#include "flexclusive_cache.h"

flexclusive_cache :: flexclusive_cache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, uint32_t mshrs,   uint32_t tagLat, uint32_t ways, uint32_t cands, uint32_t _domain, const g_string& _name)
    : Cache(_numLines, _cc, _array, _rp, _accLat, _invLat, _name) {
//do the non-inclusive cache access here
//also need to test multiple levels of non_inclusive
//timing cache


}

uint64_t flexclusive_cache :: access(MemReq& req){

  //uint64_t respCycle = req.cycle;
  //bool skipAccess = cc->startAccess(req); //may need to skip access due to races (NOTE: may change req.type!)

  //need to implement set dueling to decide between
  //inclusive and exclusive
  //need to implement set dueling as shown by qureshi's paper

  return req.cycle;

}
