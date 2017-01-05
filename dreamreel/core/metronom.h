#ifndef METRONOM_H
#define METRONOM_H

#define MAX_PTS 0x7FFFFFFFFFFFFFFF

void init_metronom(void);
void start_metronom(void);
void stop_metronom(void);
void metronom_set(int64_t new_pts);
void set_next_video_pts(int64_t next_pts, void (*callback)(void));
void set_next_audio_pts(int64_t next_pts, void (*callback)(void));

#endif
