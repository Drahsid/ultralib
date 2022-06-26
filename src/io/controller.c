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

__OSContGCNOrigin __osGCNOrigins[MAXCONTROLLERS] = {
    (__OSContGCNOrigin){ 128, 128, 128, 128, 0, 0, 0, 0 },
    (__OSContGCNOrigin){ 128, 128, 128, 128, 0, 0, 0, 0 },
    (__OSContGCNOrigin){ 128, 128, 128, 128, 0, 0, 0, 0 },
    (__OSContGCNOrigin){ 128, 128, 128, 128, 0, 0, 0, 0 },
};

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

#ifdef FAILED_ATTEMPT
    __osContGetGCNOrigins(mq);
    __osContLastCmd = CONT_CMD_GCN_READORIGIN;
    __osContReadGCNOrigins();
#endif

    __osSiCreateAccessQueue();
    osCreateMesgQueue(&__osEepromTimerQ, &__osEepromTimerMsg, 1);

    return ret;
}

u8 osContGetType(u32 index) {
    return __osControllerTypes[index];
}

#ifdef FAILED_ATTEMPT
void __osContReadGCNOrigins(void) {
    u8* ptr = (u8*)__osContPifRam.ramarray;
    __OSContGCNOriginFormat* readformat;
    int i;

    for (i = 0; i < __osMaxControllers; i++) {
        readformat = ptr;
        if (__osControllerTypes[i] == CONT_TYPE_GCN) {
            __osGCNOrigins[i].stick_x = readformat->origin.stick_x;
            __osGCNOrigins[i].stick_y = readformat->origin.stick_y;
            __osGCNOrigins[i].c_stick_x = readformat->origin.c_stick_x;
            __osGCNOrigins[i].c_stick_y = readformat->origin.c_stick_y;
            __osGCNOrigins[i].l_trig = readformat->origin.l_trig;
            __osGCNOrigins[i].r_trig = readformat->origin.r_trig;
            __osGCNOrigins[i].a = readformat->origin.a;
            __osGCNOrigins[i].b = readformat->origin.b;
        }
        ptr += sizeof(__OSContGCNOriginFormat);
    }
}

void __osContGetGCNOrigins(OSMesgQueue* mq) {
    u8* ptr = (u8*)__osContPifRam.ramarray;
    __OSContGCNOriginFormat readformat;
    OSMesg dummy;
    int i;

    for (i = 0; i < ARRLEN(__osContPifRam.ramarray); i++) {
        __osContPifRam.ramarray[i] = 0;
    }

    __osContPifRam.pifstatus = CONT_CMD_EXE;
    readformat.txsize = CONT_CMD_GCN_READORIGIN_TX;
    readformat.rxsize = CONT_CMD_GCN_READORIGIN_RX;
    readformat.cmd = CONT_CMD_GCN_READORIGIN;
    readformat.button = 0;
    readformat.origin.stick_x = 128;
    readformat.origin.stick_y = 128;
    readformat.origin.c_stick_x = 128;
    readformat.origin.c_stick_y = 128;
    readformat.origin.l_trig = 0;
    readformat.origin.r_trig = 0;
    readformat.origin.a = 0;
    readformat.origin.b = 0;

    for (i = 0; i < __osMaxControllers; i++) {
        *(__OSContGCNOriginFormat*)ptr = readformat;
        ptr += sizeof(__OSContGCNOriginFormat);
    }

    __osSiRawStartDma(OS_WRITE, __osContPifRam.ramarray);
    osRecvMesg(mq, &dummy, OS_MESG_BLOCK);

    __osSiRawStartDma(OS_READ, __osContPifRam.ramarray);
    osRecvMesg(mq, &dummy, OS_MESG_BLOCK);
}
#endif

void __osContGetInitData(u8* pattern, OSContStatus* data) {
    u8* ptr;
    __OSContRequesFormat requestHeader;
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

u16 __osTranslateGCNButtons(u16 input, s8 c_stick_x, s8 c_stick_y) {
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

