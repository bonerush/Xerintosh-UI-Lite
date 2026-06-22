#ifndef FLASHER_H
#define FLASHER_H

#ifdef __cplusplus
extern "C" {
#endif

void flasher_init(void *ud);
void flasher_loop(void *ud);
void flasher_exit(void *ud);

#ifdef __cplusplus
}
#endif

#endif
