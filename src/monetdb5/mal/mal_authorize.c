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
 * @f mal_authorize
 * @a M. Kersten, F. Groffen
 * @v 0.3
 * @+ Authorisation adminstration management
 * Authorisation of users is a key concept in protecting the server from
 * malicious and unauthorised users.  This file contains a number of
 * functions that administrate a set of BATs backing the authorisation
 * tables.
 *
 * The implementation is based on three persistent BATs, which keep the
 * usernames, passwords and allowed scenarios for users of the server.
 *
 */
/*
 * @-
 */
#include "monetdb_config.h"
#include "mal_authorize.h"
#include "mcrypt.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif


static str AUTHdecypherValue(str *ret, str *value);
static str AUTHcypherValue(str *ret, str *value);
static str AUTHverifyPassword(int *ret, str *passwd);

static BAT *user = NULL;
static BAT *pass = NULL;

/**
 * Requires the current client to be the admin user thread.  If not the case,
 * this function returns an InvalidCredentialsException.
 */
str
AUTHrequireAdmin(Client *c) {
	oid id;
	Client cntxt = *c;

	if (cntxt == NULL)
		return(MAL_SUCCEED);
	id = cntxt->user;

	if (id != 0) {
		char u[BUFSIZ] = "";
		str user = u;
		str tmp;

		rethrow("requireAdmin", tmp, AUTHresolveUser(&user, &id));
		throw(INVCRED, "requireAdmin", INVCRED_ACCESS_DENIED " '%s'", user);
	}

	return(MAL_SUCCEED);
}

/**
 * Requires the current client to be the admin user, or the user with
 * the given username.  If not the case, this function returns an
 * InvalidCredentialsException.
 */
str
AUTHrequireAdminOrUser(Client *c, str *username) {
	oid id = (*c)->user;
	char u[BUFSIZ] = "";
	str user = u;
	str tmp = MAL_SUCCEED;

	/* root?  then all is well */
	if (id == 0)
		return(MAL_SUCCEED);

	rethrow("requireAdminOrUser", tmp, AUTHresolveUser(&user, &id));
	if (username == NULL || *username == NULL || strcmp(*username, user) != 0) {
		throw(INVCRED, "requireAdminOrUser", INVCRED_ACCESS_DENIED " '%s'", user);
	}

	return(MAL_SUCCEED);
}

static void
AUTHcommit(void)
{
	bat blist[3];

	blist[0] = 0;

	assert(user);
	blist[1] = ABS(user->batCacheid);
	assert(pass);
	blist[2] = ABS(pass->batCacheid);
	TMsubcommit_list(blist, 3);
}

/*
 * @-
 * Localize the authorization tables in the database.  The authorization
 * tables are a set of aligned BATs that store username, password (hashed)
 * and scenario permissions.
 * If the BATs do not exist, they are created, and the monetdb/monetdb
 * administrator account is added.  Initialising the authorization tables
 * can only be done after the GDK kernel has been initialized.
 */
str
AUTHinitTables(void) {
	bat bid;
	BAT *b;
	int isNew = 1;
	str msg = MAL_SUCCEED;

	/* skip loading if already loaded */
	if (user != NULL && pass != NULL)
		return(MAL_SUCCEED);

	/* if one is not NULL here, something is seriously screwed up */
	assert (user == NULL);
	assert (pass == NULL);

	/* load/create users BAT */
	bid = BBPindex("M5system_auth_user");
	if (!bid) {
		b = BATnew(TYPE_oid, TYPE_str, 256);
		if (b == NULL)
			throw(MAL, "initTables.user", MAL_MALLOC_FAIL " user table");

		BATkey(BATmirror(b), TRUE);
		BBPrename(BBPcacheid(b), "M5system_auth_user");
		BATmode(b, PERSISTENT);
	} else {
		b = BATdescriptor(bid);
		isNew = 0;
	}
	assert(b);
	user = b;

	/* load/create password BAT */
	bid = BBPindex("M5system_auth_passwd_v2");
	if (!bid) {
		b = BATnew(TYPE_oid, TYPE_str, 256);
		if (b == NULL)
			throw(MAL, "initTables.passwd", MAL_MALLOC_FAIL " password table");

		BBPrename(BBPcacheid(b), "M5system_auth_passwd_v2");
		BATmode(b, PERSISTENT);
	} else {
		b = BATdescriptor(bid);
		isNew = 0;
	}
	assert(b);
	pass = b;

	if (isNew == 1) {
		/* insert the monetdb/monetdb administrator account on a
		 * complete fresh and new auth tables system */
		str user = "monetdb";
		str pw; /* will become the right hash for "monetdb" */
		int len = (int) strlen(user);
		oid uid;
		Client c = &mal_clients[0];

		pw = mcrypt_BackendSum(user /* because user == pass */, len);
		msg = AUTHaddUser(&uid, &c, &user, &pw);
		free(pw);
		if (msg)
			return msg;
		if (uid != 0)
			throw(MAL, "initTables", INTERNAL_AUTHORIZATION " while they were just created!");
		AUTHcommit();
	}

	return(MAL_SUCCEED);
}

/**
 * Checks the credentials supplied and throws an exception if invalid.
 * The user id of the authenticated user is returned upon success.
 */
str
AUTHcheckCredentials(
		oid *uid,
		Client *c,
		str *username,
		str *passwd,
		str *challenge,
		str *algo)
{
	str tmp;
	str pwd = NULL;
	str hash = NULL;
	BUN p, q;
	oid *id;
	BATiter useri, passi;

	rethrow("checkCredentials", tmp, AUTHrequireAdminOrUser(c, username));
	assert(user);
	assert(pass);

	if (*username == NULL || strNil(*username))
		throw(INVCRED, "checkCredentials", "invalid credentials for unknown user");

	p = BUNfnd(BATmirror(user), *username);
	if (p == BUN_NONE) {
		/* DO NOT reveal that the user doesn't exist here! */
		throw(INVCRED, "checkCredentials", INVCRED_INVALID_USER " '%s'", *username);
	}
	useri = bat_iterator(user);
	id = (oid*)(BUNhead(useri, p));

	/* a NULL password is impossible (since we should be dealing with
	 * hashes here) so we can bail out immediately
	 */
	if (*passwd == NULL || strNil(*passwd)) {
		/* DO NOT reveal that the password is NULL here! */
		throw(INVCRED, "checkCredentials", INVCRED_INVALID_USER " '%s'", *username);
	}

	/* find the corresponding password to the user */
	q = BUNfnd(pass, id);
	assert (q != BUN_NONE);
	passi = bat_iterator(pass);
	tmp = (str)BUNtail(passi, q);
	assert (tmp != NULL);
	/* decypher the password (we lose the original tmp here) */
	rethrow("checkCredentials", tmp, AUTHdecypherValue(&pwd, &tmp));
	/* generate the hash as the client should have done */
	hash = mcrypt_hashPassword(*algo, pwd, *challenge);
	GDKfree(pwd);
	/* and now we have it, compare it to what was given to us */
	if (strcmp(*passwd, hash) != 0) {
		/* of course we DO NOT print the password here */
		free(hash);
		throw(INVCRED, "checkCredentials", INVCRED_INVALID_USER " '%s'", *username);
	}
	free(hash);

	*uid = *id;
	return(MAL_SUCCEED);
}

/**
 * Adds the given user with password to the administration.  The
 * return value of this function is the user id of the added user.
 */
str
AUTHaddUser(oid *uid, Client *c, str *username, str *passwd) {
	BUN p;
	oid *id;
	str tmp;
	str hash = NULL;
	BATiter useri;

	rethrow("addUser", tmp, AUTHrequireAdmin(c));
	assert(user);
	assert(pass);

	/* some pre-condition checks */
	if (*username == NULL || strNil(*username))
		throw(ILLARG, "addUser", "username should not be nil");
	if (*passwd == NULL || strNil(*passwd))
		throw(ILLARG, "addUser", "password should not be nil");
	rethrow("addUser", tmp, AUTHverifyPassword(NULL, passwd));

	/* ensure that the username is not already there */
	p = BUNfnd(BATmirror(user), *username);
	if (p != BUN_NONE)
		throw(MAL, "addUser", "user '%s' already exists", *username);

	/* we assume the BATs are still aligned */
	rethrow("addUser", tmp, AUTHcypherValue(&hash, passwd));
	/* needs force, as SQL makes a view over user */
	BUNappend(user, *username, TRUE);
	BUNappend(pass, hash, FALSE);
	GDKfree(hash);
	/* retrieve the oid of the just inserted user */
	p = BUNfnd(BATmirror(user), *username);
	assert (p != BUN_NONE);
	useri = bat_iterator(user);
	id = (oid*)(BUNhead(useri, p));

	/* make the stuff persistent */
	AUTHcommit();

	*uid = *id;
	return(MAL_SUCCEED);
}

/**
 * Removes the given user from the administration.
 */
str
AUTHremoveUser(Client *c, str *username) {
	BUN p;
	BAT *b;
	oid id;
	str tmp;
	BATiter useri;

	rethrow("removeUser", tmp, AUTHrequireAdmin(c));
	assert(user);
	assert(pass);

	/* pre-condition check */
	if (*username == NULL || strNil(*username))
		throw(ILLARG, "removeUser", "username should not be nil");

	/* ensure that the username exists */
	p = BUNfnd(BATmirror(user), *username);
	if (p == BUN_NONE)
		throw(MAL, "removeUser", "no such user: '%s'", *username);
	useri = bat_iterator(user);
	id = *(oid*)(BUNhead(useri, p));

	/* find the name of the administrator and see if it equals username */
	if (id == (*c)->user)
		throw(MAL, "removeUser", "cannot remove yourself");

	/* now, we got the oid, start removing the related tuples */
	b = BATmirror(BATselect(BATmirror(user), &id, &id));
	assert(BATcount(b) != 0);
	BATdel(user, b, TRUE);
	b = BATmirror(BATselect(BATmirror(pass), &id, &id));
	assert(BATcount(b) != 0);
	BATdel(pass, b, FALSE);

	/* make the stuff persistent */
	AUTHcommit();

	return(MAL_SUCCEED);
}

/**
 * Changes the username of the user indicated by olduser into newuser.
 * If the username is already in use, an exception is thrown and nothing
 * is modified.
 */
str
AUTHchangeUsername(Client *c, str *olduser, str *newuser)
{
	BUN p, q;
	str tmp;
	BATiter useri;
	oid id;

	rethrow("addUser", tmp, AUTHrequireAdminOrUser(c, olduser));

	/* precondition checks */
	if (*olduser == NULL || strNil(*olduser))
		throw(ILLARG, "changeUsername", "old username should not be nil");
	if (*newuser == NULL || strNil(*newuser))
		throw(ILLARG, "changeUsername", "new username should not be nil");

	/* see if the olduser is valid */
	p = BUNfnd(BATmirror(user), *olduser);
	if (p == BUN_NONE)
		throw(MAL, "changeUsername", "user '%s' does not exist", *olduser);
	/* ... and if the newuser is not there yet */
	q = BUNfnd(BATmirror(user), *newuser);
	if (q != BUN_NONE)
		throw(MAL, "changeUsername", "user '%s' already exists", *newuser);

	/* ok, just do it! (with force, because sql makes view over it) */
	useri = bat_iterator(user);
	id = *(oid*)BUNhead(useri, p);
	BUNinplace(user, p, &id, *newuser, TRUE);
	AUTHcommit();

	return(MAL_SUCCEED);
}

/**
 * Changes the password of the current user to the given password.  The
 * old password must match the one stored before the new password is
 * set.
 */
str
AUTHchangePassword(Client *c, str *oldpass, str *passwd) {
	BUN p;
	str tmp= NULL;
	str hash= NULL;
	oid id;
	BATiter passi;
	str msg= MAL_SUCCEED;

	/* precondition checks */
	if (*oldpass == NULL || strNil(*oldpass))
		throw(ILLARG, "changePassword", "old password should not be nil");
	if (*passwd == NULL || strNil(*passwd))
		throw(ILLARG, "changePassword", "password should not be nil");
	rethrow("changePassword", tmp, AUTHverifyPassword(NULL, passwd));

	/* check the old password */
	id = (*c)->user;
	p = BUNfnd(pass, &id);
	assert(p != BUN_NONE);
	passi = bat_iterator(pass);
	tmp = BUNtail(passi, p);
	assert (tmp != NULL);
	/* decypher the password */
	msg= AUTHdecypherValue(&hash, &tmp);
	if ( msg){
		GDKfree(hash);
		return msg;
	}
	if (strcmp(hash, *oldpass) != 0){
		GDKfree(hash);
		throw(INVCRED, "changePassword", "Access denied");
	}

	GDKfree(hash);
	/* cypher the password */
	msg= AUTHcypherValue(&hash, passwd);
	if ( msg){
		GDKfree(hash);
		return msg;
	}

	/* ok, just overwrite the password field for this user */
	BUNinplace(pass, p, &id, hash, FALSE);
	GDKfree(hash);
	AUTHcommit();

	return(MAL_SUCCEED);
}

/**
 * Changes the password of the given user to the given password.  This
 * function can be used by the administrator to reset the password for a
 * user.  Note that for the administrator to change its own password, it
 * cannot use this function for obvious reasons.
 */
str
AUTHsetPassword(Client *c, str *username, str *passwd) {
	BUN p;
	str tmp;
	str hash = NULL;
	oid id;
	BATiter useri;

	rethrow("setPassword", tmp, AUTHrequireAdmin(c));

	/* precondition checks */
	if (*username == NULL || strNil(*username))
		throw(ILLARG, "setPassword", "username should not be nil");
	if (*passwd == NULL || strNil(*passwd))
		throw(ILLARG, "setPassword", "password should not be nil");
	rethrow("setPassword", tmp, AUTHverifyPassword(NULL, passwd));

	id = (*c)->user;
	/* find the name of the administrator and see if it equals username */
	p = BUNfnd(user, &id);
	assert (p != BUN_NONE);
	useri = bat_iterator(user);
	tmp = BUNtail(useri, p);
	assert (tmp != NULL);
	if (strcmp(tmp, *username) == 0)
		throw(INVCRED, "setPassword", "The administrator cannot set its own password, use changePassword instead");

	/* see if the user is valid */
	p = BUNfnd(BATmirror(user), *username);
	if (p == BUN_NONE)
		throw(MAL, "setPassword", "no such user '%s'", *username);
	id = *(oid*)BUNhead(useri, p);

	/* cypher the password */
	rethrow("setPassword", tmp, AUTHcypherValue(&hash, passwd));
	/* ok, just overwrite the password field for this user */
	p = BUNfnd(pass, &id);
	assert (p != BUN_NONE);
	BUNinplace(pass, p, &id, hash, FALSE);
	GDKfree(hash);
	AUTHcommit();

	return(MAL_SUCCEED);
}

/**
 * Resolves the given user id and returns the associated username.  If
 * the id is invalid, an exception is thrown.  The given pointer to the
 * username char buffer should be NULL if this function is supposed to
 * allocate memory for it.  If the pointer is pointing to an already
 * allocated buffer, it is supposed to be of size BUFSIZ.
 */
str
AUTHresolveUser(str *username, oid *uid)
{
	BUN p;
	BATiter useri;

	if (uid == NULL || *uid == oid_nil)
		throw(ILLARG, "resolveUser", "userid should not be nil");

	p = BUNfnd(user, uid);
	if (p == BUN_NONE)
		throw(MAL, "resolveUser", "No such user with id: " OIDFMT, *uid);

	assert (username != NULL);

	useri = bat_iterator(user);
	if (*username == NULL) {
		*username = GDKstrdup((str)(BUNtail(useri, p)));
	} else {
		snprintf(*username, BUFSIZ, "%s", (str)(BUNtail(useri, p)));
	}

	return(MAL_SUCCEED);
}

/**
 * Returns the username of the given client.
 */
str
AUTHgetUsername(str *username, Client *c) {
	BUN p;
	oid id;
	BATiter useri;

	id = (*c)->user;
	p = BUNfnd(user, &id);

	/* If you ask for a username using a client struct, and that user
	 * doesn't exist, you seriously screwed up somehow.  If this
	 * happens, it may be a security breach/attempt, and hence
	 * terminating the entire system seems like the right thing to do to
	 * me. */
	if (p == BUN_NONE)
		GDKfatal("Internal error: user id that doesn't exist: " OIDFMT, id);

	useri = bat_iterator(user);
	*username = GDKstrdup( BUNtail(useri, p));
	return(MAL_SUCCEED);
}

/**
 * Returns a BAT with user names in the tail, and user ids in the head.
 */
str
AUTHgetUsers(BAT **ret, Client *c) {
	str tmp;

	rethrow("getUsers", tmp, AUTHrequireAdmin(c));

	*ret = BATcopy(user, user->htype, user->ttype, FALSE);
	return(NULL);
}

/**
 * Returns the password hash as used by the backend for the given
 * username.  Throws an exception if called by a non-superuser.
 */
str
AUTHgetPasswordHash(str *ret, Client *c, str *username) {
	BUN p;
	oid id;
	BATiter i;
	str tmp;
	str passwd = NULL;

	rethrow("getPasswordHash", tmp, AUTHrequireAdmin(c));

	if (*username == NULL || strNil(*username))
		throw(ILLARG, "getPasswordHash", "username should not be nil");

	p = BUNfnd(BATmirror(user), *username);
	if (p == BUN_NONE)
		throw(MAL, "getPasswordHash", "user '%s' does not exist", *username);
	i = bat_iterator(user);
	id = *(oid*)BUNhead(i, p);
	p = BUNfnd(pass, &id);
	assert(p != BUN_NONE);
	i = bat_iterator(pass);
	tmp = BUNtail(i, p);
	assert (tmp != NULL);
	/* decypher the password */
	rethrow("changePassword", tmp, AUTHdecypherValue(&passwd, &tmp));

	*ret = GDKstrdup(passwd);
	return(NULL);
}


/*=== the vault ===*/

/* yep, the vault key is just stored in memory */
static str vaultKey = NULL;

/**
 * Unlocks the vault with the given password.  Since the password is
 * just the decypher key, it is not possible to directly check whether
 * the given password is correct.  If incorrect, however, all decypher
 * operations will probably fail or return an incorrect decyphered
 * value.
 */
str
AUTHunlockVault(str *password) {
	if (password == NULL || strNil(*password))
		throw(ILLARG, "unlockVault", "password should not be nil");

	/* even though I think this function should be called only once, it
	 * is not of real extra efforts to avoid a mem-leak if it is used
	 * multiple times */
	if (vaultKey != NULL)
		GDKfree(vaultKey);

	vaultKey = GDKstrdup(*password);
	return(MAL_SUCCEED);
}

/**
 * Decyphers a given value, using the vaultKey.  The returned value
 * might be incorrect if the vaultKey is incorrect or unset.  If the
 * cypher algorithm fails or detects an invalid password, it might throw
 * an exception.  The ret string is GDKmalloced, and should be GDKfreed
 * by the caller.
 */
static str
AUTHdecypherValue(str *ret, str *value) {
	/* Cyphering and decyphering can be done using many algorithms.
	 * Future requirements might want a stronger cypher than the XOR
	 * cypher chosen here.  It is left up to the implementor how to do
	 * that once those algoritms become available.  It could be
	 * #ifdef-ed or on if-basis depending on whether the cypher
	 * algorithm is a compile, or runtime option.  When necessary, this
	 * function could be extended with an extra argument that indicates
	 * the cypher algorithm.
	 */

	/* this is the XOR decypher implementation */
	str r = GDKmalloc(sizeof(char) * (strlen(*value) + 1));
	str w = r;
	str s = *value;
	char t = '\0';
	int escaped = 0;
	/* we default to some garbage key, just to make password unreadable
	 * (a space would only uppercase the password) */
	int keylen = 0;

	if (vaultKey == NULL)
		throw(MAL, "decypherValue", "The vault is still locked!");

	keylen = (int) strlen(vaultKey);

	/* XOR all characters.  If we encounter a 'one' char after the XOR
	 * operation, it is an escape, so replace it with the next char. */
	for (; (t = *s) != '\0'; s++) {
		if (t == '\1' && escaped == 0) {
			escaped = 1;
			continue;
		} else if (escaped != 0) {
			t -= 1;
			escaped = 0;
		}
		*w = t ^ vaultKey[(w - r) % keylen];
		w++;
	}
	*w = '\0';

	*ret = r;
	return(MAL_SUCCEED);
}

/**
 * Cyphers the given string using the vaultKey.  If the cypher algorithm
 * fails or detects an invalid password, it might throw an exception.
 * The ret string is GDKmalloced, and should be GDKfreed by the caller.
 */
static str
AUTHcypherValue(str *ret, str *value) {
	/* this is the XOR cypher implementation */
	str r = GDKmalloc(sizeof(char) * (strlen(*value) * 2 + 1));
	str w = r;
	str s = *value;
	/* we default to some garbage key, just to make password unreadable
	 * (a space would only uppercase the password) */
	int keylen = 0;

	if (vaultKey == NULL)
		throw(MAL, "cypherValue", "The vault is still locked!");

	keylen = (int) strlen(vaultKey);

	/* XOR all characters.  If we encounter a 'zero' char after the XOR
	 * operation, escape it with an 'one' char. */
	for (; *s != '\0'; s++) {
		*w = *s ^ vaultKey[(s - *value) % keylen];
		if (*w == '\0') {
			*w++ = '\1';
			*w = '\1';
		} else if (*w == '\1') {
			*w++ = '\1';
			*w = '\2';
		}
		w++;
	}
	*w = '\0';

	*ret = r;
	return(MAL_SUCCEED);
}

/**
 * Checks if the given string is a (hex represented) hash for the
 * current backend.  This check allows to at least forbid storing
 * trivial plain text passwords by a simple check.
 */
static str
AUTHverifyPassword(int *ret, str *passwd) {
	char *p = *passwd;
	size_t len = strlen(p);

	(void)ret;

#ifdef HAVE_RIPEMD160
	if (strcmp(MONETDB5_PASSWDHASH, "RIPEMD160") == 0) {
		if (len != 20 * 2)
			throw(MAL, "verifyPassword",
					"password is not 40 chars long, is it a hex "
					"representation of a RIPEMD160 password hash?");
	} else
#endif
#ifdef HAVE_SHA512
	if (strcmp(MONETDB5_PASSWDHASH, "SHA512") == 0) {
		if (len != 64 * 2)
			throw(MAL, "verifyPassword",
					"password is not 128 chars long, is it a hex "
					"representation of a SHA-2 512-bits password hash?");
	} else
#endif
#ifdef HAVE_SHA384
	if (strcmp(MONETDB5_PASSWDHASH, "SHA384") == 0) {
		if (len != 48 * 2)
			throw(MAL, "verifyPassword",
					"password is not 96 chars long, is it a hex "
					"representation of a SHA-2 384-bits password hash?");
	} else
#endif
#ifdef HAVE_SHA256
	if (strcmp(MONETDB5_PASSWDHASH, "SHA256") == 0) {
		if (len != 32 * 2)
			throw(MAL, "verifyPassword",
					"password is not 64 chars long, is it a hex "
					"representation of a SHA-2 256-bits password hash?");
	} else
#endif
#ifdef HAVE_SHA224
	if (strcmp(MONETDB5_PASSWDHASH, "SHA224") == 0) {
		if (len != 28 * 2)
			throw(MAL, "verifyPassword",
					"password is not 56 chars long, is it a hex "
					"representation of a SHA-2 224-bits password hash?");
	} else
#endif
#ifdef HAVE_SHA1
	if (strcmp(MONETDB5_PASSWDHASH, "SHA1") == 0) {
		if (len != 20 * 2)
			throw(MAL, "verifyPassword",
					"password is not 40 chars long, is it a hex "
					"representation of a SHA-1 password hash?");
	} else
#endif
#ifdef HAVE_MD5
	if (strcmp(MONETDB5_PASSWDHASH, "MD5") == 0) {
		if (len != 16 * 2)
			throw(MAL, "verifyPassword",
					"password is not 32 chars long, is it a hex "
					"representation of an MD5 password hash?");
	} else
#endif
	{
		throw(MAL, "verifyPassword", "Unknown backend hash algorithm: %s",
				MONETDB5_PASSWDHASH);
	}

	while (*p != '\0') {
		if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')))
			throw(MAL, "verifyPassword",
					"password does contain invalid characters, is it a"
					"lowercase hex representation of a hash?");
		p++;
	}

	return(MAL_SUCCEED);
}
