/*
  Firmware ESP32 - Controle de Ar-condicionado via IR (BFT)
  Sala: LABORATÓRIO - até 2 aparelhos, cada um com sua marca/protocolo

  Bibliotecas (Arduino IDE > Gerenciador de Bibliotecas):
    - WiFiManager (tzapu)
    - IRremoteESP8266 (crankyoldgit)
    - ArduinoJson (bblanchon, v7+)
  Placa: qualquer ESP32 (ex: "ESP32 Dev Module")
  Partition Scheme: Tools > Partition Scheme > "Huge APP (3MB No OTA/1MB SPIFFS)"
  (o HTTPS da ponte deixa o firmware maior que a partição padrão de 1.2MB)

  HARDWARE (ver HANDOFF.md pra histórico completo). O código é o mesmo
  pras duas ligações abaixo, só muda a montagem:

  A) Sem transistor (funciona, alcance curto):
    GPIO4 --[150ohm]-- anodo (+) do LED IR
    GND   ------------ catodo (-) do LED IR

  B) Com transistor NPN (C945/BC547/2N2222, mais alcance):
    3V3 --[150ohm]-- anodo (+) do LED -- catodo (-) do LED -- COLETOR
    GPIO4 --[1k]-- BASE --[10k]-- GND
    EMISSOR -- GND   (GND do ESP comum a tudo)

  2 APARELHOS NA MESMA SALA: o mesmo LED IR fica posicionado de forma a
  alcançar os dois ar-condicionados. Cada aparelho tem seu próprio
  protocolo (marca), configurado pelo app (Admin > Salas). Comandos "pra
  sala toda" (automação/agenda) mandam o sinal de cada aparelho em
  sequência, com uma pausa entre eles; comandos individuais (tela de
  controle de 1 aparelho no app) mandam só o sinal daquele aparelho.
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
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ===================== CONFIGURAÇÃO =====================
const int SALA_ID = 6;                 // id da sala "Laboratório" cadastrada no app
const uint16_t IR_LED_PIN = 4;         // GPIO -> resistor 150ohm -> anodo do LED IR
const char* ADMIN_SENHA = "admin123";  // senha de fábrica; depois de trocada pelo app, vale a salva na memória
const char* WIFI_PORTAL_SSID = "BFT-ESP-Setup";
const char* WIFI_PORTAL_SENHA = "bft12345"; // mínimo 8 caracteres
const char* MDNS_HOST = "bft-lab"; // endereço fixo: http://bft-lab.local, não muda mesmo trocando de rede

// Servidor-ponte: endereço FIXO de verdade (nuvem), não depende de rede nem
// de notebook ligado. O ESP consulta ele sozinho (o ESP nunca recebe conexão
// de fora, só pergunta "tem comando novo?"). Ver ponte-servidor/servidor.js.
const char* PONTE_URL = "https://ar-condicionado-bft-ponte.onrender.com";
const unsigned long PONTE_INTERVALO_MS = 3000; // a cada 3s - mantém o Render acordado tbm

// ===================== ESTADO POR APARELHO =====================
// Até 2 ar-condicionados físicos na mesma sala, cada um com seu protocolo
// (marca) - configurável pelo app (Admin > Salas) ou, pra descobrir o
// protocolo de um aparelho específico, pelo Monitor Serial (comandos
// a1/a2 escolhem o alvo, depois v/p<n> testam e salvam nele).
// IR não tem retorno: o app nunca sabe o estado real do aparelho, só o
// que foi enviado por último.
const int MAX_APARELHOS = 2;
struct Aparelho {
  bool ligado = false;
  int temperatura = 22;
  decode_type_t protocolo = decode_type_t::MIDEA;
};
int qtdAparelhos = 1; // 1 ou 2, salvo na NVS
Aparelho aparelhos[MAX_APARELHOS];

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

// ===================== PERSISTÊNCIA (sobrevive a reboot) =====================
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

// Quantidade de aparelhos e o protocolo de cada um - configurado pelo app
// (Admin > Salas) ou pelo Monitor Serial (a1/a2 + v/p<n>).
void salvarConfigAparelhos() {
  prefs.begin("bft", false);
  prefs.putInt("qtd", qtdAparelhos);
  prefs.putInt("proto", (int)aparelhos[0].protocolo);
  prefs.putInt("proto2", (int)aparelhos[1].protocolo);
  prefs.end();
}

void carregarConfig() {
  prefs.begin("bft", true);
  agenda.ativa = prefs.getBool("ativa", false);
  agenda.horaLigar = prefs.getString("on", "07:00");
  agenda.horaDesligar = prefs.getString("off", "18:00");
  agenda.tempLigar = prefs.getInt("temp", 22);
  uint8_t mascara = prefs.getUChar("dias", 0b11111);
  for (int i = 0; i < 5; i++) agenda.diasAtivos[i] = mascara & (1 << i);
  adminSenha = prefs.getString("senha", ADMIN_SENHA);
  qtdAparelhos = prefs.getInt("qtd", 1);
  if (qtdAparelhos < 1 || qtdAparelhos > MAX_APARELHOS) qtdAparelhos = 1;
  aparelhos[0].protocolo = (decode_type_t)prefs.getInt("proto", (int)decode_type_t::MIDEA);
  aparelhos[1].protocolo = (decode_type_t)prefs.getInt("proto2", (int)decode_type_t::MIDEA);
  prefs.end();
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

// Manda o sinal de UM aparelho específico (índice 0 ou 1), com o protocolo dele.
void aplicarAparelho(int idx) {
  if (idx < 0 || idx >= qtdAparelhos) return;
  enviarComandoAc(aparelhos[idx].ligado, aparelhos[idx].temperatura, aparelhos[idx].protocolo);
}

// Manda o sinal de TODOS os aparelhos configurados, em sequência (pausa entre
// eles quando são 2, cada um com seu protocolo). Usado pela automação/agenda
// e pelos botões "ligar/desligar a sala toda".
void aplicarTodos() {
  for (int i = 0; i < qtdAparelhos; i++) {
    if (i > 0) delay(400); // dá tempo do aparelho anterior processar antes do próximo sinal
    aplicarAparelho(i);
  }
}

// Manda o sinal só dos aparelhos que estão LIGADOS (evita reenviar "desligar"
// pros que já estão desligados sem necessidade - usado ao mudar temperatura).
void aplicarLigados() {
  bool primeiro = true;
  for (int i = 0; i < qtdAparelhos; i++) {
    if (!aparelhos[i].ligado) continue;
    if (!primeiro) delay(400);
    aplicarAparelho(i);
    primeiro = false;
  }
}

// ===================== SINCRONIZAÇÃO COM A PONTE (endereço fixo na nuvem) =====================
// O ESP nunca recebe conexão de fora - só pergunta pra ponte "tem comando
// novo?" de tempos em tempos. Funciona em qualquer rede, mesmo as que
// bloqueiam conexão direta entre aparelhos (hotspot de celular etc).
// setInsecure() = não valida o certificado HTTPS da ponte (simplifica muito
// no ESP32; aceitável aqui pelo mesmo motivo de /wifi/reset e /teste não
// terem senha - ver "Problemas conhecidos" no HANDOFF).
long ultimoSeqAplicadoPonte = -1;
bool primeiroContatoPonte = true;
unsigned long ultimaSincPonte = 0;

void reportarEstadoNaPonte() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String(PONTE_URL) + "/dispositivo/" + String(SALA_ID) + "/estado";
  if (!http.begin(client, url)) return;
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  JsonObject ap = doc["aplicado"].to<JsonObject>();
  ap["qtd"] = qtdAparelhos;
  JsonArray arr = ap["aparelhos"].to<JsonArray>();
  for (int i = 0; i < qtdAparelhos; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ligado"] = aparelhos[i].ligado;
    o["temperatura"] = aparelhos[i].temperatura;
  }
  JsonObject ag = doc["agendaAplicada"].to<JsonObject>();
  ag["ativa"] = agenda.ativa;
  ag["horaLigar"] = agenda.horaLigar;
  ag["horaDesligar"] = agenda.horaDesligar;
  ag["tempLigar"] = agenda.tempLigar;
  JsonArray dias = ag["diasAtivos"].to<JsonArray>();
  for (int i = 0; i < 5; i++) dias.add(agenda.diasAtivos[i]);

  String corpo;
  serializeJson(doc, corpo);
  http.POST(corpo);
  http.end();
}

void sincronizarComPonte() {
  if (millis() - ultimaSincPonte < PONTE_INTERVALO_MS) return;
  ultimaSincPonte = millis();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String(PONTE_URL) + "/dispositivo/" + String(SALA_ID) + "/comando";
  if (!http.begin(client, url)) return;
  http.setTimeout(8000);
  int codigo = http.GET();
  if (codigo != 200) { http.end(); return; }
  String resposta = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, resposta)) return;

  long seq = doc["desejado"]["seq"] | -1L;
  if (primeiroContatoPonte) {
    // Só sincroniza o número de sequência na primeira vez - não dispara IR
    // sozinho assim que o ESP liga (evitaria repetir o último comando à toa).
    ultimoSeqAplicadoPonte = seq;
    primeiroContatoPonte = false;
  } else if (seq != ultimoSeqAplicadoPonte) {
    if (doc["desejado"]["aparelhos"].is<JsonArray>()) {
      JsonArray arr = doc["desejado"]["aparelhos"];
      for (int i = 0; i < qtdAparelhos && i < (int)arr.size(); i++) {
        aparelhos[i].ligado = arr[i]["ligado"] | false;
        aparelhos[i].temperatura = arr[i]["temperatura"] | 22;
      }
    }
    String alvo = doc["desejado"]["alvo"] | String("todos");
    if (alvo == "todos") aplicarTodos();
    else aplicarAparelho(alvo.toInt());
    ultimoSeqAplicadoPonte = seq;
  }

  // Config (quantidade de aparelhos + protocolo de cada um), definida pelo
  // app em Admin > Salas. Só regrava na NVS se algo realmente mudou, pra não
  // desgastar a flash gravando a cada 3s à toa.
  if (!doc["configDesejada"].isNull()) {
    JsonObject cd = doc["configDesejada"];
    int novoQtd = cd["qtd"] | qtdAparelhos;
    decode_type_t novosProtocolos[MAX_APARELHOS];
    for (int i = 0; i < MAX_APARELHOS; i++) novosProtocolos[i] = aparelhos[i].protocolo;
    if (cd["protocolos"].is<JsonArray>()) {
      JsonArray arr = cd["protocolos"];
      for (int i = 0; i < MAX_APARELHOS && i < (int)arr.size(); i++) {
        String nome = arr[i].as<String>();
        decode_type_t p = strToDecodeType(nome.c_str());
        if (IRac::isProtocolSupported(p)) novosProtocolos[i] = p;
      }
    }
    bool mudouCfg = (novoQtd != qtdAparelhos);
    for (int i = 0; i < MAX_APARELHOS; i++) if (novosProtocolos[i] != aparelhos[i].protocolo) mudouCfg = true;
    if (mudouCfg && novoQtd >= 1 && novoQtd <= MAX_APARELHOS) {
      qtdAparelhos = novoQtd;
      for (int i = 0; i < MAX_APARELHOS; i++) aparelhos[i].protocolo = novosProtocolos[i];
      salvarConfigAparelhos();
    }
  }

  // Agenda, mesma lógica: só regrava se mudou de verdade.
  if (!doc["agendaDesejada"].isNull()) {
    JsonObject ad = doc["agendaDesejada"];
    bool novaAtiva = ad["ativa"] | agenda.ativa;
    String novoLigar = String((const char*)(ad["horaLigar"] | agenda.horaLigar.c_str()));
    String novoDesligar = String((const char*)(ad["horaDesligar"] | agenda.horaDesligar.c_str()));
    int novoTemp = ad["tempLigar"] | agenda.tempLigar;
    bool novosDias[5];
    for (int i = 0; i < 5; i++) novosDias[i] = agenda.diasAtivos[i];
    if (ad["diasAtivos"].is<JsonArray>()) {
      JsonArray dias = ad["diasAtivos"];
      for (int i = 0; i < 5 && i < (int)dias.size(); i++) novosDias[i] = dias[i];
    }
    bool mudouAg = (novaAtiva != agenda.ativa || novoLigar != agenda.horaLigar ||
                    novoDesligar != agenda.horaDesligar || novoTemp != agenda.tempLigar);
    for (int i = 0; i < 5; i++) if (novosDias[i] != agenda.diasAtivos[i]) mudouAg = true;
    if (mudouAg) {
      agenda.ativa = novaAtiva;
      agenda.horaLigar = novoLigar;
      agenda.horaDesligar = novoDesligar;
      agenda.tempLigar = novoTemp;
      for (int i = 0; i < 5; i++) agenda.diasAtivos[i] = novosDias[i];
      salvarAgenda();
      agendaUltimoDisparo = "";
    }
  }

  reportarEstadoNaPonte();
}

// ===================== HANDLERS HTTP (sala toda - "automação") =====================
void handleEstado() {
  JsonDocument doc;
  doc["id"] = SALA_ID;
  doc["qtd"] = qtdAparelhos;
  doc["ligado"] = aparelhos[0].ligado;        // compat: topo = aparelho 1
  doc["temperatura"] = aparelhos[0].temperatura;
  JsonArray arr = doc["aparelhos"].to<JsonArray>();
  for (int i = 0; i < qtdAparelhos; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ligado"] = aparelhos[i].ligado;
    o["temperatura"] = aparelhos[i].temperatura;
    o["protocolo"] = typeToString(aparelhos[i].protocolo);
  }
  enviarJson(200, doc);
}

void handleLigar() {
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  int temp = (!err && body["temperatura"].is<int>()) ? (int)body["temperatura"] : aparelhos[0].temperatura;
  if (temp < 18 || temp > 24) { enviarErro(400, "Temperatura inválida"); return; }

  for (int i = 0; i < qtdAparelhos; i++) { aparelhos[i].ligado = true; aparelhos[i].temperatura = temp; }
  aplicarTodos();

  JsonDocument doc;
  doc["id"] = SALA_ID;
  doc["ligado"] = true;
  doc["temperatura"] = temp;
  enviarJson(200, doc);
}

void handleDesligar() {
  for (int i = 0; i < qtdAparelhos; i++) aparelhos[i].ligado = false;
  aplicarTodos();

  JsonDocument doc;
  doc["id"] = SALA_ID;
  doc["ligado"] = false;
  doc["temperatura"] = aparelhos[0].temperatura;
  enviarJson(200, doc);
}

void handleTemperatura() {
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err || !body["temperatura"].is<int>()) { enviarErro(400, "JSON inválido"); return; }
  int temp = body["temperatura"];
  if (temp < 18 || temp > 24) { enviarErro(400, "Temperatura inválida"); return; }

  bool algumLigado = false;
  for (int i = 0; i < qtdAparelhos; i++) if (aparelhos[i].ligado) algumLigado = true;
  if (!algumLigado) { enviarErro(409, "Sala desligada"); return; }

  for (int i = 0; i < qtdAparelhos; i++) if (aparelhos[i].ligado) aparelhos[i].temperatura = temp;
  aplicarLigados();

  JsonDocument doc;
  doc["id"] = SALA_ID;
  doc["ligado"] = true;
  doc["temperatura"] = temp;
  enviarJson(200, doc);
}

// ===================== HANDLERS HTTP (1 aparelho específico) =====================
void enviarEstadoAparelho(int idx) {
  JsonDocument doc;
  doc["id"] = SALA_ID;
  doc["aparelho"] = idx + 1;
  doc["ligado"] = aparelhos[idx].ligado;
  doc["temperatura"] = aparelhos[idx].temperatura;
  enviarJson(200, doc);
}

void handleLigarAparelhoN(int idx) {
  if (idx < 0 || idx >= qtdAparelhos) { enviarErro(400, "Aparelho inexistente nessa sala"); return; }
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  int temp = (!err && body["temperatura"].is<int>()) ? (int)body["temperatura"] : aparelhos[idx].temperatura;
  if (temp < 18 || temp > 24) { enviarErro(400, "Temperatura inválida"); return; }
  aparelhos[idx].ligado = true;
  aparelhos[idx].temperatura = temp;
  aplicarAparelho(idx);
  enviarEstadoAparelho(idx);
}
void handleDesligarAparelhoN(int idx) {
  if (idx < 0 || idx >= qtdAparelhos) { enviarErro(400, "Aparelho inexistente nessa sala"); return; }
  aparelhos[idx].ligado = false;
  aplicarAparelho(idx);
  enviarEstadoAparelho(idx);
}
void handleTemperaturaAparelhoN(int idx) {
  if (idx < 0 || idx >= qtdAparelhos) { enviarErro(400, "Aparelho inexistente nessa sala"); return; }
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err || !body["temperatura"].is<int>()) { enviarErro(400, "JSON inválido"); return; }
  int temp = body["temperatura"];
  if (temp < 18 || temp > 24) { enviarErro(400, "Temperatura inválida"); return; }
  if (!aparelhos[idx].ligado) { enviarErro(409, "Aparelho desligado"); return; }
  aparelhos[idx].temperatura = temp;
  aplicarAparelho(idx);
  enviarEstadoAparelho(idx);
}

void handleLigarAparelho1() { handleLigarAparelhoN(0); }
void handleLigarAparelho2() { handleLigarAparelhoN(1); }
void handleDesligarAparelho1() { handleDesligarAparelhoN(0); }
void handleDesligarAparelho2() { handleDesligarAparelhoN(1); }
void handleTemperaturaAparelho1() { handleTemperaturaAparelhoN(0); }
void handleTemperaturaAparelho2() { handleTemperaturaAparelhoN(1); }

// ===================== CONFIG (quantidade de aparelhos + marca/protocolo de cada) =====================
void handleGetConfig() {
  JsonDocument doc;
  doc["qtd"] = qtdAparelhos;
  JsonArray arr = doc["protocolos"].to<JsonArray>();
  for (int i = 0; i < MAX_APARELHOS; i++) arr.add(typeToString(aparelhos[i].protocolo));
  enviarJson(200, doc);
}

void handleSetConfig() {
  if (!autenticado()) { enviarErro(401, "Não autenticado"); return; }
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err) { enviarErro(400, "JSON inválido"); return; }
  int qtd = body["qtd"] | qtdAparelhos;
  if (qtd < 1 || qtd > MAX_APARELHOS) { enviarErro(400, "Quantidade inválida"); return; }

  decode_type_t novosProtocolos[MAX_APARELHOS];
  for (int i = 0; i < MAX_APARELHOS; i++) novosProtocolos[i] = aparelhos[i].protocolo;
  if (body["protocolos"].is<JsonArray>()) {
    JsonArray arr = body["protocolos"];
    for (int i = 0; i < qtd && i < (int)arr.size(); i++) {
      String nome = arr[i].as<String>();
      decode_type_t p = strToDecodeType(nome.c_str());
      if (!IRac::isProtocolSupported(p)) { enviarErro(400, "Protocolo não suportado: " + nome); return; }
      novosProtocolos[i] = p;
    }
  }

  qtdAparelhos = qtd;
  for (int i = 0; i < MAX_APARELHOS; i++) aparelhos[i].protocolo = novosProtocolos[i];
  salvarConfigAparelhos();

  JsonDocument doc;
  doc["ok"] = true;
  doc["qtd"] = qtdAparelhos;
  enviarJson(200, doc);
}

// ===================== ADMIN (login, agenda, senha) =====================
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
    for (int i = 0; i < qtdAparelhos; i++) { aparelhos[i].ligado = true; aparelhos[i].temperatura = agenda.tempLigar; }
    aplicarTodos();
    Serial.println("[Agenda] Ligou automaticamente (todos os aparelhos da sala)");
  } else if (String(hhmm) == agenda.horaDesligar) {
    agendaUltimoDisparo = chave;
    for (int i = 0; i < qtdAparelhos; i++) aparelhos[i].ligado = false;
    aplicarTodos();
    Serial.println("[Agenda] Desligou automaticamente (todos os aparelhos da sala)");
  }
}

// ===================== PÁGINA DE TESTE DE PROTOCOLO (pelo celular, perto do ar) =====================
// Abra http://<IP do ESP>/teste no celular. "Mirar Aparelho 1/2" escolhe em
// qual dos dois aparelhos o teste age; cada "Próximo" manda "ligar 22C" no
// próximo protocolo de ar suportado; "Funcionou! Salvar" grava o atual na NVS
// naquele aparelho. shortcut: sem senha (qualquer um na rede pode trocar o
// protocolo); proteger se for pra produção.
int testeAtual = -1;
int aparelhoTeste = 0; // 0 ou 1 - qual aparelho o Monitor Serial/página de teste está mirando agora

int proximoSuportado(int de, int passo) {
  for (int n = 0; n <= kLastDecodeType + 1; n++) {
    de += passo;
    if (de > kLastDecodeType) de = 0;
    if (de < 0) de = kLastDecodeType;
    if (IRac::isProtocolSupported((decode_type_t)de)) return de;
  }
  return (int)aparelhos[aparelhoTeste].protocolo;
}

const char PAGINA_TESTE[] = R"HTML(<!doctype html><html lang=pt-br><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Teste IR</title>
<style>body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 16px}
button{width:100%;padding:18px;margin:6px 0;font-size:18px;border-radius:10px;border:1px solid #888}
#s{font-size:22px;margin:16px 0;font-weight:bold}.ok{background:#1a7f37;color:#fff;border:0}
.ap{display:flex;gap:8px}.ap button{margin:0;padding:10px;font-size:14px}
.ap button.sel{background:#444;color:#fff;border:0}</style>
<h2>Teste de protocolo IR</h2>
<div class=ap><button id=bap1 onclick="c('ap1')">Mirar Aparelho 1</button><button id=bap2 onclick="c('ap2')">Mirar Aparelho 2</button></div>
<p>Ar desligado, LED apontado pro receptor do aparelho escolhido acima. Aperte <b>Próximo</b> e espere uns 3s. Quando o ar apitar ou ligar, aperte <b>Funcionou</b>.</p>
<div id=s>Protocolo atual: ...</div>
<button onclick="c('next')">Próximo ▶</button>
<button onclick="c('again')">Repetir este</button>
<button onclick="c('prev')">◀ Anterior</button>
<button onclick="c('off')">Desligar (neste protocolo)</button>
<button class=ok onclick="c('save')">Funcionou! Salvar este</button>
<script>async function c(a){s.textContent='Enviando...';try{const r=await(await fetch('/teste/cmd?a='+a)).json();
s.textContent='Aparelho '+r.aparelho+' - p'+r.numero+' '+r.nome+(r.salvo?' (salvo, o site usa este)':'');
bap1.className=r.aparelho===1?'sel':'';bap2.className=r.aparelho===2?'sel':''}catch(e){s.textContent='Erro: '+e}}c('info')</script>)HTML";

void handleTestePagina() {
  server.send(200, "text/html; charset=utf-8", PAGINA_TESTE);
}

void handleTesteCmd() {
  String a = server.arg("a");
  if (testeAtual < 0) testeAtual = (int)aparelhos[aparelhoTeste].protocolo;

  if (a == "ap1") { aparelhoTeste = 0; testeAtual = (int)aparelhos[0].protocolo; }
  else if (a == "ap2") { aparelhoTeste = 1; testeAtual = (int)aparelhos[1].protocolo; }
  else if (a == "next") testeAtual = proximoSuportado(testeAtual, 1);
  else if (a == "prev") testeAtual = proximoSuportado(testeAtual, -1);

  if (a == "next" || a == "prev" || a == "again") enviarComandoAc(true, 22, (decode_type_t)testeAtual);
  else if (a == "off") enviarComandoAc(false, 22, (decode_type_t)testeAtual);
  else if (a == "save") {
    aparelhos[aparelhoTeste].protocolo = (decode_type_t)testeAtual;
    salvarConfigAparelhos();
    Serial.printf("Protocolo salvo no Aparelho %d: %s\n", aparelhoTeste + 1, typeToString((decode_type_t)testeAtual).c_str());
  }

  JsonDocument doc;
  doc["numero"] = testeAtual;
  doc["nome"] = typeToString((decode_type_t)testeAtual);
  doc["aparelho"] = aparelhoTeste + 1;
  doc["salvo"] = (testeAtual == (int)aparelhos[aparelhoTeste].protocolo);
  enviarJson(200, doc);
}

// ===================== MENU DE TESTE DE PROTOCOLO (via Monitor Serial) =====================
// Manda "ligar 22C" em cada protocolo de ar suportado, com 4s entre eles.
// Quando o ar apitar/ligar, o usuário aperta Enter e o protocolo atual fica
// salvo no aparelho mirado (a1/a2, padrão: Aparelho 1).
void varrerProtocolos() {
  Serial.printf("Varredura no Aparelho %d. Aponte o LED pro ar. Quando ele APITAR ou LIGAR, aperte Enter.\n", aparelhoTeste + 1);
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
        aparelhos[aparelhoTeste].protocolo = p;
        salvarConfigAparelhos();
        Serial.printf("Protocolo salvo no Aparelho %d: %s (vale pro site e depois de reiniciar)\n", aparelhoTeste + 1, typeToString(p).c_str());
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
  // "a1"/"a2" escolhe qual dos 2 aparelhos os comandos de teste abaixo (v, p<n>, 1..7) afetam.
  if (linha.equalsIgnoreCase("a1")) { aparelhoTeste = 0; Serial.println("Testando/salvando agora pro Aparelho 1."); return; }
  if (linha.equalsIgnoreCase("a2")) { aparelhoTeste = 1; Serial.println("Testando/salvando agora pro Aparelho 2."); return; }
  // "v" varre TODOS os protocolos de ar da biblioteca (pra marcas que ela não lista, ex: Elgin).
  if (linha.equalsIgnoreCase("v")) { varrerProtocolos(); return; }
  // "p<numero>" testa e salva um protocolo pelo número mostrado na varredura (ex: p18), no aparelho mirado.
  if (linha.startsWith("p") || linha.startsWith("P")) {
    decode_type_t p = (decode_type_t)linha.substring(1).toInt();
    if (!IRac::isProtocolSupported(p)) { Serial.println("Numero de protocolo invalido."); return; }
    aparelhos[aparelhoTeste].protocolo = p;
    salvarConfigAparelhos();
    Serial.printf("Protocolo salvo no Aparelho %d: %s\n", aparelhoTeste + 1, typeToString(p).c_str());
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
    aparelhos[aparelhoTeste].protocolo = protocolos[opc - 1];
    salvarConfigAparelhos();
    Serial.printf("Testando %s no Aparelho %d: ligando em 22C...\n", nomes[opc - 1], aparelhoTeste + 1);
    enviarComandoAc(true, 22, aparelhos[aparelhoTeste].protocolo);
  } else if (opc == 0) {
    Serial.printf("Enviando DESLIGAR no protocolo atual do Aparelho %d...\n", aparelhoTeste + 1);
    enviarComandoAc(false, aparelhos[aparelhoTeste].temperatura, aparelhos[aparelhoTeste].protocolo);
  } else {
    Serial.println("1=COOLIX 2=MIDEA 3=GREE 4=ELECTRA_AC 5=FUJITSU_AC 6=HITACHI_AC1 7=TCL112AC  0=desligar  v=varrer todos  p<n>=usar protocolo n  a1/a2=mirar aparelho 1 ou 2  w=trocar Wi-Fi  t=LED aceso 5s");
  }
}

// ===================== SETUP / LOOP =====================
unsigned long ultimaChecagemAgenda = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  carregarConfig();

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
  server.on((base + "/config").c_str(), HTTP_GET, handleGetConfig);
  server.on((base + "/config").c_str(), HTTP_POST, handleSetConfig);
  server.on((base + "/config").c_str(), HTTP_OPTIONS, handlePreflight);

  server.on((base + "/aparelho/1/ligar").c_str(), HTTP_POST, handleLigarAparelho1);
  server.on((base + "/aparelho/1/ligar").c_str(), HTTP_OPTIONS, handlePreflight);
  server.on((base + "/aparelho/1/desligar").c_str(), HTTP_POST, handleDesligarAparelho1);
  server.on((base + "/aparelho/1/desligar").c_str(), HTTP_OPTIONS, handlePreflight);
  server.on((base + "/aparelho/1/temperatura").c_str(), HTTP_POST, handleTemperaturaAparelho1);
  server.on((base + "/aparelho/1/temperatura").c_str(), HTTP_OPTIONS, handlePreflight);
  server.on((base + "/aparelho/2/ligar").c_str(), HTTP_POST, handleLigarAparelho2);
  server.on((base + "/aparelho/2/ligar").c_str(), HTTP_OPTIONS, handlePreflight);
  server.on((base + "/aparelho/2/desligar").c_str(), HTTP_POST, handleDesligarAparelho2);
  server.on((base + "/aparelho/2/desligar").c_str(), HTTP_OPTIONS, handlePreflight);
  server.on((base + "/aparelho/2/temperatura").c_str(), HTTP_POST, handleTemperaturaAparelho2);
  server.on((base + "/aparelho/2/temperatura").c_str(), HTTP_OPTIONS, handlePreflight);

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
  Serial.println("1=COOLIX 2=MIDEA 3=GREE 4=ELECTRA_AC 5=FUJITSU_AC 6=HITACHI_AC1 7=TCL112AC  0=desligar  v=varrer todos  p<n>=usar protocolo n  a1/a2=mirar aparelho 1 ou 2  w=trocar Wi-Fi  t=LED aceso 5s");
}

void loop() {
  server.handleClient();
  menuSerial();
  sincronizarComPonte();

  if (millis() - ultimaChecagemAgenda > 20000) {
    ultimaChecagemAgenda = millis();
    checarAgenda();
  }
}
