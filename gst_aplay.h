#ifndef __GST_APLAY_H
#define __GST_APLAY_H


#define SUCCESS                  0
#define ERR_UNABLE_TO_PLAY      -1
#define ERR_CANCELED            -2

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SINGLE = 0,
    LOOP
} play_type_t;
typedef struct gst_player gst_player_t;
typedef void (*gst_player_callback_t)(int play_task_id, int result, void* arg);


extern gst_player_t* gst_player_new();
extern int gst_player_is_playing(gst_player_t* player, int play_task_id);
extern int gst_player_play(gst_player_t* player, const char* uri, gst_player_callback_t callback, void* arg, play_type_t play_type);
extern int gst_player_seek(gst_player_t* player, int play_task_id, int ms_offset);
extern void gst_player_stop(gst_player_t* player, int play_task_id);
extern void gst_player_stop_all(gst_player_t* player);
extern void gst_player_pause(gst_player_t* player, int play_task_id);
extern void gst_player_resume(gst_player_t* player, int play_task_id);
extern void gst_player_destroy(gst_player_t* player);


#ifdef __cplusplus
}
#endif

#endif