#include "kern_timer.h"
#include "kern_sync.h"
#include "kern_sched.h"
#include "kern_init.h"
#include "kern_task.h"

#include <string.h>

#define KERN_TIMER_CMD_QUEUE_LEN 16

typedef enum {
    KERN_TIMER_CMD_START,
    KERN_TIMER_CMD_STOP,
    KERN_TIMER_CMD_RESET
} kern_timer_cmd_type_t;

typedef struct {
    kern_timer_cmd_type_t type;
    kern_timer_t         *timer;
} kern_timer_cmd_t;

static kern_timer_t    *s_timer_list = NULL;
static xeros_spinlock_t s_timer_lock;
static bool             s_timer_inited = false;
static kern_pid_t       s_timerd_pid = -1;

static kern_timer_cmd_t s_cmd_queue[KERN_TIMER_CMD_QUEUE_LEN];
static volatile uint8_t s_cmd_head = 0;
static volatile uint8_t s_cmd_tail = 0;
static xeros_spinlock_t s_cmd_lock;

static void timerd_wakeup(void)
{
    if (s_timerd_pid < 0) return;
    kern_task_t *task = kern_task_get(s_timerd_pid);
    if (task == NULL) return;
    if (task->state != KERN_TASK_RUNNING && task->state != KERN_TASK_READY) {
        task->state = KERN_TASK_READY;
    }
}

static kern_err_t timer_cmd_enqueue(kern_timer_cmd_type_t type, kern_timer_t *timer)
{
    xeros_spinlock_lock(&s_cmd_lock);
    uint8_t next = (s_cmd_tail + 1) % KERN_TIMER_CMD_QUEUE_LEN;
    if (next == s_cmd_head) {
        xeros_spinlock_unlock(&s_cmd_lock);
        return KERN_ENOSPC;
    }
    s_cmd_queue[s_cmd_tail].type = type;
    s_cmd_queue[s_cmd_tail].timer = timer;
    s_cmd_tail = next;
    xeros_spinlock_unlock(&s_cmd_lock);

    timerd_wakeup();
    return KERN_OK;
}

static void timer_daemon_entry(void *arg);

kern_err_t kern_timer_init(void)
{
    if (s_timer_inited) return KERN_OK;
    xeros_spinlock_init(&s_timer_lock);
    xeros_spinlock_init(&s_cmd_lock);
    s_timer_list = NULL;
    s_cmd_head = s_cmd_tail = 0;

    kern_pid_t pid = kern_spawn("timerd", timer_daemon_entry, NULL, 2048);
    if (pid < 0) {
        kern_log(KERN_LOG_WARN, "timer daemon spawn failed");
    } else {
        s_timerd_pid = pid;
    }

    s_timer_inited = true;
    return KERN_OK;
}

kern_err_t kern_timer_create(kern_timer_t *timer, const char *name,
                              kern_timer_callback_t cb, void *arg,
                              uint32_t period_ms, kern_timer_mode_t mode)
{
    if (timer == NULL || cb == NULL || period_ms == 0) return KERN_EINVAL;

    memset(timer, 0, sizeof(*timer));
    if (name != NULL) {
        strncpy(timer->name, name, KERN_TASK_NAME_LEN);
        timer->name[KERN_TASK_NAME_LEN] = '\0';
    } else {
        strcpy(timer->name, "timer");
    }
    timer->period_ms = period_ms;
    timer->mode = mode;
    timer->callback = cb;
    timer->arg = arg;
    timer->active = false;
    timer->next = NULL;
    return KERN_OK;
}

kern_err_t kern_timer_start(kern_timer_t *timer)
{
    if (timer == NULL) return KERN_EINVAL;
    return timer_cmd_enqueue(KERN_TIMER_CMD_START, timer);
}

kern_err_t kern_timer_stop(kern_timer_t *timer)
{
    if (timer == NULL) return KERN_EINVAL;
    return timer_cmd_enqueue(KERN_TIMER_CMD_STOP, timer);
}

kern_err_t kern_timer_reset(kern_timer_t *timer)
{
    if (timer == NULL) return KERN_EINVAL;
    return timer_cmd_enqueue(KERN_TIMER_CMD_RESET, timer);
}

static void timer_insert_active(kern_timer_t *timer)
{
    xeros_spinlock_lock(&s_timer_lock);
    timer->expiry = g_sched_ticks + timer->period_ms;
    timer->active = true;
    timer->next = s_timer_list;
    s_timer_list = timer;
    xeros_spinlock_unlock(&s_timer_lock);
}

static void timer_process_commands(void)
{
    xeros_spinlock_lock(&s_cmd_lock);
    while (s_cmd_head != s_cmd_tail) {
        kern_timer_cmd_t *cmd = &s_cmd_queue[s_cmd_head];
        s_cmd_head = (s_cmd_head + 1) % KERN_TIMER_CMD_QUEUE_LEN;
        kern_timer_t *t = cmd->timer;

        switch (cmd->type) {
        case KERN_TIMER_CMD_START:
            timer_insert_active(t);
            break;
        case KERN_TIMER_CMD_STOP:
            t->active = false;
            break;
        case KERN_TIMER_CMD_RESET:
            t->expiry = g_sched_ticks + t->period_ms;
            break;
        }
    }
    xeros_spinlock_unlock(&s_cmd_lock);
}

static void timer_process_expired(void)
{
    xeros_spinlock_lock(&s_timer_lock);
    uint32_t now = g_sched_ticks;
    kern_timer_t **pp = &s_timer_list;

    while (*pp != NULL) {
        kern_timer_t *t = *pp;
        if (!t->active) {
            pp = &t->next;
            continue;
        }

        if ((int32_t)(now - t->expiry) >= 0) {
            kern_timer_callback_t cb = t->callback;
            void *arg = t->arg;

            if (t->mode == KERN_TIMER_AUTORELOAD) {
                t->expiry = now + t->period_ms;
            } else {
                /* 一次性定时器触发后从活动列表移除，避免后续测试复用栈地址时产生状态污染 */
                *pp = t->next;
                t->active = false;
            }

            xeros_spinlock_unlock(&s_timer_lock);
            cb(arg);
            xeros_spinlock_lock(&s_timer_lock);
            pp = &s_timer_list;
        } else {
            pp = &t->next;
        }
    }
    xeros_spinlock_unlock(&s_timer_lock);
}

void kern_timer_reset_all(void)
{
    xeros_spinlock_lock(&s_timer_lock);
    s_timer_list = NULL;
    xeros_spinlock_unlock(&s_timer_lock);

    xeros_spinlock_lock(&s_cmd_lock);
    s_cmd_head = 0;
    s_cmd_tail = 0;
    xeros_spinlock_unlock(&s_cmd_lock);
}

void kern_timer_process(void)
{
    if (!s_timer_inited) return;
    timer_process_commands();
    timer_process_expired();
}

static void timer_daemon_entry(void *arg)
{
    (void)arg;
    while (1) {
        kern_timer_process();
        kern_sleep_ms(1);
    }
}
