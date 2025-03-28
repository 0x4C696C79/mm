#include "ultra64.h"
#include "stack.h"

#pragma increment_block_number "n64-us:128"

// Variables are put before most headers as a hacky way to bypass bss reordering
OSMesgQueue sSerialEventQueue;
OSMesg sSerialMsgBuf[1];
u32 gSegments[NUM_SEGMENTS];
struct Scheduler gScheduler;
struct IrqMgrClient sIrqClient;
OSMesgQueue sIrqMgrMsgQueue;
OSMesg sIrqMgrMsgBuf[60];
OSThread gGraphThread;
STACK(sGraphStack, 0x1800);
STACK(sSchedStack, 0x600);
STACK(sAudioStack, 0x800);
STACK(sPadMgrStack, 0x500);
struct StackEntry sGraphStackInfo;
struct StackEntry sSchedStackInfo;
struct StackEntry sAudioStackInfo;
struct StackEntry sPadMgrStackInfo;
struct AudioMgr sAudioMgr;
static s32 sBssPad;
struct PadMgr gPadMgr;

#include "main.h"

#include "audiomgr.h"
#include "buffers.h"
#include "CIC6105.h"
#include "fault.h"
#include "idle.h"
#include "irqmgr.h"
#include "padmgr.h"
#include "regs.h"
#include "segment_symbols.h"
#include "stack.h"
#include "libu64/stackcheck.h"
#include "scheduler.h"
#include "sys_initial_check.h"
#include "libu64/system_heap.h"
#include "z64nmi_buff.h"
#include "z64thread.h"

s32 gScreenWidth = SCREEN_WIDTH;
s32 gScreenHeight = SCREEN_HEIGHT;
size_t gSystemHeapSize = 0;

void Main(void* arg) {
    uintptr_t fb;
    uintptr_t sysHeap;
    s32 exit;
    s16* msg;

    gScreenWidth = SCREEN_WIDTH;
    gScreenHeight = SCREEN_HEIGHT;

    Nmi_Init();
    Fault_Init();
    Check_RegionIsSupported();
    Check_ExpansionPak();

    sysHeap = (uintptr_t)SEGMENT_END(buffers);
    fb = FRAMEBUFFERS_START_ADDR;
    gSystemHeapSize = fb - sysHeap;
    SystemHeap_Init((void*)sysHeap, gSystemHeapSize);

    Regs_Init();

    R_ENABLE_ARENA_DBG = 0;

    osCreateMesgQueue(&sSerialEventQueue, sSerialMsgBuf, ARRAY_COUNT(sSerialMsgBuf));
    osSetEventMesg(OS_EVENT_SI, &sSerialEventQueue, NULL);

    osCreateMesgQueue(&sIrqMgrMsgQueue, sIrqMgrMsgBuf, ARRAY_COUNT(sIrqMgrMsgBuf));

    StackCheck_Init(&sSchedStackInfo, sSchedStack, STACK_TOP(sSchedStack), 0, 0x100, "sched");
    Sched_Init(&gScheduler, STACK_TOP(sSchedStack), Z_PRIORITY_SCHED, gViConfigModeType, 1, &gIrqMgr);

    CIC6105_AddRomInfoFaultPage();

    IrqMgr_AddClient(&gIrqMgr, &sIrqClient, &sIrqMgrMsgQueue);

    StackCheck_Init(&sAudioStackInfo, sAudioStack, STACK_TOP(sAudioStack), 0, 0x100, "audio");
    AudioMgr_Init(&sAudioMgr, STACK_TOP(sAudioStack), Z_PRIORITY_AUDIOMGR, Z_THREAD_ID_AUDIOMGR, &gScheduler, &gIrqMgr);

    StackCheck_Init(&sPadMgrStackInfo, sPadMgrStack, STACK_TOP(sPadMgrStack), 0, 0x100, "padmgr");
    PadMgr_Init(&sSerialEventQueue, &gIrqMgr, Z_THREAD_ID_PADMGR, Z_PRIORITY_PADMGR, STACK_TOP(sPadMgrStack));

    AudioMgr_Unlock(&sAudioMgr);

    StackCheck_Init(&sGraphStackInfo, sGraphStack, STACK_TOP(sGraphStack), 0, 0x100, "graph");
    osCreateThread(&gGraphThread, Z_THREAD_ID_GRAPH, Graph_ThreadEntry, arg, STACK_TOP(sGraphStack), Z_PRIORITY_GRAPH);
    osStartThread(&gGraphThread);

    exit = false;

    while (!exit) {
        msg = NULL;
        osRecvMesg(&sIrqMgrMsgQueue, (OSMesg*)&msg, OS_MESG_BLOCK);
        if (msg == NULL) {
            break;
        }

        switch (*msg) {
            case OS_SC_PRE_NMI_MSG:
                Nmi_SetPrenmiStart();
                break;

            case OS_SC_NMI_MSG:
                exit = true;
                break;
        }
    }

    IrqMgr_RemoveClient(&gIrqMgr, &sIrqClient);
    osDestroyThread(&gGraphThread);
}
