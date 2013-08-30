#!/usr/bin/python

import sys
import argparse
import os
import time
import pickle
import threading
import re

from gen_synthetic import *
from import_tools import *
from gen_queries import *
from querying import *
from watcher import *


#output = sys.stdout
#mschemaoutput = sys.stdout
#pschemaoutput = sys.stdout
maxrange = 65535

# options:
# --full
# --almost-full
# --with-monetinR=yes|no
# --with-MonetDBR=yes|no
# --with-RSQLite=yes|no
# --with-ff=no|yes
# --with-datatable=no|yes
# --do-selection=yes|no
# --do-projection=yes|no
# --do-grouping=yes|no
# --do-joins=yes|no
# --do-projsel=yes|no
# --with-selectivity=tuple
# --with-groupsize=tuple
# --with-joinsize=tuple
# --with-ncols=tuple
# --with-nrows=tuple
# --with-sizes=tuple
# --mdbfarm-path=path
# name

files = []

import signal
import time
 
class Timeout():
    """Timeout class using ALARM signal."""
    class Timeout(Exception):
        pass
 
    def __init__(self, sec):
        self.sec = sec
 
    def __enter__(self):
        signal.signal(signal.SIGALRM, self.raise_timeout)
        signal.alarm(self.sec)
 
    def __exit__(self, *args):
        signal.alarm(0)    # disable alarm
 
    def raise_timeout(self, *args):
        raise Timeout.Timeout()

def sort_fun(a, b):
    #sort tools:
    order = ['monetinR', 'RSQLite', 'data.table', 'MonetDB.R']
    aa = a.split('/')
    bb = b.split('/')
    if not aa[0] == bb[0]:
        if order.index(aa[0]) < order.index(bb[0]):
            return -1
        else:
            return 1
    #sort datasets
    aaa = aa[1].split('_')
    bbb = bb[1].split('_')
    if not aaa[0] == bbb[0]:
        if len(aaa[0]) < len(bbb[0]):
            return -1
        else:
            return 1
    #sort op
    order = ['sel', 'proj', 'psel', 'group', 'join']
    if not aaa[1] == bbb[1]:
        if order.index(aaa[1]) < order.index(bbb[1]):
            return -1
        else:
            return 1
    #sort oparg
    aaaa = aaa[2].split('.')
    bbbb = bbb[2].split('.')
    if not aaaa[0] == bbbb[0]:
        if float(aaaa[0]) < float(bbbb[0]):
            return -1
        else:
            return 1
    return 0

def process_files():
    global files
    files.sort(sort_fun)
    print files
    watchers_file = []
    while files:
        fi = files.pop(0)
        try:
            with Timeout(3600):
                sys.stdout.write("Execution de %s ..."%fi)
                pss = ['R']
                R = 'R'
                if "MonetDB.R" in fi: 
                    pss.append("mserver5")
            #                pss.append("monetdb")
                if "monetinR" in fi: R = '/export/scratch2/lajus/Rbuild/bin/R'
                watchers = []
                watchers_file = []
                for ps in pss: 
                    f = open("data/ps/%s.ps.%s"%(fi, ps), "w")
                    watchers_file.append(f)
                watchers.append(get_watcher(ps, 0.01, f))
                for w in watchers:
                    w.start()
                os.system("%s --vanilla --slave < src/%s"%(R, fi))
                for f in watchers_file:
                    f.close()
                sys.stdout.write(" OK\n")
                save_it(files)
        except Timeout.Timeout:
            sys.stdout.write(" Timeout\n")
            sys.stderr.write("Computation of %s :timeout\n"%fi)
            os.system("pkill R")
            os.system("pkill mserver5")
            time.sleep(1)
            for f in watchers_file:
                f.close()
            fi = fi.split('_')
            tool = fi[0].split('/')
            pattern = re.compile(tool[0] + '/*_' + fi[1] + '_*.R')
            files = filter(lambda x: not pattern.match(x), files) # if one operation for one tool timeout, no need to try to do bigger computation. => sorted list
            save_it(files)

def save_state(state):
    f = open("src/state", "wb")
    pickle.dump(state, f)
    f.close()

def save_it(it):
    f = open("src/.backup", "wb")
    pickle.dump(it, f)
    f.close()

def load_it():
    f = open("src/.backup", "rb")
    res = pickle.load(f)
    f.close()
    return res

def relative_or_absolute(arg):
    if arg[0].capitalize() == 'R':
        if arg[-1] == '%':
            return 'R', float(arg[1:-1])/100
        return 'R', float(arg[1:])
    elif arg[0].capitalize() == 'A':
        return 'A', float(arg[1:])
    elif arg[-1] == '%':
        return 'R', float(arg[:-1])/100
    else:
        return 'A', float(arg)

def ra_to_absolute(parg, relatively):
    t,v = parg
    if t == 'A':
        return int(v)
    elif t == 'R':
        return int(v*relatively)
    else:
        raise ValueError(parg)

class Ayesno(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        if values == "yes" or values == "no":
            setattr(namespace, self.dest, values == "yes")
        else:
            parser.error("invalid argument for %s: expected null|yes|no had %s"%(option_string, value))

def main():
    global files
    state = 0
    
    parser=argparse.ArgumentParser()
    parser.add_argument("-f", "--full", action="store_true", dest="full", default=False,
                      help="All tools-All queries")
    parser.add_argument("--almost-full", action="store_true", dest="almost_full", default=True,
                      help="All DBs-All queries")
    parser.add_argument("--with-monetinR", action=Ayesno, nargs='?', dest="mdbinR", const="yes", default="yes")
    parser.add_argument("--with-MonetDBR", action=Ayesno, nargs='?', dest="mdbR", const="yes", default="yes")
    parser.add_argument("--with-RSQLite", action=Ayesno, nargs='?', dest="rsqlite", const="yes", default="yes")
    parser.add_argument("--with-ff", action=Ayesno, nargs='?', dest="ff", const="no", default="no")
    parser.add_argument("--with-datatable", action=Ayesno, nargs='?', dest="dt", const="no", default="no")
    parser.add_argument("--do-selection", action=Ayesno, nargs='?', dest="do_sel", const="yes", default="yes")
    parser.add_argument("--do-projection", action=Ayesno, nargs='?', dest="do_proj", const="yes", default="yes")
    parser.add_argument("--do-grouping", action=Ayesno, nargs='?', dest="do_group", const="yes", default="yes")
    parser.add_argument("--do-joins", action=Ayesno, nargs='?', dest="do_join", const="yes", default="yes")
    parser.add_argument("--do-projsel", action=Ayesno, nargs='?', dest="do_ps", const="yes", default="yes")
    parser.add_argument("--with-selectivity", type=float, dest="sel", nargs="+", default=[0.01])
    parser.add_argument("--with-groupsize", type=relative_or_absolute, dest="group", nargs="+", default=[('A', 5)])
    parser.add_argument("--with-joinsize", type=float, dest="joins", nargs="+", default=[0.01])
    parser.add_argument("--with-ncols", type=int, dest="ncol")
    parser.add_argument("--with-nrows", type=int, dest="nrow", nargs="+")
    parser.add_argument("--with-sizes", type=float, dest="size", nargs="+")
    parser.add_argument("--repeat", type=int, dest="repeat", default=10)
    parser.add_argument("--mdbfarm-path", dest="mdbpath", default="/export/scratch2/lajus/monetdb/farm")
    parser.add_argument("--resume", dest="resume", metavar="PATH")
#    parser.add_argument("name", default="")
    
    options = parser.parse_args()
    resuming = False

    if options.resume:
        os.chdir(options.resume)
        f = open('src/options', 'rb')
        wd = options.resume
        options = pickle.load(f)
        f.close()
        resuming = True

    if not resuming:
        if options.size and options.nrow:
            parser.error("options nrow and size are mutually exclusive")
    
        if not options.ncol or options.ncol < 3:
            options.ncol = 3
        if not options.nrow and not options.size:
            options.nrow = [100]
        if options.size:
            options.nrow = [int(s * 10**9 / (4*options.ncol)) for s in options.size]
    
    random.seed()
    tools = []
    queries = 0
    if options.almost_full:
        tools = ["monetinR", "MonetDB.R", "RSQLite"]
        queries = 31
    if options.full:
        tools = ["monetinR", "MonetDB.R", "RSQLite", "ff", "data.table"]
        queries = 31
    if options.mdbinR and "monetinR" not in tools: tools.append("monetinR")
    if not options.mdbinR and "monetinR" in tools: tools.remove("monetinR")
    if options.mdbR and "MonetDB.R" not in tools: tools.append("MonetDB.R")
    if not options.mdbR and "MonetDB.R" in tools: tools.remove("MonetDB.R")
    if options.rsqlite and "RSQLite" not in tools: tools.append("RSQLite")
    if not options.rsqlite and "RSQLite" in tools: tools.remove("RSQLite")
    if options.ff and "ff" not in tools: tools.append("ff")
    if not options.ff and "ff" in tools: tools.remove("ff")
    if options.dt and "data.table" not in tools: tools.append("data.table")
    if not options.dt and "data.table" in tools: tools.remove("data.table")
    if options.do_sel and queries % 2 is 0: queries += 1
    if not options.do_sel and queries % 2 is 1: queries -= 1
    if options.do_proj and (queries >> 1) % 2 is 0: queries += (1 << 1)
    if not options.do_proj and (queries >> 1) % 2 is 1: queries -= (1 << 1)
    if options.do_group and (queries >> 2) % 2 is 0: queries += (1 << 2)
    if not options.do_group and (queries >> 2) % 2 is 1: queries -= (1 << 2)
    if options.do_join and (queries >> 3) % 2 is 0: queries += (1 << 3)
    if not options.do_join and (queries >> 3) % 2 is 1: queries -= (1 << 3)
    if options.do_ps and (queries >> 4) % 2 is 0: queries += (1 << 4)
    if not options.do_ps and (queries >> 4) % 2 is 1: queries -= (1 << 4)

    if not resuming:
        wd = "benchmark-%s"%time.strftime("%Y%m%d-%H%M%S")
#    if not options.name: options.name = wd
        os.mkdir(wd, 0755)
        os.chdir(wd)
        os.mkdir("data", 0755)
        os.mkdir("src", 0755)
        os.mkdir("data/res", 0755)
        os.mkdir("data/ps", 0755)
        for t in tools:
            os.mkdir("data/res/%s"%t, 0755)
            os.mkdir("data/ps/%s"%t, 0755)
            os.mkdir("src/%s"%t, 0755)
    
        f = open('src/options', 'wb')
        pickle.dump(options, f)
        f.close()
        save_state(state)

        createDatabase("monetdb", wd, options.mdbpath)
    else:
        f = open('src/state', 'rb')
        state = pickle.load(f)
        f.close()

    # queries : dict - datasets => dict - "sel" => dict - selectivity => query list
    #                                   - "proj" => query list
    #                                   - "group" => dict - grsize => query
    #                                   - "joins" => dict - joinsize => query
    #                                   - "projsel" => dict - selctivity => query list

    if state == 0:
        tnames = {}
        if resuming: tnames = load_it()

        for n in options.nrow:
            assert n < (1 << 31 - 1)
            ftname = "n%d"%n
            if (ftname in tnames and ((queries >> 3) % 2 is 0 or len(tnames[ftname]) == len(options.joins))):
                continue
            elif ftname not in tnames:
                f = open("src/%s.csv"%ftname, "w")
                writeRows(options.ncol, n, map(lambda x: ra_to_absolute(x, n), options.group), f)
                f.close()
                f = open("src/mdb_import_%s.sql"%ftname, "w")
                createSchema(options.ncol, ftname, "src/%s.csv"%ftname, f, "monetdb")
                f.close()
                f = open("src/sqlite_import_%s.sql"%ftname, "w")
                createSchema(options.ncol, ftname, "src/%s.csv"%ftname, f, "sqlite")
                f.close()
                tnames[ftname] = []
                save_it(tnames)

            if (queries >> 3) % 2 is 1:
                for j in options.joins:
                    if int(n*j) == 0.0: continue
                    dtname = "n%d_j%d"%(n, int(n*j))
                    if dtname in tnames[ftname]: continue
                    f = open("src/%s.csv"%dtname, "w")
                    writeJoinRows(int(n*j), n, f)
                    f.close()
                    f = open("src/mdb_import_%s.sql"%dtname, "w")
                    createJoinSchema(dtname, ftname, "src/%s.csv"%dtname, f, "monetdb")
                    f.close()
                    f = open("src/sqlite_import_%s.sql"%dtname, "w")
                    createJoinSchema(dtname, ftname, "src/%s.csv"%dtname, f, "sqlite")
                    f.close()
                    tnames[ftname].append(dtname)
                    save_it(tnames)

        print "Datasets generated"
        state += 1
        save_state(state)
        
    if state == 1:
        importDataset(["src/mdb_import_*.sql"], "monetdb", options.mdbpath+'/'+wd)
        importDataset(["src/sqlite_import_*.sql"], "sqlite", 'src/'+wd)

        print "Datasets imported"
        state += 1
        save_state(state)
    
    if state == 2: 
        dquery = {}
        if not vars().has_key("tnames"): tnames = load_it() 
        # This is very cheap, recreate everything

        for n in options.nrow:
            ftname = "n%d"%n
            dquery[ftname] = {}
            f = open("src/%s_queries.sql"%ftname, "w")
            if (queries) % 2 is 1:
                dquery[ftname]["sel"] = {}
                for s in options.sel:
                    dquery[ftname]["sel"][s] = []
                    f.write("-- SELECTION (%f)\n"%s)
                    for i in range(options.repeat):
                        dquery[ftname]["sel"][s].append(selection_query(n, ftname, s, f))
                    f.write('\n')
                f.write('\n')
            if (queries >> 1) % 2 is 1:
                dquery[ftname]["proj"] = []
                f.write("-- PROJECTION\n")
                for i in range(options.repeat):
                    dquery[ftname]["proj"].append(projection_query(options.ncol, ftname, f))
                f.write('\n\n')
            if (queries >> 2) % 2 is 1:
                dquery[ftname]["group"] = {}
                f.write("-- GROUPING (1)\n")
                dquery[ftname]["group"][1] = [group1_query(options.ncol, ftname, f)]
                grouping = map(lambda x: ra_to_absolute(x, n), options.group)
                for col in range(1, len(options.group)+1):
                    f.write("\n-- GROUPING (%d)\n"%grouping[col-1])
                    dquery[ftname]["group"][grouping[col-1]] = [group_query(col, ftname, f)]
                f.write('\n\n')
            if (queries >> 3) % 2 is 1:
                i = 0
                dquery[ftname]["join"] = {}
                for dtname in tnames[ftname]:
                    f.write("-- JOIN (%f)\n"%options.joins[i])
                    dquery[ftname]["join"][options.joins[i]] = [join_query(options.ncol, ftname, dtname, f)]
                    i += 1
                f.write('\n\n')
            if (queries >> 4) % 2 is 1:
                dquery[ftname]["psel"] = {}
                for s in options.sel:
                    dquery[ftname]["psel"][s] = []
                    f.write("-- PROJECTION-SELECTION (%f)\n"%s)
                    for i in range(options.repeat):
                        dquery[ftname]["psel"][s].append(projsel_query(n, options.ncol, ftname, s, f))
                    f.write('\n')
            f.close()

        print "Queries generated"
        state += 1
        save_state(state)
        save_it(dquery)

    if state == 3:
        files = []
        if resuming and not vars().has_key('dquery'): dquery = load_it()
        for ftname in dquery:
            for t in tools:
                if t == "RSQLite":
                    dbpath = os.path.join(os.path.abspath("src"), wd)
                elif t == "data.table":
                    dbpath = os.path.abspath("src")
                else:
                    dbpath = os.path.join(options.mdbpath, wd)
                for qt in dquery[ftname]:
                    if qt == "proj":
                        f = open("src/%s/%s_%s_0.R"%(t, ftname, qt), "w")
                        files.append("%s/%s_%s_0.R"%(t, ftname, qt))
                        gen_querying(t, dquery[ftname][qt], ftname, qt, "0", wd, dbpath, options.ncol, f)
                        f.close()
                    else:
                        for qarg in dquery[ftname][qt]:
                            f = open("src/%s/%s_%s_%s.R"%(t, ftname, qt, str(qarg)), "w")
                            files.append("%s/%s_%s_%s.R"%(t, ftname, qt, str(qarg)))
                            gen_querying(t, dquery[ftname][qt][qarg], ftname, qt, str(qarg), wd, dbpath, options.ncol, f)
                            f.close()

        print "R files generated"
        state += 1
        save_state(state)
        save_it(files.sort())

    if state == 4:
        if resuming and not files: files = load_it()
        process_files()
        state += 1
        save_state(state)

if __name__ == "__main__":
    main()
