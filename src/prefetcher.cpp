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

#include "prefetcher.h"
#include "bithacks.h"
#include "event_recorder.h"
#include "zsim.h"
#include "timing_event.h"

//#define DBG(args...) info(args)
#define DBG(args...)

void StreamPrefetcher::setParents(uint32_t _childId,
                                  const g_vector<MemObject*>& parents,
                                  Network* network) {
  childId = _childId;
  if (parents.size() != 1) panic("Must have one parent");
  if (network) panic("Network not handled");
  parent = parents[0];
}

void StreamPrefetcher::setChildren(const g_vector<BaseCache*>& children,
                                   Network* network) {
  if (children.size() != 1) panic("Must have one children");
  if (network) panic("Network not handled");
  child = children[0];
}

void StreamPrefetcher::initStats(AggregateStat* parentStat) {
  AggregateStat* s = new AggregateStat();
  s->init(name.c_str(), "Prefetcher stats");
  profAccesses.init("acc", "Accesses");
  s->append(&profAccesses);
  profPrefetches.init("pf", "Issued prefetches");
  s->append(&profPrefetches);
  profDoublePrefetches.init("dpf", "Issued double prefetches");
  s->append(&profDoublePrefetches);
  profPageHits.init("pghit", "Page/entry hit");
  s->append(&profPageHits);
  profHits.init("hit", "Prefetch buffer hits, short and full");
  s->append(&profHits);
  profShortHits.init("shortHit", "Prefetch buffer short hits");
  s->append(&profShortHits);
  profStrideSwitches.init("strideSwitches", "Predicted stride switches");
  s->append(&profStrideSwitches);
  profLowConfAccs.init("lcAccs", "Low-confidence accesses with no prefetches");
  s->append(&profLowConfAccs);
  parentStat->append(s);
}

uint64_t StreamPrefetcher::access(MemReq& req) {
  uint32_t origChildId = req.childId;
  req.childId = childId;
  bool pf1, pf2;

  pf1 = false;
  pf2 = false;

  if (req.type != GETS) {
    // info ("IGNORING PREFETCH, the request type is %d", req.type);
    return parent->access(req);  // other reqs ignored, including stores
  }

  // uint32_t cycle_req = req.cycle;

  // info ("STARTING PREFETCH with access type %d", req.type  );

  profAccesses.inc();

  uint64_t respCycle = parent->access(req);
  uint64_t reqCycle = req.cycle;

  assert_msg (*(req.state)  == S || *(req.state) == E, "The childstate is not S or E !")


  //-------- get trace from current access ----------//
  EventRecorder* evRec =
      zinfo->eventRecorders[req.srcId];  // remove the current record
  TimingRecord tr;
  tr.clear();

  if (evRec->hasRecord()) {
    tr = evRec->popRecord();
    // info ("popped record, with reqcycle = %d", tr.reqCycle);
  }
  assert (tr.type != PUTS);
  assert (tr.type != PUTX);

  // info ("req cycle is %d", reqCycle);
  //-------- got trae ---------//

  Address pageAddr = req.lineAddr >> 6;
  uint32_t pos = req.lineAddr & (64 - 1);
  uint32_t idx = 16;
  // This loop gets unrolled and there are no control dependences. Way faster
  // than a break (but should watch for the avoidable loop-carried dep)
  for (uint32_t i = 0; i < 16; i++) {
    bool match = (pageAddr == tag[i]);
    idx = match ? i : idx;  // ccmov, no branch
  }
  DBG("%s: 0x%lx page %lx pos %d", name.c_str(), req.lineAddr, pageAddr, pos);

  if (idx == 16) {  // entry miss
    uint32_t cand = 16;
    uint64_t candScore = -1;
    // uint64_t candScore = 0;
    for (uint32_t i = 0; i < 16; i++) {
      if (array[i].lastCycle > reqCycle + 500)
        continue;  // warm prefetches, not even a candidate
      /*uint64_t score = (reqCycle - array[i].lastCycle)*(3 -
      array[i].conf.counter());
      if (score > candScore) {
          cand = i;
          candScore = score;
      }*/
      if (array[i].ts < candScore) {  // just LRU
        cand = i;
        candScore = array[i].ts;
      }
    }

    if (cand < 16) {
      idx = cand;
      array[idx].alloc(reqCycle);
      array[idx].lastPos = pos;
      array[idx].ts = timestamp++;
      tag[idx] = pageAddr;
    }

    DBG("%s: MISS alloc idx %d", name.c_str(), idx);
  } else {  // entry hit
    profPageHits.inc();
    Entry& e = array[idx];
    array[idx].ts = timestamp++;
    DBG("%s: PAGE HIT idx %d", name.c_str(), idx);

    // 1. Did we prefetch-hit?
    bool shortPrefetch = false;
    if (e.valid[pos]) {
      uint64_t pfRespCycle = e.times[pos].respCycle;
      shortPrefetch = pfRespCycle > respCycle;
      e.valid[pos] = false;  // close, will help with long-lived transactions
      // info ("Here!");
      // info ("respcycle is %d", (int)respCycle);
      // info ("pfRespcycle is %d", (int)pfRespCycle);
      respCycle = MAX(pfRespCycle, respCycle);
      e.lastCycle = MAX(respCycle, e.lastCycle);

      profHits.inc();
      if (shortPrefetch) profShortHits.inc();
      DBG(
          "%s: pos %d prefetched on %ld, pf resp %ld, demand resp %ld, short "
          "%d",
          name.c_str(), pos, e.times[pos].startCycle, pfRespCycle, respCycle,
          shortPrefetch);
    }

    // 2. Update predictors, issue prefetches
    int32_t stride = pos - e.lastPos;
    DBG("%s: pos %d lastPos %d lastLastPost %d e.stride %d", name.c_str(), pos,
        e.lastPos, e.lastLastPos, e.stride);
    if (e.stride == stride) {
      e.conf.inc();
      if (e.conf.pred()) {  // do prefetches
        int32_t fetchDepth = (e.lastPrefetchPos - e.lastPos) / stride;
        uint32_t prefetchPos = e.lastPrefetchPos + stride;
        if (fetchDepth < 1) {
          prefetchPos = pos + stride;
          fetchDepth = 1;
        }

        DBG(
            "%s: pos %d stride %d conf %d lastPrefetchPos %d prefetchPos %d "
            "fetchDepth %d",
            name.c_str(), pos, stride, e.conf.counter(), e.lastPrefetchPos,
            prefetchPos, fetchDepth);

        if (prefetchPos < 64 && !e.valid[prefetchPos]) {
          MemReq::Flag f = MemReq::PREFETCH;
          if (tr.startEvent == nullptr) {
            f = (MemReq::Flag)(MemReq::DONT_RECORD | MemReq::PREFETCH);
          }
          // info ("The prefetch flags is %d", f);

          MESIState state = I;
          MemReq pfReq = {req.lineAddr + prefetchPos - pos,
                          GETS,
                          req.childId,
                          &state,
                          reqCycle,
                          req.childLock,
                          state,
                          req.srcId,
                          f};

          uint64_t pfRespCycle =
              parent->access(pfReq);  // FIXME, might segfault
          // info ("did pf1");
          pf1 = true;

          // ----- trace for the prefetch access ---
          // ----- needs to be attached to the access trace as a second branch
          // ----- trace for prefetch access ---

          if (evRec->hasRecord()) {
            if ((tr.startEvent != nullptr)) {
              assert_msg(tr.startEvent != nullptr,
                         "The start event is not true");
              TimingRecord acc = evRec->popRecord();
              assert_msg(acc.startEvent != nullptr,
                         "The start event is not true");
              assert(tr.reqCycle >= reqCycle);
              assert(acc.reqCycle >= reqCycle);
              DelayEvent* startEv = new (evRec) DelayEvent(0);
              DelayEvent* dWbEv =
                  new (evRec) DelayEvent(tr.reqCycle - reqCycle);
              DelayEvent* dAccEv = new (evRec) DelayEvent(499);
              // DelayEvent* dAccEv = new (evRec) DelayEvent(acc.reqCycle -
              // reqCycle);
              startEv->setMinStartCycle(reqCycle);
              dWbEv->setMinStartCycle(reqCycle);
              dAccEv->setMinStartCycle(reqCycle);

              assert_msg(dWbEv != nullptr, "the msg is not valid");
              assert_msg(dAccEv != nullptr, "the msg is not valid");
              assert_msg(tr.startEvent != nullptr, "null ptr start event");

              TimingEvent* x = startEv->addChild(dWbEv, evRec);
              // info ("The state is %d", (tr.startEvent)->state);
              // info ("added first child");
              assert(x);
              TimingEvent* y = tr.startEvent;
              assert_msg(y != nullptr, "y is a null ptr");
              assert_msg(y != NULL, "y is a NULL");
              assert_msg((long)y != 0, "y is 0");
              assert(y != nullptr);
              assert(y != NULL);
              assert((long)y != 0);  // asserts don't work here
              // info ("Printing y = %d", (long)y);

              x->addChild(y, evRec);  //-> addChild(dAccEv,
                                      //evRec)->addChild(acc.startEvent, evRec);
              startEv->addChild(dAccEv, evRec)->addChild(acc.startEvent, evRec);

              acc.reqCycle = reqCycle;
              acc.startEvent = startEv;

              acc.endEvent = tr.endEvent;
              acc.respCycle = tr.respCycle;
              acc.type = tr.type;
              // endEvent / endCycle stay the same; wbAcc's endEvent not
              // connected

              // info (" PREFETCH returning stitched prefetch record");
              evRec->pushRecord(acc);

              // info ("req cycle is %d", acc.reqCycle);
              // info ("resp cycle is %d", acc.respCycle);
              //
              // return acc.respCycle;
              // return respCycle;
            } else {
              DelayEvent* startEv = new (evRec) DelayEvent(0);

              TimingRecord t = evRec->popRecord();
              startEv->addChild(t.startEvent, evRec);
              t.startEvent = startEv;
              t.endEvent = t.startEvent;
              t.respCycle = respCycle;
              t.type = GETS;
              // t.type = PUTX;  //so that prefetch not on critical path
              evRec->pushRecord(t);  // pure prefetch
              //            info ("PREFETCH Pure timing record");
              //            info("here");
            }
          } else {
            assert(!evRec->hasRecord());
            // info (" NO PREFETCH returning non-stitched prefetch record");

            if (tr.isValid()) {
              evRec->pushRecord(tr);
            }

            // info ("req cycle is %d", tr.reqCycle);
            // info ("resp cycle is %d", tr.respCycle);
            // info ("pushed record");

            // return tr.respCycle;
            // return respCycle;
          }

          e.valid[prefetchPos] = true;
          e.times[prefetchPos].fill(reqCycle, pfRespCycle);
          profPrefetches.inc();

          if (shortPrefetch && fetchDepth < 8 && prefetchPos + stride < 64 &&
              !e.valid[prefetchPos + stride]) {
            prefetchPos += stride;
            pfReq.lineAddr += stride;
            //info("doing pf2");

            TimingRecord tr_1;
            tr_1.clear();

            if (evRec->hasRecord()) {
              tr_1 = evRec->popRecord();
            }

            pfRespCycle = parent->access(pfReq);

            pf2 = true;

            if (evRec->hasRecord()) {
              if ((tr_1.startEvent != nullptr)) {
                assert_msg(tr_1.startEvent != nullptr,
                           "The start event is not true");
                TimingRecord acc = evRec->popRecord();
                assert_msg(acc.startEvent != nullptr,
                           "The start event is not true");
                assert(tr_1.reqCycle >= reqCycle);
                assert(acc.reqCycle >= reqCycle);
                DelayEvent* startEv = new (evRec) DelayEvent(0);
                DelayEvent* dWbEv =
                    new (evRec) DelayEvent(tr_1.reqCycle - reqCycle);
                DelayEvent* dAccEv =
                    new (evRec) DelayEvent(acc.reqCycle - reqCycle);
                startEv->setMinStartCycle(reqCycle);
                dWbEv->setMinStartCycle(reqCycle);
                dAccEv->setMinStartCycle(reqCycle);

                assert_msg(dWbEv != nullptr, "the msg is not valid");
                assert_msg(dAccEv != nullptr, "the msg is not valid");
                assert_msg(tr_1.startEvent != nullptr, "null ptr start event");

                TimingEvent* x = startEv->addChild(dWbEv, evRec);
                // info ("The state is %d", (tr.startEvent)->state);
                // info ("added first child");
                assert(x);
                TimingEvent* y = tr_1.startEvent;
                assert_msg(y != nullptr, "y is a null ptr");
                assert_msg(y != NULL, "y is a NULL");
                assert_msg((long)y != 0, "y is 0");
                assert(y != nullptr);
                assert(y != NULL);
                assert((long)y != 0);  // asserts don't work here
                // info ("Printing y = %d", (long)y);

                x->addChild(y, evRec);
                startEv->addChild(dAccEv, evRec)
                    ->addChild(acc.startEvent, evRec);

                if (tr.startEvent ==
                    nullptr) {  // means this is the second pure prefetch
                  acc.startEvent = startEv; 
                  acc.endEvent = startEv;
                  acc.respCycle = respCycle;
                  acc.reqCycle = reqCycle;
                  acc.type = tr.type;
                  assert (tr.type == GETS);

                }else{  //means this is second prefetch, but not pure
                        //there was an actual access
                        //there might have been a first prefetch but we don't know 
                        //as we only have the combined record


                acc.reqCycle = reqCycle;
                acc.startEvent = startEv;
                acc.respCycle = tr_1.respCycle;
                acc.endEvent = tr_1.endEvent;
                acc.type = tr_1.type;
                  assert (tr_1.type == GETS);
                }

                // endEvent / endCycle stay the same; wbAcc's endEvent not
                // connected

                // info ("returning stitched prefetch record");
                evRec->pushRecord(acc);

                // info ("req cycle is %d", acc.reqCycle);
                // info ("resp cycle is %d", acc.respCycle);
                //
                // return acc.respCycle;
                // return respCycle;
              } else {  // means it is the first pure prefetch

                DelayEvent* startEv = new (evRec) DelayEvent(0);
                TimingRecord t = evRec->popRecord();
                startEv->addChild(t.startEvent, evRec);
                t.startEvent = startEv;
                t.endEvent = t.startEvent;
                t.respCycle = respCycle;
                t.type = GETS;
                evRec->pushRecord(t);  // pure prefetch
                // info("here");
              }
            } else {
              assert(!evRec->hasRecord());
              // info ("returning non-stitched prefetch record");

              if (tr_1.isValid()) {
                evRec->pushRecord(tr_1);
              }

              // info ("req cycle is %d", tr.reqCycle);
              // info ("resp cycle is %d", tr.respCycle);
              // info ("pushed record");
              //

              // return tr.respCycle;
              // return respCycle;
            }

            e.valid[prefetchPos] = true;
            e.times[prefetchPos].fill(reqCycle, pfRespCycle);
            profPrefetches.inc();
            profDoublePrefetches.inc();
          }

          e.lastPrefetchPos = prefetchPos;
          assert(state ==
                 I);  // prefetch access should not give us any permissions
        }
      } else {
        profLowConfAccs.inc();
      }
    } else {
      e.conf.dec();
      // See if we need to switch strides
      if (!e.conf.pred()) {
        int32_t lastStride = e.lastPos - e.lastLastPos;

        if (stride && stride != e.stride && stride == lastStride) {
          e.conf.reset();
          e.stride = stride;
          profStrideSwitches.inc();
        }
      }
      e.lastPrefetchPos = pos;
    }

    e.lastLastPos = e.lastPos;
    e.lastPos = pos;
  }

  req.childId = origChildId;

  if (!pf1 && !pf2) {
    assert(!evRec->hasRecord());
    // info ("returning non-stitched prefetch record");

    // info ("NO PREFETCH ");

    if (tr.isValid()) {
      evRec->pushRecord(tr);
    }
  }

  // info ("respcycle without record is %d", respCycle);
  return respCycle;
}

// nop for now; do we need to invalidate our own state?
uint64_t StreamPrefetcher::invalidate(const InvReq& req) {
  return child->invalidate(req);
}
