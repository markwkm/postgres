/*
 *	qsort.c: a modified recursive parallel quicksort algorithm
 *
 * FIXME: Do we need to set up function for auxiliary processes for sorting?
 *
 *	Modifications from vanilla NetBSD source:
 *	  Add do ... while() macro fix
 *	  Remove __inline, _DIAGASSERTs, __P
 *	  Remove ill-considered "swap_cnt" switch to insertion sort,
 *	  in favor of a simple check for presorted input.
 *
 *	CAUTION: if you change this file, see also qsort_arg.c
 *
 *	$PostgreSQL: pgsql/src/port/qsort.c,v 1.13 2007/03/18 05:36:50 neilc Exp $
 */

/*	$NetBSD: qsort.c,v 1.13 2003/08/07 16:43:42 agc Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *	  may be used to endorse or promote products derived from this software
 *	  without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.	IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <unistd.h>
#include <sys/sem.h>

#include "c.h"


/*
 * FIXME:
 * The only reason for defining these three variables here is because the
 * quicksort code is built into libpgport and other files, such as
 * postmaster.c and proc.c do not.  I do not know what other scenarios would
 * be more appropriate.
 */
int sem_id_dop;
int *shm_dop;
int degree_of_parallelism;

static void get_lock();
static void release_lock();

static char *med3(char *a, char *b, char *c,
	 int (*cmp) (const void *, const void *));
static void swapfunc(char *, char *, size_t, int);

/*
 * Qsort routine based on J. L. Bentley and M. D. McIlroy,
 * "Engineering a sort function",
 * Software--Practice and Experience 23 (1993) 1249-1265.
 * We have modified their original by adding a check for already-sorted input,
 * which seems to be a win per discussions on pgsql-hackers around 2006-03-21.
 */
#define swapcode(TYPE, parmi, parmj, n) \
do {		\
	size_t i = (n) / sizeof (TYPE);			\
	TYPE *pi = (TYPE *)(void *)(parmi);			\
	TYPE *pj = (TYPE *)(void *)(parmj);			\
	do {						\
		TYPE	t = *pi;			\
		*pi++ = *pj;				\
		*pj++ = t;				\
		} while (--i > 0);				\
} while (0)

#define SWAPINIT(a, es) swaptype = ((char *)(a) - (char *)0) % sizeof(long) || \
	(es) % sizeof(long) ? 2 : (es) == sizeof(long)? 0 : 1;

static void
swapfunc(char *a, char *b, size_t n, int swaptype)
{
	if (swaptype <= 1)
		swapcode(long, a, b, n);
	else
		swapcode(char, a, b, n);
}

#define swap(a, b)						\
	if (swaptype == 0) {					\
		long t = *(long *)(void *)(a);			\
		*(long *)(void *)(a) = *(long *)(void *)(b);	\
		*(long *)(void *)(b) = t;			\
	} else							\
		swapfunc(a, b, es, swaptype)

#define vecswap(a, b, n) if ((n) > 0) swapfunc((a), (b), (size_t)(n), swaptype)

static void
get_lock()
{
	struct sembuf operations[1];

	operations[0].sem_num = 0;
	operations[0].sem_op = -1;
	operations[0].sem_flg = 0;
	/* FIXME: handle error */
	semop(sem_id_dop, operations, 1);
}

void static
release_lock()
{
	struct sembuf operations[1];

	operations[0].sem_num = 0;
	operations[0].sem_op = 1;
	operations[0].sem_flg = 0;
	/* FIXME: handle error */
	semop(sem_id_dop, operations, 1);
}

static char *
med3(char *a, char *b, char *c, int (*cmp) (const void *, const void *))
{
	return cmp(a, b) < 0 ?
		(cmp(b, c) < 0 ? b : (cmp(a, c) < 0 ? c : a))
		: (cmp(b, c) > 0 ? b : (cmp(a, c) < 0 ? a : c));
}

void
pg_qsort(void *a, size_t n, size_t es, int (*cmp) (const void *, const void *))
{
	char	   *pa,
			   *pb,
			   *pc,
			   *pd,
			   *pl,
			   *pm,
			   *pn;
	int			d,
				r,
				swaptype,
				presorted;

	/*
	 * Use -1 to initialize because fork() uses 0 to identify a process as a
	 * child.
	 */
	int lchild = -1;
	int rchild = -1;
	int status; /* For waitpid() only. */

	SWAPINIT(a, es);
	if (n < 7)
	{
		/* Insertion sort if less than 7 elements. */
		for (pm = (char *) a + es; pm < (char *) a + n * es; pm += es)
			for (pl = pm; pl > (char *) a && cmp(pl - es, pl) > 0;
				 pl -= es)
				swap(pl, pl - es);
		return;
	}
	presorted = 1;
	/* Check if sorted. */
	for (pm = (char *) a + es; pm < (char *) a + n * es; pm += es)
	{
		if (cmp(pm - es, pm) > 0)
		{
			presorted = 0;
			break;
		}
	}
	if (presorted)
		return;
	pm = (char *) a + (n / 2) * es;
	if (n > 7)
	{
		pl = (char *) a;
		pn = (char *) a + (n - 1) * es;
		if (n > 40)
		{
			d = (n / 8) * es;
			pl = med3(pl, pl + d, pl + 2 * d, cmp);
			pm = med3(pm - d, pm, pm + d, cmp);
			pn = med3(pn - 2 * d, pn - d, pn, cmp);
		}
		pm = med3(pl, pm, pn, cmp);
	}
	/* Begin "partition" logic. */
	swap(a, pm);
	pa = pb = (char *) a + es;
	pc = pd = (char *) a + (n - 1) * es;
	for (;;)
	{
		while (pb <= pc && (r = cmp(pb, a)) <= 0)
		{
			if (r == 0)
			{
				swap(pa, pb);
				pa += es;
			}
			pb += es;
		}
		while (pb <= pc && (r = cmp(pc, a)) >= 0)
		{
			if (r == 0)
			{
				swap(pc, pd);
				pd -= es;
			}
			pc -= es;
		}
		if (pb > pc)
			break;
		swap(pb, pc);
		pb += es;
		pc -= es;
	}
	/* End "partition" logic. */
	pn = (char *) a + n * es;
	r = Min(pa - (char *) a, pb - pa);
	vecswap(a, pb - r, r);
	r = Min(pd - pc, pn - pd - es);
	vecswap(pb, pn - r, r);
	if ((r = pb - pa) > es)
	{
		get_lock();
		if (*shm_dop < degree_of_parallelism)
		{
			/* Under the degree limit, fork. */
			++*shm_dop;
			release_lock();

			lchild = fork(); /* FIXME: handle error */
			if (lchild == 0) {
				/* The 'left' child starts processing. */
				qsort(a, r / es, es, cmp);

				get_lock();
				--*shm_dop;
				release_lock();
				exit(0);
			}
		}
		else
		{
			release_lock();
			qsort(a, r / es, es, cmp);
		}
	}
	if ((r = pd - pc) > es)
	{
		get_lock();
		if (*shm_dop < degree_of_parallelism)
		{
			/* Under the degree limit, fork. */
			++*shm_dop;
			release_lock();

			rchild = fork(); /* FIXME: handle error */
			if (lchild == 0) {
				/* The 'right' child starts processing. */
				qsort(pn - r, r / es, es, cmp);

				get_lock();
				--*shm_dop;
				release_lock();
				exit(0);
			}
		}
		else
		{
			release_lock();
			qsort(pn - r, r / es, es, cmp);
		}
	}
	waitpid(lchild, &status, 0);
	waitpid(rchild, &status, 0);
}
