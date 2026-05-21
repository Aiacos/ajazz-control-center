#!/usr/bin/env python3
"""Frida capture of iot_driver's HID feature writes — crack the dongle-mediated
mouse OLED-clock (opcode 0x28) wire format that the vendor driver sends.

Hooks HidD_SetFeature / HidD_GetFeature in the running iot_driver process and
dumps every feature-report buffer. Run while triggering a clock set in the
vendor app. Read-only observation of the user's own driver.
"""
from __future__ import annotations

import sys
import time

import frida

TARGET = "iot_driver_v193.exe"
DURATION = 60

JS = r"""
function hex(ptr, len) {
  var n = Math.min(len, 67);
  var b = new Uint8Array(ptr.readByteArray(n));
  return Array.prototype.map.call(b, function (x) {
    return ('0' + x.toString(16)).slice(-2);
  }).join(' ');
}
function resolve(mod, name) {
  try { return Process.getModuleByName(mod).getExportByName(name); } catch (e) {}
  try { return Module.getGlobalExportByName(name); } catch (e) {}
  return null;
}
['HidD_SetFeature', 'HidD_GetFeature'].forEach(function (name) {
  var addr = resolve('hid.dll', name);
  if (!addr) { send({warn: name + ' not found'}); return; }
  Interceptor.attach(addr, {
    onEnter: function (args) {
      this.buf = args[1];
      this.len = args[2].toInt32();
      this.name = name;
      if (name === 'HidD_SetFeature') {
        send({api: name, len: this.len, hex: hex(this.buf, this.len)});
      }
    },
    onLeave: function (retval) {
      if (this.name === 'HidD_GetFeature') {
        send({api: this.name, len: this.len, hex: hex(this.buf, this.len), ret: retval.toInt32()});
      }
    }
  });
});
send({info: 'hooks installed: SetFeature/GetFeature (full 67 bytes)'});
"""


def main() -> None:
    try:
        session = frida.attach(TARGET)
    except frida.ProcessNotFoundError:
        sys.exit(f"{TARGET} not running")
    script = session.create_script(JS)

    def on_message(message, data):
        if message["type"] == "send":
            p = message["payload"]
            if "hex" in p:
                # Highlight the clock opcode 0x28 (byte index 1 after report id).
                mark = "  <<< 0x28 CLOCK" if " 28 " in (" " + p["hex"] + " ") else ""
                print(f"{p['api']} len={p['len']} {p['hex']}{mark}")
            else:
                print("[frida]", p)
        else:
            print("[error]", message)

    script.on("message", on_message)
    script.load()
    print(f"capturing {TARGET} for {DURATION}s — set the OLED clock in the vendor app now…")
    time.sleep(DURATION)
    session.detach()
    print("capture done.")


if __name__ == "__main__":
    main()
