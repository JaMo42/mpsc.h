// This is only used for better assertion messages in smallunit and is optional.
#if __has_include(<fmt.h>)
#define FMT_IMPLEMENTATION
#include <fmt.h>
#endif
#include <smallunit.h>

#define MPSC_IMPLEMENTATION
#include "mpsc.h"

#define MAKETS(ms) { (ms)/1000, ((ms)%1000) * 1000000 }

// A short delay, long enough to always give correct results,
// but short enough to not make tests too slow.
#define SHORTMS 250
static const struct timespec SHORT = MAKETS(SHORTMS);
static const struct timespec LONGER = MAKETS(SHORTMS*2);

static const int VALUE = 12;
static const int NONE = -1;

su_module(sync, {
    SENDER(int) tx;
    RECEIVER(int) rx;
    int i = NONE;

    su_test("simple send and recv", {
        MPSC_CHANNEL(tx, rx);
        su_assert_eq(MPSC_SEND(tx, VALUE), mpsc_OK);
        MPSC_DROP_SENDER(tx);
        i = NONE;
        su_assert_eq(MPSC_RECV(rx, i), mpsc_OK);
        su_assert(i == VALUE);
        MPSC_DROP_RECEIVER(rx);
    })

    su_test("try recv", {
        MPSC_CHANNEL(tx, rx);
        i = NONE;
        su_assert_eq(MPSC_TRY_RECV(rx, i), mpsc_EMPTY);
        su_assert_eq(i, NONE);
        su_assert_eq(MPSC_SEND(tx, VALUE), mpsc_OK);
        su_assert_eq(MPSC_TRY_RECV(rx, i), mpsc_OK);
        su_assert_eq(i, VALUE);
        MPSC_DROP_SENDER(tx);
        i = NONE;
        su_assert_eq(MPSC_TRY_RECV(rx, i), mpsc_CLOSED);
        su_assert_eq(i, NONE);
        MPSC_DROP_RECEIVER(rx);
    })

    su_test("send on closed channel", {
        MPSC_CHANNEL(tx, rx);
        MPSC_DROP_RECEIVER(rx);
        su_assert_eq(MPSC_SEND(tx, VALUE), mpsc_CLOSED);
        MPSC_DROP_SENDER(tx);
    })

    su_test("recv on closed channel", {
        MPSC_CHANNEL(tx, rx);
        MPSC_DROP_SENDER(tx);
        su_assert_eq(MPSC_RECV(rx, i), mpsc_CLOSED);
        MPSC_DROP_RECEIVER(rx);
    })

    su_test("recv left over data on closed channel", {
        MPSC_CHANNEL(tx, rx);
        su_assert_eq(MPSC_SEND(tx, VALUE), mpsc_OK);
        MPSC_DROP_SENDER(tx);
        i = NONE;
        su_assert_eq(MPSC_RECV(rx, i), mpsc_OK);
        su_assert_eq(i, VALUE);
        su_assert_eq(MPSC_RECV(rx, i), mpsc_CLOSED);
        MPSC_DROP_RECEIVER(rx);
    })

    su_test("re-open", {
        MPSC_CHANNEL(tx, rx);
        MPSC_DROP_RECEIVER(rx);
        su_assert_eq(MPSC_SEND(tx, VALUE), mpsc_CLOSED);
        rx = MPSC_NEW_RECEIVER_FOR(tx);
        su_assert_eq(MPSC_SEND(tx, VALUE), mpsc_OK);
        i = NONE;
        su_assert_eq(MPSC_RECV(rx, i), mpsc_OK);
        su_assert_eq(i, VALUE);

        MPSC_DROP_SENDER(tx);
        i = NONE;
        su_assert_eq(MPSC_RECV(rx, i), mpsc_CLOSED);
        su_assert_eq(i, NONE);
        tx = MPSC_NEW_SENDER_FOR(rx);
        su_assert_eq(MPSC_SEND(tx, VALUE), mpsc_OK);
        i = NONE;
        su_assert_eq(MPSC_RECV(rx, i), mpsc_OK);
        su_assert_eq(i, VALUE);
        MPSC_DROP_RECEIVER(rx);
        MPSC_DROP_SENDER(tx);
    })
});

int send_data_immidiately(SENDER(int) tx) {
    MPSC_SEND(tx, VALUE);
    MPSC_DROP_SENDER(tx);
    return 0;
}

int send_data_after_short_delay(SENDER(int) tx) {
    thrd_sleep(&SHORT, NULL);
    MPSC_SEND(tx, VALUE);
    MPSC_DROP_SENDER(tx);
    return 0;
}

int send_data_after_longer_delay(SENDER(int) tx) {
    thrd_sleep(&LONGER, NULL);
    MPSC_SEND(tx, VALUE);
    MPSC_DROP_SENDER(tx);
    return 0;
}

int drop_sender_after_short_delay(SENDER(int) tx) {
    thrd_sleep(&SHORT, NULL);
    MPSC_DROP_SENDER(tx);
    return 0;
}

su_module(async, {
    SENDER(int) tx;
    RECEIVER(int) rx;
    int i = NONE;
    thrd_t thread;

    su_test("wait for data", {
        MPSC_CHANNEL(tx, rx);
        thrd_create(&thread, (thrd_start_t)send_data_after_short_delay, tx);
        i = NONE;
        su_assert_eq(MPSC_RECV(rx, i), mpsc_OK);
        su_assert_eq(i, VALUE);
        MPSC_DROP_RECEIVER(rx);
        thrd_join(thread, NULL);
    })

    su_test("timeout", {
        MPSC_CHANNEL(tx, rx);
        thrd_create(&thread, (thrd_start_t)send_data_after_longer_delay, tx);
        i = NONE;
        su_assert_eq(MPSC_RECV_TIMEOUT(rx, i, &SHORT), mpsc_TIMEOUT);
        su_assert_eq(i, NONE);
        MPSC_DROP_RECEIVER(rx);
        thrd_join(thread, NULL);
    })

    su_test("drop sender during recv", {
        MPSC_CHANNEL(tx, rx);
        thrd_create(&thread, (thrd_start_t)drop_sender_after_short_delay, tx);
        i = NONE;
        su_assert_eq(MPSC_RECV(rx, i), mpsc_CLOSED);
        su_assert_eq(i, NONE);
        MPSC_DROP_RECEIVER(rx);
        thrd_join(thread, NULL);
    })

    su_test("lots of senders", {
        enum { COUNT = 100 };
        thrd_t *threads = (thrd_t *)calloc(COUNT, sizeof(thrd_t));
        MPSC_CHANNEL(tx, rx);
        for (int t = 0; t < COUNT; t++) {
            thrd_create(&threads[t], (thrd_start_t)send_data_immidiately, MPSC_CLONE(tx));
        }
        MPSC_DROP_SENDER(tx);
        i = NONE;
        // note: valgrind checks makes using MPSC_RECV_TIMEOUT impractical
        // since it slows the program down too much to be reliable.
        int count = 0;
        enum mpsc_error res;
        for (;;) {
            res = MPSC_RECV(rx, i);
            if (res == mpsc_OK) {
                su_assert_eq(i, VALUE);
                i = NONE;
                ++count;
            } else if (res == mpsc_CLOSED) {
                break;
            } else {
                const char *result = mpsc_error_message(res);
                su_bad_value(result, "should be OK or CLOSED");
            }
        }
        su_assert_eq(count, COUNT);
        MPSC_DROP_RECEIVER(rx);
        for (int t = 0; t < COUNT; t++) {
            thrd_join(threads[t], NULL);
        }
        free(threads);
    })
});

int main(void) {
    SUResult res = su_new_result();
    su_add_result(&res, su_run_module(sync));
    su_add_result(&res, su_run_module(async));
    fmt_println("Total:");
    su_print_result(&res);
}
