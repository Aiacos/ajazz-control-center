// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tauri v2 compatibility shim for hosting the OpenDeck Svelte SPA inside
// QtWebEngine (the `webui` UI mode). It bridges the frontend's
// `@tauri-apps/api` calls — which resolve to `window.__TAURI_INTERNALS__` and
// `window.__TAURI_EVENT_PLUGIN_INTERNALS__` — onto a QWebChannel object named
// "opendeck" (our C++ OpenDeckBridge). See docs/opendeck-ui/01-contract.md.
//
// Contract (FIXED, matches OpenDeckBridge):
//   slot   invoke(requestId, command, argsJson)
//   signal invokeResponse(requestId, resultJson, errorJson)
//   signal event(name, payloadJson)
//
// Loaded as the FIRST <head> script (classic, non-module) so the globals exist
// before the SPA's deferred module scripts run. invoke() calls made before the
// channel finishes connecting are queued and flushed on connect.
(function () {
	"use strict";

	var DEBUG = /[?&]oddebug\b/.test(location.search);
	function log() { if (DEBUG) console.log.apply(console, ["[od-shim]"].concat([].slice.call(arguments))); }

	var bridge = null;          // the QWebChannel "opendeck" object once connected
	var ready = false;
	var seq = 0;
	var pending = {};           // requestId -> {resolve, reject}
	var preQueue = [];          // invoke() calls made before `ready`
	var listeners = {};         // eventName -> { id: handlerFn }
	var listenSeq = 0;

	function doInvoke(requestId, command, argsJson) {
		bridge.invoke(requestId, command, argsJson);
	}

	function invoke(command, args) {
		return new Promise(function (resolve, reject) {
			var id = "r" + (++seq);
			pending[id] = { resolve: resolve, reject: reject };
			var argsJson;
			try { argsJson = JSON.stringify(args || {}); } catch (e) { argsJson = "{}"; }
			log("invoke", command, args || {});
			if (ready) doInvoke(id, command, argsJson);
			else preQueue.push([id, command, argsJson]);
		});
	}

	// ---- event plugin (Tauri's listen/unlisten/emit go through invoke) ------
	function handleEventCommand(command, args) {
		// The @tauri-apps/api event plugin issues these as invoke() commands.
		if (command === "plugin:event|listen") {
			var ev = args && args.event;
			var id = ++listenSeq;
			(listeners[ev] = listeners[ev] || {})[id] = (args && args.handler) || null;
			log("listen", ev, "->", id);
			return Promise.resolve(id);
		}
		if (command === "plugin:event|unlisten") {
			var e2 = args && args.event, i2 = args && args.eventId;
			if (listeners[e2]) delete listeners[e2][i2];
			return Promise.resolve(null);
		}
		if (command === "plugin:event|emit" || command === "plugin:event|emit_to") {
			return Promise.resolve(null);
		}
		// Tauri window plugin: OpenDeck resizes the OS window to fit the device
		// geometry (set_size/set_min_size). Inside our embedded webview the
		// WebEngineView fills our Qt window, so these are local no-ops. Resolve
		// (don't reject) so the SPA's `getCurrentWindow().setSize(...)` etc. don't
		// throw. A few getters return a sane default.
		if (command.indexOf("plugin:window|") === 0) {
			if (command === "plugin:window|scale_factor") return Promise.resolve(1);
			if (command === "plugin:window|theme") return Promise.resolve("dark");
			if (command === "plugin:window|is_maximized"
				|| command === "plugin:window|is_minimized"
				|| command === "plugin:window|is_fullscreen") return Promise.resolve(false);
			log("window no-op", command);
			return Promise.resolve(null);
		}
		return null; // not an event/window command
	}

	function dispatchEvent(name, payload) {
		var ls = listeners[name];
		if (!ls) return;
		// Tauri event object shape: { event, id, payload }
		Object.keys(ls).forEach(function (id) {
			var fn = ls[id];
			if (typeof fn === "function") {
				try { fn({ event: name, id: Number(id), payload: payload }); }
				catch (e) { console.error("[od-shim] listener error", name, e); }
			}
		});
	}

	// ---- install the Tauri v2 internals -------------------------------------
	var cbId = 0;
	var callbacks = {};
	window.__TAURI_INTERNALS__ = {
		invoke: function (command, args) {
			var ev = handleEventCommand(command, args);
			return ev !== null ? ev : invoke(command, args);
		},
		transformCallback: function (cb, once) {
			var id = ++cbId;
			callbacks[id] = function (payload) { if (once) delete callbacks[id]; return cb(payload); };
			return id;
		},
		convertFileSrc: function (p) { return p; },
		metadata: { currentWindow: { label: "main" }, currentWebview: { label: "main" } },
	};
	window.__TAURI_TO_IPC_KEY__ = "__TAURI_TO_IPC_KEY__";
	window.__TAURI_EVENT_PLUGIN_INTERNALS__ = {
		unregisterListener: function (event, id) { if (listeners[event]) delete listeners[event][id]; },
	};

	// ---- connect the QWebChannel --------------------------------------------
	function onChannelReady(channel) {
		bridge = channel.objects.opendeck;
		if (!bridge) { console.error("[od-shim] 'opendeck' object missing on the channel"); return; }
		bridge.invokeResponse.connect(function (requestId, resultJson, errorJson) {
			var p = pending[requestId];
			if (!p) return;
			delete pending[requestId];
			if (errorJson && errorJson !== "" && errorJson !== "null") {
				var err; try { err = JSON.parse(errorJson); } catch (e) { err = errorJson; }
				p.reject(err);
			} else {
				var res = null; try { res = resultJson ? JSON.parse(resultJson) : null; } catch (e) { res = null; }
				p.resolve(res);
			}
		});
		bridge.event.connect(function (name, payloadJson) {
			var payload = null; try { payload = payloadJson ? JSON.parse(payloadJson) : null; } catch (e) {}
			log("event", name, payload);
			dispatchEvent(name, payload);
		});
		ready = true;
		log("channel ready; flushing", preQueue.length, "queued invokes");
		preQueue.splice(0).forEach(function (q) { doInvoke(q[0], q[1], q[2]); });
	}

	function initChannel() {
		if (typeof QWebChannel === "undefined" || typeof qt === "undefined" || !qt.webChannelTransport) {
			console.error("[od-shim] QWebChannel/transport unavailable");
			return;
		}
		new QWebChannel(qt.webChannelTransport, onChannelReady);
	}

	// qwebchannel.js ships inside QtWebEngine at qrc:///qtwebchannel/qwebchannel.js.
	var s = document.createElement("script");
	s.src = "qrc:///qtwebchannel/qwebchannel.js";
	s.onload = initChannel;
	s.onerror = function () { console.error("[od-shim] failed to load qwebchannel.js"); };
	document.head.appendChild(s);

	log("installed (Tauri compat shim, webui mode)");
})();
