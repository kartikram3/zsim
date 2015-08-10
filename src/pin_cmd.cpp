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

#include "pin_cmd.h"
#include <iostream>
#include <sstream>
#include <string>
#include <wordexp.h> //for posix-shell command expansion
#include "config.h"
#include <stdio.h>
#include <string.h>

//Funky macro expansion stuff
#define QUOTED_(x) #x
#define QUOTED(x) QUOTED_(x)

PinCmd::PinCmd(Config* conf, const char* configFile, const char* outputDir, uint64_t shmid) {
    //Figure the program paths
    conf = conf;
    shm=g_string((std::to_string(shmid)).c_str());
    oDir = g_string( outputDir);
    if(configFile)
    cfg = g_string( configFile); 
    nullapp_path = conf->get<const char *>("sim.nullapp_path", "");

    info("output dir is %s", outputDir);


    /*zsimEnvPath = getenv("ZSIM_PATH");
    if (zsimEnvPath) {
        info("Using env path %s", zsimEnvPath);
        pinPath = zsimEnvPath;
        pinPath += "/pinbin";
        zsimPath = zsimEnvPath;
        zsimPath += "/libzsim.so";
    } else {
        pinPath = QUOTED(PIN_PATH);
        zsimPath = QUOTED(ZSIM_PATH);

    //pinPath = "/home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/pin";
    args.push_back(pinPath);

    }*/
    
    /*if(use_pinplay){ 
        args.push_back("-xyzzy");
        args.push_back("-reserve_memory");

        args.push_back("/home/kartik/Prefetch_Simulator/PINBALL_GHENT_1/INTcpu2006-pinpoints-w100M-d30M-m10/cpu2006-astar_1-ref-1.pp/cpu2006-astar_1-ref-1_t0r6_warmup100001500_prolog0_region30000016_epilog0_006_0-19071.0.address");


    //Global pin options
        args.push_back("-follow_execv"); //instrument child processes
        args.push_back("-tool_exit_timeout"); //don't wait much of internal threads
        args.push_back("1");

    //Load tool
        args.push_back("-t");
        args.push_back(zsimPath);

        args.push_back("-replay");
        args.push_back("-xyzzy");
        args.push_back("-replay:basename");

        args.push_back("/home/kartik/Prefetch_Simulator/PINBALL_GHENT_1/INTcpu2006-pinpoints-w100M-d30M-m10/cpu2006-astar_1-ref-1.pp/cpu2006-astar_1-ref-1_t0r6_warmup100001500_prolog0_region30000016_epilog0_006_0-19071.0");
        args.push_back("-replay:playout");
        args.push_back("0");
        args.push_back("-log:mt");
        args.push_back("0");

    }else{

    //Global pin options
        args.push_back("-follow_execv"); //instrument child processes
        args.push_back("-tool_exit_timeout"); //don't wait much of internal threads
        args.push_back("1");

    //Load tool
        args.push_back("-t");
        args.push_back(zsimPath);
      */

    //Additional options (e.g., -smc_strict for Java), parsed from config
        const char* pinOptions = conf->get<const char*>("sim.pinOptions", "");
        wordexp_t p;
        wordexp(pinOptions, &p, 0);
        for (uint32_t i = 0; i < p.we_wordc; i++) {
            pinoptions.push_back(g_string(p.we_wordv[i]));
        }
        wordfree(&p);
       
        //Tool options
    /*    if (configFile) {
            //Check configFile is an absolute path
            //NOTE: We check rather than canonicalizing it ourselves because by the time we're created, we might be in another directory
            char* absPath = realpath(configFile, nullptr);
            if (std::string(configFile) != std::string(absPath)) {
                panic("Internal zsim bug, configFile should be absolute");
            }
            free(absPath);
       
            args.push_back("-config");
            args.push_back(configFile);
        }
       
        args.push_back("-outputDir");
        args.push_back(outputDir);
       
        std::stringstream shmid_ss;
        shmid_ss << shmid;
    
        if (conf->get<bool>("sim.logToFile", false)) {
            args.push_back("-logToFile");
        }
   
        args.push_back("-shmid");
        args.push_back(shmid_ss.str().c_str());
       

    }*/
    if (conf->get<bool>("sim.logToFile", false)) {
      //args.push_back("-logToFile");
      logToFile=true;
    }

    info("H1");
    //Read the per-process params of the processes run directly by the harness
    while (true) {
        std::stringstream p_ss;
        p_ss << "process" << procInfo.size();

        info ("p_ss is %s", p_ss.str().c_str());

        if (!conf->exists(p_ss.str().c_str())) break;

        const char* cmd = conf->get<const char*>(p_ss.str() +  ".command");
        const char* input = conf->get<const char*>(p_ss.str() +  ".input", "");
        const char* loader = conf->get<const char*>(p_ss.str() +  ".loader", "");
        const char* env = conf->get<const char*>(p_ss.str() +  ".env", "");
        use_pinplay = conf->get<bool>(p_ss.str() + ".use_pinplay", true);
        const char* pinplay_arg_1 = conf->get<const char *>(p_ss.str() + ".pinplay_arg_1");
        const char* pinplay_arg_2 = conf->get<const char *>(p_ss.str() + ".pinplay_arg_2");

        ProcCmdInfo pi = {g_string(cmd), g_string(input), g_string(loader), g_string(env), use_pinplay,
                            g_string(pinplay_arg_1), g_string(pinplay_arg_2)};
        procInfo.push_back(pi);
    }

    info("H2");
}

g_vector<g_string> PinCmd::getPinCmdArgs(uint32_t procIdx) {

    args.clear();
    g_string pinPath,zsimPath;
    const char * zsimEnvPath;
    zsimEnvPath = getenv("ZSIM_PATH");
    if (zsimEnvPath) {
        info("Using env path %s", zsimEnvPath);
        pinPath = zsimEnvPath;
        pinPath += "/pinbin";
        zsimPath = zsimEnvPath;
        zsimPath += "/libzsim.so";
    } else {
        pinPath = QUOTED(PIN_PATH);
        zsimPath = QUOTED(ZSIM_PATH);

    //pinPath = "/home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/pin";
    args.push_back(pinPath);

    }

    info("H3");

    use_pinplay= procInfo[procIdx].use_pinplay;
    const char * pinplay_arg_1 = procInfo[procIdx].pinplay_arg_1.c_str();
    const char * pinplay_arg_2 = procInfo[procIdx].pinplay_arg_2.c_str();
    if(use_pinplay) {info ("using pinplay");}
    else info ("no pinplay");

    if(use_pinplay){
        args.push_back("-xyzzy");
        args.push_back("-reserve_memory");

        args.push_back(pinplay_arg_1);


    //Global pin options
        args.push_back("-follow_execv"); //instrument child processes
        args.push_back("-tool_exit_timeout"); //don't wait much of internal threads
        args.push_back("1");

    //Load tool
        args.push_back("-t");
        args.push_back(zsimPath);

        args.push_back("-replay");
        args.push_back("-xyzzy");
        args.push_back("-replay:basename");

        args.push_back(pinplay_arg_2);
        args.push_back("-replay:playout");
        args.push_back("0");
        args.push_back("-log:mt");
        args.push_back("0");
    }else{

    //Global pin options
        args.push_back("-follow_execv"); //instrument child processes
        args.push_back("-tool_exit_timeout"); //don't wait much of internal threads
        args.push_back("1");

    //Load tool
        args.push_back("-t");
        args.push_back(zsimPath);
    }

   info("H4");

   int size = pinoptions.size();
   for (int i=0; i<size;i++){
      args.push_back(g_string(pinoptions[i].c_str()));
    }   

    info ("H5");

   //Tool options
   if (cfg.c_str()) {
       //Check configFile is an absolute path
       //NOTE: We check rather than canonicalizing it ourselves because by the time we're created, we might be in another directory
       char* absPath = realpath(cfg.c_str(), nullptr);
       if (std::string(cfg.c_str()) != std::string(absPath)) {
           panic("Internal zsim bug, configFile should be absolute");
       }
       free(absPath);
   
       args.push_back("-config");
       args.push_back(cfg);
   }


   args.push_back("-outputDir");
   info ("H6");
   args.push_back(oDir);
   
   //std::stringstream shmid_ss;
   //shmid_ss << shm.c_str();
 
   info ("the string is %s", shm.c_str());  
   args.push_back("-shmid");
   //args.push_back(shmid_ss.str().c_str());
   args.push_back(shm.c_str());
   
    info ("H6");

   if (logToFile) {
       args.push_back("-logToFile");
   }
    

    info("H4");
    g_vector<g_string> res = args;

    std::stringstream procIdx_ss;
    procIdx_ss << procIdx;
    res.push_back("-procIdx");
    res.push_back(procIdx_ss.str().c_str());


/*  res.push_back("-xyzzy");
    res.push_back("-reserve_memory");
    res.push_back("/home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/extras/pinplay/PinPoints/scripts/specrand.test_30290.pp/specrand.test_30290_t0r3_warmup301500_prolog0_region100003_epilog0_003_0-72245.0.address");
    res.push_back("-replay");
    res.push_back("-xyzzy");
    res.push_back("-replay:basename");
    res.push_back("/home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/extras/pinplay/PinPoints/scripts/specrand.test_30290.pp/specrand.test_30290_t0r3_warmup301500_prolog0_region100003_epilog0_003_0-72245.0");
    res.push_back("-replay:playout");
    res.push_back("0");
    res.push_back("-log:mt");
    res.push_back("0");
    res.push_back("-phaselen");
    res.push_back("300000");
    res.push_back("-statfile");
    res.push_back("/home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/extras/pinplay/PinPoints/scripts/specrand.test_30290.pp/specrand.test_30290_t0r3_warmup301500_prolog0_region100003_epilog0_003_0-72245.0.brpred.txt");*/

    res.push_back("--");

    info("Size of res is %d", (int)res.size());

    return res;
}

g_vector<g_string> PinCmd::getFullCmdArgs(uint32_t procIdx, const char** inputFile) {

    info ("Hello full cmd args !");
    assert(procIdx < procInfo.size()); //must be one of the topmost processes
    info ("the size of args is %d",(int)args.size());
    g_vector<g_string> res = getPinCmdArgs(procIdx);

    g_string cmd = procInfo[procIdx].cmd;

    /* Loader injection: Turns out that Pin mingles with the simulated binary, which decides the loader used,
     * even when PIN_VM_LIBRARY_PATH is used. This kill the invariance on libzsim.so's loaded address, because
     * loaders in different children have different sizes. So, if specified, we prefix the program with the
     * given loader. This is optional because it won't work with statically linked binaries.
     *
     * BTW, thinking of running pin under a specific loaderto fix this instead? Nope, it gets into an infinite loop.
     */
    if (procInfo[procIdx].loader != "") {
        cmd = procInfo[procIdx].loader + " " + cmd;
        info("Injected loader on process%d, command line: %s", procIdx, cmd.c_str());
        warn("Loader injection makes Pin unaware of symbol routines, so things like routine patching"
             "will not work! You can homogeneize the loaders instead by editing the .interp ELF section");
    }

    //nullapp_path = "/home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/extras/pinplay/bin/intel64/nullapp" ;
    if(use_pinplay)
      res.push_back( nullapp_path );
    else{
        //Parse command -- use glibc's wordexp to parse things like quotes, handle argument expansion, etc correctly
       wordexp_t p;
       wordexp(cmd.c_str(), &p, 0);
       for (uint32_t i = 0; i < p.we_wordc; i++) {
           res.push_back(g_string(p.we_wordv[i]));
       }
       wordfree(&p);
    }


    //Input redirect
    *inputFile = (procInfo[procIdx].input == "")? nullptr : procInfo[procIdx].input.c_str();
    return res;
}

void PinCmd::setEnvVars(uint32_t procIdx) {
    assert(procIdx < procInfo.size()); //must be one of the topmost processes
    if (procInfo[procIdx].env != "") {
        wordexp_t p;
        wordexp(procInfo[procIdx].env.c_str(), &p, 0);
        for (uint32_t i = 0; i < p.we_wordc; i++) {
            char* var = strdup(p.we_wordv[i]); //putenv() does not make copies, and takes non-const char* in
            if (putenv(var) != 0) {
                panic("putenv(%s) failed", var);
            }
        }
        wordfree(&p);
    }
}
