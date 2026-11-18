#pragma once
#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

namespace Provision {
Preferences storage;
WebServer web(80);
DNSServer dns;
bool active = false;
bool joining = false;
bool failed = false;
bool hotspot = false;
uint32_t started = 0;
uint32_t attemptedAt = 0;
uint32_t connectedAt = 0;
String target, pass;

#if defined(ANNA_LANG_ZH)
const char page[] PROGMEM = R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Dot Wi-Fi 设置</title>
<style>body{font:17px system-ui;color:#f1f3ff;background:#0a1129;margin:36px auto;padding:0 22px;max-width:420px}h1{font-size:28px}label{display:block;margin-top:20px}button,input,select{font:inherit;width:100%;box-sizing:border-box;padding:12px;border-radius:9px;margin-top:7px}button{border:0;background:#aed1ff;color:#101a32;font-weight:bold}button:disabled{opacity:.5}p{line-height:1.55;color:#d1daf6}</style>
<h1>连接 Dot 的 Wi-Fi</h1><p>请选择附近的 2.4 GHz 网络。连接成功后，手表会退出配网。</p>
<button id="scan">扫描附近 Wi-Fi</button><form id="form"><label>附近的网络<select id="network"><option value="">也可在下方手动填写</option></select></label><label>网络名称<input id="ssid" required autocomplete="off" maxlength="32"></label><label>Wi-Fi 密码<input id="password" type="password" autocomplete="off" maxlength="63"></label><label><input id="hotspot" type="checkbox" style="width:auto"> 这是手机热点，保留家里的 Wi-Fi</label><button id="join">保存并连接</button></form>
<p id="status" aria-live="polite"></p><script>
const scan=document.querySelector('#scan'), network=document.querySelector('#network'), ssid=document.querySelector('#ssid'), hotspot=document.querySelector('#hotspot'), status=document.querySelector('#status'), join=document.querySelector('#join');
network.onchange=()=>{if(network.value)ssid.value=network.value;};
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
scan.onclick=async()=>{scan.disabled=true; status.textContent='正在扫描…';try{let data;
 for(let i=0;i<16;i++){data=await fetch('/scan',{cache:'no-store'}).then(r=>r.json());if(!data.scanning)break;await delay(500);}
 network.replaceChildren(new Option('请选择网络',''));for(const n of data.networks||[]) network.add(new Option(n.ssid+(n.open?'（开放）':''),n.ssid));
 status.textContent=network.options.length>1?'请选择要连接的网络。':'没有找到网络，请重新扫描。';}catch{status.textContent='扫描失败，请确认手机仍连着 Dot-Setup。';}finally{scan.disabled=false;}};
document.querySelector('#form').onsubmit=async e=>{e.preventDefault();join.disabled=true;status.textContent='手表正在连接…';
try{const response=await fetch('/join',{method:'POST',body:new URLSearchParams({ssid:ssid.value,password:document.querySelector('#password').value,hotspot:hotspot.checked?'1':'0'})});
 if(!response.ok)throw new Error();
 if(hotspot.checked){status.textContent='已保存待连接信息。现在让这台手机断开 Dot-Setup，打开个人热点。手表会等待两分钟。';document.querySelector('#password').value='';return;}
 for(let i=0;i<30;i++){await delay(700);const data=await fetch('/status',{cache:'no-store'}).then(r=>r.json());if(data.state==='connected'){status.textContent='已连接。你可以回到原来的 Wi-Fi。';document.querySelector('#password').value='';return;}if(data.state==='failed'){status.textContent='连接失败，请检查密码和信号后重试。';return;}}
 status.textContent='连接等待超时，请查看手表屏幕。';}catch{status.textContent='未能确认结果，请查看手表屏幕。';}finally{join.disabled=false;}};
scan.click();
</script></html>)HTML";
#elif defined(ANNA_LANG_JA)
const char page[] PROGMEM = R"HTML(<!doctype html><html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Dot の Wi-Fi 設定</title>
<style>body{font:17px system-ui;color:#f1f3ff;background:#0a1129;margin:36px auto;padding:0 22px;max-width:420px}h1{font-size:28px}label{display:block;margin-top:20px}button,input,select{font:inherit;width:100%;box-sizing:border-box;padding:12px;border-radius:9px;margin-top:7px}button{border:0;background:#aed1ff;color:#101a32;font-weight:bold}button:disabled{opacity:.5}p{line-height:1.55;color:#d1daf6}</style>
<h1>Dot を Wi-Fi に接続</h1><p>近くの 2.4 GHz ネットワークを選択してください。接続後、時計の設定画面が閉じます。</p>
<button id="scan">Wi-Fi を検索</button><form id="form"><label>近くのネットワーク<select id="network"><option value="">下に手入力することもできます</option></select></label><label>ネットワーク名<input id="ssid" required autocomplete="off" maxlength="32"></label><label>Wi-Fi パスワード<input id="password" type="password" autocomplete="off" maxlength="63"></label><label><input id="hotspot" type="checkbox" style="width:auto"> 自宅 Wi-Fi を保持し、テザリングを使う</label><button id="join">保存して接続</button></form>
<p id="status" aria-live="polite"></p><script>
const scan=document.querySelector('#scan'), network=document.querySelector('#network'), ssid=document.querySelector('#ssid'), hotspot=document.querySelector('#hotspot'), status=document.querySelector('#status'), join=document.querySelector('#join');
network.onchange=()=>{if(network.value)ssid.value=network.value;};
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
scan.onclick=async()=>{scan.disabled=true; status.textContent='検索中…';try{let data;
 for(let i=0;i<16;i++){data=await fetch('/scan',{cache:'no-store'}).then(r=>r.json());if(!data.scanning)break;await delay(500);}
 network.replaceChildren(new Option('ネットワークを選択',''));for(const n of data.networks||[]) network.add(new Option(n.ssid+(n.open?'（オープン）':''),n.ssid));
 status.textContent=network.options.length>1?'接続先を選択してください。':'ネットワークが見つかりません。再検索してください。';}catch{status.textContent='検索に失敗しました。Dot-Setup に接続しているか確認してください。';}finally{scan.disabled=false;}};
document.querySelector('#form').onsubmit=async e=>{e.preventDefault();join.disabled=true;status.textContent='時計を接続中…';
try{const response=await fetch('/join',{method:'POST',body:new URLSearchParams({ssid:ssid.value,password:document.querySelector('#password').value,hotspot:hotspot.checked?'1':'0'})});
 if(!response.ok)throw new Error();
 if(hotspot.checked){status.textContent='保存しました。Dot-Setup から切断し、テザリングをオンにしてください。時計は 2 分間待機します。';document.querySelector('#password').value='';return;}
 for(let i=0;i<30;i++){await delay(700);const data=await fetch('/status',{cache:'no-store'}).then(r=>r.json());if(data.state==='connected'){status.textContent='接続しました。元の Wi-Fi に戻れます。';document.querySelector('#password').value='';return;}if(data.state==='failed'){status.textContent='接続に失敗しました。パスワードと電波を確認してください。';return;}}
 status.textContent='接続がタイムアウトしました。時計の画面を確認してください。';}catch{status.textContent='結果は時計の画面で確認してください。';}finally{join.disabled=false;}};
scan.click();
</script></html>)HTML";
#else
const char page[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Dot Wi-Fi Setup</title>
<style>body{font:17px system-ui;color:#f1f3ff;background:#0a1129;margin:36px auto;padding:0 22px;max-width:420px}h1{font-size:28px}label{display:block;margin-top:20px}button,input,select{font:inherit;width:100%;box-sizing:border-box;padding:12px;border-radius:9px;margin-top:7px}button{border:0;background:#aed1ff;color:#101a32;font-weight:bold}button:disabled{opacity:.5}p{line-height:1.55;color:#d1daf6}</style>
<h1>Connect Dot to Wi-Fi</h1><p>Choose a nearby 2.4 GHz network. The watch closes setup once connected.</p>
<button id="scan">Scan for Wi-Fi</button><form id="form"><label>Nearby networks<select id="network"><option value="">Or enter a network below</option></select></label><label>Network name<input id="ssid" required autocomplete="off" maxlength="32"></label><label>Wi-Fi password<input id="password" type="password" autocomplete="off" maxlength="63"></label><label><input id="hotspot" type="checkbox" style="width:auto"> Use phone hotspot and keep home Wi-Fi</label><button id="join">Save and connect</button></form>
<p id="status" aria-live="polite"></p><script>
const scan=document.querySelector('#scan'), network=document.querySelector('#network'), ssid=document.querySelector('#ssid'), hotspot=document.querySelector('#hotspot'), status=document.querySelector('#status'), join=document.querySelector('#join');
network.onchange=()=>{if(network.value)ssid.value=network.value;};
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
scan.onclick=async()=>{scan.disabled=true; status.textContent='Scanning…';try{let data;
 for(let i=0;i<16;i++){data=await fetch('/scan',{cache:'no-store'}).then(r=>r.json());if(!data.scanning)break;await delay(500);}
 network.replaceChildren(new Option('Select a network',''));for(const n of data.networks||[]) network.add(new Option(n.ssid+(n.open?' (open)':''),n.ssid));
 status.textContent=network.options.length>1?'Choose a network to join.':'No networks found. Scan again.';}catch{status.textContent='Scan failed. Check that your phone is still on Dot-Setup.';}finally{scan.disabled=false;}};
document.querySelector('#form').onsubmit=async e=>{e.preventDefault();join.disabled=true;status.textContent='Connecting the watch…';
try{const response=await fetch('/join',{method:'POST',body:new URLSearchParams({ssid:ssid.value,password:document.querySelector('#password').value,hotspot:hotspot.checked?'1':'0'})});
 if(!response.ok)throw new Error();
 if(hotspot.checked){status.textContent='Saved. Disconnect this phone from Dot-Setup and start your hotspot. The watch will wait two minutes.';document.querySelector('#password').value='';return;}
 for(let i=0;i<30;i++){await delay(700);const data=await fetch('/status',{cache:'no-store'}).then(r=>r.json());if(data.state==='connected'){status.textContent='Connected. You can switch back to your usual Wi-Fi.';document.querySelector('#password').value='';return;}if(data.state==='failed'){status.textContent='Connection failed. Check the password and signal, then retry.';return;}}
 status.textContent='Connection timed out. Check the watch screen.';}catch{status.textContent='Check the watch screen for the result.';}finally{join.disabled=false;}};
scan.click();
</script></html>)HTML";
#endif

String quote(const String& value) {
    String out = "\"";
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<uint8_t>(c) < 0x20) out += ' ';
        else out += c;
    }
    return out + '"';
}

void setup(const char* setupPass) {
    active = true; failed = false; connectedAt = 0;
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("Dot-Setup", setupPass);
    dns.start(53, "*", WiFi.softAPIP());
    web.on("/", HTTP_GET, [] { web.send_P(200, "text/html; charset=utf-8", page); });
    web.on("/scan", HTTP_GET, [] {
        if (joining || connectedAt) { web.send(200, "application/json", "{\"scanning\":false,\"networks\":[]}"); return; }
        const int count = WiFi.scanComplete();
        if (count == WIFI_SCAN_FAILED) WiFi.scanNetworks(true, false);
        if (count < 0) { web.send(200, "application/json", "{\"scanning\":true}"); return; }
        String output = "{\"scanning\":false,\"networks\":[";
        bool comma = false;
        for (int i = 0; i < count && i < 30; ++i) {
            const String ssid = WiFi.SSID(i);
            if (!ssid.length()) continue;
            if (comma) output += ',';
            output += "{\"ssid\":" + quote(ssid) + ",\"open\":" +
                (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true}" : "false}");
            comma = true;
        }
        web.send(200, "application/json", output + "]}");
        WiFi.scanDelete();
    });
    web.on("/join", HTTP_POST, [] {
        const String ssid = web.arg("ssid"), password = web.arg("password");
        if (joining || connectedAt || !ssid.length() || ssid.length() > 32 ||
            password.length() > 63 || (password.length() > 0 && password.length() < 8)) {
            web.send(400, "text/plain", "Invalid Wi-Fi setup"); return;
        }
        target = ssid; pass = password; started = millis(); failed = false;
        hotspot = web.arg("hotspot") == "1";
        attemptedAt = 0;
        joining = true;
        WiFi.scanDelete();
        web.send(202, "text/plain", "Accepted");
    });
    web.on("/status", HTTP_GET, [] {
        web.sendHeader("Cache-Control", "no-store");
        web.send(200, "application/json", connectedAt ? "{\"state\":\"connected\"}" :
                 failed ? "{\"state\":\"failed\"}" : joining ? "{\"state\":\"joining\"}" : "{\"state\":\"idle\"}");
    });
    web.onNotFound([] { web.sendHeader("Location", "http://192.168.4.1/"); web.send(302); });
    web.begin();
}

void tick() {
    if (!active) return;
    dns.processNextRequest();
    web.handleClient();
    if (joining && WiFi.status() == WL_CONNECTED && WiFi.SSID() == target) {
        storage.putString(hotspot ? "hotspot-ssid" : "ssid", target);
        storage.putString(hotspot ? "hotspot-pass" : "pass", pass);
        storage.putBool("prefer-hotspot", hotspot);
        pass = "";
        joining = false;
        connectedAt = millis();
    } else if (joining && millis() - started > (hotspot ? 120000 : 18000)) {
        joining = false; failed = true; pass = "";
        WiFi.disconnect(false, false);
    } else if (joining && (!attemptedAt || (hotspot && millis() - attemptedAt > 10000))) {
        attemptedAt = millis();
        WiFi.begin(target.c_str(), pass.c_str());
    }
    if (connectedAt && millis() - connectedAt > 9000) ESP.restart();
}
}
