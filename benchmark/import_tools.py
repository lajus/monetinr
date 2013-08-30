import subprocess
import os

def createDatabase(dbms, dbname, dbpath):
    dat = open("/dev/null", "w")
    if dbms == "monetdb":
        subprocess.call("monetdbd start %s"%dbpath, shell=True, stdout=dat)
        subprocess.check_call("monetdb create %s"%dbname, shell=True, stdout=dat)
        subprocess.check_call("monetdb release %s"%dbname, shell=True, stdout=dat)
        subprocess.check_call("monetdbd stop %s"%dbpath, shell=True, stdout=dat)
    elif dbms == "postgres":
        subprocess.check_call("createdb %s"%dbname, shell=True, stdout=dat)
    elif dbms == "sqlite":
        pass
    dat.close()
    return

def importDataset(importFiles, dbms, dbpath):
    dat = open("/dev/null", "w")
    subprocess.check_call("cat %s > temporary_import_files.sql"%(" ".join(importFiles)), shell=True)
    if dbms == "monetdb":
        com = "mserver5 --dbpath=%s --dbinit=\"sql.start();\" < temporary_import_files.sql"%(os.path.abspath(dbpath))
    elif dbms == "postgres":
        com = "psql %s < temporary_import_files.sql"%dbpath
    elif dbms == "sqlite":
        com = "sqlite3 %s < temporary_import_files.sql"%dbpath 
    subprocess.check_call(com, shell=True, stdout=dat)
    while True:
        try:
            subprocess.check_call("rm temporary_import_files.sql", shell=True)
            break
        except:
            pass
    dat.close()
    return
