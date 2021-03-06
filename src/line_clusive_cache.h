//do a flexclusive cache that models different inclusion/exclusion properties

#ifndef  LINE_CLUSIVE_CACHE_
#define LINE_CLUSIVE_CACHE_

#include "breakdown_stats.h"
#include "cache.h"


class line_clusive_cache: public Cache{

    protected :

        int x=10;

    public :

        line_clusive_cache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, uint32_t mshrs,  uint32_t tagLat, uint32_t ways, uint32_t cands, uint32_t _domain, const g_string& _name);

        uint64_t access(MemReq& req);

        void setasLLC();


};

#endif
