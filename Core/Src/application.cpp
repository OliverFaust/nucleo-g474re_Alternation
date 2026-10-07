#include "application.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"  // StaticTask_t: the control block of a statically created thread
#include "csp/csp4cmsis.h"

#include <cstdio>

// --- Configuration ---
#define TOTAL_MESSAGES_PER_SENDER 1000000
#define CHECK_INTERVAL 10000
#define MAX_TOTAL_MESSAGES (TOTAL_MESSAGES_PER_SENDER * 2)


using namespace csp;

struct Message {
  int source_id;
  int sequence_num;
};

// --- 1. Define Channels ---
// A Channel is a Rendezvous (capacity 0) synchronisation point.
using AltChannel = Channel < Message > ;

// --- 2. Define Processes ---
// CSProcessStatic<N> gives each process a statically allocated,
// compile-time-sized stack of N words -- no heap allocation involved.
class Sender: public CSProcessStatic < 256 > {
  private: Chanout < Message > out;
  int id;
  public: Sender(Chanout < Message > w, int sender_id): out(w),
  id(sender_id) {}

  const char * name() const override {
    return "Sender";
  }

  void run() override {
    printf("[Sender %d] Starting sequence.\r\n", id);
    for (int i = 0; i < TOTAL_MESSAGES_PER_SENDER; ++i) {
      Message msg = {
        id,
        i
      };
      out << msg;
    }
    printf("[Sender %d] Finished.\r\n", id);
    while (true) {
      SleepFor(Forever);  // done: sleep for ever
    }
  }
};

class Receiver: public CSProcessStatic < 512 > {
  private: Chanin < Message > inA;
  Chanin < Message > inB;
  public: Receiver(Chanin < Message > rA, Chanin < Message > rB): inA(rA),
  inB(rB) {}

  const char * name() const override {
    return "Receiver";
  }

  void run() override {
    SleepFor(Milliseconds(10));
    printf("[Receiver] Task running. Using Resident-Guard ALT.\r\n");

    Message msgA, msgB;
    int count = 0;
    int next_seqA = 0;
    int next_seqB = 0;
    bool error_found = false;

    // External choice over two input guards. The guards live in the channel ends inA and inB
    // (no allocation). fairSelect() checks the guards starting after the last selected one, and
    // the first ready guard wins, so neither sender can starve the other. Exactly one guard is
    // selected per call, and its message has been transferred when fairSelect() returns.
    Alternative alt(inA | msgA, inB | msgB);

    while (count < MAX_TOTAL_MESSAGES) {
      int selected = alt.fairSelect();

      if (selected == 0) {
        if (msgA.source_id != 1 || msgA.sequence_num != next_seqA) {
          printf("!! DATA ERROR Chan A: Expected ID 1 Seq %d, Got ID %d Seq %d\r\n",
            next_seqA, msgA.source_id, msgA.sequence_num);
          error_found = true;
        }
        next_seqA++;
      } else if (selected == 1) {
        if (msgB.source_id != 2 || msgB.sequence_num != next_seqB) {
          printf("!! DATA ERROR Chan B: Expected ID 2 Seq %d, Got ID %d Seq %d\r\n",
            next_seqB, msgB.source_id, msgB.sequence_num);
          error_found = true;
        }
        next_seqB++;
      }

      count++;
      if (count % CHECK_INTERVAL == 0) {
        printf("[Receiver] Verified %d messages...\r\n", count);
        if (error_found) break;
      }
    }

    if (!error_found) {
      printf("[Receiver] SUCCESS: %d messages verified heap-free.\r\n", count);
    }
    while (true) {
      SleepFor(Forever);  // done: sleep for ever
    }
  }
};

// Start order. MainApp runs at a higher priority than the network it launches, so
// Run(..., StaticNetwork) only creates the three process threads and returns: none of them
// can preempt MainApp, and they first run after MainApp has printed its messages and exited.
// All stay below CubeMX's defaultTask (osPriorityNormal).
static constexpr osPriority_t MAIN_APP_PRIORITY = osPriorityBelowNormal;
static constexpr osPriority_t NETWORK_PRIORITY  = osPriorityLow;

// MainApp's stack and control block are static: creating the thread takes no heap.
// CMSIS-RTOS2 counts the stack in bytes: 384 words = 1.5 KB. Measured on the NUCLEO-G474RE:
// MainApp uses 620 B (Debug, -O0) and 308 B (Release, -Os) of it.
alignas(8) static uint32_t mainAppStack[384];
static StaticTask_t mainAppControlBlock;

// --- 3. The Main Application Task ---
void MainApp_Task(void * argument) {
  (void) argument;
  osDelay(10);

  printf("\r\n--- Launching CSP Static Network (Zero-Heap) ---\r\n");

  // Static storage (.data segment) -- no dynamic allocation.
  static AltChannel chan_A;
  static AltChannel chan_B;

  static Sender sA(chan_A.writer(), 1);
  static Sender sB(chan_B.writer(), 2);
  static Receiver r1(chan_A.reader(), chan_B.reader());

  Run(InParallel(sA, sB, r1), ExecutionMode::StaticNetwork, NETWORK_PRIORITY);

  // StaticNetwork: Run() has created the processes and returns at once; they run on their own.
  printf("*** MainApp_Task: network started. Terminating. ***\r\n");
  osThreadExit();
}

void csp_app_main_init(void) {
  osThreadAttr_t attr = {};
  attr.name       = "MainApp";
  attr.stack_mem  = mainAppStack;
  attr.stack_size = sizeof(mainAppStack);
  attr.cb_mem     = &mainAppControlBlock;
  attr.cb_size    = sizeof(mainAppControlBlock);
  attr.priority   = MAIN_APP_PRIORITY;
  if (osThreadNew(MainApp_Task, NULL, &attr) == NULL) {
    printf("ERROR: MainApp_Task creation failed!\r\n");
  }
}
