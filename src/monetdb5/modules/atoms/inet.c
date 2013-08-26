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
 * @f inet
 * @a Fabian Groffen
 * @v 1.0
 * @* The inet module
 * The inet module contains a collection of functions that operate on IPv4
 * addresses.  The most relevant functions are the `containment' functions
 * that deal with subnet masks.  The functionality of this module is
 * greatly inspired by the PostgreSQL inet atom.
 *
 */
#include "monetdb_config.h"
#include <gdk.h>
#include "mal.h"
#include "mal_exception.h"
#include "inet.h"
/**
 * Creates a new inet from the given string.
 * Warning: GDK function, does NOT pass a string by reference, and wants
 * a pointer to a pointer for the retval!
 * Returns the number of chars read
 */
int
INETfromString(str src, int *len, inet **retval)
{
	int i, last, type;
	lng parse;
	char *endptr;
	char sep;

	last = 0;
	type = 0;

	if (*len < (int)sizeof(inet)) {
		if (*retval != NULL)
			GDKfree(*retval);
		*retval = GDKzalloc(sizeof(inet));
	} else {
		memset(*retval, 0, sizeof(inet));
	}

	/* handle the nil string */
	if (strNil(src)) {
		in_setnil(*retval);
		return(0);
	}

	/* use the DIY technique to guarantee maximum cross-platform
	 * portability */
	for (i = 0; src[i] != '\0'; i++) {
		if (src[i] == '.' || src[i] == '/') {
			sep = src[i];
			src[i] = '\0';
			parse = strtol(src + last, &endptr, 10);
			if (*endptr != '\0') {
				/* this is for the cat his violin
				throw(PARSE, "inet.fromStr", "Error while parsing, unexpected string '%s'", endptr);
				*/
				goto error;	/* yeah, I know, but I'm just simulating try-catch stuff in C now */
			}
			if (parse > 255 || parse < 0) {
				/* this is for the cat his violin
				throw(PARSE, "inet.fromStr", "Illegal quad value: %d", parse);
				*/
				goto error;	/* yeah, I know, but I'm just simulating try-catch stuff in C now */
			}
			switch (type) {
				case 0:
					(*retval)->q1 = (unsigned char) parse;
				break;
				case 1:
					(*retval)->q2 = (unsigned char) parse;
				break;
				case 2:
					(*retval)->q3 = (unsigned char) parse;
				break;
				case 3:
					(*retval)->q4 = (unsigned char) parse;
				break;
			}

			last = i + 1;
			type++;

			if (sep == '/') {
				/* zero out (default) unused bytes */
				switch (type) {
					case 1:
						(*retval)->q2 = (unsigned char) 0;
					case 2:
						(*retval)->q3 = (unsigned char) 0;
					case 3:
						(*retval)->q4 = (unsigned char) 0;
					break;
				}
				/* force evaluation of the mask below when we break
				 * out of this loop */
				type = 4;
				break;
			}
		}
	}
	/* parse the last quad
	 * the contract is that the caller makes sure the string is
	 * null-terminated here */
	parse = strtol(src + last, &endptr, 10);
	if (*endptr != '\0') {
		/* this is for the cat his violin
		throw(PARSE, "inet.fromStr", "Error while parsing, unexpected string '%s'", endptr);
		*/
		goto error;	/* yeah, I know, but I'm just simulating try-catch stuff in C now */
	}
	if (type == 3) {
		if (parse > 255 || parse < 0) {
			/* this is for the cat his violin
			throw(PARSE, "inet.fromStr", "Illegal quad value: %d", parse);
			*/
			goto error;	/* yeah, I know, but I'm just simulating try-catch stuff in C now */
		}
		(*retval)->q4 = (unsigned char) parse;
		/* default to an exact match (all bits) */
		(*retval)->mask = (unsigned char) 32;
	} else if (type == 4) {
		if (parse < 0 || parse > 32) {
			/* this is for the cat his violin
			throw(PARSE, "inet.fromStr", "Illegal mask value: %d", parse);
			*/
			goto error;	/* yeah, I know, but I'm just simulating try-catch stuff in C now */
		}
		(*retval)->mask = (unsigned char) parse;
	} else {
		/* this is for the cat his violin
		   throw(PARSE, "inet.fromStr", "Error while parsing, unexpected string '%s'", endptr);
		   */
		goto error;	/* yeah, I know, but I'm just simulating try-catch stuff in C now */
	}

	return(i);
error: /* catch exception: return NULL */
	in_setnil(*retval);
	*len = 0;	/* signal INETnew something went wrong */
	return(i - 1);
}
/**
 * Returns the string representation of the given inet value.
 * Warning: GDK function
 * Returns the length of the string
 */
int
INETtoString(str *retval, int *len, inet *handle)
{
	inet *value = (inet *)handle;

	if (*len < 19) {
		if (*retval != NULL)
			GDKfree(*retval);
		*retval = GDKmalloc(sizeof(str) * (*len = 19));
	}
	if (in_isnil(value)) {
		*len = snprintf(*retval, *len, "(nil)");
	} else if (value->mask == 32) {
		*len = snprintf(*retval, *len, "%d.%d.%d.%d", value->q1, value->q2, value->q3, value->q4);
	} else {
		*len = snprintf(*retval, *len, "%d.%d.%d.%d/%d", value->q1, value->q2, value->q3, value->q4, value->mask);
	}

	return(*len);
}
/**
 * Returns a inet, parsed from a string.  The fromStr function is used
 * to parse the string.
 */
str
INETnew(inet *retval, str *in)
{
	int pos;
	int len = sizeof(inet);

	pos = INETfromString(*in, &len, &retval);
	if (len == 0)
		throw(PARSE, "inet.new", "Error while parsing at char %d", pos + 1);

	return (MAL_SUCCEED);
}


/* === Operators === */
/**
 * Returns whether val represents a nil inet value
 */
str
INET_isnil(bit *retval, inet * val)
{
	*retval = in_isnil(val);

	return (MAL_SUCCEED);
}
/**
 * Returns whether val1 and val2 are equal.
 */
str
INET_comp_EQ(bit *retval, inet * val1, inet * val2)
{
	if (in_isnil(val1) || in_isnil(val2)) {
		*retval = bit_nil;
	} else if (val1->q1 == val2->q1 && val1->q2 == val2->q2 && val1->q3 == val2->q3 && val1->q4 == val2->q4 && val1->mask == val2->mask) {
		*retval = 1;
	} else {
		*retval = 0;
	}

	return (MAL_SUCCEED);
}
/**
 * Returns whether val1 and val2 are not equal.
 */
str
INET_comp_NEQ(bit *retval, inet * val1, inet * val2)
{
	if (in_isnil(val1) || in_isnil(val2)) {
		*retval = bit_nil;
	} else if (val1->q1 == val2->q1 && val1->q2 == val2->q2 && val1->q3 == val2->q3 && val1->q4 == val2->q4 && val1->mask == val2->mask) {
		*retval = 0;
	} else {
		*retval = 1;
	}

	return (MAL_SUCCEED);
}
/**
 * Returns whether val1 is smaller than val2.
 */
str
INET_comp_LT(bit *retval, inet * val1, inet * val2)
{
	if (in_isnil(val1) || in_isnil(val2)) {
		*retval = bit_nil;
	} else if (val1->q1 < val2->q1) {
		*retval = 1;
	} else if (val1->q1 > val2->q1) {
		*retval = 0;
	} else if (val1->q2 < val2->q2) {
		*retval = 1;
	} else if (val1->q2 > val2->q2) {
		*retval = 0;
	} else if (val1->q3 < val2->q3) {
		*retval = 1;
	} else if (val1->q3 > val2->q3) {
		*retval = 0;
	} else if (val1->q4 < val2->q4) {
		*retval = 1;
	} else if (val1->q4 > val2->q4) {
		*retval = 0;
	} else if (val1->mask < val2->mask) {
		*retval = 1;
	} else {
		*retval = 0;
	}

	return (MAL_SUCCEED);
}
/**
 * Returns whether val1 is greater than val2.
 */
str
INET_comp_GT(bit *retval, inet * val1, inet * val2)
{
	return (INET_comp_LT(retval, val2, val1));
}
/**
 * Returns whether val1 is smaller than or equal to val2.
 */
str
INET_comp_LE(bit *retval, inet * val1, inet * val2)
{
	bit ret;

	INET_comp_LT(&ret, val1, val2);
	if (ret == 0)
		INET_comp_EQ(&ret, val1, val2);

	*retval = ret;
	return (MAL_SUCCEED);
}
/**
 * Returns whether val1 is smaller than or equal to val2.
 */
str
INET_comp_GE(bit *retval, inet * val1, inet * val2)
{
	bit ret;

	/* warning: we use LT here with swapped arguments to avoid one
	 * method invocation, since inet_comp_GT does the same */
	INET_comp_LT(&ret, val2, val1);
	if (ret == 0)
		INET_comp_EQ(&ret, val1, val2);

	*retval = ret;
	return (MAL_SUCCEED);
}
/**
 * Returns whether val1 is contained within val2
 */
str
INET_comp_CW(bit *retval, inet * val1, inet * val2)
{
	if (in_isnil(val1) || in_isnil(val2)) {
		*retval = bit_nil;
	} else if (val1->mask <= val2->mask) {
		/* if the mask is bigger (less specific) or equal it can never
		 * be contained within */
		*retval = 0;
	} else {
		int mask;
		unsigned char m[4] = { 255, 255, 255, 255 };

		/* all operations here are done byte based, to avoid byte sex
		 * problems */

		/* adjust the mask such that it represents a bit string where
		 * each 1 represents a bit that should match
		 * this is not much clarifying, I know */
		mask = 32 - val2->mask;
		if (mask > 0)
			m[3] <<= mask;
		mask -= 8;
		if (mask > 0)
			m[2] <<= mask;
		mask -= 8;
		if (mask > 0)
			m[1] <<= mask;
		mask -= 8;
		if (mask > 0)
			m[0] <<= mask;

		/* if you want to see some bytes, remove this comment
		   fprintf(stderr, "%x %x %x %x => %x %x %x %x  %x %x %x %x\n",
		   m[0], m[1], m[2], m[3], val1->q1, val1->q2,
		   val1->q3, val1->q4, val2->q1, val2->q2, val2->q3,
		   val2->q4);
		 */

		if ((val1->q1 & m[0]) == (val2->q1 & m[0]) && (val1->q2 & m[1]) == (val2->q2 & m[1]) && (val1->q3 & m[2]) == (val2->q3 & m[2]) && (val1->q4 & m[3]) == (val2->q4 & m[3])) {
			*retval = 1;
		} else {
			*retval = 0;
		}

		/* example: (hex notation)
		 * inet1: 10.0.0.0/24
		 * IP1:   10 00 00 00
		 * mask1: ff ff ff 00
		 * &1:    10 00 00 00
		 * inet2: 10.0.0.254
		 * IP2:   10 00 00 ef
		 * mask1: ff ff ff 00
		 * &2:    10 00 00 00
		 * &1 and &2 are equal, so inet2 is within inet1
		 */
	}
	return (MAL_SUCCEED);
}
/**
 * Returns whether val1 is contained within or equal to val2
 */
str
INET_comp_CWE(bit *retval, inet * val1, inet * val2)
{
	bit ret;

	/* use existing code, not fully optimal, but cheap enough */
	INET_comp_CW(&ret, val1, val2);
	if (!ret)
		INET_comp_EQ(&ret, val1, val2);

	*retval = ret;
	return (MAL_SUCCEED);
}
/**
 * Returns whether val1 is contains val2
 */
str
INET_comp_CS(bit *retval, inet * val1, inet * val2)
{
	/* swap the input arguments and call the contained within function */
	return (INET_comp_CW(retval, val2, val1));
}
/**
 * Returns whether val1 contains or is equal to val2
 */
str
INET_comp_CSE(bit *retval, inet * val1, inet * val2)
{
	/* swap the input arguments and call the contained within function */
	return (INET_comp_CWE(retval, val2, val1));
}


/* === Functions === */
/**
 * Returns the broadcast address for the network the inet represents.
 * If the subnet mask is 32, the given input inet is returned.
 */
str
INETbroadcast(inet * retval, inet * val)
{
	*retval = *val;
	if (!in_isnil(val) && val->mask != 32) {
		int mask;
		unsigned char m[4] = { 255, 255, 255, 255 };

		/* all operations here are done byte based, to avoid byte sex
		 * problems */

		/* adjust the mask such that it represents a bit string where
		 * each 1 represents a bit that should match
		 * this is not much clarifying, I know */
		mask = val->mask;
		if (mask > 0)
			m[0] >>= mask;
		mask -= 8;
		if (mask > 0)
			m[1] >>= mask;
		mask -= 8;
		if (mask > 0)
			m[2] >>= mask;
		mask -= 8;
		if (mask > 0)
			m[3] >>= mask;

		/* if you want to see some bytes, remove this comment
		   fprintf(stderr, "%x %x %x %x => %x %x %x %x\n",
		   m[0], m[1], m[2], m[3], val->q1, val->q2,
		   val->q3, val->q4);
		 */

		/* apply the inverted mask, so we get the broadcast */
		retval->q1 |= m[0];
		retval->q2 |= m[1];
		retval->q3 |= m[2];
		retval->q4 |= m[3];

		/* example: (hex notation)
		 * inet: 10.0.0.1/24
		 * IP:   10 00 00 01
		 * mask: 00 00 00 ff
		 * &:    10 00 00 ff
		 * results in 10.0.0.255
		 */
	}
	return (MAL_SUCCEED);
}
/**
 * Extract only the IP address as text.  Unlike the toString function,
 * this function never returns the netmask length.
 */
str
INEThost(str *retval, inet * val)
{
	str ip;

	if (in_isnil(val)) {
		*retval = GDKstrdup(str_nil);
	} else {
		ip = GDKmalloc(sizeof(char) * 16);

		sprintf(ip, "%d.%d.%d.%d", val->q1, val->q2, val->q3, val->q4);

		*retval = ip;
	}
	return (MAL_SUCCEED);
}
/**
 * Extract netmask length.
 */
str
INETmasklen(int *retval, inet * val)
{
	if (in_isnil(val)) {
		*retval = int_nil;
	} else {
		*retval = val->mask;
	}
	return (MAL_SUCCEED);
}
/**
 * Set netmask length for inet value.
 */
str
INETsetmasklen(inet * retval, inet * val, int *mask)
{
	if (*mask < 0 || *mask > 32)
		throw(ILLARG, "inet.setmask", "Illegal netmask length value: %d", *mask);

	*retval = *val;
	if (!in_isnil(val))
		retval->mask = *mask;

	return (MAL_SUCCEED);
}
/**
 * Construct netmask for network.
 */
str
INETnetmask(inet * retval, inet * val)
{
	*retval = *val;
	if (!in_isnil(val)) {
		int mask;
		unsigned char m[4] = { 255, 255, 255, 255 };

		/* all operations here are done byte based, to avoid byte sex
		 * problems */

		/* adjust the mask such that it represents a bit string where
		 * each 1 represents a bit that should match
		 * this is not much clarifying, I know */
		mask = 32 - val->mask;
		if (mask > 0)
			m[3] <<= mask;
		mask -= 8;
		if (mask > 0)
			m[2] <<= mask;
		mask -= 8;
		if (mask > 0)
			m[1] <<= mask;
		mask -= 8;
		if (mask > 0)
			m[0] <<= mask;

		retval->q1 = m[0];
		retval->q2 = m[1];
		retval->q3 = m[2];
		retval->q4 = m[3];
		retval->mask = 32;

		/* example: (hex notation)
		 * inet: 10.0.0.1/24
		 * mask: ff ff ff 00
		 * results in 255.255.255.0
		 */
	}
	return (MAL_SUCCEED);
}
/**
 * Construct host mask for network.
 */
str
INEThostmask(inet * retval, inet * val)
{
	INETnetmask(retval, val);
	/* invert the netmask to obtain the host mask */
	if (!in_isnil(retval)) {
		retval->q1 = ~retval->q1;
		retval->q2 = ~retval->q2;
		retval->q3 = ~retval->q3;
		retval->q4 = ~retval->q4;
	}

	/* example: (hex notation)
	 * netmask: 255.255.255.0
	 * IP:      ff ff ff 00
	 * ~:       00 00 00 ff
	 * results in 0.0.0.255
	 */

	return (MAL_SUCCEED);
}
/**
 * Extract network part of address, returns the same inet if the netmask
 * is equal to 32.  This function basically zeros out values that are
 * not covered by the netmask.
 */
str
INETnetwork(inet * retval, inet * val)
{
	*retval = *val;
	if (!in_isnil(val)) {
		int mask;
		unsigned char m[4] = { 255, 255, 255, 255 };

		/* all operations here are done byte based, to avoid byte sex
		 * problems */

		/* adjust the mask such that it represents a bit string where
		 * each 1 represents a bit that should match
		 * this is not much clarifying, I know */
		mask = 32 - val->mask;
		if (mask > 0)
			m[3] <<= mask;
		mask -= 8;
		if (mask > 0)
			m[2] <<= mask;
		mask -= 8;
		if (mask > 0)
			m[1] <<= mask;
		mask -= 8;
		if (mask > 0)
			m[0] <<= mask;

		retval->q1 &= m[0];
		retval->q2 &= m[1];
		retval->q3 &= m[2];
		retval->q4 &= m[3];

		/* example: (hex notation)
		 * inet: 10.0.0.1/24
		 * IP:   10 00 00 01
		 * mask: ff ff ff 00
		 * &:    10 00 00 00
		 * results in 10.0.0.0/24
		 */
	}
	return (MAL_SUCCEED);
}
/**
 * Extract IP address and netmask length as text.  Unlike the toStr
 * function, this function always prints the netmask length.
 */
str
INETtext(str *retval, inet * val)
{
	str ip;

	if (in_isnil(val)) {
		*retval = GDKstrdup(str_nil);
	} else {
		ip = GDKmalloc(sizeof(char) * 19);

		sprintf(ip, "%d.%d.%d.%d/%d", val->q1, val->q2, val->q3, val->q4, val->mask);

		*retval = ip;
	}
	return (MAL_SUCCEED);
}
/**
 * Abbreviated display format as text.  The abbreviation is only made if
 * the value has no bits set to right of mask.  Otherwise the return of
 * this function is equal to the function text.
 */
str
INETabbrev(str *retval, inet * val)
{
	str ip;

	if (in_isnil(val)) {
		*retval = GDKstrdup(str_nil);
	} else {
		int mask = 32 - val->mask;
		unsigned char m[4] = { 255, 255, 255, 255 };

		/* Zero all bits that are allowed to be in there according to
		 * the netmask length.  Afterwards it is easy to see if there
		 * are bits set to the right of the mask, since then all four
		 * quads are zero. */
		mask = val->mask;
		if (mask > 0)
			m[0] >>= mask;
		mask -= 8;
		if (mask > 0)
			m[1] >>= mask;
		mask -= 8;
		if (mask > 0)
			m[2] >>= mask;
		mask -= 8;
		if (mask > 0)
			m[3] >>= mask;

		if ((val->q1 & m[0]) != 0 || (val->q2 & m[1]) != 0 || (val->q3 & m[2]) != 0 || (val->q4 & m[3]) != 0) {
			mask = 32;
		} else {
			mask = val->mask;
		}

		/* example: (hex notation)
		 * inet: 10.1.0.0/16
		 * IP:   10 01 00 00
		 * mask: 00 00 ff ff
		 * &:    00 00 00 00
		 * all zero, thus no bits on the right side of the mask
		 */

		ip = GDKmalloc(sizeof(char) * 19);

		if (mask > 24) {
			sprintf(ip, "%d.%d.%d.%d/%d", val->q1, val->q2, val->q3, val->q4, val->mask);
		} else if (mask > 16) {
			sprintf(ip, "%d.%d.%d/%d", val->q1, val->q2, val->q3, val->mask);
		} else if (mask > 8) {
			sprintf(ip, "%d.%d/%d", val->q1, val->q2, val->mask);
		} else if (mask > 0) {
			sprintf(ip, "%d/%d", val->q1, val->mask);
		} else {
			sprintf(ip, "/0");
		}

		*retval = ip;
	}
	return (MAL_SUCCEED);
}
str
INET_inet(inet *d, inet *s)
{
	*d = *s;
	return MAL_SUCCEED;
}
str
INET_fromstr(inet *ret, str *s)
{
	int len = sizeof(inet);
	INETfromString(*s, &len, &ret);
	return MAL_SUCCEED;
}
