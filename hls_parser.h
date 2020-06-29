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
        hls_playlist_t *parent;
    };

    extern void hls_playlist_init(hls_playlist_t *playlist, hls_playlist_t *parent);
    extern uint32_t hls_playlist_size(hls_playlist_t *playlist);
    extern void hls_read(hls_playlist_t *playlist, int fd);
    extern void hls_fix_discontinuity(hls_playlist_t *playlist);

#ifdef __cplusplus
}
#endif

#endif