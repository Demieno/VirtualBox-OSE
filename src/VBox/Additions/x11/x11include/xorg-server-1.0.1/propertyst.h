/* $Xorg: propertyst.h,v 1.4 2001/02/09 02:05:15 xorgcvs Exp $ */
/***********************************************************

Copyright 1987, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.


Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/
/* $XFree86: xc/programs/Xserver/include/propertyst.h,v 3.2 2001/01/17 22:36:57 dawes Exp $ */

#ifndef PROPERTYSTRUCT_H
#define PROPERTYSTRUCT_H 
#include "misc.h"
#include "property.h"
/* 
 *   PROPERTY -- property element
 */

typedef struct _Property {
        struct _Property       *next;
	ATOM 		propertyName;
	ATOM		type;       /* ignored by server */
	short		format;     /* format of data for swapping - 8,16,32 */
	long		size;       /* size of data in (format/8) bytes */
	pointer         data;       /* private to client */
#if defined(LBX) || defined(LBX_COMPAT)
	/*  If space is at a premium and binary compatibility is not
	 *  an issue, you may want to put the owner_pid next to format
	 *  so that the two shorts pack together without padding.
	 */
  	short		owner_pid;	/* proxy that has the data */
  	XID		tag_id;
#endif
} PropertyRec;

#endif /* PROPERTYSTRUCT_H */

