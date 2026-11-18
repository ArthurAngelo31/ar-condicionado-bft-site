/*
  Firmware ESP32 - Controle de Ar-condicionado via IR (BFT)
  Sala: LABORATÓRIO - testando unidade MIDEA (antes: Philco)

  Bibliotecas (Arduino IDE > Gerenciador de Bibliotecas):
    - WiFiManager (tzapu)
    - IRremoteESP8266 (crankyoldgit)
    - ArduinoJson (bblanchon, v7+)
  Placa: qualquer ESP32 (ex: "ESP32 Dev Module")

  HARDWARE (ver HANDOFF.md pra histórico completo). O código é o mesmo
  pras duas ligações abaixo, só muda a montagem:

  A) Sem transistor (funciona, alcance curto):
    GPIO4 --[150ohm]-- anodo (+) do LED IR
    GND   ------------ catodo (-) do LED IR

  B) Com transistor NPN (C945/BC547/2N2222, mais alcance):
    3V3 --[150ohm]-- anodo (+) do LED -- catodo (-) do LED -- COLETOR
    GPIO4 --[1k]-- BASE --[10k]-- GND
    EMISSOR -- GND   (GND do ESP comum a tudo)
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRutils.h>
#include <ESPmDNS.h>

// ===================== CONFIGURAÇÃO =====================
const int SALA_ID = 6;                 // id da sala "Laboratório" cadastrada no app
const uint16_t IR_LED_PIN = 4;         // GPIO -> resistor 150ohm -> anodo do LED IR
const char* ADMIN_SENHA = "admin123";  // senha de fábrica; depois de trocada pelo app, vale a salva na memória
const char* WIFI_PORTAL_SSID = "BFT-ESP-Setup";
const char* WIFI_PORTAL_SENHA = "bft12345"; // mínimo 8 caracteres
const char* MDNS_HOST = "bft-lab"; // endereço fixo: http://bft-lab.local, não muda mesmo trocando de rede

// Teste atual: Midea. Muitos Midea (e Springer Midea) usam o protocolo MIDEA, mas
// alguns modelos usam COOLIX. Se não reagir, digite 1 (COOLIX) no Monitor Serial:
// a escolha do menu passa a valer também pros comandos do site, até reiniciar.
decode_type_t acProtocol = decode_type_t::MIDEA;

// ===================== ESTADO =====================
// "ligado"/"temperatura" são o ÚLTIMO comando que o ESP mandou por IR.
// IR não tem retorno: o app nunca sabe o estado real do aparelho, só o que foi enviado.
bool ligado = false;
int temperatura = 22;
String currentToken = "";
String adminSenha = ADMIN_SENHA; // carregada da memória (NVS) no setup

struct Agenda {
  bool ativa = false;
  String horaLigar = "07:00";
  String horaDesligar = "18:00";
  bool diasAtivos[5] = {true, true, true, true, true};
  int tempLigar = 22;
};
Agenda agenda;
String agendaUltimoDisparo = "";

WebServer server(80);
IRac ac(IR_LED_PIN);
Preferences prefs;

// ===================== CORS (o app roda em outra origem) =====================
void addCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Admin-Token");
}

void handlePreflight() {
  addCors();
  server.send(204);
}

void enviarJson(int codigo, JsonDocument& doc) {
  addCors();
  String out;
  serializeJson(doc, out);
  server.send(codigo, "application/json", out);
}

void enviarErro(int codigo, const String& msg) {
  JsonDocument doc;
  doc["erro"] = msg;
  enviarJson(codigo, doc);
}

bool autenticado() {
  if (currentToken == "") return false;
  return server.header("X-Admin-Token") == currentToken;
}

// ===================== PERSISTÊNCIA DA AGENDA (sobrevive a reboot) =====================
void salvarAgenda() {
  prefs.begin("bft", false);
  prefs.putBool("ativa", agenda.ativa);
  prefs.putString("on", agenda.horaLigar);
  prefs.putString("off", agenda.horaDesligar);
  prefs.putInt("temp", agenda.tempLigar);
  uint8_t mascara = 0;
  for (int i = 0; i < 5; i++) if (agenda.diasAtivos[i]) mascara |= (1 << i);
  prefs.putUChar("dias", mascara);
  prefs.end();
}

void carregarAgenda() {
  prefs.begin("bft", true);
  agenda.ativa = prefs.getBool("ativa", false);
  agenda.horaLigar = prefs.getString("on", "07:00");
  agenda.horaDesligar = prefs.getString("off", "18:00");
  agenda.tempLigar = prefs.getInt("temp", 22);
  uint8_t mascara = prefs.getUChar("dias", 0b11111);
  for (int i = 0; i < 5; i++) agenda.diasAtivos[i] = mascara & (1 << i);
  adminSenha = prefs.getString("senha", ADMIN_SENHA);
  acProtocol = (decode_type_t)prefs.getInt("proto", (int)acProtocol);
  prefs.end();
}

// Protocolo escolhido pelo menu fica salvo: sobrevive a tirar o ESP da tomada/USB.
void salvarProtocolo(decode_type_t p) {
  acProtocol = p;
  prefs.begin("bft", false);
  prefs.putInt("proto", (int)p);
  prefs.end();
  Serial.printf("Protocolo salvo: %s (vale pro site e depois de reiniciar)\n", typeToString(p).c_str());
}

// ===================== IR =====================
void enviarComandoAc(bool on, int temp, decode_type_t protocolo) {
  ac.next.protocol = protocolo;
  ac.next.model = -1;
  ac.next.power = on;
  ac.next.mode = on ? stdAc::opmode_t::kCool : stdAc::opmode_t::kOff;
  ac.next.celsius = true;
  ac.next.degrees = temp;
  ac.next.fanspeed = stdAc::fanspeed_t::kAuto;
  ac.next.swingv = stdAc::swingv_t::kOff;
  ac.next.swingh = stdAc::swingh_t::kOff;
  ac.next.light = false;
  ac.next.beep = false;
  ac.next.econo = false;
  ac.next.filter = false;
  ac.next.turbo = false;
  ac.next.quiet = false;
  ac.next.sleep = -1;
  ac.next.clock = -1;
  ac.next.clean = false;
  ac.sendAc(ac.next, nullptr); // nullptr = sem comparação, sempre transmite

  Serial.printf("[IR] protocolo=%s power=%s temp=%d\n",
                typeToString(protocolo).c_str(), on ? "ON" : "OFF", temp);
}

void aplicarEstado() {
  enviarComandoAc(ligado, temperatura, acProtocol);
}

// ===================== HANDLERS HTTP =====================
void handleEstado() {
  JsonDocument doc;
  doc["id"] = SALA_ID;
  doc["ligado"] = ligado;
  doc["temperatura"] = temperatura;
  enviarJson(200, doc);
}

void handleLigar() {
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  int temp = (!err && body["temperatura"].is<int>()) ? (int)body["temperatura"] : temperatura;
  if (temp < 18 || temp > 24) { enviarErro(400, "Temperatura inválida"); return; }

  ligado = true;
  temperatura = temp;
  aplicarEstado();

  JsonDocument doc;
  doc["id"] = SALA_ID;
  doc["ligado"] = true;
  doc["temperatura"] = temperatura;
  enviarJson(200, doc);
}

void handleDesligar() {
  ligado = false;
  aplicarEstado();

  JsonDocument doc;
  doc["id"] = SALA_ID;
  doc["ligado"] = false;
  doc["temperatura"] = temperatura;
  enviarJson(200, doc);
}

void handleTemperatura() {
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err || !body["temperatura"].is<int>()) { enviarErro(400, "JSON inválido"); return; }
  int temp = body["temperatura"];
  if (temp < 18 || temp > 24) { enviarErro(400, "Temperatura inválida"); return; }
  if (!ligado) { enviarErro(409, "Sala desligada"); return; }

  temperatura = temp;
  aplicarEstado();

  JsonDocument doc;
  doc["id"] = SALA_ID;
  doc["ligado"] = ligado;
  doc["temperatura"] = temperatura;
  enviarJson(200, doc);
}

void handleLogin() {
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  String senha = (!err && body["senha"].is<const char*>()) ? body["senha"].as<String>() : "";
  if (senha != adminSenha) { enviarErro(401, "Senha incorreta"); return; }

  currentToken = "esp-" + String(millis()) + "-" + String(random(1000, 9999));
  JsonDocument doc;
  doc["token"] = currentToken;
  enviarJson(200, doc);
}

void handleGetAgenda() {
  if (!autenticado()) { enviarErro(401, "Não autenticado"); return; }
  JsonDocument doc;
  doc["ativa"] = agenda.ativa;
  doc["horaLigar"] = agenda.horaLigar;
  doc["horaDesligar"] = agenda.horaDesligar;
  JsonArray dias = doc["diasAtivos"].to<JsonArray>();
  for (int i = 0; i < 5; i++) dias.add(agenda.diasAtivos[i]);
  doc["tempLigar"] = agenda.tempLigar;
  enviarJson(200, doc);
}

void handleSetAgenda() {
  if (!autenticado()) { enviarErro(401, "Não autenticado"); return; }
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err) { enviarErro(400, "JSON inválido"); return; }

  agenda.ativa = body["ativa"] | false;
  agenda.horaLigar = String((const char*)(body["horaLigar"] | "07:00"));
  agenda.horaDesligar = String((const char*)(body["horaDesligar"] | "18:00"));
  agenda.tempLigar = body["tempLigar"] | 22;
  if (body["diasAtivos"].is<JsonArray>()) {
    JsonArray dias = body["diasAtivos"];
    for (int i = 0; i < 5 && i < (int)dias.size(); i++) agenda.diasAtivos[i] = dias[i];
  }

  salvarAgenda();
  agendaUltimoDisparo = "";

  JsonDocument doc;
  doc["ok"] = true;
  enviarJson(200, doc);
}

// Troca a senha de admin. Exige token E a senha atual; a nova fica salva na NVS.
void handleSenha() {
  if (!autenticado()) { enviarErro(401, "Não autenticado"); return; }
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err) { enviarErro(400, "JSON inválido"); return; }
  String atual = body["atual"] | "";
  String nova = body["nova"] | "";
  if (atual != adminSenha) { enviarErro(403, "Senha atual incorreta"); return; }
  if (nova.length() < 4 || nova.length() > 32) { enviarErro(400, "Nova senha deve ter de 4 a 32 caracteres"); return; }

  prefs.begin("bft", false);
  prefs.putString("senha", nova);
  prefs.end();
  adminSenha = nova;

  JsonDocument doc;
  doc["ok"] = true;
  enviarJson(200, doc);
}

void handleWifiReset() {
  addCors();
  server.send(200, "text/plain", "Resetando WiFi. Conecte na rede BFT-ESP-Setup para reconfigurar.");
  delay(500);
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

// ===================== SCHEDULER (equivalente real do scheduler.js mock) =====================
void checarAgenda() {
  if (!agenda.ativa) return;

  time_t agora = time(nullptr);
  struct tm t;
  localtime_r(&agora, &t);

  int idxDia = t.tm_wday - 1; // tm_wday: 0=domingo..6=sábado -> seg=0..sex=4
  if (idxDia < 0 || idxDia > 4) return;
  if (!agenda.diasAtivos[idxDia]) return;

  char hhmm[6];
  snprintf(hhmm, sizeof(hhmm), "%02d:%02d", t.tm_hour, t.tm_min);
  char chave[24];
  snprintf(chave, sizeof(chave), "%d-%d|%s", t.tm_year, t.tm_yday, hhmm);
  if (agendaUltimoDisparo == String(chave)) return;

  if (String(hhmm) == agenda.horaLigar) {
    agendaUltimoDisparo = chave;
    ligado = true;
    temperatura = agenda.tempLigar;
    aplicarEstado();
    Serial.println("[Agenda] Ligou automaticamente");
  } else if (String(hhmm) == agenda.horaDesligar) {
    agendaUltimoDisparo = chave;
    ligado = false;
    aplicarEstado();
    Serial.println("[Agenda] Desligou automaticamente");
  }
}

// ===================== PÁGINA DE TESTE DE PROTOCOLO (pelo celular, perto do ar) =====================
// Abra http://<IP do ESP>/teste no celular. Cada "Próximo" manda "ligar 22C" no
// próximo protocolo de ar suportado; "Funcionou! Salvar" grava o atual na NVS.
// shortcut: sem senha (qualquer um na rede pode trocar o protocolo); proteger se for pra produção.
int testeAtual = -1;

int proximoSuportado(int de, int passo) {
  for (int n = 0; n <= kLastDecodeType + 1; n++) {
    de += passo;
    if (de > kLastDecodeType) de = 0;
    if (de < 0) de = kLastDecodeType;
    if (IRac::isProtocolSupported((decode_type_t)de)) return de;
  }
  return (int)acProtocol;
}

const char PAGINA_TESTE[] = R"HTML(<!doctype html><html lang=pt-br><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Teste IR</title>
<style>body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 16px}
button{width:100%;padding:18px;margin:6px 0;font-size:18px;border-radius:10px;border:1px solid #888}
#s{font-size:22px;margin:16px 0;font-weight:bold}.ok{background:#1a7f37;color:#fff;border:0}</style>
<h2>Teste de protocolo IR</h2>
<p>Ar desligado, LED apontado pro receptor. Aperte <b>Próximo</b> e espere uns 3s. Quando o ar apitar ou ligar, aperte <b>Funcionou</b>.</p>
<div id=s>Protocolo atual: ...</div>
<button onclick="c('next')">Próximo ▶</button>
<button onclick="c('again')">Repetir este</button>
<button onclick="c('prev')">◀ Anterior</button>
<button onclick="c('off')">Desligar (neste protocolo)</button>
<button class=ok onclick="c('save')">Funcionou! Salvar este</button>
<script>async function c(a){s.textContent='Enviando...';try{const r=await(await fetch('/teste/cmd?a='+a)).json();
s.textContent='p'+r.numero+' '+r.nome+(r.salvo?' (salvo, o site usa este)':'')}catch(e){s.textContent='Erro: '+e}}c('info')</script>)HTML";

void handleTestePagina() {
  server.send(200, "text/html; charset=utf-8", PAGINA_TESTE);
}

void handleTesteCmd() {
  String a = server.arg("a");
  if (testeAtual < 0) testeAtual = (int)acProtocol;
  if (a == "next") testeAtual = proximoSuportado(testeAtual, 1);
  else if (a == "prev") testeAtual = proximoSuportado(testeAtual, -1);

  if (a == "next" || a == "prev" || a == "again") enviarComandoAc(true, 22, (decode_type_t)testeAtual);
  else if (a == "off") enviarComandoAc(false, 22, (decode_type_t)testeAtual);
  else if (a == "save") salvarProtocolo((decode_type_t)testeAtual);

  JsonDocument doc;
  doc["numero"] = testeAtual;
  doc["nome"] = typeToString((decode_type_t)testeAtual);
  doc["salvo"] = (testeAtual == (int)acProtocol);
  enviarJson(200, doc);
}

// ===================== MENU DE TESTE DE PROTOCOLO (via Monitor Serial) =====================
// Manda "ligar 22C" em cada protocolo de ar suportado, com 4s entre eles.
// Quando o ar apitar/ligar, o usuário aperta Enter e o protocolo atual fica salvo.
void varrerProtocolos() {
  Serial.println("Varredura: aponte o LED pro ar. Quando ele APITAR ou LIGAR, aperte Enter.");
  while (Serial.available()) Serial.read();
  for (int i = 0; i <= kLastDecodeType; i++) {
    decode_type_t p = (decode_type_t)i;
    if (!IRac::isProtocolSupported(p)) continue;
    Serial.printf("[p%d] %s...\n", i, typeToString(p).c_str());
    enviarComandoAc(true, 22, p);
    unsigned long inicio = millis();
    while (millis() - inicio < 4000) {
      server.handleClient();
      if (Serial.available()) {
        while (Serial.available()) Serial.read();
        salvarProtocolo(p);
        Serial.println("Se apitou no anterior, digite p<numero> dele (ex: p18) pra confirmar.");
        return;
      }
      delay(10);
    }
  }
  Serial.println("Varredura terminou sem escolha. Digite v pra repetir mais perto do ar.");
}

void menuSerial() {
  if (!Serial.available()) return;
  String linha = Serial.readStringUntil('\n');
  linha.trim();
  // "w" apaga a rede Wi-Fi salva e reinicia no portal BFT-ESP-Setup pra escolher outra.
  // Checado antes do toInt(), que transformaria "w" em 0 (= desligar).
  if (linha.equalsIgnoreCase("w")) {
    Serial.println("Apagando Wi-Fi salvo. Conecte na rede BFT-ESP-Setup (senha bft12345) e escolha a nova rede.");
    WiFiManager wm;
    wm.resetSettings();
    delay(500);
    ESP.restart();
  }
  // "t" deixa o LED aceso 5s direto (teste de fiação/transistor: o IR normal é rápido demais pra ver na câmera).
  if (linha.equalsIgnoreCase("t")) {
    Serial.println("LED IR aceso por 5 segundos (olhe pela camera do celular)...");
    pinMode(IR_LED_PIN, OUTPUT);
    digitalWrite(IR_LED_PIN, HIGH);
    delay(5000);
    digitalWrite(IR_LED_PIN, LOW);
    Serial.println("LED apagado.");
    return;
  }
  // "v" varre TODOS os protocolos de ar da biblioteca (pra marcas que ela não lista, ex: Elgin).
  if (linha.equalsIgnoreCase("v")) { varrerProtocolos(); return; }
  // "p<numero>" testa e salva um protocolo pelo número mostrado na varredura (ex: p18).
  if (linha.startsWith("p") || linha.startsWith("P")) {
    decode_type_t p = (decode_type_t)linha.substring(1).toInt();
    if (!IRac::isProtocolSupported(p)) { Serial.println("Numero de protocolo invalido."); return; }
    salvarProtocolo(p);
    enviarComandoAc(true, 22, p);
    return;
  }
  int opc = linha.toInt();

  decode_type_t protocolos[] = {
    decode_type_t::COOLIX, decode_type_t::MIDEA, decode_type_t::GREE,
    decode_type_t::ELECTRA_AC, decode_type_t::FUJITSU_AC,
    decode_type_t::HITACHI_AC1, decode_type_t::TCL112AC
  };
  const char* nomes[] = { "COOLIX", "MIDEA", "GREE", "ELECTRA_AC", "FUJITSU_AC", "HITACHI_AC1", "TCL112AC" };

  if (opc >= 1 && opc <= 7) {
    salvarProtocolo(protocolos[opc - 1]);
    Serial.printf("Testando %s: ligando em 22C...\n", nomes[opc - 1]);
    enviarComandoAc(true, 22, acProtocol);
  } else if (opc == 0) {
    Serial.println("Enviando DESLIGAR no protocolo atual...");
    enviarComandoAc(false, temperatura, acProtocol);
  } else {
    Serial.println("1=COOLIX 2=MIDEA 3=GREE 4=ELECTRA_AC 5=FUJITSU_AC 6=HITACHI_AC1 7=TCL112AC  0=desligar  v=varrer todos  p<n>=usar protocolo n  w=trocar Wi-Fi  t=LED aceso 5s");
  }
}

// ===================== SETUP / LOOP =====================
unsigned long ultimaChecagemAgenda = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  carregarAgenda();

  WiFiManager wm;
  bool ok = wm.autoConnect(WIFI_PORTAL_SSID, WIFI_PORTAL_SENHA);
  if (!ok) {
    Serial.println("Não conectou ao WiFi, reiniciando...");
    ESP.restart();
  }

  Serial.println();
  Serial.print("Conectado! IP deste ESP: ");
  Serial.println(WiFi.localIP());
  Serial.println("-> Cole esse IP no campo de IP da sala, no painel admin do app.");

  if (MDNS.begin(MDNS_HOST)) {
    Serial.print("mDNS ativo: http://");
    Serial.print(MDNS_HOST);
    Serial.println(".local  (endereço fixo, não muda trocando de rede - se o celular/notebook suportar)");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("mDNS falhou ao iniciar (usa o IP numérico mesmo).");
  }

  configTime(-3 * 3600, 0, "a.st1.ntp.br", "pool.ntp.org");

  String base = "/sala/" + String(SALA_ID);
  server.on((base + "/estado").c_str(), HTTP_GET, handleEstado);
  server.on((base + "/estado").c_str(), HTTP_OPTIONS, handlePreflight);
  server.on((base + "/ligar").c_str(), HTTP_POST, handleLigar);
  server.on((base + "/ligar").c_str(), HTTP_OPTIONS, handlePreflight);
  server.on((base + "/desligar").c_str(), HTTP_POST, handleDesligar);
  server.on((base + "/desligar").c_str(), HTTP_OPTIONS, handlePreflight);
  server.on((base + "/temperatura").c_str(), HTTP_POST, handleTemperatura);
  server.on((base + "/temperatura").c_str(), HTTP_OPTIONS, handlePreflight);

  server.on("/admin/login", HTTP_POST, handleLogin);
  server.on("/admin/login", HTTP_OPTIONS, handlePreflight);
  server.on("/admin/agenda", HTTP_GET, handleGetAgenda);
  server.on("/admin/agenda", HTTP_POST, handleSetAgenda);
  server.on("/admin/agenda", HTTP_OPTIONS, handlePreflight);
  server.on("/admin/senha", HTTP_POST, handleSenha);
  server.on("/admin/senha", HTTP_OPTIONS, handlePreflight);
  server.on("/wifi/reset", HTTP_GET, handleWifiReset);
  server.on("/teste", HTTP_GET, handleTestePagina);
  server.on("/teste/cmd", HTTP_GET, handleTesteCmd);

  // Sem isso o WebServer descarta o header e server.header("X-Admin-Token") volta vazio.
  const char* headersAdmin[] = { "X-Admin-Token" };
  server.collectHeaders(headersAdmin, 1);
  server.begin();

  Serial.println("Servidor HTTP pronto na porta 80.");
  Serial.println("Menu de teste IR: 1=COOLIX 2=MIDEA 3=GREE 4=ELECTRA_AC 5=FUJITSU_AC 6=HITACHI_AC1 7=TCL112AC  0=desligar  v=varrer todos  p<n>=usar protocolo n  w=trocar Wi-Fi  t=LED aceso 5s");
}

void loop() {
  server.handleClient();
  menuSerial();

  if (millis() - ultimaChecagemAgenda > 20000) {
    ultimaChecagemAgenda = millis();
    checarAgenda();
  }
}
