#include "event_recorder.h"
#include "timing_event.h"
#include "zsim.h"
#include "flexclusive_cache.h"

flexclusive_cache::flexclusive_cache(uint32_t _numLines, CC* _cc,
                                     CacheArray* _array, ReplPolicy* _rp,
                                     uint32_t _accLat, uint32_t _invLat,
                                     uint32_t mshrs, uint32_t tagLat,
                                     uint32_t ways, uint32_t cands,
                                     uint32_t _domain, const g_string& _name)
    : Cache(_numLines, _cc, _array, _rp, _accLat, _invLat, _name) {
  // do the non-inclusive cache access here
  // also need to test multiple levels of non_inclusive
  // timing cache
}

void flexclusive_cache::setasLLC() {  // invoked at cache creation only
  Cache::setLLCflag();  // access parent cache llc set function
}

uint64_t flexclusive_cache::access(MemReq& req){
  // need to implement set dueling to decide between
  // inclusive and exclusive
  // need to implement set dueling as shown by qureshi's paper
  // we need a new access function that
  // accesses data in a non-inclusive/exclusive/line-wise
  // inclusive way also

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
          cc->processEviction(req, wbLineAddr, lineId,
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
        cc->processEviction(req, wbLineAddr, lineId,
                            respCycle);  // 1. if needed, send
                                         // invalidates/downgrades to lower
                                         // level

        array->postinsert(req.lineAddr, &req,
                          lineId);  // do the actual insertion. NOTE: Now we
                                    // must split insert into a 2-phase thing
                                    // because cc unlocks us.
      }
    }


    // respCycle = cc->processAccess(req, lineId, respCycle);
    // Enforce single-record invariant: Writeback access may have a timing
    // record. If so, read it.

    //#if 0
    EventRecorder* evRec = zinfo->eventRecorders[req.srcId];
    // src is the source core
    TimingRecord wbAcc;
    // if writeback happened, then
    // maybe a timing record was creted at that
    // time
    wbAcc.clear();
    if (unlikely(evRec && evRec->hasRecord())) {
      wbAcc = evRec->popRecord();
    }

    respCycle = cc->processAccess(req, lineId, respCycle, nullptr, cs);

    // Access may have generated another timing record. If *both* access
    // and wb have records, stitch them together
    if (unlikely(wbAcc.isValid())) {
      if (!evRec->hasRecord()) {
        // Downstream should not care about endEvent for PUTs
        wbAcc.endEvent = nullptr;
        evRec->pushRecord(wbAcc);
      } else {
        // Connect both events
        // info("weird wbAcc happened");
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

  assert_msg(respCycle >= req.cycle,
             "[%s] resp < req? 0x%lx type %s childState %s, respCycle %ld "
             "reqCycle %ld",
             name.c_str(), req.lineAddr, AccessTypeName(req.type),
             MESIStateName(*req.state), respCycle, req.cycle);
  return respCycle;
}
