#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "gplayer_defs.h"
#include "mpegts_parser.h"
#include "hls_parser.h"

typedef enum
{
    CTX_META,
    CTX_MEDIA,
    CTX_PLAYLIST,
} hls_parser_ctx_t;

static void load_media(hls_playlist_t *playlist, char *path);

void hls_playlist_init(hls_playlist_t *playlist, hls_playlist_t *parent, const char *url)
{
    if (!playlist || !url)
    {
        LOG_ERR(EINVAL, "parameter missing\n");
        return;
    }

    memset(playlist, 0, sizeof(hls_playlist_t));
    cdsl_dlistEntryInit(&playlist->sublist);
    cdsl_dlistNodeInit(&playlist->dl);
    if (parent)
    {
        playlist->parent = parent;
        cdsl_dlistPutTail(&parent->sublist, &playlist->dl);
    }

    playlist->url = (char *)malloc(sizeof(char) * strlen(url));
    if (!playlist->url)
    {
        LOG_ERR(ENOMEM, "fail to allocate!!\n");
    }
    strcpy(playlist->url, url);
}

uint32_t hls_playlist_size(hls_playlist_t *playlist)
{
    if (!playlist)
    {
        return 0;
    }

    return cdsl_dlistSize(&playlist->sublist);
}

void hls_parse(hls_playlist_t *playlist)
{
    if (!playlist)
    {
        return;
    }
    FILE *fp = fopen(playlist->url, "r");
    if (!fp)
    {
        return;
    }
    char cb[512];
    memset(cb, 0, sizeof(cb));
    while (fgets(cb, sizeof(cb), fp))
    {
        char *tag_starter = strchr(cb, '#');
        if (tag_starter)
        {
        }
        else
        {
            load_media(playlist, cb);
        }
        memset(cb, 0, sizeof(cb));
    }
    fclose(fp);
}

void hls_print_timestamp(hls_playlist_t *playlist, uint16_t pid)
{
    if (!playlist)
    {
        return;
    }

    listIter_t iter;
    cdsl_dlistIterInit(&playlist->sublist, &iter);
    while (cdsl_dlistIterHasNext(&iter))
    {
        mpegts_stream_t *stream = (mpegts_stream_t *)cdsl_dlistIterNext(&iter);
        mpegts_stream_print_pes_header(stream, pid);
    }
}

static void load_media(hls_playlist_t *playlist, char *path)
{
    if (!playlist)
    {
        return;
    }

    char *nl = strchr(path, '\n');
    if (nl)
    {
        *nl = '\0';
    }
    mpegts_stream_t *stream = (mpegts_stream_t *)malloc(sizeof(mpegts_stream_t));
    if (!stream)
    {
        LOG_ERR(ENOMEM, "fail to allocate ts stream");
        return;
    }
    mpegts_stream_init(stream, path);
    mpegts_stream_read_segment(stream);
    cdsl_dlistPutTail(&playlist->sublist, &stream->ln);
}

void hls_fix_key_frame_info(hls_playlist_t *playlist, uint16_t pid)
{
    if (!playlist)
    {
        return;
    }
    listIter_t iter;
    cdsl_dlistIterInit(&playlist->sublist, &iter);
    while (cdsl_iterHasNext(&iter))
    {
        mpegts_stream_t *stream = (mpegts_stream_t *)cdsl_iterNext(&iter);
        mpegts_stream_fix_keyframe(stream, pid);
    }
}

void hls_update_pcr_by_pts(hls_playlist_t *playlist, uint16_t pid)
{
    if (!playlist)
    {
        return;
    }
    listIter_t iter;
    cdsl_dlistIterInit(&playlist->sublist, &iter);
    while (cdsl_iterHasNext(&iter))
    {
        mpegts_stream_t *stream = (mpegts_stream_t *)cdsl_iterNext(&iter);
        mpegts_stream_update_pcr_by_pts(stream, pid);
    }
}

void hls_fix_discontinuity(hls_playlist_t *playlist, int *pids, size_t pid_count)
{
    if (!playlist)
    {
        return;
    }

    listIter_t iter;
    cdsl_dlistIterInit(&playlist->sublist, &iter);
    size_t i = 0;
    for (; i < pid_count; i++)
    {
        uint8_t cc = 0;
        while (cdsl_iterHasNext(&iter))
        {
            mpegts_stream_t *stream = (mpegts_stream_t *)cdsl_iterNext(&iter);
            cc = mpegts_stream_update_cc(stream, 0x100, cc);
            LOG_DBG("CC : %u\n", cc);
        }
    }
}

void hls_update(hls_playlist_t *playlist)
{
    if (!playlist)
    {
        return;
    }
    listIter_t iter;
    cdsl_dlistIterInit(&playlist->sublist, &iter);
    while (cdsl_dlistIterHasNext(&iter))
    {
        mpegts_stream_t *stream = (mpegts_stream_t *)cdsl_dlistIterNext(&iter);
        mpegts_stream_write(stream, NULL);
    }
}
