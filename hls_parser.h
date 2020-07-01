#ifndef __HLS_PARSER_H
#define __HLS_PARSER_H

#include "utils/cdsl_dlist.h"
#include "mpegts_parser.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct hls_playlist hls_playlist_t;

    struct hls_playlist
    {
        dlistNode_t dl;
        dlistEntry_t sublist;
        char *url;
        hls_playlist_t *parent;
    };

    extern void hls_playlist_init(hls_playlist_t *playlist, hls_playlist_t *parent, const char *url);
    extern uint32_t hls_playlist_size(hls_playlist_t *playlist);
    extern void hls_parse(hls_playlist_t *playlist);
    extern void hls_print_timestamp(hls_playlist_t *playlist, uint16_t pid);
    extern void hls_fix_discontinuity(hls_playlist_t *playlist, int *pids, size_t pid_count);
    extern void hls_fix_key_frame_info(hls_playlist_t *playlist, uint16_t pid);
    extern void hls_update_pcr_by_pts(hls_playlist_t *playlist, uint16_t pid);
    extern void hls_update(hls_playlist_t *playlist);

#ifdef __cplusplus
}
#endif

#endif