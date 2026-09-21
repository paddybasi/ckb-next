#include "bragi_notification.h"
#include "bragi_common.h"

typedef struct {
    usbdevice* kb;
    uchar prop;
} bragithreadctx;

// This runs on a new thread so that the input thread isn't blocked
static void* _add_remove_new_bragi_device(void* context){
    bragithreadctx* ctx = context;
    usbdevice* kb = ctx->kb;
    ckb_info("ckb%d: bragi dongle hotplug thread started", INDEX_OF(kb, keyboard));

    // bragi_update_dongle_subdevs() performs physical USB I/O on the dongle (routed
    // through kb for each child it probes). dmutex must be held while accessing the
    // USB interface (see usb.h), but connection-status notifications can arrive in a
    // burst, spawning multiple concurrent instances of this thread. Without this lock
    // they race on the dongle's shared endpoint, stepping on each other's requests and
    // responses, which manifests as a flood of "Timeout while waiting for response"
    // and subdevices repeatedly appearing/disappearing instead of ever settling.
    queued_mutex_lock(dmutex(kb));
    bragi_update_dongle_subdevs(kb, ctx->prop);
    queued_mutex_unlock(dmutex(kb));

    free(context);
    ckb_info("ckb%d: bragi dongle hotplug thread finished", INDEX_OF(kb, keyboard));
    return NULL;
}

void bragi_process_notification(usbdevice* kb, usbdevice* subkb, const uchar* const buffer){
    // Do NOT lock dmutex here unless it's required in very specific notifications
    // Doing so will result in devices failing to initialise

    // We'll just assume that no device other than a dongle ever sends BRAGI_NOTIFICATION_CONNECTIONSTATUS
    if(buffer[2] == BRAGI_NOTIFICATION_CONNECTIONSTATUS){
        bragithreadctx* ctx = malloc(sizeof(bragithreadctx));
        ctx->kb = kb;
        ctx->prop = buffer[4];

        pthread_t t;
        pthread_create(&t, NULL, _add_remove_new_bragi_device, ctx);
        pthread_detach(t);
    } else {
        ckb_warn("ckb%d: Unknown bragi notification for dev %d", INDEX_OF(kb, keyboard), INDEX_OF(subkb, keyboard));
    }
}
