# Handoff — Ar-condicionados BFT (estado em 18/11/2026, fim do dia)

> Leia este arquivo inteiro antes de mexer em qualquer coisa. Ele resume
> tudo o que foi feito e decidido nas conversas anteriores, que NÃO estão
> disponíveis no notebook.

## O que é o projeto

Site (PWA, HTML/JS puro, sem framework e sem build) que controla os
ar-condicionados de uma escola. Cada sala tem um **ESP32** com um **LED
infravermelho** que imita o controle remoto do ar daquela sala. O site fala
com cada ESP por HTTP simples, na mesma rede Wi-Fi.

O IR é só de ida: o site mostra o **último comando enviado**, não o estado
real do ar. Se alguém usar o controle remoto, o site não fica sabendo.

## Estrutura

```
ar-condicionado-bft (1)/               ← pasta raiz (abrir o chat AQUI)
├── CLAUDE.md                          ← instruções pro Claude (lido sozinho)
├── ABRIR-SITE.bat                     ← 2 cliques: sobe o site em http://localhost:8080
├── ferramentas/
│   ├── servidor-site.js               ← servidor do site (Node), porta 8080, aberto pra rede
│   └── ACHAR-ESP.ps1                  ← acha o IP do ESP na rede atual
├── .claude/launch.json                ← "bft-site" (mesmo servidor, pro preview do Claude)
└── ar-condicionado-bft/
    ├── HANDOFF.md                     ← este arquivo
    ├── index.html, manifest.json, service-worker.js, css/, img/, icons/
    ├── js/
    │   ├── config.js    ← CFG (IP_DEFAULT, USE_MOCK...), lista de salas, agenda
    │   ├── api.js       ← toda comunicação com os ESPs (login, agenda, senha, salas)
    │   ├── admin.js     ← painel admin (salas/IPs, senha, programação)
    │   ├── home.js, sala.js, app.js, theme.js, icons.js, modal.js
    │   └── scheduler.js ← agenda SÓ do modo mock (o ESP roda a dele de verdade)
    └── firmware/
        └── esp32_laboratorio_philco/
            └── esp32_laboratorio_philco.ino   ← firmware ATUAL (já gravado na placa)
```

O Arduino IDE exige que o `.ino` fique numa pasta com o mesmo nome; por isso
a subpasta `esp32_laboratorio_philco/`.

## Estado atual (tudo confirmado no hardware em 18/11/2026)

- **Firmware mais recente está gravado na placa** (conferido pela rede: a
  página `/teste` responde e o protocolo salvo é `MIDEA`).
- **Site ↔ ESP funcionando**: o usuário confirmou que a comunicação funcionou.
- **Transistor funcionando**: LED acende pelo GPIO4 (comando `t`).
- **Ainda NÃO confirmado**: o ar-condicionado obedecer ao IR. Nenhum ar
  ligou pelo ESP até agora (Philco nunca foi confirmado; Midea e Elgin não
  foram testados ainda).

## Plano de amanhã (19/11/2026): teste na escola

Ar-condicionado: **Elgin, modelo inverter**. O usuário **não tem como
capturar os códigos do controle** (sem receptor IR). A biblioteca
IRremoteESP8266 **não lista Elgin** em nenhum modelo nem issue (checado em
18/11), então o protocolo só se descobre **testando um por um** pela página
`/teste`.

Passo a passo esperado:
1. **Wi-Fi**: na escola o ESP não acha a rede salva (`brisa-1345041`, da
   casa do usuário) e **abre sozinho o portal** `BFT-ESP-Setup` (senha
   `bft12345`). Conectar o celular/notebook nela, abrir `http://192.168.4.1`
   e escolher a Wi-Fi da escola.
   - Se a Wi-Fi da escola tiver página de login (portal cativo) ou bloquear
     aparelhos de se verem (isolamento de clientes), o ESP não vai
     funcionar nela. Alternativa: **hotspot do notebook** (Windows →
     Configurações → Rede → Hotspot móvel) e ligar o ESP e o celular nele.
2. **Achar o IP do ESP**: rodar `ferramentas/ACHAR-ESP.ps1` (botão direito →
   Executar com o PowerShell), ou ver no Monitor Serial (115200) a linha
   "Conectado! IP deste ESP:".
3. **Descobrir o protocolo do Elgin**: com o ar **desligado** e o LED
   apontado pro receptor (30–50 cm), abrir no celular `http://IP-DO-ESP/teste`,
   apertar **Próximo** e esperar ~3s; repetir até o Elgin apitar/ligar;
   apertar **Funcionou! Salvar este**. Testar **Desligar**. O protocolo
   fica salvo na placa e passa a valer pro site.
   - Se nenhum funcionar: a biblioteca não conhece o protocolo do Elgin.
     Única saída é comprar um receptor IR (ex: VS1838B) e capturar o sinal
     do controle original.
4. **Abrir o site**: dois cliques em `ABRIR-SITE.bat` (precisa de Node.js
   instalado; sem Node ele abre o `index.html` direto, o que pode não
   conseguir falar com o ESP dependendo do navegador). Admin → senha
   `admin123` (se não foi trocada) → Geral → Salas e ESPs → colocar o IP
   do ESP na sala **Laboratório** → Salvar.
   - Se o login falhar com "Nenhum ESP respondeu", o site está procurando o
     ESP no IP errado. `CFG.IP_DEFAULT` em `js/config.js` está
     `192.168.0.9` (IP da casa do usuário). Trocar pelo IP novo e
     recarregar com Ctrl+Shift+R duas vezes (o service worker guarda cópia).
5. Testar pelo site: ligar, desligar, temperatura. Conferir olhando o ar.
6. Ver o **alcance**: de quantos metros o Elgin ainda obedece.

## Site publicado (GitHub Pages)

Repositório: https://github.com/ArthurAngelo31/ar-condicionado-bft-site
Link público: https://arthurangelo31.github.io/ar-condicionado-bft-site/

- Repo **público**, criado a pedido do usuário mesmo sabendo que o firmware
  tem as senhas em texto puro (`ADMIN_SENHA`, `WIFI_PORTAL_SENHA`) — decisão
  consciente dele, não esquecimento.
- Pra **atualizar o site publicado** depois de mudar algo: dentro da pasta
  `ar-condicionado-bft/`, `git add -A && git commit -m "..." && git push`.
- **Controlar o ar pelo link público só funciona quando quem acessa está na
  MESMA rede Wi-Fi do ESP** (o fetch é HTTP simples pro IP/hostname local).
  Fora dessa rede, o site abre normal mas todas as salas aparecem "sem
  conexão". Navegadores também podem bloquear o pedido HTTPS→HTTP local
  (mixed content / Private Network Access) dependendo do navegador/versão —
  se isso acontecer, só dá pra confirmar testando de verdade.

## mDNS: endereço fixo do ESP (bft-lab.local)

Pra não precisar atualizar o IP toda vez que o ESP troca de rede, o firmware
agora registra o hostname **`bft-lab.local`** via `ESPmDNS` (`setup()`, logo
depois de conectar no Wi-Fi). `CFG.IP_DEFAULT` em `js/config.js` já usa esse
hostname em vez de um IP numérico, e o campo "IP padrão do ESP32" no Admin
(`js/admin.js`, `enderecoValido()`) agora aceita IP **ou** hostname.

- **Limitação conhecida**: `.local` (mDNS) funciona bem em iOS/Mac, mas em
  Android depende da marca/versão (alguns não resolvem `.local` no navegador).
  Se `bft-lab.local` não funcionar num aparelho específico, usa o IP numérico
  de sempre (via `ferramentas/ACHAR-ESP.ps1` ou lendo "Conectado! IP deste
  ESP:" no Serial) no campo do Admin naquele navegador — é por navegador/
  aparelho, não precisa mexer no código.
- Isso **não resolve** Wi-Fi com isolamento de clientes (escola pode
  bloquear ESP↔celular mesmo com endereço certo — ver seção do teste de
  19/11).
- Se precisar trocar o hostname, é a constante `MDNS_HOST` no `.ino`.

## Firmware: comandos e rotas

Bibliotecas: `WiFiManager` (tzapu), `IRremoteESP8266` (crankyoldgit, classe
`IRac`), `ArduinoJson` v7, `WebServer.h` do core ESP32. Placa: ESP32 Dev
Module. Sala: `SALA_ID = 6` (Laboratório). LED IR: `IR_LED_PIN = 4`.

Rotas HTTP (CORS liberado):
- `GET /sala/6/estado`, `POST /sala/6/ligar` `{temperatura}`,
  `POST /sala/6/desligar`, `POST /sala/6/temperatura` `{temperatura}` (18–24)
- `POST /admin/login` `{senha}` → `{token}` (token em memória: some se o ESP
  reiniciar → sair e entrar de novo no admin)
- `GET/POST /admin/agenda` (header `X-Admin-Token`), salva na NVS
- `POST /admin/senha` `{atual, nova}` (token; 403 se a atual estiver errada;
  4–32 caracteres; salva na NVS)
- `GET /teste` página de teste de protocolo pro celular; `GET /teste/cmd?a=`
  `next|prev|again|off|save|info`
- `GET /wifi/reset` apaga o Wi-Fi e reinicia no portal

Monitor Serial (115200):
- `1`..`7`: testa COOLIX, MIDEA, GREE, ELECTRA_AC, FUJITSU_AC, HITACHI_AC1,
  TCL112AC (e salva); `0`: desliga no protocolo atual
- `v`: varre todos os protocolos de ar (4s cada); Enter salva o atual
- `pN`: testa e salva o protocolo número N (ex: `p34` = MIDEA)
- `t`: LED aceso 5s direto (ver na câmera do celular)
- `w`: apaga o Wi-Fi salvo e abre o portal `BFT-ESP-Setup`

Fica salvo na placa (NVS, sobrevive a desligar): Wi-Fi, protocolo
(`proto`), senha do admin (`senha`), agenda.
Senha de fábrica: `ADMIN_SENHA = "admin123"`. Esqueceu a senha: Arduino IDE →
Tools → Erase All Flash Before Sketch Upload → Enabled, e gravar de novo
(apaga também Wi-Fi, protocolo e agenda).

Os comandos `1`..`7` disparam um "ligar"; a lista é a do código, em
`menuSerial()`.

## Site: o que mudou nas últimas sessões

- Cada sala tem seu IP de ESP (Admin → Geral → Salas e ESPs); em branco =
  `CFG.IP_DEFAULT`.
- Login/agenda/senha falam com **todos os ESPs distintos** das salas; tokens
  ficam em `localStorage['bft_admin_token']` como `{ ip: token }`. Entra se
  pelo menos um ESP aceitar; os toasts listam quais falharam.
- A agenda é lida do ESP ao entrar no admin (não só do navegador).
- Cada navegador (PC, celular, notebook) guarda sua própria lista de salas
  e IPs no `localStorage`: configurar em cada um.
- `service-worker.js` usa `CACHE = 'bft-v12'`. Ao mudar JS/CSS, subir a versão,
  senão os celulares continuam com a cópia velha.
- Campo "IP padrão do ESP32" (Admin → Geral) e o campo de IP por sala agora
  aceitam **hostname** também, não só IP numérico (pra caber o `bft-lab.local`
  do mDNS — ver seção acima). Validação em `enderecoValido()` (`admin.js`).
- **Importante**: o IP/endereço salvo fica no `localStorage` de **cada
  navegador** (`SK.IP`). Trocar `CFG.IP_DEFAULT` no código não muda o que já
  foi salvo antes em cada aparelho — é preciso entrar no Admin → Geral em
  cada um (notebook, celular) e salvar `bft-lab.local` (ou o IP novo) lá,
  pelo menos uma vez.

## Hardware

- ESP32 Dev Module, USB-serial CH9102 (era COM10 no PC; no notebook a porta
  pode ser outra).
- Ligação atual (funcionando, sem o pull-down de 10k):
  ```
  3V3   --[150Ω]-- (+) LED IR (-) -- COLETOR
  GPIO4 --[1kΩ]--- BASE
  EMISSOR -- GND
  ```
  Transistor NPN; pinos identificados pelo usuário com multímetro (modo
  diodo). LED: perna maior = +.
- Upload às vezes exige modo bootloader manual: segurar BOOT, tocar EN/RST,
  soltar BOOT quando a barra de progresso começar.
- O ESP pode ser alimentado por qualquer USB (notebook, carregador): ele
  conecta sozinho no Wi-Fi salvo e o servidor sobe sozinho.

## Problemas conhecidos (não resolvidos)

- `/wifi/reset` e `/teste` **não têm senha**: qualquer um na mesma rede pode
  resetar o Wi-Fi ou trocar o protocolo. Ok pra teste, proteger antes de
  usar de verdade na escola.
- Se você editar as salas no admin, sair sem salvar e voltar, o rascunho
  antigo continua na tela (variável `salasDraft` em `admin.js`).
- O firmware só controla UMA sala (`SALA_ID = 6`). As outras 5 salas do site
  aparecem "sem conexão" até terem ESP.
- O IP do ESP pode mudar ao reiniciar (DHCP).
- No PC do usuário o disco C: estava 100% cheio (causa não achada; suspeita
  de OneDrive). A compilação do Arduino foi apontada pro D: em
  `C:\Users\Arthur Angelo\.arduinoIDE\arduino-cli.yaml` (`build_cache.path`).
  Isso é só do PC, não do notebook.

## Histórico de hardware (pra não repetir)

1. Transistor 2SC945/C945 (NPN): a primeira fiação tinha Base e Emissor
   trocados, o que prendia a Base no GND. Mesmo corrigido não funcionou, e o
   LED ficou um tempo direto no GPIO (150Ω, alcance curto).
2. Em 18/11 o usuário identificou B/C/E com o multímetro e montou a ligação
   simples acima (4 fios, sem dividir o 3V3): funcionou na primeira.
3. O pino do LED mudou de GPIO25 pra GPIO4 em 18/11 a pedido do usuário.
