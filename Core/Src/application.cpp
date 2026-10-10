#include "application.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"  // StaticTask_t
#include "csp/csp4cmsis.h"

#include <cstdio>

#define TOTAL_MESSAGES_PER_SENDER 1000000
#define CHECK_INTERVAL 10000
#define MAX_TOTAL_MESSAGES (TOTAL_MESSAGES_PER_SENDER * 2)

using namespace csp;

struct Message {
  int source_id;
  int sequence_num;
};

class Sender : public CSProcessStatic<256> {
  Chanout<Message> out;
  int id;

 public:
  Sender(Chanout<Message> w, int sender_id) : out(w), id(sender_id) {}

  void run() override {
    for (int i = 0; i < TOTAL_MESSAGES_PER_SENDER; ++i) {
      Message msg = {id, i};
      out << msg;
    }
    while (true) {
      SleepFor(Forever);  // done
    }
  }
};

class Receiver : public CSProcessStatic<512> {
  Chanin<Message> inA;
  Chanin<Message> inB;

 public:
  Receiver(Chanin<Message> rA, Chanin<Message> rB) : inA(rA), inB(rB) {}

  // The only process that prints: two threads printing at once would lose characters.
  void run() override {
    SleepFor(Milliseconds(10));
    printf("[Receiver] Task running. Using Resident-Guard ALT.\r\n");

    Message msgA, msgB;
    int count = 0;
    int next_seqA = 0;
    int next_seqB = 0;
    bool error_found = false;

    // fairSelect() starts after the guard selected last time, so neither sender can starve
    // the other. When it returns, the selected guard's message has been transferred.
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
        if (next_seqA == TOTAL_MESSAGES_PER_SENDER) {
          printf("[Receiver] Sender 1: all %d messages received\r\n", next_seqA);
        }
      } else if (selected == 1) {
        if (msgB.source_id != 2 || msgB.sequence_num != next_seqB) {
          printf("!! DATA ERROR Chan B: Expected ID 2 Seq %d, Got ID %d Seq %d\r\n",
                 next_seqB, msgB.source_id, msgB.sequence_num);
          error_found = true;
        }
        next_seqB++;
        if (next_seqB == TOTAL_MESSAGES_PER_SENDER) {
          printf("[Receiver] Sender 2: all %d messages received\r\n", next_seqB);
        }
      }

      count++;
      if (count % CHECK_INTERVAL == 0) {
        printf("[Receiver] Verified %d messages...\r\n", count);
        if (error_found) break;
      }
    }

    if (!error_found) {
      printf("[Receiver] SUCCESS: %d messages verified heap-free.\r\n", count);
    } else {
      printf("[Receiver] DATA ERROR: verification stopped after %d messages.\r\n", count);
    }
    while (true) {
      SleepFor(Forever);  // done
    }
  }
};

// MainApp runs above the network, so the processes first run after MainApp has printed
// its messages and exited.
static constexpr osPriority_t MAIN_APP_PRIORITY = osPriorityBelowNormal;
static constexpr osPriority_t NETWORK_PRIORITY  = osPriorityLow;

// Static stack (384 words = 1.5 KB) and control block: no heap.
alignas(8) static uint32_t mainAppStack[384];
static StaticTask_t mainAppControlBlock;

void MainApp_Task(void* argument) {
  (void)argument;
  osDelay(10);
  printf("\r\n--- Launching CSP Static Network (Zero-Heap) ---\r\n");

  static Channel<Message> chan_A;
  static Channel<Message> chan_B;

  static Sender sA(chan_A.writer(), 1);
  static Sender sB(chan_B.writer(), 2);
  static Receiver r1(chan_A.reader(), chan_B.reader());

  Run(InParallel(sA, sB, r1), ExecutionMode::StaticNetwork, NETWORK_PRIORITY);
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
