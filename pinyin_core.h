#ifndef SIXUAN_PINYIN_CORE_H
#define SIXUAN_PINYIN_CORE_H

#include <rime_api.h>
#include <stdint.h>

RimeApi *pinyin_rime_api(void);
RimeSessionId pinyin_create_session(void);
int pinyin_ensure_session_schema(RimeSessionId session);
int pinyin_input_english(void);
void pinyin_set_input_english(int english);

#endif
