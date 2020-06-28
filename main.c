#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "gplayer_defs.h"
#include "thread_pool.h"
#include <gst/gst.h>
#include "gst_aplay.h"
#include "mpegts_parser.h"

static void player_callback(int play_task_id, int result, void *arg);

int main(int argc, const char *argv[])
{
  int i = 0;
  const char *output_path = NULL;
  const char *input_path = "3.ts";
  char argb[255];
  char ctx = 0;
  for (i = 0; i < argc; i++)
  {
    char *token = NULL;
    strcpy(argb, argv[i]);
    token = strchr(argb, '-');
    if (token)
    {
      ctx = token[1];
    }
    else
    {
      switch (ctx)
      {
      case 'i':
      case 'I':
        input_path = argv[i];
        break;
      case 'o':
      case 'O':
        output_path = argv[i];
        break;
      default:
        break;
      }
    }
  }

  int fd = open(input_path, O_RDONLY);
  int ofd = 0;
  if (output_path)
  {
    remove(output_path);
    ofd = open(output_path, O_CREAT | O_RDWR, 0644);
  }

  mpegts_stream_t stream;
  mpegts_stream_init(&stream);
  mpegts_stream_read_segment(&stream, fd);
  // TODO : manipulate segment
  mpegts_stream_pes_reset_len(&stream);
  mpegts_stream_print(&stream);
  if (ofd)
  {
    mpegts_stream_write_segment(&stream, ofd);
  }
  mpegts_stream_free(&stream);
  close(fd);
  if (ofd)
  {
    close(ofd);
  }

  // gst_init(NULL, NULL);
  // gst_player_t* player = gst_player_new();
  // int c = 10;

  // // const int play_id = gst_player_play(player, "http://1.225.1.16/media/melon/playlist.m3u8", player_callback, NULL, SINGLE);
  // // const int play_id = gst_player_play(player, "https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/master.m3u8", player_callback, NULL, SINGLE);

  // const int play_id = gst_player_play(player, "file:///home/innocentevil/Music/melon/playlist.m3u8", player_callback, NULL, SINGLE);
  // while(c)
  // {
  //   sleep(3);
  //   if(!gst_player_seek(player, play_id, 20015))
  //   {
  //     LOG_DBG("seek to 30 sec. offset\n");
  //   }
  //   else
  //   {
  //     LOG_DBG("fail to seek\n");
  //     exit(-1);
  //   }
  // }
  // gst_player_stop(player, play_id);
  // gst_player_destroy(player);
  return 0;
}

static void player_callback(int play_task_id, int result, void *arg)
{
  LOG_DBG("result : %d @ (%d)\n", result, play_task_id);
}
