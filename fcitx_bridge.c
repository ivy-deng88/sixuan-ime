#include "fcitx_protocol.h"

#include <glib.h>
#include <string.h>

extern int goFcitxGesture(int, int);

static GDBusConnection *session_bus(void) {
  static GDBusConnection *connection;
  if (!connection) {
    GError *error = NULL;
    connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    g_clear_error(&error);
  }
  return connection;
}

static int name_has_owner(GDBusConnection *bus, const char *name) {
  if (!bus)
    return FALSE;
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "NameHasOwner", g_variant_new("(s)", name),
      G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
  g_clear_error(&error);
  if (!reply)
    return FALSE;
  gboolean owned = FALSE;
  g_variant_get(reply, "(b)", &owned);
  g_variant_unref(reply);
  return owned;
}

static GVariant *controller_call(const char *method, GVariant *params,
                                 const char *reply_type) {
  GDBusConnection *bus = session_bus();
  if (!bus)
    return NULL;
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_sync(
      bus, SIXUAN_FCITX5_SERVICE, SIXUAN_FCITX5_PATH,
      SIXUAN_FCITX5_INTERFACE, method, params,
      reply_type ? G_VARIANT_TYPE(reply_type) : NULL, G_DBUS_CALL_FLAGS_NONE,
      3000, NULL, &error);
  if (error) {
    g_message("Fcitx 5 %s failed: %s", method, error->message);
    g_clear_error(&error);
  }
  return reply;
}

int native_fcitx_running(void) {
  return name_has_owner(session_bus(), SIXUAN_FCITX5_SERVICE);
}

int native_fcitx_frontend_available(void) {
  return name_has_owner(session_bus(), SIXUAN_FCITX_SERVICE);
}

static int available_has_input_method(GVariant *reply) {
  if (!reply || g_variant_n_children(reply) != 1)
    return FALSE;
  GVariant *array = g_variant_get_child_value(reply, 0);
  int found = FALSE;
  for (gsize i = 0; i < g_variant_n_children(array); i++) {
    GVariant *entry = g_variant_get_child_value(array, i);
    GVariant *name = g_variant_get_child_value(entry, 0);
    const char *value = g_variant_get_string(name, NULL);
    if (!strcmp(value, "sixuan"))
      found = TRUE;
    g_variant_unref(name);
    g_variant_unref(entry);
    if (found)
      break;
  }
  g_variant_unref(array);
  return found;
}

static int fcitx_has_sixuan(void) {
  GVariant *reply = controller_call(
      "AvailableInputMethods", NULL, "(a(ssssssb))");
  int found = available_has_input_method(reply);
  if (reply)
    g_variant_unref(reply);
  return found;
}

char *native_fcitx_register(void) {
  GDBusConnection *bus = session_bus();
  if (!bus)
    return g_strdup("无法连接会话 D-Bus");
  if (!native_fcitx_running())
    return g_strdup("Fcitx 5 未运行，请先启动 Fcitx 5");

  if (!fcitx_has_sixuan()) {
    GVariant *restart = controller_call("Restart", NULL, NULL);
    if (restart)
      g_variant_unref(restart);
    for (int i = 0; i < 60 && !fcitx_has_sixuan(); i++)
      g_usleep(100000);
  }
  if (!fcitx_has_sixuan())
    return g_strdup("Fcitx 5 未加载思玄插件，请重启 Fcitx 5 后重试");

  GVariant *group_reply =
      controller_call("CurrentInputMethodGroup", NULL, "(s)");
  if (!group_reply)
    return g_strdup("无法读取 Fcitx 5 当前输入法组");
  const char *group = NULL;
  g_variant_get(group_reply, "(&s)", &group);
  gchar *group_name = g_strdup(group);
  g_variant_unref(group_reply);

  GVariant *info = controller_call("InputMethodGroupInfo",
                                   g_variant_new("(s)", group_name),
                                   "(sa(ss))");
  if (!info) {
    g_free(group_name);
    return g_strdup("无法读取 Fcitx 5 输入法组配置");
  }
  const char *layout = NULL;
  GVariant *items = NULL;
  g_variant_get(info, "(&s@a(ss))", &layout, &items);
  gchar *default_layout = g_strdup(layout);

  gboolean found = FALSE;
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ss)"));
  for (gsize i = 0; i < g_variant_n_children(items); i++) {
    GVariant *item = g_variant_get_child_value(items, i);
    const char *name = NULL, *item_layout = NULL;
    g_variant_get(item, "(&s&s)", &name, &item_layout);
    if (!strcmp(name, "sixuan"))
      found = TRUE;
    g_variant_builder_add(&builder, "(ss)", name, item_layout);
    g_variant_unref(item);
  }
  if (!found)
    g_variant_builder_add(&builder, "(ss)", "sixuan", "");
  g_variant_unref(items);
  g_variant_unref(info);

  if (!found) {
    GVariant *set = controller_call(
        "SetInputMethodGroupInfo",
        g_variant_new("(ss@a(ss))", group_name, default_layout,
                      g_variant_builder_end(&builder)),
        NULL);
    if (set)
      g_variant_unref(set);
  } else {
    g_variant_builder_clear(&builder);
  }
  g_free(default_layout);
  g_free(group_name);
  return NULL;
}

static GVariant *call_frontend(const char *method, GVariant *params,
                               const char *reply_type) {
  GDBusConnection *bus = session_bus();
  if (!bus || !name_has_owner(bus, SIXUAN_FCITX_SERVICE)) {
    if (params)
      g_variant_unref(g_variant_ref_sink(params));
    return NULL;
  }
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_sync(
      bus, SIXUAN_FCITX_SERVICE, SIXUAN_FCITX_PATH,
      SIXUAN_FCITX_INTERFACE, method, params,
      reply_type ? G_VARIANT_TYPE(reply_type) : NULL, G_DBUS_CALL_FLAGS_NONE,
      2000, NULL, &error);
  if (error) {
    g_message("Sixuan Fcitx frontend %s failed: %s", method, error->message);
    g_clear_error(&error);
  }
  return reply;
}

int native_fcitx_deliver_text(uint64_t generation, const char *text, int final) {
  (void)generation;
  GVariant *reply = call_frontend(
      "DeliverText",
      g_variant_new("(sb)", text ? text : "", final ? TRUE : FALSE), "(b)");
  if (!reply)
    return FALSE;
  gboolean delivered = FALSE;
  g_variant_get(reply, "(b)", &delivered);
  g_variant_unref(reply);
  return delivered;
}

void native_fcitx_cancel_text(void) {
  GVariant *reply = call_frontend("CancelText", NULL, NULL);
  if (reply)
    g_variant_unref(reply);
}

static GVariant *copy_state(GVariant *state) {
  if (!state)
    return NULL;
  guint64 session = 0;
  gboolean handled = FALSE, english = FALSE;
  const char *preedit = NULL, *raw = NULL, *commit = NULL;
  gint32 caret = 0, cursor = 0;
  GVariant *array = NULL;
  g_variant_get(state, SIXUAN_FCITX_STATE_TYPE, &session, &handled, &english,
                &preedit, &caret, &cursor, &array, &raw, &commit);
  GVariantBuilder candidates;
  g_variant_builder_init(&candidates, G_VARIANT_TYPE("a(ss)"));
  for (gsize i = 0; i < g_variant_n_children(array); i++) {
    GVariant *item = g_variant_get_child_value(array, i);
    const char *text = NULL, *source = NULL;
    g_variant_get(item, "(&s&s)", &text, &source);
    g_variant_builder_add(&candidates, "(ss)", text ? text : "",
                          source ? source : "");
    g_variant_unref(item);
  }
  g_variant_unref(array);
  return g_variant_new(SIXUAN_FCITX_STATE_TYPE, session, handled, english,
                       preedit ? preedit : "", caret, cursor,
                       g_variant_builder_end(&candidates), raw ? raw : "",
                       commit ? commit : "");
}

void native_fcitx_publish_state(GVariant *state) {
  if (!state)
    return;
  GVariant *sunk = g_variant_ref_sink(state);
  GVariant *params = copy_state(state);
  GVariant *reply = call_frontend("RefreshCandidates", params, NULL);
  if (reply)
    g_variant_unref(reply);
  g_variant_unref(sunk);
}

static void return_state(GDBusMethodInvocation *invocation, GVariant *state) {
  GVariant *tuple = copy_state(state);
  if (!tuple)
    tuple = g_variant_new(SIXUAN_FCITX_STATE_TYPE, (guint64)0, FALSE, FALSE,
                          "", 0, 0,
                          g_variant_new_array(G_VARIANT_TYPE("(ss)"), NULL, 0),
                          "", "");
  g_dbus_method_invocation_return_value(invocation, tuple);
}

int native_fcitx_command_call(const char *method, GVariant *params,
                              GDBusMethodInvocation *invocation) {
  if (!strcmp(method, "PinyinKey")) {
    guint64 session;
    guint32 keyval, state;
    const char *text;
    g_variant_get(params, "(tuus)", &session, &keyval, &state, &text);
    GVariant *result = fcitx_pinyin_key(session, keyval, state, text);
    return_state(invocation, result);
    g_variant_unref(g_variant_ref_sink(result));
    return TRUE;
  }
  if (!strcmp(method, "PinyinSelect")) {
    guint64 session;
    gint32 index;
    g_variant_get(params, "(ti)", &session, &index);
    GVariant *result = fcitx_pinyin_select(session, index);
    return_state(invocation, result);
    g_variant_unref(g_variant_ref_sink(result));
    return TRUE;
  }
  if (!strcmp(method, "PinyinState")) {
    guint64 session;
    g_variant_get(params, "(t)", &session);
    GVariant *result = fcitx_pinyin_state(session);
    return_state(invocation, result);
    g_variant_unref(g_variant_ref_sink(result));
    return TRUE;
  }
  if (!strcmp(method, "PinyinClose")) {
    guint64 session;
    g_variant_get(params, "(t)", &session);
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(b)", fcitx_pinyin_close(session)));
    return TRUE;
  }
  if (!strcmp(method, "GestureKey")) {
    gint32 code, value;
    g_variant_get(params, "(ii)", &code, &value);
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(b)", goFcitxGesture(code, value)));
    return TRUE;
  }
  return FALSE;
}
