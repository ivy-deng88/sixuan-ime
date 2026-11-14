/* No display, service, or bus connection is needed for these settings paths. */
#include "../desktop.c"

/* desktop.c probes the Fcitx bridge, which is not linked into this test.
 * Report Fcitx as absent so the IBus/GNOME paths stay exercisable. */
int native_fcitx_running(void) { return 0; }
int native_fcitx_command_call(const char *method, GVariant *params,
                              GDBusMethodInvocation *invocation) {
  (void)method;
  (void)params;
  (void)invocation;
  return 0;
}

char *goBuildName(int index) {
  return g_strdup(index == 1 ? "test-pinyin" : "");
}

int main(void) {
  g_assert_null(g_settings_schema_source_get_default());
  native_space(0);
  native_space(1);
  native_enable_pinyin(0);
  return 0;
}
