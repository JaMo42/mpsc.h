# mpsc.h

Single-header multi producer single consumer queue.

## Example

```c
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
```

See `example_no_macros.c` for an equivalent example without using the macros.

## Requirements

Only tested with gcc13 and clang17.

To build the tests [smallunit](github.com/JaMo42/smallunit) is required.

Required standard:

- Using macros: `c23` (if not using `-pedantic` can probably also get away with lower versions, especially with the `gnu` versions)
- Just using the functions directly: `c11` (`c99` very likely works too, just need `<threads.h>` from C11 to exist)

## Design

The way the macro-based api was designed wasn't done with care but is just the first thing I came up with that works, there are some oddities about it such as macros expecting variable identifiers instead of values but I won't spend time reworking it as I don't think it's neccessary because it's good enough for me and the base api without macros can be used without issues as well.

The implementation is also kept simple and straight forward and is not the most optimized, but easy to modify and adjust.

## Quirks

Since the queues ownership is shared between all senders/receivers, a closed queue is still kept alive and can be re-opened by creating new senders/receivers to it despite all senders/receivers having been dropped previously, unlike for example in rust where once all senders/receivers are dropped the queue is closed for good.
The queue is only deleted once all senders and receivers are dropped.
I don't know if this is every useful and it's not a consicous design decision but just how the implementation works.

## License

2-Clause BSD License, it's in `mpsc.h` at the bottom.