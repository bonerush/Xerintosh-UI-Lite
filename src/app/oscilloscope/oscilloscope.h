#ifndef OSCILLOSCOPE_H
#define OSCILLOSCOPE_H

#ifdef __cplusplus
extern "C" {
#endif

void oscilloscope_init(void *user_data);
void oscilloscope_loop(void *user_data);
void oscilloscope_exit(void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* OSCILLOSCOPE_H */
