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

#include "cache_arrays.h"
#include "hash.h"
#include "repl_policies.h"

/* Set-associative array implementation */

SetAssocArray::SetAssocArray(uint32_t _numLines, uint32_t _assoc,
                             ReplPolicy* _rp, HashFamily* _hf)
    : rp(_rp), hf(_hf), numLines(_numLines), assoc(_assoc) {
  array = gm_calloc<Address>(numLines);
  numSets = numLines / assoc;
  setMask = numSets - 1;
  assert_msg(isPow2(numSets),
             "must have a power of 2 # sets, but you specified %d", numSets);


}

int32_t SetAssocArray::lookup(const Address lineAddr, const MemReq* req,
                              bool updateReplacement) {

  uint32_t set = hf->hash(0, lineAddr) & setMask;
  uint32_t first = set * assoc;
  for (uint32_t id = first; id < first + assoc; id++) {
    if (array[id] == lineAddr) {
      if (updateReplacement){
        //means it was a cache hit
        rp->update(id, req);
      }


      return id;
    }
  }
  return -1;
}

int32_t SetAssocArray::lookup_norpupdate(const Address lineAddr) {
  uint32_t set = hf->hash(0, lineAddr) & setMask;
  uint32_t first = set * assoc;
  for (uint32_t id = first; id < first + assoc; id++) {
    if (array[id] == lineAddr) {
      return id;
    }
  }
  return -1;
}

uint32_t SetAssocArray::preinsert(
    const Address lineAddr, const MemReq* req,
    Address* wbLineAddr) {  // TODO: Give out valid bit of wb cand?
  uint32_t set = hf->hash(0, lineAddr) & setMask;
  uint32_t first = set * assoc;

  uint32_t candidate = rp->rankCands(req, SetAssocCands(first, first + assoc));

  *wbLineAddr = array[candidate];
  return candidate;
}

void SetAssocArray::postinsert(const Address lineAddr, const MemReq* req,
                               uint32_t candidate) {
  rp->replaced(candidate);
  array[candidate] = lineAddr;
  rp->update(candidate, req);
}

/* Set duelling array */

void FlexclusiveArray::initStats(AggregateStat * parentStat){
    
  AggregateStat* objStats = new AggregateStat();
  objStats->init("array", "FlexclusiveArray stats");
  state_ex.init("ni_to_ex", "No of times set ex policy");
  state_ni.init("ex_to_ni", "Num times set ni policy");
  duel_ni_hits.init("duel_ni_hits", "NI cache line hits during set duelling");
  duel_ni_accesses.init("duel_ni_acc", "NI cache line accesses during set duelling");
  duel_ex_hits.init("duel_ex_hits", "EX cache line hits during set duelling");
  duel_ex_accesses.init("duel_ex_accesses", "EX cache line accesses during set duelling");

  objStats->append(&state_ex);
  objStats->append(&state_ni);
  objStats->append(&duel_ni_hits);
  objStats->append(&duel_ni_accesses);
  objStats->append(&duel_ex_hits);
  objStats->append(&duel_ex_accesses);

  parentStat->append(objStats);

}

FlexclusiveArray::FlexclusiveArray(uint32_t _numLines, uint32_t _assoc,
                                   ReplPolicy* _rp, HashFamily* _hf)
    : rp(_rp), hf(_hf), numLines(_numLines), assoc(_assoc) {
  array = gm_calloc<Address>(numLines);
  numSets = numLines / assoc;
  setMask = numSets - 1;
  assert_msg(isPow2(numSets),
             "must have a power of 2 # sets, but you specified %d", numSets);
}



int32_t FlexclusiveArray::lookup(const Address lineAddr, const MemReq* req,
                                 bool updateReplacement) {
  uint32_t set = hf->hash(0, lineAddr) & setMask;
  uint32_t first = set * assoc;
  for (uint32_t id = first; id < first + assoc; id++) {
    if (array[id] == lineAddr) {
      if (updateReplacement) rp->update(id, req);
      return id;
    }
  }
  return -1;
}

int32_t FlexclusiveArray::lookup_norpupdate(const Address lineAddr) {
  uint32_t set = hf->hash(0, lineAddr) & setMask;
  uint32_t first = set * assoc;
  for (uint32_t id = first; id < first + assoc; id++) {
    if (array[id] == lineAddr) {
      return id;
    }
  }
  return -1;
}

uint32_t FlexclusiveArray::preinsert(
    const Address lineAddr, const MemReq* req,
    Address* wbLineAddr) {  // TODO: Give out valid bit of wb cand?
  uint32_t set = hf->hash(0, lineAddr) & setMask;
  uint32_t first = set * assoc;

  uint32_t candidate = rp->rankCands(req, SetAssocCands(first, first + assoc));

  *wbLineAddr = array[candidate];
  return candidate;
}

void FlexclusiveArray::postinsert(const Address lineAddr, const MemReq* req,
                                  uint32_t candidate) {
  rp->replaced(candidate);
  array[candidate] = lineAddr;
  rp->update(candidate, req);
}

CLUState FlexclusiveArray::getCLU(const Address lineAddr) {
  uint32_t set = hf->hash(0, lineAddr) & setMask;

  if ((set % 16) == 1) {
    return NI;
  } else if ((set % 16) == 0) {
    return EX;
  } else
    return policy;
}

void FlexclusiveArray::updateCounters(const Address lineAddr, uint32_t lineId) {
  uint32_t set = hf->hash(0, lineAddr) & setMask;

  if ((int)lineId != -1) {  // means there was a hit
    if (set % 16 == 1) {
      ni_hit_counter++;
      ni_access_counter++;
      duel_ni_hits.inc();
      duel_ni_accesses.inc();
    } else if (set % 16 == 0) {
      ex_hit_counter++;
      ex_access_counter++;
      duel_ex_hits.inc();
      duel_ex_accesses.inc();
    }
  } else {
    if (set % 16 == 1){
        ni_access_counter++;
        duel_ni_accesses.inc();
    } else if (set % 16 == 0){
        ex_access_counter++;
        duel_ex_accesses.inc();
    }
  }

  access_counter++ ;

  if (access_counter == 10000) {  // calculate next clusion policy

    float ni_hit_rate = (float)ni_hit_counter / (float)ni_access_counter;
    float ex_hit_rate = (float)ex_hit_counter / (float)ex_access_counter;

    if (ni_hit_rate >
        0.9 * ex_hit_rate) {  // if ni does 10 % better, then use that
      policy = NI;
      state_ni.inc();
    } else {
      policy = EX;
      state_ex.inc();
    }

    info("The ni hit rate is %f, The ex hit rate is %f", ni_hit_rate,
         ex_hit_rate);
    info("The ni access counter is %d, the ex access counter is %d",
         ni_access_counter, ex_access_counter);
    info("The ni hit counter is %d, the ex hit counter is %d", ni_hit_counter,
         ex_hit_counter);

    access_counter = 0;
    ni_hit_counter = 0;
    ex_hit_counter = 0;
    ex_access_counter = 0;
    ni_access_counter = 0;
  }
}

/* Line based clusion arrays */
void LineBasedArray::initStats( AggregateStat * parentStat){

  AggregateStat* objStats = new AggregateStat();
  objStats->init("array", "LineBasedArray stats");


  parentStat->append(objStats);
}



LineBasedArray::LineBasedArray(uint32_t _numLines, uint32_t _assoc,
                                   ReplPolicy* _rp, HashFamily* _hf)
    : rp(_rp), hf(_hf), numLines(_numLines), assoc(_assoc) {
  array = gm_calloc<Address>(numLines);
  clu_array = gm_calloc<CLUState>(numLines);
  dup_array = gm_calloc<DUPState>(numLines);
  numSets = numLines / assoc;
  setMask = numSets - 1;
  assert_msg(isPow2(numSets),
             "must have a power of 2 # sets, but you specified %d", numSets);
}

int32_t LineBasedArray::lookup(const Address lineAddr, const MemReq* req,
                                 bool updateReplacement) {
  uint32_t set = hf->hash(0, lineAddr) & setMask;
  uint32_t first = set * assoc;
  for (uint32_t id = first; id < first + assoc; id++) {
    if (array[id] == lineAddr) {
      if (updateReplacement) rp->update(id, req);
          return id;
    }
  }
  return -1;
}

int32_t LineBasedArray::lookup_norpupdate(const Address lineAddr) {
  uint32_t set = hf->hash(0, lineAddr) & setMask;
  uint32_t first = set * assoc;
  for (uint32_t id = first; id < first + assoc; id++) {
    if (array[id] == lineAddr) {
      return id;
    }
  }
  return -1;
}

uint32_t LineBasedArray::preinsert(
    const Address lineAddr, const MemReq* req,
    Address* wbLineAddr) {  // TODO: Give out valid bit of wb cand?
  uint32_t set = hf->hash(0, lineAddr) & setMask;
  uint32_t first = set * assoc;

  uint32_t candidate = rp->rankCands(req, SetAssocCands(first, first + assoc));

  *wbLineAddr = array[candidate];
  return candidate;
}

void LineBasedArray::postinsert(const Address lineAddr, const MemReq* req,
                                  uint32_t candidate) {
  rp->replaced(candidate);
  array[candidate] = lineAddr;
  rp->update(candidate, req);
}

CLUState LineBasedArray::getCLU(uint32_t id) {
    return clu_array[id];
}

void LineBasedArray::setCLU(uint32_t id, CLUState clu){
    clu_array[id] = clu;
}

void LineBasedArray::setDup(uint32_t lineId){
      assert((int)lineId != -1);
      dup_array[lineId] = DUP;
}

void LineBasedArray::setnoDup(uint32_t lineId){
      assert((int)lineId != -1);
      dup_array[lineId] = noDUP;
}

DUPState LineBasedArray::getDup(uint32_t lineId){
      return  dup_array[lineId];
}



/* ZCache implementation */

ZArray::ZArray(uint32_t _numLines, uint32_t _ways, uint32_t _candidates,
               ReplPolicy* _rp, HashFamily* _hf)  //(int _size, int _lineSize,
    // int _assoc, int _zassoc,
    // ReplacementPolicy<T>* _rp,
    // int _hashType)
    : rp(_rp),
      hf(_hf),
      numLines(_numLines),
      ways(_ways),
      cands(_candidates) {
  assert_msg(ways > 1, "zcaches need >=2 ways to work");
  assert_msg(cands >= ways,
             "candidates < ways does not make sense in a zcache");
  assert_msg(numLines % ways == 0, "number of lines is not a multiple of ways");

  // Populate secondary parameters
  numSets = numLines / ways;
  assert_msg(isPow2(numSets),
             "must have a power of 2 # sets, but you specified %d", numSets);
  setMask = numSets - 1;

  lookupArray = gm_calloc<uint32_t>(numLines);
  array = gm_calloc<Address>(numLines);
  for (uint32_t i = 0; i < numLines; i++) {
    lookupArray[i] = i;  // start with a linear mapping; with swaps, it'll get
                         // progressively scrambled
  }
  swapArray = gm_calloc<uint32_t>(
      cands / ways + 2);  // conservative upper bound (tight within 2 ways)
}

void ZArray::initStats(AggregateStat* parentStat) {
  AggregateStat* objStats = new AggregateStat();
  objStats->init("array", "ZArray stats");
  statSwaps.init("swaps", "Block swaps in replacement process");
  objStats->append(&statSwaps);
  parentStat->append(objStats);
}

int32_t ZArray::lookup(const Address lineAddr, const MemReq* req,
                       bool updateReplacement) {
  /* Be defensive: If the line is 0, panic instead of asserting. Now this can
   * only happen on a segfault in the main program, but when we move to full
   * system, phy page 0 might be used, and this will hit us in a very subtle
   * way if we don't check.
   */
  if (unlikely(!lineAddr))
    panic("ZArray::lookup called with lineAddr==0 -- your app just segfaulted");

  for (uint32_t w = 0; w < ways; w++) {
    uint32_t lineId =
        lookupArray[w * numSets + (hf->hash(w, lineAddr) & setMask)];
    if (array[lineId] == lineAddr) {
      if (updateReplacement) {
        rp->update(lineId, req);
      }
      return lineId;
    }
  }
  return -1;
}

uint32_t ZArray::preinsert(const Address lineAddr, const MemReq* req,
                           Address* wbLineAddr) {
  ZWalkInfo candidates[cands + ways];  // extra ways entries to avoid checking
                                       // on every expansion

  bool all_valid = true;
  uint32_t fringeStart = 0;
  uint32_t numCandidates = ways;  // seeds

  // info("Replacement for incoming 0x%lx", lineAddr);

  // Seeds
  for (uint32_t w = 0; w < ways; w++) {
    uint32_t pos = w * numSets + (hf->hash(w, lineAddr) & setMask);
    uint32_t lineId = lookupArray[pos];
    candidates[w].set(pos, lineId, -1);
    all_valid &= (array[lineId] != 0);
    // info("Seed Candidate %d addr 0x%lx pos %d lineId %d", w, array[lineId],
    // pos, lineId);
  }

  // Expand fringe in BFS fashion
  while (numCandidates < cands && all_valid) {
    uint32_t fringeId = candidates[fringeStart].lineId;
    Address fringeAddr = array[fringeId];
    assert(fringeAddr);
    for (uint32_t w = 0; w < ways; w++) {
      uint32_t hval = hf->hash(w, fringeAddr) & setMask;
      uint32_t pos = w * numSets + hval;
      uint32_t lineId = lookupArray[pos];

// Logically, you want to do this...
#if 0
            if (lineId != fringeId) {
                //info("Candidate %d way %d addr 0x%lx pos %d lineId %d parent %d", numCandidates, w, array[lineId], pos, lineId, fringeStart);
                candidates[numCandidates++].set(pos, lineId, (int32_t)fringeStart);
                all_valid &= (array[lineId] != 0);
            }
#endif
      // But this compiles as a branch and ILP sucks (this data-dependent branch
      // is long-latency and mispredicted often)
      // Logically though, this is just checking for whether we're revisiting
      // ourselves, so we can eliminate the branch as follows:
      candidates[numCandidates].set(pos, lineId, (int32_t)fringeStart);
      all_valid &= (array[lineId] != 0);  // no problem, if lineId == fringeId
                                          // the line's already valid, so no
                                          // harm done
      numCandidates += (lineId != fringeId);  // if lineId == fringeId, the cand
                                              // we just wrote will be
                                              // overwritten
    }
    fringeStart++;
  }

  // Get best candidate (NOTE: This could be folded in the code above, but it's
  // messy since we can expand more than zassoc elements)
  assert(!all_valid || numCandidates >= cands);
  numCandidates = (numCandidates > cands) ? cands : numCandidates;

  // info("Using %d candidates, all_valid=%d", numCandidates, all_valid);

  uint32_t bestCandidate =
      rp->rankCands(req, ZCands(&candidates[0], &candidates[numCandidates]));
  assert(bestCandidate < numLines);

  // Fill in swap array

  // Get the *minimum* index of cands that matches lineId. We need the minimum
  // in case there are loops (rare, but possible)
  uint32_t minIdx = -1;
  for (uint32_t ii = 0; ii < numCandidates; ii++) {
    if (bestCandidate == candidates[ii].lineId) {
      minIdx = ii;
      break;
    }
  }
  assert(minIdx >= 0);
  // info("Best candidate is %d lineId %d", minIdx, bestCandidate);

  lastCandIdx =
      minIdx;  // used by timing simulation code to schedule array accesses

  int32_t idx = minIdx;
  uint32_t swapIdx = 0;
  while (idx >= 0) {
    swapArray[swapIdx++] = candidates[idx].pos;
    idx = candidates[idx].parentIdx;
  }
  swapArrayLen = swapIdx;
  assert(swapArrayLen > 0);

  // Write address of line we're replacing
  *wbLineAddr = array[bestCandidate];

  return bestCandidate;
}

void ZArray::postinsert(const Address lineAddr, const MemReq* req,
                        uint32_t candidate) {
  // We do the swaps in lookupArray, the array stays the same
  assert(lookupArray[swapArray[0]] == candidate);
  for (uint32_t i = 0; i < swapArrayLen - 1; i++) {
    // info("Moving position %d (lineId %d) <- %d (lineId %d)", swapArray[i],
    // lookupArray[swapArray[i]], swapArray[i+1], lookupArray[swapArray[i+1]]);
    lookupArray[swapArray[i]] = lookupArray[swapArray[i + 1]];
  }
  lookupArray[swapArray[swapArrayLen - 1]] =
      candidate;  // note that in preinsert() we walk the array backwards when
                  // populating swapArray, so the last elem is where the new
                  // line goes
  // info("Inserting lineId %d in position %d", candidate,
  // swapArray[swapArrayLen-1]);

  rp->replaced(candidate);
  array[candidate] = lineAddr;
  rp->update(candidate, req);

  statSwaps.inc(swapArrayLen - 1);
}
