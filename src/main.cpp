/**
 * MemouRF32 - TTGO LoRa32: RF clone & replay, web UI, HomeKit.
 * Connect to WiFi, open http://<ip> for clone/saved buttons; use Home app to add accessory.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "config.h"
#include "storage.h"
#include "rf_handler.h"
#include <HomeSpan.h>
#include <ArduinoJson.h>

#include "device_config.h"
#include "provisioning.h"
#include "gateway.h"
#include "gateway_homekit.h"
#include "gateway_web.h"
#include "remote.h"

static WebServer server(WEB_SERVER_PORT);
static DNSServer dnsServer;
static bool s_isAPMode = false;   // true = we are in hotspot, show WiFi config
static unsigned long s_cloneStartMs = 0;
static const unsigned long CLONE_TIMEOUT_MS = (unsigned long)CLONE_CAPTURE_MS;

// ---------- Device identity (unique per ESP32, derived from MAC) ----------
static String s_deviceId;     // last 4 hex chars of MAC, e.g. "A3F1"
static String s_deviceName;   // e.g. "MemouRF32-A3F1"

static void buildDeviceId() {
  uint64_t efuse = ESP.getEfuseMac();
  uint8_t* mac = (uint8_t*)&efuse;
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  s_deviceId = String(suffix);
  s_deviceName = String("MemouRF32-") + s_deviceId;
}

// ---------- Active device role (resolved at boot) ----------
static DeviceRole s_activeRole = ROLE_STANDALONE;

// ---------- WiFi reconnect state ----------
static bool s_homeSpanReady = false;
static bool s_hasSavedCreds = false;
static String s_savedSsid, s_savedPass;
static unsigned long s_lastReconnectAttempt = 0;

// ---------- WiFi credentials in NVS ----------
static const char* WIFI_PREFS_NS = "wifi";
static bool wifiConfigLoad(String& ssid, String& password) {
  Preferences prefs;
  if (!prefs.begin(WIFI_PREFS_NS, true)) return false;
  ssid = prefs.getString("ssid", "");
  password = prefs.getString("pass", "");
  prefs.end();
  return ssid.length() > 0;
}
static bool wifiConfigSave(const String& ssid, const String& password) {
  Preferences prefs;
  if (!prefs.begin(WIFI_PREFS_NS, false)) return false;
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.end();
  return true;
}
static void wifiConfigClear() {
  Preferences prefs;
  prefs.begin(WIFI_PREFS_NS, false);
  prefs.clear();
  prefs.end();
}

// ---------- Auth (uses WebServer built-in basic auth) ----------
static bool requireAuth() {
  if (!server.authenticate(WEB_USER, WEB_PASS)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// ---------- API: clone ----------
void handleCloneStart() {
  if (!requireAuth()) return;
  rfCaptureStart();
  s_cloneStartMs = millis();
  server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Capture started\"}");
}

void handleCloneStop() {
  if (!requireAuth()) return;
  rfCaptureStop();
  server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Capture stopped\"}");
}

void handleCloneStatus() {
  if (!requireAuth()) return;
  bool running = rfCaptureRunning();
  unsigned int count = rfCaptureCount();
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"running\":%s,\"count\":%u}", running ? "true" : "false", count);
  server.send(200, "application/json", buf);
}

void handleCloneResult() {
  if (!requireAuth()) return;
  std::vector<int32_t> pulses;
  rfCaptureGetPulses(pulses);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int32_t p : pulses) arr.add(p);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", "{\"pulses\":" + out + "}");
}

// ---------- API: buttons ----------
void handleButtonsList() {
  if (!requireAuth()) return;
  std::vector<SavedButton> btns = storageLoadButtons();
  JsonDocument doc;
  JsonArray arr = doc["buttons"].to<JsonArray>();
  for (const auto& b : btns) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = b.id;
    o["name"] = b.name;
    o["type"] = b.type;
    o["rawPulses"] = b.rawPulses;
    o["rcCode"] = b.rcCode;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleButtonSave() {
  if (!requireAuth()) return;
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
  String body = server.arg("plain");
  if (body.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"No body\"}"); return; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}"); return; }
  SavedButton b;
  b.name = doc["name"].as<String>();
  b.type = doc["type"].as<String>();
  b.rawPulses = doc["rawPulses"].as<String>();
  b.rcCode = doc["rcCode"].as<String>();
  if (doc["rcProtocol"].is<JsonObject>()) {
    JsonObject p = doc["rcProtocol"];
    b.rcProtocol.inverted = p["inverted"];
    b.rcProtocol.pulseLengthUs = p["pulseLength"] | 305;
    b.rcProtocol.zeroHigh = p["zeroHigh"] | 1;
    b.rcProtocol.zeroLow = p["zeroLow"] | 3;
    b.rcProtocol.oneHigh = p["oneHigh"] | 3;
    b.rcProtocol.oneLow = p["oneLow"] | 1;
  }
  if (b.name.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"name required\"}"); return; }
  b.id = "btn_" + String(millis());
  if (b.type != "rc") b.type = "raw";
  if (!storageAddButton(b)) { server.send(500, "application/json", "{\"ok\":false,\"error\":\"Save failed\"}"); return; }
  server.send(200, "application/json", "{\"ok\":true,\"id\":\"" + b.id + "\"}");
}

void handleButtonTrigger() {
  if (!requireAuth()) return;
  String id = server.pathArg(0);
  std::vector<SavedButton> btns = storageLoadButtons();
  for (const auto& b : btns) {
    if (b.id == id) {
      rfReplayButton(b);
      server.send(200, "application/json", "{\"ok\":true}");
      return;
    }
  }
  server.send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
}

void handleButtonDelete() {
  if (!requireAuth()) return;
  String id = server.pathArg(0);
  if (storageRemoveButton(id))
    server.send(200, "application/json", "{\"ok\":true}");
  else
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
}

// ---------- WiFi config page HTML ----------
static const char WIFI_PAGE[] PROGMEM = R"rawhtml(<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>MemouRF32 - WiFi</title><style>
body{font-family:sans-serif;max-width:420px;margin:1em auto;padding:1em;background:#fafafa;}
h1{font-size:1.3em;color:#333;} label{display:block;margin-top:10px;font-weight:bold;}
input{width:100%;padding:10px;margin:4px 0;box-sizing:border-box;border:1px solid #ccc;border-radius:4px;}
button{width:100%;padding:12px;margin-top:14px;background:#0d6efd;color:#fff;border:none;border-radius:6px;font-size:1em;cursor:pointer;}
button:active{background:#0b5ed7;}
.msg{color:#0a0;margin-top:8px;} .err{color:#c00;}
</style></head><body>
<h1>MemouRF32</h1>
<p>Configure your home WiFi. The device will reboot and connect.</p>
<form id='f'>
<label>Network name (SSID)</label>
<input type='text' id='ssid' name='ssid' placeholder='Your WiFi name' autocomplete='off' required>
<label>Password</label>
<input type='password' id='pass' name='pass' placeholder='WiFi password'>
<button type='submit'>Save and connect</button>
</form><p id='out' class='msg'></p>
<script>
document.getElementById('f').onsubmit=function(e){
  e.preventDefault();
  var o=document.getElementById('out');
  o.className='msg'; o.textContent='Saving...';
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/api/wifi/save',true);
  xhr.setRequestHeader('Content-Type','application/json');
  xhr.onload=function(){
    try{var d=JSON.parse(xhr.responseText);
      if(d.ok){o.textContent='Saved! Rebooting...';}
      else{o.className='err';o.textContent=d.error||'Failed';}
    }catch(ex){o.className='err';o.textContent='Bad response';}
  };
  xhr.onerror=function(){o.className='err';o.textContent='Request failed';};
  xhr.send(JSON.stringify({ssid:document.getElementById('ssid').value,password:document.getElementById('pass').value}));
};
</script></body></html>)rawhtml";

void handleWifiConfigPage() {
  server.send(200, "text/html", FPSTR(WIFI_PAGE));
}

void handleCaptiveRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "Redirecting to setup");
}

void handleWifiSave() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
  String body = server.arg("plain");
  if (body.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"No body\"}"); return; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}"); return; }
  String ssid = doc["ssid"].as<String>();
  String password = doc["password"].as<String>();
  if (ssid.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"SSID required\"}"); return; }
  if (!wifiConfigSave(ssid, password)) { server.send(500, "application/json", "{\"ok\":false,\"error\":\"Save failed\"}"); return; }
  server.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

// ---------- Web UI (HTML stored in PROGMEM) ----------
static const char ROOT_PAGE[] PROGMEM = R"rawhtml(<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>MemouRF32</title><style>
body{font-family:sans-serif;max-width:600px;margin:1em auto;padding:1em;background:#fafafa;}
a{color:#0d6efd;} h1{color:#333;}
</style></head><body>
<h1>MemouRF32</h1>
<p><a href='/clone'>Clone RF</a> | <a href='/buttons'>Saved buttons</a> | <a href='/wifi'>WiFi settings</a></p>
<p>Use <b>Clone RF</b> to capture a signal, then save it as a button.</p>
<hr><p style='margin-top:1em;'><button onclick="if(confirm('This will clear the device config and reboot into the provisioning portal. Continue?')){fetch('/api/reconfigure',{method:'POST'}).then(()=>{document.body.innerHTML='<h2>Rebooting into provisioning...</h2><p>Connect to the MemouRF32-Setup hotspot to reconfigure.</p>'})}" style='background:#dc3545;color:#fff;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;'>Reconfigure device</button></p>
</body></html>)rawhtml";

static const char CLONE_PAGE[] PROGMEM = R"rawhtml(<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Clone RF</title><style>
body{font-family:sans-serif;max-width:600px;margin:1em auto;padding:1em;background:#fafafa;}
button{margin:4px;padding:8px 12px;cursor:pointer;border:1px solid #ccc;border-radius:4px;background:#fff;}
.bp{background:#0d6efd;color:#fff;border:none;}
pre{background:#eee;padding:8px;overflow:auto;font-size:11px;min-height:40px;max-height:200px;}
input{width:100%;padding:8px;margin:4px 0;box-sizing:border-box;border:1px solid #ccc;border-radius:4px;}
#status{margin:8px 0;font-weight:bold;}
.ok{color:#198754;} .warn{color:#dc3545;}
</style></head><body>
<h1>Clone RF</h1><p><a href='/'>Back</a></p>
<div id='status'>Idle. Press Start, then press your remote button.</div>
<button class='bp' id='startBtn' onclick='doStart()'>Start capture</button>
<button id='stopBtn' onclick='doStop()' disabled>Stop capture</button>
<pre id='out'></pre>
<div id='info'></div>
<label>Button name: <input type='text' id='name' placeholder='e.g. Garage door'></label>
<button class='bp' onclick='doSave()'>Save as button</button>
<script>
var capturing=false;
function doStart(){
  document.getElementById('out').textContent='';
  document.getElementById('info').innerHTML='';
  var x=new XMLHttpRequest();x.open('POST','/api/clone/start',true);
  x.onload=function(){
    capturing=true;
    document.getElementById('status').textContent='Listening... press your remote now';
    document.getElementById('startBtn').disabled=true;
    document.getElementById('stopBtn').disabled=false;
    poll();
  };
  x.send();
}
function doStop(){
  capturing=false;
  var x=new XMLHttpRequest();x.open('POST','/api/clone/stop',true);
  x.onload=function(){
    document.getElementById('stopBtn').disabled=true;
    document.getElementById('startBtn').disabled=false;
    getResult();
  };
  x.send();
}
function poll(){
  if(!capturing)return;
  setTimeout(function(){
    if(!capturing)return;
    var x=new XMLHttpRequest();x.open('GET','/api/clone/status',true);
    x.onload=function(){
      try{
        var d=JSON.parse(x.responseText);
        document.getElementById('status').textContent='Listening... '+d.count+' edges captured';
        if(!d.running){
          capturing=false;
          document.getElementById('stopBtn').disabled=true;
          document.getElementById('startBtn').disabled=false;
          document.getElementById('status').textContent='Signal detected and captured!';
          getResult();
        }else{
          poll();
        }
      }catch(e){poll();}
    };
    x.onerror=function(){poll();};
    x.send();
  },400);
}
function getResult(){
  var x=new XMLHttpRequest();x.open('GET','/api/clone/result',true);
  x.onload=function(){
    try{
      var d=JSON.parse(x.responseText);
      var p=d.pulses;
      document.getElementById('out').textContent=JSON.stringify(p);
      var info=document.getElementById('info');
      if(p.length>=20){
        info.innerHTML='<span class="ok">Captured '+p.length+' pulses. Looks good!</span>';
      }else if(p.length>0){
        info.innerHTML='<span class="warn">Only '+p.length+' pulses. Try again closer to the receiver.</span>';
      }else{
        info.innerHTML='<span class="warn">No signal detected. Press remote closer and try again.</span>';
      }
      document.getElementById('status').textContent='Done. '+p.length+' pulses captured.';
    }catch(e){
      document.getElementById('status').textContent='Error reading result';
    }
  };
  x.send();
}
function doSave(){
  var name=document.getElementById('name').value.trim();
  if(!name){alert('Enter a button name first');return;}
  var pulses=document.getElementById('out').textContent;
  try{var arr=JSON.parse(pulses);if(!arr.length){alert('No signal captured yet');return;}}
  catch(e){alert('No signal captured. Start capture and press your remote.');return;}
  var x=new XMLHttpRequest();x.open('POST','/api/buttons',true);
  x.setRequestHeader('Content-Type','application/json');
  x.onload=function(){
    try{
      var d=JSON.parse(x.responseText);
      if(d.ok){alert('Saved! Reboot device for HomeKit to update names.');window.location='/buttons';}
      else alert(d.error||'Save failed');
    }catch(e){alert('Save failed');}
  };
  x.send(JSON.stringify({name:name,type:'raw',rawPulses:pulses}));
}
</script></body></html>)rawhtml";

static const char BUTTONS_PAGE[] PROGMEM = R"rawhtml(<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Saved buttons</title><style>
body{font-family:sans-serif;max-width:600px;margin:1em auto;padding:1em;background:#fafafa;}
button{margin:4px;padding:6px 10px;cursor:pointer;border:1px solid #ccc;border-radius:4px;background:#fff;}
.bp{background:#0d6efd;color:#fff;border:none;} .bd{background:#dc3545;color:#fff;border:none;}
ul{list-style:none;padding:0;} li{border:1px solid #eee;margin:4px 0;padding:8px;display:flex;align-items:center;gap:8px;background:#fff;border-radius:4px;}
li span{flex:1;}
</style></head><body>
<h1>Saved buttons</h1><p><a href='/'>Back</a></p><ul id='list'></ul>
<script>
function load(){
  var x=new XMLHttpRequest();x.open('GET','/api/buttons',true);
  x.onload=function(){
    var d=JSON.parse(x.responseText);var list=document.getElementById('list');list.innerHTML='';
    var btns=d.buttons||d;
    if(!btns.length){list.innerHTML='<li>No saved buttons yet. <a href="/clone">Clone one.</a></li>';return;}
    btns.forEach(function(b){
      var li=document.createElement('li');
      li.innerHTML='<span><b>'+b.name+'</b> ('+b.type+')</span><button class="bp" onclick="trigger(\''+b.id+'\')">Send</button><button class="bd" onclick="del(\''+b.id+'\')">Delete</button>';
      list.appendChild(li);
    });
  };x.send();
}
function trigger(id){var x=new XMLHttpRequest();x.open('POST','/api/buttons/'+id+'/trigger',true);x.send();}
function del(id){if(!confirm('Delete?'))return;var x=new XMLHttpRequest();x.open('DELETE','/api/buttons/'+id,true);x.onload=function(){load();};x.send();}
load();setInterval(load,5000);
</script></body></html>)rawhtml";

void handleRoot() {
  if (s_isAPMode) { handleWifiConfigPage(); return; }
  if (!requireAuth()) return;
  server.send(200, "text/html", FPSTR(ROOT_PAGE));
}

void handleClonePage() {
  if (!requireAuth()) return;
  server.send(200, "text/html", FPSTR(CLONE_PAGE));
}

void handleButtonsPage() {
  if (!requireAuth()) return;
  server.send(200, "text/html", FPSTR(BUTTONS_PAGE));
}

void handleReconfigure() {
  if (!requireAuth()) return;
  server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Rebooting into provisioning\"}");
  delay(500);
  deviceConfigClear();
  delay(200);
  ESP.restart();
}

// ---------- HomeKit: bridge with Switch services ----------

struct RFSwitchService : Service::Switch {
  int buttonIndex;
  SpanCharacteristic* power;
  unsigned long autoOffAt;

  RFSwitchService(int idx) : Service::Switch(), buttonIndex(idx), autoOffAt(0) {
    power = new Characteristic::On(false);
  }

  boolean update() override {
    if (power->getNewVal()) {
      std::vector<SavedButton> btns = storageLoadButtons();
      if (buttonIndex >= 0 && buttonIndex < (int)btns.size())
        rfReplayButton(btns[buttonIndex]);
      autoOffAt = millis() + 1000;
    }
    return true;
  }

  void loop() override {
    if (autoOffAt > 0 && millis() >= autoOffAt) {
      power->setVal(0);
      autoOffAt = 0;
    }
  }
};

static RFSwitchService* g_rfButtons[MAX_SAVED_BUTTONS] = {};

void setupHomeSpan() {
  homeSpan.setWifiCredentials(s_savedSsid.c_str(), s_savedPass.c_str());

  homeSpan.setLogLevel(0);
  homeSpan.setPairingCode("12081208");
  homeSpan.setPortNum(1201);
  homeSpan.begin(Category::Bridges, s_deviceName.c_str(), s_deviceName.c_str(), "MemouRF32 1.0");

  char bridgeSerial[20];
  snprintf(bridgeSerial, sizeof(bridgeSerial), "MRF32-%s", s_deviceId.c_str());

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Name(s_deviceName.c_str());
  new Characteristic::Manufacturer("MemouRF32");
  new Characteristic::SerialNumber(bridgeSerial);
  new Characteristic::Model("RF Bridge");
  new Characteristic::FirmwareRevision("1.0");
  new Characteristic::Identify();

  std::vector<SavedButton> btns = storageLoadButtons();
  char serial[20];
  char nameBuf[64];

  for (int i = 0; i < MAX_SAVED_BUTTONS; i++) {
    new SpanAccessory();
    new Service::AccessoryInformation();
    if (i < (int)btns.size() && btns[i].name.length() > 0) {
      snprintf(nameBuf, sizeof(nameBuf), "%s", btns[i].name.c_str());
    } else {
      snprintf(nameBuf, sizeof(nameBuf), "RF Slot %d", i + 1);
    }
    new Characteristic::Name(nameBuf);
    snprintf(serial, sizeof(serial), "MRF32-%s-%02d", s_deviceId.c_str(), i + 1);
    new Characteristic::SerialNumber(serial);
    new Characteristic::Manufacturer("MemouRF32");
    new Characteristic::Model("RF Switch");
    new Characteristic::FirmwareRevision("1.0");
    new Characteristic::Identify();
    g_rfButtons[i] = new RFSwitchService(i);
  }
}

// ---------- Standalone setup (original behavior, unchanged) ----------
static void standaloneSetup() {
  if (!storageBegin()) Serial.println("Storage init failed");
  if (!rfBegin()) Serial.println("RF init failed (check SX127x)");

  s_hasSavedCreds = wifiConfigLoad(s_savedSsid, s_savedPass) && s_savedSsid.length() > 0;

  if (s_hasSavedCreds) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(s_deviceName.c_str());
    WiFi.setAutoReconnect(true);
    WiFi.begin(s_savedSsid.c_str(), s_savedPass.c_str());
    unsigned long deadline = millis() + (unsigned long)WIFI_CONNECT_TIMEOUT_MS;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
      delay(300);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.print("WiFi OK: ");
      Serial.println(WiFi.localIP());
      s_isAPMode = false;
    } else {
      Serial.println(" STA timeout, starting AP+STA for background retry");
      WiFi.mode(WIFI_AP_STA);
      WiFi.setHostname(s_deviceName.c_str());
      WiFi.setAutoReconnect(true);
      WiFi.softAP(s_deviceName.c_str(), AP_PASSWORD, 1, 0, 4);
      s_isAPMode = true;
      delay(100);
      dnsServer.start(53, "*", WiFi.softAPIP());
      Serial.print("Hotspot: ");
      Serial.print(s_deviceName);
      Serial.print(" IP ");
      Serial.println(WiFi.softAPIP());
      WiFi.begin(s_savedSsid.c_str(), s_savedPass.c_str());
      s_lastReconnectAttempt = millis();
    }
  } else {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(s_deviceName.c_str(), AP_PASSWORD, 1, 0, 4);
    s_isAPMode = true;
    delay(100);
    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial.print("Hotspot: ");
    Serial.print(s_deviceName);
    Serial.print(" IP ");
    Serial.println(WiFi.softAPIP());
  }

  server.on("/", handleRoot);
  server.on("/wifi", handleWifiConfigPage);
  server.on("/api/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/api/reconfigure", HTTP_POST, handleReconfigure);
  server.on("/generate_204", handleCaptiveRedirect);
  server.on("/gen_204", handleCaptiveRedirect);
  server.on("/hotspot-detect.html", handleCaptiveRedirect);
  server.on("/library/test/success.html", handleCaptiveRedirect);
  server.on("/ncsi.txt", handleCaptiveRedirect);
  server.on("/connecttest.txt", handleCaptiveRedirect);
  server.on("/fwlink", handleCaptiveRedirect);
  server.on("/clone", handleClonePage);
  server.on("/buttons", handleButtonsPage);
  server.on("/api/clone/start", HTTP_POST, handleCloneStart);
  server.on("/api/clone/stop", HTTP_POST, handleCloneStop);
  server.on("/api/clone/status", handleCloneStatus);
  server.on("/api/clone/result", handleCloneResult);
  server.on("/api/buttons", HTTP_GET, handleButtonsList);
  server.on("/api/buttons", HTTP_POST, handleButtonSave);
  server.onNotFound([]() {
    if (s_isAPMode) {
      handleCaptiveRedirect();
      return;
    }
    String uri = server.uri();
    if (uri.startsWith("/api/buttons/")) {
      int start = 13;
      int slash = uri.indexOf("/", start);
      String id = (slash > 0) ? uri.substring(start, slash) : uri.substring(start);
      String action = (slash > 0 && slash + 1 < (int)uri.length()) ? uri.substring(slash + 1) : "";
      if (server.method() == HTTP_POST && action == "trigger") {
        for (const auto& b : storageLoadButtons())
          if (b.id == id) { rfReplayButton(b); server.send(200, "application/json", "{\"ok\":true}"); return; }
        server.send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
        return;
      }
      if (server.method() == HTTP_DELETE && action.length() == 0) {
        if (storageRemoveButton(id)) { server.send(200, "application/json", "{\"ok\":true}"); return; }
        server.send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
        return;
      }
    }
    server.send(404, "text/plain", "Not Found");
  });

  server.begin();
  Serial.print("Web server on port ");
  Serial.println(WEB_SERVER_PORT);

  if (!s_isAPMode) {
    setupHomeSpan();
    s_homeSpanReady = true;
    Serial.println("HomeKit bridge ready. Add accessory in Home app.");
  } else if (s_hasSavedCreds) {
    Serial.println("AP+STA mode: WiFi will keep retrying in background.");
  } else {
    Serial.println("AP mode: configure WiFi to enable HomeKit.");
  }
}

// ---------- Gateway WiFi + web setup ----------
static void gatewayWifiSetup(const DeviceConfig& devCfg) {
  if (!storageBegin()) Serial.println("Storage init failed");

  s_hasSavedCreds = wifiConfigLoad(s_savedSsid, s_savedPass) && s_savedSsid.length() > 0;
  if (s_hasSavedCreds) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(s_deviceName.c_str());
    WiFi.setAutoReconnect(true);
    WiFi.begin(s_savedSsid.c_str(), s_savedPass.c_str());
    unsigned long deadline = millis() + (unsigned long)WIFI_CONNECT_TIMEOUT_MS;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) { delay(300); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.print("WiFi OK: ");
      Serial.println(WiFi.localIP());
      s_isAPMode = false;
    } else {
      Serial.println(" STA timeout, starting AP+STA");
      WiFi.mode(WIFI_AP_STA);
      WiFi.setHostname(s_deviceName.c_str());
      WiFi.setAutoReconnect(true);
      WiFi.softAP(s_deviceName.c_str(), AP_PASSWORD, 1, 0, 4);
      s_isAPMode = true;
      delay(100);
      dnsServer.start(53, "*", WiFi.softAPIP());
      WiFi.begin(s_savedSsid.c_str(), s_savedPass.c_str());
      s_lastReconnectAttempt = millis();
    }
  } else {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(s_deviceName.c_str(), AP_PASSWORD, 1, 0, 4);
    s_isAPMode = true;
    delay(100);
    dnsServer.start(53, "*", WiFi.softAPIP());
  }

  // Register gateway-specific web routes (node management + existing clone/buttons)
  server.on("/", handleRoot);
  server.on("/wifi", handleWifiConfigPage);
  server.on("/api/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/api/reconfigure", HTTP_POST, handleReconfigure);
  server.on("/generate_204", handleCaptiveRedirect);
  server.on("/gen_204", handleCaptiveRedirect);
  server.on("/hotspot-detect.html", handleCaptiveRedirect);
  server.on("/library/test/success.html", handleCaptiveRedirect);
  server.on("/ncsi.txt", handleCaptiveRedirect);
  server.on("/connecttest.txt", handleCaptiveRedirect);
  server.on("/fwlink", handleCaptiveRedirect);
  server.on("/clone", handleClonePage);
  server.on("/buttons", handleButtonsPage);
  server.on("/api/clone/start", HTTP_POST, handleCloneStart);
  server.on("/api/clone/stop", HTTP_POST, handleCloneStop);
  server.on("/api/clone/status", handleCloneStatus);
  server.on("/api/clone/result", handleCloneResult);
  server.on("/api/buttons", HTTP_GET, handleButtonsList);
  server.on("/api/buttons", HTTP_POST, handleButtonSave);
  // Gateway node APIs are registered by gatewayWebSetup (separate module)
  server.onNotFound([]() {
    if (s_isAPMode) { handleCaptiveRedirect(); return; }
    String uri = server.uri();
    if (uri.startsWith("/api/buttons/")) {
      int start = 13;
      int slash = uri.indexOf("/", start);
      String id = (slash > 0) ? uri.substring(start, slash) : uri.substring(start);
      String action = (slash > 0 && slash + 1 < (int)uri.length()) ? uri.substring(slash + 1) : "";
      if (server.method() == HTTP_POST && action == "trigger") {
        for (const auto& b : storageLoadButtons())
          if (b.id == id) { rfReplayButton(b); server.send(200, "application/json", "{\"ok\":true}"); return; }
        server.send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
        return;
      }
      if (server.method() == HTTP_DELETE && action.length() == 0) {
        if (storageRemoveButton(id)) { server.send(200, "application/json", "{\"ok\":true}"); return; }
        server.send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
        return;
      }
    }
    server.send(404, "text/plain", "Not Found");
  });

  gatewayWebSetup(server);

  server.begin();
  Serial.print("Web server on port ");
  Serial.println(WEB_SERVER_PORT);

  gatewaySetup(devCfg);

  if (!s_isAPMode) {
    gatewayHomeKitSetup(devCfg, s_deviceName.c_str(), s_deviceId.c_str());
    s_homeSpanReady = true;
    Serial.println("Gateway HomeKit bridge ready.");
  }
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  delay(500);

  buildDeviceId();
  Serial.print("MemouRF32 starting [");
  Serial.print(s_deviceName);
  Serial.println("]");

  // Check if provisioning is needed (no config or PRG button held)
  if (provisioningShouldRun()) {
    provisioningRun(s_deviceName.c_str());
    return; // provisioningRun reboots, but just in case
  }

  DeviceConfig devCfg = deviceConfigLoad();
  s_activeRole = devCfg.provisioned ? devCfg.role : ROLE_STANDALONE;
  Serial.printf("Role: %s\n",
    s_activeRole == ROLE_GATEWAY ? "Gateway" :
    s_activeRole == ROLE_REMOTE  ? "Remote"  : "Standalone");

  switch (s_activeRole) {
    case ROLE_STANDALONE:
      standaloneSetup();
      break;
    case ROLE_GATEWAY:
      gatewayWifiSetup(devCfg);
      break;
    case ROLE_REMOTE:
      remoteSetup(devCfg);
      break;
  }
}

// ---------- Standalone loop (original behavior) ----------
static void standaloneLoop() {
  if (s_homeSpanReady) homeSpan.poll();
  if (s_isAPMode) dnsServer.processNextRequest();

  if (s_hasSavedCreds && WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - s_lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS) {
      Serial.println("WiFi reconnect attempt...");
      WiFi.begin(s_savedSsid.c_str(), s_savedPass.c_str());
      s_lastReconnectAttempt = now;
    }
  }

  if (s_hasSavedCreds && WiFi.status() == WL_CONNECTED && !s_homeSpanReady) {
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
    if (s_isAPMode) {
      dnsServer.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      WiFi.setHostname(s_deviceName.c_str());
      WiFi.setAutoReconnect(true);
      s_isAPMode = false;
    }
    setupHomeSpan();
    s_homeSpanReady = true;
    Serial.println("HomeKit bridge ready.");
  }

  if (rfCaptureRunning()) {
    if ((millis() - s_cloneStartMs >= CLONE_TIMEOUT_MS) || rfCaptureShouldAutoStop())
      rfCaptureStop();
  }
  server.handleClient();
  delay(1);
}

// ---------- Gateway loop ----------
static void gatewayMainLoop() {
  if (s_homeSpanReady) homeSpan.poll();
  if (s_isAPMode) dnsServer.processNextRequest();

  if (s_hasSavedCreds && WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - s_lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS) {
      WiFi.begin(s_savedSsid.c_str(), s_savedPass.c_str());
      s_lastReconnectAttempt = now;
    }
  }

  gatewayLoop();

  if (rfCaptureRunning()) {
    if ((millis() - s_cloneStartMs >= CLONE_TIMEOUT_MS) || rfCaptureShouldAutoStop())
      rfCaptureStop();
  }
  server.handleClient();
  delay(1);
}

void loop() {
  switch (s_activeRole) {
    case ROLE_STANDALONE: standaloneLoop(); break;
    case ROLE_GATEWAY:    gatewayMainLoop(); break;
    case ROLE_REMOTE:     remoteLoop(); break;
  }
}
