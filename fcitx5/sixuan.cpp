#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>

#include <gio/gio.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

FCITX_DEFINE_LOG_CATEGORY(sixuan_log, "sixuan");
#define SX_DEBUG() FCITX_LOGC(sixuan_log, Debug)
#define SX_WARN() FCITX_LOGC(sixuan_log, Warn)

namespace {

constexpr const char *kDaemonService = "dev.sixuan.IME";
constexpr const char *kDaemonPath = "/dev/sixuan/IME";
constexpr const char *kDaemonInterface = "dev.sixuan.IME";
constexpr const char *kFrontendService = "dev.sixuan.Fcitx5";
constexpr const char *kFrontendPath = "/dev/sixuan/Fcitx5";
constexpr const char *kFrontendInterface = "dev.sixuan.Fcitx5";
constexpr const char *kStateSignature = "(ttbbsiia(ss)ss)";

constexpr guint32 kShiftMask = 1u << 0;
constexpr guint32 kLockMask = 1u << 1;
constexpr guint32 kControlMask = 1u << 2;
constexpr guint32 kAltMask = 1u << 3;
constexpr guint32 kSuperMask = 1u << 6;
constexpr guint32 kReleaseMask = 1u << 30;

struct Candidate {
    std::string text;
    std::string source;
};

struct SessionState {
    uint64_t session = 0;
    uint64_t revision = 0;
    std::string preedit;
    std::string raw;
    int32_t caret = 0;
    int32_t cursor = 0;
    bool english = false;
    std::vector<Candidate> candidates;
};

class SixuanEngine;

class SixuanCandidateWord final : public fcitx::CandidateWord {
public:
    SixuanCandidateWord(SixuanEngine *engine, fcitx::InputContext *ic,
                        uint64_t session, uint64_t revision, int index,
                        const Candidate &candidate)
        : engine_(engine), ic_(ic), session_(session), revision_(revision),
          index_(index) {
        setText(fcitx::Text(candidate.text));
        if (!candidate.source.empty())
            setComment(fcitx::Text(candidate.source));
    }

    void select(fcitx::InputContext *inputContext) const override;

private:
    SixuanEngine *engine_;
    fcitx::InputContext *ic_;
    uint64_t session_;
    uint64_t revision_;
    int index_;
};

class SixuanEngine final : public fcitx::InputMethodEngineV2 {
public:
    explicit SixuanEngine(fcitx::Instance *instance);
    ~SixuanEngine() override;

    void activate(const fcitx::InputMethodEntry &entry,
                  fcitx::InputContextEvent &event) override;
    void deactivate(const fcitx::InputMethodEntry &entry,
                    fcitx::InputContextEvent &event) override;
    void reset(const fcitx::InputMethodEntry &entry,
               fcitx::InputContextEvent &event) override;
    void keyEvent(const fcitx::InputMethodEntry &entry,
                  fcitx::KeyEvent &event) override;
    std::string subMode(const fcitx::InputMethodEntry &entry,
                        fcitx::InputContext &ic) override;
    std::string subModeLabelImpl(const fcitx::InputMethodEntry &entry,
                                 fcitx::InputContext &ic) override;

    void selectCandidate(fcitx::InputContext *ic, uint64_t session,
                         uint64_t revision, int index);

private:
    friend class SixuanCandidateWord;

    SessionState *stateFor(fcitx::InputContext *ic);
    GVariant *call(const char *method, GVariant *params, const char *replyType,
                   int timeout = 1200);
    bool applySnapshot(fcitx::InputContext *ic, SessionState &state,
                       GVariant *reply, bool checkSequence);
    void updateUI(fcitx::InputContext *ic, SessionState &state);
    bool pinyinKey(fcitx::InputContext *ic, SessionState &state,
                   guint32 keyval, guint32 rimeState, const std::string &text,
                   bool *handled);
    void closeSession(SessionState &state);

    static void handleFrontendCall(GDBusConnection *, const gchar *,
                                   const gchar *, const gchar *,
                                   const gchar *, GVariant *,
                                   GDBusMethodInvocation *, gpointer);

    fcitx::Instance *instance_;
    GDBusConnection *bus_ = nullptr;
    guint registrationId_ = 0;
    guint ownerId_ = 0;
    std::unordered_map<fcitx::InputContext *, std::unique_ptr<SessionState>>
        states_;
    fcitx::InputContext *active_ = nullptr;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>
        destroyHandler_;
};

const char *frontendXml =
    "<node>"
    "<interface name='dev.sixuan.Fcitx5'>"
    "<method name='RefreshCandidates'>"
    "<arg type='t' direction='in'/><arg type='b' direction='in'/>"
    "<arg type='b' direction='in'/><arg type='s' direction='in'/>"
    "<arg type='i' direction='in'/><arg type='i' direction='in'/>"
    "<arg type='a(ss)' direction='in'/><arg type='s' direction='in'/>"
    "<arg type='s' direction='in'/>"
    "</method>"
    "</interface>"
    "</node>";

guint32 rimeState(const fcitx::KeyEvent &event) {
    const auto states = event.rawKey().states();
    guint32 value = 0;
    if (states.test(fcitx::KeyState::Shift))
        value |= kShiftMask;
    if (states.test(fcitx::KeyState::CapsLock))
        value |= kLockMask;
    if (states.test(fcitx::KeyState::Ctrl))
        value |= kControlMask;
    if (states.test(fcitx::KeyState::Alt))
        value |= kAltMask;
    if (states.test(fcitx::KeyState::Super) ||
        states.test(fcitx::KeyState::Super2))
        value |= kSuperMask;
    if (event.isRelease())
        value |= kReleaseMask;
    return value;
}

bool sensitive(fcitx::InputContext *ic) {
    return ic->capabilityFlags().testAny(
        fcitx::CapabilityFlags{fcitx::CapabilityFlag::Password,
                               fcitx::CapabilityFlag::Sensitive,
                               fcitx::CapabilityFlag::Disable});
}

bool isPrintableControl(fcitx::KeySym sym) {
    switch (sym) {
    case FcitxKey_BackSpace:
    case FcitxKey_Delete:
    case FcitxKey_Return:
    case FcitxKey_KP_Enter:
    case FcitxKey_Up:
    case FcitxKey_Down:
    case FcitxKey_Page_Up:
    case FcitxKey_Page_Down:
    case FcitxKey_Escape:
    case FcitxKey_space:
    case FcitxKey_Shift_L:
    case FcitxKey_Shift_R:
        return true;
    default:
        return false;
    }
}

} // namespace

SixuanEngine::SixuanEngine(fcitx::Instance *instance) : instance_(instance) {
    bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (bus_) {
        GError *error = nullptr;
        GDBusNodeInfo *info =
            g_dbus_node_info_new_for_xml(frontendXml, &error);
        if (info) {
            static const GDBusInterfaceVTable vtable = [] {
                GDBusInterfaceVTable value{};
                value.method_call = &SixuanEngine::handleFrontendCall;
                return value;
            }();
            registrationId_ = g_dbus_connection_register_object(
                bus_, kFrontendPath, info->interfaces[0], &vtable, this, nullptr,
                nullptr);
            g_dbus_node_info_unref(info);
        }
        g_clear_error(&error);
        ownerId_ = g_bus_own_name_on_connection(
            bus_, kFrontendService, G_BUS_NAME_OWNER_FLAGS_NONE, nullptr,
            nullptr, this, nullptr);
    }
    // Sessions are keyed by InputContext pointer; clean up daemon-side state
    // when the context itself is destroyed.
    destroyHandler_ = instance_->watchEvent(
        fcitx::EventType::InputContextDestroyed,
        fcitx::EventWatcherPhase::Default, [this](fcitx::Event &event) {
            auto *ic =
                static_cast<fcitx::InputContextEvent &>(event).inputContext();
            auto iter = states_.find(ic);
            if (iter != states_.end()) {
                closeSession(*iter->second);
                states_.erase(iter);
            }
            if (active_ == ic)
                active_ = nullptr;
        });
}

SixuanEngine::~SixuanEngine() {
    destroyHandler_.reset();
    for (auto &pair : states_)
        closeSession(*pair.second);
    states_.clear();
    if (ownerId_)
        g_bus_unown_name(ownerId_);
    if (registrationId_ && bus_)
        g_dbus_connection_unregister_object(bus_, registrationId_);
    if (bus_)
        g_object_unref(bus_);
}

SessionState *SixuanEngine::stateFor(fcitx::InputContext *ic) {
    auto iter = states_.find(ic);
    if (iter != states_.end())
        return iter->second.get();
    auto state = std::make_unique<SessionState>();
    auto *result = state.get();
    states_.emplace(ic, std::move(state));
    return result;
}

GVariant *SixuanEngine::call(const char *method, GVariant *params,
                             const char *replyType, int timeout) {
    if (!bus_)
        return nullptr;
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(
        bus_, kDaemonService, kDaemonPath, kDaemonInterface, method, params,
        replyType ? G_VARIANT_TYPE(replyType) : nullptr,
        G_DBUS_CALL_FLAGS_NONE, timeout, nullptr, &error);
    if (error) {
        SX_WARN() << "daemon call " << method << " failed: " << error->message;
        g_clear_error(&error);
    }
    return reply;
}

void SixuanEngine::activate(const fcitx::InputMethodEntry & /*entry*/,
                            fcitx::InputContextEvent &event) {
    active_ = event.inputContext();
    stateFor(active_);
}

void SixuanEngine::closeSession(SessionState &state) {
    if (!state.session)
        return;
    GVariant *reply =
        call("PinyinClose", g_variant_new("(t)", state.session), "(b)");
    if (reply)
        g_variant_unref(reply);
    state.session = 0;
}

void SixuanEngine::deactivate(const fcitx::InputMethodEntry & /*entry*/,
                              fcitx::InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto iter = states_.find(ic);
    if (iter != states_.end()) {
        closeSession(*iter->second);
        iter->second->preedit.clear();
        iter->second->candidates.clear();
    }
    if (active_ == ic)
        active_ = nullptr;
    ic->inputPanel().reset();
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void SixuanEngine::reset(const fcitx::InputMethodEntry & /*entry*/,
                         fcitx::InputContextEvent &event) {
    auto *ic = event.inputContext();
    auto iter = states_.find(ic);
    if (iter != states_.end()) {
        closeSession(*iter->second);
        iter->second->preedit.clear();
        iter->second->candidates.clear();
    }
    ic->inputPanel().reset();
    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

bool SixuanEngine::pinyinKey(fcitx::InputContext *ic, SessionState &state,
                             guint32 keyval, guint32 stateMask,
                             const std::string &text, bool *handled) {
    GVariant *reply =
        call("PinyinKey",
             g_variant_new("(tuus)", state.session, keyval, stateMask,
                           text.c_str()),
             kStateSignature);
    if (!reply)
        return false;
    *handled = applySnapshot(ic, state, reply, false);
    g_variant_unref(reply);
    return true;
}

bool SixuanEngine::applySnapshot(fcitx::InputContext *ic, SessionState &state,
                                 GVariant *reply, bool /*checkSequence*/) {
    guint64 session = 0, revision = 0;
    gboolean handled = FALSE, english = FALSE;
    const char *preedit = nullptr, *raw = nullptr, *commit = nullptr;
    gint32 caret = 0, cursor = 0;
    GVariant *array = nullptr;
    g_variant_get(reply, "(ttbbsii@a(ss)ss)", &session, &revision, &handled,
                  &english, &preedit, &caret, &cursor, &array, &raw, &commit);

    if (session != 0)
        state.session = session;
    state.revision = revision;
    state.english = english;
    state.preedit = preedit ? preedit : "";
    state.caret = caret;
    state.cursor = cursor;
    state.raw = raw ? raw : "";
    state.candidates.clear();
    for (gsize i = 0; i < g_variant_n_children(array); i++) {
        GVariant *item = g_variant_get_child_value(array, i);
        const char *text = nullptr, *source = nullptr;
        g_variant_get(item, "(&s&s)", &text, &source);
        state.candidates.push_back({text ? text : "", source ? source : ""});
        g_variant_unref(item);
    }
    g_variant_unref(array);

    if (commit && *commit)
        ic->commitString(commit);
    updateUI(ic, state);
    return handled;
}

void SixuanEngine::updateUI(fcitx::InputContext *ic, SessionState &state) {
    auto &panel = ic->inputPanel();
    panel.reset();

    if (!state.preedit.empty()) {
        fcitx::Text preedit;
        preedit.append(state.preedit, fcitx::TextFormatFlag::Underline);
        preedit.setCursor(state.caret);
        if (ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit))
            panel.setClientPreedit(preedit);
        else
            panel.setPreedit(preedit);
    }

    if (!state.candidates.empty()) {
        auto list = std::make_unique<fcitx::CommonCandidateList>();
        for (size_t i = 0; i < state.candidates.size(); i++) {
            list->append<SixuanCandidateWord>(
                this, ic, state.session, state.revision, static_cast<int>(i),
                state.candidates[i]);
        }
        list->setPageSize(9);
        list->setSelectionKey(
            {fcitx::Key(FcitxKey_1), fcitx::Key(FcitxKey_2),
             fcitx::Key(FcitxKey_3), fcitx::Key(FcitxKey_4),
             fcitx::Key(FcitxKey_5), fcitx::Key(FcitxKey_6),
             fcitx::Key(FcitxKey_7), fcitx::Key(FcitxKey_8),
             fcitx::Key(FcitxKey_9)});
        list->setGlobalCursorIndex(state.cursor);
        panel.setCandidateList(std::move(list));
    } else {
        panel.setCandidateList(nullptr);
    }

    ic->updatePreedit();
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void SixuanEngine::keyEvent(const fcitx::InputMethodEntry & /*entry*/,
                            fcitx::KeyEvent &event) {
    auto *ic = event.inputContext();
    active_ = ic;
    if (sensitive(ic))
        return;

    auto *state = stateFor(ic);
    const auto sym = event.rawKey().sym();
    const auto stateMask = rimeState(event);
    const bool release = event.isRelease();
    const bool hasComposition =
        !state->preedit.empty() || !state->candidates.empty();

    if (!release && sym == FcitxKey_Escape) {
        if (hasComposition) {
            bool handled = false;
            if (pinyinKey(ic, *state, sym, stateMask, "", &handled) &&
                handled)
                event.filterAndAccept();
        }
        return;
    }

    if (!release && sym == FcitxKey_space) {
        if (hasComposition) {
            bool handled = false;
            if (pinyinKey(ic, *state, sym, stateMask, " ", &handled) &&
                handled)
                event.filterAndAccept();
        }
        return;
    }

    if (sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R) {
        bool handled = false;
        if (pinyinKey(ic, *state, sym, stateMask, "", &handled) && handled)
            event.filterAndAccept();
        return;
    }

    if (release)
        return;

    const bool chord =
        event.rawKey().states().testAny(fcitx::KeyStates{
            fcitx::KeyState::Ctrl, fcitx::KeyState::Alt,
            fcitx::KeyState::Super});
    const bool wordDelete =
        hasComposition &&
        event.rawKey().states().test(fcitx::KeyState::Ctrl) &&
        (sym == FcitxKey_BackSpace || sym == FcitxKey_Delete);
    if (chord && !wordDelete)
        return;

    /* Without a composition, bare editing keys (BackSpace, arrows, F-keys,
     * ...) belong to the application: pass them through without a round
     * trip to the daemon. keySymToUTF8 maps editing keys to control
     * characters (BackSpace is "\b", Return "\n"), so a plain emptiness
     * check mistakes them for text. */
    const std::string text = fcitx::Key::keySymToUTF8(event.key().sym());
    const bool printable = !text.empty() &&
                           static_cast<unsigned char>(text[0]) >= 0x20 &&
                           static_cast<unsigned char>(text[0]) != 0x7f;
    if (!hasComposition && !printable)
        return;
    if (hasComposition && !isPrintableControl(sym) &&
        !(sym >= FcitxKey_1 && sym <= FcitxKey_9) && !printable)
        return;

    bool handled = false;
    if (pinyinKey(ic, *state, sym, stateMask, text, &handled) && handled)
        event.filterAndAccept();
}

void SixuanEngine::selectCandidate(fcitx::InputContext *ic, uint64_t session,
                                   uint64_t revision, int index) {
    if (!session)
        return;
    GVariant *reply =
        call("PinyinSelect", g_variant_new("(tti)", session, revision, index),
             kStateSignature);
    if (!reply)
        return;
    auto *state = stateFor(ic);
    state->session = session;
    applySnapshot(ic, *state, reply, false);
    g_variant_unref(reply);
}

void SixuanCandidateWord::select(fcitx::InputContext *inputContext) const {
    engine_->selectCandidate(inputContext, session_, revision_, index_);
}

std::string SixuanEngine::subMode(const fcitx::InputMethodEntry & /*entry*/,
                                  fcitx::InputContext &ic) {
    auto iter = states_.find(&ic);
    if (iter != states_.end() && iter->second->english)
        return "英";
    return "中";
}

std::string SixuanEngine::subModeLabelImpl(
    const fcitx::InputMethodEntry & /*entry*/, fcitx::InputContext &ic) {
    auto iter = states_.find(&ic);
    if (iter != states_.end() && iter->second->english)
        return "英";
    return "中";
}

void SixuanEngine::handleFrontendCall(GDBusConnection *, const gchar *,
                                      const gchar *, const gchar *,
                                      const gchar *method, GVariant *params,
                                      GDBusMethodInvocation *invocation,
                                      gpointer userData) {
    auto *engine = static_cast<SixuanEngine *>(userData);
    if (!strcmp(method, "RefreshCandidates")) {
        guint64 session = 0, revision = 0;
        gboolean handled = FALSE, english = FALSE;
        const char *preedit = nullptr, *raw = nullptr, *commit = nullptr;
        gint32 caret = 0, cursor = 0;
        GVariant *array = nullptr;
        g_variant_get(params, "(ttbbsii@a(ss)ss)", &session, &revision,
                      &handled, &english, &preedit, &caret, &cursor, &array,
                      &raw, &commit);
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ss)"));
        for (gsize i = 0; i < g_variant_n_children(array); i++) {
            GVariant *item = g_variant_get_child_value(array, i);
            const char *text = nullptr, *source = nullptr;
            g_variant_get(item, "(&s&s)", &text, &source);
            g_variant_builder_add(&builder, "(ss)", text ? text : "",
                                  source ? source : "");
            g_variant_unref(item);
        }
        g_variant_unref(array);
        bool matched = false;
        for (auto &pair : engine->states_) {
            if (pair.second->session != session)
                continue;
            auto *ic = pair.first;
            if (ic->hasFocus() && !sensitive(ic)) {
                GVariant *stateReply = g_variant_new(
                    kStateSignature, session, revision, handled, english,
                    preedit ? preedit : "", caret, cursor,
                    /* Bare "a(ss)" consumes an open builder and ends it. */
                    &builder, raw ? raw : "",
                    commit ? commit : "");
                engine->applySnapshot(ic, *pair.second, stateReply, false);
                g_variant_unref(g_variant_ref_sink(stateReply));
                matched = true;
            }
            break;
        }
        if (!matched)
            g_variant_builder_clear(&builder);
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    g_dbus_method_invocation_return_dbus_error(
        invocation, "org.freedesktop.DBus.Error.UnknownMethod",
        "Unknown Sixuan frontend method");
}

class SixuanEngineFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new SixuanEngine(manager->instance());
    }
};

FCITX_ADDON_FACTORY_V2(sixuan, SixuanEngineFactory);
