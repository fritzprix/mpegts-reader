#ifndef __MPEGTS_PARSER
#define __MPEGTS_PARSER

#include <stdint.h>
#include "utils/cdsl_dlist.h"
#include "utils/cdsl_avltree.h"

#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct
    {
        uint32_t sync : 8, tei : 1, pusi : 1, prior : 1, pid : 13, tscramble_control : 2, adaptation_field_ctrl : 2, continuity_counter : 4;

    } ts_header_t;

    typedef struct
    {
        uint16_t len : 8,
            discontinuity : 1, rand_acc : 1, prior : 1, has_pcr : 1, has_opcr : 1, has_splic : 1, has_private : 1, ad_ext : 1;
        uint64_t pcr;
        uint64_t opcr;
        uint8_t splice_count;
        uint8_t priv_len;
    } ts_adapt_field_t;

    typedef struct
    {
        uint8_t stream_id;
        uint16_t len;
        uint32_t scramble : 2, priority : 1, align_ind : 1, cp_right : 1, original : 1, pts_ind : 2, escr : 1, es : 1, dsm_trick : 1, additional_cp_info : 1, crc : 1, ext : 1, pes_header_len : 8;
    } pes_header_t;

    typedef struct mpegts_segment mpegts_segement_t;
    typedef uint8_t *(payload_parser_t)(uint8_t *, struct mpegts_segment *);

    struct mpegts_segment
    {
        dlistNode_t ln;
        ts_header_t header;
        ts_adapt_field_t adaptation_field;
        uint8_t payload[184];
        pes_header_t *pes_header;
        uint8_t *payload_start;
        payload_parser_t *payload_parser;
    };

    typedef struct
    {
        dlistEntry_t segment_list;
    } mpegts_stream_t;

    extern void mpegts_stream_init(mpegts_stream_t *stream);
    extern void mpegts_segment_init(mpegts_segement_t* segment);
    extern void mpegts_stream_read_segment(mpegts_stream_t *stream, int fd);
    extern void mpegts_stream_pes_reset_len(mpegts_stream_t* stream);
    extern void mpegts_stream_write_segment(mpegts_stream_t *stream, int fd);
    extern void mpegts_stream_print(const mpegts_stream_t *stream);
    extern void mpegts_stream_free(mpegts_stream_t* stream);

#ifdef __cplusplus
}
#endif

#endif