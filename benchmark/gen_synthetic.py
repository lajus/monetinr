import random
import os
import sys

maxint = 1 << 31 - 1 

def createSchema(ncol, tname, csvfile, output, dbms):
    if dbms == "sqlite":
        col = "key int PRIMARY KEY"
    else:
        col = "key int"
    for i in range(ncol-1):
        col += ",col_%d int"%i
    output.write("CREATE TABLE %s (%s);\n"%(tname, col))
    if dbms == "monetdb":
        output.write("COPY INTO %s FROM '%s' USING DELIMITERS ',','\\n','\"';\n"%(tname, os.path.abspath(csvfile)))
    elif dbms == "postgres":
        output.write("COPY %s FROM '%s' USING DELIMITERS ',' CSV;\n"%(tname, os.path.abspath(csvfile)))
    elif dbms == "sqlite":
        output.write(".separator ','\n.import %s %s\n"%(os.path.abspath(csvfile), tname))
    if dbms != "sqlite": output.write("ALTER TABLE %s ADD PRIMARY KEY (key);\n"%tname)
    return

def writeRows(ncol, nrow, rr, output):
    i = 0
    while i < nrow:
        row = "%d"%i
        for j in range(ncol-1):
            if j < len(rr): 
                row += ",%d"%random.randint(0, min(rr[j], maxint))
            else:
                row += ",%d"%random.randint(0, maxint)
        output.write("%s\n"%row)
        i += 1
    return

def createJoinSchema(dtname, ftname, csvfile, output, dbms):
    if dbms == "monetdb":
        output.write("CREATE TABLE %s (key int, fkey int);\n"%(dtname))
        output.write("COPY INTO %s FROM '%s' USING DELIMITERS ',','\\n','\"';\n"%(dtname, os.path.abspath(csvfile)))
        output.write("ALTER TABLE %s ADD PRIMARY KEY (key);\n"%dtname) 
        output.write("ALTER TABLE %s ADD FOREIGN KEY (fkey) REFERENCES %s(key);\n"%(dtname, ftname)) 
    elif dbms == "sqlite":
        output.write("CREATE TABLE %s (key int PRIMARY KEY, fkey int, FOREIGN KEY(fkey) REFERENCES %s(key));\n"%(dtname, ftname))
        output.write(".separator ','\n.import %s %s\n"%(os.path.abspath(csvfile), dtname))
    
    return

def writeJoinRows(dnrow, fnrow, output):
    i = 0
    while i < dnrow:
        output.write("%s,%s\n"%(i, random.randrange(fnrow)))
        i += 1
    return
