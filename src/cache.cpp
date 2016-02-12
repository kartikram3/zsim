/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
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


#include "cache.h"
#include "hash.h"

#include "event_recorder.h"
#include "timing_event.h"
#include "zsim.h"
#include "g_std/g_multimap.h"


Cache::Cache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, const g_string& _name)
    : cc(_cc), array(_array), rp(_rp), numLines(_numLines), accLat(_accLat), invLat(_invLat), name(_name) {}

const char* Cache::getName() {
    return name.c_str();
    return 0;
}

void Cache::setParents(uint32_t childId, const g_vector<MemObject*>& parents, Network* network) {
    cc->setParents(childId, parents, network);
}

void Cache::setChildren(const g_vector<BaseCache*>& children, Network* network) {
    cc->setChildren(children, network);
}

void Cache::setasLLC(){

   llc=true;  //dummy, we should never invoke this
}

void Cache::setLLCflag(){

   llc=true; //sets the llc flag for this cache

}

void Cache::initStats(AggregateStat* parentStat) {
    AggregateStat* cacheStat = new AggregateStat();
    cacheStat->init(name.c_str(), "Cache stats");
    initCacheStats(cacheStat);
    parentStat->append(cacheStat);
}

void Cache::initCacheStats(AggregateStat* cacheStat) {
    cc->initStats(cacheStat);
    array->initStats(cacheStat);
    rp->initStats(cacheStat);


    //add line lifetime metadata
   phase_lifetimes.init("phaseLifetime", "per phase cache line per dump lifetimes", 100 ); //size is hardcoded at 100
   agg_lifetimes.init( "aggLifetime",  "aggregate cache line per dump lifetimes", 100);
   agg_hit_counter.init("agghitHist", "aggregate line hit histogram", 100);
   phase_hit_counter.init("phasehitHist", "phase line hit histogram", 100);
   hotInv.init("hotInv", "Number of hot lines invalidated");
   prefetchPollution.init("prefetchPollution", "Number of hot lines evicted due to prefetch");

   cacheStat->append( &phase_lifetimes);
   cacheStat->append( &agg_lifetimes);
   cacheStat->append( &phase_hit_counter);
   cacheStat->append( &agg_hit_counter);
   cacheStat->append( &hotInv );
   cacheStat->append( &prefetchPollution );


}

uint64_t Cache::access(MemReq& req) {
    //we need a new access function that
    //accesses data in a non-inclusive/exclusive/line-wise
    //inclusive way also


    uint64_t respCycle = req.cycle;
              //request and response cycle
              
    if ( req.flags & MemReq::LLC_PREFETCH){
       //info ("Here");
       assert (!llc);
       req.flags = req.flags & ~MemReq::LLC_PREFETCH ;  //works for inclusive
                                            //for other kinds, maybe not
       assert(req.flags & MemReq::PREFETCH);                                     
       return cc->access_next_level(req);
    }

    bool skipAccess = cc->startAccess(req); //may need to skip access due to races (NOTE: may change req.type!)


    if (likely(!skipAccess)){
        bool updateReplacement = (req.type == GETS) || (req.type == GETX);
        int32_t lineId = array->lookup(req.lineAddr, &req, updateReplacement);
        respCycle += accLat;

        if (lineId != -1 && (req.type == GETS || req.type == GETX)){ //means it is a hit
             //phase_life_start[lineId] = req.cycle;
             //agg_life_start[lineId] = req.cycle;
             phase_hits[lineId]++;
             agg_hits[lineId]++;
        }

        //if lineId is not -1, then it is a hit.
        //StlGlobAlloc<std::pair<uint64_t, uint64_t>>x ;
        //std::pair<uint64_t, uint64_t> *  y = x.allocate(sizeof(std::pair<uint64_t, uint64_t>)) ;
        //y->first = 0;
        //y->second = 0;

        //g_map<uint64_t, uint64_t> * temp = zinfo->phase_life_start;
        //g_unordered_map<uint64_t, uint64_t> * temp = new g_unordered_map<uint64_t, uint64_t>();
        //(*zinfo->phase_life_start)[0] = 100;
        //(*temp)[0] = 10;
        //(*temp)[20] = 10;

        //g_unordered_map<uint64_t, uint64_t> * phase_life_start = zinfo->phase_life_start;
        //phase_life_start->insert(std::pair<uint64_t, uint64_t>(req.lineAddr,req.cycle));

//        for ( std::pair<uint64_t, uint64_t> x : *(zinfo->phase_life_start) ){
//              info ("x.first is %d", (int)x.first);
//              info ("x.second is %d", (int)x.second);
//        }



        if (lineId == -1 && cc->shouldAllocate(req)) {
            //Make space for new line
            //info ("making space for new line");

            Address wbLineAddr;
            lineId = array->preinsert(req.lineAddr, &req, &wbLineAddr); //find the lineId to replace
            trace(Cache, "[%s] Evicting 0x%lx", name.c_str(), wbLineAddr);

            //Evictions are not in the critical path in any sane implementation -- we do not include their delays
            //NOTE: We might be "evicting" an invalid line for all we know. Coherence controllers will know what to do
            cc->processEviction(req, wbLineAddr, lineId, respCycle); //1. if needed, send invalidates/downgrades to lower level
            //g_unordered_map<uint64_t, uint64_t> * phase_life_end = zinfo->phase_life_end;
            //phase_life_end->insert(std::pair<uint64_t, uint64_t>(req.lineAddr, respCycle  ));

            array->postinsert(req.lineAddr, &req, lineId); //do the actual insertion. NOTE: Now we must split insert into a 2-phase thing because cc unlocks us.

            uint64_t lifetime =  (float) ( respCycle - phase_life_start[lineId] ) * 100.00 / (float)(zinfo->phaseLength * zinfo->statsPhaseInterval) ; //histogram partition on cycles per dump
            //assert_msg (lifetime < 100, "lifetimes was more than 100");

            //info ("the lifetime is %d", (int)lifetime);
            if (lifetime > 99){  //lump greater than dump length lifetimes in the same category
                lifetime = 99;
            }

            //info ("Here !");
            //

            assert_msg(lifetime >= 0, "Lifetimes is %d", (int)lifetime);
            assert_msg(lifetime < 100, "Lifetimes is %d", (int)lifetime);

            phase_lifetimes.inc(lifetime); // approximate histogram of lifetimes of lines in a phase
            agg_lifetimes.inc(lifetime); // approximate histogram of lifetimes of lines in a phase

            if (req.flags & MemReq::DONT_RECORD){ //means was evicted due to prefetch
                if (agg_hits[lineId] > 10) {  //means the line was hot
                   prefetchPollution.inc();
                }
            }


            phase_life_start[lineId] = respCycle;
            agg_life_start[lineId] = respCycle;


            uint64_t hits = phase_hits[lineId] ;
            //info ("the hits are %d",(int) hits);
            if (hits > 99) hits = 99;

            assert_msg(hits >=0, "The hit count is %d", (int)hits);
            assert_msg(hits <100, "The hit count is %d", (int)hits);

            agg_hit_counter.inc(hits);
            phase_hit_counter.inc(hits);

            agg_hits[lineId] = 0;
            phase_hits[lineId] = 0;

        }



        //respCycle = cc->processAccess(req, lineId, respCycle);
        // Enforce single-record invariant: Writeback access may have a timing
        // record. If so, read it.

        //#if 0
        EventRecorder* evRec = zinfo->eventRecorders[req.srcId];
                                              //src is the source core

        TimingRecord wbAcc;
                        //if writeback happened, then
                       //maybe a timing record was creted at that
                        //time

        wbAcc.clear();

        if (unlikely(evRec && evRec->hasRecord())) {
            wbAcc = evRec->popRecord();
        }

        respCycle = cc->processAccess(req, lineId, respCycle);

        // Access may have generated another timing record. If *both* access
        // and wb have records, stitch them together
        if (unlikely(wbAcc.isValid())) {

            if (!evRec->hasRecord()) {
                // Downstream should not care about endEvent for PUTs
                wbAcc.endEvent = nullptr;
                evRec->pushRecord(wbAcc);
            } else {
                // Connect both events
                //info("weird wbAcc happened");
                TimingRecord acc = evRec->popRecord();
                assert(wbAcc.reqCycle >= req.cycle);
                assert(acc.reqCycle >= req.cycle);
                DelayEvent* startEv = new (evRec) DelayEvent(0);
                DelayEvent* dWbEv = new (evRec) DelayEvent(wbAcc.reqCycle - req.cycle);
                DelayEvent* dAccEv = new (evRec) DelayEvent(acc.reqCycle - req.cycle);
                startEv->setMinStartCycle(req.cycle);
                dWbEv->setMinStartCycle(req.cycle);
                dAccEv->setMinStartCycle(req.cycle);
                startEv->addChild(dWbEv, evRec)->addChild(wbAcc.startEvent, evRec);
                startEv->addChild(dAccEv, evRec)->addChild(acc.startEvent, evRec);

                acc.reqCycle = req.cycle;
                acc.startEvent = startEv;
                // endEvent / endCycle stay the same; wbAcc's endEvent not connected

                evRec->pushRecord(acc);
            }
        }
        //#endif
    }

    cc->endAccess(req);


    assert_msg(respCycle >= req.cycle, "[%s] resp < req? 0x%lx type %s childState %s, respCycle %ld reqCycle %ld",
            name.c_str(), req.lineAddr, AccessTypeName(req.type), MESIStateName(*req.state), respCycle, req.cycle);
    return respCycle;
}

void Cache::startInvalidate() {
    cc->startInv(); //note we don't grab tcc; tcc serializes multiple up accesses, down accesses don't see it
}

uint64_t Cache::finishInvalidate(const InvReq& req) {
  int32_t lineId = array->lookup(req.lineAddr, nullptr, false);

  if (lineId == -1 ){ panic("Problem, as lineId was -1 in cache %s", name.c_str()); cc->unlock_bcc(); return req.cycle; }; //means no invalidate happens
                                        //have to remove this bug
  //info("Doing invalidate !");

  if (agg_hits[lineId] > 10){
      hotInv.inc();
  }

  assert_msg(lineId != -1, "[%s] Invalidate on non-existing address 0x%lx type %s lineId %d, reqWriteback %d", name.c_str(), req.lineAddr, InvTypeName(req.type), lineId, *req.writeback);
    uint64_t respCycle = req.cycle + invLat;
    trace(Cache, "[%s] Invalidate start 0x%lx type %s lineId %d, reqWriteback %d", name.c_str(), req.lineAddr, InvTypeName(req.type), lineId, *req.writeback);
    respCycle = cc->processInv(req, lineId, respCycle); //send invalidates or downgrades to children, and adjust our own state
    trace(Cache, "[%s] Invalidate end 0x%lx type %s lineId %d, reqWriteback %d, latency %ld", name.c_str(), req.lineAddr, InvTypeName(req.type), lineId, *req.writeback, respCycle - req.cycle);

    return respCycle + 1;
}



        void Cache::dumpLifetimeStats(){
          info ("Dumping lifetime stats for cache lines which were never evicted");
          for (auto it = phase_life_start.begin(); it != agg_life_start.end(); it++){
            uint64_t lifetime =  (float) ( zinfo->contentionSim->getCurCycle(0) - it->second ) * 100.00 / (float)(zinfo->phaseLength * zinfo->statsPhaseInterval) ; //histogram partition on cycles per dump
            //assert_msg (lifetime < 100, "lifetimes was more than 100");

            assert_msg (lifetime >= 0, "lifetime of non-evicted line was negative in final dump");  //something is very wrong if this fails
            if (lifetime > 99){  //lump greater than dump length lifetimes in the same category
                lifetime = 99;
            }

            phase_lifetimes.inc(lifetime); // approximate histogram of lifetimes of lines in a phase
            agg_lifetimes.inc(lifetime); // approximate histogram of lifetimes of lines in a phase
          }
        }



//uint64_t Cache::snoop(SnoopReq &req, uint64_t respCycle){
//      cc->startSnoop();
//
//      int32_t lineId = array->lookup(req.lineAddr, nullptr, false);
//      respCycle += accLat;
//
//      if(lineId == -1) {
//            cc->processSnoop(req);
//
//      }          //means the snoop did not find the line
//                 //so continue the snoop
//      else {
//
//
//      }          //means snoop found the line
//
//      return 0;
//}
