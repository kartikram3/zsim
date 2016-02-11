#!/usr/bin/python
#This script looks at a particular benchmark's simulation in detail
#Here we look at the histogram of cache line lifetimes

from __future__ import division #for floating point division

import h5py # presents HDF5 files as numpy arrays
import numpy as np
import matplotlib.pyplot as plt

from settings import *
import os
import sys
import string

from scipy import stats as scistats  #from stackoverflow
import matplotlib.patches as mpatches

path_bench = "/home/kartik/zsim_kartik/results_2-2-2016/no_prefetch/results/1454309004_mcf"

f_path = path_bench +"/"+"zsim.h5"
f = h5py.File(f_path, 'r')
dset = f["stats"]["root"]

x = dset[-1]['l1d_beefy'][0]['aggLifetime']
print(len(x))
y = range (0,100)
print(len(y))


n_groups = len(x)
fig, ax = plt.subplots()
index = np.arange(n_groups)
bar_width = 0.35
opacity = 0.4

rects1 = plt.bar(index,x, bar_width,
                  alpha=opacity,
                  color='b',
                  label='Line Count')

plt.xlabel('10 Phases')
plt.ylabel('Number of lines')
plt.title('Cache line lifetime histogram ')
plt.xticks(index + bar_width, y, rotation=90, fontsize=4 )
plt.legend()
plt.tight_layout()
plt.savefig('hist.pdf')
