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
 *  N.J. Nes, M.L. Kersten
 * The String Module
 * Strings can be created in many ways. Already in the built-in operations
 * each atom can be cast to a string using the str(atom) mil command.
 * The string module gives the possibility of construction string as a
 * substring of the a given string (s). There are two such construction functions.
 * The first is the substring from some position (offset) until the end of
 * the string. The second start again on the given offset position but only
 * copies count number of bytes. The functions fail when the position and
 * count fall out of bounds. A negative position indicates that the position is
 * computed from the end of the source string.
 *
 * The strings can be compared using the "=" and "!=" operators.
 *
 * The operator "+" concatenates a string and an atom. The atom will be
 * converted to a string using the atom to string c function. The
 * string and the result of the conversion are concatenated to form a new
 * string. This string is returned.
 *
 * The length function returns the length of the string. The length is
 * the number of characters in the string. The maximum string length
 * handled by the kernel is 32-bits long.
 *
 * chrAt() returns the character at position index in the string s. The
 * function will fail when the index is out of range. The range is
 * from 0 to length(s)-1.
 *
 * The startsWith and endsWith functions test if the string s starts with or
 * ends with the given prefix or suffix.
 *
 * The toLower and toUpper functions cast the string to lower or upper case
 * characters.
 *
 * The search(str,chr) function searches for the first occurrence of a
 * character from the begining of the string. The search(chr,str) searches
 * for the last occurrence (or first from the end of the string). The last
 * search function locates the position of first occurrence of the string s2
 * in string s. All search functions return -1 when the search failed.
 * Otherwise the position is returned.
 *
 * All string functions fail when an incorrect string (NULL pointer) is given.
 * In the current implementation, a fail is signaled by returning nil, since
 * this facilitates the use of the string module in bulk operations.
 *
 * All functions in the module have now been converted to Unicode. Internally,
 * we use UTF-8 to store strings as Unicode in zero-terminated byte-sequences.
 */
#include "monetdb_config.h"
#include "str.h"
#include <string.h>

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif
#ifdef HAVE_ICONV
#include <iconv.h>
#endif

/*
 * UTF-8 Handling
 * UTF-8 is a way to store Unicode strings in zero-terminated byte sequences, which you can e.g.
 * strcmp() with old 8-bit Latin-1 strcmp() functions and which then gives the same results as doing
 * the strcmp() on equivalent Latin-1 and ASCII character strings stored in simple one-byte sequences.
 * These characteristics make UTF-8 an attractive format for upgrading an ASCII-oriented computer
 * program towards one that supports Unicode. That is why we use UTF-8 in Monet.
 *
 * For Monet, UTF-8 mostly has no consequences, as strings stored in BATs are regarded as data,
 * and it does not matter for the database kernel whether the zero-terminated byte sequence it is
 * processing has UTF-8 or Latin-1 semantics. This module is the only place where explicit string
 * functionality is located. We {\bf do} have to adapt the behavior of the MIL length(), search(),
 * substring() and the like commands to the fact that one (Unicode) character is now stored in
 * a variable number of bytes (possibly > 1).
 *
 * One of the things that become more complex in Unicode are uppercase/lowercase conversions. The
 * below tables are the simple one-to-one Unicode case mappings. We do not support the special casing mappings
 * (e.g. from one to two letters).
 *
 * References:
 * \begin{verbatim}
 * simple casing:	http://www.unicode.org/Public/UNIDATA/UnicodeData.txt
 * complex casing: http://www.unicode.org/Public/UNIDATA/SpecialCasing.txt
 * \end{verbatim}
 *
 * The Unicode case conversion implementation in Monet fills a mapping BAT of int,int combinations,
 * in which we perform high-performance hash-lookup (all code inlined).
 */
/* This table was generated from the Unicode 5.0.0 spec.
   The table is generated by using the codes for conversion to lower
   case and for conversion to title case and upper case.
   A few code points have been moved in order to get reasonable
   conversions (if two code points are converted to the same value,
   the first one in this table wins).  The code points that have
   been interchanged are:
   U+0345 (COMBINING GREEK YPOGEGRAMMENI) / U+03B9 (GREEK SMALL LETTER IOTA) <-> U+0399 (GREEK CAPITAL LETTER IOTA)
   U+00B5 (MICRO SIGN) / U+03BC (GREEK SMALL LETTER MU) <-> U+039C (GREEK CAPITAL LETTER MU)
   U+03C2 (GREEK SMALL LETTER FINAL SIGMA) / U+03C3 (GREEK SMALL LETTER SIGMA) <-> U+3A3 (GREEK CAPITAL LETTER SIGMA)

   In addition, there are a few code points where there are different
   versions for upper case and title case.  These had to be switched
   around a little so that the mappings are done sensibly.

   The following combinations are included in this order:
   lower case <-> title case
   lower case <-  upper case
   upper case  -> title case
   The conversion title case -> upper case was removed

   The relevant code points are:
   U+01C4 (LATIN CAPITAL LETTER DZ WITH CARON)
   U+01C5 (LATIN CAPITAL LETTER D WITH SMALL LETTER Z WITH CARON)
   U+01C6 (LATIN SMALL LETTER DZ WITH CARON)
   U+01C7 (LATIN CAPITAL LETTER LJ)
   U+01C8 (LATIN CAPITAL LETTER L WITH SMALL LETTER J)
   U+01C9 (LATIN SMALL LETTER LJ)
   U+01CA (LATIN CAPITAL LETTER NJ)
   U+01CB (LATIN CAPITAL LETTER N WITH SMALL LETTER J)
   U+01CC (LATIN SMALL LETTER NJ)
   U+01F1 (LATIN CAPITAL LETTER DZ)
   U+01F2 (LATIN CAPITAL LETTER D WITH SMALL LETTER Z)
   U+01F3 (LATIN SMALL LETTER DZ)

   The script used was basically:
(cut -d\; -f1,14 UnicodeData.txt | sed -n 's/\(.*\);\(..*\)/\2;\1/p'
 cut -d\; -f1,15 UnicodeData.txt | grep -v ';$'
 cut -d\; -f1,13 UnicodeData.txt | grep -v ';$'
) | grep -v '^\([^ ]*\);\1$' | sort -t\; -u | sed 's/\(.*\);\(.*\)/{0x\1,0x\2,},/'
   with some hand munging afterward.  The data file is UnicodeData.txt
   from http://www.unicode.org/.
 */
struct UTF8_lower_upper {
	unsigned int lower, upper;
} UTF8_lower_upper[] = {
	{ 0x0061, 0x0041, },
	{ 0x0062, 0x0042, },
	{ 0x0063, 0x0043, },
	{ 0x0064, 0x0044, },
	{ 0x0065, 0x0045, },
	{ 0x0066, 0x0046, },
	{ 0x0067, 0x0047, },
	{ 0x0068, 0x0048, },
	{ 0x0069, 0x0049, },
	{ 0x0069, 0x0130, },
	{ 0x006A, 0x004A, },
	{ 0x006B, 0x004B, },
	{ 0x006B, 0x212A, },
	{ 0x006C, 0x004C, },
	{ 0x006D, 0x004D, },
	{ 0x006E, 0x004E, },
	{ 0x006F, 0x004F, },
	{ 0x0070, 0x0050, },
	{ 0x0071, 0x0051, },
	{ 0x0072, 0x0052, },
	{ 0x0073, 0x0053, },
	{ 0x0074, 0x0054, },
	{ 0x0075, 0x0055, },
	{ 0x0076, 0x0056, },
	{ 0x0077, 0x0057, },
	{ 0x0078, 0x0058, },
	{ 0x0079, 0x0059, },
	{ 0x007A, 0x005A, },
	{ 0x03BC, 0x039C, },
	{ 0x00E0, 0x00C0, },
	{ 0x00E1, 0x00C1, },
	{ 0x00E2, 0x00C2, },
	{ 0x00E3, 0x00C3, },
	{ 0x00E4, 0x00C4, },
	{ 0x00E5, 0x00C5, },
	{ 0x00E5, 0x212B, },
	{ 0x00E6, 0x00C6, },
	{ 0x00E7, 0x00C7, },
	{ 0x00E8, 0x00C8, },
	{ 0x00E9, 0x00C9, },
	{ 0x00EA, 0x00CA, },
	{ 0x00EB, 0x00CB, },
	{ 0x00EC, 0x00CC, },
	{ 0x00ED, 0x00CD, },
	{ 0x00EE, 0x00CE, },
	{ 0x00EF, 0x00CF, },
	{ 0x00F0, 0x00D0, },
	{ 0x00F1, 0x00D1, },
	{ 0x00F2, 0x00D2, },
	{ 0x00F3, 0x00D3, },
	{ 0x00F4, 0x00D4, },
	{ 0x00F5, 0x00D5, },
	{ 0x00F6, 0x00D6, },
	{ 0x00F8, 0x00D8, },
	{ 0x00F9, 0x00D9, },
	{ 0x00FA, 0x00DA, },
	{ 0x00FB, 0x00DB, },
	{ 0x00FC, 0x00DC, },
	{ 0x00FD, 0x00DD, },
	{ 0x00FE, 0x00DE, },
	{ 0x00FF, 0x0178, },
	{ 0x0101, 0x0100, },
	{ 0x0103, 0x0102, },
	{ 0x0105, 0x0104, },
	{ 0x0107, 0x0106, },
	{ 0x0109, 0x0108, },
	{ 0x010B, 0x010A, },
	{ 0x010D, 0x010C, },
	{ 0x010F, 0x010E, },
	{ 0x0111, 0x0110, },
	{ 0x0113, 0x0112, },
	{ 0x0115, 0x0114, },
	{ 0x0117, 0x0116, },
	{ 0x0119, 0x0118, },
	{ 0x011B, 0x011A, },
	{ 0x011D, 0x011C, },
	{ 0x011F, 0x011E, },
	{ 0x0121, 0x0120, },
	{ 0x0123, 0x0122, },
	{ 0x0125, 0x0124, },
	{ 0x0127, 0x0126, },
	{ 0x0129, 0x0128, },
	{ 0x012B, 0x012A, },
	{ 0x012D, 0x012C, },
	{ 0x012F, 0x012E, },
	{ 0x0131, 0x0049, },
	{ 0x0133, 0x0132, },
	{ 0x0135, 0x0134, },
	{ 0x0137, 0x0136, },
	{ 0x013A, 0x0139, },
	{ 0x013C, 0x013B, },
	{ 0x013E, 0x013D, },
	{ 0x0140, 0x013F, },
	{ 0x0142, 0x0141, },
	{ 0x0144, 0x0143, },
	{ 0x0146, 0x0145, },
	{ 0x0148, 0x0147, },
	{ 0x014B, 0x014A, },
	{ 0x014D, 0x014C, },
	{ 0x014F, 0x014E, },
	{ 0x0151, 0x0150, },
	{ 0x0153, 0x0152, },
	{ 0x0155, 0x0154, },
	{ 0x0157, 0x0156, },
	{ 0x0159, 0x0158, },
	{ 0x015B, 0x015A, },
	{ 0x015D, 0x015C, },
	{ 0x015F, 0x015E, },
	{ 0x0161, 0x0160, },
	{ 0x0163, 0x0162, },
	{ 0x0165, 0x0164, },
	{ 0x0167, 0x0166, },
	{ 0x0169, 0x0168, },
	{ 0x016B, 0x016A, },
	{ 0x016D, 0x016C, },
	{ 0x016F, 0x016E, },
	{ 0x0171, 0x0170, },
	{ 0x0173, 0x0172, },
	{ 0x0175, 0x0174, },
	{ 0x0177, 0x0176, },
	{ 0x017A, 0x0179, },
	{ 0x017C, 0x017B, },
	{ 0x017E, 0x017D, },
	{ 0x017F, 0x0053, },
	{ 0x0180, 0x0243, },
	{ 0x0183, 0x0182, },
	{ 0x0185, 0x0184, },
	{ 0x0188, 0x0187, },
	{ 0x018C, 0x018B, },
	{ 0x0192, 0x0191, },
	{ 0x0195, 0x01F6, },
	{ 0x0199, 0x0198, },
	{ 0x019A, 0x023D, },
	{ 0x019E, 0x0220, },
	{ 0x01A1, 0x01A0, },
	{ 0x01A3, 0x01A2, },
	{ 0x01A5, 0x01A4, },
	{ 0x01A8, 0x01A7, },
	{ 0x01AD, 0x01AC, },
	{ 0x01B0, 0x01AF, },
	{ 0x01B4, 0x01B3, },
	{ 0x01B6, 0x01B5, },
	{ 0x01B9, 0x01B8, },
	{ 0x01BD, 0x01BC, },
	{ 0x01BF, 0x01F7, },
	{ 0x01C6, 0x01C5, },
	{ 0x01C6, 0x01C4, },
	{ 0x01C4, 0x01C5, },
	{ 0x01C9, 0x01C8, },
	{ 0x01C9, 0x01C7, },
	{ 0x01C7, 0x01C8, },
	{ 0x01CC, 0x01CB, },
	{ 0x01CC, 0x01CA, },
	{ 0x01CA, 0x01CB, },
	{ 0x01CE, 0x01CD, },
	{ 0x01D0, 0x01CF, },
	{ 0x01D2, 0x01D1, },
	{ 0x01D4, 0x01D3, },
	{ 0x01D6, 0x01D5, },
	{ 0x01D8, 0x01D7, },
	{ 0x01DA, 0x01D9, },
	{ 0x01DC, 0x01DB, },
	{ 0x01DD, 0x018E, },
	{ 0x01DF, 0x01DE, },
	{ 0x01E1, 0x01E0, },
	{ 0x01E3, 0x01E2, },
	{ 0x01E5, 0x01E4, },
	{ 0x01E7, 0x01E6, },
	{ 0x01E9, 0x01E8, },
	{ 0x01EB, 0x01EA, },
	{ 0x01ED, 0x01EC, },
	{ 0x01EF, 0x01EE, },
	{ 0x01F3, 0x01F2, },
	{ 0x01F3, 0x01F1, },
	{ 0x01F1, 0x01F2, },
	{ 0x01F5, 0x01F4, },
	{ 0x01F9, 0x01F8, },
	{ 0x01FB, 0x01FA, },
	{ 0x01FD, 0x01FC, },
	{ 0x01FF, 0x01FE, },
	{ 0x0201, 0x0200, },
	{ 0x0203, 0x0202, },
	{ 0x0205, 0x0204, },
	{ 0x0207, 0x0206, },
	{ 0x0209, 0x0208, },
	{ 0x020B, 0x020A, },
	{ 0x020D, 0x020C, },
	{ 0x020F, 0x020E, },
	{ 0x0211, 0x0210, },
	{ 0x0213, 0x0212, },
	{ 0x0215, 0x0214, },
	{ 0x0217, 0x0216, },
	{ 0x0219, 0x0218, },
	{ 0x021B, 0x021A, },
	{ 0x021D, 0x021C, },
	{ 0x021F, 0x021E, },
	{ 0x0223, 0x0222, },
	{ 0x0225, 0x0224, },
	{ 0x0227, 0x0226, },
	{ 0x0229, 0x0228, },
	{ 0x022B, 0x022A, },
	{ 0x022D, 0x022C, },
	{ 0x022F, 0x022E, },
	{ 0x0231, 0x0230, },
	{ 0x0233, 0x0232, },
	{ 0x023C, 0x023B, },
	{ 0x0242, 0x0241, },
	{ 0x0247, 0x0246, },
	{ 0x0249, 0x0248, },
	{ 0x024B, 0x024A, },
	{ 0x024D, 0x024C, },
	{ 0x024F, 0x024E, },
	{ 0x0253, 0x0181, },
	{ 0x0254, 0x0186, },
	{ 0x0256, 0x0189, },
	{ 0x0257, 0x018A, },
	{ 0x0259, 0x018F, },
	{ 0x025B, 0x0190, },
	{ 0x0260, 0x0193, },
	{ 0x0263, 0x0194, },
	{ 0x0268, 0x0197, },
	{ 0x0269, 0x0196, },
	{ 0x026B, 0x2C62, },
	{ 0x026F, 0x019C, },
	{ 0x0272, 0x019D, },
	{ 0x0275, 0x019F, },
	{ 0x027D, 0x2C64, },
	{ 0x0280, 0x01A6, },
	{ 0x0283, 0x01A9, },
	{ 0x0288, 0x01AE, },
	{ 0x0289, 0x0244, },
	{ 0x028A, 0x01B1, },
	{ 0x028B, 0x01B2, },
	{ 0x028C, 0x0245, },
	{ 0x0292, 0x01B7, },
	{ 0x03B9, 0x0399, },
	{ 0x037B, 0x03FD, },
	{ 0x037C, 0x03FE, },
	{ 0x037D, 0x03FF, },
	{ 0x03AC, 0x0386, },
	{ 0x03AD, 0x0388, },
	{ 0x03AE, 0x0389, },
	{ 0x03AF, 0x038A, },
	{ 0x03B1, 0x0391, },
	{ 0x03B2, 0x0392, },
	{ 0x03B3, 0x0393, },
	{ 0x03B4, 0x0394, },
	{ 0x03B5, 0x0395, },
	{ 0x03B6, 0x0396, },
	{ 0x03B7, 0x0397, },
	{ 0x03B8, 0x0398, },
	{ 0x03B8, 0x03F4, },
	{ 0x0345, 0x0399, },
	{ 0x03BA, 0x039A, },
	{ 0x03BB, 0x039B, },
	{ 0x00B5, 0x039C, },
	{ 0x03BD, 0x039D, },
	{ 0x03BE, 0x039E, },
	{ 0x03BF, 0x039F, },
	{ 0x03C0, 0x03A0, },
	{ 0x03C1, 0x03A1, },
	{ 0x03C3, 0x03A3, },
	{ 0x03C2, 0x03A3, },
	{ 0x03C4, 0x03A4, },
	{ 0x03C5, 0x03A5, },
	{ 0x03C6, 0x03A6, },
	{ 0x03C7, 0x03A7, },
	{ 0x03C8, 0x03A8, },
	{ 0x03C9, 0x03A9, },
	{ 0x03C9, 0x2126, },
	{ 0x03CA, 0x03AA, },
	{ 0x03CB, 0x03AB, },
	{ 0x03CC, 0x038C, },
	{ 0x03CD, 0x038E, },
	{ 0x03CE, 0x038F, },
	{ 0x03D0, 0x0392, },
	{ 0x03D1, 0x0398, },
	{ 0x03D5, 0x03A6, },
	{ 0x03D6, 0x03A0, },
	{ 0x03D9, 0x03D8, },
	{ 0x03DB, 0x03DA, },
	{ 0x03DD, 0x03DC, },
	{ 0x03DF, 0x03DE, },
	{ 0x03E1, 0x03E0, },
	{ 0x03E3, 0x03E2, },
	{ 0x03E5, 0x03E4, },
	{ 0x03E7, 0x03E6, },
	{ 0x03E9, 0x03E8, },
	{ 0x03EB, 0x03EA, },
	{ 0x03ED, 0x03EC, },
	{ 0x03EF, 0x03EE, },
	{ 0x03F0, 0x039A, },
	{ 0x03F1, 0x03A1, },
	{ 0x03F2, 0x03F9, },
	{ 0x03F5, 0x0395, },
	{ 0x03F8, 0x03F7, },
	{ 0x03FB, 0x03FA, },
	{ 0x0430, 0x0410, },
	{ 0x0431, 0x0411, },
	{ 0x0432, 0x0412, },
	{ 0x0433, 0x0413, },
	{ 0x0434, 0x0414, },
	{ 0x0435, 0x0415, },
	{ 0x0436, 0x0416, },
	{ 0x0437, 0x0417, },
	{ 0x0438, 0x0418, },
	{ 0x0439, 0x0419, },
	{ 0x043A, 0x041A, },
	{ 0x043B, 0x041B, },
	{ 0x043C, 0x041C, },
	{ 0x043D, 0x041D, },
	{ 0x043E, 0x041E, },
	{ 0x043F, 0x041F, },
	{ 0x0440, 0x0420, },
	{ 0x0441, 0x0421, },
	{ 0x0442, 0x0422, },
	{ 0x0443, 0x0423, },
	{ 0x0444, 0x0424, },
	{ 0x0445, 0x0425, },
	{ 0x0446, 0x0426, },
	{ 0x0447, 0x0427, },
	{ 0x0448, 0x0428, },
	{ 0x0449, 0x0429, },
	{ 0x044A, 0x042A, },
	{ 0x044B, 0x042B, },
	{ 0x044C, 0x042C, },
	{ 0x044D, 0x042D, },
	{ 0x044E, 0x042E, },
	{ 0x044F, 0x042F, },
	{ 0x0450, 0x0400, },
	{ 0x0451, 0x0401, },
	{ 0x0452, 0x0402, },
	{ 0x0453, 0x0403, },
	{ 0x0454, 0x0404, },
	{ 0x0455, 0x0405, },
	{ 0x0456, 0x0406, },
	{ 0x0457, 0x0407, },
	{ 0x0458, 0x0408, },
	{ 0x0459, 0x0409, },
	{ 0x045A, 0x040A, },
	{ 0x045B, 0x040B, },
	{ 0x045C, 0x040C, },
	{ 0x045D, 0x040D, },
	{ 0x045E, 0x040E, },
	{ 0x045F, 0x040F, },
	{ 0x0461, 0x0460, },
	{ 0x0463, 0x0462, },
	{ 0x0465, 0x0464, },
	{ 0x0467, 0x0466, },
	{ 0x0469, 0x0468, },
	{ 0x046B, 0x046A, },
	{ 0x046D, 0x046C, },
	{ 0x046F, 0x046E, },
	{ 0x0471, 0x0470, },
	{ 0x0473, 0x0472, },
	{ 0x0475, 0x0474, },
	{ 0x0477, 0x0476, },
	{ 0x0479, 0x0478, },
	{ 0x047B, 0x047A, },
	{ 0x047D, 0x047C, },
	{ 0x047F, 0x047E, },
	{ 0x0481, 0x0480, },
	{ 0x048B, 0x048A, },
	{ 0x048D, 0x048C, },
	{ 0x048F, 0x048E, },
	{ 0x0491, 0x0490, },
	{ 0x0493, 0x0492, },
	{ 0x0495, 0x0494, },
	{ 0x0497, 0x0496, },
	{ 0x0499, 0x0498, },
	{ 0x049B, 0x049A, },
	{ 0x049D, 0x049C, },
	{ 0x049F, 0x049E, },
	{ 0x04A1, 0x04A0, },
	{ 0x04A3, 0x04A2, },
	{ 0x04A5, 0x04A4, },
	{ 0x04A7, 0x04A6, },
	{ 0x04A9, 0x04A8, },
	{ 0x04AB, 0x04AA, },
	{ 0x04AD, 0x04AC, },
	{ 0x04AF, 0x04AE, },
	{ 0x04B1, 0x04B0, },
	{ 0x04B3, 0x04B2, },
	{ 0x04B5, 0x04B4, },
	{ 0x04B7, 0x04B6, },
	{ 0x04B9, 0x04B8, },
	{ 0x04BB, 0x04BA, },
	{ 0x04BD, 0x04BC, },
	{ 0x04BF, 0x04BE, },
	{ 0x04C2, 0x04C1, },
	{ 0x04C4, 0x04C3, },
	{ 0x04C6, 0x04C5, },
	{ 0x04C8, 0x04C7, },
	{ 0x04CA, 0x04C9, },
	{ 0x04CC, 0x04CB, },
	{ 0x04CE, 0x04CD, },
	{ 0x04CF, 0x04C0, },
	{ 0x04D1, 0x04D0, },
	{ 0x04D3, 0x04D2, },
	{ 0x04D5, 0x04D4, },
	{ 0x04D7, 0x04D6, },
	{ 0x04D9, 0x04D8, },
	{ 0x04DB, 0x04DA, },
	{ 0x04DD, 0x04DC, },
	{ 0x04DF, 0x04DE, },
	{ 0x04E1, 0x04E0, },
	{ 0x04E3, 0x04E2, },
	{ 0x04E5, 0x04E4, },
	{ 0x04E7, 0x04E6, },
	{ 0x04E9, 0x04E8, },
	{ 0x04EB, 0x04EA, },
	{ 0x04ED, 0x04EC, },
	{ 0x04EF, 0x04EE, },
	{ 0x04F1, 0x04F0, },
	{ 0x04F3, 0x04F2, },
	{ 0x04F5, 0x04F4, },
	{ 0x04F7, 0x04F6, },
	{ 0x04F9, 0x04F8, },
	{ 0x04FB, 0x04FA, },
	{ 0x04FD, 0x04FC, },
	{ 0x04FF, 0x04FE, },
	{ 0x0501, 0x0500, },
	{ 0x0503, 0x0502, },
	{ 0x0505, 0x0504, },
	{ 0x0507, 0x0506, },
	{ 0x0509, 0x0508, },
	{ 0x050B, 0x050A, },
	{ 0x050D, 0x050C, },
	{ 0x050F, 0x050E, },
	{ 0x0511, 0x0510, },
	{ 0x0513, 0x0512, },
	{ 0x0561, 0x0531, },
	{ 0x0562, 0x0532, },
	{ 0x0563, 0x0533, },
	{ 0x0564, 0x0534, },
	{ 0x0565, 0x0535, },
	{ 0x0566, 0x0536, },
	{ 0x0567, 0x0537, },
	{ 0x0568, 0x0538, },
	{ 0x0569, 0x0539, },
	{ 0x056A, 0x053A, },
	{ 0x056B, 0x053B, },
	{ 0x056C, 0x053C, },
	{ 0x056D, 0x053D, },
	{ 0x056E, 0x053E, },
	{ 0x056F, 0x053F, },
	{ 0x0570, 0x0540, },
	{ 0x0571, 0x0541, },
	{ 0x0572, 0x0542, },
	{ 0x0573, 0x0543, },
	{ 0x0574, 0x0544, },
	{ 0x0575, 0x0545, },
	{ 0x0576, 0x0546, },
	{ 0x0577, 0x0547, },
	{ 0x0578, 0x0548, },
	{ 0x0579, 0x0549, },
	{ 0x057A, 0x054A, },
	{ 0x057B, 0x054B, },
	{ 0x057C, 0x054C, },
	{ 0x057D, 0x054D, },
	{ 0x057E, 0x054E, },
	{ 0x057F, 0x054F, },
	{ 0x0580, 0x0550, },
	{ 0x0581, 0x0551, },
	{ 0x0582, 0x0552, },
	{ 0x0583, 0x0553, },
	{ 0x0584, 0x0554, },
	{ 0x0585, 0x0555, },
	{ 0x0586, 0x0556, },
	{ 0x1D7D, 0x2C63, },
	{ 0x1E01, 0x1E00, },
	{ 0x1E03, 0x1E02, },
	{ 0x1E05, 0x1E04, },
	{ 0x1E07, 0x1E06, },
	{ 0x1E09, 0x1E08, },
	{ 0x1E0B, 0x1E0A, },
	{ 0x1E0D, 0x1E0C, },
	{ 0x1E0F, 0x1E0E, },
	{ 0x1E11, 0x1E10, },
	{ 0x1E13, 0x1E12, },
	{ 0x1E15, 0x1E14, },
	{ 0x1E17, 0x1E16, },
	{ 0x1E19, 0x1E18, },
	{ 0x1E1B, 0x1E1A, },
	{ 0x1E1D, 0x1E1C, },
	{ 0x1E1F, 0x1E1E, },
	{ 0x1E21, 0x1E20, },
	{ 0x1E23, 0x1E22, },
	{ 0x1E25, 0x1E24, },
	{ 0x1E27, 0x1E26, },
	{ 0x1E29, 0x1E28, },
	{ 0x1E2B, 0x1E2A, },
	{ 0x1E2D, 0x1E2C, },
	{ 0x1E2F, 0x1E2E, },
	{ 0x1E31, 0x1E30, },
	{ 0x1E33, 0x1E32, },
	{ 0x1E35, 0x1E34, },
	{ 0x1E37, 0x1E36, },
	{ 0x1E39, 0x1E38, },
	{ 0x1E3B, 0x1E3A, },
	{ 0x1E3D, 0x1E3C, },
	{ 0x1E3F, 0x1E3E, },
	{ 0x1E41, 0x1E40, },
	{ 0x1E43, 0x1E42, },
	{ 0x1E45, 0x1E44, },
	{ 0x1E47, 0x1E46, },
	{ 0x1E49, 0x1E48, },
	{ 0x1E4B, 0x1E4A, },
	{ 0x1E4D, 0x1E4C, },
	{ 0x1E4F, 0x1E4E, },
	{ 0x1E51, 0x1E50, },
	{ 0x1E53, 0x1E52, },
	{ 0x1E55, 0x1E54, },
	{ 0x1E57, 0x1E56, },
	{ 0x1E59, 0x1E58, },
	{ 0x1E5B, 0x1E5A, },
	{ 0x1E5D, 0x1E5C, },
	{ 0x1E5F, 0x1E5E, },
	{ 0x1E61, 0x1E60, },
	{ 0x1E63, 0x1E62, },
	{ 0x1E65, 0x1E64, },
	{ 0x1E67, 0x1E66, },
	{ 0x1E69, 0x1E68, },
	{ 0x1E6B, 0x1E6A, },
	{ 0x1E6D, 0x1E6C, },
	{ 0x1E6F, 0x1E6E, },
	{ 0x1E71, 0x1E70, },
	{ 0x1E73, 0x1E72, },
	{ 0x1E75, 0x1E74, },
	{ 0x1E77, 0x1E76, },
	{ 0x1E79, 0x1E78, },
	{ 0x1E7B, 0x1E7A, },
	{ 0x1E7D, 0x1E7C, },
	{ 0x1E7F, 0x1E7E, },
	{ 0x1E81, 0x1E80, },
	{ 0x1E83, 0x1E82, },
	{ 0x1E85, 0x1E84, },
	{ 0x1E87, 0x1E86, },
	{ 0x1E89, 0x1E88, },
	{ 0x1E8B, 0x1E8A, },
	{ 0x1E8D, 0x1E8C, },
	{ 0x1E8F, 0x1E8E, },
	{ 0x1E91, 0x1E90, },
	{ 0x1E93, 0x1E92, },
	{ 0x1E95, 0x1E94, },
	{ 0x1E9B, 0x1E60, },
	{ 0x1EA1, 0x1EA0, },
	{ 0x1EA3, 0x1EA2, },
	{ 0x1EA5, 0x1EA4, },
	{ 0x1EA7, 0x1EA6, },
	{ 0x1EA9, 0x1EA8, },
	{ 0x1EAB, 0x1EAA, },
	{ 0x1EAD, 0x1EAC, },
	{ 0x1EAF, 0x1EAE, },
	{ 0x1EB1, 0x1EB0, },
	{ 0x1EB3, 0x1EB2, },
	{ 0x1EB5, 0x1EB4, },
	{ 0x1EB7, 0x1EB6, },
	{ 0x1EB9, 0x1EB8, },
	{ 0x1EBB, 0x1EBA, },
	{ 0x1EBD, 0x1EBC, },
	{ 0x1EBF, 0x1EBE, },
	{ 0x1EC1, 0x1EC0, },
	{ 0x1EC3, 0x1EC2, },
	{ 0x1EC5, 0x1EC4, },
	{ 0x1EC7, 0x1EC6, },
	{ 0x1EC9, 0x1EC8, },
	{ 0x1ECB, 0x1ECA, },
	{ 0x1ECD, 0x1ECC, },
	{ 0x1ECF, 0x1ECE, },
	{ 0x1ED1, 0x1ED0, },
	{ 0x1ED3, 0x1ED2, },
	{ 0x1ED5, 0x1ED4, },
	{ 0x1ED7, 0x1ED6, },
	{ 0x1ED9, 0x1ED8, },
	{ 0x1EDB, 0x1EDA, },
	{ 0x1EDD, 0x1EDC, },
	{ 0x1EDF, 0x1EDE, },
	{ 0x1EE1, 0x1EE0, },
	{ 0x1EE3, 0x1EE2, },
	{ 0x1EE5, 0x1EE4, },
	{ 0x1EE7, 0x1EE6, },
	{ 0x1EE9, 0x1EE8, },
	{ 0x1EEB, 0x1EEA, },
	{ 0x1EED, 0x1EEC, },
	{ 0x1EEF, 0x1EEE, },
	{ 0x1EF1, 0x1EF0, },
	{ 0x1EF3, 0x1EF2, },
	{ 0x1EF5, 0x1EF4, },
	{ 0x1EF7, 0x1EF6, },
	{ 0x1EF9, 0x1EF8, },
	{ 0x1F00, 0x1F08, },
	{ 0x1F01, 0x1F09, },
	{ 0x1F02, 0x1F0A, },
	{ 0x1F03, 0x1F0B, },
	{ 0x1F04, 0x1F0C, },
	{ 0x1F05, 0x1F0D, },
	{ 0x1F06, 0x1F0E, },
	{ 0x1F07, 0x1F0F, },
	{ 0x1F10, 0x1F18, },
	{ 0x1F11, 0x1F19, },
	{ 0x1F12, 0x1F1A, },
	{ 0x1F13, 0x1F1B, },
	{ 0x1F14, 0x1F1C, },
	{ 0x1F15, 0x1F1D, },
	{ 0x1F20, 0x1F28, },
	{ 0x1F21, 0x1F29, },
	{ 0x1F22, 0x1F2A, },
	{ 0x1F23, 0x1F2B, },
	{ 0x1F24, 0x1F2C, },
	{ 0x1F25, 0x1F2D, },
	{ 0x1F26, 0x1F2E, },
	{ 0x1F27, 0x1F2F, },
	{ 0x1F30, 0x1F38, },
	{ 0x1F31, 0x1F39, },
	{ 0x1F32, 0x1F3A, },
	{ 0x1F33, 0x1F3B, },
	{ 0x1F34, 0x1F3C, },
	{ 0x1F35, 0x1F3D, },
	{ 0x1F36, 0x1F3E, },
	{ 0x1F37, 0x1F3F, },
	{ 0x1F40, 0x1F48, },
	{ 0x1F41, 0x1F49, },
	{ 0x1F42, 0x1F4A, },
	{ 0x1F43, 0x1F4B, },
	{ 0x1F44, 0x1F4C, },
	{ 0x1F45, 0x1F4D, },
	{ 0x1F51, 0x1F59, },
	{ 0x1F53, 0x1F5B, },
	{ 0x1F55, 0x1F5D, },
	{ 0x1F57, 0x1F5F, },
	{ 0x1F60, 0x1F68, },
	{ 0x1F61, 0x1F69, },
	{ 0x1F62, 0x1F6A, },
	{ 0x1F63, 0x1F6B, },
	{ 0x1F64, 0x1F6C, },
	{ 0x1F65, 0x1F6D, },
	{ 0x1F66, 0x1F6E, },
	{ 0x1F67, 0x1F6F, },
	{ 0x1F70, 0x1FBA, },
	{ 0x1F71, 0x1FBB, },
	{ 0x1F72, 0x1FC8, },
	{ 0x1F73, 0x1FC9, },
	{ 0x1F74, 0x1FCA, },
	{ 0x1F75, 0x1FCB, },
	{ 0x1F76, 0x1FDA, },
	{ 0x1F77, 0x1FDB, },
	{ 0x1F78, 0x1FF8, },
	{ 0x1F79, 0x1FF9, },
	{ 0x1F7A, 0x1FEA, },
	{ 0x1F7B, 0x1FEB, },
	{ 0x1F7C, 0x1FFA, },
	{ 0x1F7D, 0x1FFB, },
	{ 0x1F80, 0x1F88, },
	{ 0x1F81, 0x1F89, },
	{ 0x1F82, 0x1F8A, },
	{ 0x1F83, 0x1F8B, },
	{ 0x1F84, 0x1F8C, },
	{ 0x1F85, 0x1F8D, },
	{ 0x1F86, 0x1F8E, },
	{ 0x1F87, 0x1F8F, },
	{ 0x1F90, 0x1F98, },
	{ 0x1F91, 0x1F99, },
	{ 0x1F92, 0x1F9A, },
	{ 0x1F93, 0x1F9B, },
	{ 0x1F94, 0x1F9C, },
	{ 0x1F95, 0x1F9D, },
	{ 0x1F96, 0x1F9E, },
	{ 0x1F97, 0x1F9F, },
	{ 0x1FA0, 0x1FA8, },
	{ 0x1FA1, 0x1FA9, },
	{ 0x1FA2, 0x1FAA, },
	{ 0x1FA3, 0x1FAB, },
	{ 0x1FA4, 0x1FAC, },
	{ 0x1FA5, 0x1FAD, },
	{ 0x1FA6, 0x1FAE, },
	{ 0x1FA7, 0x1FAF, },
	{ 0x1FB0, 0x1FB8, },
	{ 0x1FB1, 0x1FB9, },
	{ 0x1FB3, 0x1FBC, },
	{ 0x1FBE, 0x0399, },
	{ 0x1FC3, 0x1FCC, },
	{ 0x1FD0, 0x1FD8, },
	{ 0x1FD1, 0x1FD9, },
	{ 0x1FE0, 0x1FE8, },
	{ 0x1FE1, 0x1FE9, },
	{ 0x1FE5, 0x1FEC, },
	{ 0x1FF3, 0x1FFC, },
	{ 0x214E, 0x2132, },
	{ 0x2170, 0x2160, },
	{ 0x2171, 0x2161, },
	{ 0x2172, 0x2162, },
	{ 0x2173, 0x2163, },
	{ 0x2174, 0x2164, },
	{ 0x2175, 0x2165, },
	{ 0x2176, 0x2166, },
	{ 0x2177, 0x2167, },
	{ 0x2178, 0x2168, },
	{ 0x2179, 0x2169, },
	{ 0x217A, 0x216A, },
	{ 0x217B, 0x216B, },
	{ 0x217C, 0x216C, },
	{ 0x217D, 0x216D, },
	{ 0x217E, 0x216E, },
	{ 0x217F, 0x216F, },
	{ 0x2184, 0x2183, },
	{ 0x24D0, 0x24B6, },
	{ 0x24D1, 0x24B7, },
	{ 0x24D2, 0x24B8, },
	{ 0x24D3, 0x24B9, },
	{ 0x24D4, 0x24BA, },
	{ 0x24D5, 0x24BB, },
	{ 0x24D6, 0x24BC, },
	{ 0x24D7, 0x24BD, },
	{ 0x24D8, 0x24BE, },
	{ 0x24D9, 0x24BF, },
	{ 0x24DA, 0x24C0, },
	{ 0x24DB, 0x24C1, },
	{ 0x24DC, 0x24C2, },
	{ 0x24DD, 0x24C3, },
	{ 0x24DE, 0x24C4, },
	{ 0x24DF, 0x24C5, },
	{ 0x24E0, 0x24C6, },
	{ 0x24E1, 0x24C7, },
	{ 0x24E2, 0x24C8, },
	{ 0x24E3, 0x24C9, },
	{ 0x24E4, 0x24CA, },
	{ 0x24E5, 0x24CB, },
	{ 0x24E6, 0x24CC, },
	{ 0x24E7, 0x24CD, },
	{ 0x24E8, 0x24CE, },
	{ 0x24E9, 0x24CF, },
	{ 0x2C30, 0x2C00, },
	{ 0x2C31, 0x2C01, },
	{ 0x2C32, 0x2C02, },
	{ 0x2C33, 0x2C03, },
	{ 0x2C34, 0x2C04, },
	{ 0x2C35, 0x2C05, },
	{ 0x2C36, 0x2C06, },
	{ 0x2C37, 0x2C07, },
	{ 0x2C38, 0x2C08, },
	{ 0x2C39, 0x2C09, },
	{ 0x2C3A, 0x2C0A, },
	{ 0x2C3B, 0x2C0B, },
	{ 0x2C3C, 0x2C0C, },
	{ 0x2C3D, 0x2C0D, },
	{ 0x2C3E, 0x2C0E, },
	{ 0x2C3F, 0x2C0F, },
	{ 0x2C40, 0x2C10, },
	{ 0x2C41, 0x2C11, },
	{ 0x2C42, 0x2C12, },
	{ 0x2C43, 0x2C13, },
	{ 0x2C44, 0x2C14, },
	{ 0x2C45, 0x2C15, },
	{ 0x2C46, 0x2C16, },
	{ 0x2C47, 0x2C17, },
	{ 0x2C48, 0x2C18, },
	{ 0x2C49, 0x2C19, },
	{ 0x2C4A, 0x2C1A, },
	{ 0x2C4B, 0x2C1B, },
	{ 0x2C4C, 0x2C1C, },
	{ 0x2C4D, 0x2C1D, },
	{ 0x2C4E, 0x2C1E, },
	{ 0x2C4F, 0x2C1F, },
	{ 0x2C50, 0x2C20, },
	{ 0x2C51, 0x2C21, },
	{ 0x2C52, 0x2C22, },
	{ 0x2C53, 0x2C23, },
	{ 0x2C54, 0x2C24, },
	{ 0x2C55, 0x2C25, },
	{ 0x2C56, 0x2C26, },
	{ 0x2C57, 0x2C27, },
	{ 0x2C58, 0x2C28, },
	{ 0x2C59, 0x2C29, },
	{ 0x2C5A, 0x2C2A, },
	{ 0x2C5B, 0x2C2B, },
	{ 0x2C5C, 0x2C2C, },
	{ 0x2C5D, 0x2C2D, },
	{ 0x2C5E, 0x2C2E, },
	{ 0x2C61, 0x2C60, },
	{ 0x2C65, 0x023A, },
	{ 0x2C66, 0x023E, },
	{ 0x2C68, 0x2C67, },
	{ 0x2C6A, 0x2C69, },
	{ 0x2C6C, 0x2C6B, },
	{ 0x2C76, 0x2C75, },
	{ 0x2C81, 0x2C80, },
	{ 0x2C83, 0x2C82, },
	{ 0x2C85, 0x2C84, },
	{ 0x2C87, 0x2C86, },
	{ 0x2C89, 0x2C88, },
	{ 0x2C8B, 0x2C8A, },
	{ 0x2C8D, 0x2C8C, },
	{ 0x2C8F, 0x2C8E, },
	{ 0x2C91, 0x2C90, },
	{ 0x2C93, 0x2C92, },
	{ 0x2C95, 0x2C94, },
	{ 0x2C97, 0x2C96, },
	{ 0x2C99, 0x2C98, },
	{ 0x2C9B, 0x2C9A, },
	{ 0x2C9D, 0x2C9C, },
	{ 0x2C9F, 0x2C9E, },
	{ 0x2CA1, 0x2CA0, },
	{ 0x2CA3, 0x2CA2, },
	{ 0x2CA5, 0x2CA4, },
	{ 0x2CA7, 0x2CA6, },
	{ 0x2CA9, 0x2CA8, },
	{ 0x2CAB, 0x2CAA, },
	{ 0x2CAD, 0x2CAC, },
	{ 0x2CAF, 0x2CAE, },
	{ 0x2CB1, 0x2CB0, },
	{ 0x2CB3, 0x2CB2, },
	{ 0x2CB5, 0x2CB4, },
	{ 0x2CB7, 0x2CB6, },
	{ 0x2CB9, 0x2CB8, },
	{ 0x2CBB, 0x2CBA, },
	{ 0x2CBD, 0x2CBC, },
	{ 0x2CBF, 0x2CBE, },
	{ 0x2CC1, 0x2CC0, },
	{ 0x2CC3, 0x2CC2, },
	{ 0x2CC5, 0x2CC4, },
	{ 0x2CC7, 0x2CC6, },
	{ 0x2CC9, 0x2CC8, },
	{ 0x2CCB, 0x2CCA, },
	{ 0x2CCD, 0x2CCC, },
	{ 0x2CCF, 0x2CCE, },
	{ 0x2CD1, 0x2CD0, },
	{ 0x2CD3, 0x2CD2, },
	{ 0x2CD5, 0x2CD4, },
	{ 0x2CD7, 0x2CD6, },
	{ 0x2CD9, 0x2CD8, },
	{ 0x2CDB, 0x2CDA, },
	{ 0x2CDD, 0x2CDC, },
	{ 0x2CDF, 0x2CDE, },
	{ 0x2CE1, 0x2CE0, },
	{ 0x2CE3, 0x2CE2, },
	{ 0x2D00, 0x10A0, },
	{ 0x2D01, 0x10A1, },
	{ 0x2D02, 0x10A2, },
	{ 0x2D03, 0x10A3, },
	{ 0x2D04, 0x10A4, },
	{ 0x2D05, 0x10A5, },
	{ 0x2D06, 0x10A6, },
	{ 0x2D07, 0x10A7, },
	{ 0x2D08, 0x10A8, },
	{ 0x2D09, 0x10A9, },
	{ 0x2D0A, 0x10AA, },
	{ 0x2D0B, 0x10AB, },
	{ 0x2D0C, 0x10AC, },
	{ 0x2D0D, 0x10AD, },
	{ 0x2D0E, 0x10AE, },
	{ 0x2D0F, 0x10AF, },
	{ 0x2D10, 0x10B0, },
	{ 0x2D11, 0x10B1, },
	{ 0x2D12, 0x10B2, },
	{ 0x2D13, 0x10B3, },
	{ 0x2D14, 0x10B4, },
	{ 0x2D15, 0x10B5, },
	{ 0x2D16, 0x10B6, },
	{ 0x2D17, 0x10B7, },
	{ 0x2D18, 0x10B8, },
	{ 0x2D19, 0x10B9, },
	{ 0x2D1A, 0x10BA, },
	{ 0x2D1B, 0x10BB, },
	{ 0x2D1C, 0x10BC, },
	{ 0x2D1D, 0x10BD, },
	{ 0x2D1E, 0x10BE, },
	{ 0x2D1F, 0x10BF, },
	{ 0x2D20, 0x10C0, },
	{ 0x2D21, 0x10C1, },
	{ 0x2D22, 0x10C2, },
	{ 0x2D23, 0x10C3, },
	{ 0x2D24, 0x10C4, },
	{ 0x2D25, 0x10C5, },
	{ 0xFF41, 0xFF21, },
	{ 0xFF42, 0xFF22, },
	{ 0xFF43, 0xFF23, },
	{ 0xFF44, 0xFF24, },
	{ 0xFF45, 0xFF25, },
	{ 0xFF46, 0xFF26, },
	{ 0xFF47, 0xFF27, },
	{ 0xFF48, 0xFF28, },
	{ 0xFF49, 0xFF29, },
	{ 0xFF4A, 0xFF2A, },
	{ 0xFF4B, 0xFF2B, },
	{ 0xFF4C, 0xFF2C, },
	{ 0xFF4D, 0xFF2D, },
	{ 0xFF4E, 0xFF2E, },
	{ 0xFF4F, 0xFF2F, },
	{ 0xFF50, 0xFF30, },
	{ 0xFF51, 0xFF31, },
	{ 0xFF52, 0xFF32, },
	{ 0xFF53, 0xFF33, },
	{ 0xFF54, 0xFF34, },
	{ 0xFF55, 0xFF35, },
	{ 0xFF56, 0xFF36, },
	{ 0xFF57, 0xFF37, },
	{ 0xFF58, 0xFF38, },
	{ 0xFF59, 0xFF39, },
	{ 0xFF5A, 0xFF3A, },
	{ 0x10428, 0x10400, },
	{ 0x10429, 0x10401, },
	{ 0x1042A, 0x10402, },
	{ 0x1042B, 0x10403, },
	{ 0x1042C, 0x10404, },
	{ 0x1042D, 0x10405, },
	{ 0x1042E, 0x10406, },
	{ 0x1042F, 0x10407, },
	{ 0x10430, 0x10408, },
	{ 0x10431, 0x10409, },
	{ 0x10432, 0x1040A, },
	{ 0x10433, 0x1040B, },
	{ 0x10434, 0x1040C, },
	{ 0x10435, 0x1040D, },
	{ 0x10436, 0x1040E, },
	{ 0x10437, 0x1040F, },
	{ 0x10438, 0x10410, },
	{ 0x10439, 0x10411, },
	{ 0x1043A, 0x10412, },
	{ 0x1043B, 0x10413, },
	{ 0x1043C, 0x10414, },
	{ 0x1043D, 0x10415, },
	{ 0x1043E, 0x10416, },
	{ 0x1043F, 0x10417, },
	{ 0x10440, 0x10418, },
	{ 0x10441, 0x10419, },
	{ 0x10442, 0x1041A, },
	{ 0x10443, 0x1041B, },
	{ 0x10444, 0x1041C, },
	{ 0x10445, 0x1041D, },
	{ 0x10446, 0x1041E, },
	{ 0x10447, 0x1041F, },
	{ 0x10448, 0x10420, },
	{ 0x10449, 0x10421, },
	{ 0x1044A, 0x10422, },
	{ 0x1044B, 0x10423, },
	{ 0x1044C, 0x10424, },
	{ 0x1044D, 0x10425, },
	{ 0x1044E, 0x10426, },
	{ 0x1044F, 0x10427, },
};

#define UTF8_CONVERSIONS (sizeof(UTF8_lower_upper) / sizeof(UTF8_lower_upper[0]))

static BAT *UTF8_toupperBat = NULL, *UTF8_tolowerBat;

bat *
strPrelude(void)
{
	if (!UTF8_toupperBat) {
		int i = UTF8_CONVERSIONS;

		UTF8_toupperBat = BATnew(TYPE_int, TYPE_int, UTF8_CONVERSIONS);
		if (UTF8_toupperBat == NULL)
			return NULL;
		while (--i >= 0) {
			int lower = UTF8_lower_upper[i].lower;
			int upper = UTF8_lower_upper[i].upper;

			BUNins(UTF8_toupperBat, &lower, &upper, FALSE);
		}
		UTF8_tolowerBat = BATmirror(UTF8_toupperBat);
		BATname(UTF8_toupperBat, "monet_unicode_case");
	}
	return NULL;
}

str
strEpilogue(void)
{
	if (UTF8_toupperBat)
		BBPunfix(UTF8_toupperBat->batCacheid);
	return MAL_SUCCEED;
}

#define UTF8_GETCHAR(X1,X2)\
	if (*X2 < 0x80) {\
		(X1) = *(X2)++;\
	} else if (*(X2) < 0xE0) {\
		(X1)  = (*(X2)++ & 0x1F) << 6;\
		(X1) |= (*(X2)++ & 0x3F);\
	} else if (*(X2) < 0xF0) {\
		(X1)  = (*(X2)++ & 0x0F) << 12;\
		(X1) |= (*(X2)++ & 0x3F) << 6;\
		(X1) |= (*(X2)++ & 0x3F);\
	} else if (*X2 < 0xF8) {\
		(X1)  = (*(X2)++ & 0x07) << 18;\
		(X1) |= (*(X2)++ & 0x3F) << 12;\
		(X1) |= (*(X2)++ & 0x3F) << 6;\
		(X1) |= (*(X2)++ & 0x3F);\
	} else if (*X2 < 0xFC) {\
		(X1)  = (*(X2)++ & 0x03) << 24;\
		(X1) |= (*(X2)++ & 0x3F) << 18;\
		(X1) |= (*(X2)++ & 0x3F) << 12;\
		(X1) |= (*(X2)++ & 0x3F) << 6;\
		(X1) |= (*(X2)++ & 0x3F);\
	} else if (*X2 < 0xFE) {\
		(X1)  = (*(X2)++ & 0x01) << 30;\
		(X1) |= (*(X2)++ & 0x3F) << 24;\
		(X1) |= (*(X2)++ & 0x3F) << 18;\
		(X1) |= (*(X2)++ & 0x3F) << 12;\
		(X1) |= (*(X2)++ & 0x3F) << 6;\
		(X1) |= (*(X2)++ & 0x3F);\
	} else {\
		(X1) = int_nil;\
	}

#define UTF8_PUTCHAR(X1,X2)\
	if ((X1) < 0 || (SIZEOF_INT > 4 && (int) (X1) >= 0x80000000)) {\
		*(X2)++ = '\200';\
	} else if ((X1) < 0x80) {\
		*(X2)++ = (X1);\
	} else if ((X1) < 0x800) {\
		*(X2)++ = 0xC0 | ((X1) >> 6);\
		*(X2)++ = 0x80 | ((X1) & 0x3F);\
	} else if ((X1) < 0x10000) {\
		*(X2)++ = 0xE0 | ((X1) >> 12);\
		*(X2)++ = 0x80 | (((X1) >> 6) & 0x3F);\
		*(X2)++ = 0x80 | ((X1) & 0x3F);\
	} else if ((X1) < 0x200000) {\
		*(X2)++ = 0xF0 | ((X1) >> 18);\
		*(X2)++ = 0x80 | (((X1) >> 12) & 0x3F);\
		*(X2)++ = 0x80 | (((X1) >> 6) & 0x3F);\
		*(X2)++ = 0x80 | ((X1) & 0x3F);\
	} else if ((X1) < 0x4000000) {\
		*(X2)++ = 0xF8 | ((X1) >> 24);\
		*(X2)++ = 0x80 | (((X1) >> 18) & 0x3F);\
		*(X2)++ = 0x80 | (((X1) >> 12) & 0x3F);\
		*(X2)++ = 0x80 | (((X1) >> 6) & 0x3F);\
		*(X2)++ = 0x80 | ((X1) & 0x3F);\
	} else /* if ((X1) < 0x80000000) */ {\
		*(X2)++ = 0xFC | ((X1) >> 30);\
		*(X2)++ = 0x80 | (((X1) >> 24) & 0x3F);\
		*(X2)++ = 0x80 | (((X1) >> 18) & 0x3F);\
		*(X2)++ = 0x80 | (((X1) >> 12) & 0x3F);\
		*(X2)++ = 0x80 | (((X1) >> 6) & 0x3F);\
		*(X2)++ = 0x80 | ((X1) & 0x3F);\
	}

static inline int
UTF8_strlen(str val)
{
	unsigned char *s = (unsigned char *) val;
	int pos = 0;

	while (*s) {
		int c = *s++;

		pos++;
		if (c < 0xC0)
			continue;
		if (*s++ < 0x80)
			return int_nil;
		if (c < 0xE0)
			continue;
		if (*s++ < 0x80)
			return int_nil;
		if (c < 0xF0)
			continue;
		if (*s++ < 0x80)
			return int_nil;
		if (c < 0xF8)
			continue;
		if (*s++ < 0x80)
			return int_nil;
		if (c < 0xFC)
			continue;
		if (*s++ < 0x80)
			return int_nil;
	}
	return pos;
}

static inline int
UTF8_strpos(str val, str end)
{
	unsigned char *s = (unsigned char *) val;
	int pos = 0;

	if (s > (unsigned char *) end) {
		return -1;
	}
	while (s < (unsigned char *) end) {
		int c = *s++;

		pos++;
		if (c == 0)
			return -1;
		if (c < 0xC0)
			continue;
		if (*s++ < 0x80)
			return -1;
		if (c < 0xE0)
			continue;
		if (*s++ < 0x80)
			return -1;
		if (c < 0xF0)
			continue;
		if (*s++ < 0x80)
			return -1;
		if (c < 0xF8)
			continue;
		if (*s++ < 0x80)
			return -1;
		if (c < 0xFC)
			continue;
		if (*s++ < 0x80)
			return -1;
	}
	return pos;
}

static inline str
UTF8_strtail(str val, int pos)
{
	unsigned char *s = (unsigned char *) val;

	while (*s && pos-- > 0) {
		int c = *s++;

		if (c < 0xC0)
			continue;
		if (*s++ < 0x80)
			return NULL;
		if (c < 0xE0)
			continue;
		if (*s++ < 0x80)
			return NULL;
		if (c < 0xF0)
			continue;
		if (*s++ < 0x80)
			return NULL;
		if (c < 0xF8)
			continue;
		if (*s++ < 0x80)
			return NULL;
		if (c < 0xFC)
			continue;
		if (*s++ < 0x80)
			return NULL;
	}
	return (str) s;
}

#define RETURN_NIL_IF(b,t)						\
	if (b) {							\
		if (ATOMextern(t)) {					\
			*(ptr*) res = (ptr) ATOMnil(t);			\
		} else {						\
			memcpy(res, ATOMnilptr(t), ATOMsize(t));	\
 		}							\
		return GDK_SUCCEED;					\
	}

#ifdef MAX
#undef MAX
#endif
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#ifdef MIN
#undef MIN
#endif
#define MIN(x, y) ((x) < (y) ? (x) : (y))

int
strConcat(str *res, str s, ptr val, int t)
{
	str valstr = NULL;
	size_t l1;
	int l2 = 0;

	RETURN_NIL_IF(strNil(s) || ATOMcmp(t, val, ATOMnilptr(t)) == 0, TYPE_str);
	if (t <= 0)
		return GDK_FAIL;
	l1 = strlen(s);
	if (t != TYPE_str) {
		BATatoms[t].atomToStr(&valstr, &l2, val);
		val = (ptr) valstr;
	} else
		l2 = (int) strlen((str) val);

	if (* (str) val == '\200' || *s == '\200')
		*res = GDKstrdup(str_nil);
	else {
		if (l1+l2+1 >= INT_MAX) {
			if (valstr && (str) valstr != str_nil)
				GDKfree(valstr);
			return GDK_FAIL;
		}
		*res = (str) GDKmalloc((int) (l1 + l2 + 1));
		memcpy(*res, s, l1);
		memcpy(*res + l1, (str) val, l2);
		(*res)[l1 + l2] = '\0';
	}
	if (valstr && (str) valstr != str_nil)
		GDKfree(valstr);
	return GDK_SUCCEED;
}

int
strLength(int *res, str s)
{
	size_t l;
	RETURN_NIL_IF(strNil(s), TYPE_int);
	l=  UTF8_strlen(s);
	assert(l <INT_MAX);
	*res = (int) l;
	return GDK_SUCCEED;
}

int
strBytes(int *res, str s)
{
	size_t l;
	l= strlen(s);
	assert(l <INT_MAX);
	*res = (int) l;
	return GDK_SUCCEED;
}

int
strTail(str *res, str s, int *offset)
{
	int off = *offset;

	RETURN_NIL_IF(strNil(s) || off == int_nil, TYPE_str);
	if (off < 0) {
		size_t l = UTF8_strlen(s);
		int len= (int) l;
		assert(l < INT_MAX);

		RETURN_NIL_IF(len == int_nil, TYPE_str);
		off = len + off;
		if (off < 0)
			off = 0;
	}
	*res = (char *) GDKstrdup(UTF8_strtail(s, off));
	return GDK_SUCCEED;
}

int
strSubString(str *res, str s, int *offset, int *length)
{
	int len, off = *offset, l = *length;

	RETURN_NIL_IF(strNil(s) || off == int_nil || l == int_nil, TYPE_str);
	if (off < 0) {
		len = UTF8_strlen(s);
		RETURN_NIL_IF(len == int_nil, TYPE_str);
		off = len + off;
		if (off < 0) {
			l += off;
			off = 0;
		}
	}
	/* here, off >= 0 */
	if (l < 0) {
		*res = GDKstrdup("");
		return GDK_SUCCEED;
	}
	s = UTF8_strtail(s, off);
	len = (int)(UTF8_strtail(s, l) - s);
	*res = (char *) GDKmalloc(len + 1);
	strncpy(*res, s, len);
	(*res)[len] = 0;
	return GDK_SUCCEED;
}

int
strFromWChr(str *res, int *c)
{
	str s = *res = GDKmalloc(7);

	UTF8_PUTCHAR(*c,s);
	*s = 0;
	return GDK_SUCCEED;
}


int
strWChrAt(int *res, str val, int *at)
{
/* 64bit: should have wrd arg */
	unsigned char *s = (unsigned char *) val;

	RETURN_NIL_IF(strNil(val) || *at == int_nil || *at < 0, TYPE_int);
	s = (unsigned char *) UTF8_strtail((str) s, *at);
	RETURN_NIL_IF(*s == 0, TYPE_int);
	UTF8_GETCHAR(*res,s);
	return GDK_SUCCEED;
}

int
codeset(str *res)
{
#ifdef HAVE_NL_LANGINFO
	char *code_set = nl_langinfo(CODESET);

	if (!code_set)
		return GDK_FAIL;
	*res = GDKstrdup(code_set);
	return GDK_SUCCEED;
#else
	*res = GDKstrdup("UTF-8");
	return GDK_SUCCEED;
#endif
}

int
strIconv(str *res, str org, str f, str t)
{
#ifdef HAVE_ICONV
	size_t len = strlen(org);
	iconv_t cd = iconv_open(t, f);
	size_t size = 4 * len;	/* make sure enough memory is claimed */
	char *r;
	ICONV_CONST char *from = org;

	if (cd == (iconv_t)(-1)) {
		GDKerror("strIconv: Cannot convert strings from (%s) to (%s)\n", f, t);
		return GDK_FAIL;
	}
	*res = r = GDKmalloc(size);
	if (iconv(cd, &from, &len, &r, &size) == (size_t) - 1) {
		GDKfree(*res);
		*res = NULL;
		GDKerror("strIconv: String conversion failed from (%s) to (%s)\n", f, t);
		return GDK_FAIL;
	}
	*r = 0;
	iconv_close(cd);
	return GDK_SUCCEED;
#else
	*res = NULL;
	if (strcmp(f, t) == 0) {
		*res = GDKstrdup(org);
		return GDK_SUCCEED;
	}
	return GDK_FAIL;
#endif
}

int
strPrefix(bit *res, str s, str prefix)
{
	size_t pl, i;

	RETURN_NIL_IF(strNil(s) || strNil(prefix), TYPE_bit);
	pl = strlen(prefix);
	if (strlen(s) < pl) {
		*res = 0;
		return GDK_SUCCEED;
	}
	*res = 1;
	for (i = 0; i < pl; i++) {
		if (s[i] != prefix[i]) {
			*res = 0;
			return GDK_SUCCEED;
		}
	}
	return GDK_SUCCEED;
}

int
strSuffix(bit *res, str s, str suffix)
{
	size_t i, sl, sul;

	RETURN_NIL_IF(strNil(s) || strNil(suffix), TYPE_bit);
	sl = strlen(s);
	sul = strlen(suffix);

	if (sl < sul) {
		*res = 0;
		return GDK_SUCCEED;
	}
	*res = 1;
	for (i = 0; i < sul; i++) {
		if (s[sl - 1 - i] != suffix[sul - 1 - i]) {
			*res = 0;
			return GDK_SUCCEED;
		}
	}
	return GDK_SUCCEED;
}

int
strLower(str *res, str s)
{
	BATiter UTF8_tolowerBati = bat_iterator(UTF8_tolowerBat);
	size_t len = strlen(s);
	unsigned char *dst, *src = (unsigned char *) s, *end = (unsigned char *) (src + len);

	RETURN_NIL_IF(strNil(s), TYPE_str);
	*res = GDKmalloc(len + 1);
	dst = (unsigned char *) *res;
	while (src < end) {
		int c;

		UTF8_GETCHAR(c,src);
		{
			BUN UTF8_CONV_r;
			int UTF8_CONV_v = (c);
			HASHfnd_int(UTF8_CONV_r, UTF8_tolowerBati, &UTF8_CONV_v);
			if (UTF8_CONV_r != BUN_NONE)
				(c) = *(int*) BUNtloc(UTF8_tolowerBati, UTF8_CONV_r);
		}
		if (dst + 6 > (unsigned char *) *res + len) {
			/* not guaranteed to fit, so allocate more space;
			   also allocate enough for the rest of the source */
			size_t off = dst - (unsigned char *) *res;

			*res = GDKrealloc(*res, (len += 6 + (end - src)) + 1);
			dst = (unsigned char *) *res + off;
		}
		UTF8_PUTCHAR(c,dst);
	}
	*dst = 0;
	return GDK_SUCCEED;
}

int
strUpper(str *res, str s)
{
	BATiter UTF8_toupperBati = bat_iterator(UTF8_toupperBat);
	size_t len = strlen(s);
	unsigned char *dst, *src = (unsigned char *) s, *end = (unsigned char *) (src + len);

	RETURN_NIL_IF(strNil(s), TYPE_str);
	*res = GDKmalloc(len + 1);
	dst = (unsigned char *) *res;
	while (src < end) {
		int c;

		UTF8_GETCHAR(c,src);
		{
			BUN UTF8_CONV_r;
			int UTF8_CONV_v = (c);
			HASHfnd_int(UTF8_CONV_r, UTF8_toupperBati, &UTF8_CONV_v);
			if (UTF8_CONV_r != BUN_NONE)
				(c) = *(int*) BUNtloc(UTF8_toupperBati, UTF8_CONV_r);
		}
		if (dst + 6 > (unsigned char *) *res + len) {
			/* not guaranteed to fit, so allocate more space;
			   also allocate enough for the rest of the source */
			size_t off = dst - (unsigned char *) *res;

			*res = GDKrealloc(*res, (len += 6 + (end - src)) + 1);
			dst = (unsigned char *) *res + off;
		}
		UTF8_PUTCHAR(c,dst);
	}
	*dst = 0;
	return GDK_SUCCEED;
}

int
strStrSearch(int *res, str s, str s2)
{
/* 64bit: should return wrd */
	char *p;

	RETURN_NIL_IF(strNil(s) || strNil(s2), TYPE_int);
	if ((p = strstr(s, s2)) != 0)
		*res = UTF8_strpos(s, p);
	else
		*res = -1;
	return GDK_SUCCEED;
}

int
strReverseStrSearch(int *res, str s, str s2)
{
/* 64bit: should return wrd */
	size_t len, slen;
	char *p, *q;
	size_t i;

	RETURN_NIL_IF(strNil(s) || strNil(s2), TYPE_int);
	*res = -1;
	len = strlen(s);
	slen = strlen(s2);
	for (p = s + len - slen; p >= s; p--) {
		for (i = 0, q = p; i < slen && *q == s2[i]; i++, q++)
			;
		if (i == slen) {
			*res = UTF8_strpos(s, p);
			break;
		}
	}

	return GDK_SUCCEED;
}

int
strStrip(str *res, str s)
{
	str start = s;
	size_t len;

	while (GDKisspace(*start))
		start++;

	/* Remove the trailing spaces.  Make sure not to pass the start */
	/* pointer in case a string only contains spaces.		*/
	s = start + strlen(start);
	while (s > start && GDKisspace(*(s - 1)))
		s--;

	len = s - start + 1;
	*res = GDKmalloc(len);
	memcpy(*res, start, len - 1);
	(*res)[len - 1] = '\0';
	return GDK_SUCCEED;
}

int
strLtrim(str *res, str s)
{
	RETURN_NIL_IF(strNil(s), TYPE_str);
	while (GDKisspace(*s))
		s++;
	*res = GDKstrdup(s);
	return GDK_SUCCEED;
}

int
strRtrim(str *res, str s)
{
	size_t len = strlen(s);

	RETURN_NIL_IF(strNil(s), TYPE_str);
	while (len > 0 && GDKisspace(s[len - 1]))
		len--;
	*res = GDKmalloc(len + 1);
	memcpy(*res, s, len);
	(*res)[len] = '\0';
	return GDK_SUCCEED;
}

int
strSubstitute(str *res, str s, str src, str dst, bit *g)
{
	int repeat = *g;
	size_t lsrc = (src?strlen(src):0), ldst = (dst?strlen(dst):0);
	size_t l = (s?strLen(s):0), n = l + ldst;
	str buf, fnd, end;

	if (repeat && ldst > lsrc && lsrc) {
		n = (ldst * l) / lsrc;	/* max length */
	}
	buf = *res = (str) GDKmalloc(n);
	end = buf + l;
	fnd = buf;
	strcpy(buf, s);
	if (!lsrc)
		return GDK_SUCCEED;
	do {
		fnd = strstr((fnd < buf) ? buf : fnd, src);
		if (!fnd)
			break;
		memmove(fnd + ldst, fnd + lsrc, end - fnd);
		memcpy(fnd, dst, ldst);
		end += ldst - lsrc;
		fnd += ldst;
	} while (repeat);

	return GDK_SUCCEED;
}

int
strSQLLength(int *res, str s)
{
	str r = NULL;
	strRtrim(&r, s);
	strLength(res, r);
	GDKfree(r);
	return GDK_SUCCEED;
}

/*
 * Here you find the wrappers around the version 4 library code
 * It also contains the direct implementation of the string
 * matching support routines.
 */
#include "mal_exception.h"

str
STRfindUnescapedOccurrence(str b, str c, str esc){
	str t;

	t= strstr(b,c);
	while( t){
		/* check for escaped version */
		if (t>b && *esc == *(t-1) ) {
			t= strstr(t+1,c);
		} else return t;
	}
	return 0;
}
/*
 * The SQL like function return a boolean
 */
int
STRlike(str s, str pat, str esc){
	str t,p;

	t= s;
	for( p= pat; *p && *t; p++){
		if(esc && *p == *esc) {
			p++;
			if( *p != *t) return FALSE;
			t++;
		} else
		if( *p == '_') t++;
		else
		if( *p == '%'){
			p++;
			while(*p == '%') p++;
			if( *p == 0) return TRUE; /* tail is acceptable */
			for(; *p && *t; t++)
				if( STRlike(t,p,esc))
					return TRUE;
			if( *p == 0 && *t == 0) return TRUE;
			return FALSE;
		} else
		if( *p == *t) t++;
		else return FALSE;
	}
	if( *p == '%' && *(p+1)==0) return TRUE;
	return *t == 0 && *p == 0;
}

str
STRlikewrap(bit *ret, str *s, str *pat, str *esc){
	*ret = STRlike(*s,*pat,*esc);
	return MAL_SUCCEED;
}
str
STRlikewrap2(bit *ret, str *s, str *pat){
	*ret = STRlike(*s,*pat,0);
	return MAL_SUCCEED;
}

str
STRtostr(str *res, str *src)
{
	if( *src == 0)
		*res= GDKstrdup(str_nil);
	else *res = GDKstrdup(*src);
	return MAL_SUCCEED;
}

/*
 * The concatenate operator requires a type in most cases.
 */
str
STRConcat(str *res, str *val1, str *val2)
{
	if (strConcat(res, *val1, *val2, TYPE_str) == GDK_FAIL)
		throw(MAL, "str.concat", "Allocation failed");
	return MAL_SUCCEED;
}

str
STRLength(int *res, str *arg1)
{
	strLength(res, *arg1);
	return MAL_SUCCEED;
}

str
STRBytes(int *res, str *arg1)
{
	strBytes(res, *arg1);
	return MAL_SUCCEED;
}

str
STRTail(str *res, str *arg1, int *offset)
{
	strTail(res, *arg1, offset);
	return MAL_SUCCEED;
}

str
STRSubString(str *res, str *arg1, int *offset, int *length)
{
	strSubString(res, *arg1, offset, length);
	return MAL_SUCCEED;
}

str
STRFromWChr(str *res, int *at)
{
	strFromWChr(res, at);
	return MAL_SUCCEED;
}

str
STRWChrAt(int *res, str *arg1, int *at)
{
	strWChrAt(res, *arg1, at);
	return MAL_SUCCEED;
}

str
STRcodeset(str *res)
{
	codeset(res);
	return MAL_SUCCEED;
}

str
STRIconv(str *res, str *o, str *fp, str *tp)
{
	strIconv(res, *o, *fp, *tp);
	return MAL_SUCCEED;
}

str
STRPrefix(bit *res, str *arg1, str *arg2)
{
	strPrefix(res, *arg1, *arg2);
	return MAL_SUCCEED;
}

str
STRSuffix(bit *res, str *arg1, str *arg2)
{
	strSuffix(res, *arg1, *arg2);
	return MAL_SUCCEED;
}

str
STRLower(str *res, str *arg1)
{
	strLower(res, *arg1);
	return MAL_SUCCEED;
}

str
STRUpper(str *res, str *arg1)
{
	strUpper(res, *arg1);
	return MAL_SUCCEED;
}

str
STRstrSearch(int *res, str *arg1, str *arg2)
{
	strStrSearch(res, *arg1, *arg2);
	return MAL_SUCCEED;
}

str
STRReverseStrSearch(int *res, str *arg1, str *arg2)
{
	strReverseStrSearch(res, *arg1, *arg2);
	return MAL_SUCCEED;
}

str
STRStrip(str *res, str *arg1)
{
	strStrip(res, *arg1);
	return MAL_SUCCEED;
}

str
STRLtrim(str *res, str *arg1)
{
	strLtrim(res, *arg1);
	return MAL_SUCCEED;
}


str
STRmax(str *res, str *left, str *right){
	if (strcmp(*left, str_nil) == 0 ||
		strcmp(*right, str_nil) == 0 )
		*res = GDKstrdup(str_nil);
	else
	if (strcmp(*left,*right)< 0 )
		*res = GDKstrdup(*right);
	else
		*res = GDKstrdup(*left);
	return MAL_SUCCEED;
}

str
STRmax_no_nil(str *res, str *left, str *right){
	if (strcmp(*left, str_nil) == 0)
		*res = GDKstrdup(*right);
	else
	if (strcmp(*right, str_nil) == 0)
		*res = GDKstrdup(*left);
	else
	if (strcmp(*left,*right)< 0 )
		*res = GDKstrdup(*right);
	else
		*res = GDKstrdup(*left);
	return MAL_SUCCEED;
}

str
STRmin(str *res, str *left, str *right){
	if (strcmp(*left, str_nil) == 0 ||
		strcmp(*right, str_nil) == 0 )
		*res = GDKstrdup(str_nil);
	else
	if (strcmp(*left,*right)< 0 )
		*res = GDKstrdup(*left);
	else
		*res = GDKstrdup(*right);
	return MAL_SUCCEED;
}
str
STRmin_no_nil(str *res, str *left, str *right){
	if (strcmp(*left, str_nil) == 0)
		*res = GDKstrdup(*right);
	else
	if (strcmp(*right, str_nil) == 0)
		*res = GDKstrdup(*left);
	else
	if (strcmp(*left,*right)< 0 )
		*res = GDKstrdup(*left);
	else
		*res = GDKstrdup(*right);
	return MAL_SUCCEED;
}

str
STRRtrim(str *res, str *arg1)
{
	strRtrim(res, *arg1);
	return MAL_SUCCEED;
}

str
STRSubstitute(str *res, str *arg1, str *arg2, str *arg3, bit *g)
{
	strSubstitute(res, *arg1, *arg2, *arg3, g);
	return MAL_SUCCEED;
}

/*
 * A few old MIL procs implementations
 */
str
STRascii(int *ret, str *s){
	int offset=0;
	return STRWChrAt(ret,s,&offset);
}

str
STRsubstringTail(str *ret, str *s, int *start)
{
	int offset= *start;
	if( offset <1) offset =1;
	offset--;
	return STRTail(ret, s, &offset);
}

str
STRsubstring(str *ret, str *s, int *start, int *l)
{
	int offset= *start;
	if( offset <1) offset =1;
	offset--;
	return STRSubString(ret, s, &offset, l);
}
str
STRprefix(str *ret, str *s, int *l){
	int start =0;
	return STRSubString(ret,s,&start,l);
}
str
STRsuffix(str *ret, str *s, int *l){
	int start = (int) (strlen(*s)- *l);
	return STRSubString(ret,s,&start,l);
}
str
STRlocate(int *ret, str *s1, str *s2){
	int p;
	strStrSearch(&p, *s2, *s1);
	*ret=  p>=0? p+1:0;
	return MAL_SUCCEED;
}
str
STRlocate2(int *ret, str *s1, str *s2, int *start){
	int p;
	str dummy;
	strTail(&dummy, *s1, start);
	strStrSearch(&p, *s2, dummy);
	if( dummy) GDKfree(dummy);
	*ret=  p>=0? p+1:0;
	return MAL_SUCCEED;
}

str
STRinsert(str *ret, str *s, int *start, int *l, str *s2){
	str v;
	if(strcmp(*s2,str_nil) ==0 || strcmp(*s,str_nil)==0 )
		*ret = GDKstrdup( (str)str_nil);
	else {
		if( *start <0) *start =1;
		if(strlen(*s)+strlen(*s2)+1 >= INT_MAX) {
			throw(MAL, "str.insert", "Allocation failed");
		}
		v= *ret = GDKmalloc((int)strlen(*s)+(int)strlen(*s2)+1 );
		strncpy(v, *s,*start);
		v[*start]=0;
		strcat(v,*s2);
		if( *start + *l < (int) strlen(*s))
			strcat(v,*s + *start + *l);
	}
	return MAL_SUCCEED;
}

str
STRreplace(str *ret, str *s1, str *s2, str *s3){
	bit flag= TRUE;
	return STRSubstitute(ret,s1,s2,s3,&flag);
}

str
STRrepeat(str *ret, str *s, int *c)
{
	str t;
	int i;
	size_t l;

	if (*c < 0 || strcmp(*s, str_nil) == 0) {
		*ret = GDKstrdup(str_nil);
	} else {
		l = strlen(*s);
		if (l >= INT_MAX)
			throw(MAL, "str.repeat", "Allocation failed");
		t = *ret = GDKmalloc( *c * l + 1);

		if (!t)
			throw(MAL, "str.repeat", "Allocation failed");
		*t = 0;
		for(i = *c; i>0; i--, t += l)
			strcpy(t, *s);
	}
	return MAL_SUCCEED;
}
str
STRspace(str *ret, int *l){
	char buf[]= " ", *s= buf;
	return STRrepeat(ret,&s,l);
}

str
STRstringLength(int *res, str *s)
{
	str r = NULL;
	strRtrim(&r, *s);
	strLength(res, r);
	GDKfree(r);
	return MAL_SUCCEED;
}


