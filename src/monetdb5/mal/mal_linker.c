/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.monetdb.org/Legal/MonetDBLicense
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the MonetDB Database System.
 *
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 * Copyright August 2008-2013 MonetDB B.V.
 * All Rights Reserved.
 */

/*
 * @a M. Kersten
 * @v 0.0
 * @-
 * @+ Module Loading
 * The server is bootstrapped by processing a MAL script with
 * module definitions or extensions.
 * For each module file encountered, the object library
 * lib_<modulename>.so is searched for in the location identified
 * by monet_mod_path=exec_prefix/lib/MonetDB5:exec_prefix/lib/MonetDB5/lib:exec_prefix/lib/MonetDB5/bin.
 *
 * The corresponding signature are defined
 * in @dots{}/lib(64)/<modulename>.mal.
 * @-
 * The default bootstrap script is called @dots{}/lib/MonetDB5/mal_init.mal
 * and it is designated in the configuration file as the mal_init property.
 * The rationale for this set-up is that database administrators can
 * extend/overload the bootstrap procedure without affecting the
 * software package being distributed.
 * It merely requires a different direction for the mal_init property.
 * The scheme also isolates the functionality embedded in modules from
 * inadvertise use on non-compliant databases.
 * @-
 * Unlike previous versions of MonetDB, modules can not be unloaded.
 * Dynamic libraries are always global and, therefore, it
 * is best to load them as part of the server initialization phase.
 * @-
 * For the time being we assume that all commands are statically linked.
 */
/*
 * @-
 * The MAL module should be compiled with -rdynamic and -ldl (Linux).
 * This enables loading the routines and finding out the address
 * of a particular routine.
 * The mapping from MAL module.function() identifier to an address is
 * resolved in the function getAddress. Since all modules libraries are loaded
 * completely with GLOBAL visibility, it suffices to provide the internal function
 * name.
 * In case an attempt to link to an address fails,
 * a final attempt is made to locate the *.o file in
 * the current directory.
 *
 * Note, however, that the libraries are reference counted. Although we
 * don't close them until end of session it seems prudent to maintain
 * the consistency of this counter.
 *
 */
#include "monetdb_config.h"
#include "mal_module.h"
#include "mal_linker.h"
#include "mal_function.h"	/* for throw() */
#include "mal_import.h"		/* for slash_2_dir_sep() */

#include "mutils.h"
#include <sys/types.h> /* opendir */
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <unistd.h>

#if defined(_MSC_VER) && _MSC_VER >= 1400
#define open _open
#define close _close
#endif

static int noDlopen;
#define MAXMODULES 512

typedef struct{
	str filename;
	str fullname;
	void **handle;
} FileRecord;

static FileRecord filesLoaded[MAXMODULES];
static int maxfiles = MAXMODULES;
static int lastfile = 0;

/*
 * @-
 * Search for occurrence of the function in the library identified by the filename.
 */
MALfcn
getAddress(stream *out, str filename, str modnme, str fcnname, int silent)
{
	void *dl = 0;
	MALfcn adr;
	static int idx=0;
	static int prev= -1;

	(void) modnme;
	if( prev >= 0){
		adr = (MALfcn) dlsym(filesLoaded[prev].handle, fcnname);
		if( adr != NULL)
			return adr; /* found it */
	}
	if( filename && prev >= 0) {
		if( strcmp(filename, filesLoaded[prev].filename)==0) {
			adr = (MALfcn) dlsym(filesLoaded[prev].handle, fcnname);
			if( adr != NULL)
				return adr; /* found it */
		}
	}
	/*
	 * @-
	 * Search for occurrence of the function in any library already loaded.
	 * This deals with the case that files are linked together to reduce
	 * the loading time, while the signatures of the functions are still
	 * obtained from the source-file MAL script.
	 */
	for (idx =0; idx < lastfile; idx++)
		if (filesLoaded[idx].handle) {
			adr = (MALfcn) dlsym(filesLoaded[idx].handle, fcnname);
			if (adr != NULL)  {
				prev = idx;
				return adr; /* found it */
			}
		}
	/*
	 * @-
	 * Try the program libraries at large or run through all
	 * loaded files and try to resolve the functionname again.
	 */
	if (dl == NULL) {
		/* the first argument must be the same as the base name of the
		 * library that is created in src/tools */
		dl = mdlopen("libmonetdb5", RTLD_NOW | RTLD_GLOBAL);
	}
	if( dl != NULL){
		adr = (MALfcn) dlsym(dl, fcnname);
		if( adr != NULL)
			return adr; /* found it */
	}
	if (!silent)
		showException(out, MAL,"MAL.getAddress", "address of '%s.%s' not found",
			(modnme?modnme:"<unknown>"), fcnname);
	return NULL;
}
/*
 * @+ Module file loading
 * The default location to search for the module is in monet_mod_path
 * unless an absolute path is given.
 * Loading further relies on the Linux policy to search for the module
 * location in the following order: 1) the colon-separated list of
 * directories in the user's LD_LIBRARY_PATH, 2) the libraries specified
 * in /etc/ld.so.cache and 3) /usr/lib followed by /lib.
 * If the module contains a routine _init, then that code is executed
 * before the loader returns. Likewise the routine _fini is called just
 * before the module is unloaded.
 *
 * A module loading conflict emerges if a function is redefined.
 * A duplicate load is simply ignored by keeping track of modules
 * already loaded.
 */

int
isLoaded(str modulename)
{
	int idx;

	for (idx = 0; idx < lastfile; idx++)
		if (filesLoaded[idx].filename &&
		    strcmp(filesLoaded[idx].filename, modulename) == 0) {
			return 1;
		}
	return 0;
}

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif
str
loadLibrary(str filename, int flag)
{
	int mode = RTLD_NOW | RTLD_GLOBAL;
	char nme[MAXPATHLEN];
	void *handle = NULL;
	str s;
	int idx;
	char *mod_path = GDKgetenv("monet_mod_path");

	/* AIX requires RTLD_MEMBER to load a module that is a member of an
	 * archive.  */
#ifdef RTLD_MEMBER
	mode |= RTLD_MEMBER;
#endif

	for (idx = 0; idx < lastfile; idx++)
		if (filesLoaded[idx].filename &&
		    strcmp(filesLoaded[idx].filename, filename) == 0)
			/* already loaded */
			return MAL_SUCCEED;

	/* ignore any path given */
	if ((s = strrchr(filename, DIR_SEP)) == NULL)
		s = filename;

	if (mod_path != NULL) {
		while (*mod_path == PATH_SEP)
			mod_path++;
		if (*mod_path == 0)
			mod_path = NULL;
	}
	if (mod_path == NULL) {
		if (flag)
			throw(LOADER, "loadLibrary", RUNTIME_FILE_NOT_FOUND ":%s", s);
		return MAL_SUCCEED;
	}

	while (*mod_path) {
		char *p;
		struct stat stbuf;

		if ((p = strchr(mod_path, PATH_SEP)) != NULL)
			*p = '\0';
		/* try hardcoded SO_EXT if that is the same for modules */
#ifdef _AIX
		snprintf(nme, MAXPATHLEN, "%s%c%s_%s%s(%s_%s.0)",
				mod_path, DIR_SEP, SO_PREFIX, s, SO_EXT, SO_PREFIX, s);
#else
		snprintf(nme, MAXPATHLEN, "%s%c%s_%s%s",
				mod_path, DIR_SEP, SO_PREFIX, s, SO_EXT);
#endif
		if (stat(nme, &stbuf) != 0 || !S_ISREG(stbuf.st_mode))
			*nme = '\0';
		if (*nme == '\0' && strcmp(SO_EXT, ".so") != 0) {
			/* try .so */
			snprintf(nme, MAXPATHLEN, "%s%c%s_%s.so",
					mod_path, DIR_SEP, SO_PREFIX, s);
			if (stat(nme, &stbuf) != 0 || !S_ISREG(stbuf.st_mode))
				*nme = '\0';
		}
#ifdef __APPLE__
		if (*nme == '\0' && strcmp(SO_EXT, ".bundle") != 0) {
			/* try .bundle */
			snprintf(nme, MAXPATHLEN, "%s%c%s_%s.bundle",
					mod_path, DIR_SEP, SO_PREFIX, s);
			if (stat(nme, &stbuf) != 0 || !S_ISREG(stbuf.st_mode))
				*nme = '\0';
		}
#endif

		/* restore path */
		if (p != NULL)
			*p = PATH_SEP;

		if (*nme != '\0') {
			handle = dlopen(nme, mode);
			if (handle != NULL) {
				break;
			} else {
				throw(LOADER, "loadLibrary",
						"failed to load library: %s", dlerror());
			}
		}

		if (p == NULL)
			break;
		mod_path = p + 1;
	}

	if (handle == NULL) {
		if (flag)
			throw(LOADER, "loadLibrary", RUNTIME_LOAD_ERROR " could not locate library %s (from within file '%s')", s, filename);
	}

	MT_lock_set(&mal_contextLock, "loadModule");
	if (lastfile == maxfiles) {
		if (handle)
			dlclose(handle);
		showException(GDKout, MAL,"loadModule", "internal error, too many modules loaded");
	} else {
		filesLoaded[lastfile].filename = GDKstrdup(filename);
		filesLoaded[lastfile].fullname = GDKstrdup(nme);
		filesLoaded[lastfile].handle = handle;
		lastfile ++;
	}
	MT_lock_unset(&mal_contextLock, "loadModule");

	return MAL_SUCCEED;
}

/*
 * @-
 * For analysis of memory leaks we should cleanup the libraries before
 * we exit the server. This does not involve the libraries themselves,
 * because they may still be in use.
 */
void
unloadLibraries(void)
{
	int i;

	MT_lock_set(&mal_contextLock, "unloadModule");
	for (i = 0; i < lastfile; i++)
		if (filesLoaded[i].fullname) {
			/* dlclose(filesLoaded[i].handle);*/
			GDKfree(filesLoaded[i].filename);
			GDKfree(filesLoaded[i].fullname);
		}
	lastfile = 0;
	MT_lock_unset(&mal_contextLock, "unloadModule");
}
/*
 * @-
 * To speedup restart and to simplify debugging, the MonetDB server can
 * be statically linked with some (or all) of the modules libraries.
 * A complicating factor is then to avoid users to initiate another load
 * of the module file, because it would lead to a @code{dlopen()} error.
 *
 * The partial way out of this dilema is to administer somewhere
 * the statically bound modules, or to enforce that each module
 * comes with a known routine for which we can search.
 * In the current version we use the former approach.
 *
 * The routine below turns off dynamic loading while parsing the
 * command signature files.
 */
static str preloaded[] = {
	"kernel/bat",
	0
};

int
isPreloaded(str nme)
{
	int i;

	for (i = 0; preloaded[i]; i++)
		if (strcmp(preloaded[i], nme) == 0)
			return 1;
	return 0;
}

void
initLibraries(void)
{
	int i;
	str msg;

	noDlopen = TRUE;
	if(noDlopen == FALSE)
	for(i=0;preloaded[i];i++) {
	    msg = loadLibrary(preloaded[i],FALSE);
		if ( msg )
			mnstr_printf(GDKerr,"#%s\n",msg);
	}
}

/*
 * @+ Handling of Module Library Search Path
 * The plausible locations of the modules can be designated by
 * an environment variable.
 */
static int
cmpstr(const void *_p1, const void *_p2)
{
	const char *p1 = *(char* const*)_p1;
	const char *p2 = *(char* const*)_p2;
	const char *f1 = strrchr(p1, (int) DIR_SEP);
	const char *f2 = strrchr(p2, (int) DIR_SEP);
	return strcmp(f1?f1:p1, f2?f2:p2);
}


#define MAXMULTISCRIPT 48
static char *
locate_file(const char *basename, const char *ext, bit recurse)
{
	char *mod_path = GDKgetenv("monet_mod_path");
	char *fullname;
	size_t fullnamelen;
	size_t filelen = strlen(basename) + strlen(ext);
	str strs[MAXMULTISCRIPT]; /* hardwired limit */
	int lasts = 0;

	if (mod_path == NULL)
		return NULL;

	while (*mod_path == PATH_SEP)
		mod_path++;
	if (*mod_path == 0)
		return NULL;
	fullnamelen = 512;
	fullname = GDKmalloc(fullnamelen);
	if (fullname == NULL)
		return NULL;
	while (*mod_path) {
		size_t i;
		char *p;
		int fd;
		DIR *rdir;

		if ((p = strchr(mod_path, PATH_SEP)) != NULL) {
			i = p - mod_path;
		} else {
			i = strlen(mod_path);
		}
		while (i + filelen + 2 > fullnamelen) {
			fullnamelen += 512;
			fullname = GDKrealloc(fullname, fullnamelen);
			if (fullname == NULL)
				return NULL;
		}
		/* we are now sure the directory name, file
		   base name, extension, and separator fit
		   into fullname, so we don't need to do any
		   extra checks */
		strncpy(fullname, mod_path, i);
		fullname[i] = DIR_SEP;
		strcpy(fullname + i + 1, basename);
		/* see if this is a directory, if so, recurse */
		if (recurse == 1 && (rdir = opendir(fullname)) != NULL) {
			struct dirent *e;
			/* list *ext, sort, return */
			while ((e = readdir(rdir)) != NULL) {
				if (strcmp(e->d_name, "..") == 0 || strcmp(e->d_name, ".") == 0)
					continue;
				if (strcmp(e->d_name + strlen(e->d_name) - strlen(ext), ext) == 0) {
					strs[lasts] = GDKmalloc(strlen(fullname) + sizeof(DIR_SEP)
							+ strlen(e->d_name) + sizeof(PATH_SEP) + 1);
					if (strs[lasts] == NULL) {
						while (lasts >= 0)
							GDKfree(strs[lasts--]);
						GDKfree(fullname);
						return NULL;
					}
					sprintf(strs[lasts], "%s%c%s%c", fullname, DIR_SEP, e->d_name, PATH_SEP);
					lasts++;
				}
				if (lasts >= MAXMULTISCRIPT)
					break;
			}
			(void)closedir(rdir);
		} else {
			strcat(fullname + i + 1, ext);
			if ((fd = open(fullname, O_RDONLY)) >= 0) {
				close(fd);
				return GDKrealloc(fullname, strlen(fullname) + 1);
			}
		}
		if ((mod_path = p) == NULL)
			break;
		while (*mod_path == PATH_SEP)
			mod_path++;
	}
	if (lasts > 0) {
		size_t i = 0;
		int c;
		/* assure that an ordering such as 10_first, 20_second works */
		qsort(strs, lasts, sizeof(char *), cmpstr);
		for (c = 0; c < lasts; c++)
			i += strlen(strs[c]) + 1; /* PATH_SEP or \0 */
		fullname = GDKrealloc(fullname, i);
		i = 0;
		for (c = 0; c < lasts; c++) {
			if (strstr(fullname, strs[c]) == NULL) {
				strcpy(fullname + i, strs[c]);
				i += strlen(strs[c]);
			}
			GDKfree(strs[c]);
		}
		fullname[i - 1] = '\0';
		return fullname;
	}
	/* not found */
	GDKfree(fullname);
	return NULL;
}

char *
MSP_locate_script(const char *filename)
{
	return locate_file(filename, MAL_EXT, 1);
}

char *
MSP_locate_sqlscript(const char *filename, bit recurse)
{
	/* no directory semantics (yet) */
	return locate_file(filename, SQL_EXT, recurse);
}

str
loadCoreLibrary(void)
{
	int idx;
	void *handle = NULL;
	int mode = RTLD_NOW | RTLD_GLOBAL;
	char nme[MAXPATHLEN], *mdb_lib_path;
	const char *s = "monetdb5";

		/* AIX requires RTLD_MEMBER to load a module that is a member of an
		 * archive.  */

	for (idx = 0; idx < lastfile; idx++)
		if (filesLoaded[idx].filename &&
		    strcmp(filesLoaded[idx].filename, "libmonetdb5") == 0)
			/* already loaded */
			return MAL_SUCCEED;

#ifdef _AIX
	snprintf(nme, MAXPATHLEN, "%s%s%s(%s%s.0)", SO_PREFIX, s, SO_EXT, SO_PREFIX, s);
#else
	snprintf(nme, MAXPATHLEN, "%s%s%s", SO_PREFIX, s, SO_EXT);
#endif
	mdb_lib_path = locate_file(nme, "", 1);

	if (mdb_lib_path == NULL && strcmp(SO_EXT, ".so") != 0) {
		/* try .so */
		*nme = '\0';
		snprintf(nme, MAXPATHLEN, "%s%s.so", SO_PREFIX, s);
		mdb_lib_path = locate_file(nme, "", 1);
	}
#ifdef __APPLE__
	if (mdb_lib_path == NULL && strcmp(SO_EXT, ".bundle") != 0) {
		/* try .bundle */
		snprintf(nme, MAXPATHLEN, "%s%s.bundle", SO_PREFIX, s);
		mdb_lib_path = locate_file(nme, "", 1);
	}
#endif

	if (mdb_lib_path != NULL)
	{
		handle = dlopen(nme, mode);
		if (handle == NULL) {
			throw(LOADER, "loadCoreLibrary", "failed to load library: %s", dlerror());
		}
	} else {
		throw(LOADER, "loadCoreLibrary", RUNTIME_LOAD_ERROR " could not locate library monetdb5");
	}

	MT_lock_set(&mal_contextLock, "loadModule");
	if (lastfile == maxfiles) {
		if (handle)
			dlclose(handle);
		showException(GDKout, MAL,"loadModule", "internal error, too many modules loaded");
	} else {
		filesLoaded[lastfile].filename = "libmonetdb5";
		filesLoaded[lastfile].fullname = GDKstrdup(mdb_lib_path);
		filesLoaded[lastfile].handle = handle;
		lastfile ++;
	}
	MT_lock_unset(&mal_contextLock, "loadModule");

	return MAL_SUCCEED;
}

void *
getHandler(str modulename)
{
	int idx;
	loadLibrary(modulename, 0);

	for (idx = 0; idx < lastfile; idx++) {
		if (filesLoaded[idx].filename && strcmp(filesLoaded[idx].filename, modulename) == 0)
			return filesLoaded[idx].handle;
	}
	return NULL;
}
