# Test-only instrumentation of the Receiver's ALT loop (never committed). Usage: fair_patch.py <application.cpp>
# Counts per guard, switches between guards, longest run of the same guard, per-10000-selection block
# min/max of guard-A selections, where the longest run ended (selection index, guard), number of
# runs that reached 100, start/end tick (HAL_GetTick) of the 2,000,000 selections.
import sys
f = sys.argv[1]; s = open(f).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, a; s = s.replace(a, b)
rep('using namespace csp;', '''using namespace csp;
extern "C" uint32_t HAL_GetTick(void);
extern "C" { volatile uint32_t t_n, t_selA, t_selB, t_switch, t_maxrun, t_blkmin = 0xFFFFFFFFu, t_blkmax,
                                t_t0, t_t1, t_done, t_maxrun_end, t_maxrun_guard, t_runs100; }
static uint32_t t_last = 2, t_run, t_blkA;''')
rep('      int selected = alt.fairSelect();\n', '''      int selected = alt.fairSelect();
      if (t_n == 0) t_t0 = HAL_GetTick();
      t_n++;
      if (selected == 0) { t_selA++; t_blkA++; } else { t_selB++; }
      if ((uint32_t)selected == t_last) { if (++t_run > t_maxrun) { t_maxrun = t_run; t_maxrun_end = t_n; t_maxrun_guard = (uint32_t)selected; } if (t_run == 100) t_runs100++; } else { if (t_last != 2) t_switch++; t_run = 1; if (t_maxrun == 0) t_maxrun = 1; }
      t_last = (uint32_t)selected;
      if (t_n % 10000 == 0) { if (t_blkA < t_blkmin) t_blkmin = t_blkA; if (t_blkA > t_blkmax) t_blkmax = t_blkA; t_blkA = 0; }
''')
rep('    if (!error_found) {\n', '    t_t1 = HAL_GetTick(); t_done = 1;\n    if (!error_found) {\n')
open(f, 'w').write(s)
