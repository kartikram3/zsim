#include "event_recorder.h"
#include "timing_event.h"
#include "zsim.h"
#include "line_clusive_cache.h"

line_clusive_cache :: line_clusive_cache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, uint32_t mshrs,   uint32_t tagLat, uint32_t ways, uint32_t cands, uint32_t _domain, const g_string& _name)
    : Cache(_numLines, _cc, _array, _rp, _accLat, _invLat, _name) {

  //do the non-inclusive cache access here
  //also need to test multiple levels of non_inclusive
  //timing cache

}



