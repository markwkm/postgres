/*-------------------------------------------------------------------------
 *
 * httplistener.h
 *    Exports from httpd/httplistener.c.
 *
 * Portions Copyright (c) 2012, PostgreSQL Global Development Group
 *
 * src/include/httpd/httplistener.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef _HTTPLISTENER_H
#define _HTTPLISTENER_H

#include "c.h"

/* user-settable parameters */
extern bool EnableHttpListener;
extern int PostHttpPortNumber;

#endif   /* _HTTPLISTENER_H */
