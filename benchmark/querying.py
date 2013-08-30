
def dt_query(ql, dbpath, nbcol, output):
    colname = ['"key"']
    for i in range(nbcol - 1):
        colname.append('"col_%d"'%i)
    colname = ",".join(colname)
    log = ''
    q = ql[0]
    if '*' and 'WHERE' in q: 
        t = 'sel'
    elif 'WHERE' in q:
        t = 'psel'
    elif 'AVG' in q:
        t = 'group'
    elif 'JOIN' in q:
        t = 'join'
    else:
        t = 'proj'
    #Prelude
    q = q.split(' ')
    if t is not 'join':
        if len(q) == 4: q[3] = q[3].replace(';\n', '')
        output.write('dt <- fread("%s/%s.csv")\n'%(dbpath,q[3]))
    else:
        output.write('dt <- fread("%s/%s.csv")\n'%(dbpath,q[9]))
    output.write('setnames(dt, c(%s))\n'%colname)
    output.write('setkeyv(dt, "key")\n')
    if t is 'join':
        output.write('dt2 <- fread("%s/%s.csv")\n'%(dbpath,q[4]))
        output.write('setnames(dt, c("jval", "key"))\n')
    #Query
    output.write('bench <- microbenchmark(test <- ')
    while ql:
        q = ql.pop()
        q = q.replace(';', ' ')
        if t is 'sel' or t is 'psel':
            q = q.replace('AND', '&')
            output.write('subset(dt, %s)'%(q.split('WHERE')[1]))
            if t is 'psel':
                q = q.split(' ')
                output.write('$%s'%q[1])
        elif t is 'proj':
            q = q.split(' ')
            output.write('dt$%s'%q[1])
        elif t is 'group':
            by = ''
            if 'BY' in q:
                by = ', by=%s'%(q[q.index(' ', q.index('BY')+1):])
            q = q.split('(')
            output.write('dt[, mean(%s)%s]'%(q[1][:q[1].index(')')], by))
        elif t is 'join':
            q = q.split('.')
            output.write('merge(dt, dt2, by="key")[, jval, %s]'%(q[2][:q[2].index(' ')]))
        if ql: output.write(', ')
    output.write(', times=1)\n')

def do_prelude(tool, dbname, dbpath, output):
    #print tool
    output.write("library(%s)\n"%tool)
    output.write("library(microbenchmark)\n")
    if tool == "monetinR":
        output.write("monetinR::init(\"%s\")\n"%dbpath)
    elif tool == "MonetDB.R":
        output.write("system(\"pkill monetdb\")\n")
        output.write("system(\"mserver5 --daemon=yes --dbpath=%s &\")\n"%dbpath)
        output.write("db <- dbConnect(MonetDB.R(), \"monetdb://localhost/%s\")\n"%dbname)
    elif tool == "RSQLite":
        output.write("db <- dbConnect(SQLite(), \"%s\")\n"%(dbpath))
    elif tool == "data.table":
        pass
    else:
        # Consider: ff: read.csv.ffdf(file=csvfile)
        #   data.table: read.csv -> data.frame to convert to data.table
        raise NotYetImplemented

def do_conclude(tool, ftname, qt, qarg, output):
    if tool == "MonetDB.R":
        output.write("sock = monetdbGetTransferredBytes()\n")
        output.write("save(bench, sock, file=paste(Sys.getenv(\"PWD\"), \"/data/res/%s/%s_%s_%s.RData\", sep=\"\"))\n"%(tool, ftname, qt, qarg))
        output.write("system(\"pkill mserver5\")\n")
    else:
        output.write("save(bench, file=paste(Sys.getenv(\"PWD\"), \"/data/res/%s/%s_%s_%s.RData\", sep=\"\"))\n"%(tool, ftname, qt, qarg))
    if tool == "MonetDB.R" or tool == "RSQLite":
        output.write("dbDisconnect(db)\n")

def do_query(tool, queries, output):
    output.write("bench <- microbenchmark(test <- ")
    if tool == "monetinR":
        output.write(",".join("monetinR::query(\"%s\")"%q for q in queries))
    elif tool == "MonetDB.R" or tool == "RSQLite":
        output.write(",".join("dbGetQuery(db, \"%s\")"%q for q in queries))
    output.write(",times=1)\n")

def gen_querying(tool, queries, ftname, qt, qarg, dbname, dbpath, ncol, output):
    do_prelude(tool, dbname, dbpath, output)
    if tool == "data.table":
        dt_query(queries, dbpath, ncol, output)
    else:
        do_query(tool, queries, output)
    do_conclude(tool, ftname, qt, qarg, output)
