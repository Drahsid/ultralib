#include "PR/os_internal.h"
#include "controller.h"
#include "siint.h"

static void __osPackReadData(void);

s32 osContStartReadData(OSMesgQueue* mq) {
    s32 ret = 0;

    __osSiGetAccess();

    if (__osContLastCmd != CONT_CMD_READ_BUTTON) {
        __osPackReadData();
        ret = __osSiRawStartDma(OS_WRITE, __osContPifRam.ramarray);
        osRecvMesg(mq, NULL, OS_MESG_BLOCK);
    }

    ret = __osSiRawStartDma(OS_READ, __osContPifRam.ramarray);
    __osContLastCmd = CONT_CMD_READ_BUTTON;
    __osSiRelAccess();

    return ret;
}

void osContGetReadData(OSContPad* data) {
    u8* ptr = (u8*)__osContPifRam.ramarray;
    __OSContReadFormat readformat;
    __OSContGCNShortPollFormat readformatgcn;
    int i;

    for (i = 0; i < __osMaxControllers; i++, data++) {
        if (__osControllerTypes[i] == CONT_TYPE_GCN) {
            s32 stick_x, stick_y, c_stick_x, c_stick_y;
            readformatgcn = *(__OSContGCNShortPollFormat*)ptr;
            stick_x = ((s32)readformatgcn.stick_x) - 128;
            stick_y = ((s32)readformatgcn.stick_y) - 128;
            data->stick_x = stick_x;
            data->stick_y = stick_y;
            c_stick_x = ((s32)readformatgcn.c_stick_x) - 128;
            c_stick_y = ((s32)readformatgcn.c_stick_y) - 128;
            data->c_stick_x = c_stick_x;
            data->c_stick_y = c_stick_y;
            data->button = __osTranslateGCNButtons(readformatgcn.button, c_stick_x, c_stick_y);
            data->l_trig = readformatgcn.l_trig;
            data->r_trig = readformatgcn.r_trig;
            ptr += sizeof(__OSContGCNShortPollFormat);
        }
        else {
            readformat = *(__OSContReadFormat*)ptr;
            data->errno = CHNL_ERR(readformat);
            
            if (data->errno != 0) {
                continue;
            }

            data->stick_x = readformat.stick_x;
            data->stick_y = readformat.stick_y;
            data->button = readformat.button;
            data->c_stick_x = 0;
            data->c_stick_y = 0;
            data->l_trig = 0;
            data->r_trig = 0;
            ptr += sizeof(__OSContReadFormat);
        }
    }
}

static void __osPackReadData(void) {
    u8* ptr = (u8*)__osContPifRam.ramarray;
    __OSContReadFormat readformat;
    __OSContGCNShortPollFormat readformatgcn;
    int i;

    for (i = 0; i < ARRLEN(__osContPifRam.ramarray); i++) {
        __osContPifRam.ramarray[i] = 0;
    }

    __osContPifRam.pifstatus = CONT_CMD_EXE;
    readformat.dummy = CONT_CMD_NOP;
    readformat.txsize = CONT_CMD_READ_BUTTON_TX;
    readformat.rxsize = CONT_CMD_READ_BUTTON_RX;
    readformat.cmd = CONT_CMD_READ_BUTTON;
    readformat.button = 0xFFFF;
    readformat.stick_x = -1;
    readformat.stick_y = -1;

    readformatgcn.dummy = CONT_CMD_NOP;
    readformatgcn.txsize = CONT_CMD_GCN_SHORTPOLL_TX;
    readformatgcn.rxsize = CONT_CMD_GCN_SHORTPOLL_RX;
    readformatgcn.cmd = CONT_CMD_GCN_SHORTPOLL;
    readformatgcn.analog_mode = 3;
    readformatgcn.rumble = 0;
    readformatgcn.button = 0xFFFF;
    readformatgcn.stick_x = -1;
    readformatgcn.stick_y = -1;

    for (i = 0; i < __osMaxControllers; i++) {
        if (__osControllerTypes[i] == CONT_TYPE_GCN) {
            readformatgcn.rumble = __osGamecubeRumbleEnabled[i];
            *(__OSContGCNShortPollFormat*)ptr = readformatgcn;
            ptr += sizeof(__OSContGCNShortPollFormat);
        }
        else {
            *(__OSContReadFormat*)ptr = readformat;
            ptr += sizeof(__OSContReadFormat);
        }
    }
    
    *ptr = CONT_CMD_END;
}
