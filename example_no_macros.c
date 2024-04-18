#include <stdio.h>

#define MPSC_IMPLEMENTATION
#include "mpsc.h"

int worker(struct mpsc_sender *sender) {
    const char *msg1 = "Hello, world!";
    const char *msg2 = "Delayed for 2 seconds";
    const struct timespec ts = {2, 0};
    mpsc_sender_send(sender, &msg1);
    thrd_sleep(&ts, NULL);
    mpsc_sender_send(sender, &msg2);
    mpsc_sender_drop(sender);
    return 0;
}

int main(void) {
    struct mpsc_sender *tx;
    struct mpsc_receiver *rx;
    mpsc_channel(&tx, &rx, sizeof(char*));

    thrd_t thread;
    thrd_create(&thread, (thrd_start_t)worker, tx);

    const char *msg;
    mpsc_receiver_recv(rx, &msg); // immediately
    puts(msg);
    mpsc_receiver_recv(rx, &msg); // after 2 seconds
    puts(msg);

    thrd_join(thread, NULL);
    mpsc_receiver_drop(rx);
}
