#!/usr/bin/python
#This script looks at a particular benchmark's simulation in detail

from __future__ import division #for floating point division

import h5py # presents HDF5 files as numpy arrays
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from settings import *
import os
import sys
import string

from scipy import stats as scistats  #from stackoverflow
import matplotlib.patches as mpatches

path_bench = "/home/kartik/zsim_kartik/results_2-2-2016/no_prefetch/results/1454309004_tonto"
instrs_profile = {}
cycles_profile = {}
IPC_profile = {}
miss_profile = {}
hit_rate = {}

f_path = path_bench +"/"+"zsim.h5"
f = h5py.File(f_path, 'r')
dset = f["stats"]["root"]
print len(dset)

if len(dset) != 0 :
    l_instrs = dset[-1]['beefy'][0]['instrs']
    l_cycles = dset[-1]['beefy'][0]['cycles'] +  dset[-1]['beefy'][0]['cCycles']
    l1_access = dset[-1]['l1d_beefy'][0]['fhGETS'] + dset[-1]['l1d_beefy'][0]['fhGETX'] +  dset[-1]['l1d_beefy'][0]['hGETS'] + dset[-1]['l1d_beefy'][0]['hGETX'] + dset[-1]['l1d_beefy'][0]['mGETS'] + dset[-1]['l1d_beefy'][0]['mGETXIM'] + dset[-1]['l1d_beefy'][0]['mGETXSM'] + dset[-1]['l1d_beefy'][1]['fhGETS'] + dset[-1]['l1d_beefy'][1]['fhGETX'] +  dset[-1]['l1d_beefy'][1]['hGETS'] + dset[-1]['l1d_beefy'][1]['hGETX'] + dset[-1]['l1d_beefy'][1]['mGETS'] + dset[-1]['l1d_beefy'][1]['mGETXIM'] + dset[-1]['l1d_beefy'][1]['mGETXSM']
    l1_hit_rate =( dset[-1]['l1d_beefy'][0]['fhGETS'] + dset[-1]['l1d_beefy'][0]['fhGETX'] +  dset[-1]['l1d_beefy'][0]['hGETS'] + dset[-1]['l1d_beefy'][0]['hGETX'] + dset[-1]['l1d_beefy'][1]['fhGETS'] + dset[-1]['l1d_beefy'][1]['fhGETX'] +  dset[-1]['l1d_beefy'][1]['hGETS'] + dset[-1]['l1d_beefy'][1]['hGETX'] )/ l1_access
    print "l_instrs is :"
    print l_instrs
    print "l_cycles is :"
    print l_cycles
    print "IPC is :"
    print (l_instrs/l_cycles)
    print "The l1 hit rate is "
    print l1_hit_rate
    for i in range(2,31):
        instrs_profile[i]=dset[i]['beefy'][0]['instrs'] # / (dset[i]['beefy'][0]['cycles'] +  dset[i]['beefy'][0]['cCycles'])
        cycles_profile[i] = (dset[i]['beefy'][0]['cycles'] +  dset[i]['beefy'][0]['cCycles'])
        IPC_profile[i]= ( dset[i]['beefy'][0]['instrs'] -  dset[i-1]['beefy'][0]['instrs'] ) / ( (dset[i]['beefy'][0]['cycles'] +  dset[i]['beefy'][0]['cCycles']) - (dset[i-1]['beefy'][0]['cycles'] +  dset[i-1]['beefy'][0]['cCycles'])  )
        miss_profile[i] =  dset[i]['l1d_beefy'][0]['mGETS'] + dset[i]['l1d_beefy'][0]['mGETXIM'] + dset[i]['l1d_beefy'][0]['mGETXSM'] + dset[i]['l1d_beefy'][1]['mGETS'] + dset[i]['l1d_beefy'][1]['mGETXIM'] + dset[i]['l1d_beefy'][1]['mGETXSM'] + dset[i]['l1d_beefy'][2]['mGETS'] + dset[i]['l1d_beefy'][2]['mGETXIM'] + dset[i]['l1d_beefy'][2]['mGETXSM'] + dset[i]['l1d_beefy'][3]['mGETS'] + dset[i]['l1d_beefy'][3]['mGETXIM'] + dset[i]['l1d_beefy'][3]['mGETXSM']   -(  dset[i-1]['l1d_beefy'][0]['mGETS'] + dset[i-1]['l1d_beefy'][0]['mGETXIM'] + dset[i-1]['l1d_beefy'][0]['mGETXSM'] + dset[i-1]['l1d_beefy'][1]['mGETS'] + dset[i-1]['l1d_beefy'][1]['mGETXIM'] + dset[i-1]['l1d_beefy'][1]['mGETXSM'] + dset[i-1]['l1d_beefy'][2]['mGETS'] + dset[i-1]['l1d_beefy'][2]['mGETXIM'] + dset[i-1]['l1d_beefy'][2]['mGETXSM'] + dset[i-1]['l1d_beefy'][3]['mGETS'] + dset[i-1]['l1d_beefy'][3]['mGETXIM'] + dset[i-1]['l1d_beefy'][3]['mGETXSM'] )
        hit_rate[i] =  (( dset[i]['l1d_beefy'][0]['fhGETS'] + dset[i]['l1d_beefy'][0]['fhGETX'] +  dset[i]['l1d_beefy'][0]['hGETS'] + dset[i]['l1d_beefy'][0]['hGETX'] ) - ( dset[i-1]['l1d_beefy'][0]['fhGETS'] + dset[i-1]['l1d_beefy'][0]['fhGETX'] +  dset[i-1]['l1d_beefy'][0]['hGETS'] + dset[i-1]['l1d_beefy'][0]['hGETX'] )) / ((   dset[i]['l1d_beefy'][0]['fhGETS'] + dset[i]['l1d_beefy'][0]['fhGETX'] +  dset[i]['l1d_beefy'][0]['hGETS'] + dset[i]['l1d_beefy'][0]['hGETX'] + dset[i]['l1d_beefy'][0]['mGETS'] + dset[i]['l1d_beefy'][0]['mGETXIM'] + dset[i]['l1d_beefy'][0]['mGETXSM']  ) -  (  dset[i-1]['l1d_beefy'][0]['fhGETS'] + dset[i-1]['l1d_beefy'][0]['fhGETX'] +  dset[i-1]['l1d_beefy'][0]['hGETS'] + dset[i-1]['l1d_beefy'][0]['hGETX'] + dset[i-1]['l1d_beefy'][0]['mGETS'] + dset[i-1]['l1d_beefy'][0]['mGETXIM'] + dset[i-1]['l1d_beefy'][0]['mGETXSM']  ))

print "==================== Stats when prefetching enabled ==================="

path_bench = "/home/kartik/zsim_kartik/results_2-4-2016/prefetch/results/1454626697_tonto"
IPC_profile_p = {}
miss_profile_p ={}
hit_rate_p = {}

f_path = path_bench +"/"+"zsim.h5"
f = h5py.File(f_path, 'r')
dset = f["stats"]["root"]
print len(dset)

if len(dset) != 0 :
    l_instrs = dset[-1]['beefy'][0]['instrs']
    l_cycles = dset[-1]['beefy'][0]['cycles'] +  dset[-1]['beefy'][0]['cCycles']
    l1_access = dset[-1]['l1d_beefy'][0]['fhGETS'] + dset[-1]['l1d_beefy'][0]['fhGETX'] +  dset[-1]['l1d_beefy'][0]['hGETS'] + dset[-1]['l1d_beefy'][0]['hGETX'] + dset[-1]['l1d_beefy'][0]['mGETS'] + dset[-1]['l1d_beefy'][0]['mGETXIM'] + dset[-1]['l1d_beefy'][0]['mGETXSM'] + dset[-1]['l1d_beefy'][1]['fhGETS'] + dset[-1]['l1d_beefy'][1]['fhGETX'] +  dset[-1]['l1d_beefy'][1]['hGETS'] + dset[-1]['l1d_beefy'][1]['hGETX'] + dset[-1]['l1d_beefy'][1]['mGETS'] + dset[-1]['l1d_beefy'][1]['mGETXIM'] + dset[-1]['l1d_beefy'][1]['mGETXSM']
    l1_hit_rate =( dset[-1]['l1d_beefy'][0]['fhGETS'] + dset[-1]['l1d_beefy'][0]['fhGETX'] +  dset[-1]['l1d_beefy'][0]['hGETS'] + dset[-1]['l1d_beefy'][0]['hGETX'] + dset[-1]['l1d_beefy'][1]['fhGETS'] + dset[-1]['l1d_beefy'][1]['fhGETX'] +  dset[-1]['l1d_beefy'][1]['hGETS'] + dset[-1]['l1d_beefy'][1]['hGETX'] )/ l1_access

    print "l_instrs is :"
    print l_instrs
    print "l_cycles is :"
    print l_cycles
    print "IPC is :"
    print (l_instrs/l_cycles)
    print "The l1 hit rate is "
    print l1_hit_rate
    for i in range(2,31):
        instrs_profile[i]=dset[i]['beefy'][0]['instrs'] # / (dset[i]['beefy'][0]['cycles'] +  dset[i]['beefy'][0]['cCycles'])
        cycles_profile[i] = (dset[i]['beefy'][0]['cycles'] +  dset[i]['beefy'][0]['cCycles'])
        IPC_profile_p[i]= ( dset[i]['beefy'][0]['instrs'] -  dset[i-1]['beefy'][0]['instrs'] ) / ( (dset[i]['beefy'][0]['cycles'] +  dset[i]['beefy'][0]['cCycles']) - (dset[i-1]['beefy'][0]['cycles'] +  dset[i-1]['beefy'][0]['cCycles'])  )
        miss_profile_p[i] =  dset[i]['l1d_beefy'][0]['mGETS'] + dset[i]['l1d_beefy'][0]['mGETXIM'] + dset[i]['l1d_beefy'][0]['mGETXSM'] + dset[i]['l1d_beefy'][1]['mGETS'] + dset[i]['l1d_beefy'][1]['mGETXIM'] + dset[i]['l1d_beefy'][1]['mGETXSM'] + dset[i]['l1d_beefy'][2]['mGETS'] + dset[i]['l1d_beefy'][2]['mGETXIM'] + dset[i]['l1d_beefy'][2]['mGETXSM'] + dset[i]['l1d_beefy'][3]['mGETS'] + dset[i]['l1d_beefy'][3]['mGETXIM'] + dset[i]['l1d_beefy'][3]['mGETXSM']   -(  dset[i-1]['l1d_beefy'][0]['mGETS'] + dset[i-1]['l1d_beefy'][0]['mGETXIM'] + dset[i-1]['l1d_beefy'][0]['mGETXSM'] + dset[i-1]['l1d_beefy'][1]['mGETS'] + dset[i-1]['l1d_beefy'][1]['mGETXIM'] + dset[i-1]['l1d_beefy'][1]['mGETXSM'] + dset[i-1]['l1d_beefy'][2]['mGETS'] + dset[i-1]['l1d_beefy'][2]['mGETXIM'] + dset[i-1]['l1d_beefy'][2]['mGETXSM'] + dset[i-1]['l1d_beefy'][3]['mGETS'] + dset[i-1]['l1d_beefy'][3]['mGETXIM'] + dset[i-1]['l1d_beefy'][3]['mGETXSM'] )
        hit_rate_p[i] =  (( dset[i]['l1d_beefy'][0]['fhGETS'] + dset[i]['l1d_beefy'][0]['fhGETX'] +  dset[i]['l1d_beefy'][0]['hGETS'] + dset[i]['l1d_beefy'][0]['hGETX'] ) - ( dset[i-1]['l1d_beefy'][0]['fhGETS'] + dset[i-1]['l1d_beefy'][0]['fhGETX'] +  dset[i-1]['l1d_beefy'][0]['hGETS'] + dset[i-1]['l1d_beefy'][0]['hGETX'] )) / ((   dset[i]['l1d_beefy'][0]['fhGETS'] + dset[i]['l1d_beefy'][0]['fhGETX'] +  dset[i]['l1d_beefy'][0]['hGETS'] + dset[i]['l1d_beefy'][0]['hGETX'] + dset[i]['l1d_beefy'][0]['mGETS'] + dset[i]['l1d_beefy'][0]['mGETXIM'] + dset[i]['l1d_beefy'][0]['mGETXSM']  ) -  (  dset[i-1]['l1d_beefy'][0]['fhGETS'] + dset[i-1]['l1d_beefy'][0]['fhGETX'] +  dset[i-1]['l1d_beefy'][0]['hGETS'] + dset[i-1]['l1d_beefy'][0]['hGETX'] + dset[i-1]['l1d_beefy'][0]['mGETS'] + dset[i-1]['l1d_beefy'][0]['mGETXIM'] + dset[i-1]['l1d_beefy'][0]['mGETXSM']  ))


#Plot the profiles
#Plot IPC profiles
plt.clf();
plt.plot(IPC_profile.keys(), IPC_profile.values(), 'ro', IPC_profile_p.keys(), IPC_profile_p.values(),'bo' )
plt.ylabel('IPC profile for tonto')
plt.xlabel('1000 phases')
plt.title(' tonto IPC - 4 -multiprogramming ')
red_patch = mpatches.Patch(color='red', label='No Prefetch')
blue_patch = mpatches.Patch(color='blue', label='Prefetch Enabled')
plt.legend(handles=[red_patch, blue_patch])
plt.savefig('IPC_tonto_profile_4_core.pdf')
#plt.show()

#Plot the MPTI
plt.clf();
plt.plot(miss_profile.keys(), miss_profile.values(), 'ro', miss_profile_p.keys(), miss_profile_p.values(),'bo' )
plt.ylabel('MPTI (misses per ten thousand instructions) tonto')
plt.xlabel('1000 phases')
plt.title(' tonto MPTI - 4 -multiprogramming ')
red_patch = mpatches.Patch(color='red', label='No Prefetch')
blue_patch = mpatches.Patch(color='blue', label='Prefetch Enabled')
plt.legend(handles=[red_patch, blue_patch])
plt.savefig('MPTI_tonto_4_core.pdf')

#Plot the hit rate evolution

plt.clf();
plt.plot(hit_rate.keys(), hit_rate.values(), 'ro', hit_rate_p.keys(), hit_rate_p.values(),'bo' )
plt.ylabel('L1 hit rate profile tonto')
plt.xlabel(' 1000 phases')
plt.title(' tonto L1 hit rate profile - 4 -multiprogramming ')
red_patch = mpatches.Patch(color='red', label='No Prefetch')
blue_patch = mpatches.Patch(color='blue', label='Prefetch Enabled')
plt.legend(handles=[red_patch, blue_patch])
plt.savefig('Hit_rate_profile_tonto_4_core.pdf')

