#include "event_recorder.h"
#include "timing_event.h"
#include "zsim.h"
#include "timing_flexclusive_cache.h"

//should work like exclusive cache
//Follow mod listed in timing exclusive cache
//If need to use for multiprogramming benchmarks
//Also, need to decide statistics for use with these
//files


// Events
class HitEvent : public TimingEvent {
    private:
        timing_flexclusive_cache* cache;

    public:
        HitEvent(timing_flexclusive_cache* _cache,  uint32_t postDelay, int32_t domain) : TimingEvent(0, postDelay, domain), cache(_cache) {}

        void simulate(uint64_t startCycle) {
            cache->simulateHit(this, startCycle);
        }
};

class MissStartEvent : public TimingEvent {
    private:
        timing_flexclusive_cache* cache;
    public:
        uint64_t startCycle; //for profiling purposes
        MissStartEvent(timing_flexclusive_cache* _cache,  uint32_t postDelay, int32_t domain) : TimingEvent(0, postDelay, domain), cache(_cache) {}
        void simulate(uint64_t startCycle) {cache->simulateMissStart(this, startCycle);}
};

class MissResponseEvent : public TimingEvent {
    private:
        timing_flexclusive_cache* cache;
        MissStartEvent* mse;
    public:
        MissResponseEvent(timing_flexclusive_cache* _cache, MissStartEvent* _mse, int32_t domain) : TimingEvent(0, 0, domain), cache(_cache), mse(_mse) {}
        void simulate(uint64_t startCycle) {cache->simulateMissResponse(this, startCycle, mse);}
};

class MissWritebackEvent : public TimingEvent {
    private:
        timing_flexclusive_cache* cache;
        MissStartEvent* mse;
    public:
        MissWritebackEvent(timing_flexclusive_cache* _cache,  MissStartEvent* _mse, uint32_t postDelay, int32_t domain) : TimingEvent(0, postDelay, domain), cache(_cache), mse(_mse) {}
        void simulate(uint64_t startCycle) {cache->simulateMissWriteback(this, startCycle, mse);}
};

class ReplAccessEvent : public TimingEvent {
    private:
        timing_flexclusive_cache* cache;
    public:
        uint32_t accsLeft;
        ReplAccessEvent(timing_flexclusive_cache* _cache, uint32_t _accsLeft, uint32_t preDelay, uint32_t postDelay, int32_t domain) : TimingEvent(preDelay, postDelay, domain), cache(_cache), accsLeft(_accsLeft) {}
        void simulate(uint64_t startCycle) {cache->simulateReplAccess(this, startCycle);}
};



void timing_flexclusive_cache::initStats(AggregateStat* parentStat) {
    AggregateStat* cacheStat = new AggregateStat();
    cacheStat->init(name.c_str(), "Timing cache stats");
    initCacheStats(cacheStat);

    //Stats specific to timing cache
    profOccHist.init("occHist", "Occupancy MSHR cycle histogram", numMSHRs+1);
    cacheStat->append(&profOccHist);

    //Stats specific to flexclusive cache
    //Other stats are in cache array and maybe elsewhere
    agg_EX_hits.init("agg_EX_hits", "Aggregate hits in Exclusive mode");
    agg_EX_accesses.init("agg_EX_accesses", "Aggregate accesses in Exclusive mode");

    agg_NI_hits.init("agg_NI_hits", "Aggregate hits in Non Inclusive mode");
    agg_NI_accesses.init("agg_NI_accesses", "Aggregate accesses in Non Inclusive mode");

    profHitLat.init("latHit", "Cumulative latency accesses that hit (demand and non-demand)");
    profMissRespLat.init("latMissResp", "Cumulative latency for miss start to response");
    profMissLat.init("latMiss", "Cumulative latency for miss start to finish (free MSHR)");

    cacheStat->append(&agg_EX_hits);
    cacheStat->append(&agg_EX_accesses);
    cacheStat->append(&agg_NI_hits);
    cacheStat->append(&agg_NI_accesses);

    cacheStat->append(&profHitLat);
    cacheStat->append(&profMissRespLat);
    cacheStat->append(&profMissLat);

    parentStat->append(cacheStat);
}



timing_flexclusive_cache::timing_flexclusive_cache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp,
        uint32_t _accLat, uint32_t _invLat, uint32_t mshrs, uint32_t _tagLat, uint32_t _ways, uint32_t _cands, uint32_t _domain, const g_string& _name)
    : Cache(_numLines, _cc, _array, _rp, _accLat, _invLat, _name), numMSHRs(mshrs), tagLat(_tagLat), ways(_ways), cands(_cands){

    lastFreeCycle = 0;
    lastAccCycle = 0;
    assert(numMSHRs > 0);
    activeMisses = 0;
    domain = _domain;
    info("%s: mshrs %d domain %d", name.c_str(), numMSHRs, domain);

  // do the non-inclusive cache access here
  // also need to test multiple levels of non_inclusive
  // timing cache
}

void timing_flexclusive_cache::setasLLC() {  // invoked at cache creation only
  Cache::setLLCflag();  // access parent cache llc set function
}

uint64_t timing_flexclusive_cache::access(MemReq& req){
  // need to implement set dueling to decide between
  // inclusive and exclusive
  // need to implement set dueling as shown by qureshi's paper
  // we need a new access function that
  // accesses data in a non-inclusive/exclusive/line-wise
  // inclusive way also


    EventRecorder* evRec = zinfo->eventRecorders[req.srcId];
    assert_msg(evRec, "TimingCache is not connected to TimingCore");

    TimingRecord writebackRecord, accessRecord;
    writebackRecord.clear();
    accessRecord.clear();
    uint64_t evDoneCycle = 0;
    bool hitflag = false;


  uint64_t respCycle = req.cycle;
  // request and response cycle
  bool skipAccess = cc->startAccess(req);  // may need to skip access due to
                                           // races (NOTE: may change req.type!)
  if (likely(!skipAccess)) {

    bool updateReplacement = (req.type == GETS) || (req.type == GETX);
    int32_t lineId = array->lookup(req.lineAddr, &req, updateReplacement);
    respCycle += accLat;

    CLUState cs = array->getCLU(req.lineAddr);

    if (llc) {
      if (cc->search_inner_banks(req.lineAddr, req.childId))
        req.flags |=
            MemReq::INNER_COPY;  // says that the private caches had a copy
    }

    if(req.type == GETS || req.type == GETX){
        array->updateCounters(req.lineAddr , lineId);
    }

        if (lineId != -1 && (req.type == GETS || req.type == GETX)){ //means it is a hit
             //phase_life_start[lineId] = req.cycle;
             //agg_life_start[lineId] = req.cycle;
             phase_hits[lineId]++;
             agg_hits[lineId]++;
        }

    if (lineId != -1){
          hitflag = true;
    }

    if (cs == EX){
         agg_EX_accesses.inc();
        if (lineId != -1){
          agg_EX_hits.inc();
        }

    }else { ///means NI clusion state
         agg_NI_accesses.inc();
         if (lineId != -1){
           agg_NI_hits.inc();
         }
    }


    if (lineId == -1)  {
      // Make space for new line
      // info ("making space for new line");

      if (cs == EX) {
        if ((req.type == PUTS) || (req.type == PUTX)) {
          Address wbLineAddr;
          lineId = array->preinsert(req.lineAddr, &req,
                                    &wbLineAddr);  // find the lineId to replace
          trace(Cache, "[%s] Evicting 0x%lx", name.c_str(), wbLineAddr);

          // Evictions are not in the critical path in any sane implementation
          // -- we do not include their delays
          // NOTE: We might be "evicting" an invalid line for all we know.
          // Coherence controllers will know what to do
          evDoneCycle = cc->processEviction(req, wbLineAddr, lineId,
                              respCycle);  // 1. if needed, send
                                           // invalidates/downgrades to lower
                                           // level
          array->postinsert(req.lineAddr, &req,
                            lineId);  // do the actual insertion. NOTE: Now we
                                      // must split insert into a 2-phase thing
                                      // because cc unlocks us.
                                      //
          //info("Got EX state for line and allocated for PUTS || PUTX");
        }
        //info("Got EX state for line and not allocated for PUTS || PUTX");
      } else if (cc->shouldAllocate(req)) {
        Address wbLineAddr;
        lineId = array->preinsert(req.lineAddr, &req,
                                  &wbLineAddr);  // find the lineId to replace
        trace(Cache, "[%s] Evicting 0x%lx", name.c_str(), wbLineAddr);

        // Evictions are not in the critical path in any sane implementation --
        // we do not include their delays
        // NOTE: We might be "evicting" an invalid line for all we know.
        // Coherence controllers will know what to do
        evDoneCycle = cc->processEviction(req, wbLineAddr, lineId,
                            respCycle);  // 1. if needed, send
                                         // invalidates/downgrades to lower
                                         // level

        array->postinsert(req.lineAddr, &req,
                          lineId);  // do the actual insertion. NOTE: Now we
                                    // must split insert into a 2-phase thing
                                    // because cc unlocks us.
      }

      if ( lineId != -1){


            if (evRec->hasRecord()) writebackRecord = evRec->popRecord();


            uint64_t lifetime =  (float) ( respCycle - phase_life_start[lineId] ) * 100.00 / (float)(zinfo->phaseLength * zinfo->statsPhaseInterval) ; //histogram partition on cycles per dump
            //assert_msg (lifetime < 100, "lifetimes was more than 100");

            if (lifetime > 99){  //lump greater than dump length lifetimes in the same category
                lifetime = 99;
            }

            phase_lifetimes.inc(lifetime); // approximate histogram of lifetimes of lines in a phase
            agg_lifetimes.inc(lifetime); // approximate histogram of lifetimes of lines in a phase


            if (req.flags & MemReq::DONT_RECORD){ //means was evicted due to prefetch
                if (agg_hits[lineId] > 10) {  //means the line was hot
                   prefetchPollution.inc();
                }
            }


            phase_life_start[lineId] = respCycle;
            agg_life_start[lineId] = respCycle;


            int hits = phase_hits[lineId] ;
            if (hits > 99) hits = 99;

            agg_hit_counter.inc(hits);
            phase_hit_counter.inc(hits);

            agg_hits[lineId] = 0;
            phase_hits[lineId] = 0;

      }
    }


    uint64_t getDoneCycle;
    respCycle = cc->processAccess(req, lineId, respCycle, &getDoneCycle, cs);


    if (lineId != -1){ //means we allocate a line or got a hit. In this case, we need to create a cache event

        if (evRec->hasRecord()) accessRecord = evRec->popRecord();

        // At this point we have all the info we need to hammer out the timing record
        TimingRecord tr = {req.lineAddr << lineBits, req.cycle, respCycle, req.type, nullptr, nullptr}; //note the end event is the response, not the wback

        //info ("Created timing cache record BEBE , acc type is %d, req cycle is %d", req.type, (int)req.cycle);

        //if (getDoneCycle - req.cycle == accLat) {
        if (hitflag && !accessRecord.isValid()){
            // Hit
            assert(!writebackRecord.isValid());
            assert(!accessRecord.isValid());
            uint64_t hitLat = respCycle - req.cycle; // accLat + invLat
            HitEvent* ev = new (evRec) HitEvent(this, hitLat, domain);
            ev->setMinStartCycle(req.cycle);
            tr.startEvent = tr.endEvent = ev;
        } else {
            //assert_msg(getDoneCycle == respCycle, "gdc %ld rc %ld", getDoneCycle, respCycle);
            getDoneCycle = respCycle;
            // Miss events:
            // MissStart (does high-prio lookup) -> getEvent || evictionEvent || replEvent (if needed) -> MissWriteback

            MissStartEvent* mse = new (evRec) MissStartEvent(this, accLat, domain);
            MissResponseEvent* mre = new (evRec) MissResponseEvent(this, mse, domain);
            MissWritebackEvent* mwe = new (evRec) MissWritebackEvent(this, mse, accLat, domain);

            mse->setMinStartCycle(req.cycle);
            mre->setMinStartCycle(getDoneCycle);
            mwe->setMinStartCycle(MAX(evDoneCycle, getDoneCycle));

            // Tie two events to an optional timing record
            // TODO: Promote to evRec if this is more generally useful

            auto connect = [evRec](const TimingRecord* r, TimingEvent* startEv, TimingEvent* endEv, uint64_t startCycle, uint64_t endCycle) {
              //info ("Connecting records");
              assert_msg(startCycle <= endCycle, "start > end? %ld %ld", startCycle, endCycle);
                if (r) {
                    assert_msg(startCycle <= r->reqCycle, "%ld / %ld", startCycle, r->reqCycle);
                    assert_msg(r->respCycle <= endCycle, "%ld %ld %ld %ld", startCycle, r->reqCycle, r->respCycle, endCycle);
                    uint64_t upLat = r->reqCycle - startCycle;
                    uint64_t downLat = endCycle - r->respCycle;

                    if (upLat) {
                        DelayEvent* dUp = new (evRec) DelayEvent(upLat);
                        dUp->setMinStartCycle(startCycle);
                        startEv->addChild(dUp, evRec)->addChild(r->startEvent, evRec);
                    } else {
                        startEv->addChild(r->startEvent, evRec);
                    }

                    if (downLat) {
                        DelayEvent* dDown = new (evRec) DelayEvent(downLat);
                        dDown->setMinStartCycle(r->respCycle);
                        r->endEvent->addChild(dDown, evRec)->addChild(endEv, evRec);
                    } else {
                        r->endEvent->addChild(endEv, evRec);
                    }
                } else {
                    if (startCycle == endCycle) {
                        startEv->addChild(endEv, evRec);
                    } else {
                        DelayEvent* dEv = new (evRec) DelayEvent(endCycle - startCycle);
                        dEv->setMinStartCycle(startCycle);
                        startEv->addChild(dEv, evRec)->addChild(endEv, evRec);
                    }
                }
            };

            // Get path
            connect(accessRecord.isValid()? &accessRecord : nullptr, mse, mre, req.cycle + accLat, getDoneCycle);
            mre->addChild(mwe, evRec);

            // Eviction path
            if (evDoneCycle) {
                connect(writebackRecord.isValid()? &writebackRecord : nullptr, mse, mwe, req.cycle + accLat, evDoneCycle);
            }

            // Replacement path
            if (evDoneCycle && cands > ways) {
                uint32_t replLookups = (cands + (ways-1))/ways - 1; // e.g., with 4 ways, 5-8 -> 1, 9-12 -> 2, etc.
                assert(replLookups);

                uint32_t fringeAccs = ways - 1;
                uint32_t accsSoFar = 0;

                TimingEvent* p = mse;

                // Candidate lookup events
                while (accsSoFar < replLookups) {
                    uint32_t preDelay = accsSoFar? 0 : tagLat;
                    uint32_t postDelay = tagLat - MIN(tagLat - 1, fringeAccs);
                    uint32_t accs = MIN(fringeAccs, replLookups - accsSoFar);
                    //info("ReplAccessEvent rl %d fa %d preD %d postD %d accs %d", replLookups, fringeAccs, preDelay, postDelay, accs);
                    ReplAccessEvent* raEv = new (evRec) ReplAccessEvent(this, accs, preDelay, postDelay, domain);
                    raEv->setMinStartCycle(req.cycle /*lax...*/);
                    accsSoFar += accs;
                    p->addChild(raEv, evRec);
                    p = raEv;
                    fringeAccs *= ways - 1;
                }

                // Swap events -- typically, one read and one write work for 1-2 swaps. Exact number depends on layout.
                ReplAccessEvent* rdEv = new (evRec) ReplAccessEvent(this, 1, tagLat, tagLat, domain);
                rdEv->setMinStartCycle(req.cycle /*lax...*/);
                ReplAccessEvent* wrEv = new (evRec) ReplAccessEvent(this, 1, 0, 0, domain);
                wrEv->setMinStartCycle(req.cycle /*lax...*/);

                p->addChild(rdEv, evRec)->addChild(wrEv, evRec)->addChild(mwe, evRec);
            }


            tr.startEvent = mse;
            tr.endEvent = mre; // note the end event is the response, not the wback
        }
        evRec->pushRecord(tr);




    }else{

          assert (!writebackRecord.isValid());
          assert (!accessRecord.isValid());
    }



  }
  cc->endAccess(req);

  assert_msg(respCycle >= req.cycle,
             "[%s] resp < req? 0x%lx type %s childState %s, respCycle %ld "
             "reqCycle %ld",
             name.c_str(), req.lineAddr, AccessTypeName(req.type),
             MESIStateName(*req.state), respCycle, req.cycle);
  return respCycle;
}




uint64_t timing_flexclusive_cache::highPrioAccess(uint64_t cycle) {
    //assert(cycle >= lastFreeCycle);

    if (lastFreeCycle > cycle){  //hack -- prefetching contention not modelled in timing cache
                                  // we lose about 100 cycles because lastFreeCycle is ahead
                                  //  due to prefetcher accesses

                                  //panic ("last free cycle is %d, the cycle is %d",(int) lastFreeCycle, (int) cycle);
        lastFreeCycle = cycle;
    }
    uint64_t lookupCycle = MAX(cycle, lastAccCycle+1);
    if (lastAccCycle < cycle-1) lastFreeCycle = cycle-1; //record last free run
    lastAccCycle = lookupCycle;
    return lookupCycle;
}

/* The simple things you see here are complicated,
 * I look pretty young but I'm just back-dated...
 *
 * To make this efficient, we do not want to keep priority queues. Instead, a
 * low-priority access is granted if there was a free slot on the *previous*
 * cycle. This means that low-prio accesses should be post-dated by 1 cycle.
 * This is fine to do, since these accesses are writebacks and non critical
 * path accesses. Essentially, we're modeling that we know those accesses one
 * cycle in advance.
 */
uint64_t timing_flexclusive_cache::tryLowPrioAccess(uint64_t cycle) {
    if (lastAccCycle < cycle-1 || lastFreeCycle == cycle-1) {
        lastFreeCycle = 0;
        lastAccCycle = MAX(cycle-1, lastAccCycle);
        return cycle;
    } else {
        return 0;
    }
}

void timing_flexclusive_cache::simulateHit(HitEvent* ev, uint64_t cycle) {
    if (activeMisses < numMSHRs) {
        uint64_t lookupCycle = highPrioAccess(cycle);
        profHitLat.inc(lookupCycle-cycle);
        ev->done(lookupCycle);  // postDelay includes accLat + invalLat
    } else {
        // queue
        ev->hold();
        pendingQueue.push_back(ev);
    }
}

void timing_flexclusive_cache::simulateMissStart(MissStartEvent* ev, uint64_t cycle) {
    if (activeMisses < numMSHRs){
        activeMisses++;
        profOccHist.transition(activeMisses, cycle);

        ev->startCycle = cycle;
        uint64_t lookupCycle = highPrioAccess(cycle);
        ev->done(lookupCycle);
    } else {
        //info("Miss, all MSHRs used, queuing");
        ev->hold();
        pendingQueue.push_back(ev);
    }
}

void timing_flexclusive_cache::simulateMissResponse(MissResponseEvent* ev, uint64_t cycle, MissStartEvent* mse) {
    profMissRespLat.inc(cycle - mse->startCycle);
    ev->done(cycle);
}

void timing_flexclusive_cache::simulateMissWriteback(MissWritebackEvent* ev, uint64_t cycle, MissStartEvent* mse) {
    uint64_t lookupCycle = tryLowPrioAccess(cycle);
    if (lookupCycle) { //success, release MSHR
        assert(activeMisses);
        profMissLat.inc(cycle - mse->startCycle);
        activeMisses--;
        profOccHist.transition(activeMisses, lookupCycle);

        if (!pendingQueue.empty()) {
            //info("XXX %ld elems in pending queue", pendingQueue.size());
            for (TimingEvent* qev : pendingQueue) {
                qev->requeue(cycle+1);
            }
            pendingQueue.clear();
        }
        ev->done(cycle);
    } else {
        ev->requeue(cycle+1);
    }
}

void timing_flexclusive_cache::simulateReplAccess(ReplAccessEvent* ev, uint64_t cycle) {
    assert(ev->accsLeft);
    uint64_t lookupCycle = tryLowPrioAccess(cycle);
    if (lookupCycle) {
        ev->accsLeft--;
        if (!ev->accsLeft) {
            ev->done(cycle);
        } else {
            ev->requeue(cycle+1);
        }
    } else {
        ev->requeue(cycle+1);
    }
}
