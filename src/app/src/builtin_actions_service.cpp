// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file builtin_actions_service.cpp
 * @brief Implementation of @ref ajazz::app::BuiltinActionsService.
 *
 * All JSON parsing happens in this file (app tier, QJsonDocument — COD-031: the
 * core registry receives and passes settingsJson verbatim without parsing).
 *
 * Built-in UUID set (LOCKED, from docs/protocols/streamdeck/akp_plugin_sdk.md §1):
 *   page.previous, page.next, page.goto, page.indicator, page.change
 *   profile.openchild, profile.backtoparent, profile.rotate
 *   device.brightness
 *   system.hotkey, plain.text, system.multimedia, system.volume
 *   browser
 *   multiactions, multiactions.LunBo
 *   obsstudio (gated AJAZZ_HAVE_WEBSOCKETS)
 *
 * Deferred (v1.3+ backlog, NOT wired here): vmix, youtube, network, mouse.event,
 *   device.k1proLED+-, pageindicatororgoto, pagebackorforword.
 */
#include "builtin_actions_service.hpp"

#include "ajazz/core/action_engine.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/core/profile.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#ifdef AJAZZ_HAVE_WEBSOCKETS
#include <QUrl>
#endif

namespace ajazz::app {

namespace {

/// Convert a settingsJson string_view to a QJsonObject, logging on failure.
[[nodiscard]] QJsonObject parseSettings(std::string_view settingsJson, std::string_view ctx) {
    auto const doc = QJsonDocument::fromJson(
        QByteArray{settingsJson.data(), static_cast<qsizetype>(settingsJson.size())});
    if (!doc.isObject()) {
        AJAZZ_LOG_WARN(
            "builtin", "{}: malformed settingsJson (not a JSON object): {}", ctx, settingsJson);
        return {};
    }
    return doc.object();
}

/// Clamp an int to [0..100].
[[nodiscard]] int clamp0to100(int v) noexcept {
    return std::clamp(v, 0, 100);
}

/// Map a string to a MediaKey enum variant. Returns std::nullopt on unknown.
[[nodiscard]] std::optional<core::MediaKey> parseMediaKey(QString const& key) {
    if (key == QStringLiteral("PlayPause") || key == QStringLiteral("play_pause")) {
        return core::MediaKey::PlayPause;
    }
    if (key == QStringLiteral("Stop") || key == QStringLiteral("stop")) {
        return core::MediaKey::Stop;
    }
    if (key == QStringLiteral("Next") || key == QStringLiteral("next")) {
        return core::MediaKey::Next;
    }
    if (key == QStringLiteral("Previous") || key == QStringLiteral("previous")) {
        return core::MediaKey::Previous;
    }
    if (key == QStringLiteral("VolumeUp") || key == QStringLiteral("volume_up")) {
        return core::MediaKey::VolumeUp;
    }
    if (key == QStringLiteral("VolumeDown") || key == QStringLiteral("volume_down")) {
        return core::MediaKey::VolumeDown;
    }
    if (key == QStringLiteral("Mute") || key == QStringLiteral("mute")) {
        return core::MediaKey::Mute;
    }
    return std::nullopt;
}

/// Decode a QJsonArray of modifier strings to a vector of HID Usage IDs.
/// Recognised modifiers: "ctrl"/"control", "shift", "alt"/"option", "meta"/"win"/"super".
/// HID Usage page 0x07 (keyboard/keypad):
///   0x000700E0 Left Ctrl, 0x000700E1 Left Shift, 0x000700E2 Left Alt, 0x000700E3 Left GUI.
[[nodiscard]] std::vector<std::uint32_t> parseModifiers(QJsonArray const& arr) {
    std::vector<std::uint32_t> mods;
    for (auto const& v : arr) {
        auto const s = v.toString().toLower();
        if (s == QStringLiteral("ctrl") || s == QStringLiteral("control")) {
            mods.push_back(0x000700E0u); // Left Ctrl
        } else if (s == QStringLiteral("shift")) {
            mods.push_back(0x000700E1u); // Left Shift
        } else if (s == QStringLiteral("alt") || s == QStringLiteral("option")) {
            mods.push_back(0x000700E2u); // Left Alt
        } else if (s == QStringLiteral("meta") || s == QStringLiteral("win") ||
                   s == QStringLiteral("super")) {
            mods.push_back(0x000700E3u); // Left GUI/Win/Super
        }
    }
    return mods;
}

/// Map a single-character or named key string to a HID Usage ID.
/// Single printable ASCII characters: Usage = 0x00070004 + (c - 'a') for letters;
/// for digits 1-9: 0x0007001E + (d - 1); for '0': 0x00070027.
/// Named keys: "Return"=0x00070028, "Escape"=0x00070029, "Tab"=0x0007002B,
///             "Space"=0x0007002C, "F1"..F12, etc.
[[nodiscard]] std::uint32_t parseKeyToHid(QString const& key) {
    if (key.isEmpty()) {
        return 0;
    }
    auto const lower = key.toLower();
    // Named keys
    if (lower == QStringLiteral("return") || lower == QStringLiteral("enter")) {
        return 0x00070028u;
    }
    if (lower == QStringLiteral("escape") || lower == QStringLiteral("esc")) {
        return 0x00070029u;
    }
    if (lower == QStringLiteral("backspace")) {
        return 0x0007002Au;
    }
    if (lower == QStringLiteral("tab")) {
        return 0x0007002Bu;
    }
    if (lower == QStringLiteral("space")) {
        return 0x0007002Cu;
    }
    if (lower == QStringLiteral("delete") || lower == QStringLiteral("del")) {
        return 0x0007004Cu;
    }
    // F-keys F1..F12 -> 0x0007003A..0x00070045
    if (lower.startsWith(QStringLiteral("f")) && lower.size() <= 3) {
        bool ok = false;
        int const n = lower.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 12) {
            return 0x00070039u + static_cast<std::uint32_t>(n); // F1=0x3A, F12=0x45
        }
    }
    // Single letter a-z -> Usage 0x00070004..0x0007001D
    if (lower.size() == 1) {
        QChar const c = lower[0];
        if (c >= u'a' && c <= u'z') {
            return 0x00070004u + static_cast<std::uint32_t>(c.unicode() - u'a');
        }
        // Digits 1-9 -> 0x0007001E..0x00070026; '0' -> 0x00070027
        if (c >= u'1' && c <= u'9') {
            return 0x0007001Du + static_cast<std::uint32_t>(c.unicode() - u'0');
        }
        if (c == u'0') {
            return 0x00070027u;
        }
    }
    AJAZZ_LOG_WARN("builtin", "parseKeyToHid: unrecognised key string '{}'", key.toStdString());
    return 0;
}

/// Build a stable binding id string for LunBo cursor keying.
/// Uses "bindingId" from settings if present; otherwise falls back to "page:key".
[[nodiscard]] std::string makeLunBoBindingId(QJsonObject const& obj) {
    auto const bindingId = obj.value(QStringLiteral("bindingId")).toString();
    if (!bindingId.isEmpty()) {
        return bindingId.toStdString();
    }
    // Fallback: encode page + key index as a stable composite id.
    auto const page = obj.value(QStringLiteral("page")).toString(QStringLiteral("root"));
    auto const key = obj.value(QStringLiteral("key")).toInt(-1);
    return page.toStdString() + ":" + std::to_string(key);
}

/// Decode a JSON array of Action descriptors into an ActionChain.
/// Each element is expected to carry: "kind" (int), "id" (string),
/// "settingsJson" (string), "label" (string), "delayMs" (int).
[[nodiscard]] core::ActionChain decodeActionChain(QJsonArray const& arr) {
    core::ActionChain chain;
    chain.reserve(static_cast<std::size_t>(arr.size()));
    for (auto const& v : arr) {
        auto const obj = v.toObject();
        core::Action action;
        action.kind = static_cast<core::ActionKind>(obj.value(QStringLiteral("kind")).toInt(0));
        action.id = obj.value(QStringLiteral("id")).toString().toStdString();
        action.settingsJson = obj.value(QStringLiteral("settingsJson")).toString().toStdString();
        action.label = obj.value(QStringLiteral("label")).toString().toStdString();
        action.delayMs = static_cast<std::uint32_t>(obj.value(QStringLiteral("delayMs")).toInt(0));
        chain.push_back(std::move(action));
    }
    return chain;
}

} // namespace

BuiltinActionsService::BuiltinActionsService(BrightnessSink brightness,
                                             NavigateSink navigate,
                                             OpenUrlFn openUrl,
                                             core::ActionEngine* engine,
                                             PluginFallback fallback,
                                             QObject* parent)
    : QObject(parent), m_brightness(std::move(brightness)), m_navigate(std::move(navigate)),
      m_openUrl(std::move(openUrl)), m_engine(engine), m_fallback(std::move(fallback)),
      m_synth(core::makeDefaultInputSynthesizer()) {
    populate();
}

BuiltinActionsService::~BuiltinActionsService() = default;

void BuiltinActionsService::setSynthesizer(std::unique_ptr<core::IInputSynthesizer> synth) {
    m_synth = std::move(synth);
}

void BuiltinActionsService::onPluginAction(std::string_view id, std::string_view settingsJson) {
    if (m_registry.handles(id)) {
        m_registry.dispatch(id, settingsJson);
        return;
    }
    if (m_fallback) {
        m_fallback(id, settingsJson);
    }
}

void BuiltinActionsService::resetLunBoCursors() {
    m_lunboCursors.clear();
}

void BuiltinActionsService::setCaptureHotkeys(bool enable) {
    m_captureEnabled = enable;
    if (m_synth) {
        if (enable) {
            // Opt-in capture: activate the global hook in the synthesizer.
            m_synth->captureHotkeys(true, [](core::KeyChord const& /*chord*/) {
                // Future: route captured chord to bound action.
                AJAZZ_LOG_INFO("builtin", "captureHotkeys: chord captured (not yet wired)");
            });
        } else {
            // Disable: pass an empty callback to deactivate.
            m_synth->captureHotkeys(false, {});
        }
    }
}

void BuiltinActionsService::populate() {
    using namespace std::string_view_literals;

    // ---- Page navigation ----

    // page.previous: navigate(-1) = previous page in the flat carousel.
    m_registry.registerAction("com.hotspot.streamdock.page.previous",
                              [this](std::string_view /*settings*/) {
                                  if (m_navigate) {
                                      m_navigate(-1);
                                  }
                              });

    // page.next: navigate(+1) = next page in the flat carousel.
    m_registry.registerAction("com.hotspot.streamdock.page.next",
                              [this](std::string_view /*settings*/) {
                                  if (m_navigate) {
                                      m_navigate(+1);
                                  }
                              });

    // page.goto: navigate to an absolute page index from settingsJson {"target": N}.
    // Decision (Open Question 2 / A3): map "target" (0-based page index) to navigate(target).
    // The NavigateSink receives the target index as-is; the control service interprets
    // values > 1 as an absolute position when they fall outside {-1, +1}.
    m_registry.registerAction("com.hotspot.streamdock.page.goto",
                              [this](std::string_view settingsJson) {
                                  auto const obj = parseSettings(settingsJson, "page.goto"sv);
                                  int const target = obj.value(QStringLiteral("target")).toInt(0);
                                  if (m_navigate) {
                                      m_navigate(target);
                                  }
                              });

    // page.indicator: display-only hint; no dispatch side effect.
    // Logged as a no-op (the device renders the indicator natively from profile state).
    m_registry.registerAction(
        "com.hotspot.streamdock.page.indicator", [](std::string_view /*settings*/) {
            AJAZZ_LOG_INFO("builtin", "page.indicator: display-only hint (no-op)");
        });

    // page.change (Knob): the knob rotates through carousel pages; direction from
    // {"direction":"CW"|"CCW"} -> navigate(+1/-1).
    m_registry.registerAction(
        "com.hotspot.streamdock.page.change", [this](std::string_view settingsJson) {
            auto const obj = parseSettings(settingsJson, "page.change"sv);
            auto const dir = obj.value(QStringLiteral("direction")).toString();
            int const d = (dir == QStringLiteral("CCW")) ? -1 : +1;
            if (m_navigate) {
                m_navigate(d);
            }
        });

    // ---- Profile navigation ----

    // profile.openchild: push a child page (folder) onto the ActionEngine nav stack.
    // settingsJson: {"childId": "<page-id>"}
    m_registry.registerAction(
        "com.hotspot.streamdock.profile.openchild", [this](std::string_view settingsJson) {
            auto const obj = parseSettings(settingsJson, "profile.openchild"sv);
            auto const childId = obj.value(QStringLiteral("childId")).toString();
            if (m_engine && !childId.isEmpty()) {
                m_engine->pushPage(childId.toStdString());
            }
        });

    // profile.backtoparent: pop one page from the ActionEngine nav stack.
    m_registry.registerAction("com.hotspot.streamdock.profile.backtoparent",
                              [this](std::string_view /*settings*/) {
                                  if (m_engine) {
                                      m_engine->popPage();
                                  }
                              });

    // profile.rotate: cycle the active profile.
    // Decision (Open Question 2 / A3): no ProfileController accessor is available in the
    // current injection seam set. Log a documented deferral rather than inventing a new seam.
    // A future phase that exposes a ProfileRotateFn injection seam will implement this fully.
    m_registry.registerAction("com.hotspot.streamdock.profile.rotate",
                              [](std::string_view /*settings*/) {
                                  AJAZZ_LOG_WARN("builtin",
                                                 "profile.rotate: deferred (no ProfileController "
                                                 "rotate seam in 21-03; future phase)");
                              });

    // ---- Device controls ----

    // device.brightness: parse {"level": N} -> clamp(0..100) -> BrightnessSink.
    m_registry.registerAction(
        "com.hotspot.streamdock.device.brightness", [this](std::string_view settingsJson) {
            auto const obj = parseSettings(settingsJson, "device.brightness"sv);
            int const raw = obj.value(QStringLiteral("level")).toInt(80);
            int const level = clamp0to100(raw);
            if (m_brightness) {
                m_brightness(level);
            }
        });

    // ---- OS input synthesis ----

    // system.hotkey: parse {"modifiers":["ctrl",...], "key":"P"} -> sendChord (OUTPUT).
    // CAPTURE is opt-in (T-21-hook): captureHotkeys is NOT activated here by default.
    m_registry.registerAction(
        "com.hotspot.streamdock.system.hotkey", [this](std::string_view settingsJson) {
            auto const obj = parseSettings(settingsJson, "system.hotkey"sv);
            core::KeyChord chord;
            chord.modifiers = parseModifiers(obj.value(QStringLiteral("modifiers")).toArray());
            chord.key = parseKeyToHid(obj.value(QStringLiteral("key")).toString());
            if (m_synth && chord.key != 0) {
                m_synth->sendChord(chord);
            }
        });

    // plain.text: parse {"text":"..."} -> typeText (literal-string synthesis, no shell).
    m_registry.registerAction(
        "com.hotspot.streamdock.plain.text", [this](std::string_view settingsJson) {
            auto const obj = parseSettings(settingsJson, "plain.text"sv);
            auto const text = obj.value(QStringLiteral("text")).toString().toStdString();
            if (m_synth && !text.empty()) {
                m_synth->typeText(text);
            }
        });

    // system.multimedia: parse {"key":"PlayPause"|"Stop"|"Next"|"Previous"} -> sendMediaKey.
    m_registry.registerAction(
        "com.hotspot.streamdock.system.multimedia", [this](std::string_view settingsJson) {
            auto const obj = parseSettings(settingsJson, "system.multimedia"sv);
            auto const keyStr = obj.value(QStringLiteral("key")).toString();
            auto const mk = parseMediaKey(keyStr);
            if (m_synth && mk.has_value()) {
                m_synth->sendMediaKey(*mk);
            } else if (!mk.has_value()) {
                AJAZZ_LOG_WARN(
                    "builtin", "system.multimedia: unknown key '{}'", keyStr.toStdString());
            }
        });

    // system.volume: parse {"key":"VolumeUp"|"VolumeDown"|"Mute"} -> sendMediaKey.
    // Also accepts {"direction":"up"|"down"} as an alias.
    m_registry.registerAction(
        "com.hotspot.streamdock.system.volume", [this](std::string_view settingsJson) {
            auto const obj = parseSettings(settingsJson, "system.volume"sv);
            // Try "key" first; fall back to "direction" alias.
            auto keyStr = obj.value(QStringLiteral("key")).toString();
            if (keyStr.isEmpty()) {
                auto const dir = obj.value(QStringLiteral("direction")).toString();
                if (dir == QStringLiteral("up")) {
                    keyStr = QStringLiteral("VolumeUp");
                } else if (dir == QStringLiteral("down")) {
                    keyStr = QStringLiteral("VolumeDown");
                } else {
                    keyStr = dir; // let parseMediaKey reject it
                }
            }
            auto const mk = parseMediaKey(keyStr);
            if (m_synth && mk.has_value()) {
                m_synth->sendMediaKey(*mk);
            } else if (!mk.has_value()) {
                AJAZZ_LOG_WARN("builtin", "system.volume: unknown key '{}'", keyStr.toStdString());
            }
        });

    // ---- Browser ----

    // browser: parse {"url":"https://..."} -> openUrl (http/https only; WR-01).
    // The openUrl executor in Application already validates the scheme; we validate
    // here too so tests can assert the rejection without the full app wiring.
    m_registry.registerAction(
        "com.hotspot.streamdock.browser", [this](std::string_view settingsJson) {
            auto const obj = parseSettings(settingsJson, "browser"sv);
            auto const urlStr = obj.value(QStringLiteral("url")).toString();
            // Scheme validation (WR-01 / Phase-20 lesson): only http/https.
            if (!urlStr.startsWith(QStringLiteral("http://")) &&
                !urlStr.startsWith(QStringLiteral("https://"))) {
                AJAZZ_LOG_WARN(
                    "builtin", "browser: rejected non-http(s) URL '{}'", urlStr.toStdString());
                return;
            }
            if (m_openUrl) {
                m_openUrl(urlStr.toStdString());
            }
        });

    // ---- Multi-actions ----

    // multiactions: decode an ordered ActionChain from settingsJson {"actions":[...]}
    // and call ActionEngine::run (reuse the existing sequencer — no re-implementation).
    m_registry.registerAction("com.hotspot.streamdock.multiactions",
                              [this](std::string_view settingsJson) {
                                  auto const obj = parseSettings(settingsJson, "multiactions"sv);
                                  auto const arr = obj.value(QStringLiteral("actions")).toArray();
                                  if (m_engine) {
                                      m_engine->run(decodeActionChain(arr));
                                  }
                              });

    // multiactions.LunBo: per-key carousel. Each press advances the key's cursor by one,
    // cycling through the "actions" array. The cursor is keyed by a stable binding id
    // (T-21-lunbo; Pitfall 6 — per-key, not global). Reset on profile change.
    m_registry.registerAction(
        "com.hotspot.streamdock.multiactions.LunBo", [this](std::string_view settingsJson) {
            auto const obj = parseSettings(settingsJson, "multiactions.LunBo"sv);
            auto const arr = obj.value(QStringLiteral("actions")).toArray();
            if (arr.isEmpty()) {
                return;
            }
            // Stable per-key cursor key.
            auto const bindingId = makeLunBoBindingId(obj);
            // Advance the cursor for this key (wrap around).
            auto& cursor = m_lunboCursors[bindingId];
            auto const chainSize = static_cast<std::size_t>(arr.size());
            cursor = cursor % chainSize;
            // Run only the action at the current cursor position.
            core::ActionChain singleStep;
            singleStep.push_back(decodeActionChain(arr)[cursor]);
            if (m_engine) {
                m_engine->run(singleStep);
            }
            // Advance cursor for next press.
            cursor = (cursor + 1) % chainSize;
        });

#ifdef AJAZZ_HAVE_WEBSOCKETS
    // obsstudio: parse {"action":"setScene"|"setPreviewScene"|"toggleRecord"|"toggleStream",
    //                   "sceneName":"..."} -> ObsClient.
    // Auth default-on (T-21-obsauth): inherited from ObsClient — it refuses Identify
    // when OBS demands auth but no password is configured.
    // host/port/password are read from QSettings on first invocation.
    m_obs = std::make_unique<ObsClient>(this);
    QObject::connect(m_obs.get(), &ObsClient::connected, this, [this]() {
        m_obsConnected = true;
        AJAZZ_LOG_INFO("builtin", "obsstudio: connected to OBS");
    });
    QObject::connect(m_obs.get(), &ObsClient::authFailed, this, [](QString const& reason) {
        AJAZZ_LOG_WARN("builtin", "obsstudio: auth failed -- {}", reason.toStdString());
    });
    QObject::connect(m_obs.get(), &ObsClient::errorOccurred, this, [this](QString const& reason) {
        m_obsConnected = false;
        AJAZZ_LOG_WARN("builtin", "obsstudio: error -- {}", reason.toStdString());
    });

    m_registry.registerAction(
        "com.hotspot.streamdock.obsstudio", [this](std::string_view settingsJson) {
            auto const obj = parseSettings(settingsJson, "obsstudio"sv);
            auto const action = obj.value(QStringLiteral("action")).toString();
            auto const sceneName = obj.value(QStringLiteral("sceneName")).toString();

            // Ensure connected (lazy connect on first action).
            if (!m_obsConnected) {
                QSettings qs;
                auto const host =
                    qs.value(QStringLiteral("obs/host"), QStringLiteral("127.0.0.1")).toString();
                auto const port =
                    static_cast<quint16>(qs.value(QStringLiteral("obs/port"), 4455).toInt());
                auto const password =
                    qs.value(QStringLiteral("obs/password"), QString{}).toString();
                // Auth default-on (T-21-obsauth): if password is empty
                // and OBS demands auth, ObsClient will emit authFailed
                // and refuse to send Identify.
                m_obs->connectToObs(host, port, password);
            }

            // Route to the appropriate ObsClient method.
            if (action == QStringLiteral("setScene")) {
                m_obs->setScene(sceneName);
            } else if (action == QStringLiteral("setPreviewScene")) {
                m_obs->setPreviewScene(sceneName);
            } else if (action == QStringLiteral("toggleRecord")) {
                m_obs->toggleRecord();
            } else if (action == QStringLiteral("toggleStream")) {
                m_obs->toggleStream();
            } else {
                AJAZZ_LOG_WARN("builtin", "obsstudio: unknown action '{}'", action.toStdString());
            }
        });
#endif // AJAZZ_HAVE_WEBSOCKETS

    // ---- Deferred / not in scope ----
    // vmix, youtube, network, mouse.event, device.k1proLED+-, pageindicatororgoto,
    // pagebackorforword: v1.3+ backlog. Not registered; handles() returns false for them;
    // the plugin executor forwards them to the Phase-19 path (logged no-op or bridge).
}

} // namespace ajazz::app
