#include "macros.h"
#include "PR/os_internal.h"
#include "controller.h"

#define GCN_C_STICK_THRESHOLD 38

s32 __osContinitialized = 0;

OSPifRam __osContPifRam ALIGNED(16);
u8 __osContLastCmd;
u8 __osMaxControllers;

OSTimer __osEepromTimer;
OSMesgQueue __osEepromTimerQ ALIGNED(8);
OSMesg __osEepromTimerMsg;

u8 __osControllerTypes[MAXCONTROLLERS];
u8 __osGamecubeRumbleEnabled[MAXCONTROLLERS];

s32 osContInit(OSMesgQueue* mq, u8* bitpattern, OSContStatus* data) {
    OSMesg dummy;
    s32 ret = 0;
    OSTime t;
    OSTimer mytimer;
    OSMesgQueue timerMesgQueue;

    if (__osContinitialized != 0) {
        return 0;
    }

    __osContinitialized = 1;

    t = osGetTime();
    if (t < OS_USEC_TO_CYCLES(500000)) {
        osCreateMesgQueue(&timerMesgQueue, &dummy, 1);
        osSetTimer(&mytimer, OS_USEC_TO_CYCLES(500000) - t, 0, &timerMesgQueue, &dummy);
        osRecvMesg(&timerMesgQueue, &dummy, OS_MESG_BLOCK);
    }

    __osMaxControllers = 4;

    __osPackRequestData(CONT_CMD_REQUEST_STATUS);

    ret = __osSiRawStartDma(OS_WRITE, __osContPifRam.ramarray);
    osRecvMesg(mq, &dummy, OS_MESG_BLOCK);

    ret = __osSiRawStartDma(OS_READ, __osContPifRam.ramarray);
    osRecvMesg(mq, &dummy, OS_MESG_BLOCK);

    __osContGetInitData(bitpattern, data);
    __osContLastCmd = CONT_CMD_REQUEST_STATUS;
    __osSiCreateAccessQueue();
    osCreateMesgQueue(&__osEepromTimerQ, &__osEepromTimerMsg, 1);

    return ret;
}

u8 osContGetType(u32 index) {
    return __osControllerTypes[index];
}

typedef struct {
    /* 0x0 */ u8 dummy;
    /* 0x1 */ u8 txsize;
    /* 0x2 */ u8 rxsize;
    /* 0x3 */ u8 cmd;
} __NullCommand;

void __osContGetInitData(u8* pattern, OSContStatus* data) {
    u8* ptr;
    __OSContRequesFormat requestHeader;
    __NullCommand packet;
    s32 i;
    OSMesgQueue mq;
    OSMesg dummy;
    u8 bits;

    bits = 0;
    ptr = (u8*)__osContPifRam.ramarray;
    for (i = 0; i < __osMaxControllers; i++, ptr += sizeof(requestHeader), data++) {
        requestHeader = *(__OSContRequesFormat*)ptr;
        data->errno = CHNL_ERR(requestHeader);
        if (data->errno == 0) {
            data->type = requestHeader.typel << 8 | requestHeader.typeh;
            
            if (data->type & CONT_GCN) {
                __osControllerTypes[i] = CONT_TYPE_GCN;
            }
            else if (data->type & CONT_RELATIVE) {
                __osControllerTypes[i] = CONT_TYPE_MOUSE;
            }
            else {
                __osControllerTypes[i] = CONT_TYPE_N64;
            }

            data->status = requestHeader.status;

            bits |= 1 << i;

            // send null packet
            packet.dummy = 0;
            packet.txsize = CONT_CMD_REQUEST_STATUS_TX;
            packet.rxsize = CONT_CMD_REQUEST_STATUS_RX;
            packet.cmd = CONT_CMD_REQUEST_STATUS;

            *(__NullCommand*)ptr = packet;
        }
    }
    *pattern = bits;

    __osSiRawStartDma(OS_WRITE, __osContPifRam.ramarray);
    osCreateMesgQueue(&mq, &dummy, 1);
    osRecvMesg(&mq, &dummy, OS_MESG_NOBLOCK);
}

void __osPackRequestData(u8 cmd) {
    u8* ptr;
    __OSContRequesFormat requestHeader;
    s32 i;

    for (i = 0; i < ARRLEN(__osContPifRam.ramarray); i++) {
        __osContPifRam.ramarray[i] = 0;
    }

    __osContPifRam.pifstatus = CONT_CMD_EXE;
    ptr = (u8*)__osContPifRam.ramarray;
    requestHeader.dummy = CONT_CMD_NOP;
    requestHeader.txsize = CONT_CMD_RESET_TX;
    requestHeader.rxsize = CONT_CMD_RESET_RX;
    requestHeader.cmd = cmd;
    requestHeader.typeh = CONT_CMD_NOP;
    requestHeader.typel = CONT_CMD_NOP;
    requestHeader.status = CONT_CMD_NOP;
    requestHeader.dummy1 = CONT_CMD_NOP;

    for (i = 0; i < __osMaxControllers; i++) {
        *(__OSContRequesFormat*)ptr = requestHeader;
        ptr += sizeof(requestHeader);
    }
    *ptr = CONT_CMD_END;
}

u16 __osTranslateGCNButtons(u16 input, s32 c_stick_x, s32 c_stick_y) {
    u16 ret = 0;

    // Face buttons
    if (input & CONT_GCN_A) {
        ret |= A_BUTTON;
    }
    if (input & CONT_GCN_B) {
        ret |= B_BUTTON;
    }
    if (input & CONT_GCN_START) {
        ret |= START_BUTTON;
    }
    if (input & CONT_GCN_X) {
        ret |= X_BUTTON;
    }
    if (input & CONT_GCN_Y) {
        ret |= Y_BUTTON;
    }

    // Triggers & Z
    if (input & CONT_GCN_Z) {
        ret |= Z_TRIG;
    }
    if (input & CONT_GCN_R) {
        ret |= R_TRIG;
    }
    if (input & CONT_GCN_L) {
        ret |= L_TRIG;
    }

    // D-Pad
    if (input & CONT_GCN_UP) {
        ret |= U_JPAD;
    }
    if (input & CONT_GCN_DOWN) {
        ret |= D_JPAD;
    }
    if (input & CONT_GCN_LEFT) {
        ret |= L_JPAD;
    }
    if (input & CONT_GCN_RIGHT) {
        ret |= R_JPAD;
    }

    // C-stick to C-buttons
    if (c_stick_x > GCN_C_STICK_THRESHOLD) {
        ret |= R_CBUTTONS;
    }
    if (c_stick_x < -GCN_C_STICK_THRESHOLD) {
        ret |= L_CBUTTONS;
    }
    if (c_stick_y > GCN_C_STICK_THRESHOLD) {
        ret |= U_CBUTTONS;
    }
    if (c_stick_y < -GCN_C_STICK_THRESHOLD) {
        ret |= D_CBUTTONS;
    }

    return ret;
}

