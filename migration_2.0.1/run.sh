#!/bin/bash
# run.sh <elf> <tag> [seconds]: board run (UART log) and SWD readings (stacks, TCBs, heap, sbrk) for
# this example. Logs go to $OUT (default: .). Needs NUCLEO_RUN and STM32_PROGRAMMER_CLI (see measure.py).
S=${OUT:-.}; M=$(dirname $0); E=$1; T=$2; SEC=${3:-170}
args=(--stack 'SenderA=MainApp_Task(void*)::sA+0x10:1024' --stack 'SenderB=MainApp_Task(void*)::sB+0x10:1024'
      --stack 'Receiver=MainApp_Task(void*)::r1+0x10:2048'
      --tcb 'SenderA=MainApp_Task(void*)::sA+0x410' --tcb 'SenderB=MainApp_Task(void*)::sB+0x410' --tcb 'Receiver=MainApp_Task(void*)::r1+0x810')
arm-none-eabi-nm -C $E | grep -q ' mainAppStack$' && args+=(--stack MainApp=mainAppStack --stack defaultTask=defaultTaskBuffer --tcb MainApp=mainAppControlBlock --tcb defaultTask=defaultTaskControlBlock)
python3 $M/measure.py $E $S/${T}_uart.txt $SEC "${args[@]}" > $S/${T}_measure.txt
cat $S/${T}_measure.txt
