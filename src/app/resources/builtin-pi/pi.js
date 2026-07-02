// Shared Property Inspector bootstrap for the in-tree OpenDeck builtin
// actions. The PluginAssetServer's injected shim receives the SPA's postMessage
// "connect" and calls the standard Elgato entry point below with
// [port, uuid(context), registerEvent, info, actionInfo].
//
// Contract with the C++ side (plugin_device_bridge::handleSettingsAction):
//   - register as a Property Inspector with uuid = the instance context;
//   - getSettings  -> didReceiveSettings {payload:{settings}} reply;
//   - setSettings  -> persisted per-context AND mirrored into the profile
//     binding, which is what the builtin reads at press time.
/* exported connectElgatoStreamDeckSocket, save */
"use strict";

let odWs = null;
let odCtx = null;

// Each page defines applySettings(settings) to fill its form.
function connectElgatoStreamDeckSocket(inPort, inUUID, inRegisterEvent, inInfo, inActionInfo) {
    odCtx = inUUID;
    // Seed the form from actionInfo immediately (the SPA passes the bound
    // instance's settings), then confirm via getSettings once registered.
    try {
        const ai = JSON.parse(inActionInfo);
        if (ai && ai.payload && ai.payload.settings) {
            applySettings(ai.payload.settings);
        }
    } catch (e) { /* actionInfo is best-effort */ }

    odWs = new WebSocket("ws://127.0.0.1:" + inPort);
    odWs.onopen = () => {
        odWs.send(JSON.stringify({ event: inRegisterEvent, uuid: inUUID }));
        odWs.send(JSON.stringify({ event: "getSettings", context: inUUID }));
    };
    odWs.onmessage = (msg) => {
        let ev;
        try {
            ev = JSON.parse(msg.data);
        } catch (e) {
            return;
        }
        if (ev.event === "didReceiveSettings" && ev.payload && ev.payload.settings) {
            applySettings(ev.payload.settings);
        }
    };
}

function save(settings) {
    if (odWs && odWs.readyState === WebSocket.OPEN && odCtx) {
        odWs.send(JSON.stringify({ event: "setSettings", context: odCtx, payload: settings }));
    }
}
