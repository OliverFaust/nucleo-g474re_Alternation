#!/usr/bin/env python3
"""measure.py <elf> <uart-log> <seconds> [--stack NAME=SYMBOL[+OFFSET][:BYTES]]... [--tcb NAME=SYMBOL[+OFFSET]]...

Flash and log the UART (nucleo_run.py), then read over SWD (hotplug, no reset):
  --stack  untouched 0xA5 fill from the low end of a stack buffer (BYTES defaults to the symbol size);
  --tcb    priority and name from a FreeRTOS 10.3.1 TCB_t (uxPriority +0x2c, pcTaskName +0x34);
  always   FreeRTOS heap_4 counters and newlib's __sbrk_heap_end.
SYMBOL is a demangled name from `arm-none-eabi-nm -C`, e.g. 'MainApp_Task(void*)::sender'; OFFSET is
the member offset inside a process object (CSProcessStatic<N>: stack at +0x10, TCB at +0x10 + 4*N;
check with arm-none-eabi-gdb: print &((Class*)0)->m_stack).
Tools: NUCLEO_RUN (CSP4CMSIS tests/hw_nucleo_g474/nucleo_run.py) and STM32_PROGRAMMER_CLI from the
environment."""
import os, re, subprocess, sys
elf, log, secs = sys.argv[1], sys.argv[2], sys.argv[3]
specs = sys.argv[4:]
CLI = os.environ.get('STM32_PROGRAMMER_CLI', 'STM32_Programmer_CLI')
print(subprocess.run(['python3', os.environ['NUCLEO_RUN'], elf, log, secs],
                     capture_output=True, text=True).stdout.strip().splitlines()[-1])
syms, sizes = {}, {}
for l in subprocess.run(['arm-none-eabi-nm', '-C', '-S', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split(' ', 3)
    if len(p) == 4: syms[p[3]], sizes[p[3]] = int(p[0], 16), int(p[1], 16)
    elif len(p) == 3: syms[p[2]] = int(p[0], 16)
def words(addr, n):
    out = subprocess.run([CLI, '-c', 'port=SWD', 'mode=HOTPLUG', '-r32', hex(addr), str(n * 4)],
                         capture_output=True, text=True).stdout
    out = re.sub(r'\x1b\[[0-9;]*m', '', out)
    w = []
    for l in out.splitlines():
        if re.match(r'^0x[0-9A-Fa-f]{8} :', l): w += [int(x, 16) for x in l.split(':', 1)[1].split()]
    return w[:n]
def resolve(ref):
    m = re.match(r'^(.*?)(?:\+(0x[0-9a-fA-F]+|\d+))?(?::(\d+))?$', ref)
    sym, off, size = m.group(1), int(m.group(2), 0) if m.group(2) else 0, m.group(3)
    return syms[sym] + off, int(size) if size else sizes.get(sym)
i = 0
while i < len(specs):
    kind, arg = specs[i], specs[i + 1]; i += 2
    name, ref = arg.split('=', 1)
    addr, nbytes = resolve(ref)
    if kind == '--stack':
        w = words(addr, nbytes // 4); free = 0
        for x in w:
            if x != 0xA5A5A5A5: break
            free += 1
        print(f'stack  {name:12s} {nbytes:5d} B: used {nbytes - 4 * free:5d} B, never touched {4 * free:5d} B')
    elif kind == '--tcb':
        w = words(addr, 0x44 // 4)
        nm = b''.join(x.to_bytes(4, 'little') for x in w[0x34 // 4:0x44 // 4]).split(b'\0')[0].decode(errors='replace')
        print(f'thread {name:12s} priority {w[0x2c // 4]:2d}  name "{nm}"')
h = words(syms['xFreeBytesRemaining'], 4)
print(f'FreeRTOS heap: free {h[0]} B, minimum ever free {h[1]} B, allocations {h[2]}, frees {h[3]}')
print(f'newlib sbrk: __sbrk_heap_end = {words(syms["__sbrk_heap_end"], 1)[0]:#010x}'
      f' (_end = {syms.get("_end", 0):#010x})')
