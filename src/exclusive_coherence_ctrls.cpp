
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
    MESIState* state = &array[lineId];
    if (lowerLevelWriteback) {
        //If this happens, when tcc issued the invalidations, it got a writeback. This means we have to do a PUTX, i.e. we have to transition to M if we are in E
        assert(*state == M || *state == E); //Must have exclusive permission!
        *state = M; //Silent E->M transition (at eviction); now we'll do a PUTX
    }
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
    MESIState* state = &array[lineId];
    switch (type) {
        // A PUTS/PUTX does nothing w.r.t. higher coherence levels --- it dies here
        case PUTS: //Clean writeback, nothing to do (except profiling)
            assert(*state != I);
            profPUTS.inc();
            break;
        case PUTX: //Dirty writeback
            assert(*state == M || *state == E);
            if (*state == E) {
                //Silent transition, record that block was written to
                *state = M;
            }
            profPUTX.inc();
            break;
        case GETS:
            if (*state == I) {
                uint32_t parentId = getParentId(lineAddr);
                MemReq req = {lineAddr, GETS, selfId, state, cycle, &ccLock, *state, srcId, flags};
                uint32_t nextLevelLat = parents[parentId]->access(req) - cycle;
                uint32_t netLat = parentRTTs[parentId];
                profGETNextLevelLat.inc(nextLevelLat);
                profGETNetLat.inc(netLat);
                respCycle += nextLevelLat + netLat;
                profGETSMiss.inc();
                assert(*state == S || *state == E);
            } else {
                profGETSHit.inc();
            }
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
            } else {
                if (*state == E) {
                    // Silent transition
                    // NOTE: When do we silent-transition E->M on an ML hierarchy... on a GETX, or on a PUTX?
                    /* Actually, on both: on a GETX b/c line's going to be modified anyway, and must do it if it is the L1 (it's OK not
                     * to transition if L2+, we'll TX on the PUTX or invalidate, but doing it this way minimizes the differences between
                     * L1 and L2+ controllers); and on a PUTX, because receiving a PUTX while we're in E indicates the child did a silent
                     * transition and now that it is evictiong, it's our turn to maintain M info.
                     */
                    *state = M;
                }
                profGETXHit.inc();
            }
            assert_msg(*state == M, "Wrong final state on GETX, lineId %d numLines %d, finalState %s", lineId, numLines, MESIStateName(*state));
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
    MemReq req = {lineAddr, type, selfId, state, cycle, &ccLock, *state, srcId, flags | MemReq::NONINCLWB};
    uint64_t respCycle = parents[getParentId(lineAddr)]->access(req);
    return respCycle;
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

uint64_t exclusive_MESITopCC::sendInvalidates(Address lineAddr, uint32_t lineId, InvType type, bool* reqWriteback, uint64_t cycle, uint32_t srcId) {
    //Send down downgrades/invalidates
    Entry* e = &array[lineId];

    //Don't propagate downgrades if sharers are not exclusive.
    if (type == INVX && !e->isExclusive()) {
        return cycle;
    }

    uint64_t maxCycle = cycle; //keep maximum cycle only, we assume all invals are sent in parallel
    if (!e->isEmpty()) {
        uint32_t numChildren = children.size();
        uint32_t sentInvs = 0;
        for (uint32_t c = 0; c < numChildren; c++) {
            if (e->sharers[c]) {
                InvReq req = {lineAddr, type, reqWriteback, cycle, srcId};
                uint64_t respCycle = children[c]->invalidate(req);
                respCycle += childrenRTTs[c];
                maxCycle = MAX(respCycle, maxCycle);
                if (type == INV) e->sharers[c] = false;
                sentInvs++;
            }
        }
        assert(sentInvs == e->numSharers);
        if (type == INV) {
            e->numSharers = 0;
        } else {
            //TODO: This is kludgy -- once the sharers format is more sophisticated, handle downgrades with a different codepath
            assert(e->exclusive);
            assert(e->numSharers == 1);
            e->exclusive = false;
        }
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

    Entry* e = &array[lineId];
    uint64_t respCycle = cycle;
    switch (type) {
        case PUTX:
            assert(e->isExclusive());
            if (flags & MemReq::PUTX_KEEPEXCL) {
                assert(e->sharers[childId]);
                assert(*childState == M);
                *childState = E; //they don't hold dirty data anymore
                break; //don't remove from sharer set. It'll keep exclusive perms.
            }
            //note NO break in general
        case PUTS:
            assert(e->sharers[childId]);
            e->sharers[childId] = false;
            e->numSharers--;
            *childState = I;
            break;
        case GETS:
            if (e->isEmpty() && haveExclusive && !(flags & MemReq::NOEXCL)) {
                //Give in E state
                e->exclusive = true;
                e->sharers[childId] = true;
                e->numSharers = 1;
                *childState = E;
            } else {
                //Give in S state
                assert(e->sharers[childId] == false);

                if (e->isExclusive()) {
                    //Downgrade the exclusive sharer
                    respCycle = sendInvalidates(lineAddr, lineId, INVX, inducedWriteback, cycle, srcId);
                }

                assert_msg(!e->isExclusive(), "Can't have exclusivity here. isExcl=%d excl=%d numSharers=%d", e->isExclusive(), e->exclusive, e->numSharers);

                e->sharers[childId] = true;
                e->numSharers++;
                e->exclusive = false; //dsm: Must set, we're explicitly non-exclusive
                *childState = S;
            }
            break;
        case GETX:
            assert(haveExclusive); //the current cache better have exclusive access to this line

            // If child is in sharers list (this is an upgrade miss), take it out
            if (e->sharers[childId]) {
                assert_msg(!e->isExclusive(), "Spurious GETX, childId=%d numSharers=%d isExcl=%d excl=%d", childId, e->numSharers, e->isExclusive(), e->exclusive);
                e->sharers[childId] = false;
                e->numSharers--;
            }

            // Invalidate all other copies
            respCycle = sendInvalidates(lineAddr, lineId, INV, inducedWriteback, cycle, srcId);

            // Set current sharer, mark exclusive
            e->sharers[childId] = true;
            e->numSharers++;
            e->exclusive = true;

            assert(e->numSharers == 1);

            *childState = M; //give in M directly
            break;

        default: panic("!?");
    }

    return respCycle;
}

uint64_t exclusive_MESITopCC::processInval(Address lineAddr, uint32_t lineId, InvType type, bool* reqWriteback, uint64_t cycle, uint32_t srcId) {
    if (type == FWD) {//if it's a FWD, we should be inclusive for now, so we must have the line, just invLat works
        return cycle;
    } else {
        //Just invalidate or downgrade down to children as needed
        return sendInvalidates(lineAddr, lineId, type, reqWriteback, cycle, srcId);
    }
}
