/* https://github.com/JaMo42/mpsc-c */
#ifndef MPSC_H
#define MPSC_H
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <threads.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Result of a send or receive operation. Non-zero is an error.
enum mpsc_error {
    /// No error.
    mpsc_OK = 0,
    /// Returned by send or receive function is the other half of the channel
    /// is disconnected.
    mpsc_CLOSED,
    /// Returned by `MPSC_TRY_RECV` if there is no data to receive.
    mpsc_EMPTY,
    /// Returned by `MPSC_RECV_TIMEOUT` if the timeout is reached.
    mpsc_TIMEOUT,
};

struct mpsc_queue_node {
    struct mpsc_queue_node *next;
    size_t size;
    char data[];
};

struct mpsc_queue {
    struct mpsc_queue_node *head;
    struct mpsc_queue_node *tail;
    mtx_t mutex;
    cnd_t cond;
    atomic_size_t senders;
    atomic_size_t receivers;
};

struct mpsc_shared_queue_inner {
    struct mpsc_queue *queue;
    atomic_size_t refcount;
};

struct mpsc_shared_queue {
    struct mpsc_shared_queue_inner *inner;
};

struct mpsc_sender {
    struct mpsc_shared_queue queue;
};

struct mpsc_receiver {
    struct mpsc_shared_queue queue;
};

#define MPSC__STATIC_ASSERT_EXPR(_expr, _msg) \
    (sizeof(struct { _Static_assert((_expr), _msg); char _; }))

#define MPSC__TYPECHECK(_s_or_r_ident, _tident) \
    ((void)MPSC__STATIC_ASSERT_EXPR( \
        __builtin_types_compatible_p(typeof(*_s_or_r_ident), typeof(*_tident)), \
        "channel/target type mismatch" \
    ))

/// `T*`, used to show intent.
#define SENDER(T) T*

/// `T*`, used to show intent.
#define RECEIVER(T) T*

/// Creates a new channel, first argument is the sender, second is the receiver.
/// The two paramters should be pointers to the type of the channel.
///
/// Example
/// -------
/// ```c
/// int *sender, *receiver;
/// MPSC_CHANNEL(sender, receiver);
/// ```
#define MPSC_CHANNEL(_sident, _rident) \
    ( \
        _rident = (typeof(_rident))mpsc_receiver_new(mpsc_shared_queue_new()), \
        _sident = (typeof(_sident))mpsc_sender_new( \
            mpsc_shared_queue_clone(((struct mpsc_receiver*)_rident)->queue) \
        ) \
    )

/// Creates a new sender for the channel of the given receiver.
///
/// Example
/// -------
/// ```c
/// int *sender, *receiver;
/// MPSC_CHANNEL(sender, receiver);
/// MPSC_DROP_SENDER(sender);  // channel is closed
/// sender = MPSC_NEW_SENDER_FOR(receiver);  // channel is re-opened
/// ```
#define MPSC_NEW_SENDER_FOR(_rident) \
    ((typeof(_rident))mpsc_sender_new( \
        mpsc_shared_queue_clone(((struct mpsc_receiver*)_rident)->queue) \
    ))

/// Creates a new receiver for the channel of the given sender.
/// Note that multiple receivers should not exist so this is almost never
/// something that should be used, but it can re-open a closed channel.
///
/// Example
/// -------
/// ```c
/// int *sender, *receiver;
/// MPSC_CHANNEL(sender, receiver);
/// MPSC_DROP_RECEIVER(receiver);  // channel is closed
/// receiver = MPSC_NEW_RECEIVER_FOR(sender);  // channel is re-opened
/// ```
#define MPSC_NEW_RECEIVER_FOR(_sident) \
    ((typeof(_sident))mpsc_receiver_new( \
        mpsc_shared_queue_clone(((struct mpsc_sender*)_sident)->queue) \
    ))

/// Clones a sender.
///
/// Example
/// -------
/// ```c
/// int *sender, *receiver;
/// MPSC_CHANNEL(sender, receiver);
/// int *sender2 = MPSC_CLONE(sender);
/// run_thread(sender);
/// run_thread(sender2);
/// ```
#define MPSC_CLONE(_sident) \
    ((typeof(_sident))mpsc_sender_clone((struct mpsc_sender*)_sident))

/// Drops a sender.
#define MPSC_DROP_SENDER(_sident) \
    (mpsc_sender_drop((struct mpsc_sender*)_sident), _sident = NULL)

/// Drops a receiver.
#define MPSC_DROP_RECEIVER(_rident) \
    (mpsc_receiver_drop((struct mpsc_receiver*)_rident), _rident = NULL)

// Note: the NOLINTNEXTLINE is required for clang-tidy if _data is a pointer
//       since it gets suspicous at sizeof of a pointer.

/// Sends data over the channel.  The data parameter is the identifier of a
/// value, not a pointer to it, and not a literal.  Returns mpsc_CLOSED if
/// the other half of the channel is disconnected.
///
/// Example
/// -------
/// ```c
/// int *sender, *receiver;
/// MPSC_CHANNEL(sender, receiver);
/// int data = 12;
/// MPSC_SEND(sender, data);
/// // MPSC_SEND(sender, 12);  // error: data is a literal
/// // MPSC_SEND(sender, &data);  // error: data is a pointer to the target
/// ```
#define MPSC_SEND(_sident, _data) \
    (MPSC__TYPECHECK(_sident, &_data), \
    /* NOLINTNEXTLINE */\
    mpsc_sender_send((struct mpsc_sender*)_sident, (void*)&_data, sizeof(_data)))

/// Receives data over the channel.  The data parameter is the identifier of a
/// value, not a pointer to it.  Returns mpsc_CLOSED if the other half of the
/// channel is disconnected, and leaves the data unchanged.
///
/// Example
/// -------
/// ```c
/// int *sender, *receiver;
/// MPSC_CHANNEL(sender, receiver);
/// // do something that sends data...
/// int data;
/// MPSC_RECV(receiver, data);
/// // MPSC_RECV(receiver, &data);  // error: data is a pointer to the target
/// ```
#define MPSC_RECV(_rident, _data) \
    (MPSC__TYPECHECK(_rident, &_data), \
    /* NOLINTNEXTLINE */\
    mpsc_receiver_recv((struct mpsc_receiver*)_rident, (void*)&_data, sizeof(_data)))

/// Tries to receive data over the channel if there is any, returns mpsc_EMPTY
/// otherwise.  See MPSC_RECV for more information.
#define MPSC_TRY_RECV(_rident, _data) \
    (MPSC__TYPECHECK(_rident, &_data), \
    /* NOLINTNEXTLINE */\
    mpsc_receiver_try_recv((struct mpsc_receiver*)_rident, (void*)&_data, sizeof(_data)))

/// Receives data over the channel with a timeout, returns mpsc_TIMEOUT if the
/// timeout is reached.  See MPSC_RECV for more information.
#define MPSC_RECV_TIMEOUT(_rident, _data, _timeout) \
    (MPSC__TYPECHECK(_rident, &_data), \
    /* NOLINTNEXTLINE */\
    mpsc_receiver_recv_timeout((struct mpsc_receiver*)_rident, (void*)&_data, sizeof(_data), _timeout))

/// Returns a string representation of the error.
const char* mpsc_error_message(enum mpsc_error err);

struct mpsc_queue* mpsc_queue_new(void);
void mpsc_queue_drop(struct mpsc_queue *queue);
void mpsc_queue_push(struct mpsc_queue *queue, const void *data, size_t size);
enum mpsc_error mpsc_queue_pop(struct mpsc_queue *queue, void *data, size_t size);

struct mpsc_shared_queue mpsc_shared_queue_new(void);
struct mpsc_queue* mpsc_shared_queue_get(struct mpsc_shared_queue shared_queue);
struct mpsc_shared_queue mpsc_shared_queue_clone(struct mpsc_shared_queue shared_queue);
void mpsc_shared_queue_drop(struct mpsc_shared_queue shared_queue);

void mpsc_channel(struct mpsc_sender **tx, struct mpsc_receiver **rx);

struct mpsc_receiver* mpsc_receiver_new(struct mpsc_shared_queue queue);
void mpsc_receiver_drop(struct mpsc_receiver *receiver);
enum mpsc_error mpsc_receiver_recv(struct mpsc_receiver *receiver, void *data, size_t size);
enum mpsc_error mpsc_receiver_try_recv(struct mpsc_receiver *receiver, void *data, size_t size);
enum mpsc_error mpsc_receiver_recv_timeout(
    struct mpsc_receiver *receiver, void *data, size_t size, const struct timespec *timeout);

struct mpsc_sender* mpsc_sender_new(struct mpsc_shared_queue queue);
struct mpsc_sender* mpsc_sender_clone(struct mpsc_sender *sender);
void mpsc_sender_drop(struct mpsc_sender *sender);
enum mpsc_error mpsc_sender_send(struct mpsc_sender *sender, const void *data, size_t size);
#endif



#ifdef MPSC_IMPLEMENTATION
const char* mpsc_error_message(enum mpsc_error err) {
    switch (err) {
        case mpsc_OK: return "OK";
        case mpsc_CLOSED: return "CLOSED";
        case mpsc_EMPTY: return "EMPTY";
        case mpsc_TIMEOUT: return "TIMEOUT";
    }
    __builtin_unreachable();
}

struct mpsc_queue* mpsc_queue_new(void) {
    struct mpsc_queue *q = (struct mpsc_queue*)malloc(sizeof(*q));
    q->head = q->tail = NULL;
    mtx_init(&q->mutex, mtx_plain);
    cnd_init(&q->cond);
    atomic_init(&q->senders, 0);
    atomic_init(&q->receivers, 0);
    return q;
}

void mpsc_queue_drop(struct mpsc_queue *queue) {
    mtx_destroy(&queue->mutex);
    cnd_destroy(&queue->cond);
    if (queue->head) {
        struct mpsc_queue_node *node, *next;
        node = queue->head;
        while (node) {
            next = node->next;
            free(node);
            node = next;
        }
        queue->head = NULL;
        queue->tail = NULL;
    }
    free(queue);
}

void mpsc_queue_push(struct mpsc_queue *queue, const void *data, size_t size) {
    struct mpsc_queue_node *node
        = (struct mpsc_queue_node*)malloc(sizeof(*node) + size);
    memcpy(node->data, data, size);
    node->size = size;
    node->next = NULL;
    mtx_lock(&queue->mutex);
    if (queue->tail) {
        queue->tail->next = node;
    } else {
        queue->head = node;
    }
    queue->tail = node;
    mtx_unlock(&queue->mutex);
    cnd_signal(&queue->cond);
}

static int mpsc_queue_closed(struct mpsc_queue *queue) {
    return queue->senders == 0 || queue->receivers == 0;
}

static int mpsc_queue_closed_and_empty(struct mpsc_queue *queue) {
    return mpsc_queue_closed(queue) && !queue->head;
}

enum mpsc_error mpsc_queue_pop(struct mpsc_queue *queue, void *data, size_t size) {
    mtx_lock(&queue->mutex);
    while (!queue->head && !mpsc_queue_closed(queue)) {
        cnd_wait(&queue->cond, &queue->mutex);
    }
    if (mpsc_queue_closed_and_empty(queue)) {
        mtx_unlock(&queue->mutex);
        return mpsc_CLOSED;
    }
    struct mpsc_queue_node *node = queue->head;
    queue->head = node->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    mtx_unlock(&queue->mutex);
    if (node->size != size) {
        fprintf(
            stderr,
            "mpsc: warning: size does not match data size "
            "(contains %zu bytes, requested %zu).\n",
            node->size,
            size
        );
    }
    memcpy(data, node->data, size);
    free(node);
    cnd_signal(&queue->cond);
    return mpsc_OK;
}

struct mpsc_shared_queue mpsc_shared_queue_new(void) {
    struct mpsc_shared_queue shared_queue;
    shared_queue.inner
        = (struct mpsc_shared_queue_inner*)malloc(sizeof(*shared_queue.inner));
    shared_queue.inner->queue = mpsc_queue_new();
    atomic_init(&shared_queue.inner->refcount, 1);
    return shared_queue;
}

struct mpsc_queue* mpsc_shared_queue_get(struct mpsc_shared_queue shared_queue) {
    return shared_queue.inner->queue;
}

struct mpsc_shared_queue mpsc_shared_queue_clone(struct mpsc_shared_queue shared_queue) {
    atomic_fetch_add(&shared_queue.inner->refcount, 1);
    return shared_queue;
}

void mpsc_shared_queue_drop(struct mpsc_shared_queue shared_queue) {
    if (atomic_fetch_sub(&shared_queue.inner->refcount, 1) == 1) {
        mpsc_queue_drop(shared_queue.inner->queue);
        shared_queue.inner->queue = NULL;
        free(shared_queue.inner);
    }
}

void mpsc_channel(struct mpsc_sender **tx, struct mpsc_receiver **rx) {
    struct mpsc_shared_queue queue = mpsc_shared_queue_new();
    *rx = mpsc_receiver_new(mpsc_shared_queue_clone(queue));
    *tx = mpsc_sender_new(queue);
}

struct mpsc_receiver* mpsc_receiver_new(struct mpsc_shared_queue queue) {
    struct mpsc_receiver *r = (struct mpsc_receiver*)malloc(sizeof(*r));
    r->queue = queue;
    if (atomic_fetch_add(&mpsc_shared_queue_get(queue)->receivers, 1) > 0) {
        fprintf(stderr, "mpsc: warning: got multiple receivers for the same queue\n");
    }
    return r;
}

void mpsc_receiver_drop(struct mpsc_receiver *receiver) {
    struct mpsc_queue *queue = mpsc_shared_queue_get(receiver->queue);
    if (atomic_fetch_sub(&queue->receivers, 1) == 1) {
        cnd_signal(&queue->cond);
    }
    mpsc_shared_queue_drop(receiver->queue);
    memset(receiver, 0, sizeof(*receiver));
    free(receiver);
}

enum mpsc_error mpsc_receiver_recv(struct mpsc_receiver *receiver, void *data, size_t size) {
    struct mpsc_queue *q = mpsc_shared_queue_get(receiver->queue);
    if (mpsc_queue_closed_and_empty(q)) { return mpsc_CLOSED; }
    return mpsc_queue_pop(q, data, size);
}

enum mpsc_error mpsc_receiver_try_recv(
    struct mpsc_receiver *receiver, void *data, size_t size
) {
    struct mpsc_queue *q = mpsc_shared_queue_get(receiver->queue);
    if (mpsc_queue_closed_and_empty(q)) { return mpsc_CLOSED; }
    mtx_lock(&q->mutex);
    if (!q->head) {
        mtx_unlock(&q->mutex);
        return mpsc_EMPTY;
    }
    mtx_unlock(&q->mutex);
    return mpsc_queue_pop(q, data, size);
}

enum mpsc_error mpsc_receiver_recv_timeout(
    struct mpsc_receiver *receiver, void *data, size_t size, const struct timespec *timeout
) {
    struct mpsc_queue *q = mpsc_shared_queue_get(receiver->queue);
    if (mpsc_queue_closed_and_empty(q)) { return mpsc_CLOSED; }
    mtx_lock(&q->mutex);
    while (!q->head) {
        if (cnd_timedwait(&q->cond, &q->mutex, timeout) == thrd_timedout) {
            mtx_unlock(&q->mutex);
            return mpsc_TIMEOUT;
        }
    }
    mtx_unlock(&q->mutex);
    return mpsc_queue_pop(q, data, size);
}

struct mpsc_sender* mpsc_sender_new(struct mpsc_shared_queue queue) {
    struct mpsc_sender *s = (struct mpsc_sender*)malloc(sizeof(*s));
    s->queue = queue;
    atomic_fetch_add(&mpsc_shared_queue_get(queue)->senders, 1);
    return s;
}

struct mpsc_sender* mpsc_sender_clone(struct mpsc_sender *sender) {
    return mpsc_sender_new(mpsc_shared_queue_clone(sender->queue));
}

void mpsc_sender_drop(struct mpsc_sender *sender) {
    struct mpsc_queue *queue = mpsc_shared_queue_get(sender->queue);
    if (atomic_fetch_sub(&queue->senders, 1) == 1) {
        cnd_signal(&queue->cond);
    }
    mpsc_shared_queue_drop(sender->queue);
    memset(sender, 0, sizeof(*sender));
    free(sender);
}

enum mpsc_error mpsc_sender_send(struct mpsc_sender *sender, const void *data, size_t size) {
    struct mpsc_queue *q = mpsc_shared_queue_get(sender->queue);
    if (mpsc_queue_closed(q)) { return mpsc_CLOSED; }
    mpsc_queue_push(q, data, size);
    return mpsc_OK;
}
#endif

#ifdef __cplusplus
}
#endif

// Copyright 2024 Jakob Mohrbacher
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
