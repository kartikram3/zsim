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

#include "coherence_ctrls.h"
#include "cache.h"
#include "network.h"
#include "flexclusive_coherence_ctrls.h"

/* Do a simple XOR block hash on address to determine its bank. Hacky for now,
 * should probably have a class that deals with this with a real hash function
 * (TODO)
 */
uint32_t flexclusive_MESIBottomCC::getParentId(Address lineAddr) {
  // Hash things a bit
  uint32_t res = 0;
  uint64_t tmp = lineAddr;
  for (uint32_t i = 0; i < 4; i++) {
    res ^= (uint32_t)(((uint64_t)0xffff) & tmp);
    tmp = tmp >> 16;
  }
  return (res % parents.size());
}

void flexclusive_MESIBottomCC::init(const g_vector<MemObject*>& _parents,
                                    Network* network, const char* name) {
  parents.resize(_parents.size());
  parentRTTs.resize(_parents.size());
  for (uint32_t p = 0; p < parents.size(); p++) {
    parents[p] = _parents[p];
    parentRTTs[p] =
        (network) ? network->getRTT(name, parents[p]->getName()) : 0;
  }
}

uint64_t flexclusive_MESIBottomCC::processEviction(Address wbLineAddr,
                                                   uint32_t lineId,
                                                   bool lowerLevelWriteback,
                                                   uint64_t cycle,
                                                   uint32_t srcId) {
  MESIState* state = &array[lineId];
  if (lowerLevelWriteback) {
    // If this happens, when tcc issued the invalidations, it got a writeback.
    // This means we have to do a PUTX, i.e. we have to transition to M if we
    // are in E
    assert(*state == M || *state == E);  // Must have exclusive permission!
    *state = M;  // Silent E->M transition (at eviction); now we'll do a PUTX
  }
  uint64_t respCycle = cycle;
  switch (*state) {
    case I:
      break;  // Nothing to do
    case S:
    case E: {
      MemReq req = {wbLineAddr, PUTS,   selfId, state, cycle,
                    &ccLock,    *state, srcId,  0 /*no flags*/};
      respCycle = parents[getParentId(wbLineAddr)]->access(req);
    } break;
    case M: {
      MemReq req = {wbLineAddr, PUTX,   selfId, state, cycle,
                    &ccLock,    *state, srcId,  0 /*no flags*/};
      respCycle = parents[getParentId(wbLineAddr)]->access(req);
    } break;

    default:
      panic("!?");
  }
  assert_msg(*state == I, "Wrong final state %s on eviction",
             MESIStateName(*state));
  return respCycle;
}

uint64_t flexclusive_MESIBottomCC::processAccess(Address lineAddr,
                                                 uint32_t lineId,
                                                 AccessType type,
                                                 uint64_t cycle, uint32_t srcId,
                                                 uint32_t flags, CLUState cs) {
  uint64_t respCycle = cycle;

  if (cs == EX) {
    if ((int)lineId == -1) {
      //info("The line id is %d", (int) lineId);
      assert_msg((type == GETS || type == GETX) , "The type is %d", type);
      if (type == GETS)
        profGETSMiss.inc();
      else
        profGETXMissIM.inc();
      if (!(flags & MemReq::INNER_COPY)) {  // i.e. if line was found in inner
                                            // levels in case of excl llc
        MESIState dummyState = I;  // does this affect race conditions ?
        MemReq req = {lineAddr, type,       selfId, &dummyState, cycle,
                      &ccLock,  dummyState, srcId,  flags};
        uint32_t parentId = getParentId(lineAddr);
        uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
        uint32_t netLat = parentRTTs[parentId];
        profGETNextLevelLat.inc(nextLevelLat);
        profGETNetLat.inc(netLat);
        respCycle += nextLevelLat + netLat;
      }

      assert_msg(respCycle >= cycle, "XXX %ld %ld", respCycle, cycle);
      return respCycle;
    }

    MESIState* state = &array[lineId];
    switch (type) {
      // A PUTS/PUTX does nothing w.r.t. higher coherence levels --- it dies
      // here
      case PUTS:  // Clean writeback, nothing to do (except profiling)
        // assert(*state == I); //we can't assert this a
        // a copy of the data may still be there
        // in the cache from somewhere else
        if (flags & MemReq::INNER_COPY) {
        }  // assert(*state == I)}
        else
          *state = E;  // receive the data in exclusive state
        // for multithreaded application, may need to
        // receive data in shared state also
        profPUTS.inc();
        break;
      case PUTX:  // Dirty writeback
        //assert(*state == I);
        // Silent transition, record that block was written to
        if (flags & MemReq::INNER_COPY) {
          //assert(*state == I);
        } else
          *state = M;
        profPUTX.inc();
        break;
      case GETS:
        if (*state == I && (!(flags & MemReq::INNER_COPY))) {
          uint32_t parentId = getParentId(lineAddr);
          MESIState dummyState = I;  // does this affect race conditions ?
          MemReq req = {lineAddr, GETS,       selfId, &dummyState, cycle,
                        &ccLock,  dummyState, srcId,  flags};
          uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
          uint32_t netLat = parentRTTs[parentId];
          profGETNextLevelLat.inc(nextLevelLat);
          profGETNetLat.inc(netLat);
          respCycle += nextLevelLat + netLat;
          profGETSMiss.inc();
        } else {
          profGETSHit.inc();
        }
        *state = I;
        break;
      case GETX:
        if ((*state == I || *state == S) && (!(flags & MemReq::INNER_COPY))) {
          // Profile before access, state changes
          if (*state == I)
            profGETXMissIM.inc();
          else
            profGETXMissSM.inc();
          uint32_t parentId = getParentId(lineAddr);
          MemReq req = {lineAddr, GETX,   selfId, state, cycle,
                        &ccLock,  *state, srcId,  flags};
          uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
          uint32_t netLat = parentRTTs[parentId];
          profGETNextLevelLat.inc(nextLevelLat);
          profGETNetLat.inc(netLat);
          respCycle += nextLevelLat + netLat;
        } else {  // means state is E or M
          profGETXHit.inc();
        }
        *state = I;  // inv because cache is exclusive
        break;

      default:
        panic("!?");
    }

  } else {
    MESIState* state = &array[lineId];

    switch (type) {
      // A PUTS/PUTX does nothing w.r.t. higher coherence levels --- it dies
      // here
      case PUTS:  // Clean writeback, nothing to do (except profiling)
        assert(*state != I);
        profPUTS.inc();
        break;
      case PUTX:  // Dirty writeback
        assert(*state == M || *state == E);
        if (*state == E) {
          // Silent transition, record that block was written to
          *state = M;
        }
        profPUTX.inc();
        break;
      case GETS:
        if (*state == I) {
          uint32_t parentId = getParentId(lineAddr);
          if (!(flags & MemReq::INNER_COPY)) {
            MemReq req = {lineAddr, GETS,   selfId, state, cycle,
                          &ccLock,  *state, srcId,  flags};
            uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
            uint32_t netLat = parentRTTs[parentId];
            profGETNextLevelLat.inc(nextLevelLat);
            profGETNetLat.inc(netLat);
            respCycle += nextLevelLat + netLat;
          } else {
            // also need to send invx to the parents
            *state = S;  // don't change respCycle
          }
          profGETSMiss.inc();

          assert(*state == S || *state == E);
        } else {
          profGETSHit.inc();
        }
        break;
      case GETX:
        if (*state == I || *state == S) {
          // Profile before access, state changes
          if (*state == I)
            profGETXMissIM.inc();
          else
            profGETXMissSM.inc();

          if ((flags & MemReq::INNER_COPY)) {  // means line was not found in
                                               // the non-inclusive llc
            // assert(*state == I); //not true anymore, as we always check for
            // inner copy
            *state = M;  // since it is a GETX, the final state should be M
          } else if (!(flags & MemReq::INNER_COPY)) {
            uint32_t parentId = getParentId(lineAddr);
            MemReq req = {lineAddr, GETX,   selfId, state, cycle,
                          &ccLock,  *state, srcId,  flags};
            uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
            uint32_t netLat = parentRTTs[parentId];
            profGETNextLevelLat.inc(nextLevelLat);
            profGETNetLat.inc(netLat);
            respCycle += nextLevelLat + netLat;
          }
        } else {
          if (*state == E) {
            // Silent transition
            // NOTE: When do we silent-transition E->M on an ML hierarchy... on
            // a GETX, or on a PUTX?
            /* Actually, on both: on a GETX b/c line's going to be modified
             * anyway, and must do it if it is the L1 (it's OK not
             * to transition if L2+, we'll TX on the PUTX or invalidate, but
             * doing it this way minimizes the differences between
             * L1 and L2+ controllers); and on a PUTX, because receiving a PUTX
             * while we're in E indicates the child did a silent
             * transition and now that it is evictiong, it's our turn to
             * maintain M info.
             */
            *state = M;
          }
          profGETXHit.inc();
        }

        assert_msg(
            *state == M,
            "Wrong final state on GETX, lineId %d numLines %d, finalState %s",
            lineId, numLines, MESIStateName(*state));
        break;

      default:
        panic("!?");
    }
  }

  assert_msg(respCycle >= cycle, "XXX %ld %ld", respCycle, cycle);
  return respCycle;
}

void flexclusive_MESIBottomCC::processWritebackOnAccess(Address lineAddr,
                                                        uint32_t lineId,
                                                        AccessType type) {
  MESIState* state = &array[lineId];
  assert(*state == M || *state == E);
  if (*state == E) {
    // Silent transition to M if in E
    *state = M;
  }
}

// void flexclusive_MESIBottomCC::processInval(Address lineAddr, uint32_t
// lineId, InvType type, bool* reqWriteback) {
//    MESIState* state = &array[lineId];
//    assert(*state != I);
//    switch (type) {
//        case INVX: //lose exclusivity
//            //Hmmm, do we have to propagate loss of exclusivity down the tree?
//            (nah, topcc will do this automatically -- it knows the final
//            state, always!)
//            assert_msg(*state == E || *state == M, "Invalid state %s",
//            MESIStateName(*state));
//            if (*state == M) *reqWriteback = true;
//            *state = S;
//            profINVX.inc();
//            break;
//        case INV: //invalidate
//            assert(*state != I);
//            if (*state == M) *reqWriteback = true;
//            *state = I;
//            profINV.inc();
//            break;
//        case FWD: //forward
//            assert_msg(*state == S, "Invalid state %s on FWD",
//            MESIStateName(*state));
//            profFWD.inc();
//            break;
//        default: panic("!?");
//    }
//    //NOTE: BottomCC never calls up on an invalidate, so it adds no extra
//    latency
//}

void flexclusive_MESIBottomCC::processInval(Address lineAddr, uint32_t lineId,
                                            InvType type, bool* reqWriteback) {
  MESIState* state = &array[lineId];

  // assert(*state != I);  //need to change inner banks search so that these
  // assertions can be put back in place
  // assertions are good
  switch (type) {
    case INVX:  // lose exclusivity
      // Hmmm, do we have to propagate loss of exclusivity down the tree? (nah,
      // topcc will do this automatically -- it knows the final state, always!)
      // assert_msg(*state == E || *state == M, "Invalid state %s",
      // MESIStateName(*state));
      if (*state == M) *reqWriteback = true;
      if (*state != I) *state = S;
      profINVX.inc();
      break;
    case INV:  // invalidate
      // assert(*state != I);
      if (*state == M) *reqWriteback = true;
      *state = I;
      profINV.inc();
      break;
    case FWD:  // forward
      assert_msg(*state == S, "Invalid state %s on FWD", MESIStateName(*state));
      profFWD.inc();
      break;
    default:
      panic("!?");
  }
  // NOTE: BottomCC never calls up on an invalidate, so it adds no extra latency
}
//

uint64_t flexclusive_MESIBottomCC::processNonInclusiveWriteback(
    Address lineAddr, AccessType type, uint64_t cycle, MESIState* state,
    uint32_t srcId, uint32_t flags) {

   // info("Non-inclusive wback, forwarding");
  MemReq req = {lineAddr, type,  selfId,
                state,    cycle, &ccLock,
                *state,   srcId, flags | MemReq::NONINCLWB};
  uint64_t respCycle = parents[getParentId(lineAddr)]->access(req);
  return respCycle;
}

/* flexclusive_MESITopCC implementation */

void flexclusive_MESITopCC::init(const g_vector<BaseCache*>& _children,
                                 Network* network, const char* name) {
  if (_children.size() > MAX_CACHE_CHILDREN) {
    panic("[%s] Children size (%d) > MAX_CACHE_CHILDREN (%d)", name,
          (uint32_t)_children.size(), MAX_CACHE_CHILDREN);
  }
  children.resize(_children.size());
  childrenRTTs.resize(_children.size());
  for (uint32_t c = 0; c < children.size(); c++) {
    children[c] = _children[c];
    childrenRTTs[c] =
        (network) ? network->getRTT(name, children[c]->getName()) : 0;
  }
}

uint64_t flexclusive_MESITopCC::sendInvalidates(Address lineAddr,
                                                uint32_t lineId, InvType type,
                                                bool* reqWriteback,
                                                uint64_t cycle, uint32_t srcId,
                                                bool non_incl) {
  // Send down downgrades/invalidates
  if((int)lineId == -1) {
    uint64_t maxCycle = cycle; //keep maximum cycle only, we assume all invals are sent in parallel
    //uint32_t num_valid_children = valid_children.size();
    //uint32_t numChildren = 0;
        //info ("numchildren is %d", numChildren);
        uint32_t sentInvs = 0;
        uint32_t numChildren = valid_children.size();
        //uint32_t numChildren = children.size();
        uint32_t c;
        for (uint32_t i = 0; i < numChildren; i++) { //iterate through children

          c = valid_children[i];
          //if (i==childId ){ continue;} //no need to invalidate child
                                       //which sent the request
                                       //this should never happen
                //info ("doing invalidate !, numchildren is %d", numChildren);
                InvReq req = {lineAddr, type, reqWriteback, cycle, srcId};
                uint64_t respCycle = children[c]->invalidate(req);
                respCycle += childrenRTTs[c];
                maxCycle = MAX(respCycle, maxCycle);
                sentInvs++;
        }
    return maxCycle;
  }

  Entry* e = &array[lineId];

  uint64_t maxCycle = cycle;
  // keep maximum cycle only, we assume all invals are sent in parallel
  if (!e->isEmpty()) {
    // uint32_t numChildren = children.size();
    uint32_t numChildren = valid_children.size();
    uint32_t c;
    // assert (numChildren == 4);
    uint32_t sentInvs = 0;
    for (uint32_t i = 0; i < numChildren; i++) {
      c = valid_children[i];
      InvReq req = {lineAddr, type, reqWriteback, cycle, srcId};
      if (e->sharers[c]) {
        uint64_t respCycle = children[c]->invalidate(req);
        respCycle += childrenRTTs[c];
        maxCycle = MAX(respCycle, maxCycle);

        // info("e->sharers[c] is %d, c is %d, type is %d", (int) e->sharers[c],
        // c, type);
        if ((type == INV)) {
          e->sharers[c] = false;
        } else if (respCycle == cycle) {  // means the invx did not really
                                          // happen
          e->sharers[c] = false;
          e->numSharers--;
        }
        sentInvs++;
      }
    }

    // assert(sentInvs == e->numSharers); //removed assertion because sometimes
    // we don't invalidate if the line was not present
    // need to fix valid_children to make this work
    if (type == INV) {
      e->numSharers = 0;
      assert(e->sharers[0] == false);
      assert(e->sharers[1] == false);
      assert(e->sharers[2] == false);
      assert(e->sharers[3] == false);
    }

    if (type == INVX) {
      assert(e->numSharers >= 0);
      if (e->numSharers > 0)  // means there is a sharer other than childId
        e->exclusive = false;
      else
        e->exclusive = true;
    }
  }
  return maxCycle;
}

uint64_t flexclusive_MESITopCC::processEviction(Address wbLineAddr,
                                                uint32_t lineId,
                                                bool* reqWriteback,
                                                uint64_t cycle,
                                                uint32_t srcId) {
  // Don't invalidate anything, just clear our entry
  array[lineId].clear();
  return cycle;
}

uint64_t flexclusive_MESITopCC::processAccess(
    Address lineAddr, uint32_t lineId, AccessType type, uint32_t childId,
    bool haveExclusive, MESIState* childState, bool* inducedWriteback,
    uint64_t cycle, uint32_t srcId, uint32_t flags, bool isValid, CLUState cs) {
  if (cs == EX) {
    uint64_t respCycle = cycle;

    if ((int)lineId == -1) {
      assert(type == GETS || type == GETX);
      if (!(flags & MemReq::INNER_COPY)) {  // i.e. if line was not found in
                                            // inner levels in case of excl llc
        assert(search_inner_banks(lineAddr, childId) == 0);
        if (type == GETS) {
          assert(*childState == I);
          *childState = E;  // as line is gonna come in E state, as levels below
                            // excl are excl
          // if shared cache, we don't know which core owns the copy though
        } else
          *childState = M;
        return respCycle;
      } else {
        assert(search_inner_banks(lineAddr, childId) == 1);
        if (type == GETS) {
          respCycle = sendInvalidates(
              lineAddr, lineId, INVX, inducedWriteback, cycle, srcId,
              childId);  // sets inner level copies to S state
          *childState = S;
        } else {
          respCycle = sendInvalidates(
              lineAddr, lineId, INV, inducedWriteback, cycle, srcId,
              childId);  // sets inner level copies to I state
          *childState = M;
        }
        return respCycle;
      }
    }

    // Entry* e = &array[lineId]; //not needed for exclusive cache

    switch (type) {
      case PUTX:
      case PUTS:
        *childState = I;  // if data should not be duplicated in
        // any child of the child cache
        // then we should not be cycling the data
        // So we need the duplicate bit to enable this
        // decision but oh well
        // that is for the flexclusive cache
        break;

      case GETS:
        assert(*childState == I);
        // assert_msg(search_inner_banks(lineAddr, childId) == 0, "Haveexclusive
        // is %d", haveExclusive);
        // this assertion is not correct
        *childState = E;  // could also be M
        // we just specified E
        // need to change this for accuracy
        break;
      case GETX:
        // assert(search_inner_banks(lineAddr, childId) == 0); //this assertion
        // is not correct
        assert(haveExclusive);  // the current cache better have exclusive
                                // access to this line

        *childState = M;  // give in M directly
        break;

      default:
        panic("!?");
    }

    return respCycle;

  } else {
    Entry* e = &array[lineId];
    uint64_t respCycle = cycle;

    e->numSharers=0;

    uint32_t i,c;

    for(i=0; i<(uint32_t)children.size(); i++){
        e->sharers[i] = false;
    }

    for (i = 0; i < (uint32_t)valid_children.size(); i++) {
      c = valid_children[i];
      if (c == childId) {
        continue;
      }
      e->sharers[c] = true;
      e->numSharers++;
    }

    switch (type) {
      case PUTX:
        //assert(e->isExclusive());
        // info ("doing a PUTS due to eviction");
        if (flags & MemReq::PUTX_KEEPEXCL){ //never executed
          assert(e->sharers[childId]);
          assert(*childState == M);
          *childState = E;  // they don't hold dirty data anymore
          break;  // don't remove from sharer set. It'll keep exclusive perms.
        }
      // note NO break in general

      case PUTS:
        // info ("doing a PUTS due to eviction");
        //assert(e->sharers[childId]);
        //e->sharers[childId] = false;
        //e->numSharers--;
        *childState = I;
        break;

      case GETS:
        if ((flags & MemReq::INNER_COPY) && isValid) {
          // if there was an inner copy then what do we do ?
          // we should INVX it
          // and then put it as a sharer in the TCC

          // info ("Found flag !");

          // assert(e->isEmpty()); //need not be empty
          // assert(e->sharers[0] == false);
          // assert(e->sharers[1] == false);
          // assert(e->sharers[2] == false);
          // assert(e->sharers[3] == false);
          e->exclusive = false;  // it is in shared state
          e->numSharers = 0;

          // put everything except child ID as sharer
          uint32_t i;
          assert(e->sharers[childId] == false);
          uint32_t c;
          for (i = 0; i < (uint32_t)valid_children.size(); i++) {
            c = valid_children[i];
            if (c == childId) {
              continue;
            }
            e->sharers[c] = true;
            e->numSharers++;
          }

          // assert (e->sharers[childId] == false);

          uint64_t respCycle;
          respCycle = sendInvalidates(lineAddr, lineId, INVX, inducedWriteback,
                                      cycle, srcId, true);

          // assert_msg( e->sharers[1-childId] == true, "child Id is %d,
          // e->sharers[0] is %d,  e->sharers[1] is %d ", childId,
          // (int)e->sharers[0], (int)e->sharers[1] );
          // not true because the number of sharers may not change if search
          // inner banks gave false positive
          e->sharers[childId] = true;  // set the final directory state

          // e->numSharers = 2; //2 core procesor

          e->numSharers++;  // add the child as a sharer

          assert_msg(e->numSharers > 1,
                     "The sharers at %d %d %d %d, childId is %d",
                     (int)e->sharers[0], (int)e->sharers[1], (int)e->sharers[2],
                     (int)e->sharers[3],
                     childId);  // otherwise something is wrong
          // because the inner level search
          // did a false positive and thus childstate should
          // be E
          *childState = S;
          return respCycle;
        }

        if (e->isEmpty() && haveExclusive && !(flags & MemReq::NOEXCL)) {
          // Give in E state
          //assert(e->sharers[childId] == false);
          e->exclusive = true;
          e->sharers[childId] = true;
          e->numSharers = 1;
          *childState = E;

        } else {
          // Give in S state

          assert(*childState == I);
          //assert_msg(e->sharers[childId] == false,
                     //"haveExclusive is %d, numsharers is %d, childstate is %d",
                     //haveExclusive, e->numSharers, *childState);

          //info("Here");

          //if (e->isExclusive()) {
          if((e->numSharers == 1) && haveExclusive){
            // Downgrade the exclusive sharer
            assert(e->sharers[childId]);
            respCycle = sendInvalidates(lineAddr, lineId, INVX,
                                        inducedWriteback, cycle, srcId, false);
          }

          //assert_msg(
              //!e->isExclusive(),
              //"Can't have exclusivity here. isExcl=%d excl=%d numSharers=%d",
              //e->isExclusive(), e->exclusive, e->numSharers);

          e->sharers[childId] = true;
          e->numSharers++;
          e->exclusive = false;  // dsm: Must set, we're explicitly
                                 // non-exclusive
          *childState = S;

          // assert(e->numSharers == 1);
        }

        assert(*childState != I);
        assert(e->sharers[childId] == true);
        break;
      case GETX:
        assert(haveExclusive);  // the current cache better have exclusive
                                // access to this line
           e->numSharers =0;
          for(i=0; i<(uint32_t)children.size(); i++){
           e->sharers[i] = false;
          }
           for (i = 0; i < (uint32_t)valid_children.size(); i++) {
            c = valid_children[i];
            if (c == childId) continue;  // should not happen
            e->sharers[c] = true;
            e->numSharers++;
          }
        if ((flags & MemReq::INNER_COPY) && isValid) {
          // if there was an inner copy then what do we do ?
          // we should INVX it
          // and then put it as a sharer in the TCC
          // assert(e->sharers[0] == false);
          // assert(e->sharers[1] == false);
          // assert(e->sharers[2] == false);
          // assert(e->sharers[3] == false);

          e->exclusive = false;  // it is in shared state
          e->numSharers = 0;

          uint32_t i;
          e->sharers[childId] = false;
          uint32_t c;
          for (i = 0; i < (uint32_t)valid_children.size(); i++) {
            c = valid_children[i];
            if (c == childId) continue;  // should not happen
            e->sharers[c] = true;
            e->numSharers++;
          }

          // assert (e->sharers[childId] == false);

          uint64_t respCycle;
          respCycle = sendInvalidates(lineAddr, lineId, INV, inducedWriteback,
                                      cycle, srcId, true);

          e->exclusive = true;
          e->numSharers = 1;
          e->sharers[childId] = true;

          *childState = M;

          return respCycle;
        }

        // If child is in sharers list (this is an upgrade miss), take it out
        if (e->sharers[childId]) {
          assert_msg(
              e->numSharers == 1,
              "Spurious GETX, childId=%d numSharers=%d isExcl=%d excl=%d",
              childId, e->numSharers, e->isExclusive(), e->exclusive);
          e->sharers[childId] = false;
          e->numSharers--;
        }

        // Invalidate all other copies
        respCycle = sendInvalidates(lineAddr, lineId, INV, inducedWriteback,
                                    cycle, srcId, false);

        // Set current sharer, mark exclusive
        e->sharers[childId] = true;
        e->numSharers++;
        e->exclusive = true;

        assert(e->numSharers == 1);

        *childState = M;  // give in M directly
        break;

      default:
        panic("!?");
    }
    return respCycle;
  }
}

uint64_t flexclusive_MESITopCC::processInval(Address lineAddr, uint32_t lineId,
                                             InvType type, bool* reqWriteback,
                                             uint64_t cycle,
                                             uint32_t srcId) {  // never called
  if (type == FWD) {  // if it's a FWD, we should be inclusive for now, so we
                      // must have the line, just invLat works
    assert(!nonInclusiveHack);  // dsm: ask me if you see this failing and don't
                                // know why
    return cycle;
  } else {
    // Just invalidate or downgrade down to children as needed
    return sendInvalidates(lineAddr, lineId, type, reqWriteback, cycle, srcId,
                           true);
  }
}
