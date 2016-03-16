/** $lic$
 info* Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CACHE_H_
#define CACHE_H_

#include "cache_arrays.h"
#include "coherence_ctrls.h"
#include "g_std/g_string.h"
#include "g_std/g_vector.h"
#include "memory_hierarchy.h"
#include "repl_policies.h"
#include "stats.h"
#include "g_std/g_unordered_map.h"
#include "zsim.h"
#include "contention_sim.h"

class Network;

/* General coherent modular cache. The replacement policy and cache array are
 * pretty much mix and match. The coherence controller interfaces are general
 * too, but to avoid virtual function call overheads we work with MESI
 * controllers, since for now we only have MESI controllers
 */

class Cache : public BaseCache {
    protected:
        CC* cc;
        CacheArray* array;
        ReplPolicy* rp;

        uint32_t numLines;

        //Latencies
        uint32_t accLat; //latency of a normal access (could split in get/put, probably not needed)
        uint32_t invLat; //latency of an invalidation

        g_string name;

        bool llc=false; //sets the cache as llc



    public:


        g_unordered_map<uint64_t, uint64_t> phase_life_start;
        g_unordered_map<uint64_t, uint64_t> phase_hits;


        g_unordered_map<uint64_t, uint64_t> agg_life_start;
        g_unordered_map<uint64_t, uint64_t> agg_hits;

        VectorCounter phase_lifetimes;
        VectorCounter phase_hit_counter;
        VectorCounter agg_lifetimes;
        VectorCounter agg_hit_counter;
        Counter hotInv;
        Counter prefetchPollution;
        Counter should_be_exclusive_lines;
        Counter excl_interval; //time between fetching two potentially exclusive lines

        Cache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, const g_string& _name);

        const char* getName();
        void setParents(uint32_t _childId, const g_vector<MemObject*>& parents, Network* network);
        void setChildren(const g_vector<BaseCache*>& children, Network* network);
        void initStats(AggregateStat* parentStat);

        virtual uint64_t access(MemReq& req);
        virtual void setasLLC();
        void setLLCflag();


        //NOTE: reqWriteback is pulled up to true, but not pulled down to false.
        virtual uint64_t invalidate(const InvReq& req) {
            startInvalidate();
            return finishInvalidate(req);
        }

        //virtual uint64_t snoop(SnoopReq &req, uint64_t respCycle);

        void dumpLifetimeStats();

        enum cache_type { INCLUSIVE, NON_INCLUSIVE, EXCLUSIVE, FLEXCLUSIVE, LINE_BASED_CLUSION };

    protected:
        void initCacheStats(AggregateStat* cacheStat);

        void startInvalidate(); // grabs cc's downLock

        uint64_t finishInvalidate(const InvReq& req); // performs inv and releases downLock

        virtual uint64_t snoop(){  return 0; } ;

        virtual uint64_t lookup(const Address lineAddr){
            int32_t lineId = array->lookup_norpupdate(lineAddr);
            if ((lineId != -1) && cc->isValid(lineId)) return 1; //means we found an address value
                                                                 //which is valid
            else return -1;
         } ; //check the value in the cache
};
#endif  // CACHE_H_
