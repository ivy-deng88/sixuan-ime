#ifndef SIXUAN_FCITX_PROTOCOL_H
#define SIXUAN_FCITX_PROTOCOL_H

#include <gio/gio.h>
#include <stdint.h>

#define SIXUAN_FCITX_SERVICE "dev.sixuan.Fcitx5"
#define SIXUAN_FCITX_PATH "/dev/sixuan/Fcitx5"
#define SIXUAN_FCITX_INTERFACE "dev.sixuan.Fcitx5"
#define SIXUAN_FCITX5_SERVICE "org.fcitx.Fcitx5"
#define SIXUAN_FCITX5_PATH "/controller"
#define SIXUAN_FCITX5_INTERFACE "org.fcitx.Fcitx.Controller1"

/* The daemon owns Rime. The Fcitx addon only renders this state tuple:
 * session, revision, handled, english, preedit, preedit cursor,
 * candidate cursor, candidates (text, source), raw input, text to commit.
 * The revision guards stale candidate clicks against newer state. */
#define SIXUAN_FCITX_STATE_TYPE "(ttbbsiia(ss)ss)"

GVariant *fcitx_pinyin_key(uint64_t session, uint32_t keyval, uint32_t state,
                           const char *text);
GVariant *fcitx_pinyin_select(uint64_t session, uint64_t revision, int index);
GVariant *fcitx_pinyin_state(uint64_t session);
int fcitx_pinyin_close(uint64_t session);
int fcitx_pinyin_add(uint64_t id, uint64_t revision, const char *text,
                     const char *source, int cloud);
int fcitx_pinyin_refresh(uint64_t id, uint64_t revision);
void fcitx_pinyin_preferences_changed(void);
char *fcitx_pinyin_schema_status(void);
void native_fcitx_publish_state(GVariant *state);

int native_fcitx_command_call(const char *method, GVariant *params,
                              GDBusMethodInvocation *invocation);
int native_fcitx_running(void);
int native_fcitx_frontend_available(void);
char *native_fcitx_register(void);

#endif
