#include <stdio.h>

#define MPSC_IMPLEMENTATION
#include "mpsc.h"

int worker(SENDER(const char *) sender) {
    // Note: we're sending the actual pointer to the message here, not the
    // strings contents, this works of course because it's a string literal
    // but keep in mind that stack data cannot be sent this way.
    const char *msg1 = "Hello, world!";
    const char *msg2 = "Delayed for 2 seconds";
    struct timespec ts = {2, 0};
    MPSC_SEND(sender, msg1);
    thrd_sleep(&ts, NULL);
    MPSC_SEND(sender, msg2);
    MPSC_DROP_SENDER(sender);
    return 0;
}

int main(void) {
    const char **tx, **rx;
    MPSC_CHANNEL(tx, rx);

    thrd_t thread;
    thrd_create(&thread, (thrd_start_t)worker, tx);

    const char *msg;
    MPSC_RECV(rx, msg); // immediately
    puts(msg);
    MPSC_RECV(rx, msg); // after 2 seconds
    puts(msg);

    thrd_join(thread, NULL);
    MPSC_DROP_RECEIVER(rx);
}
