import random

def selection_query(nrow, tname, selectivity, output, col="*"):
    rr = int(nrow*selectivity)
    ra = random.randrange(nrow-rr)
    q = "SELECT %s FROM %s WHERE key >= %d AND key < %d;\n"%(col, tname, ra, ra+rr)
    output.write(q)
    return q

def projection(ncol):
    return "col_%d"%random.randrange(ncol - 1)

def projection_query(ncol, tname, output):
    q = "SELECT %s FROM %s;\n"%(projection(ncol), tname)
    output.write(q)
    return q

def projsel_query(nrow, ncol, tname, selectivity, output):
    return selection_query(nrow, tname, selectivity, output, projection(ncol))

def group1_query(ncol, tname, output):
    q = "SELECT AVG(%s) FROM %s;\n"%(projection(ncol), tname)
    output.write(q)
    return q

def group_query(col, tname, output):
    q = "SELECT AVG(key) FROM %s GROUP BY col_%d;\n"%(tname, col)
    output.write(q)
    return q

def join_query(ncol, ftname, dtname, output):
    q = "SELECT d.key, f.%s FROM %s AS d INNER JOIN %s AS f ON d.fkey=f.key;\n"%(projection(ncol), dtname, ftname)
    output.write(q)
    return q
    
