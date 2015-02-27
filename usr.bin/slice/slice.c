/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * David Hitz of Auspex Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1991, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)look.c	8.2 (Berkeley) 5/4/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>

/*
 * look -- find lines in a sorted list.
 *
 * The man page said that TABs and SPACEs participate in -d comparisons.
 * In fact, they were ignored.  This implements historic practice, not
 * the manual page.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>


#include "pathnames.h"

static char _path_words[] = _PATH_WORDS;

#define	EQUAL		0
#define	GREATER		1
#define	LESS		(-1)

int dflag, fflag;

char    *binary_search(time_t from, wchar_t *, unsigned char *, unsigned char *);
int      compare(wchar_t *, unsigned char *, unsigned char *);
char    *linear_search(time_t from, time_t to, wchar_t *, unsigned char *, unsigned char *);
int      look(time_t from, time_t to, wchar_t *, unsigned char *, unsigned char *);
wchar_t	*prepkey(const char *, wchar_t);
void     print_from(wchar_t *, unsigned char *, unsigned char *);
time_t make_time(unsigned char *line);
int time_compare(unsigned char *line, time_t pattern_time);

static void usage(void);

int
main(int argc, char *argv[])
{
	struct stat sb;
	int ch, fd, match;
	wchar_t termchar;
	unsigned char *back, *front;
	unsigned const char *file;
	wchar_t *key;
	time_t pattern_time_from = -1;
	time_t pattern_time_to = -1;

	(void) setlocale(LC_CTYPE, "");

    char *tz;

    tz = getenv("TZ");
    setenv("TZ", "", 0);
    tzset();

	file = _path_words;
	termchar = L'\0';
	while ((ch = getopt(argc, argv, "dft:")) != -1)
		switch(ch) {
		case 'd':
			dflag = 1;
			break;
		case 'f':
			fflag = 1;
			break;
		case 't':
			if (mbrtowc(&termchar, optarg, MB_LEN_MAX, NULL) !=
			    strlen(optarg))
				errx(2, "invalid termination character");
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc < 3)
		usage();

    pattern_time_from = make_time(*argv++);
	pattern_time_to = make_time(*argv++);
	if (pattern_time_from == -1 || pattern_time_to == -1)
		usage();

	file = *argv++;

	match = 1;
	do {
		if ((fd = open(file, O_RDONLY, 0)) < 0 || fstat(fd, &sb))
			err(2, "%s", file);
		if (sb.st_size == 0) {
			close(fd);
			continue;
		}
		if ((front = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_SHARED, fd, (off_t)0)) == MAP_FAILED)
			err(2, "%s", file);
		back = front + sb.st_size;
		match *= (look(pattern_time_from, pattern_time_to, key, front, back));
		close(fd);
	} while (argc-- > 2 && (file = *argv++));

	exit(match);
}

wchar_t *
prepkey(const char *string, wchar_t termchar)
{
	const char *readp;
	wchar_t *key, *writep;
	wchar_t ch;
	size_t clen;

	/*
	 * Reformat search string and convert to wide character representation
	 * to avoid doing it multiple times later.
	 */
	if ((key = malloc(sizeof(wchar_t) * (strlen(string) + 1))) == NULL)
		err(2, NULL);
	readp = string;
	writep = key;
	while ((clen = mbrtowc(&ch, readp, MB_LEN_MAX, NULL)) != 0) {
		//if (clen == (size_t)-1 || clen == (size_t)-2)
		//	err(2, EILSEQ, NULL);
		if (fflag)
			ch = towlower(ch);
		if (!dflag || iswalnum(ch))
			*writep++ = ch;
		readp += clen;
	}
	*writep = L'\0';
	if (termchar != L'\0' && (writep = wcschr(key, termchar)) != NULL)
		*++writep = L'\0';
	return (key);
}

#define	SKIP_PAST_NEWLINE(p, back) \
	while (p < back && *p++ != '\n');

int
look(time_t from, time_t to, wchar_t *string, unsigned char *front, unsigned char *back)
{
	unsigned char *from_line = binary_search(from, string, front, back);
	unsigned char *to_line = binary_search(to, string, front, back);
	SKIP_PAST_NEWLINE(to_line, back);

    if (from_line && to_line)
	{
		for (;from_line < to_line; ++from_line)
			putchar(*from_line);
	}
	return (front ? 0 : 1);
}

int time_compare(unsigned char *line, time_t pattern_time)
{
	time_t line_time = make_time(line);
	return line_time > 0 ? difftime(pattern_time, line_time) : -1;
}

char *
binary_search(time_t from, wchar_t *string, unsigned char *front, unsigned char *back)
{
	unsigned char *p;

	p = front + (back - front) / 2;
	SKIP_PAST_NEWLINE(p, back);

	unsigned char *temp;
	struct tm tm;
	/*
	 * If the file changes underneath us, make sure we don't
	 * infinitely loop.
	 */
	while (p < back && back > front) {
		double d;
		temp = p;
		d = time_compare(temp, from);
        if (d > 0)
        	front = p;
		else
			back = p;
		p = front + (back - front) / 2;
		SKIP_PAST_NEWLINE(p, back);
	}
	return (front);
}

time_t make_time(unsigned char *line)
{
	struct tm tm;
	strptime(line, "%F %H:%M:%S", &tm);
	time_t ret = mktime(&tm);
	if( ret == -1 )
    {
	    printf("Error: unable to make time using mktime\n");
	}
	return ret;
}


static void
usage(void)
{
	(void)fprintf(stderr, "usage: slice [-df] [-t char] 'from' 'to' [file ...]\nExample: slice '2015-02-27 18:02:00' '2015-02-27 18:03:01' frontik/frontik*.hhnet.ru.*.log\n");
	exit(2);
}
