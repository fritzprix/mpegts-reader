
#include <gst/gst.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <malloc.h>
#include "gplayer_defs.h"
#include "gst_aplay.h"
#include "thread_pool.h"

#define MAX_WAIT 5

#define TASK_QSZ 2
#define TASK_QMSK 0x1

#define GST_TASK_STATE_CANCEL 3
#define GST_TASK_STATE_PLAYING 2
#define GST_TASK_STATE_START 1
#define GST_TASK_STATE_IDLE 0

#define GST_CMD_TEMPLATE "playbin uri=%s"

typedef struct
{
    GstElement *pipeline;
    GMainLoop *loop;
    pthread_mutex_t lock;
    gst_player_callback_t callback;
    void *cb_arg;
    volatile uint8_t state;
    play_type_t play_type;
    int id;
} gst_play_task_t;

struct gst_player
{
    thread_pool_t *pool;
    pthread_mutex_t lock;
    gst_play_task_t tasks[TASK_QSZ];
    int id;
};

static task_result_t handle_play_task(void *task);
static void play_task_callback(task_result_t result, void *task);
static void cb_message(GstBus *bus, GstMessage *msg, gst_play_task_t *task);

gst_player_t *gst_player_new()
{
    gst_player_t *player = (gst_player_t *)malloc(sizeof(gst_player_t));
    if (!player)
    {
        LOG_ERR(ENOMEM, "fail to allocate\n");
        return NULL;
    }
    memset(player, 0, sizeof(gst_player_t));
    pthread_mutex_init(&player->lock, NULL);
    player->pool = thread_pool_new(2, handle_play_task);
    int idx;
    for (idx = 0; idx < TASK_QSZ; idx++)
    {
        gst_play_task_t *p_task = &player->tasks[idx];
        pthread_mutex_init(&p_task->lock, NULL);
        p_task->state = GST_TASK_STATE_IDLE;
    }
    return player;
}

int gst_player_is_playing(gst_player_t *player, int play_task_id)
{
    if (!player)
    {
        return FALSE;
    }

    gst_play_task_t *p_task = &player->tasks[play_task_id & TASK_QMSK];
    int res = FALSE;
    if (!pthread_mutex_lock(&p_task->lock))
    {
        res = (p_task->state != GST_TASK_STATE_IDLE);
        pthread_mutex_unlock(&p_task->lock);
    }
    return res;
}

int gst_player_play(gst_player_t *player, const char *uri, gst_player_callback_t callback, void *arg, play_type_t play_type)
{
    if (!player)
    {
        return -1;
    }
    char str_buffer[255];
    int play_task_id = -1;
    sprintf(str_buffer, GST_CMD_TEMPLATE, uri);
    if (!pthread_mutex_lock(&player->lock))
    {
        play_task_id = player->id++;
        gst_play_task_t *task = &player->tasks[play_task_id & TASK_QMSK];
        pthread_mutex_unlock(&player->lock);
        if (!pthread_mutex_lock(&task->lock))
        {
            if (task->state != GST_TASK_STATE_IDLE)
            {
                LOG_DBG("fail to submit on %d\n", play_task_id & TASK_QMSK);
                pthread_mutex_unlock(&task->lock);
                return -1;
            }
            task->id = play_task_id;
            task->pipeline = gst_parse_launch(str_buffer, NULL);
            task->callback = callback;
            task->cb_arg = arg;
            task->play_type = play_type;
            LOG_DBG("gst pipeline : %s\n", str_buffer);
            task->state = GST_TASK_STATE_START;
            if (task->pipeline)
            {
                LOG_DBG("media pipeline created\n");
            }
            else
            {
                LOG_DBG("fail to create pipeline\n");
            }
            pthread_mutex_unlock(&task->lock);
        }

        int res, w_count = 0;
        if (!pthread_mutex_lock(&task->lock))
        {
            if (((res = thread_pool_submit(player->pool, task, play_task_callback, 0)) < 0))
            {
                LOG_DBG("thread pool is busy\n");
                task->state = GST_TASK_STATE_IDLE;
                gst_object_unref(task->pipeline);
                pthread_mutex_unlock(&task->lock);
                return -1;
            }
            pthread_mutex_unlock(&task->lock);
        }
        LOG_DBG("play task is submitted %d\n", res);
    }
    return play_task_id;
}

int gst_player_seek(gst_player_t *player, int play_task_id, int ms_offset)
{
    if (!player)
    {
        return -1;
    }
    gst_play_task_t *task = &player->tasks[play_task_id & TASK_QMSK];
    int res = 0;
    if (!pthread_mutex_lock(&task->lock))
    {
        LOG_DBG("seek to %d ms\n", ms_offset);

        if (!gst_element_seek(task->pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, ms_offset * GST_MSECOND, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE))
        {
            res = -1;
        }
        pthread_mutex_unlock(&task->lock);
    }
    return res;
}

void gst_player_stop(gst_player_t *player, int play_task_id)
{
    if (!player)
    {
        return;
    }
    gst_play_task_t *p_task = &player->tasks[play_task_id & TASK_QMSK];
    LOG_DBG("stop player : %d (%p)\n", play_task_id, p_task);
    if (!pthread_mutex_lock(&p_task->lock))
    {
        if (p_task->state == GST_TASK_STATE_IDLE)
        {
            LOG_DBG("task (%d) is already finished\n", p_task->id);
            pthread_mutex_unlock(&p_task->lock);
            return;
        }
        gst_element_set_state(p_task->pipeline, GST_STATE_READY);
        g_main_loop_quit(p_task->loop);
        p_task->state = GST_TASK_STATE_CANCEL;
        pthread_mutex_unlock(&p_task->lock);
    }
}

void gst_player_stop_all(gst_player_t *player)
{
    if (!player)
    {
        return;
    }
    int idx;
    for (idx = 0; idx < TASK_QSZ; idx++)
    {
        gst_player_stop(player, idx);
    }
}

void gst_player_pause(gst_player_t *player, int play_task_id)
{
    if (!player)
    {
        return;
    }
    gst_play_task_t *p_task = &player->tasks[play_task_id & TASK_QMSK];
    LOG_DBG("pause player : %d (%p)\n", play_task_id, p_task);
    if (!pthread_mutex_lock(&p_task->lock))
    {
        if (p_task->state == GST_TASK_STATE_IDLE)
        {
            LOG_DBG("task (%d) is finished\n", p_task->id);
            return;
        }
        gst_element_set_state(p_task->pipeline, GST_STATE_PAUSED);
        pthread_mutex_unlock(&p_task->lock);
    }
}

void gst_player_resume(gst_player_t *player, int play_task_id)
{
    if (!player)
    {
        return;
    }
    gst_play_task_t *p_task = &player->tasks[play_task_id & TASK_QMSK];
    LOG_DBG("resume player : %d (%p)\n", play_task_id, p_task);
    if (!pthread_mutex_lock(&p_task->lock))
    {
        if (p_task->state == GST_TASK_STATE_IDLE)
        {
            LOG_DBG("task (%d) is finished\n", p_task->id);
            return;
        }
        gst_element_set_state(p_task->pipeline, GST_STATE_PLAYING);
        pthread_mutex_unlock(&p_task->lock);
    }
}

void gst_player_destroy(gst_player_t *player)
{
}

static task_result_t handle_play_task(void *task)
{
    if (!task)
    {
        return FAIL;
    }

    GstBus *bus;
    GstStateChangeReturn ret;

    gst_play_task_t *p_task = (gst_play_task_t *)task;
    LOG_DBG("play task : %d is handled\n", p_task->id);

    bus = gst_element_get_bus(p_task->pipeline);
    ret = gst_element_set_state(p_task->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        LOG_DBG("unable to set the pipeline to the playing state\n");
        if (p_task->callback)
        {
            p_task->callback(p_task->id, ERR_UNABLE_TO_PLAY, p_task->cb_arg);
        }
        return FAIL;
    }
    LOG_DBG("gst state changed\n");
    if (!pthread_mutex_lock(&p_task->lock))
    {
        if (p_task->state != GST_TASK_STATE_CANCEL)
        {
            p_task->state = GST_TASK_STATE_PLAYING;
            LOG_DBG("playing..(%d)\n", p_task->id);

            p_task->loop = g_main_loop_new(NULL, FALSE);
            gst_bus_add_signal_watch(bus);
            g_signal_connect(bus, "message", G_CALLBACK(cb_message), p_task);
            pthread_mutex_unlock(&p_task->lock);
        }
        else
        {
            LOG_DBG("playback(%d) canceld\n", p_task->id);
            pthread_mutex_unlock(&p_task->lock);
            if (p_task->callback)
            {
                p_task->callback(p_task->id, ERR_CANCELED, p_task->cb_arg);
            }
            return FAIL;
        }
    }

    g_main_loop_run(p_task->loop);
    if (!pthread_mutex_lock(&p_task->lock))
    {
        g_main_loop_unref(p_task->loop);
        gst_object_unref(bus);
        gst_element_set_state(p_task->pipeline, GST_STATE_NULL);
        gst_object_unref(p_task->pipeline);
        LOG_DBG("playback task(%d) is done\n", p_task->id);
        p_task->pipeline = NULL;
        p_task->loop = NULL;
        p_task->state = GST_TASK_STATE_IDLE;
        pthread_mutex_unlock(&p_task->lock);
    }

    if (p_task->callback)
    {
        p_task->callback(p_task->id, SUCCESS, p_task->cb_arg);
    }
    return OK;
}

static void play_task_callback(task_result_t result, void *task)
{
    if (!task)
    {
        return;
    }

    gst_play_task_t *p_task = (gst_play_task_t *)task;
    if (pthread_mutex_lock(&p_task->lock))
    {
        LOG_DBG("fail to lock\n");
        return;
    }
    p_task->state = GST_TASK_STATE_IDLE;
    pthread_mutex_unlock(&p_task->lock);
    LOG_DBG("play task (%d) finished with %d\n", p_task->id, result);
}

static void cb_message(GstBus *bus, GstMessage *msg, gst_play_task_t *task)
{
    switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_ERROR:
    {
        GError *err;
        gchar *debug;

        gst_message_parse_error(msg, &err, &debug);
        LOG_DBG("Error: %s\n", err->message);
        g_error_free(err);
        g_free(debug);

        gst_element_set_state(task->pipeline, GST_STATE_READY);
        g_main_loop_quit(task->loop);
        break;
    }
    case GST_MESSAGE_EOS:
        /* end-of-stream */
        LOG_DBG("EOS event\n");
        switch (task->play_type)
        {
        case SINGLE:
            gst_element_set_state(task->pipeline, GST_STATE_READY);
            g_main_loop_quit(task->loop);
            break;
        case LOOP:
            if (!gst_element_seek(task->pipeline,
                                  1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                                  GST_SEEK_TYPE_SET, 0, //2 seconds (in nanoseconds)
                                  GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE))
            {
                LOG_DBG("Seek failed!\n");
            }
            break;
        }
        break;
    case GST_MESSAGE_BUFFERING:
    {
        gint percent = 0;
        /* If the stream is live, we do not care about buffering. */
        if (task->state == GST_TASK_STATE_PLAYING)
            break;

        gst_message_parse_buffering(msg, &percent);
        g_print("Buffering (%3d%%)\r", percent);
        LOG_DBG("Buffering (%3d%%)\r", percent);
        /* Wait until buffering is complete before start/resume playing */
        if (percent < 100)
            gst_element_set_state(task->pipeline, GST_STATE_PAUSED);
        else
            gst_element_set_state(task->pipeline, GST_STATE_PLAYING);
        break;
    }
    case GST_MESSAGE_CLOCK_LOST:
        /* Get a new clock */
        gst_element_set_state(task->pipeline, GST_STATE_PAUSED);
        gst_element_set_state(task->pipeline, GST_STATE_PLAYING);
        break;
    default:
        /* Unhandled message */
        break;
    }
}