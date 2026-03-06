/**
 * Gateway web UI and API implementation.
 */

#include "gateway_web.h"
#include "gateway.h"
#include "config.h"
#include <ArduinoJson.h>

static WebServer* s_server = nullptr;

static bool gwRequireAuth() {
  if (!s_server->authenticate(WEB_USER, WEB_PASS)) {
    s_server->requestAuthentication();
    return false;
  }
  return true;
}

// ---------- API: node list ----------
static void handleNodeList() {
  if (!gwRequireAuth()) return;

  JsonDocument doc;
  JsonArray arr = doc["nodes"].to<JsonArray>();
  const NodeEntry* reg = gatewayGetRegistry();

  for (int i = 0; i < MAX_GATEWAY_NODES; i++) {
    const NodeEntry& n = reg[i];
    if (n.nodeId == 0) continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"]       = n.nodeId;
    o["name"]     = n.name;
    o["flags"]    = n.flags;
    o["caps"]     = n.capabilities;
    o["relays"]   = n.relayCount;
    o["relayState"] = n.relayState;
    o["rssi"]     = n.lastRssi;
    o["snr"]      = n.lastSnr;
    o["online"]   = gatewayIsNodeOnline(n.nodeId);
    o["lastSeen"] = n.lastSeenMs > 0 ? (millis() - n.lastSeenMs) / 1000 : -1;
    char fwBuf[12];
    snprintf(fwBuf, sizeof(fwBuf), "%d.%d.%d", n.fwVersion[0], n.fwVersion[1], n.fwVersion[2]);
    o["fw"]       = fwBuf;
    o["rfButtons"] = n.rfButtonCount;
  }

  String out;
  serializeJson(doc, out);
  s_server->send(200, "application/json", out);
}

// ---------- API: approve pending node ----------
static void handleNodeApprove() {
  if (!gwRequireAuth()) return;
  String body = s_server->arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  uint8_t nodeId = doc["id"] | 0;
  if (gatewayApproveNode(nodeId)) {
    s_server->send(200, "application/json", "{\"ok\":true}");
  } else {
    s_server->send(404, "application/json", "{\"ok\":false,\"error\":\"Not found or not pending\"}");
  }
}

// ---------- API: remove node ----------
static void handleNodeRemove() {
  if (!gwRequireAuth()) return;
  String body = s_server->arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  uint8_t nodeId = doc["id"] | 0;
  if (gatewayRemoveNode(nodeId)) {
    s_server->send(200, "application/json", "{\"ok\":true}");
  } else {
    s_server->send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
  }
}

// ---------- API: rename node ----------
static void handleNodeRename() {
  if (!gwRequireAuth()) return;
  String body = s_server->arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  uint8_t nodeId = doc["id"] | 0;
  const char* name = doc["name"] | "";
  if (strlen(name) == 0) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Name required\"}");
    return;
  }
  if (gatewaySetNodeName(nodeId, name)) {
    s_server->send(200, "application/json", "{\"ok\":true}");
  } else {
    s_server->send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
  }
}

// ---------- API: relay control ----------
static void handleRelayControl() {
  if (!gwRequireAuth()) return;
  String body = s_server->arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  uint8_t nodeId = doc["nodeId"] | 0;
  uint8_t relayIndex = doc["relay"] | 0;
  bool state = doc["state"] | false;
  if (gatewayQueueRelaySet(nodeId, relayIndex, state)) {
    s_server->send(200, "application/json", "{\"ok\":true}");
  } else {
    s_server->send(500, "application/json", "{\"ok\":false,\"error\":\"Queue full\"}");
  }
}

// ---------- API: RF replay ----------
static void handleRfReplayRemote() {
  if (!gwRequireAuth()) return;
  String body = s_server->arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  uint8_t nodeId = doc["nodeId"] | 0;
  uint8_t btnIdx = doc["buttonIndex"] | 0;
  if (gatewayQueueRfReplay(nodeId, btnIdx)) {
    s_server->send(200, "application/json", "{\"ok\":true}");
  } else {
    s_server->send(500, "application/json", "{\"ok\":false,\"error\":\"Queue full\"}");
  }
}

// ---------- API: ping node ----------
static void handlePingNode() {
  if (!gwRequireAuth()) return;
  String body = s_server->arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  uint8_t nodeId = doc["id"] | 0;
  if (gatewayQueuePing(nodeId)) {
    s_server->send(200, "application/json", "{\"ok\":true}");
  } else {
    s_server->send(500, "application/json", "{\"ok\":false,\"error\":\"Queue full\"}");
  }
}

// ---------- Node management page ----------
static const char GW_NODES_PAGE[] PROGMEM = R"rawhtml(<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Gateway - Nodes</title><style>
body{font-family:sans-serif;max-width:700px;margin:1em auto;padding:1em;background:#fafafa;}
h1{color:#333;font-size:1.3em;} a{color:#0d6efd;}
table{width:100%;border-collapse:collapse;margin:8px 0;font-size:0.9em;}
th,td{border:1px solid #ddd;padding:6px 8px;text-align:left;}
th{background:#eee;}
.on{color:#198754;font-weight:bold;} .off{color:#dc3545;}
button{margin:2px;padding:4px 8px;cursor:pointer;border:1px solid #ccc;border-radius:4px;background:#fff;font-size:0.85em;}
.bp{background:#0d6efd;color:#fff;border:none;} .bd{background:#dc3545;color:#fff;border:none;} .bg{background:#198754;color:#fff;border:none;}
</style></head><body>
<h1>Gateway - Remote Nodes</h1>
<p><a href='/'>Home</a> | <a href='/clone'>Clone RF</a> | <a href='/buttons'>Saved buttons</a> | <a href='/wifi'>WiFi</a></p>
<table><thead><tr><th>ID</th><th>Name</th><th>Status</th><th>Relays</th><th>RSSI</th><th>Last seen</th><th>Actions</th></tr></thead>
<tbody id='tbl'></tbody></table>
<h3>Pending approvals</h3>
<div id='pending'>None</div>
<script>
function load(){
  var x=new XMLHttpRequest();x.open('GET','/api/nodes',true);
  x.onload=function(){
    var d=JSON.parse(x.responseText);var tbl=document.getElementById('tbl');tbl.innerHTML='';
    var pend=document.getElementById('pending');pend.innerHTML='';
    var nodes=d.nodes||[];
    nodes.forEach(function(n){
      if(n.flags&2){
        pend.innerHTML+='<div>Node '+n.id+' ('+n.name+') <button class="bg" onclick="approve('+n.id+')">Approve</button> <button class="bd" onclick="remove('+n.id+')">Reject</button></div>';
        return;
      }
      if(!(n.flags&1))return;
      var tr=document.createElement('tr');
      var rl='';
      for(var i=0;i<n.relays;i++){
        var on=(n.relayState>>i)&1;
        rl+='R'+(i+1)+': <span class="'+(on?'on':'off')+'">'+(on?'ON':'OFF')+'</span> ';
        rl+='<button class="bp" onclick="relay('+n.id+','+i+','+(on?0:1)+')">Toggle</button> ';
      }
      tr.innerHTML='<td>'+n.id+'</td><td>'+n.name+'</td><td class="'+(n.online?'on':'off')+'">'+(n.online?'Online':'Offline')+'</td><td>'+rl+'</td><td>'+n.rssi+' dBm</td><td>'+(n.lastSeen>=0?n.lastSeen+'s ago':'N/A')+'</td><td><button onclick="ping('+n.id+')">Ping</button> <button class="bd" onclick="remove('+n.id+')">Remove</button></td>';
      tbl.appendChild(tr);
    });
    if(!pend.innerHTML)pend.innerHTML='None';
  };x.send();
}
function post(url,data,cb){var x=new XMLHttpRequest();x.open('POST',url,true);x.setRequestHeader('Content-Type','application/json');x.onload=function(){cb&&cb();load();};x.send(JSON.stringify(data));}
function relay(nid,ri,st){post('/api/nodes/relay',{nodeId:nid,relay:ri,state:st});}
function ping(nid){post('/api/nodes/ping',{id:nid});}
function approve(nid){post('/api/nodes/approve',{id:nid});}
function remove(nid){if(!confirm('Remove node '+nid+'?'))return;post('/api/nodes/remove',{id:nid});}
load();setInterval(load,5000);
</script></body></html>)rawhtml";

static void handleNodesPage() {
  if (!gwRequireAuth()) return;
  s_server->send(200, "text/html", FPSTR(GW_NODES_PAGE));
}

// ---------- Registration ----------

void gatewayWebSetup(WebServer& server) {
  s_server = &server;

  server.on("/nodes", handleNodesPage);
  server.on("/api/nodes", HTTP_GET, handleNodeList);
  server.on("/api/nodes/approve", HTTP_POST, handleNodeApprove);
  server.on("/api/nodes/remove", HTTP_POST, handleNodeRemove);
  server.on("/api/nodes/rename", HTTP_POST, handleNodeRename);
  server.on("/api/nodes/relay", HTTP_POST, handleRelayControl);
  server.on("/api/nodes/replay", HTTP_POST, handleRfReplayRemote);
  server.on("/api/nodes/ping", HTTP_POST, handlePingNode);
}
