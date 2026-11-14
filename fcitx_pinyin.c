#include "fcitx_protocol.h"
#include "pinyin_core.h"

#include <glib.h>
#include <rime_api.h>
#include <string.h>

extern void goPinyinChanged(uint64_t, uint64_t, char *, char *);
extern void goPinyinLearn(char *, char *);
extern int goPinyinEnabled(void);
extern char *goPinyinSchema(void);
extern int goPreferenceValue(char *);

/* Keep numeric constants independent from either frontend library. */
#define SX_KEY_BACKSPACE 0xff08
#define SX_KEY_DELETE 0xffff
#define SX_KEY_ESCAPE 0xff1b
#define SX_KEY_SPACE 0x20
#define SX_KEY_LEFT 0xff51
#define SX_KEY_UP 0xff52
#define SX_KEY_RIGHT 0xff53
#define SX_KEY_DOWN 0xff54
#define SX_KEY_PAGE_UP 0xff55
#define SX_KEY_PAGE_DOWN 0xff56
#define SX_KEY_SHIFT_L 0xffe1
#define SX_KEY_SHIFT_R 0xffe2
#define SX_SHIFT_MASK (1u << 0)
#define SX_CONTROL_MASK (1u << 2)
#define SX_MOD1_MASK (1u << 3)
#define SX_SUPER_MASK (1u << 6)
#define SX_RELEASE_MASK (1u << 30)

typedef struct {
  char *text, *source;
  int index;
} FcitxChoice;

typedef struct {
  RimeSessionId rime;
  uint64_t id, revision;
  int cursor, navigated;
  guint shift_held, modifiers;
  int shift_alone;
  char *raw, *preedit, *commit;
  int caret;
  GPtrArray *local, *personal, *cloud, *merged;
} FcitxPinyin;

static GHashTable *sessions;
static uint64_t serial = 1ull << 63;

static void free_choice(gpointer data) {
  FcitxChoice *c = data;
  g_free(c->text);
  g_free(c->source);
  g_free(c);
}

static FcitxChoice *choice(const char *text, const char *source, int index) {
  FcitxChoice *c = g_new0(FcitxChoice, 1);
  c->text = g_strdup(text);
  c->source = g_strdup(source ? source : "");
  c->index = index;
  return c;
}

static FcitxPinyin *find(uint64_t id, uint64_t revision) {
  FcitxPinyin *s =
      sessions ? g_hash_table_lookup(sessions, (gpointer)(uintptr_t)id) : NULL;
  return s && (!revision || s->revision == revision) ? s : NULL;
}

static void free_session(gpointer data) {
  FcitxPinyin *s = data;
  if (s->rime)
    pinyin_rime_api()->destroy_session(s->rime);
  g_free(s->raw);
  g_free(s->preedit);
  g_free(s->commit);
  g_ptr_array_free(s->merged, TRUE);
  g_ptr_array_free(s->local, TRUE);
  g_ptr_array_free(s->personal, TRUE);
  g_ptr_array_free(s->cloud, TRUE);
  g_free(s);
}

static FcitxPinyin *create(void) {
  RimeApi *api = pinyin_rime_api();
  if (!api || !goPinyinEnabled())
    return NULL;
  RimeSessionId rime = pinyin_create_session();
  if (!rime)
    return NULL;
  FcitxPinyin *s = g_new0(FcitxPinyin, 1);
  s->rime = rime;
  s->id = ++serial;
  s->raw = g_strdup("");
  s->preedit = g_strdup("");
  s->commit = g_strdup("");
  s->local = g_ptr_array_new_with_free_func(free_choice);
  s->personal = g_ptr_array_new_with_free_func(free_choice);
  s->cloud = g_ptr_array_new_with_free_func(free_choice);
  s->merged = g_ptr_array_new();
  if (!sessions)
    sessions = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                     free_session);
  g_hash_table_insert(sessions, (gpointer)(uintptr_t)s->id, s);
  return s;
}

static void merge_one(FcitxPinyin *s, FcitxChoice *c) {
  for (guint i = 0; i < s->merged->len; i++)
    if (!strcmp(((FcitxChoice *)g_ptr_array_index(s->merged, i))->text,
                c->text))
      return;
  g_ptr_array_add(s->merged, c);
}

static void merge(FcitxPinyin *s) {
  g_ptr_array_set_size(s->merged, 0);
  for (guint i = 0; i < MIN(3, s->personal->len); i++)
    merge_one(s, g_ptr_array_index(s->personal, i));
  for (guint i = 0; i < MIN(5, s->local->len); i++)
    merge_one(s, g_ptr_array_index(s->local, i));
  for (guint i = 0; i < s->cloud->len; i++)
    merge_one(s, g_ptr_array_index(s->cloud, i));
  for (guint i = 5; i < s->local->len; i++)
    merge_one(s, g_ptr_array_index(s->local, i));
  for (guint i = 3; i < s->personal->len; i++)
    merge_one(s, g_ptr_array_index(s->personal, i));
  if (s->merged->len)
    s->cursor = CLAMP(s->cursor, 0, (int)s->merged->len - 1);
  else
    s->cursor = 0;
}

static void append_commit(FcitxPinyin *s, const char *text) {
  if (!text || !*text)
    return;
  char *joined = s->commit && *s->commit ? g_strconcat(s->commit, text, NULL)
                                         : g_strdup(text);
  g_free(s->commit);
  s->commit = joined;
}

static void render(FcitxPinyin *s) {
  RimeApi *api = pinyin_rime_api();
  if (!api || !s->rime)
    return;
  g_free(s->raw);
  s->raw = g_strdup(api->get_input(s->rime));
  g_free(s->preedit);
  s->preedit = g_strdup("");
  s->caret = 0;
  int english = api->get_option(s->rime, "ascii_mode");
  if (english != pinyin_input_english())
    pinyin_set_input_english(english);
  s->revision++;
  s->cursor = 0;
  s->navigated = 0;
  g_ptr_array_set_size(s->local, 0);
  g_ptr_array_set_size(s->personal, 0);
  g_ptr_array_set_size(s->cloud, 0);
  RIME_STRUCT(RimeContext, ctx);
  if (api->get_context(s->rime, &ctx)) {
    g_free(s->preedit);
    s->preedit =
        g_strdup(ctx.composition.preedit ? ctx.composition.preedit : "");
    s->caret = ctx.composition.cursor_pos;
    api->free_context(&ctx);
  }
  RimeCandidateListIterator iterator;
  if (api->candidate_list_begin(s->rime, &iterator)) {
    int index = 0;
    while (index < 50 && api->candidate_list_next(&iterator))
      g_ptr_array_add(s->local, choice(iterator.candidate.text, "", index++));
    api->candidate_list_end(&iterator);
  }
  goPinyinChanged(s->id, s->revision, s->raw, s->preedit);
  merge(s);
}

static void clear(FcitxPinyin *s) {
  if (s->rime)
    pinyin_rime_api()->clear_composition(s->rime);
  render(s);
}

static int collect_commit(FcitxPinyin *s, const char *before) {
  RimeApi *api = pinyin_rime_api();
  RIME_STRUCT(RimeCommit, commit);
  if (api && api->get_commit(s->rime, &commit)) {
    append_commit(s, commit.text);
    const char *remaining = api->get_input(s->rime);
    if (before && *before && (!remaining || !*remaining))
      goPinyinLearn((char *)before, commit.text);
    api->free_commit(&commit);
    return 1;
  }
  return 0;
}

static void commit_partial(FcitxPinyin *s) {
  RimeApi *api = pinyin_rime_api();
  const char *raw = api->get_input(s->rime);
  if (!raw || !*raw || api->get_caret_pos(s->rime) != strlen(raw) ||
      !RIME_API_AVAILABLE(api, set_input))
    return;
  RIME_STRUCT(RimeContext, ctx);
  if (!api->get_context(s->rime, &ctx))
    return;
  const char *text = ctx.composition.preedit;
  int start = ctx.composition.sel_start;
  int count = 0;
  if (!text || start <= 0 || start >= (int)strlen(text) ||
      !g_utf8_validate(text, -1, NULL) || !g_utf8_validate(text, start, NULL))
    goto done;
  for (int i = 0; i < start; i++)
    if (g_ascii_isalpha(text[i]))
      goto done;
  for (const char *p = text + start; *p; p = g_utf8_next_char(p)) {
    gunichar ch = g_utf8_get_char(p);
    if (ch == ' ' || ch == '\'')
      continue;
    if (!(ch < 128 && g_ascii_isalpha(ch)) && ch != 0xfc)
      goto done;
    count++;
  }
  if (!count)
    goto done;
  const char *suffix = raw + strlen(raw);
  while (suffix > raw && count) {
    char ch = *--suffix;
    if (g_ascii_isalpha(ch))
      count--;
    else if (ch != '\'')
      goto done;
  }
  if (count || suffix == raw)
    goto done;
  const char *end = suffix;
  while (end > raw && end[-1] == '\'')
    end--;
  while (*suffix == '\'')
    suffix++;
  char *confirmed = g_strndup(text, start);
  char *prefix = g_strndup(raw, end - raw);
  char *remaining = g_strdup(suffix);
  append_commit(s, confirmed);
  api->set_input(s->rime, remaining);
  goPinyinLearn(prefix, confirmed);
  g_free(remaining);
  g_free(prefix);
  g_free(confirmed);
done:
  api->free_context(&ctx);
}

static void choose(FcitxPinyin *s, int index) {
  RimeApi *api = pinyin_rime_api();
  if (!api || index < 0 || index >= (int)s->merged->len)
    return;
  FcitxChoice *c = g_ptr_array_index(s->merged, index);
  char *before = g_strdup(s->raw);
  if (c->index >= 0) {
    api->select_candidate(s->rime, c->index);
    if (!collect_commit(s, before))
      commit_partial(s);
  } else {
    append_commit(s, c->text);
    goPinyinLearn(before, c->text);
    api->clear_composition(s->rime);
  }
  g_free(before);
  render(s);
}

static void toggle_mode(FcitxPinyin *s) {
  RimeApi *api = pinyin_rime_api();
  int english = !api->get_option(s->rime, "ascii_mode");
  const char *raw = api->get_input(s->rime);
  if (english && raw && *raw)
    append_commit(s, raw);
  api->clear_composition(s->rime);
  pinyin_set_input_english(english);
  api->set_option(s->rime, "ascii_mode", english);
  render(s);
}

static int shift_key(FcitxPinyin *s, guint keyval, guint state) {
  guint bit = keyval == SX_KEY_SHIFT_L ? 1 : keyval == SX_KEY_SHIFT_R ? 2 : 0;
  guint chord = state & (SX_CONTROL_MASK | SX_MOD1_MASK | SX_SUPER_MASK);
  if (!bit) {
    if (s->shift_held)
      s->shift_alone = 0;
    return -1;
  }
  if (state & SX_RELEASE_MASK) {
    int toggle = (s->shift_held & bit) && s->shift_alone && !chord;
    s->shift_held &= ~bit;
    s->shift_alone = 0;
    if (toggle) {
      toggle_mode(s);
      return TRUE;
    }
    return FALSE;
  }
  if (!(s->shift_held & bit)) {
    s->shift_alone = !s->shift_held && !chord;
    s->shift_held |= bit;
  }
  return FALSE;
}

static int process(FcitxPinyin *s, guint keyval, guint state,
                   const char *text) {
  RimeApi *api = pinyin_rime_api();
  s->modifiers = state;
  if (!goPinyinEnabled()) {
    s->shift_held = s->shift_alone = 0;
    return FALSE;
  }
  if (!s->rime)
    return FALSE;
  if (pinyin_ensure_session_schema(s->rime))
    render(s);
  int shift = shift_key(s, keyval, state);
  if (shift >= 0)
    return shift;
  if (state & SX_RELEASE_MASK)
    return FALSE;
  state &= SX_SHIFT_MASK | SX_CONTROL_MASK | SX_MOD1_MASK | SX_SUPER_MASK;
  int word_delete = s->raw && *s->raw && (state & SX_CONTROL_MASK) &&
                    !(state & (SX_MOD1_MASK | SX_SUPER_MASK)) &&
                    (keyval == SX_KEY_BACKSPACE || keyval == SX_KEY_DELETE);
  if ((state & (SX_CONTROL_MASK | SX_MOD1_MASK | SX_SUPER_MASK)) &&
      !word_delete)
    return FALSE;
  if (api->get_option(s->rime, "ascii_mode")) {
    if (text && *text) {
      append_commit(s, text);
      return TRUE;
    }
    return FALSE;
  }
  if (s->raw && *s->raw) {
    if (keyval == SX_KEY_ESCAPE) {
      clear(s);
      return TRUE;
    }
    if (keyval == SX_KEY_SPACE) {
      choose(s, s->cursor);
      return TRUE;
    }
    if (keyval >= '1' && keyval <= '9') {
      choose(s, (s->cursor / 9) * 9 + keyval - '1');
      return TRUE;
    }
    int delta = 0;
    if (keyval == SX_KEY_UP)
      delta = -1;
    else if (keyval == SX_KEY_DOWN)
      delta = 1;
    else if (keyval == SX_KEY_PAGE_UP)
      delta = -9;
    else if (keyval == SX_KEY_PAGE_DOWN)
      delta = 9;
    if (delta) {
      s->navigated = 1;
      s->revision++;
      s->cursor =
          CLAMP(s->cursor + delta, 0, MAX(0, (int)s->merged->len - 1));
      goPinyinChanged(s->id, s->revision, "", "");
      return TRUE;
    }
  }
  /* With an empty composition, hand bare editing keys (BackSpace, arrows,
   * F-keys, ...) straight back to the application: the rime editor consumes
   * them even when there is nothing to edit. */
  if ((!s->raw || !*s->raw) && (!text || !*text))
    return FALSE;
  char *before = g_strdup(s->raw);
  int handled;
  if (word_delete && keyval == SX_KEY_DELETE) {
    size_t caret = api->get_caret_pos(s->rime);
    api->process_key(s->rime, SX_KEY_RIGHT, 0);
    size_t end = api->get_caret_pos(s->rime);
    api->set_caret_pos(s->rime, caret);
    while (end > caret) {
      if (!api->process_key(s->rime, SX_KEY_DELETE, 0))
        break;
      end--;
    }
    handled = TRUE;
  } else {
    handled = api->process_key(s->rime, keyval, state);
  }
  if (word_delete && !handled)
    handled = TRUE;
  collect_commit(s, before);
  g_free(before);
  if (!handled && text && *text && !api->get_input(s->rime)[0]) {
    /* Let the daemon return ordinary characters instead of forwarding them
     * after a synchronous D-Bus reply. */
    append_commit(s, text);
    handled = TRUE;
  }
  if (handled)
    render(s);
  return handled;
}

static GVariant *state_variant(FcitxPinyin *s, int handled) {
  GVariantBuilder candidates;
  g_variant_builder_init(&candidates, G_VARIANT_TYPE("a(ss)"));
  if (s) {
    for (guint i = 0; i < s->merged->len; i++) {
      FcitxChoice *c = g_ptr_array_index(s->merged, i);
      g_variant_builder_add(&candidates, "(ss)", c->text, c->source);
    }
  }
  return g_variant_new(SIXUAN_FCITX_STATE_TYPE,
                       s ? s->id : (guint64)0,
                       s ? s->revision : (guint64)0, handled ? TRUE : FALSE,
                       s && pinyin_input_english() ? TRUE : FALSE,
                       s && s->preedit ? s->preedit : "",
                       s ? s->caret : 0, s ? s->cursor : 0,
                       /* Bare "a(ss)" consumes an open builder and ends it. */
                       &candidates,
                       s && s->raw ? s->raw : "",
                       s && s->commit ? s->commit : "");
}

GVariant *fcitx_pinyin_key(uint64_t id, uint32_t keyval, uint32_t state,
                           const char *text) {
  FcitxPinyin *s = id ? find(id, 0) : NULL;
  if (!s)
    s = create();
  if (!s)
    return state_variant(NULL, FALSE);
  g_free(s->commit);
  s->commit = g_strdup("");
  int handled = process(s, keyval, state, text);
  return state_variant(s, handled);
}

GVariant *fcitx_pinyin_select(uint64_t id, uint64_t revision, int index) {
  FcitxPinyin *s = find(id, 0);
  if (!s)
    return state_variant(NULL, FALSE);
  g_free(s->commit);
  s->commit = g_strdup("");
  if (revision != s->revision) {
    /* Stale click: the composition moved on after the candidate list was
     * rendered. Refuse the selection and let the addon refresh its UI. */
    return state_variant(s, TRUE);
  }
  choose(s, index);
  return state_variant(s, TRUE);
}

GVariant *fcitx_pinyin_state(uint64_t id) {
  FcitxPinyin *s = find(id, 0);
  if (s) {
    g_free(s->commit);
    s->commit = g_strdup("");
  }
  return state_variant(s, FALSE);
}

int fcitx_pinyin_close(uint64_t id) {
  if (!sessions || !g_hash_table_remove(sessions, (gpointer)(uintptr_t)id))
    return FALSE;
  return TRUE;
}

int fcitx_pinyin_add(uint64_t id, uint64_t revision, const char *text,
                     const char *source, int cloud) {
  FcitxPinyin *s = find(id, revision);
  if (!s || s->navigated)
    return FALSE;
  GPtrArray *items = cloud ? s->cloud : s->personal;
  if (items->len >= 10)
    return TRUE;
  g_ptr_array_add(items, choice(text, source, -1));
  return TRUE;
}

int fcitx_pinyin_refresh(uint64_t id, uint64_t revision) {
  FcitxPinyin *s = find(id, revision);
  if (!s || s->navigated)
    return FALSE;
  merge(s);
  native_fcitx_publish_state(state_variant(s, FALSE));
  return TRUE;
}

void fcitx_pinyin_preferences_changed(void) {
  if (!sessions)
    return;
  GHashTableIter iterator;
  gpointer item;
  g_hash_table_iter_init(&iterator, sessions);
  while (g_hash_table_iter_next(&iterator, NULL, &item)) {
    FcitxPinyin *s = item;
    if (s->rime && pinyin_ensure_session_schema(s->rime))
      render(s);
    if (!goPinyinEnabled())
      clear(s);
    else if (!goPreferenceValue("cloud_candidates")) {
      g_ptr_array_set_size(s->cloud, 0);
      merge(s);
    }
    native_fcitx_publish_state(state_variant(s, FALSE));
  }
}

char *fcitx_pinyin_schema_status(void) {
  if (!sessions)
    return g_strdup("");
  GHashTableIter iterator;
  gpointer item;
  g_hash_table_iter_init(&iterator, sessions);
  while (g_hash_table_iter_next(&iterator, NULL, &item)) {
    FcitxPinyin *s = item;
    RimeApi *api = pinyin_rime_api();
    if (!api || !s->rime)
      continue;
    RIME_STRUCT(RimeStatus, status);
    if (api->get_status(s->rime, &status)) {
      char *result = g_strdup(status.schema_id ? status.schema_id : "");
      api->free_status(&status);
      return result;
    }
  }
  return g_strdup("");
}
