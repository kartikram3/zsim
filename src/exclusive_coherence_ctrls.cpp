#include "exclusive_coherence_ctrls.h"
#include "cache.h"
#include "network.h"

uint32_t exclusive_MESIBottomCC::getParentId(Address lineAddr) {
    //Hash things a bit
    uint32_t res = 0;
    uint64_t tmp = lineAddr;
    for (uint32_t i = 0; i < 4; i++) {
        res ^= (uint32_t) ( ((uint64_t)0xffff) & tmp);
        tmp = tmp >> 16;
    }
    return (res % parents.size());
}

void exclusive_MESIBottomCC::init(const g_vector<MemObject*>& _parents, Network* network, const char* name) {
    parents.resize(_parents.size());
    parentRTTs.resize(_parents.size() );
    for (uint32_t p = 0; p < parents.size(); p++) {
        parents[p] = _parents[p];
        parentRTTs[p] = (network)? network->getRTT(name, parents[p]->getName()) : 0;
    }
}

uint64_t exclusive_MESIBottomCC::processEviction(Address wbLineAddr, uint32_t lineId, bool lowerLevelWriteback, uint64_t cycle, uint32_t srcId) {
  
    //we don't do lower level writeback because the cache is exclusive

    MESIState* state = &array[lineId];

    uint64_t respCycle = cycle;
    switch (*state) {
        case I:
            break; //Nothing to do
        case S:
        case E:
            {
                MemReq req = {wbLineAddr, PUTS, selfId, state, cycle, &ccLock, *state, srcId, 0 /*no flags*/};
                respCycle = parents[getParentId(wbLineAddr)]->access(req);
            }
            break;
        case M:
            {
                MemReq req = {wbLineAddr, PUTX, selfId, state, cycle, &ccLock, *state, srcId, 0 /*no flags*/};
                respCycle = parents[getParentId(wbLineAddr)]->access(req);
            }
            break;

        default: panic("!?");
    }
    assert_msg(*state == I, "Wrong final state %s on eviction", MESIStateName(*state));
    return respCycle;
}

uint64_t exclusive_MESIBottomCC::processAccess(Address lineAddr, uint32_t lineId, AccessType type, uint64_t cycle, uint32_t srcId, uint32_t flags) {
    uint64_t respCycle = cycle;

    if ((int) lineId == -1){
        assert( type == GETS || type == GETX );
        if (!(flags & MemReq::INNER_COPY)){ //i.e. if line was found in inner levels in case of excl llc
           MESIState dummyState = I; // does this affect race conditions ?
           MemReq req = {lineAddr, type, selfId, &dummyState, cycle, &ccLock, dummyState , srcId, flags};
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
        // A PUTS/PUTX does nothing w.r.t. higher coherence levels --- it dies here
        case PUTS: //Clean writeback, nothing to do (except profiling)
            assert(*state == I);
            *state = E; //receive the data in exclusive state
                        //for multithreaded application, may need to
                        //receive data in shared state also
            profPUTS.inc();
            break;
        case PUTX: //Dirty writeback
            assert(*state == I);
            if (*state == I) {
                //Silent transition, record that block was written to
                *state = M;
            }
            profPUTX.inc();
            break;
        case GETS:
            if (*state == I) {
                uint32_t parentId = getParentId(lineAddr);
                MESIState dummyState = I; // does this affect race conditions ?
                MemReq req = {lineAddr, GETS, selfId, &dummyState, cycle, &ccLock, dummyState, srcId, flags};
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
            if (*state == I || *state == S) {
                //Profile before access, state changes
                if (*state == I) profGETXMissIM.inc();
                else profGETXMissSM.inc();
                uint32_t parentId = getParentId(lineAddr);
                MemReq req = {lineAddr, GETX, selfId, state, cycle, &ccLock, *state, srcId, flags};
                uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
                uint32_t netLat = parentRTTs[parentId];
                profGETNextLevelLat.inc(nextLevelLat);
                profGETNetLat.inc(netLat);
                respCycle += nextLevelLat + netLat;
            }
            *state=I; //inv because cache is exclusive
            break;

        default: panic("!?");
    }
    assert_msg(respCycle >= cycle, "XXX %ld %ld", respCycle, cycle);
    return respCycle;
}

void exclusive_MESIBottomCC::processWritebackOnAccess(Address lineAddr, uint32_t lineId, AccessType type) {
    MESIState* state = &array[lineId];
    assert(*state == M || *state == E);
    if (*state == E) {
        //Silent transition to M if in E
        *state = M;
    }
}

void exclusive_MESIBottomCC::processInval(Address lineAddr, uint32_t lineId, InvType type, bool* reqWriteback) {
    MESIState* state = &array[lineId];
    assert(*state != I);
    switch (type) {
        case INVX: //lose exclusivity
            //Hmmm, do we have to propagate loss of exclusivity down the tree? (nah, topcc will do this automatically -- it knows the final state, always!)
            assert_msg(*state == E || *state == M, "Invalid state %s", MESIStateName(*state));
            if (*state == M) *reqWriteback = true;
            *state = S;
            profINVX.inc();
            break;
        case INV: //invalidate
            assert(*state != I);
            if (*state == M) *reqWriteback = true;
            *state = I;
            profINV.inc();
            break;
        case FWD: //forward
            assert_msg(*state == S, "Invalid state %s on FWD", MESIStateName(*state));
            profFWD.inc();
            break;
        default: panic("!?");
    }
    //NOTE: BottomCC never calls up on an invalidate, so it adds no extra latency
}

uint64_t exclusive_MESIBottomCC::processNonInclusiveWriteback(Address lineAddr, AccessType type, uint64_t cycle, MESIState* state, uint32_t srcId, uint32_t flags) {

    //info("Non-inclusive wback, forwarding");
    //MemReq req = {lineAddr, type, selfId, state, cycle, &ccLock, *state, srcId, flags | MemReq::NONINCLWB};
    //uint64_t respCycle = parents[getParentId(lineAddr)]->access(req);
    //do nothing ...  this function should never be called for an exclusive cache

    return 0;
}


/* MESITopCC implementation */

void exclusive_MESITopCC::init(const g_vector<BaseCache*>& _children, Network* network, const char* name) {
    if (_children.size() > MAX_CACHE_CHILDREN) {
        panic("[%s] Children size (%d) > MAX_CACHE_CHILDREN (%d)", name, (uint32_t)_children.size(), MAX_CACHE_CHILDREN);
    }
    children.resize(_children.size());
    childrenRTTs.resize(_children.size());
    for (uint32_t c = 0; c < children.size(); c++) {
        children[c] = _children[c];
        childrenRTTs[c] = (network)? network->getRTT(name, children[c]->getName()) : 0;
    }
}

uint64_t exclusive_MESITopCC::sendInvalidates(Address lineAddr, uint32_t lineId, InvType type, bool* reqWriteback, uint64_t cycle, uint32_t srcId, 
                                               uint32_t childId) {

    uint64_t maxCycle = cycle; //keep maximum cycle only, we assume all invals are sent in parallel
        uint32_t numChildren = children.size();
        //info ("numchildren is %d", numChildren);
        uint32_t sentInvs = 0;
        for (uint32_t c = 0; c < numChildren; c++) {
                if (c==childId){ c++; continue;}
                InvReq req = {lineAddr, type, reqWriteback, cycle, srcId};
                uint64_t respCycle = children[c]->invalidate(req);
                respCycle += childrenRTTs[c];
                maxCycle = MAX(respCycle, maxCycle);
                sentInvs++;
        }
    return maxCycle;
}

uint64_t exclusive_MESITopCC::processEviction(Address wbLineAddr, uint32_t lineId, bool* reqWriteback, uint64_t cycle, uint32_t srcId) {
        // Don't invalidate anything, just clear our entry
        array[lineId].clear();
        return cycle;
}

uint64_t exclusive_MESITopCC::processAccess(Address lineAddr, uint32_t lineId, AccessType type, uint32_t childId, bool haveExclusive,
                                  MESIState* childState, bool* inducedWriteback, uint64_t cycle, uint32_t srcId, uint32_t flags) {


    uint64_t respCycle = cycle;

    if ((int) lineId == -1){
        if (!(flags & MemReq::INNER_COPY)){ //i.e. if line was not found in inner levels in case of excl llc
          assert( type == GETS || type == GETX );
          if (type == GETS)
          *childState = E; //as line is gonna come in E state, as levels below excl are excl
                           //if shared cache, we don't know which core owns the copy though
          else *childState = M;
          return respCycle;
        }else{
          if (type == GETS){
            respCycle = sendInvalidates(lineAddr, lineId, INVX, inducedWriteback, cycle, srcId, childId); //sets inner level copies to S state
            *childState = S;
          }
          else{
            respCycle = sendInvalidates(lineAddr, lineId, INV, inducedWriteback, cycle, srcId, childId); //sets inner level copies to I state
            *childState = M;
          }
          return respCycle;
        }
    }

    //Entry* e = &array[lineId]; //not needed for exclusive cache
    switch (type) {
        case PUTX:
        case PUTS:
            *childState = I; //if data should not be duplicated in 
                             //any child of the child cache
                             //then we should not be cycling the data
                             //So we need the duplicate bit to enable this
                             //decision but oh well
                             //that is for the flexclusive cache
            break;

        case GETS:
            *childState = E;
            break;
        case GETX:
            assert(haveExclusive); //the current cache better have exclusive access to this line

            *childState = M; //give in M directly
            break;

        default: panic("!?");
    }

    return respCycle;
}

uint64_t exclusive_MESITopCC::snoopInnerLevels(Address snoopAddr, uint64_t respCycle, bool * lineExists){

//    uint32_t numChildren = children.size();
//
//    //uint32_t sentInvs = 0;
//
//    for (uint32_t c = 0; c < numChildren; c++) {
//        SnoopReq req = {snoopAddr, req.cycle, req.srcId };
//        respCycle = children[c]->snoop(); //eager snooping of all children
//                                          //ideally we should not snoop banks
//                                          //which we already snooped
//                                          //FIX in the next iteration
//    }

    return 0;
}

uint64_t exclusive_MESITopCC::processInval(Address lineAddr, uint32_t lineId, InvType type, bool* reqWriteback, uint64_t cycle, uint32_t srcId){
    return cycle;
}
