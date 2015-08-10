#In this script, we run all the simulations for one program in parallel. Once they are finished,
#we weight the results appropriately and obtain the cumulative results.

#http://stackoverflow.com/questions/16953842/using-os-walk-to-recursively-traverse-directories-in-python

from settings import *
import os
import sys
import string
import pylibconfig2 as cfg
import subprocess
import re
from time import sleep
from multiprocessing.dummy import Pool
from datetime import datetime


def run_bench ( bench ):
     
     command =  "/home/kartik/zsim_kartik/build/opt/zsim " + bench
     subprocess.call( command , shell=True )

startTime = datetime.now()


#path_pinball = "".join(sys.argv[1])
#print path_pinball
#path_config = "".join(sys.argv[2]) + "/cfg"
#print path_config
#subprocess.call(['rm', '-rf', path_config]) 
#arch_name = "".join(sys.argv[3])
#print arch_name
#settings_name = "".join(sys.argv[4])
#print settings_name
#path_arch = "".join(sys.argv[5])
#print path_arch


#*** Run the modify_cfg executable ***#
subprocess.call([ '/home/kartik/zsim_kartik/scripts/modify_file',arch_name, settings_name, path_arch ])
#*** rnn moultiple copies of the same process ***#

#*** create spec pinball dictionary ***#
D = {}
D_arg1 ={}
D_arg2= {}

#**** populate the spec dictionary ***#
for root, dirs, files in os.walk(path_pinball):
  path = root.split('/')
  print ( len(path) - 1) *'---' , os.path.basename(root)
  for file in files:
    name_list = file.split('.')
    name_list = name_list[0:2]
    arg2 = ".".join(name_list)
    name_list.append("address")  
    arg1 = ".".join(name_list)
    print len(path)*'---',arg1
    print len(path)*'---',arg2
    dictionary_path = "/".join(path)
    print dictionary_path
    bench_name = "".join(path[len(path)-2])
    print bench_name
    D[bench_name] = dictionary_path
    D_arg1[bench_name] = arg1
    D_arg2[bench_name] = arg2
    break

cfg_list = []

for root, dirs, files in os.walk(path_config):
  path = root.split('/')
  print ( len(path) - 1 ) *'---' , os.path.basename(root)
  for file in files:
    print len(path)*'---', file
    print ("/".join(path) + "/" + file)
    cfg_path =  ("/".join(path) + "/" + file)
    cfg_list.append(cfg_path)
    break

#*** Run same benchmark on all the processes **#
#*** need to write to file
#*** keep track of processes opened
#*** and only start new process if not more than
#*** 5 are running currently 

process_list = [] 

for cfg in cfg_list:
  print cfg
  for key in D :
     cfg_file = open(cfg,"r") 
     cfg_split = cfg.split(".") 
     cfg_split.pop()
     cfg_mod = string.join(cfg_split)
     bench = cfg_mod + "_" + key + ".cfg"
     with open(bench,"w",1) as cfg_bench:
      for line in cfg_file:
         if re.match( ".*pinplay_arg_1.*",line ):
           line= "pinplay_arg_1 = " + "\"" + D[key] + "/" +  D_arg1[key] + "\""+ ";" + "\n"
         if re.match(".*pinplay_arg_2.*",line ):
           line= "pinplay_arg_2 = " + "\"" + D[key] + "/" + D_arg2[key] + "\""+ ";" + "\n"
         if re.match (".*sim_name.*", line):
           line = line.rsplit ("\"", 1)
           line.pop()
           line = "".join(line)
           line = line + "_" + key + "\""+ ";" + "\n"
         cfg_bench.write(line)
     #command =  "/home/kartik/zsim_kartik/build/opt/zsim " + bench
     process_list.append( bench)
     #subprocess.call( command , shell=True )
     cfg_bench.close()
     cfg_file.close()
     #print command
     #subprocess.Popen(' ../build/opt/zsim ../simple.cfg', shell=True)

print process_list # for debug
pool = Pool(processes=8)
pool.map(run_bench,process_list)


print "Total runtime for spec run: "
print datetime.now() - startTime #from stackoverflow 
#http://stackoverflow.com/questions/6786990/find-out-time-it-took-for-a-python-script-to-complete-execution 

#*** run combinations of the processes ***#
