#!/usr/bin/python
# This file plots the data so that we can interpret/present simulation results
# Currently plots :
#     IPC

from __future__ import division #for floating point division

import h5py # presents HDF5 files as numpy arrays
import numpy as np
import matplotlib.pyplot as plt

from settings import *
import os
import sys
import string

from scipy import stats as scistats  #from stackoverflow

path_results = "/home/kartik/zsim_kartik/results_2-4-2016/no_prefetch/results"
instrs = {}
cycles = {}
IPC = {}
hotinval_l2 = {}
hotinval_l1 = {}
l1_access = {}
l2_access = {}
l3_access = {}
l1_hit_rate = {}
l2_hit_rate = {}
l3_hit_rate = {}
core_l1_traffic = {}
l1_l2_traffic = {}
l2_l3_traffic = {}
l3_mem_traffic = {}

#----- walk the result directory -------- #
for root, dirs, files in os.walk(path_results):
  path = root.split('/')
  print ( len(path) - 1) *'---' , os.path.basename(root)
  f_path = path_results+"/"+os.path.basename(root)+"/"+"zsim.h5"
  if os.path.isfile(f_path):
    bench_name_split = os.path.basename(root).split("_")
    bench_name = bench_name_split[1];
    f = h5py.File(f_path, 'r')
    print f_path
    dset = f["stats"]["root"]
    print len(dset)
    if len(dset) != 0 :
        l_instrs = dset[-1]['beefy'][0]['instrs']
        l_cycles = dset[-1]['beefy'][0]['cycles'] +  dset[-1]['beefy'][0]['cCycles']
        instrs[bench_name] = l_instrs
        cycles[bench_name] = l_cycles
        IPC[bench_name] = l_instrs/l_cycles
        hotinval_l2[bench_name] = dset[-1]['l2_beefy'][0]['hotInv'] + dset[-1]['l2_beefy'][1]['hotInv'] +  dset[-1]['l2_beefy'][2]['hotInv'] +  dset[-1]['l2_beefy'][3]['hotInv']
        hotinval_l1[bench_name] = dset[-1]['l1d_beefy'][0]['hotInv'] + dset[-1]['l1d_beefy'][1]['hotInv'] +  dset[-1]['l1d_beefy'][2]['hotInv'] +  dset[-1]['l1d_beefy'][3]['hotInv']
        l1_access[bench_name] = dset[-1]['l1d_beefy'][0]['fhGETS'] + dset[-1]['l1d_beefy'][0]['fhGETX'] +  dset[-1]['l1d_beefy'][0]['hGETS'] + dset[-1]['l1d_beefy'][0]['hGETX'] + dset[-1]['l1d_beefy'][0]['mGETS'] + dset[-1]['l1d_beefy'][0]['mGETXIM'] + dset[-1]['l1d_beefy'][0]['mGETXSM'] + dset[-1]['l1d_beefy'][1]['fhGETS'] + dset[-1]['l1d_beefy'][1]['fhGETX'] +  dset[-1]['l1d_beefy'][1]['hGETS'] + dset[-1]['l1d_beefy'][1]['hGETX'] + dset[-1]['l1d_beefy'][1]['mGETS'] + dset[-1]['l1d_beefy'][1]['mGETXIM'] + dset[-1]['l1d_beefy'][1]['mGETXSM']
        l2_access[bench_name] = dset[-1]['l2_beefy'][0]['hGETS'] + dset[-1]['l2_beefy'][0]['hGETX'] + dset[-1]['l2_beefy'][0]['mGETS'] + dset[-1]['l2_beefy'][0]['mGETXIM'] + dset[-1]['l2_beefy'][0]['mGETXSM'] + dset[-1]['l2_beefy'][1]['hGETS'] + dset[-1]['l2_beefy'][1]['hGETX'] + dset[-1]['l2_beefy'][1]['mGETS'] + dset[-1]['l2_beefy'][1]['mGETXIM'] + dset[-1]['l2_beefy'][1]['mGETXSM']
        l3_access[bench_name] = dset[-1]['l3'][0]['hGETS'] + dset[-1]['l3'][0]['hGETX'] + dset[-1]['l3'][0]['mGETS'] + dset[-1]['l3'][0]['mGETXIM'] + dset[-1]['l3'][0]['mGETXSM'] + dset[-1]['l3'][1]['hGETS'] + dset[-1]['l3'][1]['hGETX'] + dset[-1]['l3'][1]['mGETS'] + dset[-1]['l3'][1]['mGETXIM'] + dset[-1]['l3'][1]['mGETXSM'] +  dset[-1]['l3'][2]['hGETS'] + dset[-1]['l3'][2]['hGETX'] + dset[-1]['l3'][2]['mGETS'] + dset[-1]['l3'][2]['mGETXIM'] + dset[-1]['l3'][2]['mGETXSM'] + dset[-1]['l3'][2]['hGETS'] + dset[-1]['l3'][2]['hGETX'] + dset[-1]['l3'][2]['mGETS'] + dset[-1]['l3'][2]['mGETXIM'] + dset[-1]['l3'][2]['mGETXSM'] +  dset[-1]['l3'][3]['hGETS'] + dset[-1]['l3'][3]['hGETX'] + dset[-1]['l3'][3]['mGETS'] + dset[-1]['l3'][3]['mGETXIM'] + dset[-1]['l3'][3]['mGETXSM'] + dset[-1]['l3'][3]['hGETS'] + dset[-1]['l3'][3]['hGETX'] + dset[-1]['l3'][3]['mGETS'] + dset[-1]['l3'][3]['mGETXIM'] + dset[-1]['l3'][3]['mGETXSM']
        l1_hit_rate[bench_name] =( dset[-1]['l1d_beefy'][0]['fhGETS'] + dset[-1]['l1d_beefy'][0]['fhGETX'] +  dset[-1]['l1d_beefy'][0]['hGETS'] + dset[-1]['l1d_beefy'][0]['hGETX'] + dset[-1]['l1d_beefy'][1]['fhGETS'] + dset[-1]['l1d_beefy'][1]['fhGETX'] +  dset[-1]['l1d_beefy'][1]['hGETS'] + dset[-1]['l1d_beefy'][1]['hGETX'] )/ l1_access[bench_name]
        l2_hit_rate[bench_name] =( dset[-1]['l2_beefy'][0]['hGETS'] + dset[-1]['l2_beefy'][0]['hGETX']  +  dset[-1]['l2_beefy'][1]['hGETS'] + dset[-1]['l2_beefy'][1]['hGETX'] )/ l2_access[bench_name]
        l3_hit_rate[bench_name] =( dset[-1]['l3'][0]['hGETS'] + dset[-1]['l3'][0]['hGETX']  +  dset[-1]['l3'][1]['hGETS'] + dset[-1]['l3'][1]['hGETX'] )/ l3_access[bench_name]
        core_l1_traffic[bench_name] =  l1_access[bench_name]
        l1_l2_traffic[bench_name] = dset[-1]['l1d_beefy'][0]['mGETS'] + dset[-1]['l1d_beefy'][0]['mGETXIM'] + dset[-1]['l1d_beefy'][0]['mGETXSM'] + dset[-1]['l2_beefy'][0]['PUTX'] + dset[-1]['l1d_beefy'][1]['mGETS'] + dset[-1]['l1d_beefy'][1]['mGETXIM'] + dset[-1]['l1d_beefy'][1]['mGETXSM'] + dset[-1]['l2_beefy'][1]['PUTX']
        l2_l3_traffic[bench_name] = dset[-1]['l2_beefy'][0]['mGETS'] + dset[-1]['l2_beefy'][0]['mGETXIM'] + dset[-1]['l1d_beefy'][0]['mGETXSM'] + + dset[-1]['l3'][0]['PUTX'] + dset[-1]['l2_beefy'][1]['mGETS'] + dset[-1]['l2_beefy'][1]['mGETXIM'] + dset[-1]['l1d_beefy'][1]['mGETXSM'] + + dset[-1]['l3'][1]['PUTX']
        l3_mem_traffic[bench_name] = dset[-1]['mem'][0]['rd'] + dset[-1]['mem'][0]['wr'] + dset[-1]['mem'][1]['rd'] + dset[-1]['mem'][1]['wr']


        #prefetches[bench_name] = dset[-1]['l1d_prefetcher'][0]['acc'] + dset[-1]['l1d_prefetcher'][1]['acc'] + dset[-1]['l1d_prefetcher'][2]['acc'] + dset[-1]['l1d_prefetcher'][3]['acc']
#----- walk another result directory -----#
path_results = "/home/kartik/zsim_kartik/results_2-4-2016/prefetch/results"
instrs_p = {}
cycles_p = {}
IPC_p = {}
pollution_l3_p = {}
pollution_l2_p = {}
hotinval_l2_p = {}
hotinval_l1_p = {}
l1_access_p = {}
l2_access_p = {}
l3_access_p = {}
prefetches = {}
l1_hit_rate_p = {}
l2_hit_rate_p = {}
l3_hit_rate_p = {}
core_l1_traffic_p = {}
l1_l2_traffic_p = {}
l2_l3_traffic_p = {}
l3_mem_traffic_p = {}

#----- walk the result directory -------- #
for root, dirs, files in os.walk(path_results):
  path = root.split('/')
  print ( len(path) - 1) *'---' , os.path.basename(root)
  f_path = path_results+"/"+os.path.basename(root)+"/"+"zsim.h5"
  if os.path.isfile(f_path):
    bench_name_split = os.path.basename(root).split("_")
    bench_name = bench_name_split[1];
    f = h5py.File(f_path, 'r')
    print f_path
    dset = f["stats"]["root"]
    print len(dset)
    if len(dset) != 0 :
        l_instrs = dset[-1]['beefy'][0]['instrs']
        l_cycles = dset[-1]['beefy'][0]['cycles'] +  dset[-1]['beefy'][0]['cCycles']
        instrs_p[bench_name] = l_instrs
        cycles_p[bench_name] = l_cycles
        IPC_p[bench_name] = l_instrs/l_cycles
        pollution_l3_p[bench_name] =  dset[-1]['l3'][0]['prefetchPollution'] +  dset[-1]['l3'][1]['prefetchPollution'] +  dset[-1]['l3'][2]['prefetchPollution'] +  dset[-1]['l3'][3]['prefetchPollution']
        pollution_l2_p[bench_name] = dset[-1]['l2_beefy'][0]['prefetchPollution'] + dset[-1]['l2_beefy'][1]['prefetchPollution'] +  dset[-1]['l2_beefy'][2]['prefetchPollution'] +  dset[-1]['l2_beefy'][3]['prefetchPollution']
        hotinval_l2_p[bench_name] =  dset[-1]['l2_beefy'][0]['hotInv'] + dset[-1]['l2_beefy'][1]['hotInv'] +  dset[-1]['l2_beefy'][2]['hotInv'] +  dset[-1]['l2_beefy'][3]['hotInv']
        hotinval_l1_p[bench_name] =  dset[-1]['l1d_beefy'][0]['hotInv'] + dset[-1]['l1d_beefy'][1]['hotInv'] +  dset[-1]['l1d_beefy'][2]['hotInv'] +  dset[-1]['l1d_beefy'][3]['hotInv']
        l1_access_p[bench_name] = dset[-1]['l1d_beefy'][0]['fhGETS'] + dset[-1]['l1d_beefy'][0]['fhGETX'] +  dset[-1]['l1d_beefy'][0]['hGETS'] + dset[-1]['l1d_beefy'][0]['hGETX'] + dset[-1]['l1d_beefy'][0]['mGETS'] + dset[-1]['l1d_beefy'][0]['mGETXIM'] + dset[-1]['l1d_beefy'][0]['mGETXSM'] + dset[-1]['l1d_beefy'][1]['fhGETS'] + dset[-1]['l1d_beefy'][1]['fhGETX'] +  dset[-1]['l1d_beefy'][1]['hGETS'] + dset[-1]['l1d_beefy'][1]['hGETX'] + dset[-1]['l1d_beefy'][1]['mGETS'] + dset[-1]['l1d_beefy'][1]['mGETXIM'] + dset[-1]['l1d_beefy'][1]['mGETXSM']
        l2_access_p[bench_name] = dset[-1]['l2_beefy'][0]['hGETS'] + dset[-1]['l2_beefy'][0]['hGETX'] + dset[-1]['l2_beefy'][0]['mGETS'] + dset[-1]['l2_beefy'][0]['mGETXIM'] + dset[-1]['l2_beefy'][0]['mGETXSM'] + dset[-1]['l2_beefy'][1]['hGETS'] + dset[-1]['l2_beefy'][1]['hGETX'] + dset[-1]['l2_beefy'][1]['mGETS'] + dset[-1]['l2_beefy'][1]['mGETXIM'] + dset[-1]['l2_beefy'][1]['mGETXSM']
        l3_access_p[bench_name] = dset[-1]['l3'][0]['hGETS'] + dset[-1]['l3'][0]['hGETX'] + dset[-1]['l3'][0]['mGETS'] + dset[-1]['l3'][0]['mGETXIM'] + dset[-1]['l3'][0]['mGETXSM'] + dset[-1]['l3'][1]['hGETS'] + dset[-1]['l3'][1]['hGETX'] + dset[-1]['l3'][1]['mGETS'] + dset[-1]['l3'][1]['mGETXIM'] + dset[-1]['l3'][1]['mGETXSM'] +  dset[-1]['l3'][2]['hGETS'] + dset[-1]['l3'][2]['hGETX'] + dset[-1]['l3'][2]['mGETS'] + dset[-1]['l3'][2]['mGETXIM'] + dset[-1]['l3'][2]['mGETXSM'] + dset[-1]['l3'][2]['hGETS'] + dset[-1]['l3'][2]['hGETX'] + dset[-1]['l3'][2]['mGETS'] + dset[-1]['l3'][2]['mGETXIM'] + dset[-1]['l3'][2]['mGETXSM'] +  dset[-1]['l3'][3]['hGETS'] + dset[-1]['l3'][3]['hGETX'] + dset[-1]['l3'][3]['mGETS'] + dset[-1]['l3'][3]['mGETXIM'] + dset[-1]['l3'][3]['mGETXSM'] + dset[-1]['l3'][3]['hGETS'] + dset[-1]['l3'][3]['hGETX'] + dset[-1]['l3'][3]['mGETS'] + dset[-1]['l3'][3]['mGETXIM'] + dset[-1]['l3'][3]['mGETXSM']
        prefetches[bench_name] = dset[-1]['l1d_prefetcher'][0]['acc'] + dset[-1]['l1d_prefetcher'][1]['acc'] + dset[-1]['l1d_prefetcher'][2]['acc'] + dset[-1]['l1d_prefetcher'][3]['acc']
        l1_hit_rate_p[bench_name] =( dset[-1]['l1d_beefy'][0]['fhGETS'] + dset[-1]['l1d_beefy'][0]['fhGETX'] +  dset[-1]['l1d_beefy'][0]['hGETS'] + dset[-1]['l1d_beefy'][0]['hGETX'] + dset[-1]['l1d_beefy'][1]['fhGETS'] + dset[-1]['l1d_beefy'][1]['fhGETX'] +  dset[-1]['l1d_beefy'][1]['hGETS'] + dset[-1]['l1d_beefy'][1]['hGETX'] )/ l1_access[bench_name]
        l2_hit_rate_p[bench_name] =( dset[-1]['l2_beefy'][0]['hGETS'] + dset[-1]['l2_beefy'][0]['hGETX']  +  dset[-1]['l2_beefy'][1]['hGETS'] + dset[-1]['l2_beefy'][1]['hGETX'] )/ l2_access[bench_name]
        l3_hit_rate_p[bench_name] =( dset[-1]['l3'][0]['hGETS'] + dset[-1]['l3'][0]['hGETX']  +  dset[-1]['l3'][1]['hGETS'] + dset[-1]['l3'][1]['hGETX'] )/ l3_access[bench_name]
        core_l1_traffic_p[bench_name] =  l1_access[bench_name]
        l1_l2_traffic_p[bench_name] = dset[-1]['l1d_beefy'][0]['mGETS'] + dset[-1]['l1d_beefy'][0]['mGETXIM'] + dset[-1]['l1d_beefy'][0]['mGETXSM'] + dset[-1]['l2_beefy'][0]['PUTX'] + dset[-1]['l1d_beefy'][1]['mGETS'] + dset[-1]['l1d_beefy'][1]['mGETXIM'] + dset[-1]['l1d_beefy'][1]['mGETXSM'] + dset[-1]['l2_beefy'][1]['PUTX']
        l2_l3_traffic_p[bench_name] = dset[-1]['l2_beefy'][0]['mGETS'] + dset[-1]['l2_beefy'][0]['mGETXIM'] + dset[-1]['l1d_beefy'][0]['mGETXSM'] + + dset[-1]['l3'][0]['PUTX'] + dset[-1]['l2_beefy'][1]['mGETS'] + dset[-1]['l2_beefy'][1]['mGETXIM'] + dset[-1]['l1d_beefy'][1]['mGETXSM'] + + dset[-1]['l3'][1]['PUTX']
        l3_mem_traffic_p[bench_name] = dset[-1]['mem'][0]['rd'] + dset[-1]['mem'][0]['wr'] + dset[-1]['mem'][1]['rd'] + dset[-1]['mem'][1]['wr']


##------ plot IPC data -------#
#assume that IPC and IPC_p have the same keys
val_array = IPC.values();
val_array_p = IPC_p.values();
IPC["geomean"] = scistats.gmean(val_array)
IPC_p["geomean"] = scistats.gmean(val_array_p)

n_groups = len (IPC)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4


rects1 = plt.bar(index, IPC.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='Without Prefetching')


rects2 = plt.bar(index+bar_width, IPC_p.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='Prefetching Enabled')

plt.xlabel('Benchmarks')
plt.ylabel('IPCs')
plt.title('IPC for 8-multiprogramming ')
plt.xticks(index + bar_width, IPC.keys(), rotation=90 )
plt.legend()
plt.tight_layout()
plt.savefig('ipc.pdf')


##----- plot prefetch pollution data ------#
plt.clf()

val_array_l2 = pollution_l2_p.values();
val_array_l3 = pollution_l3_p.values();
pollution_l2_p["geomean"] = scistats.gmean(val_array_l2)
pollution_l3_p["geomean"] = scistats.gmean(val_array_l3)



n_groups = len(pollution_l2_p)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index, pollution_l2_p.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='l2 pollution')

rects2 = plt.bar(index+bar_width, pollution_l3_p.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='l3 pollution')

plt.xlabel('Benchmarks')
plt.ylabel('Pollution quantity')
plt.title('Pollution for 8-multiprogramming ')
plt.xticks(index + bar_width, pollution_l2_p.keys(), rotation=90 )
plt.legend()
plt.tight_layout()
plt.savefig('pollution.pdf')

#--- plot hot inval data for l2 ----#
plt.clf();

val_array = hotinval_l2.values();
val_array_p = hotinval_l2_p.values();
hotinval_l2["geomean"] = scistats.gmean(val_array)
hotinval_l2_p["geomean"] = scistats.gmean(val_array_p)



n_groups = len(hotinval_l2)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index, hotinval_l2_p.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='Hot inval with prefetch')

rects2 = plt.bar(index+bar_width, hotinval_l2.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='Hot inval without prefetch')

plt.xlabel('Benchmarks')
plt.ylabel('Hot Invalidates')
plt.title('Hot Invalidates 8-multiprogramming ')
plt.xticks(index + bar_width, hotinval_l2.keys(), rotation=90)
plt.legend()
plt.tight_layout()
plt.savefig('hotinval_l2.pdf')

#--- plot hot inval data for l1 ---#
plt.clf();

val_array = hotinval_l1.values();
val_array_p = hotinval_l1_p.values();
hotinval_l1["geomean"] = scistats.gmean(val_array)
hotinval_l1_p["geomean"] = scistats.gmean(val_array_p)

n_groups = len(hotinval_l1)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index, hotinval_l1_p.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='Hot inval in l1 with prefetch')

rects2 = plt.bar(index+bar_width, hotinval_l1.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='Hot inval in l1 without prefetch')

plt.xlabel('Benchmarks')
plt.ylabel('Hot Invalidates for l1')
plt.title('Hot Invalidates for l1 - 8-multiprogramming ')
plt.xticks(index + bar_width, hotinval_l1.keys(), rotation=90 )
plt.legend()
plt.tight_layout()
plt.savefig('hotinval_l1.pdf')
#--- Plot data traffic  core-l1 ---#
plt.clf();

val_array = core_l1_traffic.values();
val_array_p = core_l1_traffic_p.values();
core_l1_traffic["geomean"] = scistats.gmean(val_array)
core_l1_traffic_p["geomean"] = scistats.gmean(val_array_p)

n_groups = len(core_l1_traffic)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index,core_l1_traffic_p.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='With Prefetch')

rects2 = plt.bar(index+bar_width, core_l1_traffic.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='Without Prefetch')

plt.xlabel('Benchmarks')
plt.ylabel('Data Traffic between Core and L1')
plt.title('Data traffic between Core and L1 8-multiprogramming ')
plt.xticks(index + bar_width, core_l1_traffic_p.keys(), rotation=90 )
plt.legend()
plt.tight_layout()
plt.savefig('data_traffic_core_l1.pdf')

#---Plot data traffic l1-l2---#
plt.clf();

val_array = l1_l2_traffic.values();
val_array_p = l1_l2_traffic_p.values();
l1_l2_traffic["geomean"] = scistats.gmean(val_array)
l1_l2_traffic_p["geomean"] = scistats.gmean(val_array_p)

n_groups = len(l1_l2_traffic)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index,l1_l2_traffic_p.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='With Prefetch')

rects2 = plt.bar(index+bar_width, l1_l2_traffic.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='Without Prefetch')

plt.xlabel('Benchmarks')
plt.ylabel('Data Traffic between L1 and L2')
plt.title('Data traffic between L1 and L2 for 8-multiprogramming ')
plt.xticks(index + bar_width, l1_l2_traffic.keys(), rotation=90 )
plt.legend()
plt.tight_layout()
plt.savefig('data_traffic_l1_l2.pdf')

#---Plot data traffic l2-l3---#
plt.clf();

val_array = l2_l3_traffic.values();
val_array_p = l2_l3_traffic_p.values();
l2_l3_traffic["geomean"] = scistats.gmean(val_array)
l2_l3_traffic_p["geomean"] = scistats.gmean(val_array_p)


n_groups = len(l2_l3_traffic)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index,l2_l3_traffic_p.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='With Prefetch')

rects2 = plt.bar(index+bar_width, l2_l3_traffic.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='Without Prefetch')

plt.xlabel('Benchmarks')
plt.ylabel('Data Traffic between L2 and L3')
plt.title('Data traffic between L2 and L3 for 8-multiprogramming ')
plt.xticks(index + bar_width, l2_l3_traffic.keys(), rotation=90 )
plt.legend()
plt.tight_layout()
plt.savefig('data_traffic_l2_l3.pdf')

#---Plot data traffic l3-mem---#
plt.clf();

val_array = l3_mem_traffic.values();
val_array_p = l3_mem_traffic_p.values();
l3_mem_traffic["geomean"] = scistats.gmean(val_array)
l3_mem_traffic_p["geomean"] = scistats.gmean(val_array_p)


n_groups = len(l3_mem_traffic)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index,l3_mem_traffic_p.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='With Prefetch')

rects2 = plt.bar(index+bar_width, l3_mem_traffic.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='Without Prefetch')

plt.xlabel('Benchmarks')
plt.ylabel('Data Traffic between L3 and mem')
plt.title('Data traffic between L3 and mem for 8-multiprogramming')
plt.xticks(index + bar_width, l3_mem_traffic.keys(), rotation=90 )
plt.legend()
plt.tight_layout()
plt.savefig('data_traffic_l3_mem.pdf')

#----Plot hit rates l1 ---#
plt.clf();

val_array = l1_hit_rate.values();
val_array_p = l1_hit_rate_p.values();
l1_hit_rate["geomean"] = scistats.gmean(val_array)
l1_hit_rate_p["geomean"] = scistats.gmean(val_array_p)


n_groups = len(l1_hit_rate)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index,l1_hit_rate_p.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='With Prefetch')

rects2 = plt.bar(index+bar_width, l1_hit_rate.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='Without Prefetch')

plt.xlabel('Benchmarks')
plt.ylabel('L1 hit rate')
plt.title('L1 hit rate for 8-multiprogramming ')
plt.xticks(index + bar_width, l1_hit_rate.keys(), rotation=90 )
plt.legend()
plt.tight_layout()
plt.savefig('l1_hit_rate.pdf')

#---Plot hit rates l2 ---#
plt.clf();


val_array = l2_hit_rate.values();
val_array_p = l2_hit_rate_p.values();
l2_hit_rate["geomean"] = scistats.gmean(val_array)
l2_hit_rate_p["geomean"] = scistats.gmean(val_array_p)

n_groups = len(l2_hit_rate)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index,l2_hit_rate_p.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='With Prefetch')

rects2 = plt.bar(index+bar_width, l2_hit_rate.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='Without Prefetch')

plt.xlabel('Benchmarks')
plt.ylabel('L2 hit rate')
plt.title('L2 hit rate for 8-multiprogramming ')
plt.xticks(index + bar_width, l2_hit_rate.keys(), rotation=90 )
plt.legend()
plt.tight_layout()
plt.savefig('l2_hit_rate.pdf')

#---Plot hit rates l3 ---#
plt.clf();


val_array = l3_hit_rate.values();
val_array_p = l3_hit_rate_p.values();
l3_hit_rate["geomean"] = scistats.gmean(val_array)
l3_hit_rate_p["geomean"] = scistats.gmean(val_array_p)

n_groups = len(l3_hit_rate)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index,l3_hit_rate_p.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='With Prefetch')

rects2 = plt.bar(index+bar_width, l3_hit_rate.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='Without Prefetch')

plt.xlabel('Benchmarks')
plt.ylabel('L3 hit rate')
plt.title('L3 hit rate for 8-multiprogramming ')
plt.xticks(index + bar_width, l3_hit_rate.keys(), rotation=90 )
plt.legend()
plt.tight_layout()
plt.savefig('l3_hit_rate.pdf')

#---- Plot prefetches ----#
plt.clf();


val_array = prefetches.values();
val_array_p = l2_access_p.values();
prefetches["geomean"] = scistats.gmean(val_array)
l2_access_p["geomean"] = scistats.gmean(val_array_p)

n_groups = len(prefetches)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index,prefetches.values(), bar_width,
                  alpha=opacity,
                  color='b',
                  label='Prefetches')


rects2 = plt.bar(index+bar_width,l2_access_p.values(), bar_width,
                  alpha=opacity,
                  color='r',
                  label='Total L2 accesses')

plt.xlabel('Benchmarks')
plt.ylabel('Prefetch Accesses v/s Total access to L2 ')
plt.title('Prefetch accesses for 8-multiprogramming ')
plt.xticks(index + bar_width, l2_access_p.keys(), rotation=90 )
plt.legend()
plt.tight_layout()
plt.savefig('prefetches.pdf')
