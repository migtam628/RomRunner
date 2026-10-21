/* Optional RDP worker thread.
 *
 * On the console the RDP draws in parallel with the CPU and RSP; the only
 * thing the rest of the machine waits for is the DP interrupt raised by
 * SYNC_FULL. So when enabled, rdp_run copies submitted command words into a
 * queue and returns at once; a worker thread executes them, and the
 * emulation thread raises the DP interrupt once the worker has passed each
 * SYNC_FULL (rdp_thread_poll, called every scheduler slice).
 *
 * Off by default (tests stay deterministic); the libretro core turns it on.
 * One worker per process, which is all libretro needs. */
#include <stdlib.h>
#include <string.h>

#include "n64.h"

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION rr_mutex;
typedef CONDITION_VARIABLE rr_cond;
typedef HANDLE rr_thread;
static void mtx_init_(rr_mutex *m) { InitializeCriticalSection(m); }
static void mtx_lock_(rr_mutex *m) { EnterCriticalSection(m); }
static void mtx_unlock_(rr_mutex *m) { LeaveCriticalSection(m); }
static void mtx_destroy_(rr_mutex *m) { DeleteCriticalSection(m); }
static void cond_init_(rr_cond *c) { InitializeConditionVariable(c); }
static void cond_wait_(rr_cond *c, rr_mutex *m) { SleepConditionVariableCS(c, m, INFINITE); }
static void cond_signal_(rr_cond *c) { WakeAllConditionVariable(c); }
static void cond_destroy_(rr_cond *c) { (void)c; }
static DWORD WINAPI worker_main(LPVOID arg);
static bool thread_start_(rr_thread *t) { *t = CreateThread(NULL, 0, worker_main, NULL, 0, NULL); return *t != NULL; }
static void thread_join_(rr_thread t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#else
#include <pthread.h>
typedef pthread_mutex_t rr_mutex;
typedef pthread_cond_t rr_cond;
typedef pthread_t rr_thread;
static void mtx_init_(rr_mutex *m) { pthread_mutex_init(m, NULL); }
static void mtx_lock_(rr_mutex *m) { pthread_mutex_lock(m); }
static void mtx_unlock_(rr_mutex *m) { pthread_mutex_unlock(m); }
static void mtx_destroy_(rr_mutex *m) { pthread_mutex_destroy(m); }
static void cond_init_(rr_cond *c) { pthread_cond_init(c, NULL); }
static void cond_wait_(rr_cond *c, rr_mutex *m) { pthread_cond_wait(c, m); }
static void cond_signal_(rr_cond *c) { pthread_cond_broadcast(c); }
static void cond_destroy_(rr_cond *c) { pthread_cond_destroy(c); }
static void *worker_main(void *arg);
static bool thread_start_(rr_thread *t) { return pthread_create(t, NULL, worker_main, NULL) == 0; }
static void thread_join_(rr_thread t) { pthread_join(t, NULL); }
#endif

#define QUEUE_WORDS (1u << 20)   /* 8 MB of command words */

static struct {
  bool running;
  N64 *n;
  rr_thread thread;
  rr_mutex lock;
  rr_cond changed;           /* words added / consumed / stop requested */
  uint64_t *q;
  uint32_t head, tail;       /* consume at head, produce at tail (mod QUEUE_WORDS) */
  bool busy;                 /* the worker is executing a batch */
  bool stop;
  uint32_t batch_syncs;      /* SYNC_FULLs in the batch being drawn (worker only) */
  volatile uint32_t syncs;   /* SYNC_FULLs drawn, published under the lock */
  uint32_t syncs_seen;       /* ... and turned into DP interrupts */
} W;

bool rdp_thread_active(void) { return W.running; }

/* Called from rdp.c for SYNC_FULL. */
void rdp_sync_full(N64 *n) {
  if (W.running) {
    W.batch_syncs++;   /* published after the batch, so its drawing is visible first */
  } else {
    n64_raise_mi(n, MI_INTR_DP);
  }
}

#ifdef _WIN32
static DWORD WINAPI worker_main(LPVOID arg)
#else
static void *worker_main(void *arg)
#endif
{
  (void)arg;
  static uint64_t batch[4096];
  mtx_lock_(&W.lock);
  for (;;) {
    while (W.head == W.tail && !W.stop) cond_wait_(&W.changed, &W.lock);
    if (W.head == W.tail && W.stop) break;
    uint32_t count = 0;
    while (W.head != W.tail && count < sizeof batch / sizeof batch[0]) {
      batch[count++] = W.q[W.head];
      W.head = (W.head + 1) % QUEUE_WORDS;
    }
    W.busy = true;
    cond_signal_(&W.changed);   /* space freed */
    mtx_unlock_(&W.lock);
    for (uint32_t i = 0; i < count; i++) rdp_word(W.n, batch[i]);
    mtx_lock_(&W.lock);
    W.syncs += W.batch_syncs;
    W.batch_syncs = 0;
    W.busy = false;
    cond_signal_(&W.changed);
  }
  mtx_unlock_(&W.lock);
#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

bool rdp_thread_start(N64 *n) {
  if (W.running) return true;
  memset(&W, 0, sizeof W);
  W.q = (uint64_t *)malloc(QUEUE_WORDS * sizeof(uint64_t));
  if (!W.q) return false;
  W.n = n;
  mtx_init_(&W.lock);
  cond_init_(&W.changed);
  if (!thread_start_(&W.thread)) {
    mtx_destroy_(&W.lock);
    cond_destroy_(&W.changed);
    free(W.q);
    W.q = NULL;
    return false;
  }
  W.running = true;
  return true;
}

void rdp_thread_push(const uint64_t *words, uint32_t count) {
  mtx_lock_(&W.lock);
  for (uint32_t i = 0; i < count; i++) {
    while ((W.tail + 1) % QUEUE_WORDS == W.head) cond_wait_(&W.changed, &W.lock);  /* full */
    W.q[W.tail] = words[i];
    W.tail = (W.tail + 1) % QUEUE_WORDS;
  }
  cond_signal_(&W.changed);
  mtx_unlock_(&W.lock);
}

/* Wait until everything queued has been drawn (before save states, reset). */
void rdp_thread_wait(N64 *n) {
  if (!W.running) return;
  mtx_lock_(&W.lock);
  while (W.head != W.tail || W.busy) cond_wait_(&W.changed, &W.lock);
  mtx_unlock_(&W.lock);
  rdp_thread_poll(n);
}

/* Raise the DP interrupt for SYNC_FULLs the worker has finished. */
void rdp_thread_poll(N64 *n) {
  if (!W.running || W.syncs == W.syncs_seen) return;
  mtx_lock_(&W.lock);   /* also makes the worker's drawing visible here */
  uint32_t s = W.syncs;
  mtx_unlock_(&W.lock);
  if (s != W.syncs_seen) {
    W.syncs_seen = s;
    n64_raise_mi(n, MI_INTR_DP);
  }
}

void rdp_thread_stop(N64 *n) {
  if (!W.running) return;
  rdp_thread_wait(n);
  mtx_lock_(&W.lock);
  W.stop = true;
  cond_signal_(&W.changed);
  mtx_unlock_(&W.lock);
  thread_join_(W.thread);
  mtx_destroy_(&W.lock);
  cond_destroy_(&W.changed);
  free(W.q);
  W.q = NULL;
  W.running = false;
}
