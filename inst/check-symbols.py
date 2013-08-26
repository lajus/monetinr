#!/bin/python
import subprocess
import os
import sys
import pprint
import warnings
import fnmatch
from time import *

common_symbols = ['_init', '_fini', '__bss_start', '_edata', '_end']

def truepath(path):
    return os.path.expandvars(os.path.expanduser(path))

def check_readelf():
    r = os.system("readelf --version >%s"%os.devnull)
    if r: sys.exit("Fatal error: readelf not found")

def check_awk():
    r = os.system("awk --version >%s"%os.devnull)
    if r: sys.exit("Fatal error: awk not found")

def check_R():
    r = os.system("R --version >%s"%os.devnull)
    if r: sys.exit("Fatal error: R not found")

def check_monetinR():
    try:
        mir_path = truepath(subprocess.check_output("R --vanilla --slave -e \"cat(Sys.getenv('R_LIBS_USER'))\"", shell=True))
    except subprocess.CalledProcessError:
        sys.exit("Fatal error: R user libs not found")
    if ('monetinR' not in os.listdir(mir_path) or
     'libs' not in os.listdir(os.path.join(mir_path, "monetinR")) or
     not os.path.exists(os.path.join(mir_path, "monetinR", "libs", "libR_interface.so"))):
        sys.exit("Fatal error: monetinR package not installed")
    return os.path.join(mir_path, "monetinR", "libs")

def R_lib_path():
    res = []
    try:
        lpath = truepath(subprocess.check_output("R --vanilla --slave -e \"cat(Sys.getenv('R_LIBS_USER'))\"", shell=True))
        if lpath: res.append(lpath)
        lpath = truepath(subprocess.check_output("R --vanilla --slave -e \"cat(Sys.getenv('R_LIBS'))\"", shell=True))
        if lpath: res.append(lpath)
        lpath = truepath(subprocess.check_output("R --vanilla --slave -e \"cat(Sys.getenv('R_LIBS_SITE'))\"", shell=True))
        if lpath: res.append(lpath)
        lpath = truepath(subprocess.check_output("R --vanilla --slave -e \"cat(Sys.getenv('R_HOME'))\"", shell=True))
        if lpath: res.append(lpath)
    except subprocess.CalledProcessError:
        sys.exit("Fatal error: R libs not found")
    return res

def read_directories(paths, exclude=""):
    res = []
    for path in paths:
        for d,sd,files in os.walk(path):
            if exclude and exclude in sd:
                sd.remove(exclude)
            for f in files:
                if fnmatch.fnmatch(f, "*.so") and not f == 'libR_interface.so':
                    res.append(os.path.join(path,d,f))
    return res

def read_symbol(path, pres):
    try:
        symbols = subprocess.check_output("readelf -Ws %s | awk '{ if ($7 != \"UND\" && $5 != \"LOCAL\" && $6 != \"HIDDEN\" && $6 != \"INTERNAL\") print $8}'"%path, shell=True)
    except subprocess.CalledProcessError:
        print >> sys.stderr, "Skip file %s"%path
        return
    symbols = symbols.splitlines()[3:]
    #print symbols
    for symbol in symbols:
        if (symbol and not fnmatch.fnmatch(symbol, "*@*") and not symbol == 'Name' and not symbol in common_symbols):
#and not fnmatch.fnmatch(symbol, "_*")):
            if symbol not in pres: pres[symbol] = []
            pres[symbol].append(os.path.basename(path))     

if __name__ == '__main__':
    # A little of interactivity
    length = 20

    sys.stdout.write('Checking for tools...')
    check_readelf()
    check_awk()
    check_R()
    sys.stdout.write(' OK')
    sleep(0.2)

    # Read symbols from monetinR libraries
    sys.stdout.write('\rReading monetdb lib directory...') 
    mlib_paths = read_directories([check_monetinR()])
    l = len(mlib_paths)
    if l is 0: sys.exit("Fatal error: no library found for monetinR")
    i = 0
    j = 0
    sys.stdout.write(' %d files found'%l)
    sleep(0.2)
    mir_symbols = {}
    sys.stdout.write('\rMonet_libs: [%s%s] %d/%d files: %d symbols'%("#"*int(length*j/l), " "*(length-int(length*j/l)), j, l, len(mir_symbols)))
    sys.stdout.flush()
    for path in mlib_paths:
        read_symbol(path, mir_symbols)
        j += 1
        if (j - i >= l / 2*length):
            sys.stdout.write('\rMonet_libs: [%s%s] %d/%d files: %d symbols'%("#"*int(length*j/l), " "*(length-int(length*j/l)), j, l, len(mir_symbols)))
            sys.stdout.flush()
            i = j
    #print mir_symbols
    sys.stdout.write('\rMonet_libs: [%s%s] %d/%d files: %d symbols'%("#"*int(length*j/l), " "*(length-int(length*j/l)), j, l, len(mir_symbols)))
    sys.stdout.flush()
    sleep(0.5)

    # Read symbols from R libraries
    sys.stdout.write('\rReading R lib directory...') 
    #print >> sys.stderr, R_lib_path()
    rlib_paths = read_directories(R_lib_path(), 'monetinR')
    l = len(rlib_paths)
    if l is 0: sys.exit("Fatal error: no library found for R")
    i = 0
    j = 0
    sys.stdout.write(' %d files found'%l)
    sleep(0.2)
    rlib_symbols = {}
    sys.stdout.write('\r    R_libs: [%s%s] %d/%d files: %d symbols'%("#"*int(length*j/l), " "*(length-int(length*j/l)), j, l, len(rlib_symbols)))
    sys.stdout.flush()
    for path in rlib_paths:
        read_symbol(path, rlib_symbols)
        j += 1
        if (j - i >= l / 2*length):
            sys.stdout.write('\r    R_libs: [%s%s] %d/%d files: %d symbols'%("#"*int(length*j/l), " "*(length-int(length*j/l)), j, l, len(rlib_symbols)))
            sys.stdout.flush()
            i = j
    sys.stdout.write('\r    R_libs: [%s%s] %d/%d files: %d symbols'%("#"*int(length*j/l), " "*(length-int(length*j/l)), j, l, len(rlib_symbols)))
    sys.stdout.flush()
    sleep(0.5)

    # Calculate collisions
    sys.stdout.write('\rCalculate collisions...')
    sys.stdout.flush()
    mir_symbols_set = set(mir_symbols.keys())
    rlib_symbols_set = set(rlib_symbols.keys())
    intersect = mir_symbols_set.intersection(rlib_symbols_set)
    error = len(intersect)
    sys.stdout.write('\r')
    sys.stdout.flush()
    if error: 
        print "%d error(s) found, please send the log file to the maintainer."%error
        f = open('check-symbols.log', 'w')
        fixit = open('check-symbols.hotfix', 'w')
        for symbol in intersect:
            print >> f, "ERROR: Multiple definitions of symbol '%s':"%symbol
            print >> f, "In monetinR libraries:"
            pprint.pprint(mir_symbols[symbol], stream=f, indent=2)
            print >> f, "In R libraries:"
            pprint.pprint(rlib_symbols[symbol], stream=f,  indent=2)
            print >> f, ""
#    print >> sys.stderr, "HOTFIX(ES):"
        for symbol in intersect:
            fixit.write(" -D%s=MONETINR_%s"%(symbol, symbol))
    else:
        print "No error found"
    sys.exit(error is not 0)

# Affichage:



# Monet_libs: [######               ] n/m files: k symbols
#     R_libs: [###                  ] p/q files: k' symbols
