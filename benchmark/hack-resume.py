#!/usr/bin/python

import benchmark
import sys
import os
import re
import pickle

def find_ftnames():
    res = []
    pattern = re.compile("n[0-9]+.csv")
    for d,sd,files in os.walk("src"):
        for f in files:
            if pattern.match(f):
                res.append(f.split('.')[0])
    #print res
    return res

def find_tname():
    res = {}
    pattern = re.compile("n[0-9]+_j[0-9]+.csv")
    for d,sd,files in os.walk("src"):
        for f in files:
            if pattern.match(f):
                if not f.split('_')[0] in res:
                    res[f.split('_')[0]] = []
                res[f.split('_')[0]].append( (f.split('.')[0]) )
    return res

def hack1():
    # recreate all joins files
    benchmark.save_state(0)
    tnames = {}
    for ftname in find_ftnames():
        tnames[ftname] = []
    benchmark.save_it(tnames)

def find_RSQLite10(x):
    print x
    return x.split('/')[0] == 'RSQLite' and x.split('/')[1][9]=='0'

def hack2():
    files = benchmark.load_it()
    delayed = filter(find_RSQLite10, files)
    for elm in delayed:
        print "Delayed: %s"%elm
        files.remove(elm)
    f = open("src/.delayed", 'wb')
    pickle.dump(delayed, f)
    f.close()
    benchmark.save_it(files)

def hack3():
    ff = [ "monetinR/%s"%f for f in os.listdir(os.path.abspath("src/monetinR")) if os.path.isfile(os.path.join(os.path.abspath("src/monetinR"), f))]
    print ff
    benchmark.save_it(ff)
    benchmark.save_state(4)

if __name__ == "__main__":
    os.chdir(sys.argv[1])
#    hack1()
#    hack2()
#    hack3()
    benchmark.save_it(find_tname())
    benchmark.save_state(2)
