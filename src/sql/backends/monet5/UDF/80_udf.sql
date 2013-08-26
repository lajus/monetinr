/*
The contents of this file are subject to the MonetDB Public License
Version 1.1 (the "License"); you may not use this file except in
compliance with the License. You may obtain a copy of the License at
http://www.monetdb.org/Legal/MonetDBLicense

Software distributed under the License is distributed on an "AS IS"
basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
License for the specific language governing rights and limitations
under the License.

The Original Code is the MonetDB Database System.

The Initial Developer of the Original Code is CWI.
Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
Copyright August 2008-2013 MonetDB B.V.
All Rights Reserved.
*/


-- add function signatures to SQL catalog


-- Reverse a string
create function reverse(src string)
returns string external name udf.reverse;


-- fuse two (1-byte) tinyint values into one (2-byte) smallint value
create function fuse(one tinyint, two tinyint)
returns smallint external name udf.fuse;

-- fuse two (2-byte) smallint values into one (4-byte) integer value
create function fuse(one smallint, two smallint)
returns integer external name udf.fuse;

-- fuse two (4-byte) integer values into one (8-byte) bigint value
create function fuse(one integer, two integer)
returns bigint external name udf.fuse;
