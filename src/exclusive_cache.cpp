#include "exclusive_cache.h"
#include "hash.h"

#include "event_recorder.h"
#include "timing_event.h"
#include "zsim.h"

exclusive_cache :: exclusive_cache(uint32_t _numLines, CC* _cc, CacheArray* _array, ReplPolicy* _rp, uint32_t _accLat, uint32_t _invLat, uint32_t mshrs,   uint32_t tagLat, uint32_t ways, uint32_t cands, uint32_t _domain, const g_string& _name)
    : Cache(_numLines, _cc, _array, _rp, _accLat, _invLat, _name) {

//do the exclusive cache access here
//also need to test multiple levels of non_inclusive
//timing cache


}

void exclusive_cache :: setasLLC(){ //invoked at cache creation only

  Cache::setLLCflag();   //access parent cache llc set function

}

uint64_t exclusive_cache :: access( MemReq & req ){ //only for GETS and GETX

  uint64_t respCycle = req.cycle;
            //request and response cycle

  bool skipAccess = cc->startAccess(req); //may need to skip access due to races (NOTE: may change req.type!)

  if (likely(!skipAccess)) {

        bool updateReplacement = (req.type == PUTS) || (req.type == PUTX); //writebacks update the replacement info
                                                                           //for exclusive cache
                                                                           //terminal cache never gets this
                                                                           //if GETS and GETX miss, then there is
                                                                           //no change in replacement, else
                                                                           //they are evicted in case of hit

        int32_t lineId = array->lookup(req.lineAddr, &req, updateReplacement);
        respCycle += accLat;

        if(llc){
             if(cc->search_inner_banks(req.lineAddr, req.childId))
                 req.flags |= MemReq::INNER_COPY; //says that the private caches had a copy
        }

        if (lineId == -1) { //if we did not find the line in the cache
                            //if load or store, we need to check private levels
                            //to find the line if llc otherwise nothing here
                            //if writeback, we need to put the data into the
                            //cache array


            if (req.type == GETS || req.type == GETX){
                 // do nothing here, we need to access the next level in our search
                 // or do the check if it is an LLC
                 //this code works even if there is only 1 level of cache
                  ;
            }
            else{
                  //in case of PUTX or PUTS
                  //we need to get the line so that we can do the writeback later
                  Address wbLineAddr;
                  lineId = array->preinsert(req.lineAddr, &req, &wbLineAddr); //find the lineId to replace

                  cc->processEviction(req, wbLineAddr, lineId, respCycle); //Never sends invalidates because exclusive cache
                  array->postinsert(req.lineAddr, &req, lineId);
            }
       }

       respCycle = cc->processAccess(req, lineId, respCycle);

  }

  cc->endAccess(req);
  assert_msg(respCycle >= req.cycle, "[%s] resp < req? 0x%lx type %s childState %s, respCycle %ld reqCycle %ld",
            name.c_str(), req.lineAddr, AccessTypeName(req.type), MESIStateName(*req.state), respCycle, req.cycle);

  return respCycle;

}

uint64_t exclusive_cache :: lookup(const Address lineAddr){

      int32_t lineId = array->lookup_norpupdate(lineAddr);
      if ((lineId != -1) && cc->isValid(lineId)) return 1; //means we found an address value
                                  //but we should also lookup the coherence state
                                  //if I, then the line was not useful
                                  //add this to future versions
                                  //for more accurate results
      else return 0;
}

uint64_t exclusive_cache :: pushEvictedData(){ // for PUTS and PUTX
                                               // we can interpret writebacks as pushing evicted data
                                               // this saves us coding time
                                               // we can use this function in future iterations
                                               // of the code

    //push evicted data to the level below
    //i.e. to the parent

    //different from writebacks, because there is no bypassing
    //also, this updates the replacement information, unlike writebacks

    //we need to define the function for main memory also

    //uint64_t respCycle = req.cycle;
    //request and response cycle

    //bool updateReplacement = true ; //this access should update the replacement information

    //non inclusive writeback -- maybe it can be handled differently instead of defining the push evicted data method

    return 0;
}
