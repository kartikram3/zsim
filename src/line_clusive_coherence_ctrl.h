// Non inclusive cache code written by Kartik

#ifndef line_clusive_COHERENCE_CTRLS_H_
#define line_clusive_COHERENCE_CTRLS_H_

#include "coherence_ctrls.h"

class CC;  // forward declaration of CC
// in coherence_ctrls.h
class Cache;
class Network;

static inline bool line_clusive_CheckForMESIRace(AccessType& type,
                                                 MESIState* state,
                                                 MESIState initialState) {
  // NOTE: THIS IS THE ONLY CODE THAT SHOULD DEAL WITH RACES. tcc, bcc et al
  // should be written as if they were race-free.
  bool skipAccess = false;
  if (*state != initialState) {
    // info("[%s] Race on line 0x%lx, %s by childId %d, was state %s, now %s",
    // name.c_str(), lineAddr, accessTypeNames[type], childId,
    // mesiStateNames[initialState], mesiStateNames[*state]);
    // An intervening invalidate happened! Two types of races:
    if (type == PUTS || type == PUTX) {  // either it is a PUT...
      // We want to get rid of this line
      if (*state == I) {
        // If it was already invalidated (INV), just skip access altogether,
        // we're already done
        skipAccess = true;
      } else {
        // We were downgraded (INVX), still need to do the PUT
        assert(*state == S);
        // If we wanted to do a PUTX, just change it to a PUTS b/c now the line
        // is not exclusive anymore
        if (type == PUTX) type = PUTS;
      }
    } else if (type == GETX) {  //...or it is a GETX
      // In this case, the line MUST have been in S and have been INValidated
      assert(initialState == S);
      assert(*state == I);
      // Do nothing. This is still a valid GETX, only it is not an upgrade miss
      // anymore
    } else {  // no GETSs can race with INVs, if we are doing a GETS it's
              // because the line was invalid to begin with!
      panic("Invalid true race happened (?)");
    }
  }
  return skipAccess;
}

class line_clusive_MESIBottomCC : public GlobAlloc {
 private:
  MESIState* array;
  g_vector<MemObject*> parents;
  g_vector<uint32_t> parentRTTs;
  uint32_t numLines;
  uint32_t selfId;

  // Profiling counters
  Counter profGETSHit, profGETSMiss, profGETXHit,
      profGETXMissIM /*from invalid*/,
      profGETXMissSM /*from S, i.e. upgrade misses*/;
  Counter profPUTS, profPUTX /*received from downstream*/;
  Counter profINV, profINVX, profFWD /*received from upstream*/;
  // Counter profWBIncl, profWBCoh /* writebacks due to inclusion or coherence,
  // received from downstream, does not include PUTS */;
  // TODO: Measuring writebacks is messy, do if needed
  Counter profGETNextLevelLat, profGETNetLat;

  PAD();
  lock_t ccLock;
  PAD();

 public:
  line_clusive_MESIBottomCC(uint32_t _numLines, uint32_t _selfId)
      : numLines(_numLines), selfId(_selfId) {
    array = gm_calloc<MESIState>(numLines);
    for (uint32_t i = 0; i < numLines; i++) {
      array[i] = I;
    }
    futex_init(&ccLock);
  }

  void init(const g_vector<MemObject*>& _parents, Network* network,
            const char* name);

  inline bool isExclusive(uint32_t lineId) {
    MESIState state = array[lineId];
    return (state == E) || (state == M);
  }

  void initStats(AggregateStat* parentStat) {
    profGETSHit.init("hGETS", "GETS hits");
    profGETXHit.init("hGETX", "GETX hits");
    profGETSMiss.init("mGETS", "GETS misses");
    profGETXMissIM.init("mGETXIM", "GETX I->M misses");
    profGETXMissSM.init("mGETXSM", "GETX S->M misses (upgrade misses)");
    profPUTS.init("PUTS", "Clean evictions (from lower level)");
    profPUTX.init("PUTX", "Dirty evictions (from lower level)");
    profINV.init("INV", "Invalidates (from upper level)");
    profINVX.init("INVX", "Downgrades (from upper level)");
    profFWD.init("FWD", "Forwards (from upper level)");
    profGETNextLevelLat.init("latGETnl", "GET request latency on next level");
    profGETNetLat.init("latGETnet",
                       "GET request latency on network to next level");

    parentStat->append(&profGETSHit);
    parentStat->append(&profGETXHit);
    parentStat->append(&profGETSMiss);
    parentStat->append(&profGETXMissIM);
    parentStat->append(&profGETXMissSM);
    parentStat->append(&profPUTS);
    parentStat->append(&profPUTX);
    parentStat->append(&profINV);
    parentStat->append(&profINVX);
    parentStat->append(&profFWD);
    parentStat->append(&profGETNextLevelLat);
    parentStat->append(&profGETNetLat);
  }

  uint64_t processEviction(Address wbLineAddr, uint32_t lineId,
                           bool lowerLevelWriteback, uint64_t cycle,
                           uint32_t srcId);

  uint64_t processAccess(Address lineAddr, uint32_t lineId, AccessType type,
                         uint64_t cycle, uint32_t srcId, uint32_t flags);

  void processWritebackOnAccess(Address lineAddr, uint32_t lineId,
                                AccessType type);

  void processInval(Address lineAddr, uint32_t lineId, InvType type,
                    bool* reqWriteback);

  uint64_t processNonInclusiveWriteback(Address lineAddr, AccessType type,
                                        uint64_t cycle, MESIState* state,
                                        uint32_t srcId, uint32_t flags);

  inline void lock() { futex_lock(&ccLock); }

  inline void unlock() { futex_unlock(&ccLock); }

  /* Replacement policy query interface */
  inline bool isValid(uint32_t lineId) { return array[lineId] != I; }

  // Could extend with isExclusive, isDirty, etc, but not needed for now.

 private:
  uint32_t getParentId(Address lineAddr);
};

class line_clusive_MESITopCC : public GlobAlloc {
 private:
  struct Entry {
    uint32_t numSharers;
    std::bitset<MAX_CACHE_CHILDREN> sharers;
    bool exclusive;

    void clear() {
      exclusive = false;
      numSharers = 0;
      sharers.reset();
    }

    bool isEmpty() { return numSharers == 0; }

    bool isExclusive() { return (numSharers == 1) && (exclusive); }
  };

  Entry* array;
  g_vector<BaseCache*> children;
  g_vector<uint32_t> childrenRTTs;
  g_vector<uint32_t> valid_children;  // checks which children are valid
  // even if line is not in the tcc
  // e cannot tell us this
  // updated by the search_inner_banks
  // function
  uint32_t numLines;

  PAD();
  lock_t ccLock;
  PAD();

 public:
  line_clusive_MESITopCC(uint32_t _numLines) : numLines(_numLines) {
    array = gm_calloc<Entry>(numLines);
    for (uint32_t i = 0; i < numLines; i++) {
      array[i].clear();
    }

    futex_init(&ccLock);
  }

  void init(const g_vector<BaseCache*>& _children, Network* network,
            const char* name);

  uint64_t processEviction(Address wbLineAddr, uint32_t lineId,
                           bool* reqWriteback, uint64_t cycle, uint32_t srcId);

  uint64_t processAccess(Address lineAddr, uint32_t lineId, AccessType type,
                         uint32_t childId, bool haveExclusive,
                         MESIState* childState, bool* inducedWriteback,
                         uint64_t cycle, uint32_t srcId, uint32_t flags,
                         bool isValid);

  uint64_t processInval(Address lineAddr, uint32_t lineId, InvType type,
                        bool* reqWriteback, uint64_t cycle, uint32_t srcId);

  uint64_t search_inner_banks(const Address lineAddr, uint32_t childId) {
    // reset the inner bank avail list
    valid_children.clear();
    uint32_t numChildren = children.size();
    bool result = 0;
    for (uint32_t c = 0; c < numChildren; c++) {
      if (c == childId) {
        continue;
      }
      int32_t lineId =
          children[c]->lookup(lineAddr);  // looks up the line address
      // set the inner bank avail list
      // info("Found lineid %d", lineId);
      if ((lineId != -1)) {
        result = 1;  // means we found the line
        valid_children.push_back(c);
      }
    }
    return result;
  }

  inline void lock() { futex_lock(&ccLock); }

  inline void unlock() { futex_unlock(&ccLock); }

  /* Replacement policy query interface */
  inline uint32_t numSharers(uint32_t lineId) {
    return array[lineId].numSharers;
  }

 private:
  uint64_t sendInvalidates(Address lineAddr, uint32_t lineId, InvType type,
                           bool* reqWriteback, uint64_t cycle, uint32_t srcId,
                           bool non_incl);
};

// non-inclusive version of the coherence
// controller (non-terminal)
class line_clusive_MESICC : public CC {
 private:
  line_clusive_MESITopCC* tcc;
  line_clusive_MESIBottomCC* bcc;
  uint32_t numLines;
  g_string name;

 public:
  line_clusive_MESICC(uint32_t _numLines, g_string& _name)
      : tcc(nullptr), bcc(nullptr), numLines(_numLines), name(_name) {}

  void setParents(uint32_t childId, const g_vector<MemObject*>& parents,
                  Network* network) {
    bcc = new line_clusive_MESIBottomCC(numLines, childId);
    bcc->init(parents, network, name.c_str());
  }

  void setChildren(const g_vector<BaseCache*>& children, Network* network) {
    tcc = new line_clusive_MESITopCC(numLines);
    tcc->init(children, network, name.c_str());
  }

  void initStats(AggregateStat* cacheStat) {
    // no tcc stats
    bcc->initStats(cacheStat);
  }

  // Access methods
  bool startAccess(MemReq& req) {
    assert((req.type == GETS) || (req.type == GETX) || (req.type == PUTS) ||
           (req.type == PUTX));

    /* Child should be locked when called. We do hand-over-hand locking when
     * going
     * down (which is why we require the lock), but not when going up, opening
     * the
     * child to invalidation races here to avoid deadlocks.
     */
    if (req.childLock) {
      futex_unlock(req.childLock);
    }

    tcc->lock();  // must lock tcc FIRST
    bcc->lock();

    /* The situation is now stable, true race-wise. No one can touch the child
     * state, because we hold
     * both parent's locks. So, we first handle races, which may cause us to
     * skip the access.
     */
    bool skipAccess = line_clusive_CheckForMESIRace(
        req.type /*may change*/, req.state, req.initialState);
    return skipAccess;
  }

  bool shouldAllocate(const MemReq& req) {
    if ((req.type == GETS) || (req.type == GETX)) {
      return true;
    } else {
      assert((req.type == PUTS) || (req.type == PUTX));
      return false;
    }
  }

  uint64_t processEviction(const MemReq& triggerReq, Address wbLineAddr,
                           int32_t lineId, uint64_t startCycle) {
    bool lowerLevelWriteback = false;
    uint64_t evCycle = tcc->processEviction(
        wbLineAddr, lineId, &lowerLevelWriteback, startCycle,
        triggerReq.srcId);  // 1. if needed, send invalidates/downgrades to
                            // lower level
    evCycle = bcc->processEviction(
        wbLineAddr, lineId, lowerLevelWriteback, evCycle,
        triggerReq.srcId);  // 2. if needed, write back line to upper level
    return evCycle;
  }

  uint64_t processAccess(const MemReq& req, int32_t lineId, uint64_t startCycle,
                         uint64_t* getDoneCycle = nullptr, CLUState cs = NA) {
    uint64_t respCycle = startCycle;
    // Handle non-inclusive writebacks by bypassing
    // NOTE: Most of the time, these are due to evictions, so the line is not
    // there. But the second condition can trigger in NUCA-initiated
    // invalidations. The alternative with this would be to capture these
    // blocks, since we have space anyway. This is so rare is doesn't matter,
    // but if we do proper NI/EX mid-level caches backed by directories, this
    // may start becoming more common (and it is perfectly acceptable to
    // upgrade without any interaction with the parent... the child had the
    // permissions!)
    if (lineId == -1) {  // can only be a non-inclusive wback
      // assert((req.type == PUTS) || (req.type == PUTX));
      assert(req.type == GETS || req.type == GETX);
      uint32_t flags = req.flags & ~MemReq::PREFETCH;  // always clear PREFETCH,
                                                       // this flag cannot
                                                       // propagate up
      respCycle = bcc->processAccess(req.lineAddr, lineId, req.type, startCycle,
                                     req.srcId, flags);
      if (getDoneCycle) *getDoneCycle = respCycle;
      bool haveExclusive = true;
      bool lowerLevelWriteback = false;
      respCycle =
          tcc->processAccess(req.lineAddr, lineId, req.type, req.childId,
                             haveExclusive, req.state, &lowerLevelWriteback,
                             respCycle, req.srcId, flags, false);
      // respCycle = bcc->processNonInclusiveWriteback(req.lineAddr, req.type,
      // startCycle, req.state, req.srcId, req.flags);
    } else {
      // Prefetches are side requests and get handled a bit differently
      bool isPrefetch = req.flags & MemReq::PREFETCH;
      assert(!isPrefetch || req.type == GETS);
      uint32_t flags = req.flags & ~MemReq::PREFETCH;  // always clear PREFETCH,
                                                       // this flag cannot
                                                       // propagate up

      // if needed, fetch line or upgrade miss from upper level
      respCycle = bcc->processAccess(req.lineAddr, lineId, req.type, startCycle,
                                     req.srcId, flags);
      if (getDoneCycle) *getDoneCycle = respCycle;
      if (!isPrefetch) {  // prefetches only touch bcc; the demand request from
                          // the core will pull the line to lower level
        // At this point, the line is in a good state w.r.t. upper levels
        bool lowerLevelWriteback = false;
        // change directory info, invalidate other children if needed, tell
        // requester about its state
        respCycle = tcc->processAccess(
            req.lineAddr, lineId, req.type, req.childId,
            bcc->isExclusive(lineId), req.state, &lowerLevelWriteback,
            respCycle, req.srcId, flags, bcc->isValid(lineId));
        if (lowerLevelWriteback) {
          // Essentially, if tcc induced a writeback, bcc may need to do an E->M
          // transition to reflect that the cache now has dirty data
          bcc->processWritebackOnAccess(req.lineAddr, lineId, req.type);
        }
      }
    }
    return respCycle;
  }

  void endAccess(const MemReq& req) {
    // Relock child before we unlock ourselves (hand-over-hand)
    if (req.childLock) {
      futex_lock(req.childLock);
    }

    bcc->unlock();
    tcc->unlock();
  }

  // Inv methods
  void startInv() {
    bcc->lock();  // note we don't grab tcc; tcc serializes multiple up
                  // accesses, down accesses don't see it
  }

  void dummy() {}

  // Search methods

  uint64_t search_inner_banks(const Address lineAddr, uint32_t childId) {
    return tcc->search_inner_banks(lineAddr, childId);
  }

  void unlock_bcc() { bcc->unlock(); }
  //        void startSnoop(){
  //
  //
  //        }
  //
  //        void processSnoop( SnoopReq& req ){
  //
  //        }
  //
  uint64_t processInv(const InvReq& req, int32_t lineId, uint64_t startCycle) {
    uint64_t respCycle = tcc->processInval(
        req.lineAddr, lineId, req.type, req.writeback, startCycle,
        req.srcId);  // send invalidates or downgrades to children
    bcc->processInval(req.lineAddr, lineId, req.type,
                      req.writeback);  // adjust our own state

    bcc->unlock();
    return respCycle;
  }

  // Repl policy interface
  uint32_t numSharers(uint32_t lineId) { return tcc->numSharers(lineId); }
  bool isValid(uint32_t lineId) { return bcc->isValid(lineId); }
};

class line_clusive_MESITerminalCC : public CC {
 private:
  line_clusive_MESIBottomCC* bcc;
  uint32_t numLines;
  g_string name;

 public:
  line_clusive_MESITerminalCC(uint32_t _numLines, const g_string& _name)
      : bcc(nullptr), numLines(_numLines), name(_name) {}

  void setParents(uint32_t childId, const g_vector<MemObject*>& parents,
                  Network* network) {
    bcc = new line_clusive_MESIBottomCC(numLines, childId);
    bcc->init(parents, network, name.c_str());
  }

  void setChildren(const g_vector<BaseCache*>& children, Network* network) {
    panic(
        "[%s] MESITerminalCC::setChildren cannot be called -- terminal caches "
        "cannot have children!",
        name.c_str());
  }

  void initStats(AggregateStat* cacheStat) { bcc->initStats(cacheStat); }

  // Access methods
  bool startAccess(MemReq& req) {
    assert((req.type == GETS) || (req.type == GETX));  // no puts!

    /* Child should be locked when called. We do hand-over-hand locking when
     * going
     * down (which is why we require the lock), but not when going up, opening
     * the
     * child to invalidation races here to avoid deadlocks.
     */
    if (req.childLock) {
      futex_unlock(req.childLock);
    }

    bcc->lock();

    /* The situation is now stable, true race-wise. No one can touch the child
     * state, because we hold
     * both parent's locks. So, we first handle races, which may cause us to
     * skip the access.
     */
    bool skipAccess = line_clusive_CheckForMESIRace(
        req.type /*may change*/, req.state, req.initialState);
    return skipAccess;
  }

  bool shouldAllocate(const MemReq& req) { return true; }

  uint64_t processEviction(const MemReq& triggerReq, Address wbLineAddr,
                           int32_t lineId, uint64_t startCycle) {
    bool lowerLevelWriteback = false;
    uint64_t endCycle = bcc->processEviction(
        wbLineAddr, lineId, lowerLevelWriteback, startCycle,
        triggerReq.srcId);  // 2. if needed, write back line to upper level
    return endCycle;  // critical path unaffected, but TimingCache needs it
  }

  uint64_t processAccess(const MemReq& req, int32_t lineId, uint64_t startCycle,
                         uint64_t* getDoneCycle = nullptr, CLUState cs = NA) {
    assert(lineId != -1);
    assert(!getDoneCycle);
    // if needed, fetch line or upgrade miss from upper level
    uint64_t respCycle = bcc->processAccess(req.lineAddr, lineId, req.type,
                                            startCycle, req.srcId, req.flags);
    // at this point, the line is in a good state w.r.t. upper levels
    return respCycle;
  }

  void endAccess(const MemReq& req) {
    // Relock child before we unlock ourselves (hand-over-hand)
    if (req.childLock) {
      futex_lock(req.childLock);
    }
    bcc->unlock();
  }

  // Inv methods
  void startInv() { bcc->lock(); }

  void dummy() {}

  // Search methods

  uint64_t search_inner_banks(const Address lineAddr, uint32_t childId) {
    return 0;
  }

  void unlock_bcc() { bcc->unlock(); }
  //        void startSnoop(){
  //
  //        }
  //
  //        void processSnoop( SnoopReq & req  ){
  //
  //        }
  //
  uint64_t processInv(const InvReq& req, int32_t lineId, uint64_t startCycle) {
    bcc->processInval(req.lineAddr, lineId, req.type,
                      req.writeback);  // adjust our own state
    bcc->unlock();
    return startCycle;  // no extra delay in terminal caches
  }

  // Repl policy interface
  uint32_t numSharers(uint32_t lineId) { return 0; }  // no sharers
  bool isValid(uint32_t lineId) { return bcc->isValid(lineId); }
};

#endif
