// SPDX-FileCopyrightText: 1999 Alan DeKok <aland@ox.org>
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef RZ_HASH_MD5_H
#define RZ_HASH_MD5_H

#include <rz_types.h>

#define RZ_HASH_MD5_DIGEST_SIZE  0x10
#define RZ_HASH_MD5_BLOCK_LENGTH 0x40

/*
 * md5.h        Structures and prototypes for md5.
 *
 * Version:     $Id$
 * License:		LGPL, but largely derived from a public domain source.
 *
 */

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include <string.h>
/*
 *  FreeRADIUS defines to ensure globally unique MD5 function names,
 *  so that we don't pick up vendor-specific broken MD5 libraries.
 */
#define MD5_CTX      librad_MD5_CTX
#define MD5Init      librad_MD5Init
#define MD5Update    librad_MD5Update
#define MD5Final     librad_MD5Final
#define MD5Transform librad_MD5Transform

/*  The below was retrieved from
 *  http://www.openbsd.org/cgi-bin/cvsweb/~checkout~/src/sys/crypto/md5.h?rev=1.1
 *  With the following changes: uint64_t => ut32[2]
 *  Commented out #include <sys/cdefs.h>
 *  Commented out the __BEGIN and __END _DECLS, and the __attributes.
 */

/*
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 */

#define MD5_BLOCK_LENGTH  64
#define MD5_DIGEST_LENGTH 16

typedef struct MD5Context {
	ut32 state[4]; /* state */
	ut32 count[2]; /* number of bits, mod 2^64 */
	ut8 buffer[MD5_BLOCK_LENGTH]; /* input buffer */
} MD5_CTX;

/* include <sys/cdefs.h> */

/* __BEGIN_DECLS */
void MD5Init(MD5_CTX *);
void MD5Update(MD5_CTX *, const ut8 *, size_t)
	/*		__attribute__((__bounded__(__string__,2,3)))*/;
void MD5Final(ut8[MD5_DIGEST_LENGTH], MD5_CTX *)
	/*		__attribute__((__bounded__(__minbytes__,1,MD5_DIGEST_LENGTH)))*/;
void MD5Transform(ut32[4], const ut8[MD5_BLOCK_LENGTH])
	/*		__attribute__((__bounded__(__minbytes__,1,4)))*/
	/*		__attribute__((__bounded__(__minbytes__,2,MD5_BLOCK_LENGTH)))*/;
/* __END_DECLS */

#endif /* RZ_HASH_MD5_H */
