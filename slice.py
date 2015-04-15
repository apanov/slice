#!/usr/bin/python
# vim: ts=4 sw=4 expandtab

import sys, os, re, time, locale
from datetime import timedelta, datetime

LINEAR_SEARCH_LIMIT = 2048
MAX_STACKTRACE = 1000

locale.setlocale(locale.LC_ALL, 'en_US.UTF-8')

def dt_fromtime(t):
    return datetime(1900, 1, 1).strptime(t, '%H:%M:%S')

def log(s):
    if False:
        sys.stderr.write(s)
        sys.stderr.flush()

def out(s):
    sys.stdout.write(s)
def outflush():
    sys.stdout.flush()

def num(n):
    return locale.format_string("%d", n, grouping=True)

def _find_line_with_time(f, time_re):
    i = 0
    while True:
        i += 1
        line = f.readline()
        if i > MAX_STACKTRACE:
            return None, line, f.tell()
        m = time_re.match(line)
        if m is not None:
            return m, line, f.tell()

def find_pos(f, dt, time_re, totalsize):
    lo_limit = 0
    hi_limit = totalsize

    cur_pos = totalsize / 2
    prev_cur_pos = 0
    while True:
        if lo_limit >= hi_limit:
            return None

        f.seek(cur_pos)
        m, line, time_pos = _find_line_with_time(f, time_re)
        if m is None:
            break

        line_dt = dt_fromtime(m.group(1))
        log("%s\n" % line_dt)

        found = False

        if abs(prev_cur_pos - cur_pos) < LINEAR_SEARCH_LIMIT:
            break

        prev_cur_pos = cur_pos
        if line_dt > dt:
            hi_limit = cur_pos
            cur_pos = lo_limit + ((cur_pos - lo_limit) / 2)
        elif line_dt < dt:
            lo_limit = cur_pos
            cur_pos = cur_pos + ((hi_limit - lo_limit) / 2)
        else:
            cur_pos = cur_pos - len(line)
            found = True

        log("%s .. %s \n" % (num(lo_limit), num(hi_limit)))
        if found:
            break

    return cur_pos

def linear_search(f, from_pos, to_pos, request_id, time_re):
    if from_pos >= to_pos:
        return 1

    f.seek(from_pos)
    stack_trace_start = False
    result = 1
    while f.tell() < to_pos:
        line = f.readline()
        if request_id in line:
            stack_trace_start = True
            out(line)
            result = 0
            continue

        m = time_re.match(line)
        if stack_trace_start and m is None:
            out(line)

        if m is not None:
            stack_trace_start = False

    return result

def adjust_bounds(f, from_pos, to_pos):
    f.seek(max(from_pos - LINEAR_SEARCH_LIMIT, 0))
    m, line, from_pos = _find_line_with_time(f, time_re)
    f.seek(min(to_pos + LINEAR_SEARCH_LIMIT, totalsize))
    m, line, to_pos = _find_line_with_time(f, time_re)
    return from_pos, to_pos


outflush()

request_id = sys.argv[1]
from_dt = datetime.fromtimestamp(long(request_id[:10]) - 1).replace(year=1900, month=1, day=1)
to_dt = datetime.fromtimestamp(long(request_id[:10]) + 3).replace(year=1900, month=1, day=1)

f = open(sys.argv[2], 'r')

totalsize = os.fstat(f.fileno()).st_size
time_re = re.compile('^.* (\d{2}:\d{2}:\d{2}).*$')

from_pos = find_pos(f, from_dt, time_re, totalsize)
to_pos = find_pos(f, to_dt, time_re, totalsize)
from_pos, to_pos = adjust_bounds(f, from_pos, to_pos)

result = 1 # no records found
if from_pos and to_pos:
    log("%d .. %d" % (from_pos, to_pos))
    result = linear_search(f, from_pos, to_pos, request_id, time_re)

f.close()
exit(result)
