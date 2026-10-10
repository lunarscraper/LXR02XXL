/* LXRXXL -- per-instance copies of the LXR engine's global state.
 *
 * The engine's globals sit in two sections (lxrstate_data, lxrstate_bss; src/shim/lxr_state.h). One process-wide lock
 * lets one instance at a time run the engine; when another instance's turn comes, the live sections are saved to the
 * instance that used them last and the new one's copy is put in. Pointers inside the engine (the parameter table points
 * into the voices) stay valid because the copies always go back to the same addresses. With one LXRXXL on a project
 * nothing is ever copied. */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "lxr_state.h"

extern char __start_lxrstate_data[] __attribute__((weak));
extern char __stop_lxrstate_data[] __attribute__((weak));
extern char __start_lxrstate_bss[] __attribute__((weak));
extern char __stop_lxrstate_bss[] __attribute__((weak));

struct LxrState { char *data, *bss; };

static pthread_mutex_t g_lock;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static LxrState *g_owner;
static char *g_pristine;   /* the data section as loaded, before any instance ran */

static size_t data_len(void) { return __start_lxrstate_data ? (size_t)(__stop_lxrstate_data - __start_lxrstate_data) : 0; }
static size_t bss_len(void) { return __start_lxrstate_bss ? (size_t)(__stop_lxrstate_bss - __start_lxrstate_bss) : 0; }

static void init_once(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutexattr_setprotocol(&a, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&g_lock, &a);
    pthread_mutexattr_destroy(&a);
    g_pristine = malloc(data_len() + 1);
    if (g_pristine && data_len()) memcpy(g_pristine, __start_lxrstate_data, data_len());
}

LxrState *lxr_state_create(void)
{
    LxrState *s;
    pthread_once(&g_once, init_once);
    s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->data = malloc(data_len() + 1);
    s->bss = calloc(1, bss_len() + 1);   /* zeroed, like the firmware's RAM at power-up */
    if (!s->data || !s->bss || !g_pristine) { free(s->data); free(s->bss); free(s); return NULL; }
    memcpy(s->data, g_pristine, data_len());
    return s;
}

void lxr_state_enter(LxrState *s)
{
    pthread_mutex_lock(&g_lock);
    if (g_owner == s) return;
    if (g_owner) {
        memcpy(g_owner->data, __start_lxrstate_data, data_len());
        memcpy(g_owner->bss, __start_lxrstate_bss, bss_len());
    }
    memcpy(__start_lxrstate_data, s->data, data_len());
    memcpy(__start_lxrstate_bss, s->bss, bss_len());
    g_owner = s;
}

void lxr_state_leave(void) { pthread_mutex_unlock(&g_lock); }

void lxr_state_destroy(LxrState *s)
{
    if (!s) return;
    pthread_mutex_lock(&g_lock);
    if (g_owner == s) g_owner = NULL;
    pthread_mutex_unlock(&g_lock);
    free(s->data);
    free(s->bss);
    free(s);
}
