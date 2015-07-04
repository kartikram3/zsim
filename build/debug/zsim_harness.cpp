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

/* ZSim master process. Handles global heap creation, configuration, launching
 * slave pin processes, coordinating and terminating runs, and stats printing.
 */

#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <sys/personality.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "config.h"
#include "constants.h"
#include "debug_harness.h"
#include "galloc.h"
#include "log.h"
#include "pin_cmd.h"
#include "version.h" //autogenerated, in build dir, see SConstruct
#include "zsim.h"

/* Globals */

typedef enum {
    OK,
    GRACEFUL_TERMINATION,
    KILL_EM_ALL,
} TerminationStatus;

TerminationStatus termStatus = OK;

typedef enum {
    PS_INVALID,
    PS_RUNNING,
    PS_DONE,
} ProcStatus;

struct ProcInfo {
    int pid;
    volatile ProcStatus status;
};

//At most as many processes as threads, plus one extra process per child if we launch a debugger
#define MAX_CHILDREN (2*MAX_THREADS)
ProcInfo childInfo[MAX_CHILDREN];

volatile uint32_t debuggerChildIdx = MAX_THREADS;

GlobSimInfo* globzinfo = nullptr; //used very sparingly, only in sig handlers. Should probably promote to a global like in zsim processes.

bool perProcessDir, aslr;

PinCmd* pinCmd;

/* Defs & helper functions */

void LaunchProcess(uint32_t procIdx);

int getNumChildren() {
    int num = 0;
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (childInfo[i].status == PS_RUNNING) num++;
    }
    return num;
}

int eraseChild(int pid) {
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (childInfo[i].pid == pid) {
            assert_msg(childInfo[i].status == PS_RUNNING, "i=%d pid=%d status=%d", i, pid, childInfo[i].status);
            childInfo[i].status = PS_DONE;
            return i;
        }
    }
    panic("Could not erase child!!");
}

/* Signal handlers */

void chldSigHandler(int sig) {
    assert(sig == SIGCHLD);
    int status;
    int cpid;
    while ((cpid = waitpid(-1, &status, WNOHANG)) > 0) {
        int idx = eraseChild(cpid);
        if (idx < MAX_THREADS) {
            info("Child %d done", cpid);
            int exitCode = WIFEXITED(status)? WEXITSTATUS(status) : 0;
            if (exitCode == PANIC_EXIT_CODE) {
                panic("Child issued a panic, killing simulation");
            }
            //Stricter check: See if notifyEnd was called (i.e. zsim caught this termination)
            //Only works for direct children though
            if (globzinfo && !globzinfo->procExited[idx]) {
                panic("Child %d (idx %d) exit was anomalous, killing simulation", cpid, idx);
            }

            if (globzinfo && globzinfo->procExited[idx] == PROC_RESTARTME) {
                info("Restarting procIdx %d", idx);
                globzinfo->procExited[idx] = PROC_RUNNING;
                LaunchProcess(idx);
            }
        } else {
            info("Child %d done (debugger)", cpid);
        }
    }
}

void sigHandler(int sig) {
    if (termStatus == KILL_EM_ALL) return; //a kill was already issued, avoid infinite recursion

    switch (sig) {
        case SIGSEGV:
            warn("Segmentation fault");
            termStatus = KILL_EM_ALL;
            break;
        case SIGINT:
            info("Received interrupt");
            termStatus = (termStatus == OK)? GRACEFUL_TERMINATION : KILL_EM_ALL;
            break;
        case SIGTERM:
            info("Received SIGTERM");
            termStatus = KILL_EM_ALL;
            break;
        default:
            warn("Received signal %d", sig);
            termStatus = KILL_EM_ALL;
    }

    if (termStatus == KILL_EM_ALL) {
        warn("Hard death, killing the whole process tree");
        kill(-getpid(), SIGKILL);
        //Exit, we have already killed everything, there should be no strays
        panic("SIGKILLs sent -- exiting");
    } else {
        info("Attempting graceful termination");
        for (int i = 0; i < MAX_CHILDREN; i++) {
            int cpid = childInfo[i].pid;
            if (childInfo[i].status == PS_RUNNING) {
                info("Killing process %d", cpid);
                kill(-cpid, SIGKILL);
                sleep(0.1);
                kill(cpid, SIGKILL);
            }
        }

        info("Done sending kill signals");
    }
}

void exitHandler() {
    // If for some reason we still have children, kill everything
    uint32_t children = getNumChildren();
    if (children) {
        warn("Hard death at exit (%d children running), killing the whole process tree", children);
        kill(-getpid(), SIGKILL);
    }
}

void debugSigHandler(int signum, siginfo_t* siginfo, void* dummy) {
    assert(signum == SIGUSR1);
    uint32_t callerPid = siginfo->si_pid;
    // Child better have this initialized...
    struct LibInfo* zsimAddrs = (struct LibInfo*) gm_get_secondary_ptr();
    uint32_t debuggerPid = launchXtermDebugger(callerPid, zsimAddrs);
    childInfo[debuggerChildIdx].pid = debuggerPid;
    childInfo[debuggerChildIdx++].status = PS_RUNNING;
}

/* Heartbeats */

static time_t startTime;
static time_t lastHeartbeatTime;
static uint64_t lastCycles = 0;

static void printHeartbeat(GlobSimInfo* zinfo) {
    uint64_t cycles = zinfo->numPhases*zinfo->phaseLength;
    time_t curTime = time(nullptr);
    time_t elapsedSecs = curTime - startTime;
    time_t heartbeatSecs = curTime - lastHeartbeatTime;

    if (elapsedSecs == 0) return;
    if (heartbeatSecs == 0) return;

    char time[128];
    char hostname[256];
    gethostname(hostname, 256);

    std::ofstream hb("heartbeat");
    hb << "Running on: " << hostname << std::endl;
    hb << "Start time: " << ctime_r(&startTime, time);
    hb << "Heartbeat time: " << ctime_r(&curTime, time);
    hb << "Stats since start:" << std:: endl;
    hb << " " << zinfo->numPhases << " phases" << std::endl;
    hb << " " << cycles << " cycles" << std::endl;
    hb << " " << (cycles)/elapsedSecs << " cycles/s" << std::endl;
    hb << "Stats since last heartbeat (" << heartbeatSecs << "s):" << std:: endl;
    hb << " " << (cycles-lastCycles)/heartbeatSecs << " cycles/s" << std::endl;

    lastHeartbeatTime = curTime;
    lastCycles = cycles;
}


void LaunchProcess(uint32_t procIdx) {
    int cpid = fork();
    if (cpid) { //parent
        assert(cpid > 0);
        childInfo[procIdx].pid = cpid;
        childInfo[procIdx].status = PS_RUNNING;
    } else { //child
        // Set the child's vars and get the command
        // NOTE: We set the vars first so that, when parsing the command, wordexp takes those vars into account
        pinCmd->setEnvVars(procIdx);
        const char* inputFile;
        g_vector<g_string> args = pinCmd->getFullCmdArgs(procIdx, &inputFile);

        //Copy args to a const char* [] for exec
        int nargs = args.size()+1;
        
        const char* aptrs[nargs];

        trace(Harness, "Calling arguments:");
        for (unsigned int i = 0; i < args.size(); i++) {
            trace(Harness, " arg%d = %s", i, args[i].c_str());
            aptrs[i] = args[i].c_str();
            info("Pin command line arg no %d for procIdx %d : %s",i, procIdx,aptrs[i]);
        }
        aptrs[nargs-1] = nullptr;

        //Chdir to process dir if needed
        if (perProcessDir) {
            std::stringstream dir_ss;
            dir_ss << "p" << procIdx << "/";
            int res = chdir(dir_ss.str().c_str());
            if (res == -1) {
                perror("Coud not chdir");
                panic("chdir to %s failed", dir_ss.str().c_str());
            }
        }

        //Input redirection if needed
        if (inputFile) {
            int fd = open(inputFile, O_RDONLY);
            if (fd == -1) {
                perror("open() failed");
                panic("Could not open input redirection file %s", inputFile);
            }
            dup2(fd, 0);
        }

        /* In a modern kernel, we must disable address space randomization. Otherwise,
         * different zsim processes will load zsim.so on different addresses,
         * which would be fine except that the vtable pointers will be different
         * per process, and virtual functions will not work.
         *
         * WARNING: The harness itself is run with randomization on, which should
         * be fine because it doesn't load zsim.so anyway. If this changes at some
         * point, we'll need to have the harness be executed via a wrapper that just
         * changes the personalily and forks, or run the harness with setarch -R
         */
        if (!aslr) {
            //Get old personality flags & update
            int pers = personality(((unsigned int)-1) /*returns current pers flags; arg is a long, hence the cast, see man*/);
            if (pers == -1 || personality(pers | ADDR_NO_RANDOMIZE) == -1) {
                perror("personality() call failed");
                panic("Could not change personality to disable address space randomization!");
            }
            int newPers = personality(((unsigned int)-1));
            if ((newPers & ADDR_NO_RANDOMIZE) == 0) panic("personality() call was not honored! old 0x%x new 0x%x", pers, newPers);
        }


/*        aptrs[0] = "/home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/pin" ; 
        aptrs[1] = "-xyzzy";
        aptrs[2] = "-reserve_memory"; 
        aptrs[3] =  */

        info("Strins is %s",aptrs[0]); 
        //const char * aptrs_1[2];
        //aptrs_1[0]="/home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/pin  -xyzzy  -reserve_memory  /home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/extras/pinplay/PinPoints/scripts/specrand.test_30290.pp/specrand.test_30290_t0r3_warmup301500_prolog0_region100003_epilog0_003_0-72245.0.address   -t /home/kartik/zsim_kartik/build/opt/libzsim.so -replay -xyzzy  -replay:basename /home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/extras/pinplay/PinPoints/scripts/specrand.test_30290.pp/specrand.test_30290_t0r3_warmup301500_prolog0_region100003_epilog0_003_0-72245.0 -replay:playout 0  -log:mt 0   -phaselen 300000 -statfile /home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/extras/pinplay/PinPoints/scripts/specrand.test_30290.pp/specrand.test_30290_t0r3_warmup301500_prolog0_region100003_epilog0_003_0-72245.0.brpred.txt -- /home/kartik/Prefetch_Simulator/pinplay-1.4-pin-2.14-67254-gcc.4.4.7-linux/extras/pinplay/bin/intel64/nullapp";
        //aptrs_1[1]=nullptr;
        //info ("%s",aptrs_1[0]);
        if (execvp(aptrs[0], (char* const*)aptrs) == -1) {
            perror("Could not exec, killing child");
            panic("Could not exec %s", aptrs[0]);
        } else {
            panic("Something is SERIOUSLY wrong. This should never execute!");
        }
    }
}


int main(int argc, char *argv[]) {
    if (argc == 2 && std::string(argv[1]) == "-v") {
        printf("%s\n", ZSIM_BUILDVERSION);
        exit(0);
    }

    InitLog("[H] ", nullptr /*log to stdout/err*/);
    info("Starting zsim, built %s (rev %s)", ZSIM_BUILDDATE, ZSIM_BUILDVERSION);
    startTime = time(nullptr);

    if (argc != 2) {
        info("Usage: %s config_file", argv[0]);
        exit(1);
    }

    //Canonicalize paths --- because we change dirs, we deal in absolute paths
    const char* configFile = realpath(argv[1], nullptr);
    const char* outputDir = getcwd(nullptr, 0); //already absolute

    Config conf(configFile);

    if (atexit(exitHandler)) panic("Could not register exit handler");

    signal(SIGSEGV, sigHandler);
    signal(SIGINT,  sigHandler);
    signal(SIGABRT, sigHandler);
    signal(SIGTERM, sigHandler);

    signal(SIGCHLD, chldSigHandler);

    //SIGUSR1 is used by children processes when they want to get a debugger session started;
    struct sigaction debugSa;
    debugSa.sa_flags = SA_SIGINFO;
    sigemptyset(&debugSa.sa_mask);//NOTE: We might want to start using sigfullsets in other signal handlers to avoid races...
    debugSa.sa_sigaction = debugSigHandler;
    if (sigaction(SIGUSR1, &debugSa, nullptr) != 0)
        panic("sigaction() failed");

    waitid(P_ALL, 0, nullptr, WEXITED);

    //Remove all zsim.log.* files (we append to them, and want to avoid outputs from multiple simulations)
    uint32_t removedLogfiles = 0;
    while (true) {
        std::stringstream ss;
        ss << "zsim.log." << removedLogfiles;
        if (remove(ss.str().c_str()) != 0) break;
        removedLogfiles++;
    }
    if (removedLogfiles) info("Removed %d old logfiles", removedLogfiles);

    uint32_t gmSize = conf.get<uint32_t>("sim.gmMBytes", (1<<10) /*default 1024MB*/);
    info("Creating global segment, %d MBs", gmSize);
    int shmid = gm_init(((size_t)gmSize) << 20 /*MB to Bytes*/);
    info("Global segment shmid = %d", shmid);
    //fprintf(stderr, "%sGlobal segment shmid = %d\n", logHeader, shmid); //hack to print shmid on both streams
    //fflush(stderr);

    trace(Harness, "Created global segment, starting pin processes, shmid = %d", shmid);

    //Do we need per-process direcories?
    perProcessDir = conf.get<bool>("sim.perProcessDir", false);

    if (perProcessDir) {
        info("Running each process in a different subdirectory"); //p0, p1, ...
    }

    bool deadlockDetection;
    bool attachDebugger = conf.get<bool>("sim.attachDebugger", false);

    if (attachDebugger) {
        info("Pausing PIN to attach debugger, and not running deadlock detection");
        deadlockDetection = false;
    } else {
        deadlockDetection = conf.get<bool>("sim.deadlockDetection", true);
    }

    info("Deadlock detection %s", deadlockDetection? "ON" : "OFF");

    aslr = conf.get<bool>("sim.aslr", false);
    if (aslr) info("Not disabling ASLR, multiprocess runs will fail");

    //Create children processes
    pinCmd = new PinCmd(&conf, configFile, outputDir, shmid);
    uint32_t numProcs = pinCmd->getNumCmdProcs();

    for (uint32_t procIdx = 0; procIdx < numProcs; procIdx++) {
        LaunchProcess(procIdx);
    }

    if (numProcs == 0) panic("No process config found. Config file needs at least a process0 entry");

    //Wait for all processes to finish
    int sleepLength = 10;
    GlobSimInfo* zinfo = nullptr;
    int32_t secsStalled = 0;

    int64_t lastNumPhases = 0;

    while (getNumChildren() > 0) {
        if (!gm_isready()) {
            usleep(1000);  // wait till proc idx 0 initializes everyhting
            continue;
        }

        if (zinfo == nullptr) {
            zinfo = static_cast<GlobSimInfo*>(gm_get_glob_ptr());
            globzinfo = zinfo;
            info("Attached to global heap");
        }

        printHeartbeat(zinfo);  // ensure we dump hostname etc on early crashes

        int left = sleep(sleepLength);
        int secsSlept = sleepLength - left;
        //info("Waking up, secs elapsed %d", secsSlept);

        __sync_synchronize();

        uint32_t ffProcs = zinfo->globalFFProcs;
        uint32_t activeProcs = zinfo->globalActiveProcs;
        uint32_t sffProcs = zinfo->globalSyncedFFProcs;
        bool simShouldAdvance = (ffProcs < activeProcs) && (sffProcs == 0);

        int64_t numPhases = zinfo->numPhases;

        if (deadlockDetection) {
            if (simShouldAdvance) {
                //info("In deadlock check zone");
                if (numPhases <= lastNumPhases) {
                    secsStalled += secsSlept;
                    if (secsStalled > 10) warn("Stalled for %d secs so far", secsStalled);
                } else {
                    //info("Not stalled, did %ld phases since last check", numPhases-lastNumPhases);
                    lastNumPhases = numPhases;
                    secsStalled = 0;
                }
            } else if (activeProcs) {
                if (numPhases == lastNumPhases) info("Some fast-forwarding is going on, not doing deadlock detection (a: %d, ff: %d, sff: %d)", activeProcs, ffProcs, sffProcs);
                lastNumPhases = numPhases;
            } //otherwise, activeProcs == 0; we're done
        }

        printHeartbeat(zinfo);

        //This solves a weird race in multiprocess where SIGCHLD does not always fire...
        int cpid = -1;

        while ((cpid = waitpid(-1, nullptr, WNOHANG)) > 0) {
            eraseChild(cpid);
            info("Child %d done (in-loop catch)", cpid);
        }

        if (secsStalled > 120) {
            warn("Deadlock detected, killing children");
            sigHandler(SIGINT);
            exit(42);
        }
    }

    uint32_t exitCode = 0;
    if (termStatus == OK) {
        info("All children done, exiting");
    } else {
        info("Graceful termination finished, exiting");
        exitCode = 1;
    }
    if (zinfo && zinfo->globalActiveProcs) warn("Unclean exit of %d children, termination stats were most likely not dumped", zinfo->globalActiveProcs);
    exit(exitCode);
}
