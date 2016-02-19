/* non inclusive cache class */

#ifndef TIMING_EXCLUSIVE_CACHE_H
#define TIMING_EXCLUSIVE CACHE_H

#include "breakdown_stats.h"
#include "cache.h"

class HitEvent;
class MissStartEvent;
class MissResponseEvent;
class MissWritebackEvent;
class ReplAccessEvent;
class TimingEvent;

class timing_exclusive_cache : public Cache {


 private:
  uint64_t lastAccCycle, lastFreeCycle;
  uint32_t numMSHRs, activeMisses;
  g_vector<TimingEvent*> pendingQueue;

  // Stats
  CycleBreakdownStat profOccHist;
  Counter profHitLat, profMissRespLat, profMissLat;

  uint32_t domain;

  // For zcache replacement simulation (pessimistic, assumes we walk the whole
  // tree)
  uint32_t tagLat, ways, cands;

  PAD();
  lock_t topLock;
  PAD();

 public:
  timing_exclusive_cache(uint32_t _numLines, CC* _cc, CacheArray* _array,
                  ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat,
                  uint32_t mshrs, uint32_t tagLat, uint32_t ways,
                  uint32_t cands, uint32_t _domain, const g_string& _name);


  void initStats(AggregateStat* parentStat);

  uint64_t access(MemReq& req);

  uint64_t pushEvictedData();

  void setasLLC();

  uint64_t lookup(const Address lineAddr);

  void simulateHit(HitEvent* ev, uint64_t cycle);
  void simulateMissStart(MissStartEvent* ev, uint64_t cycle);
  void simulateMissResponse(MissResponseEvent* ev, uint64_t cycle,
                            MissStartEvent* mse);
  void simulateMissWriteback(MissWritebackEvent* ev, uint64_t cycle,
                             MissStartEvent* mse);
  void simulateReplAccess(ReplAccessEvent* ev, uint64_t cycle);

 private:
  uint64_t highPrioAccess(uint64_t cycle);
  uint64_t tryLowPrioAccess(uint64_t cycle);

};

#endif  // EXCLUSIVE_TIMING_CACHE_H
