#!/bin/bash
# fair_read.sh <elf> <tag> [seconds]: flash + UART log, then read the fairness counters over SWD
S=${OUT:-.}; E=$1; T=$2; SEC=${3:-170}; CLI=${STM32_PROGRAMMER_CLI:-STM32_Programmer_CLI}
python3 $NUCLEO_RUN $E $S/${T}_uart.txt $SEC > /dev/null
rd() { a=$(arm-none-eabi-nm $E | awk -v s=$1 '$3==s{print $1}'); v=$($CLI -c port=SWD mode=HOTPLUG -r32 0x$a 4 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | grep -E '^0x' | awk '{print $3}'); printf '%d' 0x$v; }
t0=$(rd t_t0); t1=$(rd t_t1)
echo "done=$(rd t_done) selections=$(rd t_n) A=$(rd t_selA) B=$(rd t_selB) switches=$(rd t_switch) longest-run=$(rd t_maxrun) A-per-10000-block min=$(rd t_blkmin) max=$(rd t_blkmax) time=$((t1 - t0)) ms longest-run-ended-at=$(rd t_maxrun_end) guard=$(rd t_maxrun_guard) runs>=100: $(rd t_runs100)"
