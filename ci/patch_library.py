"""
PlatformIO post-install script to patch ESPAsyncWebServer library.

Fixes a heap corruption bug in AsyncWebSocketClient::_handleDataEvent():
the non-endOfPaquet code path writes data[len] = 0, which overwrites
one byte past the end of the lwIP pbuf receive buffer.  This corrupts
adjacent heap allocations and manifests as spurious "Invalid UTF-8 in
text frame" / "Invalid frame header" errors in the browser.
"""
import os

Import("env")

def patch_library():
    env_name = env["PIOENV"]
    project_dir = os.path.dirname(env["PROJECT_SRC_DIR"])
    libdeps_dir = os.path.join(project_dir, ".pio", "libdeps", env_name)
    ws_file = os.path.join(libdeps_dir, "ESPAsyncWebServer", "src", "AsyncWebSocket.cpp")

    if not os.path.isfile(ws_file):
        print("patch_library: ESPAsyncWebServer/AsyncWebSocket.cpp not found, skipping")
        return

    with open(ws_file, "r") as f:
        source = f.read()

    patch_marker = "Always copy to a safe buffer"
    if patch_marker in source:
        print("patch_library: already patched")
        return

    old_block = """  if (_pinfo.message_opcode == WS_TEXT) {
    if (endOfPaquet) {
      std::unique_ptr<uint8_t[]> copy(new (std::nothrow) uint8_t[len + 1]());
      if (copy) {
        memcpy(copy.get(), data, len);
        copy[len] = 0;
        _server->_handleEvent(this, WS_EVT_DATA, (void *)&_pinfo, copy.get(), len);
      } else {
        async_ws_log_e("Failed to allocate");
        if (_client) {
          _client->abort();
        }
      }
    } else {
      uint8_t backup = data[len];
      data[len] = 0;
      _server->_handleEvent(this, WS_EVT_DATA, (void *)&_pinfo, data, len);
      data[len] = backup;
    }
  } else {"""

    new_block = """  if (_pinfo.message_opcode == WS_TEXT) {
    // Always copy to a safe buffer — never write data[len] = 0 into the TCP
    // receive pbuf.  The backup/restore path below writes one byte past the
    // end of the data buffer, which can corrupt adjacent pbuf allocations and
    // cause spurious "Invalid UTF-8" / "Invalid frame header" errors in the
    // browser when the corrupted memory affects outgoing frame data.
    std::unique_ptr<uint8_t[]> copy(new (std::nothrow) uint8_t[len + 1]());
    if (copy) {
      memcpy(copy.get(), data, len);
      copy[len] = 0;
      _server->_handleEvent(this, WS_EVT_DATA, (void *)&_pinfo, copy.get(), len);
    } else {
      async_ws_log_e("Failed to allocate");
      if (_client) {
        _client->abort();
      }
    }
  } else {"""

    if old_block in source:
        source = source.replace(old_block, new_block)
        with open(ws_file, "w") as f:
            f.write(source)
        print("patch_library: patched ESPAsyncWebServer _handleDataEvent")
    else:
        print("patch_library: old block not found — library version may have changed")

def patch_websocket_send_frame():
    env_name = env["PIOENV"]
    project_dir = os.path.dirname(env["PROJECT_SRC_DIR"])
    libdeps_dir = os.path.join(project_dir, ".pio", "libdeps", env_name)
    ws_file = os.path.join(libdeps_dir, "ESPAsyncWebServer", "src", "AsyncWebSocket.cpp")

    if not os.path.isfile(ws_file):
        return

    with open(ws_file, "r") as f:
        source = f.read()

    patch_marker = "Flush the orphaned header"
    if patch_marker in source:
        print("patch_library: send-frame patch already applied")
        return

    old_block = """    if (len && mask) {
      size_t i;
      for (i = 0; i < len; i++) {
        data[i] = data[i] ^ mbuf[i % 4];
      }
    }
    if (client->add((const char *)data, len) != len) {
      // os_printf("error adding %lu data bytes\\n", len);
      //  Serial.println("SF 5");
      return 0;
    }
  }
  if (!client->send()) {"""

    new_block = """    if (len && mask) {
      size_t i;
      for (i = 0; i < len; i++) {
        data[i] = data[i] ^ mbuf[i % 4];
      }
    }
    size_t added = client->add((const char *)data, len);
    if (added != len) {
      // Partial or failed data add after header was queued.
      // Flush the orphaned header to avoid corrupting the next frame.
      client->send();
      return added;
    }
  }
  if (!client->send()) {"""

    if old_block in source:
        source = source.replace(old_block, new_block)
        with open(ws_file, "w") as f:
            f.write(source)
        print("patch_library: patched webSocketSendFrame orphaned header fix")
    else:
        print("patch_library: send-frame old block not found (may already be fixed or version changed)")

patch_library()

patch_websocket_send_frame()
