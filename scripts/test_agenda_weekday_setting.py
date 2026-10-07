#!/usr/bin/env python3
"""Exercise the real calendar JSON methods and web UI weekday control flow."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
settings = (root / "src/Settings.cpp").read_text()
header = (root / "src/Settings.h").read_text()
cpp = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <string>
struct String : std::string {
  using std::string::string;
  String(std::string s):std::string(s){}
  void remove(size_t pos) { erase(pos); }
};
namespace ArduinoJson {
template<> struct Converter<String> {
  static void toJson(const String& value, JsonVariant dst) { dst.set(value.c_str()); }
  static String fromJson(JsonVariantConst src) { return src.as<const char*>() ?: ""; }
  static bool checkJson(JsonVariantConst src) { return src.is<const char*>(); }
};
}
#define DEFAULT_CAL_LAT 0
#define DEFAULT_CAL_LON 0
#define DEFAULT_WEATHER_POLL_SEC 600
'''
cpp += header[header.index("struct CalendarSettings {"):header.index("// ---- Plane radar")]
cpp += settings[settings.index("void CalendarSettings::setDefaults()"):settings.index("// Top-level settings")].rsplit("// ===========================================================================", 1)[0]
cpp += r'''
int main() {
  CalendarSettings s; s.setDefaults(); assert(s.showWeekday);
  JsonDocument doc;
  s.toJson(doc["calendar"].to<JsonObject>());
  std::string wire; serializeJson(doc,wire);
  JsonDocument saved; assert(!deserializeJson(saved,wire));
  assert(saved["calendar"]["showWeekday"].is<bool>());
  assert(saved["calendar"]["showWeekday"].as<bool>());
  for(const char* json : {"{}", "{\"showWeekday\":null}", "{\"showWeekday\":0}", "{\"showWeekday\":\"false\"}"}) {
    JsonDocument update; assert(!deserializeJson(update,json));
    s.fromJson(update.as<JsonObjectConst>()); assert(s.showWeekday);
  }
  JsonDocument update; deserializeJson(update,"{\"showWeekday\":false}");
  s.fromJson(update.as<JsonObjectConst>()); assert(!s.showWeekday);
  JsonDocument missing; missing.to<JsonObject>();
  s.fromJson(missing.as<JsonObjectConst>()); assert(!s.showWeekday);
  deserializeJson(update,"{\"showWeekday\":1}");
  s.fromJson(update.as<JsonObjectConst>()); assert(!s.showWeekday);
  doc.clear(); s.toJson(doc["calendar"].to<JsonObject>());
  wire.clear(); serializeJson(doc,wire); deserializeJson(saved,wire);
  CalendarSettings restored; restored.setDefaults();
  restored.fromJson(saved["calendar"].as<JsonObjectConst>());
  assert(!restored.showWeekday);
  deserializeJson(update,"{\"showWeekday\":true}");
  restored.fromJson(update.as<JsonObjectConst>()); assert(restored.showWeekday);
}
'''
with tempfile.TemporaryDirectory(prefix="agenda-weekday-setting-", dir="/tmp") as tmp:
    source = Path(tmp) / "test.cpp"
    source.write_text(cpp)
    binary = Path(tmp) / "test"
    subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-I", str(root / ".pio/libdeps/smalltv/ArduinoJson/src"), str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

ui = (root / "src/webui.h").read_text()
js = r'''
const assert=require('node:assert/strict');
const elements={};
class Element {
 constructor(id) { this.id=id; this.style={}; this.dataset={}; this.value=''; this.checked=false; this.children=[]; this.options=[]; }
 appendChild(child) {
  if(child.parentNode) child.parentNode.children=child.parentNode.children.filter(x=>x!==child);
  this.children.push(child); child.parentNode=this; return child;
 }
 remove() { this.children.slice().forEach(x=>x.remove()); delete elements[this.id]; }
 set innerHTML(html) {
  this.children.slice().forEach(x=>x.remove()); this.children=[];
  for(const match of html.matchAll(/<(?:input|div)[^>]*id="([^"]+)"[^>]*>/g)) {
   const child=new Element(match[1]); child.checked=match[0].includes(' checked');
   elements[child.id]=child; this.appendChild(child);
  }
  this.options=[...html.matchAll(/value="([^"]+)"/g)].map(m=>({value:m[1]}));
 }
}
function add(id) { return elements[id]=new Element(id); }
for(const id of ['mode','carouselRow','carouselList','carouselLbl','agendaWeekdayControl','calendarShowWeekday','calendar','usage',
 'apSsid','apPass','hostname','daemonIp','brightness','brVal','rotation','autoBrightness','backlightInverted','calLat','calLon','calPlace']) add(id);
elements.calendarShowWeekday.checked=true;
elements.agendaWeekdayControl.appendChild(elements.calendarShowWeekday);
elements.carouselRow.appendChild(elements.agendaWeekdayControl);
function $(id) { return elements[id]||null; }
function sv(id,v) { if($(id))$(id).value=v; }
function sc(id,v) { if($(id))$(id).checked=v; }
function gv(id) { return $(id)?$(id).value:''; }
function gc(id) { return !!($(id)&&$(id).checked); }
function toneNum(){return 100;} function collectWifi(){return [];} function getCalIds(){return '';} function getCalColorIds(){return '';}
function setTone(){} function renderWifi(){} function renderAps(){} function renderCalIds(){} function calRefreshLocLabel(){} function resolveTz(){}
function radarSrcChanged(){} function hideFeat(name){if($(name))$(name).remove();}
const document={querySelectorAll(){return [];}};
let fixture={}; function j(){return Promise.resolve(fixture);}
var C={},_resolvedTz='',_resolvedTzPosix='UTC0';
'''
js += ui[ui.index("var CAR_MODES="):ui.index("function hideFeat(name)")]
js += ui[ui.index("function modeChanged()"):ui.index("function esc(s)")]
js += ui[ui.index("function collect()"):ui.index("function saveAll()")]
js += r'''
(async()=>{
 for(const calendar of [undefined,{}, {showWeekday:false},{showWeekday:true}]) {
  fixture={mode:'agenda',calendar}; await loadConfig();
  assert.equal(gc('calendarShowWeekday'), !calendar||calendar.showWeekday!==false);
 }
 const control=$('agendaWeekdayControl'), checkbox=$('calendarShowWeekday');
 checkbox.checked=false;
 for(const mode of ['carousel','weather','album','agenda','carousel']) {
  $('mode').value=mode; modeChanged();
  assert.equal($('calendarShowWeekday'),checkbox);
  assert.equal(checkbox.checked,false);
  assert.equal(control.style.display, mode==='agenda'||mode==='carousel'?'flex':'none');
  assert.equal(control.parentNode, mode==='carousel'?$('agendaWeekdaySlot'):$('carouselRow'));
  assert.equal(collect().calendar.showWeekday,false);
 }
 const agendaIndex=carOrder.indexOf('agenda'); carMove(agendaIndex,-1);
 assert.equal($('calendarShowWeekday'),checkbox); assert.equal(checkbox.checked,false);
 assert.equal(control.parentNode,$('agendaWeekdaySlot'));
 rebuildModeSelect(); modeChanged(); assert.equal(checkbox.checked,false);
 assert.equal(Object.keys(elements).filter(id=>id==='calendarShowWeekday').length,1);
 $('calendar').remove(); modeChanged();
 assert.equal(control.style.display,'none'); assert.equal(checkbox.checked,false);
 assert.equal(collect().calendar,undefined);
 console.log('PASS: real nested JSON defaults/bool-only/round-trip; UI load, single-node reorder/mode switches, save and absent calendar');
})().catch(error=>{console.error(error);process.exitCode=1;});
'''
subprocess.run(["node"], input=js, text=True, check=True)
