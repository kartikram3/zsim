/* non inclusive cache class */

#ifndef  EXCLUSIVE_CACHE_H
#define EXCLUSIVE CACHE_H

#include "breakdown_stats.h"
#include "cache.h"

class exclusive_cache: public Cache{
    protected :


    public :

        exclusive_cache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, uint32_t mshrs,  uint32_t tagLat, uint32_t ways, uint32_t cands, uint32_t _domain, const g_string& _name);

        uint64_t access(MemReq &req);

        uint64_t pushEvictedData();

        void setasLLC();

};

#endif
