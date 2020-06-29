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

void hls_playlist_init(hls_playlist_t *playlist, hls_playlist_t *parent)
{
    if (!playlist)
    {
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
}

uint32_t hls_playlist_size(hls_playlist_t *playlist)
{
    if (!playlist)
    {
        return 0;
    }

    return cdsl_dlistSize(&playlist->sublist);
}

void hls_read(hls_playlist_t *playlist, int fd)
{
    if (!playlist)
    {
        return;
    }
    FILE *fp = fdopen(fd, "r");
    char cb[512];
    memset(cb, 0, sizeof(cb));
    hls_parser_ctx_t ctx = CTX_META;
    while (fgets(cb, sizeof(cb), fp))
    {
        switch (ctx)
        {
        case CTX_META:
            if (strstr(cb, "EXTINF"))
            {
                ctx = CTX_MEDIA;
            }
            break;
        case CTX_MEDIA:
            printf("media : %s", cb);
            load_media(playlist, cb);
            ctx = CTX_META;
            break;
        default:
            break;
        }
        memset(cb, 0, sizeof(cb));
    }
    fclose(fp);
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
    int fd = open(path, O_RDWR);
    if (fd <= 0)
    {
        LOG_ERR(EBADFD, "fail to open %s (%d)\n", path, fd);
        return;
    }
    mpegts_stream_t *stream = (mpegts_stream_t *)malloc(sizeof(mpegts_stream_t));
    if (!stream)
    {
        LOG_ERR(ENOMEM, "fail to allocate ts stream");
        return;
    }
    mpegts_stream_init(stream);
    mpegts_stream_read_segment(stream, fd);
    cdsl_dlistPutTail(&playlist->sublist, &stream->ln);
    close(fd);
}

void hls_fix_discontinuity(hls_playlist_t *playlist)
{
    if (!playlist)
    {
        return;
    }

    listIter_t iter;
    uint8_t cc = 0;
    cdsl_dlistIterInit(&playlist->sublist, &iter);
    while (cdsl_iterHasNext(&iter))
    {
        mpegts_stream_t *stream = (mpegts_stream_t *)cdsl_iterNext(&iter);
        cc = mpegts_stream_update_cc(stream, 0x100, cc);
    }
}