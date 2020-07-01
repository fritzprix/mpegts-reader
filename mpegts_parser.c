#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "gplayer_defs.h"
#include "mpegts_parser.h"
#include "utils/cdsl_dlist.h"

#define TS_SYNC (uint8_t)0x47
#define PTS_MASK (uint8_t)0b00110001
#define DTS_MASK (uint8_t)0b00010001
#define PTS_ONLY_MASK (uint8_t)0b00100001

#define SYNC_MASK (uint32_t)0xff
#define GET_SYNC(v) ((v & SYNC_MASK))
#define TEI_MASK (uint32_t)0x800
#define GET_TEI(v) ((v & TEI_MASK) >> 11)
#define PUSI_MASK (uint32_t)0x400
#define GET_PUSI(v) ((v & PUSI_MASK) >> 10)
#define PRIOR_MASK (uint32_t)0x200
#define GET_PRIORITY(v) ((v & PRIOR_MASK) >> 9)

#define PID_LOWER_MASK (uint32_t)0xff00000
#define PID_UPPER_MASK (uint32_t)0x1f000
#define GET_PID(v) ((v & PID_UPPER_MASK) >> 12 | (v & PID_LOWER_MASK) >> 12)

#define TSC_MASK (uint32_t)0xc0000000
#define ADP_MASK (uint32_t)0x30000000
#define CC_MAKS (uint32_t)0x0f000000

static void print_ts_haeder(mpegts_segement_t *segment);
static uint64_t get_pes_pts(uint8_t marker, uint8_t *src);
static void print_adaptation_field(mpegts_segement_t *segment);
static void print_payload(mpegts_segement_t *segment);

static int parse_ts_segment(int fd, mpegts_segement_t *segment);
static int parse_header(uint32_t v, mpegts_segement_t *segment);
static uint8_t *parse_adaptation_field(mpegts_segement_t *segment);
static uint8_t *parse_pcr(uint8_t *data, uint64_t *pcr);
static uint8_t *parse_pes_header(uint8_t *data, mpegts_segement_t *segment);
typedef uint8_t *(payload_parser_t)(uint8_t *, mpegts_segement_t *);

static uint32_t write_header(mpegts_segement_t *segment, int fd);
static void write_ts_segment(mpegts_segement_t *segment, int fd);
static uint8_t *write_adaptation_field(mpegts_segement_t *segment, uint8_t *wb);
static uint8_t *write_pes_header(mpegts_segement_t *segment, uint8_t *wb);

static const char *get_tsc_value(uint16_t tsc);
static const char *get_pts_value(uint8_t pts_ind);
static const char *get_stream_type_name(uint8_t stream_id);
static const char *get_adpt_field_value(uint16_t adp);
static const char *get_pid_description(uint16_t pid);

void mpegts_stream_init(mpegts_stream_t *stream, const char *url)
{
    if (!stream)
    {
        return;
    }
    cdsl_dlistNodeInit(&stream->ln);
    cdsl_dlistEntryInit(&stream->segment_list);
    size_t len = strlen(url);
    stream->url = (char *)malloc(len * sizeof(char));
    if (!stream->url)
    {
        LOG_ERR(ENOMEM, "fail to allocate memory\n");
    }
    strcpy(stream->url, url);
}

void mpegts_segment_init(mpegts_segement_t *segment)
{
    if (!segment)
    {
        return;
    }
    memset(segment, 0, sizeof(segment));
    segment->payload_start = segment->payload;
    cdsl_dlistNodeInit(&segment->ln);
}

void mpegts_stream_pes_reset_len(mpegts_stream_t *stream)
{
    if (!stream)
    {
        return;
    }
    listIter_t iterator;
    cdsl_dlistIterInit(&stream->segment_list, &iterator);
    while (cdsl_iterHasNext(&iterator))
    {
        mpegts_segement_t *segment = (mpegts_segement_t *)cdsl_iterNext(&iterator);
        if (segment->pes_header)
        {
            segment->pes_header->len = 0;
        }
    }
}

uint8_t mpegts_stream_get_last_cc(mpegts_stream_t *stream, int pid)
{
    if (!stream)
    {
        return 0;
    }
    uint8_t last_cc = 0;
    listIter_t iter;
    cdsl_dlistIterInit(&stream->segment_list, &iter);
    while (cdsl_iterHasNext(&iter))
    {
        mpegts_segement_t *segment = (mpegts_segement_t *)cdsl_iterNext(&iter);
        if (segment->header.pid == pid)
        {
            last_cc = segment->header.continuity_counter;
        }
    }
    return last_cc;
}

ssize_t mpegts_stream_write(mpegts_stream_t *stream, const char *path)
{
    if (!stream)
    {
        return 0;
    }

    const char *dest = path ? path : stream->url;
    int fd = open(dest, O_RDWR);
    if (fd <= 0)
    {
        return 0;
    }
    listIter_t iterator;
    cdsl_dlistIterInit(&stream->segment_list, &iterator);
    while (cdsl_iterHasNext(&iterator))
    {
        mpegts_segement_t *segment = (mpegts_segement_t *)cdsl_iterNext(&iterator);
        write_ts_segment(segment, fd);
    }
    close(fd);
    return 0;
}

uint8_t mpegts_stream_update_cc(mpegts_stream_t *stream, int pid, uint8_t init_cc)
{
    if (!stream)
    {
        return 0;
    }
    listIter_t iter;
    cdsl_dlistIterInit(&stream->segment_list, &iter);
    while (cdsl_iterHasNext(&iter))
    {
        mpegts_segement_t *segment = (mpegts_segement_t *)cdsl_iterNext(&iter);
        if (segment->header.pid == pid)
        {
            segment->header.continuity_counter = init_cc++;
        }
        init_cc &= 0xF;
    }
    return init_cc;
}

static void write_ts_segment(mpegts_segement_t *segment, int fd)
{
    if (!segment)
    {
        return;
    }
    ssize_t offset = 0;
    write_header(segment, fd);
    uint8_t *cursor = write_adaptation_field(segment, segment->payload);
    cursor = write_pes_header(segment, cursor);
    write(fd, segment->payload, sizeof(segment->payload));
}

void mpegts_stream_read_segment(mpegts_stream_t *stream)
{
    if (!stream || !stream->url)
    {
        LOG_DBG("null stream");
        return;
    }
    int fd = open(stream->url, O_RDONLY);
    if (fd <= 0)
    {
        LOG_ERR(EBADFD, "fail to open : %s\n", stream->url);
        return;
    }
    cdsl_dlistEntryInit(&stream->segment_list);
    mpegts_segement_t *current = (mpegts_segement_t *)malloc(sizeof(mpegts_segement_t));
    mpegts_segment_init(current);
    while (parse_ts_segment(fd, current))
    {
        cdsl_dlistPutTail(&stream->segment_list, (dlistNode_t *)current);
        current = (mpegts_segement_t *)malloc(sizeof(mpegts_segement_t));
        mpegts_segment_init(current);
    }
    uint32_t sz = cdsl_dlistSize(&stream->segment_list);
    LOG_DBG("ts segment count : %u\n", sz);
    mpegts_segement_t *last = (mpegts_segement_t *)cdsl_dlistGetLast(&stream->segment_list);
    close(fd);
}

void mpegts_stream_print_pes_header(const mpegts_stream_t *stream, uint16_t pid)
{
    if (!stream)
    {
        return;
    }
    listIter_t iter;
    cdsl_dlistIterInit(&stream->segment_list, &iter);
    while (cdsl_dlistIterHasNext(&iter))
    {
        mpegts_segement_t *segment = (mpegts_segement_t *)cdsl_dlistIterNext(&iter);
        if (segment->header.pusi && (segment->header.pid == pid))
        {
            print_ts_haeder(segment);
            print_adaptation_field(segment);
            print_payload(segment);
        }
    }
}

void mpegts_stream_fix_keyframe(mpegts_stream_t *stream, uint16_t pid)
{
    if (!stream)
    {
        return;
    }
    listIter_t iter;
    cdsl_dlistIterInit(&stream->segment_list, &iter);
    while (cdsl_dlistIterHasNext(&iter))
    {
        mpegts_segement_t *segment = (mpegts_segement_t *)cdsl_dlistIterNext(&iter);
        if (segment->header.pusi && (segment->header.pid == pid) && segment->adaptation_field.has_pcr)
        {
            segment->adaptation_field.rand_acc = 1;
        }
    }
}

void mpegts_stream_update_pcr_by_pts(mpegts_stream_t *stream, uint16_t pid)
{
    if (!stream)
    {
        return;
    }
    listIter_t iter;
    cdsl_dlistIterInit(&stream->segment_list, &iter);
    while (cdsl_dlistIterHasNext(&iter))
    {
        mpegts_segement_t *segment = (mpegts_segement_t *)cdsl_dlistIterNext(&iter);
        if (segment->adaptation_field.has_pcr && segment->pes_header && segment->pes_header->pts)
        {
            segment->adaptation_field.pcr = segment->pes_header->pts * 300;
        }
    }
}

void mpegts_stream_print(const mpegts_stream_t *stream)
{
    listIter_t iter;
    cdsl_dlistIterInit(&stream->segment_list, &iter);
    while (cdsl_dlistIterHasNext(&iter))
    {
        mpegts_segement_t *segment = (mpegts_segement_t *)cdsl_dlistIterNext(&iter);
        print_ts_haeder(segment);
        print_adaptation_field(segment);
        print_payload(segment);
    }
}

void mpegts_stream_free(mpegts_stream_t *stream)
{
    if (!stream)
    {
        return;
    }
    while (!cdsl_dlistIsEmpty(&stream->segment_list))
    {
        mpegts_segement_t *segment = (mpegts_segement_t *)cdsl_dlistDequeue(&stream->segment_list);
        if (segment->pes_header)
        {
            free(segment->pes_header);
        }
        free(segment);
    }
    if (stream->url)
    {
        free(stream->url);
    }
}

static int parse_ts_segment(int fd, mpegts_segement_t *segment)
{
    ssize_t sz = 0;
    uint32_t tsh = 0;
    uint8_t *cursor = NULL;
    if ((sz = read(fd, &tsh, sizeof(tsh))) <= 0)
    {
        LOG_DBG("TS EOF\n");
        return FALSE;
    }
    if (!parse_header(tsh, segment))
    {
        return FALSE;
    }
    if ((sz = read(fd, &segment->payload, sizeof(segment->payload))) != sizeof(segment->payload))
    {
        LOG_ERR(EBADFD, "unexpected EOS\n");
        return FALSE;
    }
    cursor = parse_adaptation_field(segment);
    segment->payload_start = parse_pes_header(cursor, segment);
    return TRUE;
}

static void print_payload(mpegts_segement_t *segment)
{
    if (!segment)
    {
        return;
    }
    uint32_t psize = sizeof(segment->payload) - (segment->payload_start - segment->payload);
    printf("\t\t>>>PAYLOAD size %u\n", psize);
    if (segment->pes_header)
    {
        const pes_header_t *header = segment->pes_header;
        printf("\t\t>>>PAYLOAD [PES Header][Stream ID 0x%02x (%s)][PTS/DTS :(%s) / length : %u / scramble ctrl : 0x%02x / priority : %d / align ind. : %d / copyright : %d / opt. len : %d]\n",
               header->stream_id, get_stream_type_name(header->stream_id), get_pts_value(header->pts_ind), header->len, header->scramble, header->priority, header->align_ind, header->cp_right, header->pes_header_len);
        printf("\t\t>>>[ESCR : %d / ES rate : %d / DSM trick mode : %d / Add. Copyr info : %d / CRC %d / Ext. :%d]\n", header->escr, header->es, header->dsm_trick, header->additional_cp_info, header->crc, header->ext);
        printf("\t\t>>> PTS : %lu (%f sec.) / DTS : %lu\n", header->pts, header->pts / 90000.f, header->dts);
    }
}

static void print_adaptation_field(mpegts_segement_t *segment)
{
    if (!segment)
    {
        return;
    }
    ts_adapt_field_t *adf = &segment->adaptation_field;
    printf("\t> [ADP Len : %d / Discont : %d / Rand Acc : %d / has_priority : %d / has_pcr : %d / has_opcr : %d]\n\t> [splicing point : %d / Private Data : %d / Ad. Ext : %d]\n",
           adf->len,
           adf->discontinuity,
           adf->rand_acc,
           adf->prior,
           adf->has_pcr,
           adf->has_opcr,
           adf->has_splic,
           adf->has_private,
           adf->ad_ext);
    if (adf->has_pcr)
    {
        printf("\t>> PCR : %lu (%f sec.)\n", adf->pcr, adf->pcr / 27000000.f);
    }
}

static void print_ts_haeder(mpegts_segement_t *segment)
{
    if (!segment)
    {
        return;
    }
    const ts_header_t *header = &segment->header;
    if (header->sync != TS_SYNC)
    {
        LOG_ERR(EINVAL, "invalid header : %d\n", header->sync);
        return;
    }
    if (header->tei)
    {
        LOG_DBG("TS corrupted!\n");
        return;
    }
    const char *scv = get_tsc_value(header->tscramble_control);
    const char *adv = get_adpt_field_value(header->adaptation_field_ctrl);
    if (!scv || !adv)
    {
        LOG_ERR(EINVAL, "invalid header : scv => %s (%x) adv => %s(%x)\n", scv, header->tscramble_control, adv, header->adaptation_field_ctrl);
        return;
    }
    printf("\n[PID : %d (%s) / TSC : %s / AD. Field : %s / PUSI : %s / Priority : %d / CC : %d]\n",
           header->pid, get_pid_description(header->pid), scv, adv, header->pusi ? "YES" : "NO", header->prior, header->continuity_counter);
}

static uint8_t *parse_pcr(uint8_t *data, uint64_t *pcr)
{
    uint64_t pcr_base = (data[0] << 25) | (data[1] << 17) | (data[2] << 9) | (data[3] << 1) | (data[4] & 0x80);
    uint64_t pcr_ext = ((data[4] & 1) << 8) | (data[5]);
    *pcr = 300 * pcr_base + pcr_ext;
    return &data[6];
}

static uint8_t write_pcr(uint8_t *data, uint64_t pcr)
{
    uint64_t pcr_base = pcr / 300;
    uint64_t pcr_ext = pcr % 300;
    data[0] = pcr_base >> 25;
    data[1] = pcr_base >> 17;
    data[2] = pcr_base >> 9;
    data[3] = pcr_base >> 1;
    data[4] |= (pcr_base & 0x80);
    data[4] |= ((pcr_ext >> 8) & 1);
    data[5] = pcr_ext;
}

static uint32_t write_header(mpegts_segement_t *segment, int fd)
{
    if (!segment)
    {
        return 0;
    }
    ts_header_t *header = &segment->header;
    uint32_t ts_header = (header->tscramble_control << 4) & 0xC0 | (header->adaptation_field_ctrl << 4) & 0x30 | (header->continuity_counter & 0xF);
    ts_header <<= 8;
    ts_header |= (header->pid & 0xFF);
    ts_header <<= 8;
    ts_header |= ((header->pid >> 8) & 0x1F);
    ts_header |= (header->tei ? 0x80 : 0);
    ts_header |= (header->pusi ? 0x40 : 0);
    ts_header |= (header->prior ? 0x20 : 0);
    ts_header <<= 8;
    ts_header |= TS_SYNC;
    write(fd, &ts_header, sizeof(ts_header));
    return ts_header;
}

static int parse_header(uint32_t v, mpegts_segement_t *segment)
{
    if (!segment)
    {
        LOG_ERR(EINVAL, "segment is null");
        return FALSE;
    }
    ts_header_t *header = &segment->header;
    header->sync = v & SYNC_MASK;
    if (header->sync != TS_SYNC)
    {
        LOG_ERR(EINVAL, "invalid ts_sync value : %d\n", header->sync);
        return FALSE;
    }
    v >>= 8;
    header->pid = v & 0x1f;
    header->tei = ((v & 0x80) == 0x80);
    header->pusi = ((v & 0x40) == 0x40);
    header->prior = ((v & 0x20) == 0x20);
    v >>= 8;
    header->pid <<= 8;
    header->pid = (header->pid | (v & 0xff));
    v >>= 8;
    header->tscramble_control = ((v & 0xc0) >> 4);
    header->adaptation_field_ctrl = ((v & 0x30) >> 4);
    header->continuity_counter = (v & 0xf);
    return TRUE;
}

static const char *get_tsc_value(uint16_t tsc)
{
    switch (tsc)
    {
    case 0:
        return "NOT SCRAMBLED";
    case 1:
        return "RESV";
    case 2:
        return "Even Key";
    case 3:
        return "Odd Key";
    default:
        return "UNKOWN";
    }
}

static const char *get_stream_type_name(uint8_t stream_id)
{
    switch (stream_id)
    {
    case 0xBD:
        return "Private Stream1";
    case 0xBE:
        return "Padding Stream";
    case 0xBF:
        return "Private Stream2";
    default:
    {
        if (stream_id < 0xDF)
        {
            return "Audio";
        }
        else if (stream_id < 0xEF)
        {
            return "Video";
        }
    }
    }
    return "Unknown";
}

static const char *get_pts_value(uint8_t pts_ind)
{
    switch (pts_ind)
    {
    case 0:
        return "NO PTS/DTS";
    case 1:
        return "FORBIDDEN!!!";
    case 2:
        return "PTS";
    case 3:
        return "PTS | DTS";
    default:
        break;
    }
}

static const char *get_pid_description(uint16_t pid)
{
    switch (pid)
    {
    case 0x00:
        return "PAT";
    case 0x01:
        return "CAT";
    case 0x02:
        return "TSDT";
    case 0x03:
        return "IPMP";
    case 0xfff:
        return "PMT";
    case 0x1ffb:
        return "ATSC MGT meta";
    case 0x1fff:
        return "NULL Packet";
    default:
    {
        if (pid <= 15)
        {
            return "RESV";
        }
        else if (pid <= 31)
        {
            return "DVB Meta";
        }
        else if (pid <= 8190)
        {
            return "Elementary Stream";
        }
    }
    }
    return "UNKNOWN";
}

static const char *get_adpt_field_value(uint16_t adp)
{
    switch (adp)
    {
    case 0:
        return "RESV";
    case 1:
        return "(Payload)";
    case 2:
        return "(Ad Field)";
    case 3:
        return "(Ad Field | Payload)";
    default:
        return NULL;
    }
}

static uint8_t *write_adaptation_field(mpegts_segement_t *segment, uint8_t *wb)
{
    if (!segment)
    {
        return wb;
    }
    ts_adapt_field_t *adp = &segment->adaptation_field;
    if (adp->rand_acc)
    {
        wb[1] |= 0x40;
    }
    if (adp->has_pcr)
    {
        write_pcr(&wb[2], adp->pcr);
    }

    if (segment->header.adaptation_field_ctrl & 0x2)
    {
        if (adp->len)
        {
            return &wb[adp->len + 1 + adp->priv_len];
        }
    }
    return wb;
}

static uint8_t *parse_adaptation_field(mpegts_segement_t *segment)
{
    if (!segment)
    {
        return NULL;
    }
    uint8_t *data = segment->payload;
    uint8_t *next = data;
    ts_header_t *header = &segment->header;
    ts_adapt_field_t *dest = &segment->adaptation_field;
    switch (header->adaptation_field_ctrl)
    {
    case 2:
    case 3:
        dest->len = *data++;
        next = &data[dest->len];
        dest->discontinuity = (*data & 0x80) == 0x80;
        dest->rand_acc = (*data & 0x40) == 0x40;
        dest->prior = (*data & 0x20) == 0x20;
        dest->has_pcr = (*data & 0x10) == 0x10;
        dest->has_opcr = (*data & 0x08) == 0x08;
        dest->has_splic = (*data & 0x04) == 0x04;
        dest->has_private = (*data & 0x02) == 0x02;
        dest->ad_ext = (*data & 0x01) == 0x01;
        data++;
        if (dest->has_pcr)
        {
            data = parse_pcr(data, &dest->pcr);
        }
        if (dest->has_opcr)
        {
            data = parse_pcr(data, &dest->opcr);
        }
        if (dest->has_splic)
        {
            dest->splice_count = *data++;
        }
        if (dest->has_private)
        {
            dest->priv_len = *data++;
            data = &data[dest->priv_len];
        }
        return next;
    default:
        return next;
    }
}

static uint8_t *write_pes_header(mpegts_segement_t *segment, uint8_t *wb)
{
    if (!segment)
    {
        return wb;
    }
    if (!segment->pes_header)
    {
        return wb;
    }

    pes_header_t *header = segment->pes_header;
    wb[2] = 1;
    wb[3] = header->stream_id;
    wb[4] = header->len >> 8;
    wb[5] = header->len & 0xff;
    return &wb[6 + header->pes_header_len];
}

static uint8_t *parse_pes_header(uint8_t *data, mpegts_segement_t *segment)
{
    if (!segment)
    {
        return NULL;
    }
    uint8_t *next = data;
    if ((data[0] << 16 | data[1] << 8 | data[2]) != 1)
    {
        segment->pes_header = NULL;
        return data;
    }
    pes_header_t *pes_header = (pes_header_t *)malloc(sizeof(pes_header_t));
    pes_header->stream_id = data[3];
    pes_header->len = (data[4] << 8) | data[5];
    if (pes_header->len == 0)
    {
        segment->pes_header = pes_header;
        return &data[6];
    }
    pes_header->scramble = (data[6] & 0x30) >> 4;
    pes_header->priority = (data[6] & 0x8) == 0x8;
    pes_header->align_ind = (data[6] & 0x4) == 0x4;
    pes_header->cp_right = (data[6] & 0x2) == 0x2;
    pes_header->original = (data[6] & 0x1) == 0x1;
    pes_header->pts_ind = ((data[7] & 0xC0) >> 6);
    pes_header->pes_header_len = data[8];
    segment->pes_header = pes_header;
    switch (pes_header->pts_ind)
    {
    case 0x2:
        pes_header->pts = get_pes_pts(PTS_ONLY_MASK, &data[9]);
        break;
    case 0x3:
        pes_header->pts = get_pes_pts(PTS_MASK, &data[9]);
        pes_header->dts = get_pes_pts(DTS_MASK, &data[14]);
        break;
    }
    next = &data[9];
    return &next[pes_header->pes_header_len];
}

static uint64_t get_pes_pts(uint8_t marker, uint8_t *src)
{
    uint64_t v = 0;
    if ((src[0] & marker) == marker)
    {

        v = (((src[0] & 0x0F) >> 1) << 30);
        v += (((src[1] << 7) | (src[2] >> 1)) << 15);
        v += ((src[3] << 7) | (src[4] >> 1));
    }
    return v;
}