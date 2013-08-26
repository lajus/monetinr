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
 * @t New Temporal Module
 * @a Peter Boncz, Martin van Dinther
 * @v 1.0
 *
 * @+ Temporal Module
 * The goal of this module is to provide adequate functionality for
 * storing and manipulated time-related data. The minimum requirement
 * is that data can easily be imported from all common commercial
 * RDBMS products.
 *
 * This module supersedes the 'temporal' module that has a number of
 * conceptual problems and hard-to-solve bugs that stem from these
 * problems.
 *
 * The starting point of this module are SQL 92 and the ODBC
 * time-related data types.  Also, some functionalities have been
 * imported from the time classes of the Java standard library.
 *
 * This module introduces four basic types and operations on them:
 * @table @samp
 * @item date
 * a @samp{date} in the Gregorian calendar, e.g. 1999-JAN-31
 *
 * @item daytime
 * a time of day to the detail of milliseconds, e.g. 23:59:59:000
 *
 * @item timestamp
 * a combination of date and time, indicating an exact point in
 *
 * time (GMT). GMT is the time at the Greenwich meridian without a
 * daylight savings time (DST) regime. Absence of DST means that hours
 * are consecutive (no jumps) which makes it easy to perform time
 * difference calculations.
 *
 * @item timezone
 * the local time is often different from GMT (even at Greenwich in
 * summer, as the UK also has DST). Therefore, whenever a timestamp is
 * composed from a local daytime and date, a timezone should be
 * specified in order to translate the local daytime to GMT (and vice
 * versa if a timestamp is to be decomposed in a local date and
 * daytime).
 *
 * @item rule
 * There is an additional atom @samp{rule} that is used to define when
 * daylight savings time in a timezone starts and ends. We provide
 * predefined timezone objects for a number of timezones below (see
 * the init script of this module).  Also, there is one timezone
 * called the local @samp{timezone}, which can be set to one global
 * value in a running Monet server, that is used if the timezone
 * parameter is omitted from a command that needs it (if not set, the
 * default value of the local timezone is plain GMT).
 * @end table
 *
 * @+ Limitations
 * The valid ranges of the various data types are as follows:
 *
 * @table @samp
 * @item min and max year
 * The maximum and minimum dates and timestamps that can be stored are
 * in the years 5,867,411 and -5,867,411, respectively. Interestingly,
 * the year 0 is not a valid year. The year before 1 is called -1.
 *
 * @item valid dates
 * Fall in a valid year, and have a month and day that is valid in
 * that year. The first day in the year is January 1, the last
 * December 31. Months with 31 days are January, March, May, July,
 * August, October, and December, while April, June, September and
 * November have 30 days. February has 28 days, expect in a leap year,
 * when it has 29. A leap year is a year that is an exact multiple of
 * 4. Years that are a multiple of 100 but not of 400 are an
 * exception; they are no leap years.
 *
 * @item valid daytime
 * The smallest daytime is 00:00:00:000 and the largest 23:59:59:999
 * (the hours in a daytime range between [0,23], minutes and seconds
 * between [0,59] and milliseconds between [0:999]).  Daytime
 * identifies a valid time-of-day, not an amount of time (for denoting
 * amounts of time, or time differences, we use here concepts like
 * "number of days" or "number of milliseconds" denoted by some value
 * of a standard integer type).
 *
 * @item valid timestamp
 * is formed by a combination of a valid date and valid daytime.
 * @item difference in days
 * For difference calculations between dates (in numbers of days) we
 * use signed integers (the @i{int} Monet type), hence the valid range
 * for difference calculations is between -2147483648 and 2147483647
 * days (which corresponds to roughly -5,867,411 and 5,867,411 years).
 * @item difference in msecs
 * For difference between timestamps (in numbers of milliseconds) we
 * use 64-bit longs (the @i{lng} Monet type).  These are large
 * integers of maximally 19 digits, which therefore impose a limit of
 * about 106,000,000,000 years on the maximum time difference used in
 * computations.
 * @end table
 *
 * There are also conceptual limitations that are inherent to the time
 * system itself:
 * @table @samp
 * @item Gregorian calendar
 * The basics of the Gregorian calendar stem from the time of Julius
 * Caesar, when the concept of a solar year as consisting of 365.25
 * days (365 days plus once in 4 years one extra day) was
 * introduced. However, this Julian Calendar, made a year 11 minutes
 * long, which subsequently accumulated over the ages, causing a shift
 * in seasons. In medieval times this was noticed, and in 1582 Pope
 * Gregory XIII issued a decree, skipped 11 days. This measure was not
 * adopted in the whole of Europe immediately, however.  For this
 * reason, there were many regions in Europe that upheld different
 * dates.
 *
 * It was only on @b{September 14, 1752} that some consensus was
 * reached and more countries joined the Gregorian Calendar, which
 * also was last modified at that time. The modifications were
 * twofold: first, 12 more days were skipped. Second, it was
 * determined that the year starts on January 1 (in England, for
 * instance, it had been starting on March 25).
 *
 * Other parts of the world have adopted the Gregorian Calendar even
 * later.
 *
 * This module implements the Gregorian Calendar in all its
 * regularity. This means that values before the year 1752 probably do
 * not correspond with the dates that people really used in times
 * before that (what they did use, however, was very vague anyway, as
 * explained above). In solar terms, however, this calendar is
 * reasonably accurate (see the "correction seconds" note below).
 *
 * @item timezones
 * The basic timezone regime was established on @b{November 1, 1884}
 * in the @emph{International Meridian Conference} held in Greenwich
 * (UK). Before that, a different time held in almost any city. The
 * conference established 24 different time zones defined by regular
 * longitude intervals that all differed by one hour. Not for long it
 * was that national and political interest started to erode this
 * nicely regular system.  Timezones now often follow country borders,
 * and some regions (like the Guinea areas in Latin America) have
 * times that differ with a 15 minute grain from GMT rather than an
 * hour or even half-an-hour grain.
 *
 * An extra complication became the introduction of daylight saving
 * time (DST), which causes a time jump in spring, when the clock is
 * skips one hour and in autumn, when the clock is set back one hour
 * (so in a one hour span, the same times occur twice).  The DST
 * regime is a purely political decision made on a country-by-country
 * basis. Countries in the same timezone can have different DST
 * regimes. Even worse, some countries have DST in some years, and not
 * in other years.
 *
 * To avoid confusion, this module stores absolute points of time in
 * GMT only (GMT does not have a DST regime). When storing local times
 * in the database, or retrieving local times from absolute
 * timestamps, a correct timezone object should be used for the
 * conversion.
 *
 * Applications that do not make correct use of timezones, will
 * produce irregular results on e.g. time difference calculations.
 *
 * @item correction seconds
 * Once every such hundred years, a correction second is added on new
 * year's night.  As I do not know the rule, and this rule would
 * seriously complicate this module (as then the duration of a day,
 * which is now the fixed number of 24*60*60*1000 milliseconds,
 * becomes parametrized by the date), it is not implemented. Hence
 * these seconds are lost, so time difference calculations in
 * milliseconds (rather than in days) have a small error if the time
 * difference spans many hundreds of years.
 * @end table
 *
 * TODO: we cannot handle well changes in the timezone rules (e.g.,
 * DST only exists since 40 years, and some countries make frequent
 * changes to the DST policy). To accommodate this we should make
 * timezone_local a function with a year parameter. The tool should
 * maintain and access the timezone database stored in two bats
 * [str,timezone],[str,year].  Lookup of the correct timezone would be
 * dynamic in this structure. The timezone_setlocal would just set the
 * string name of the timezone.
 *
 * @+ Time/date comparison
 */

#include "monetdb_config.h"
#include "mtime.h"

#define get_rule(r)	((r).s.weekday | ((r).s.day<<6) | ((r).s.minutes<<10) | ((r).s.month<<21))
#define set_rule(r,i)							\
	do {										\
		(r).asint = int_nil;					\
		(r).s.weekday = (i)&15;					\
		(r).s.day = ((i)&(63<<6))>>6;			\
		(r).s.minutes = ((i)&(2047<<10))>>10;	\
		(r).s.month = ((i)&(15<<21))>>21;		\
	} while (0)

/* phony zero values, used to get negative numbers from unsigned
 * sub-integers in rule */
#define WEEKDAY_ZERO	8
#define DAY_ZERO	32
#define OFFSET_ZERO	4096

/* as the offset field got split in two, we need macros to get and set them */
#define get_offset(z)	(((int) (((z)->off1 << 7) + (z)->off2)) - OFFSET_ZERO)
#define set_offset(z,i)	do { (z)->off1 = (((i)+OFFSET_ZERO)&8064) >> 7; (z)->off2 = ((i)+OFFSET_ZERO)&127; } while (0)

tzone tzone_local;

/*
 * @+ Defines
 */
str MONTHS[13] = { NULL, "january", "february", "march", "april", "may", "june",
	"july", "august", "september", "october", "november", "december"
};

str DAYS[8] = { NULL, "sunday", "monday", "tuesday", "wednesday", "thursday",
	"friday", "saturday"
};
str COUNT1[7] = { NULL, "first", "second", "third", "fourth", "fifth", "last" };
str COUNT2[7] = { NULL, "1st", "2nd", "3rd", "4th", "5th", "last" };
int NODAYS[13] = { 0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
int CUMDAYS[13] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };

date DATE_MAX, DATE_MIN;		/* often used dates; computed once */

#define YEAR_MAX	5867411
#define YEAR_MIN	-YEAR_MAX
#define MONTHDAYS(m,y)	(((m)!=2)?NODAYS[m]:leapyear(y)?29:28)
#define YEARDAYS(y)	(leapyear(y)?366:365)
#define LEAPYEARS(y)	(leapyears(y)+((y)>=0))
#define DATE(d,m,y)	((m)>0&&(m)<=12&&(d)>0&&(y)!=0&&(y)>=YEAR_MIN&&(y)<=YEAR_MAX&&(d)<=MONTHDAYS(m,y))
#define TIME(h,m,s,x)	((h)>=0&&(h)<24&&(m)>=0&&(m)<60&&(s)>=0&&(s)<60&&(x)>=0 &&(x)<1000)
#define LOWER(c)	(((c) >= 'A' && (c) <= 'Z') ? (c)+'a'-'A' : (c))

/*
 * @+ auxiliary functions
 */

static union {
	timestamp ts;
	lng nilval;
} ts_nil;
static union {
	tzone tz;
	lng nilval;
} tz_nil;
timestamp *timestamp_nil = NULL;
static tzone *tzone_nil = NULL;

static void date_prelude(void);

int TYPE_date;
int TYPE_daytime;
int TYPE_timestamp;
int TYPE_tzone;
int TYPE_rule;

static bat *
monettime_prelude(void)
{
	ts_nil.nilval = lng_nil;
	tz_nil.nilval = lng_nil;

	timestamp_nil = &ts_nil.ts;
	tzone_nil = &tz_nil.tz;

	TYPE_date = ATOMindex("date");
	TYPE_daytime = ATOMindex("daytime");
	TYPE_timestamp = ATOMindex("timestamp");
	TYPE_tzone = ATOMindex("timezone");
	TYPE_rule = ATOMindex("rule");
	date_prelude();
	return NULL;
}

static int synonyms = TRUE;

#define leapyear(y)		((y) % 4 == 0 && ((y) % 100 != 0 || (y) % 400 == 0))

static int
leapyears(int year)
{
	/* count the 4-fold years that passed since jan-1-0 */
	int y4 = year / 4;

	/* count the 100-fold years */
	int y100 = year / 100;

	/* count the 400-fold years */
	int y400 = year / 400;

	return y4 + y400 - y100;	/* may be negative */
}

static date
todate(int day, int month, int year)
{
	date n = date_nil;

	if (DATE(day, month, year)) {
		if (year < 0)
			year++;				/* HACK: hide year 0 */
		n = (date) (day - 1);
		if (month > 2 && leapyear(year))
			n++;
		n += CUMDAYS[month - 1];
		/* current year does not count as leapyear */
		n += 365 * year + LEAPYEARS(year >= 0 ? year - 1 : year);
	}
	return n;
}

void
fromdate(int n, int *d, int *m, int *y)
{
	int month, year = n / 365;
	int day = (n - year * 365) - LEAPYEARS(year >= 0 ? year - 1 : year);

	if (n < 0) {
		year--;
		while (day >= 0) {
			year++;
			day -= YEARDAYS(year);
		}
		day = YEARDAYS(year) + day;
	} else {
		while (day < 0) {
			year--;
			day += YEARDAYS(year);
		}
	}
	day++;
	for (month = 1; month <= 12; month++) {
		int days = MONTHDAYS(month, year);

		if (day <= days)
			break;
		day -= days;
	}
	if (n != int_nil) {
		*d = day;
		*m = month;
		*y = (year <= 0) ? year - 1 : year;	/* HACK: hide year 0 */
	} else {
		*d = *m = *y = int_nil;
	}
}

static daytime
totime(int hour, int min, int sec, int msec)
{
	if (TIME(hour, min, sec, msec)) {
		return (daytime) (((((hour * 60) + min) * 60) + sec) * 1000 + msec);
	}
	return daytime_nil;
}

void
fromtime(int n, int *hour, int *min, int *sec, int *msec)
{
	if (n != int_nil) {
		*hour = n / 3600000;
		n -= (*hour) * 3600000;
		*min = n / 60000;
		n -= (*min) * 60000;
		*sec = n / 1000;
		n -= (*sec) * 1000;
		*msec = n;
	} else {
		*hour = *min = *sec = *msec = int_nil;
	}
}

/* matches regardless of case and extra spaces */
static int
fleximatch(str s, str pat, int min)
{
	int hit, spacy = 0;

	if (min == 0) {
		min = (int) strlen(pat);	/* default mininum required hits */
	}
	for (hit = 0; *pat; s++, hit++) {
		if (LOWER(*s) != *pat) {
			if (GDKisspace(*s) && spacy) {
				min++;
				continue;		/* extra spaces */
			}
			break;
		}
		spacy = GDKisspace(*pat);
		pat++;
	}
	return (hit >= min) ? hit : 0;
}

static int
parse_substr(int *ret, str s, int min, str list[], int size)
{
	int j = 0, i = 0;

	*ret = int_nil;
	while (++i <= size) {
		if ((j = fleximatch(s, list[i], min)) > 0) {
			*ret = i;
			break;
		}
	}
	return j;
}

static int
date_dayofweek(date v)
{
	v %= 7;
	return v <= 0 ? v + 7 : v;
}

#define SKIP_DAYS(d,w,i) d += i; w = (w + i)%7; if (w <= 0) w += 7;

static date
compute_rule(rule *val, int y)
{
	int m = val->s.month, cnt = ABS(val->s.day - DAY_ZERO);
	date d = todate(1, m, y);
	int dayofweek = date_dayofweek(d);
	int w = ABS(val->s.weekday - WEEKDAY_ZERO);

	if (val->s.weekday == WEEKDAY_ZERO || w == WEEKDAY_ZERO) {
		/* cnt-th of month */
		d += cnt - 1;
	} else if (val->s.day > DAY_ZERO) {
		if (val->s.weekday < WEEKDAY_ZERO) {
			/* first weekday on or after cnt-th of month */
			SKIP_DAYS(d, dayofweek, cnt - 1);
			cnt = 1;
		}						/* ELSE cnt-th weekday of month */
		while (dayofweek != w || --cnt > 0) {
			if (++dayofweek == WEEKDAY_ZERO)
				dayofweek = 1;
			d++;
		}
	} else {
		if (val->s.weekday > WEEKDAY_ZERO) {
			/* cnt-last weekday from end of month */
			SKIP_DAYS(d, dayofweek, MONTHDAYS(m, y) - 1);
		} else {
			/* first weekday on or before cnt-th of month */
			SKIP_DAYS(d, dayofweek, cnt - 1);
			cnt = 1;
		}
		while (dayofweek != w || --cnt > 0) {
			if (--dayofweek == 0)
				dayofweek = 7;
			d--;
		}
	}
	return d;
}

static int dummy;

#define BEFORE(d1,m1,d2,m2) (d1 < d2 || (d1 == d2 && m1 <= m2))

static int
timestamp_inside(timestamp *ret, timestamp *t, tzone *z, lng offset)
{
	/* starts with GMT time t, and returns whether it is in the DST for z */
	lng add = (offset != (lng) 0) ? offset : (get_offset(z)) * (lng) 60000;
	int start_days, start_msecs, end_days, end_msecs, year;
	rule start, end;

	MTIMEtimestamp_add(ret, t, &add);

	if (ts_isnil(*ret) || z->dst == 0) {
		return 0;
	}
	set_rule(start, z->dst_start);
	set_rule(end, z->dst_end);

	start_msecs = start.s.minutes * 60000;
	end_msecs = end.s.minutes * 60000;

	fromdate((int) ret->days, &dummy, &dummy, &year);
	start_days = compute_rule(&start, year);
	end_days = compute_rule(&end, year);

	return BEFORE(start_days, start_msecs, end_days, end_msecs) ?
		(BEFORE(start_days, start_msecs, ret->days, ret->msecs) &&
		 BEFORE(ret->days, ret->msecs, end_days, end_msecs)) :
		(BEFORE(start_days, start_msecs, ret->days, ret->msecs) ||
		 BEFORE(ret->days, ret->msecs, end_days, end_msecs));
}

/*
 * @+ ADT implementations
 * @- date
 */
int
date_fromstr(str buf, int *len, date **d)
{
	int day = 0, month = int_nil;
	int year = 0, yearneg = (buf[0] == '-'), yearlast = 0;
	int pos = 0, sep;

	if (*len < (int) sizeof(date)) {
		if (*d)
			GDKfree(*d);
		*d = (date *) GDKzalloc(*len = sizeof(date));
	}
	**d = date_nil;
	if (yearneg == 0 && !GDKisdigit(buf[0])) {
		if (synonyms == 0)
			return 0;
		yearlast = 1;
		sep = ' ';
	} else {
		for (pos = yearneg; GDKisdigit(buf[pos]); pos++) {
			year = (buf[pos] - '0') + year * 10;
			if (year > YEAR_MAX)
				break;
		}
		sep = buf[pos++];
		if (synonyms == 0 && sep != '-') {
			return 0;
		}
		sep = LOWER(sep);
		if (sep >= 'a' && sep <= 'z') {
			sep = 0;
		} else if (sep == ' ') {
			while (buf[pos] == ' ')
				pos++;
		} else if (sep != '-' && sep != '/' && sep != '\\') {
			return 0;			/* syntax error */
		}
	}
	if (GDKisdigit(buf[pos])) {
		month = buf[pos++] - '0';
		if (GDKisdigit(buf[pos])) {
			month = (buf[pos++] - '0') + month * 10;
		}
	} else if (synonyms == 0) {
		return 0;
	} else {
		pos += parse_substr(&month, buf + pos, 3, MONTHS, 12);
	}
	if (month == int_nil || (sep && buf[pos++] != sep)) {
		return 0;				/* syntax error */
	}
	if (sep == ' ') {
		while (buf[pos] == ' ')
			pos++;
	}
	if (!GDKisdigit(buf[pos])) {
		return 0;				/* syntax error */
	}
	while (GDKisdigit(buf[pos])) {
		day = (buf[pos++] - '0') + day * 10;
		if (day > 31)
			break;
	}
	if (yearlast && buf[pos] == ',') {
		while (buf[++pos] == ' ')
			;
		if (buf[pos] == '-') {
			yearneg = 1;
			pos++;
		}
		while (GDKisdigit(buf[pos])) {
			year = (buf[pos++] - '0') + year * 10;
			if (year > YEAR_MAX)
				break;
		}
	}
	/* handle semantic error here (returns nil in that case) */
	**d = todate(day, month, yearneg ? -year : year);
	return pos;
}

int
date_tostr(str *buf, int *len, date *val)
{
	int day, month, year;

	fromdate((int) *val, &day, &month, &year);
	/* longest possible string: "-5867411-01-01" i.e. 14 chars
	   without NUL (see definition of YEAR_MIN/YEAR_MAX above) */
	if (*len < 15) {
		if (*buf)
			GDKfree(*buf);
		*buf = (str) GDKzalloc(*len = 15);
	}
	if (*val == date_nil || !DATE(day, month, year)) {
		strcpy(*buf, "nil");
		return 3;
	}
	sprintf(*buf, "%d-%02d-%02d", year, month, day);
	return (int) strlen(*buf);
}

/*
 * @- daytime
 */
int
daytime_fromstr(str buf, int *len, daytime **ret)
{
	int hour, min, sec = 0, msec = 0, pos = 0;

	if (*len < (int) sizeof(daytime)) {
		if (*ret)
			GDKfree(*ret);
		*ret = (daytime *) GDKzalloc(*len = sizeof(daytime));
	}
	**ret = daytime_nil;
	if (!GDKisdigit(buf[pos])) {
		return 0;				/* syntax error */
	}
	for (hour = 0; GDKisdigit(buf[pos]); pos++) {
		if (hour <= 24)
			hour = (buf[pos] - '0') + hour * 10;
	}
	if ((buf[pos++] != ':') || !GDKisdigit(buf[pos])) {
		return 0;				/* syntax error */
	}
	for (min = 0; GDKisdigit(buf[pos]); pos++) {
		if (min <= 60)
			min = (buf[pos] - '0') + min * 10;
	}
	if ((buf[pos] == ':') && GDKisdigit(buf[pos + 1])) {
		for (pos++, sec = 0; GDKisdigit(buf[pos]); pos++) {
			if (sec <= 60)
				sec = (buf[pos] - '0') + sec * 10;
		}
		if ((buf[pos] == '.' || (synonyms && buf[pos] == ':')) &&
			GDKisdigit(buf[pos + 1])) {
			int fac = 100;

			for (pos++, msec = 0; GDKisdigit(buf[pos]); pos++) {
				msec += (buf[pos] - '0') * fac;
				fac /= 10;
			}
		}
	}
	/* handle semantic error here (returns nil in that case) */
	**ret = totime(hour, min, sec, msec);
	return pos;
}

int
daytime_tz_fromstr(str buf, int *len, daytime **ret)
{
	str s = buf;
	int pos = daytime_fromstr(s, len, ret);
	lng val, offset = 0;
	daytime mtime = 24 * 60 * 60 * 1000;

	if (!*ret || **ret == daytime_nil)
		return pos;

	s = buf + pos;
	pos = 0;
	while (GDKisspace(*s))
		s++;
	/* incase of gmt we need to add the time zone */
	if (fleximatch(s, "gmt", 0) == 3) {
		s += 3;
	}
	if ((s[0] == '-' || s[0] == '+') &&
		GDKisdigit(s[1]) && GDKisdigit(s[2]) && GDKisdigit(s[pos = 4]) &&
		((s[3] == ':' && GDKisdigit(s[5])) || GDKisdigit(s[pos = 3]))) {
		offset = (((s[1] - '0') * (lng) 10 + (s[2] - '0')) * (lng) 60 + (s[pos] - '0') * (lng) 10 + (s[pos + 1] - '0')) * (lng) 60000;
		pos += 2;
		if (s[0] != '-')
			offset = -offset;
		s += pos;
	} else {
		/* if no tzone is specified; work with the local */
		offset = get_offset(&tzone_local) * (lng) -60000;
	}
	val = **ret + offset;
	if (val < 0)
		val = mtime + val;
	if (val >= mtime)
		val = val - mtime;
	**ret = (daytime) val;
	return (int) (s - buf);
}

int
daytime_tostr(str *buf, int *len, daytime *val)
{
	int hour, min, sec, msec;

	fromtime((int) *val, &hour, &min, &sec, &msec);
	if (*len < 12) {
		if (*buf)
			GDKfree(*buf);
		*buf = (str) GDKzalloc(*len = 13);
	}
	if (*val == daytime_nil || !TIME(hour, min, sec, msec)) {
		strcpy(*buf, "nil");
		return 3;
	}
	sprintf(*buf, "%02d:%02d:%02d.%03d", hour, min, sec, msec);
	return 12;
}

/*
 * @- timestamp
 */
int
timestamp_fromstr(str buf, int *len, timestamp **ret)
{
	str s = buf;
	int pos;
	date *d;
	daytime *t;

	if (*len < (int) sizeof(timestamp)) {
		if (*ret)
			GDKfree(*ret);
		*ret = (timestamp *) GDKzalloc(*len = sizeof(timestamp));
	}
	d = &(*ret)->days;
	t = &(*ret)->msecs;
	(*ret)->msecs = 0;
	s += date_fromstr(buf, len, &d);
	if (s > buf && (*(s) == '@' || *s == ' ' || *s == '-' || *s == 'T')) {
		while (*(++s) == ' ')
			;
		pos = daytime_fromstr(s, len, &t);
		s = pos ? s + pos : buf;
	} else if (!s || *s) {
		(*ret)->msecs = daytime_nil;
	}
	if (s <= buf || (*ret)->days == date_nil || (*ret)->msecs == daytime_nil) {
		**ret = *timestamp_nil;
	} else {
		lng offset = 0;

		while (GDKisspace(*s))
			s++;
		/* incase of gmt we need to add the time zone */
		if (fleximatch(s, "gmt", 0) == 3) {
			s += 3;
		}
		if ((s[0] == '-' || s[0] == '+') &&
			GDKisdigit(s[1]) && GDKisdigit(s[2]) && GDKisdigit(s[pos = 4]) &&
			((s[3] == ':' && GDKisdigit(s[5])) || GDKisdigit(s[pos = 3]))) {
			offset = (((s[1] - '0') * (lng) 10 + (s[2] - '0')) * (lng) 60 + (s[pos] - '0') * (lng) 10 + (s[pos + 1] - '0')) * (lng) 60000;
			pos += 2;
			if (s[0] != '-')
				offset = -offset;
			s += pos;
		} else {
			/* if no tzone is specified; work with the local */
			timestamp tmp = **ret;

			offset = get_offset(&tzone_local) * (lng) -60000;
			if (timestamp_inside(&tmp, &tmp, &tzone_local, (lng) -3600000)) {
				**ret = tmp;
			}
		}
		MTIMEtimestamp_add(*ret, *ret, &offset);
	}
	return (int) (s - buf);
}

int
timestamp_tz_tostr(str *buf, int *len, timestamp *val, tzone *timezone)
{
	int len1, len2, big = 128;
	char buf1[128], buf2[128], *s = *buf, *s1 = buf1, *s2 = buf2;
	if (timezone != NULL) {
		/* int off = get_offset(timezone); */
		timestamp tmp = *val;

		if (!ts_isnil(tmp) && timestamp_inside(&tmp, val, timezone, (lng) 0)) {
			lng add = (lng) 3600000;

			MTIMEtimestamp_add(&tmp, &tmp, &add);
			/* off += 60; */
		}
		len1 = date_tostr(&s1, &big, &tmp.days);
		len2 = daytime_tostr(&s2, &big, &tmp.msecs);

		if (*len < 2 + len1 + len2) {
			if (*buf)
				GDKfree(*buf);
			*buf = (str) GDKzalloc(*len = len1 + len2 + 2);
		}
		s = *buf;
		if (ts_isnil(tmp)) {
			strcpy(s, "nil");
			return 3;
		}
		strcpy(s, buf1);
		s += len1;
		*s++ = ' ';
		strcpy(s, buf2);
		s += len2;
		/* omit GMT distance in order not to confuse the confused user
		   strcpy(s, "GMT"); s += 3;
		   if (off) {
		   *s++ = (off>=0)?'+':'-';
		   sprintf(s, "%02d%02d", ABS(off)/60, ABS(off)%60);
		   s += 4;
		   }
		 */
	}
	return (int) (s - *buf);
}

int
timestamp_tostr(str *buf, int *len, timestamp *val)
{
	return timestamp_tz_tostr(buf, len, val, &tzone_local);
}

static str
count1(int i)
{
	static char buf[16];

	if (i <= 0) {
		return "(illegal number)";
	} else if (i < 6) {
		return COUNT1[i];
	}
	sprintf(buf, "%dth", i);
	return buf;
}

/*
 * @- rule
 */
int
rule_tostr(str *buf, int *len, rule *r)
{
	int hours = r->s.minutes / 60;
	int minutes = r->s.minutes % 60;

	if (*len < 64) {
		if (*buf)
			GDKfree(*buf);
		*buf = (str) GDKzalloc(*len = 64);
	}
	if (r->asint == int_nil) {
		strcpy(*buf, "nil");
	} else if (r->s.weekday == WEEKDAY_ZERO) {
		sprintf(*buf, "%s %d@%02d:%02d",
				MONTHS[r->s.month], r->s.day - DAY_ZERO, hours, minutes);
	} else if (r->s.weekday > WEEKDAY_ZERO && r->s.day > DAY_ZERO) {
		sprintf(*buf, "%s %s from start of %s@%02d:%02d",
				count1(r->s.day - DAY_ZERO), DAYS[r->s.weekday - WEEKDAY_ZERO],
				MONTHS[r->s.month], hours, minutes);
	} else if (r->s.weekday > WEEKDAY_ZERO && r->s.day < DAY_ZERO) {
		sprintf(*buf, "%s %s from end of %s@%02d:%02d",
				count1(DAY_ZERO - r->s.day), DAYS[r->s.weekday - WEEKDAY_ZERO],
				MONTHS[r->s.month], hours, minutes);
	} else if (r->s.day > DAY_ZERO) {
		sprintf(*buf, "first %s on or after %s %d@%02d:%02d",
				DAYS[WEEKDAY_ZERO - r->s.weekday], MONTHS[r->s.month],
				r->s.day - DAY_ZERO, hours, minutes);
	} else {
		sprintf(*buf, "last %s on or before %s %d@%02d:%02d",
				DAYS[WEEKDAY_ZERO - r->s.weekday], MONTHS[r->s.month],
				DAY_ZERO - r->s.day, hours, minutes);
	}
	return (int) strlen(*buf);
}

int
rule_fromstr(str buf, int *len, rule **d)
{
	int day = 0, month = 0, weekday = 0, hours = 0, minutes = 0;
	int neg_day = 0, neg_weekday = 0, pos;
	str cur = buf;

	if (*len < (int) sizeof(rule)) {
		if (*d)
			GDKfree(*d);
		*d = (rule *) GDKzalloc(*len = sizeof(rule));
	}
	(*d)->asint = int_nil;

	/* start parsing something like "first", "second", .. etc */
	pos = parse_substr(&day, cur, 0, COUNT1, 6);
	if (pos == 0) {
		pos = parse_substr(&day, cur, 0, COUNT2, 6);
	}
	if (pos && cur[pos++] == ' ') {
		/* now we must see a weekday */
		cur += pos;
		cur += parse_substr(&weekday, cur, 3, DAYS, 7);
		if (weekday == int_nil) {
			return 0;			/* syntax error */
		}
		pos = fleximatch(cur, " from start of ", 0);
		if (pos == 0) {
			pos = fleximatch(cur, " from end of ", 0);
			if (pos)
				neg_day = 1;
		}
		if (pos && day < 6) {
			/* RULE 1+2: X-th weekday from start/end of month */
			pos = parse_substr(&month, cur += pos, 3, MONTHS, 12);
		} else if (day == 1) {
			/* RULE 3: first weekday on or after-th of month */
			pos = fleximatch(cur, " on or after ", 0);
			neg_weekday = 1;
			day = int_nil;		/* re-read below */
		} else if (day == 6) {
			/* RULE 4: last weekday on or before X-th of month */
			pos = fleximatch(cur, " on or before ", 0);
			neg_weekday = neg_day = 1;
			day = int_nil;		/* re-read below */
		}
		if (pos == 0) {
			return 0;			/* syntax error */
		}
		cur += pos;
	}
	if (day == int_nil) {
		/* RULE 5:  X-th of month */
		cur += parse_substr(&month, cur, 3, MONTHS, 12);
		if (month == int_nil || *cur++ != ' ' || !GDKisdigit(*cur)) {
			return 0;			/* syntax error */
		}
		day = 0;
		while (GDKisdigit(*cur) && day < 31) {
			day = (*(cur++) - '0') + day * 10;
		}
	}

	/* parse hours:minutes */
	if (*cur++ != '@' || !GDKisdigit(*cur)) {
		return 0;				/* syntax error */
	}
	while (GDKisdigit(*cur) && hours < 24) {
		hours = (*(cur++) - '0') + hours * 10;
	}
	if (*cur++ != ':' || !GDKisdigit(*cur)) {
		return 0;				/* syntax error */
	}
	while (GDKisdigit(*cur) && minutes < 60) {
		minutes = (*(cur++) - '0') + minutes * 10;
	}

	/* assign if semantically ok */
	if (day >= 1 && day <= NODAYS[month] &&
		hours >= 0 && hours < 60 &&
		minutes >= 0 && minutes < 60) {
		(*d)->s.month = month;
		(*d)->s.weekday = WEEKDAY_ZERO + (neg_weekday ? -weekday : weekday);
		(*d)->s.day = DAY_ZERO + (neg_day ? -day : day);
		(*d)->s.minutes = hours * 60 + minutes;
	}
	return (int) (cur - buf);
}

/*
 * @- tzone
 */
int
tzone_fromstr(str buf, int *len, tzone **d)
{
	int hours = 0, minutes = 0, neg_offset = 0, pos = 0;
	rule r1, *rp1 = &r1, r2, *rp2 = &r2;
	str cur = buf;

	rp1->asint = rp2->asint = 0;
	if (*len < (int) sizeof(tzone)) {
		if (*d)
			GDKfree(*d);
		*d = (tzone *) GDKzalloc(*len = sizeof(tzone));
	}
	**d = *tzone_nil;

	/* syntax checks */
	if (fleximatch(cur, "gmt", 0) == 0) {
		return 0;				/* syntax error */
	}
	cur += 3;
	if (*cur == '-' || *cur == '+') {
		str bak = cur + 1;

		neg_offset = (*cur++ == '-');
		if (!GDKisdigit(*cur)) {
			return 0;			/* syntax error */
		}
		while (GDKisdigit(*cur) && hours < 9999) {
			hours = (*(cur++) - '0') + hours * 10;
		}
		if (*cur == ':' && GDKisdigit(cur[1])) {
			cur++;
			do {
				minutes = (*(cur++) - '0') + minutes * 10;
			} while (GDKisdigit(*cur) && minutes < 60);
		} else if (*cur != ':' && (cur - bak) == 4) {
			minutes = hours % 100;
			hours = hours / 100;
		} else {
			return 0;			/* syntax error */
		}
	}
	if (fleximatch(cur, "-dst[", 0)) {
		pos = rule_fromstr(cur += 5, len, &rp1);
		if (pos == 0 || cur[pos++] != ',') {
			return 0;			/* syntax error */
		}
		pos = rule_fromstr(cur += pos, len, &rp2);
		if (pos == 0 || cur[pos++] != ']') {
			return 0;			/* syntax error */
		}
		cur += pos;
	}
	/* semantic check */
	if (hours < 24 && minutes < 60 &&
		rp1->asint != int_nil && rp2->asint != int_nil) {
		minutes += hours * 60;
		set_offset(*d, neg_offset ? -minutes : minutes);
		if (pos) {
			(*d)->dst = TRUE;
			(*d)->dst_start = get_rule(r1);
			(*d)->dst_end = get_rule(r2);
		} else {
			(*d)->dst = FALSE;
		}
	}
	return (int) (cur - buf);
}

int
tzone_tostr(str *buf, int *len, tzone *z)
{
	str s;

	if (*len < 160) {
		if (*buf)
			GDKfree(*buf);
		*buf = (str) GDKzalloc(*len = 160);
	}
	s = *buf;
	if (tz_isnil(*z)) {
		strcpy(s, "nil");
		s += 3;
	} else {
		rule dst_start, dst_end;
		int mins = get_offset(z);

		set_rule(dst_start, z->dst_start);
		set_rule(dst_end, z->dst_end);

		strcpy(*buf, "GMT");
		s += 3;
		if (mins > 0) {
			sprintf(s, "+%02d:%02d", mins / 60, mins % 60);
			s += 6;
		} else if (mins < 0) {
			sprintf(s, "-%02d:%02d", (-mins) / 60, (-mins) % 60);
			s += 6;
		}
		if (z->dst) {
			strcpy(s, "-DST[");
			s += 5;
			s += rule_tostr(&s, len, &dst_start);
			*s++ = ',';
			s += rule_tostr(&s, len, &dst_end);
			*s++ = ']';
			*s = 0;
		}
	}
	return (int) (s - *buf);
}

/*
 * @+ operator implementations
 */
static void
date_prelude(void)
{
	MONTHS[0] = (str) str_nil;
	DAYS[0] = (str) str_nil;
	NODAYS[0] = int_nil;
	DATE_MAX = todate(31, 12, YEAR_MAX);
	DATE_MIN = todate(1, 1, YEAR_MIN);
	tzone_local.dst = 0;
	set_offset(&tzone_local, 0);
}

static str
oldduration(int *ndays, str s)
{
	int year = 0, month = 0, day = 0;
	int hour = 0 /*, min=0 */ ;
	char *snew = s;
	int v = 0;

	while (*snew != '\0') {
		if (GDKisdigit(*snew)) {
			v = 0;
			while (GDKisdigit(*snew)) {
				v = v * 10 + (*snew) - '0';
				snew++;
			}
		} else if (isupper((int) (*snew)) || islower((int) (*snew))) {
			switch (*snew++) {
			case 'y':
			case 'Y':
				year = v;
				v = 0;
				break;
			case 'm':
			case 'M':
				if (month || day || hour)	/*min = v */
					;
				else
					month = v;
				v = 0;
				break;
			case 'd':
			case 'D':
				day = v;
				v = 0;
				break;
			case 'h':
			case 'H':
				hour = v;
				v = 0;
				break;
			case 's':
			case 'S':
				v = 0;
				break;
			default:
				/* GDKerror("duration_fromstr: wrong duration '%s'!\n",s); */
				*ndays = int_nil;
				return MAL_SUCCEED;
			}
		} else {
			snew++;
		}
	}
	*ndays = year * 365 + month * 30 + day;
	return MAL_SUCCEED;
}

static str
olddate(date *d, str buf)
{
	int day = 0, month, year, yearneg = (buf[0] == '-'), pos = yearneg;

	*d = date_nil;
	if (!GDKisdigit(buf[pos])) {
		throw(MAL, "mtime.olddate", "syntax error");
	}
	for (year = 0; GDKisdigit(buf[pos]); pos++) {
		year = (buf[pos] - '0') + year * 10;
		if (year > YEAR_MAX)
			break;
	}
	pos += parse_substr(&month, buf + pos, 3, MONTHS, 12);
	if (month == int_nil) {
		throw(MAL, "mtime.olddate", "syntax error");
	}
	if (!GDKisdigit(buf[pos])) {
		throw(MAL, "mtime.olddate", "syntax error");
	}
	while (GDKisdigit(buf[pos])) {
		day = (buf[pos] - '0') + day * 10;
		pos++;
		if (day > 31)
			break;
	}
	/* handle semantic error here (returns nil in that case) */
	*d = todate(day, month, yearneg ? -year : year);
	return MAL_SUCCEED;
}

static str
tzone_set_local(tzone *z)
{
	if (tz_isnil(*z))
		throw(MAL, "mtime.timezone_local", "cannot set timezone to nil");
	tzone_local = *z;
	return MAL_SUCCEED;
}

/* Returns number of day [1-7] from a string (or nil if does not match any). */
static str
day_from_str(int *ret, str day)
{
	if (strcmp(day, str_nil) == 0)
		*ret = int_nil;
	else
		parse_substr(ret, day, 3, DAYS, 7);
	return MAL_SUCCEED;
}

/* creates a daytime from (hours,minutes,seconds,milliseconds) parameters */
static str
daytime_create(daytime *ret, int *hour, int *min, int *sec, int *msec)
{
	*ret = totime(*hour, *min, *sec, *msec);
	return MAL_SUCCEED;
}

/* creates a timestamp from (date,daytime) parameters */
static str
timestamp_create(timestamp *ret, date *d, daytime *t, tzone *z)
{
	if (*d == date_nil || *t == daytime_nil || tz_isnil(*z)) {
		*ret = *timestamp_nil;
	} else {
		lng add = get_offset(z) * (lng) -60000;

		ret->days = *d;
		ret->msecs = *t;
		if (z->dst) {
			timestamp tmp;

			if (timestamp_inside(&tmp, ret, z, (lng) -3600000)) {
				*ret = tmp;
			}
		}
		MTIMEtimestamp_add(ret, ret, &add);
	}
	return MAL_SUCCEED;
}

/* extracts year from date (value between -5867411 and +5867411). */
static str
date_extract_year(int *ret, date *v)
{
	if (*v == date_nil) {
		*ret = int_nil;
	} else {
		fromdate((int) *v, &dummy, &dummy, ret);
	}
	return MAL_SUCCEED;
}

/* extracts month from date (value between 1 and 12) */
static str
date_extract_month(int *ret, date *v)
{
	if (*v == date_nil) {
		*ret = int_nil;
	} else {
		fromdate((int) *v, &dummy, ret, &dummy);
	}
	return MAL_SUCCEED;
}

/* extracts day from date (value between 1 and 31)*/
static str
date_extract_day(int *ret, date *v)
{
	if (*v == date_nil) {
		*ret = int_nil;
	} else {
		fromdate((int) *v, ret, &dummy, &dummy);
	}
	return MAL_SUCCEED;
}

/* Returns N where d is the Nth day of the year (january 1 returns 1). */
static str
date_extract_dayofyear(int *ret, date *v)
{
	if (*v == date_nil) {
		*ret = int_nil;
	} else {
		int year;

		fromdate((int) *v, &dummy, &dummy, &year);
		*ret = (int) (1 + *v - todate(1, 1, year));
	}
	return MAL_SUCCEED;
}

/* Returns the week number */
static str
date_extract_weekofyear(int *ret, date *v)
{
	if (*v == date_nil) {
		*ret = int_nil;
	} else {
		int year, dayofweek;
		date year_jan_1;

		fromdate((int) *v, &dummy, &dummy, &year);
		year_jan_1 = todate(1, 1, year);
		dayofweek = date_dayofweek(year_jan_1);

		if (dayofweek <= 4) {
			/* week of jan 1 belongs to this year */
			*ret = (int) (1 + (*v - year_jan_1 + dayofweek - 1) / 7);
		} else if (*v - year_jan_1 > 7 - dayofweek) {
			/* week of jan 1 belongs to last year; but this is a later week */
			*ret = (int) ((*v - year_jan_1 + dayofweek - 1) / 7);
		} else {
			/* recurse to get last weekno of previous year (it is 52 or 53) */
			date lastyear_dec_31 = todate(31, 12, (year == 1) ? -1 : year - 1);

			return date_extract_weekofyear(ret, &lastyear_dec_31);
		}
	}
	return MAL_SUCCEED;
}

/* Returns the current day  of the week where 1=sunday, .., 7=saturday */
static str
date_extract_dayofweek(int *ret, date *v)
{
	if (*v == date_nil) {
		*ret = int_nil;
	} else {
		*ret = date_dayofweek(*v);
	}
	return MAL_SUCCEED;
}

/* extracts hour from daytime (value between 0 and 23) */
static str
daytime_extract_hours(int *ret, daytime *v)
{
	if (*v == daytime_nil) {
		*ret = int_nil;
	} else {
		fromtime((int) *v, ret, &dummy, &dummy, &dummy);
	}
	return MAL_SUCCEED;
}

/* extracts minutes from daytime (value between 0 and 59) */
static str
daytime_extract_minutes(int *ret, daytime *v)
{
	if (*v == daytime_nil) {
		*ret = int_nil;
	} else {
		fromtime((int) *v, &dummy, ret, &dummy, &dummy);
	}
	return MAL_SUCCEED;
}

/* extracts seconds from daytime (value between 0 and 59) */
static str
daytime_extract_seconds(int *ret, daytime *v)
{
	if (*v == daytime_nil) {
		*ret = int_nil;
	} else {
		fromtime((int) *v, &dummy, &dummy, ret, &dummy);
	}
	return MAL_SUCCEED;
}

/* extracts (milli) seconds from daytime (value between 0 and 59000) */
static str
daytime_extract_sql_seconds(int *ret, daytime *v)
{
	int sec, milli;
	if (*v == daytime_nil) {
		*ret = int_nil;
	} else {
		fromtime((int) *v, &dummy, &dummy, &sec, &milli);
		*ret = sec * 1000 + milli;
	}
	return MAL_SUCCEED;
}

/* extracts milliseconds from daytime (value between 0 and 999) */
static str
daytime_extract_milliseconds(int *ret, daytime *v)
{
	if (*v == daytime_nil) {
		*ret = int_nil;
	} else {
		fromtime((int) *v, &dummy, &dummy, &dummy, ret);
	}
	return MAL_SUCCEED;
}

static str
daytime_add(daytime *ret, daytime *v, lng *msec)
{
	if (*v == daytime_nil) {
		*ret = int_nil;
	} else {
		*ret = *v + (daytime) (*msec);
	}
	return MAL_SUCCEED;
}

/* extracts daytime from timestamp */
static str
timestamp_extract_daytime(daytime *ret, timestamp *t, tzone *z)
{
	if (ts_isnil(*t) || tz_isnil(*z)) {
		*ret = daytime_nil;
	} else {
		timestamp tmp;

		if (timestamp_inside(&tmp, t, z, (lng) 0)) {
			lng add = (lng) 3600000;

			MTIMEtimestamp_add(&tmp, &tmp, &add);
		}
		if (ts_isnil(tmp)) {
			*ret = daytime_nil;
		} else {
			*ret = tmp.msecs;
		}
	}
	return MAL_SUCCEED;
}

/* extracts date from timestamp */
static str
timestamp_extract_date(date *ret, timestamp *t, tzone *z)
{
	if (ts_isnil(*t) || tz_isnil(*z)) {
		*ret = date_nil;
	} else {
		timestamp tmp;

		if (timestamp_inside(&tmp, t, z, (lng) 0)) {
			lng add = (lng) 3600000;

			MTIMEtimestamp_add(&tmp, &tmp, &add);
		}
		if (ts_isnil(tmp)) {
			*ret = date_nil;
		} else {
			*ret = tmp.days;
		}
	}
	return MAL_SUCCEED;
}

/* returns the date that comes a number of day after 'v' (or before
 * iff *delta < 0). */
static str
date_adddays(date *ret, date *v, int *delta)
{
	lng min = DATE_MIN, max = DATE_MAX;
	lng cur = (lng) *v, inc = *delta;

	if (cur == int_nil || inc == int_nil || (inc > 0 && (max - cur) < inc) || (inc < 0 && (min - cur) > inc)) {
		*ret = date_nil;
	} else {
		*ret = *v + *delta;
	}
	return MAL_SUCCEED;
}

/* returns the date that comes a number of months after 'v' (or before
 * if *delta < 0). */
static str
date_addmonths(date *ret, date *v, int *delta)
{
	if (*v == date_nil || *delta == int_nil) {
		*ret = date_nil;
	} else {
		int d, m, y, x, z = *delta;

		fromdate((int) *v, &d, &m, &y);
		*ret = *v;
		while (z > 0) {
			z--;
			x = MONTHDAYS(m, y);
			if (++m == 13) {
				m = 1;
				y++;
			}
			date_adddays(ret, ret, &x);
		}
		while (z < 0) {
			z++;
			if (--m == 0) {
				m = 12;
				y--;
			}
			x = -MONTHDAYS(m, y);
			date_adddays(ret, ret, &x);
		}
	}
	return MAL_SUCCEED;
}

/* returns the timestamp that comes 'milliseconds' after 'value'. */
str
MTIMEtimestamp_add(timestamp *ret, timestamp *v, lng *msecs)
{
	if (!ts_isnil(*v) && *msecs != lng_nil) {
		int days = (int) (*msecs / (24 * 60 * 60 * 1000));

		ret->msecs = (int) (v->msecs + (*msecs - ((lng) days) * (24 * 60 * 60 * 1000)));
		ret->days = v->days;
		if (ret->msecs >= (24 * 60 * 60 * 1000)) {
			days++;
			ret->msecs -= (24 * 60 * 60 * 1000);
		} else if (ret->msecs < 0) {
			days--;
			ret->msecs += (24 * 60 * 60 * 1000);
		}
		if (days) {
			date_adddays(&ret->days, &ret->days, &days);
			if (ret->days == int_nil) {
				*ret = *timestamp_nil;
			}
		}
	} else {
		*ret = *timestamp_nil;
	}
	return MAL_SUCCEED;
}

/* create a DST start/end date rule. */
static str
rule_create(rule *ret, int *month, int *day, int *weekday, int *minutes)
{
	ret->asint = int_nil;
	if (*month != int_nil && *month >= 1 && *month <= 12 &&
		*weekday != int_nil && ABS(*weekday) <= 7 &&
		*minutes != int_nil && *minutes >= 0 && *minutes < 24 * 60 &&
		*day != int_nil && ABS(*day) >= 1 && ABS(*day) <= NODAYS[*month] &&
		(*weekday || *day > 0)) {
		ret->s.month = *month;
		ret->s.day = DAY_ZERO + *day;
		ret->s.weekday = WEEKDAY_ZERO + *weekday;
		ret->s.minutes = *minutes;
	}
	return MAL_SUCCEED;
}

/* create a tzone as a simple hour difference from GMT. */
static str
tzone_create_dst(tzone *ret, int *minutes, rule *start, rule *end)
{
	*ret = *tzone_nil;
	if (*minutes != int_nil && ABS(*minutes) < 24 * 60 &&
		start->asint != int_nil && end->asint != int_nil) {
		set_offset(ret, *minutes);
		ret->dst = TRUE;
		ret->dst_start = get_rule(*start);
		ret->dst_end = get_rule(*end);
	}
	return MAL_SUCCEED;
}

/* create a tzone as an hour difference from GMT and a DST. */
static str
tzone_create(tzone *ret, int *minutes)
{
	*ret = *tzone_nil;
	if (*minutes != int_nil && ABS(*minutes) < 24 * 60) {
		set_offset(ret, *minutes);
		ret->dst = FALSE;
	}
	return MAL_SUCCEED;
}

union lng_tzone {
	lng lval;
	tzone tzval;
};

/*
 * Wrapper
 * The Monet V5 API interface is defined here
 */
#define TIMEZONES(X1, X2)									\
	do {													\
		ticks = (X2);										\
		tzone_create(&ltz.tzval, &ticks);					\
		vr.val.lval = ltz.lval;								\
		tzbatnme = BUNappend(tzbatnme, (X1), FALSE);	\
		tzbatdef = BUNappend(tzbatdef, &vr.val.lval, FALSE);	\
	} while (0)

#define TIMEZONES2(X1, X2, X3, X4)							\
	do {													\
		ticks = (X2);										\
		tzone_create_dst(&ltz.tzval, &ticks, &(X3), &(X4));	\
		vr.val.lval = ltz.lval;								\
		tzbatnme = BUNappend(tzbatnme, (X1), FALSE);	\
		tzbatdef = BUNappend(tzbatdef, &vr.val.lval, FALSE);	\
	} while (0)

/*
 * Include BAT macros
 */
#include "mal.h"
#include "mal_exception.h"
#include <mal_box.h>

str
MTIMEnil2date(date *ret, int *src)
{
	(void) src;
	*ret = date_nil;
	return MAL_SUCCEED;
}

str
MTIMEdate2date(date *ret, date *src)
{
	*ret = *src;
	return MAL_SUCCEED;
}

str
MTIMEdaytime2daytime(daytime *ret, daytime *src)
{
	*ret = *src;
	return MAL_SUCCEED;
}

str
MTIMEtimestamp2timestamp(timestamp *ret, timestamp *src)
{
	*ret = *src;
	return MAL_SUCCEED;
}

static BAT *timezone_name = NULL;
static BAT *timezone_def = NULL;

str
MTIMEprelude(void)
{
	char *msg = NULL;
	Box box;
	ValRecord vr;
	int ticks;
	union lng_tzone ltz;
	rule RULE_MAR, RULE_OCT;
	str s1 = "first sunday from end of march@02:00";
	str s2 = "first sunday from end of october@02:00";
	tzone tz;

	monettime_prelude();
	tz = *tzone_nil;			/* to ensure initialized variables */

	/* here we should initialize the time box as well */
	box = openBox("time");
	if (box == 0)
		throw(MAL, "time.prelude", "failed to open box");
	/* if the box was already filled we can skip initialization */
	if (box->sym->vtop == 0) {
		BAT *tzbatnme = BATnew(TYPE_void, TYPE_str, 30);
		BAT *tzbatdef = BATnew(TYPE_void, ATOMindex("timezone"), 30);

		if (tzbatnme == NULL || tzbatdef == NULL)
			throw(MAL, "time.prelude", "failed to create box");
		BBPrename(tzbatnme->batCacheid, "timezone_name");
		BBPrename(tzbatdef->batCacheid, "timezone_def");
		BATseqbase(tzbatnme,0);
		BATseqbase(tzbatdef,0);
		timezone_name = tzbatnme;
		timezone_def = tzbatdef;

		newVariable(box->sym, GDKstrdup("timezone_name"),
					newBatType(TYPE_str, ATOMindex("timezone")));
		if (bindBAT(box, "timezone_name", "timezone_name")) {
			throw(MAL, "time.prelude", "could not bind timezone_name");
		}
		if (bindBAT(box, "timezone_def", "timezone_def")) {
			throw(MAL, "time.prelude", "could not bind timezone_def");
		}
		vr.vtype = ATOMindex("timezone");
		TIMEZONES("Wake Island", 12 * 60);
		TIMEZONES("Melbourne/Australia", 11 * 60);
		TIMEZONES("Brisbane/Australia", 10 * 60);
		TIMEZONES("Japan", 9 * 60);
		TIMEZONES("Singapore", 8 * 60);
		TIMEZONES("Thailand", 7 * 60);
		TIMEZONES("Pakistan", 5 * 60);
		TIMEZONES("United Arab Emirates", 4 * 60);
		TIMEZONES("GMT", 0 * 0);
		TIMEZONES("Azore Islands", -1 * 60);
		TIMEZONES("Hawaii/USA", -10 * 60);
		TIMEZONES("American Samoa", -11 * 60);
		MTIMErule_fromstr(&RULE_MAR, &s1);
		MTIMErule_fromstr(&RULE_OCT, &s2);
		TIMEZONES2("Kazakhstan", 6 * 60, RULE_MAR, RULE_OCT);
		TIMEZONES2("Moscow/Russia", 3 * 60, RULE_MAR, RULE_OCT);
		TIMEZONES2("East/Europe", 2 * 60, RULE_MAR, RULE_OCT);
		TIMEZONES2("West/Europe", 1 * 60, RULE_MAR, RULE_OCT);
		TIMEZONES2("UK", 0 * 0, RULE_MAR, RULE_OCT);
		TIMEZONES2("Eastern/Brazil", -2 * 60, RULE_OCT, RULE_MAR);
		TIMEZONES2("Western/Brazil", -3 * 60, RULE_OCT, RULE_MAR);
		TIMEZONES2("Andes/Brazil", -4 * 60, RULE_OCT, RULE_MAR);
		TIMEZONES2("East/USA", -5 * 60, RULE_MAR, RULE_OCT);
		TIMEZONES2("Central/USA", -6 * 60, RULE_MAR, RULE_OCT);
		TIMEZONES2("Mountain/USA", -7 * 60, RULE_MAR, RULE_OCT);
		TIMEZONES2("Alaska/USA", -9 * 60, RULE_MAR, RULE_OCT);
	}
	msg = "West/Europe";
	return MTIMEtimezone(&tz, &msg);
}

str
MTIMEepilogue(void)
{
	closeBox("time", 0);
	return MAL_SUCCEED;
}

str
MTIMEsynonyms(bit *allow)
{
	if (*allow != bit_nil)
		synonyms = *allow;
	return MAL_SUCCEED;
}

str
MTIMEoldduration(int *ndays, str *s)
{
	return oldduration(ndays, *s);
}

str
MTIMEolddate(date *d, str *buf)
{
	return olddate(d, *buf);
}

str
MTIMEtimezone(tzone *ret, str *name)
{
	BUN p;
	str s = *name;
	tzone *z;
	BATiter tzi;

	if ((p = BUNfnd(BATmirror(timezone_name), s)) == BUN_NONE)
		throw(MAL, "mtime.setTimezone", "unknown timezone");
	tzi = bat_iterator(timezone_def);
	z = (tzone *) BUNtail(tzi, p);
	if ((s = tzone_set_local(z)) != MAL_SUCCEED)
		return s;
	*ret = *z;
	return MAL_SUCCEED;
}

str
MTIMEtzone_set_local(int res, tzone *z)
{
	(void) res;					/* fool compilers */
	return tzone_set_local(z);
}

str
MTIMEtzone_get_local(tzone *z)
{
	*z = tzone_local;
	return MAL_SUCCEED;
}

str
MTIMElocal_timezone(lng *res)
{
	tzone z;

	MTIMEtzone_get_local(&z);
	*res = get_offset(&z);
	return MAL_SUCCEED;
}

/* Returns month number [1-12] from a string (or nil if does not match any). */
str
MTIMEmonth_from_str(int *ret, str *month)
{
	parse_substr(ret, *month, 3, MONTHS, 12);
	return MAL_SUCCEED;
}

/* Returns month name from a number between [1-7], str(nil) otherwise. */
str
MTIMEmonth_to_str(str *ret, int *month)
{
	*ret = GDKstrdup(MONTHS[(*month < 1 || *month > 12) ? 0 : *month]);
	return MAL_SUCCEED;
}

str
MTIMEday_from_str(int *ret, str *day)
{
	return day_from_str(ret, *day);
}

/* Returns day name from a number between [1-7], str(nil) otherwise. */
str
MTIMEday_to_str(str *ret, int *day)
{
	*ret = GDKstrdup(DAYS[(*day < 1 || *day > 7) ? 0 : *day]);
	return MAL_SUCCEED;
}

str
MTIMEdate_date(date *d, date *s)
{
	*d = *s;
	return MAL_SUCCEED;
}

str
MTIMEdate_tostr(str *ret, date *d)
{
	int big = 128;
	char buf[128], *s1 = buf;

	*s1 = 0;
	date_tostr(&s1, &big, d);
	*ret = GDKstrdup(buf);
	return MAL_SUCCEED;
}

str
MTIMEdate_fromstr(date *ret, str *s)
{
	int len = 0;
	date *d = 0;

	if (strcmp(*s, "nil") == 0) {
		*ret = date_nil;
		return MAL_SUCCEED;
	}
	date_fromstr(*s, &len, &d);
	*ret = *d;
	GDKfree(d);
	return MAL_SUCCEED;
}

/* creates a date from (day,month,year) parameters */
str
MTIMEdate_create(date *ret, int *year, int *month, int *day)
{
	*ret = todate(*day, *month, *year);
	return MAL_SUCCEED;
}

str
MTIMEdaytime_tostr(str *ret, daytime *d)
{
	char buf[128], *s = buf;
	int len = 128;

	*s = 0;
	daytime_tostr(&s, &len, d);
	*ret = GDKstrdup(buf);
	return MAL_SUCCEED;
}

str
MTIMEdaytime_create(daytime *ret, int *hour, int *min, int *sec, int *msec)
{
	return daytime_create(ret, hour, min, sec, msec);
}

str
MTIMEtimestamp_fromstr(timestamp *ret, str *d)
{
	int len = (int) strlen(*d);

	if (strcmp(*d, "nil") == 0) {
		ret->msecs = daytime_nil;
		ret->days = date_nil;
		return MAL_SUCCEED;
	}
	timestamp_fromstr(*d, &len, &ret);
	return MAL_SUCCEED;
}

str
MTIMEtimestamp_timestamp(timestamp *d, timestamp *s)
{
	*d = *s;
	return MAL_SUCCEED;
}

str
MTIMEtimestamp_create(timestamp *ret, date *d, daytime *t, tzone *z)
{
	return timestamp_create(ret, d, t, z);
}

str
MTIMEtimestamp_create_default(timestamp *ret, date *d, daytime *t)
{
	return MTIMEtimestamp_create(ret, d, t, &tzone_local);
}

str
MTIMEtimestamp_create_from_date(timestamp *ret, date *d)
{
	daytime t = totime(0, 0, 0, 0);
	return MTIMEtimestamp_create(ret, d, &t, &tzone_local);
}

str
MTIMEdate_extract_year(int *ret, date *v)
{
	return date_extract_year(ret, v);
}

str
MTIMEdate_extract_month(int *ret, date *v)
{
	return date_extract_month(ret, v);
}

str
MTIMEdate_extract_day(int *ret, date *v)
{
	return date_extract_day(ret, v);
}

str
MTIMEdate_extract_dayofyear(int *ret, date *v)
{
	return date_extract_dayofyear(ret, v);
}

str
MTIMEdate_extract_weekofyear(int *ret, date *v)
{
	return date_extract_weekofyear(ret, v);
}

str
MTIMEdate_extract_dayofweek(int *ret, date *v)
{
	return date_extract_dayofweek(ret, v);
}

str
MTIMEdaytime_extract_hours(int *ret, daytime *v)
{
	return daytime_extract_hours(ret, v);
}

str
MTIMEdaytime_extract_minutes(int *ret, daytime *v)
{
	return daytime_extract_minutes(ret, v);
}

str
MTIMEdaytime_extract_seconds(int *ret, daytime *v)
{
	return daytime_extract_seconds(ret, v);
}

str
MTIMEdaytime_extract_sql_seconds(int *ret, daytime *v)
{
	return daytime_extract_sql_seconds(ret, v);
}

str
MTIMEdaytime_extract_milliseconds(int *ret, daytime *v)
{
	return daytime_extract_milliseconds(ret, v);
}

str
MTIMEtimestamp_extract_daytime(daytime *ret, timestamp *t, tzone *z)
{
	return timestamp_extract_daytime(ret, t, z);
}

str
MTIMEtimestamp_extract_daytime_default(daytime *ret, timestamp *t)
{
	return MTIMEtimestamp_extract_daytime(ret, t, &tzone_local);
}

str
MTIMEtimestamp_extract_date(date *ret, timestamp *t, tzone *z)
{
	return timestamp_extract_date(ret, t, z);
}

str
MTIMEtimestamp_extract_date_default(date *ret, timestamp *t)
{
	return MTIMEtimestamp_extract_date(ret, t, &tzone_local);
}

/* returns the date that comes a number of years after 'v' (or before
 * iff *delta < 0). */
str
MTIMEdate_addyears(date *ret, date *v, int *delta)
{
	if (*v == date_nil || *delta == int_nil) {
		*ret = date_nil;
	} else {
		int d, m, y, x, z = *delta;

		fromdate((int) *v, &d, &m, &y);
		if (m >= 3) {
			y++;
		}
		*ret = *v;
		while (z > 0) {
			x = YEARDAYS(y);
			date_adddays(ret, ret, &x);
			z--;
			y++;
		}
		while (z < 0) {
			z++;
			y--;
			x = -YEARDAYS(y);
			date_adddays(ret, ret, &x);
		}
	}
	return MAL_SUCCEED;
}

str
MTIMEdate_adddays(date *ret, date *v, int *delta)
{
	return date_adddays(ret, v, delta);
}

str
MTIMEdate_addmonths(date *ret, date *v, int *delta)
{
	return date_addmonths(ret, v, delta);
}

/* returns the number of days between 'val1' and 'val2'. */
str
MTIMEdate_diff(int *ret, date *v1, date *v2)
{
	if (*v1 == date_nil || *v2 == date_nil) {
		*ret = int_nil;
	} else {
		*ret = (int) (*v1 - *v2);
	}
	return MAL_SUCCEED;
}

str
MTIMEdate_diff_bulk(bat *ret, bat *bid1, bat *bid2)
{
	BAT *b1, *b2, *bn;
	date *t1, *t2;
	int *tn;
	BUN i, n;

	b1 = BATdescriptor(*bid1);
	b2 = BATdescriptor(*bid2);
	if (b1 == NULL || b2 == NULL) {
		if (b1)
			BBPreleaseref(b1->batCacheid);
		if (b2)
			BBPreleaseref(b2->batCacheid);
		throw(MAL, "batmtime.diff", RUNTIME_OBJECT_MISSING);
	}
	n = BATcount(b1);
	if (n != BATcount(b2)) {
		BBPreleaseref(b1->batCacheid);
		BBPreleaseref(b2->batCacheid);
		throw(MAL, "batmtime.diff", "inputs not the same size");
	}
	bn = BATnew(TYPE_void, TYPE_int, BATcount(b1));
	if (bn == NULL) {
		BBPreleaseref(b1->batCacheid);
		BBPreleaseref(b2->batCacheid);
		throw(MAL, "batmtime.diff", MAL_MALLOC_FAIL);
	}
	t1 = (date *) Tloc(b1, BUNfirst(b1));
	t2 = (date *) Tloc(b2, BUNfirst(b2));
	tn = (int *) Tloc(bn, BUNfirst(bn));
	bn->T->nonil = 1;
	bn->T->nil = 0;
	for (i = 0; i < n; i++) {
		if (*t1 == date_nil || *t2 == date_nil) {
			*tn = int_nil;
			bn->T->nonil = 0;
			bn->T->nil = 1;
		} else {
			*tn = (int) (*t1 - *t2);
		}
		t1++;
		t2++;
		tn++;
	}
	BBPreleaseref(b2->batCacheid);
	BATsetcount(bn, (BUN) (tn - (int *) Tloc(bn, BUNfirst(bn))));
	bn->tsorted = BATcount(bn) <= 1;
	bn->trevsorted = BATcount(bn) <= 1;
	if (b1->htype != bn->htype) {
		/* temporarily reuse b2 */
		b2 = VIEWcreate(b1, bn);
		BBPunfix(bn->batCacheid);
		bn = b2;
	} else {
		BATseqbase(bn, b1->hseqbase);
	}
	BBPreleaseref(b1->batCacheid);
	BBPkeepref(bn->batCacheid);
	*ret = bn->batCacheid;
	return MAL_SUCCEED;
}

/* returns the number of milliseconds between 'val1' and 'val2'. */
str
MTIMEtimestamp_diff(lng *ret, timestamp *v1, timestamp *v2)
{
	if (ts_isnil(*v1) || ts_isnil(*v2)) {
		*ret = lng_nil;
	} else {
		*ret = ((lng) (v1->days - v2->days)) * ((lng) 24 * 60 * 60 * 1000) + ((lng) (v1->msecs - v2->msecs));
	}
	return MAL_SUCCEED;
}

str
MTIMEtimestamp_diff_bulk(bat *ret, bat *bid1, bat *bid2)
{
	BAT *b1, *b2, *bn;
	timestamp *t1, *t2;
	lng *tn;
	BUN i, n;

	b1 = BATdescriptor(*bid1);
	b2 = BATdescriptor(*bid2);
	if (b1 == NULL || b2 == NULL) {
		if (b1)
			BBPreleaseref(b1->batCacheid);
		if (b2)
			BBPreleaseref(b2->batCacheid);
		throw(MAL, "batmtime.diff", RUNTIME_OBJECT_MISSING);
	}
	n = BATcount(b1);
	if (n != BATcount(b2)) {
		BBPreleaseref(b1->batCacheid);
		BBPreleaseref(b2->batCacheid);
		throw(MAL, "batmtime.diff", "inputs not the same size");
	}
	bn = BATnew(TYPE_void, TYPE_lng, BATcount(b1));
	if (bn == NULL) {
		BBPreleaseref(b1->batCacheid);
		BBPreleaseref(b2->batCacheid);
		throw(MAL, "batmtime.diff", MAL_MALLOC_FAIL);
	}
	t1 = (timestamp *) Tloc(b1, BUNfirst(b1));
	t2 = (timestamp *) Tloc(b2, BUNfirst(b2));
	tn = (lng *) Tloc(bn, BUNfirst(bn));
	bn->T->nonil = 1;
	bn->T->nil = 0;
	for (i = 0; i < n; i++) {
		if (ts_isnil(*t1) || ts_isnil(*t2)) {
			*tn = lng_nil;
			bn->T->nonil = 0;
			bn->T->nil = 1;
		} else {
			*tn = ((lng) (t1->days - t2->days)) * ((lng) 24 * 60 * 60 * 1000) + ((lng) (t1->msecs - t2->msecs));
		}
		t1++;
		t2++;
		tn++;
	}
	BBPreleaseref(b2->batCacheid);
	BATsetcount(bn, (BUN) (tn - (lng *) Tloc(bn, BUNfirst(bn))));
	bn->tsorted = BATcount(bn) <= 1;
	bn->trevsorted = BATcount(bn) <= 1;
	if (b1->htype != bn->htype) {
		/* temporarily reuse b2 */
		b2 = VIEWcreate(b1, bn);
		BBPunfix(bn->batCacheid);
		bn = b2;
	} else {
		BATseqbase(bn, b1->hseqbase);
	}
	BBPreleaseref(b1->batCacheid);
	BBPkeepref(bn->batCacheid);
	*ret = bn->batCacheid;
	return MAL_SUCCEED;
}

/* return whether DST holds in the tzone at a certain point of time. */
str
MTIMEtimestamp_inside_dst(bit *ret, timestamp *p, tzone *z)
{
	*ret = FALSE;

	if (tz_isnil(*z)) {
		*ret = bit_nil;
	} else if (z->dst) {
		timestamp tmp;

		if (timestamp_inside(&tmp, p, z, (lng) 0)) {
			*ret = TRUE;
		}
	}
	return MAL_SUCCEED;
}

str
MTIMErule_tostr(str *s, rule *r)
{
	char buf[128], *s1 = buf;
	int len = 128;

	*s1 = 0;
	rule_tostr(&s1, &len, r);
	*s = GDKstrdup(buf);
	return MAL_SUCCEED;
}

str
MTIMErule_fromstr(rule *ret, str *s)
{
	int len = 0;
	rule *d = 0;

	if (strcmp(*s, "nil") == 0) {
		ret->asint = int_nil;
		return MAL_SUCCEED;
	}
	rule_fromstr(*s, &len, &d);
	*ret = *d;
	GDKfree(d);
	return MAL_SUCCEED;
}

str
MTIMErule_create(rule *ret, int *month, int *day, int *weekday, int *minutes)
{
	return rule_create(ret, month, day, weekday, minutes);
}

str
MTIMEtzone_create_dst(tzone *ret, int *minutes, rule *start, rule *end)
{
	return tzone_create_dst(ret, minutes, start, end);
}

str
MTIMEtzone_create(tzone *ret, int *minutes)
{
	return tzone_create(ret, minutes);
}

str
MTIMEtzone_isnil(bit *retval, tzone *val)
{
	*retval = tz_isnil(*val);
	return MAL_SUCCEED;
}

/* extract month from rule. */
str
MTIMErule_extract_month(int *ret, rule *r)
{
	*ret = (r->asint == int_nil) ? int_nil : r->s.month;
	return MAL_SUCCEED;
}

/* extract day from rule. */
str
MTIMErule_extract_day(int *ret, rule *r)
{
	*ret = (r->asint == int_nil) ? int_nil : r->s.day - DAY_ZERO;
	return MAL_SUCCEED;
}

/* extract weekday from rule. */
str
MTIMErule_extract_weekday(int *ret, rule *r)
{
	*ret = (r->asint == int_nil) ? int_nil : r->s.weekday - WEEKDAY_ZERO;
	return MAL_SUCCEED;
}

/* extract minutes from rule. */
str
MTIMErule_extract_minutes(int *ret, rule *r)
{
	*ret = (r->asint == int_nil) ? int_nil : r->s.minutes;
	return MAL_SUCCEED;
}

/* extract rule that determines start of DST from tzone. */
str
MTIMEtzone_extract_start(rule *ret, tzone *t)
{
	if (tz_isnil(*t) || !t->dst) {
		ret->asint = int_nil;
	} else {
		set_rule(*ret, t->dst_start);
	}
	return MAL_SUCCEED;
}

/* extract rule that determines end of DST from tzone. */
str
MTIMEtzone_extract_end(rule *ret, tzone *t)
{
	if (tz_isnil(*t) || !t->dst) {
		ret->asint = int_nil;
	} else {
		set_rule(*ret, t->dst_end);
	}
	return MAL_SUCCEED;
}

/* extract number of minutes that tzone is offset wrt GMT. */
str
MTIMEtzone_extract_minutes(int *ret, tzone *t)
{
	*ret = (tz_isnil(*t)) ? int_nil : get_offset(t);
	return MAL_SUCCEED;
}

str
MTIMEdate_sub_sec_interval_wrap(date *ret, date *t, int *sec)
{
	if (*sec > 0) {
		int delta = -(*sec / 86400);

		return date_adddays(ret, t, &delta);
	}

	return MAL_SUCCEED;
}

str
MTIMEdate_sub_msec_interval_lng_wrap(date *ret, date *t, lng *msec)
{
	if (*msec > 0) {
		int delta = (int) -(*msec / 86400000);

		return date_adddays(ret, t, &delta);
	}

	return MAL_SUCCEED;
}

str
MTIMEdate_add_sec_interval_wrap(date *ret, date *t, int *sec)
{
	if (*sec > 0) {
		int delta = *sec / 86400;

		return date_adddays(ret, t, &delta);
	}

	return MAL_SUCCEED;
}

str
MTIMEdate_add_msec_interval_lng_wrap(date *ret, date *t, lng *msec)
{
	if (*msec > 0) {
		int delta = (int) (*msec / 86400000);

		return date_adddays(ret, t, &delta);
	}

	return MAL_SUCCEED;
}

str
MTIMEtimestamp_sub_msec_interval_lng_wrap(timestamp *ret, timestamp *t, lng *msec)
{
	lng Msec = *msec * -1;
	return MTIMEtimestamp_add(ret, t, &Msec);
}

str
MTIMEtimestamp_add_month_interval_wrap(timestamp *ret, timestamp *v, int *months)
{
	daytime t;
	date d;
	timestamp_extract_daytime(&t, v, &tzone_local);
	timestamp_extract_date(&d, v, &tzone_local);
	date_addmonths(&d, &d, months);
	return timestamp_create(ret, &d, &t, &tzone_local);
}

str
MTIMEtimestamp_sub_month_interval_wrap(timestamp *ret, timestamp *v, int *months)
{
	daytime t;
	date d;
	int m = 0 - *months;
	timestamp_extract_daytime(&t, v, &tzone_local);
	timestamp_extract_date(&d, v, &tzone_local);
	date_addmonths(&d, &d, &m);
	return timestamp_create(ret, &d, &t, &tzone_local);
}

str
MTIMEtime_add_msec_interval_wrap(daytime *ret, daytime *t, lng *mseconds)
{
	lng s = *mseconds;
	return daytime_add(ret, t, &s);
}

str
MTIMEtime_sub_msec_interval_wrap(daytime *ret, daytime *t, lng *mseconds)
{
	lng s = -1 * *mseconds;
	return daytime_add(ret, t, &s);
}

/* compute the date from a rule in a certain year. */
str
MTIMEcompute_rule_foryear(date *ret, rule *val, int *year)
{
	if (*(int *) val == int_nil || *year < YEAR_MIN || *year > YEAR_MAX) {
		*ret = date_nil;
	} else {
		*ret = compute_rule(val, *year);
	}
	return MAL_SUCCEED;
}

str
MTIMEtzone_tostr(str *s, tzone *ret)
{
	char buf[128], *s1 = buf;
	int len = 128;

	*s1 = 0;
	tzone_tostr(&s1, &len, ret);
	*s = GDKstrdup(buf);
	return MAL_SUCCEED;
}

str
MTIMEtzone_fromstr(tzone *ret, str *s)
{
	int len = 0;
	tzone *d = 0;

	if (strcmp(*s, "nil") == 0) {
		*ret = *tzone_nil;
		return MAL_SUCCEED;
	}
	if (tzone_fromstr(*s, &len, &d) == 0) {
		GDKfree(d);
		throw(MAL, "mtime.timezone", "syntax error");
	}
	*ret = *d;
	GDKfree(d);
	return MAL_SUCCEED;
}

str
MTIMEdaytime_fromstr(daytime *ret, str *s)
{
	int len = 0;
	daytime *d = 0;

	if (strcmp(*s, "nil") == 0) {
		*ret = daytime_nil;
		return MAL_SUCCEED;
	}
	if (daytime_fromstr(*s, &len, &d) == 0) {
		GDKfree(d);
		throw(MAL, "mtime.daytime", "syntax error");
	}
	*ret = *d;
	GDKfree(d);
	return MAL_SUCCEED;
}

/*
 * The utilities from Monet V4
 */
str
MTIMEmsecs(lng *ret, int *d, int *h, int *m, int *s, int *ms)
{
	if (*d == int_nil || *h == int_nil || *m == int_nil ||
		*s == int_nil || *ms == int_nil)
		*ret = lng_nil;
	else
		*ret = ((lng) *ms) + 1000 * (*s + 60 * (*m + 60 * (*h + 24 * *d)));
	return MAL_SUCCEED;
}

str
MTIMEdaytime1(daytime *ret, int *h)
{
	int m = 0, s = 0, ms = 0;

	return daytime_create(ret, h, &m, &s, &ms);
}

str
MTIMEsecs2daytime(daytime *ret, lng *s)
{
	*ret = (daytime) ((*s) * 1000);
	return MAL_SUCCEED;
}

str
MTIMEdaytime2(daytime *ret, int *h, int *m)
{
	int s = 0, ms = 0;

	return daytime_create(ret, h, m, &s, &ms);
}

str
MTIMEdaytime3(daytime *ret, int *h, int *m, int *s)
{
	int ms = 0;

	return daytime_create(ret, h, m, s, &ms);
}

str
MTIMEunix_epoch(timestamp *ret)
{
	date d0 = todate(1, 1, 1970);
	int zero = 0;
	str s = "GMT";
	daytime d1;
	tzone d2;
	str e;

	if ((e = daytime_create(&d1, &zero, &zero, &zero, &zero)) != MAL_SUCCEED)
		return e;
	if ((e = MTIMEtzone_fromstr(&d2, &s)) != MAL_SUCCEED)
		return e;
	return timestamp_create(ret, &d0, &d1, &d2);
}

str
MTIMEepoch(timestamp *ret)
{
	timestamp ts;
	lng t = ((lng) time(0)) * 1000;
	str e;

	/* convert number of seconds into a timestamp */
	if ((e = MTIMEunix_epoch(&ts)) == MAL_SUCCEED)
		e = MTIMEtimestamp_add(ret, &ts, &t);
	return e;
}

str
MTIMEepoch2int(int *ret, timestamp *t)
{
	timestamp e;
	lng v;
	str err;

	if ((err = MTIMEunix_epoch(&e)) != MAL_SUCCEED)
		return err;
	if ((err = MTIMEtimestamp_diff(&v, t, &e)) != MAL_SUCCEED)
		return err;
	if (v == lng_nil)
		*ret = int_nil;
	else
		*ret = (int) (v / 1000);
	return MAL_SUCCEED;
}

str
MTIMEtimestamp(timestamp *ret, int *sec)
{
	timestamp t;
	lng l;
	str e;

	if (*sec == int_nil) {
		*ret = *timestamp_nil;
		return MAL_SUCCEED;
	}
	if ((e = MTIMEunix_epoch(&t)) != MAL_SUCCEED)
		return e;
	l = ((lng) *sec) * 1000;
	return MTIMEtimestamp_add(ret, &t, &l);
}

str
MTIMEtimestamp_lng(timestamp *ret, lng *msec)
{
	timestamp t;
	lng l = *msec;
	str e;

	if ((e = MTIMEunix_epoch(&t)) != MAL_SUCCEED)
		return e;
	return MTIMEtimestamp_add(ret, &t, &l);
}

str
MTIMEruleDef0(rule *ret, int *m, int *d, int *w, int *h, int *mint)
{
	int d0 = 60 * *h;
	int d1 = d0 + *mint;

	return rule_create(ret, m, d, w, &d1);
}

str
MTIMEruleDef1(rule *ret, int *m, str *dnme, int *w, int *h, int *mint)
{
	int d;
	int d0 = 60 * *h;
	int d1 = d0 + *mint;
	str e;

	if ((e = day_from_str(&d, *dnme)) != MAL_SUCCEED)
		return e;
	return rule_create(ret, m, &d, w, &d1);
}

str
MTIMEruleDef2(rule *ret, int *m, str *dnme, int *w, int *mint)
{
	int d;
	str e;

	if ((e = day_from_str(&d, *dnme)) != MAL_SUCCEED)
		return e;
	return rule_create(ret, m, &d, w, mint);
}

str
MTIMEcurrent_timestamp(timestamp *t)
{
	return MTIMEepoch(t);
}

str
MTIMEcurrent_date(date *d)
{
	timestamp stamp;
	str e;

	if ((e = MTIMEcurrent_timestamp(&stamp)) != MAL_SUCCEED)
		return e;
	return MTIMEtimestamp_extract_date_default(d, &stamp);
}

str
MTIMEcurrent_time(daytime *t)
{
	timestamp stamp;
	str e;

	if ((e = MTIMEcurrent_timestamp(&stamp)) != MAL_SUCCEED)
		return e;
	return MTIMEtimestamp_extract_daytime_default(t, &stamp);
}

/* more SQL extraction utilities */
str
MTIMEtimestamp_year(int *ret, timestamp *t)
{
	date d;
	str e;

	if ((e = timestamp_extract_date(&d, t, &tzone_local)) != MAL_SUCCEED)
		return e;
	return date_extract_year(ret, &d);
}

str
MTIMEtimestamp_month(int *ret, timestamp *t)
{
	date d;
	str e;

	if ((e = timestamp_extract_date(&d, t, &tzone_local)) != MAL_SUCCEED)
		return e;
	return date_extract_month(ret, &d);
}

str
MTIMEtimestamp_day(int *ret, timestamp *t)
{
	date d;
	str e;

	if ((e = timestamp_extract_date(&d, t, &tzone_local)) != MAL_SUCCEED)
		return e;
	return date_extract_day(ret, &d);
}

str
MTIMEtimestamp_hours(int *ret, timestamp *t)
{
	daytime d;
	str e;

	if ((e = timestamp_extract_daytime(&d, t, &tzone_local)) != MAL_SUCCEED)
		return e;
	return daytime_extract_hours(ret, &d);
}

str
MTIMEtimestamp_minutes(int *ret, timestamp *t)
{
	daytime d;
	str e;

	if ((e = timestamp_extract_daytime(&d, t, &tzone_local)) != MAL_SUCCEED)
		return e;
	return daytime_extract_minutes(ret, &d);
}

str
MTIMEtimestamp_seconds(int *ret, timestamp *t)
{
	daytime d;
	str e;

	if ((e = timestamp_extract_daytime(&d, t, &tzone_local)) != MAL_SUCCEED)
		return e;
	return daytime_extract_seconds(ret, &d);
}

str
MTIMEtimestamp_sql_seconds(int *ret, timestamp *t)
{
	daytime d;
	str e;

	if ((e = timestamp_extract_daytime(&d, t, &tzone_local)) != MAL_SUCCEED)
		return e;
	return daytime_extract_sql_seconds(ret, &d);
}

str
MTIMEtimestamp_milliseconds(int *ret, timestamp *t)
{
	daytime d;
	str e;

	if ((e = timestamp_extract_daytime(&d, t, &tzone_local)) != MAL_SUCCEED)
		return e;
	return daytime_extract_milliseconds(ret, &d);
}

str
MTIMEsql_year(int *ret, int *t)
{
	if (*t == int_nil)
		*ret = int_nil;
	else
		*ret = *t / 12;
	return MAL_SUCCEED;
}

str
MTIMEsql_month(int *ret, int *t)
{
	if (*t == int_nil)
		*ret = int_nil;
	else
		*ret = *t % 12;
	return MAL_SUCCEED;
}

str
MTIMEsql_day(lng *ret, lng *t)
{
	if (*t == lng_nil)
		*ret = lng_nil;
	else
		*ret = *t / 86400000;
	return MAL_SUCCEED;
}

str
MTIMEsql_hours(int *ret, lng *t)
{
	if (*t == lng_nil)
		*ret = int_nil;
	else
		*ret = (int) ((*t % 86400000) / 3600000);
	return MAL_SUCCEED;
}

str
MTIMEsql_minutes(int *ret, lng *t)
{
	if (*t == lng_nil)
		*ret = int_nil;
	else
		*ret = (int) ((*t % 3600000) / 60000);
	return MAL_SUCCEED;
}

str
MTIMEsql_seconds(int *ret, lng *t)
{
	if (*t == lng_nil)
		*ret = int_nil;
	else
		*ret = (int) ((*t % 60000) / 1000);
	return MAL_SUCCEED;
}

/*
 * The BAT equivalents for these functions provide
 * speed.
 */

str
MTIMEmsec(lng *r)
{
#ifdef HAVE_GETTIMEOFDAY
	struct timeval tp;

	gettimeofday(&tp, NULL);
	*r = ((lng) (tp.tv_sec)) * LL_CONSTANT(1000) + (lng) tp.tv_usec / LL_CONSTANT(1000);
#else
#ifdef HAVE_FTIME
	struct timeb tb;

	ftime(&tb);
	*r = ((lng) (tb.time)) * LL_CONSTANT(1000) + ((lng) tb.millitm);
#endif
#endif
	return MAL_SUCCEED;
}

str
MTIMEdate_extract_year_bulk(int *ret, int *bid)
{
	BAT *b, *bn;
	int v;
	date d;
	BUN p, q;
	BATiter bi;

	if ((b = BATdescriptor(*bid)) == NULL)
		throw(MAL, "bbp.getdate", "Cannot access descriptor");

	bn = BATnew(TYPE_void, TYPE_int, BATcount(b));
	if (bn == NULL)
		throw(MAL, "batmtime.year", "memory allocation failure");
	BATseqbase(bn, b->H->seq);

	bi = bat_iterator(b);
	BATloop(b, p, q) {
		d = *(date *) BUNtail(bi, p);
		MTIMEdate_extract_year(&v, &d);
		if (BUNappend(bn, &v, FALSE) == NULL) {
			BBPunfix(bn->batCacheid);
			throw(MAL, "batmtime.year", "inserting value failed");
		}
	}

        if (b->htype != bn->htype) {
                BAT *r = VIEWcreate(b,bn);

                BBPreleaseref(bn->batCacheid);
                bn = r;
        }

	bn->H->nonil = b->H->nonil;
	bn->hsorted = b->hsorted;
	bn->hrevsorted = b->hrevsorted;
	BATkey(bn, BAThkey(b));
	bn->tsorted = FALSE;
	bn->trevsorted = FALSE;
	bn->T->nonil = FALSE;

	BBPkeepref(*ret = bn->batCacheid);
	BBPunfix(b->batCacheid);
	return MAL_SUCCEED;
}

str
MTIMEdate_extract_month_bulk(int *ret, int *bid)
{
	BAT *b, *bn;
	int v;
	date d;
	BUN p, q;
	BATiter bi;

	if ((b = BATdescriptor(*bid)) == NULL)
		throw(MAL, "bbp.getdate", "Cannot access descriptor");

	bn = BATnew(TYPE_void, TYPE_int, BATcount(b));
	if (bn == NULL)
		throw(MAL, "batmtime.month", "memory allocation failure");
	BATseqbase(bn, b->H->seq);

	bi = bat_iterator(b);
	BATloop(b, p, q) {
		d = *(date *) BUNtail(bi, p);
		MTIMEdate_extract_month(&v, &d);
		if (BUNappend(bn, &v, FALSE) == NULL) {
			BBPunfix(bn->batCacheid);
			throw(MAL, "batmtime.month", "inserting value failed");
		}
	}

        if (b->htype != bn->htype) {
                BAT *r = VIEWcreate(b,bn);

                BBPreleaseref(bn->batCacheid);
                bn = r;
        }

	bn->H->nonil = b->H->nonil;
	bn->hsorted = b->hsorted;
	bn->hrevsorted = b->hrevsorted;
	BATkey(bn, BAThkey(b));
	bn->tsorted = FALSE;
	bn->trevsorted = FALSE;
	bn->T->nonil = FALSE;

	BBPkeepref(*ret = bn->batCacheid);
	BBPunfix(b->batCacheid);
	return MAL_SUCCEED;
}

str
MTIMEdate_extract_day_bulk(int *ret, int *bid)
{
	BAT *b, *bn;
	int v;
	date d;
	BUN p, q;
	BATiter bi;

	if ((b = BATdescriptor(*bid)) == NULL)
		throw(MAL, "bbp.getdate", "Cannot access descriptor");

	bn = BATnew(TYPE_void, TYPE_int, BATcount(b));
	if (bn == NULL)
		throw(MAL, "batmtime.day", "memory allocation failure");
	BATseqbase(bn, b->H->seq);

	bi = bat_iterator(b);
	BATloop(b, p, q) {
		d = *(date *) BUNtail(bi, p);
		MTIMEdate_extract_day(&v, &d);
		if (BUNappend(bn, &v, FALSE) == NULL) {
			BBPunfix(bn->batCacheid);
			throw(MAL, "batmtime.day", "inserting value failed");
		}
	}

        if (b->htype != bn->htype) {
                BAT *r = VIEWcreate(b,bn);

                BBPreleaseref(bn->batCacheid);
                bn = r;
        }

	bn->H->nonil = b->H->nonil;
	bn->hsorted = b->hsorted;
	bn->hrevsorted = b->hrevsorted;
	BATkey(bn, BAThkey(b));
	bn->tsorted = FALSE;
	bn->trevsorted = FALSE;
	bn->T->nonil = FALSE;

	BBPkeepref(*ret = bn->batCacheid);
	BBPunfix(b->batCacheid);
	return MAL_SUCCEED;
}

str
MTIMEdaytime_extract_hours_bulk(int *ret, int *bid)
{
	BAT *b, *bn;
	int v;
	date d;
	BUN p, q;
	BATiter bi;

	if ((b = BATdescriptor(*bid)) == NULL)
		throw(MAL, "bbp.getdaytime", "Cannot access descriptor");

	bn = BATnew(TYPE_void, TYPE_int, BATcount(b));
	if (bn == NULL)
		throw(MAL, "batmtime.hours", "memory allocation failure");
	BATseqbase(bn, b->H->seq);

	bi = bat_iterator(b);
	BATloop(b, p, q) {
		d = *(date *) BUNtail(bi, p);
		MTIMEdaytime_extract_hours(&v, &d);
		if (BUNappend(bn, &v, FALSE) == NULL) {
			BBPunfix(bn->batCacheid);
			throw(MAL, "batmtime.hours", "inserting value failed");
		}
	}

        if (b->htype != bn->htype) {
                BAT *r = VIEWcreate(b,bn);

                BBPreleaseref(bn->batCacheid);
                bn = r;
        }

	bn->H->nonil = b->H->nonil;
	bn->hsorted = b->hsorted;
	bn->hrevsorted = b->hrevsorted;
	BATkey(bn, BAThkey(b));
	bn->tsorted = FALSE;
	bn->trevsorted = FALSE;
	bn->T->nonil = FALSE;

	BBPkeepref(*ret = bn->batCacheid);
	BBPunfix(b->batCacheid);
	return MAL_SUCCEED;
}

str
MTIMEdaytime_extract_minutes_bulk(int *ret, int *bid)
{
	BAT *b, *bn;
	int v;
	date d;
	BUN p, q;
	BATiter bi;

	if ((b = BATdescriptor(*bid)) == NULL)
		throw(MAL, "bbp.getdaytime", "Cannot access descriptor");

	bn = BATnew(TYPE_void, TYPE_int, BATcount(b));
	if (bn == NULL)
		throw(MAL, "batmtime.minutes", "memory allocation failure");
	BATseqbase(bn, b->H->seq);

	bi = bat_iterator(b);
	BATloop(b, p, q) {
		d = *(date *) BUNtail(bi, p);
		MTIMEdaytime_extract_minutes(&v, &d);
		if (BUNappend(bn, &v, FALSE) == NULL) {
			BBPunfix(bn->batCacheid);
			throw(MAL, "batmtime.minutes", "inserting value failed");
		}
	}

        if (b->htype != bn->htype) {
                BAT *r = VIEWcreate(b,bn);

                BBPreleaseref(bn->batCacheid);
                bn = r;
        }

	bn->H->nonil = b->H->nonil;
	bn->hsorted = b->hsorted;
	bn->hrevsorted = b->hrevsorted;
	BATkey(bn, BAThkey(b));
	bn->tsorted = FALSE;
	bn->trevsorted = FALSE;
	bn->T->nonil = FALSE;

	BBPkeepref(*ret = bn->batCacheid);
	BBPunfix(b->batCacheid);
	return MAL_SUCCEED;
}

str
MTIMEdaytime_extract_seconds_bulk(int *ret, int *bid)
{
	BAT *b, *bn;
	int v;
	date d;
	BUN p, q;
	BATiter bi;

	if ((b = BATdescriptor(*bid)) == NULL)
		throw(MAL, "bbp.getdaytime", "Cannot access descriptor");

	bn = BATnew(TYPE_void, TYPE_int, BATcount(b));
	if (bn == NULL)
		throw(MAL, "batmtime.seconds", "memory allocation failure");
	BATseqbase(bn, b->H->seq);

	bi = bat_iterator(b);
	BATloop(b, p, q) {
		d = *(date *) BUNtail(bi, p);
		MTIMEdaytime_extract_seconds(&v, &d);
		if (BUNappend(bn, &v, FALSE) == NULL) {
			BBPunfix(bn->batCacheid);
			throw(MAL, "batmtime.seconds", "inserting value failed");
		}
	}

        if (b->htype != bn->htype) {
                BAT *r = VIEWcreate(b,bn);

                BBPreleaseref(bn->batCacheid);
                bn = r;
        }

	bn->H->nonil = b->H->nonil;
	bn->hsorted = b->hsorted;
	bn->hrevsorted = b->hrevsorted;
	BATkey(bn, BAThkey(b));
	bn->tsorted = FALSE;
	bn->trevsorted = FALSE;
	bn->T->nonil = FALSE;

	BBPkeepref(*ret = bn->batCacheid);
	BBPunfix(b->batCacheid);
	return MAL_SUCCEED;
}

str
MTIMEdaytime_extract_sql_seconds_bulk(int *ret, int *bid)
{
	BAT *b, *bn;
	int v;
	date d;
	BUN p, q;
	BATiter bi;

	if ((b = BATdescriptor(*bid)) == NULL)
		throw(MAL, "bbp.getdaytime", "Cannot access descriptor");

	bn = BATnew(TYPE_void, TYPE_int, BATcount(b));
	if (bn == NULL)
		throw(MAL, "batmtime.sql_seconds", "memory allocation failure");
	BATseqbase(bn, b->H->seq);

	bi = bat_iterator(b);
	BATloop(b, p, q) {
		d = *(date *) BUNtail(bi, p);
		MTIMEdaytime_extract_sql_seconds(&v, &d);
		if (BUNappend(bn, &v, FALSE) == NULL) {
			BBPunfix(bn->batCacheid);
			throw(MAL, "batmtime.sql_seconds", "inserting value failed");
		}
	}

        if (b->htype != bn->htype) {
                BAT *r = VIEWcreate(b,bn);

                BBPreleaseref(bn->batCacheid);
                bn = r;
        }

	bn->H->nonil = b->H->nonil;
	bn->hsorted = b->hsorted;
	bn->hrevsorted = b->hrevsorted;
	BATkey(bn, BAThkey(b));
	bn->tsorted = FALSE;
	bn->trevsorted = FALSE;
	bn->T->nonil = FALSE;

	BBPkeepref(*ret = bn->batCacheid);
	BBPunfix(b->batCacheid);
	return MAL_SUCCEED;
}

str
MTIMEdaytime_extract_milliseconds_bulk(int *ret, int *bid)
{
	BAT *b, *bn;
	int v;
	date d;
	BUN p, q;
	BATiter bi;

	if ((b = BATdescriptor(*bid)) == NULL)
		throw(MAL, "bbp.getdaytime", "Cannot access descriptor");

	bn = BATnew(TYPE_void, TYPE_int, BATcount(b));
	if (bn == NULL)
		throw(MAL, "batmtime.milliseconds", "memory allocation failure");
	BATseqbase(bn, b->H->seq);

	bi = bat_iterator(b);
	BATloop(b, p, q) {
		d = *(date *) BUNtail(bi, p);
		MTIMEdaytime_extract_milliseconds(&v, &d);
		if (BUNappend(bn, &v, FALSE) == NULL) {
			BBPunfix(bn->batCacheid);
			throw(MAL, "batmtime.milliseconds", "inserting value failed");
		}
	}

        if (b->htype != bn->htype) {
                BAT *r = VIEWcreate(b,bn);

                BBPreleaseref(bn->batCacheid);
                bn = r;
        }

	bn->H->nonil = b->H->nonil;
	bn->hsorted = b->hsorted;
	bn->hrevsorted = b->hrevsorted;
	BATkey(bn, BAThkey(b));
	bn->tsorted = FALSE;
	bn->trevsorted = FALSE;
	bn->T->nonil = FALSE;

	BBPkeepref(*ret = bn->batCacheid);
	BBPunfix(b->batCacheid);
	return MAL_SUCCEED;
}

str
MTIMEstrptime(date *d, str *s, str *format)
{
#ifdef HAVE_STRPTIME
	struct tm t;

	if (strcmp(*s, str_nil) == 0 || strcmp(*format, str_nil) == 0) {
		*d = date_nil;
		return MAL_SUCCEED;
	}
	memset(&t, 0, sizeof(struct tm));
	if (strptime(*s, *format, &t) == NULL)
		throw(MAL, "mtime.str_to_date", "format '%s', doesn't match date '%s'\n", *format, *s);
	*d = todate(t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
	return MAL_SUCCEED;
#else
	throw(MAL, "mtime.str_to_date", "strptime support missing");
#endif
}

str
MTIMEstrftime(str *s, date *d, str *format)
{
#ifdef HAVE_STRFTIME
	struct tm t;
	char buf[BUFSIZ + 1];
	size_t sz;
	int mon, year;

	if (date_isnil(*d) || strcmp(*format, str_nil) == 0) {
		*s = GDKstrdup(str_nil);
		return MAL_SUCCEED;
	}
	memset(&t, 0, sizeof(struct tm));
	fromdate((int) *d, &t.tm_mday, &mon, &year);
	t.tm_mon = mon - 1;
	t.tm_year = year - 1900;
	if ((sz = strftime(buf, BUFSIZ, *format, &t)) == 0)
		throw(MAL, "mtime.date_to_str", "failed to convert date to string using format '%s'\n", *format);
	*s = GDKmalloc(sz + 1);
	if (*s == NULL)
		throw(MAL, "mtime.str_to_date", "memory allocation failure");
	strncpy(*s, buf, sz + 1);
	return MAL_SUCCEED;
#else
	throw(MAL, "mtime.str_to_date", "strptime support missing");
#endif
}
