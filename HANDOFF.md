# Handoff — Ar-condicionados BFT (estado em 23/11/2026)

> Leia este arquivo inteiro antes de mexer em qualquer coisa. Ele resume
> tudo o que foi feito e decidido nas conversas anteriores, que NÃO estão
> disponíveis no notebook.

## O que é o projeto

Site (PWA, HTML/JS puro, sem framework e sem build) que controla os
ar-condicionados de uma escola. Cada sala tem um **ESP32** com um **LED
infravermelho** que imita o controle remoto do ar daquela sala.

**Mudou em 23/11**: o site e o ESP não falam mais direto um com o outro (isso
dava problema toda vez que a rede Wi-Fi mudava — IP, mDNS bloqueado,
hotspot de celular isolando aparelhos, etc). Agora os dois falam com um
**servidor-ponte** fixo na nuvem (Render) — ver seção própria abaixo. É a
causa raiz de quase todo o caos de rede dos dias 18-19/11, resolvida de vez.

O IR é só de ida: o site mostra o **último comando enviado**, não o estado
real do ar. Se alguém usar o controle remoto, o site não fica sabendo.

**Novo (23/11, à tarde): até 2 ar-condicionados por sala, marcas diferentes.**
Várias salas da escola têm 2 ar-condicionados de marcas diferentes. Agora dá
pra configurar isso pelo Admin (sem precisar de 2 ESPs: o mesmo LED, montado
pra alcançar os dois aparelhos, manda o sinal certo de cada marca) — ver
seção "Múltiplos aparelhos por sala" mais abaixo.

## Estrutura

```
ar-condicionado-bft (1)/               ← pasta raiz (abrir o chat AQUI)
├── CLAUDE.md                          ← instruções pro Claude (lido sozinho)
├── ABRIR-SITE.bat                     ← 2 cliques: sobe o site em http://localhost:8080
├── ferramentas/
│   ├── servidor-site.js               ← servidor do site (Node), porta 8080, aberto pra rede
│   └── ACHAR-ESP.ps1                  ← acha o IP do ESP na rede atual (só serve mais pra
│                                          achar o ESP fisicamente / debug local; não é
│                                          mais necessário pro site funcionar)
├── .claude/launch.json                ← "bft-site" (mesmo servidor, pro preview do Claude)
├── ponte-servidor/                    ← servidor-ponte (Node/Express), repo PRÓPRIO,
│   ├── servidor.js                       publicado em github.com/ArthurAngelo31/
│   └── package.json                      ar-condicionado-bft-ponte, deploy no Render
└── ar-condicionado-bft/
    ├── HANDOFF.md                     ← este arquivo
    ├── index.html, manifest.json, service-worker.js, css/, img/, icons/
    ├── js/
    │   ├── config.js    ← CFG (IP_DEFAULT = endereço da ponte, USE_MOCK...), salas, agenda
    │   ├── api.js       ← toda comunicação com a ponte (login, agenda, senha, salas)
    │   ├── admin.js     ← painel admin (salas/IPs, senha, programação)
    │   ├── home.js, sala.js, app.js, theme.js, icons.js, modal.js
    │   └── scheduler.js ← agenda SÓ do modo mock (o ESP roda a dele de verdade)
    └── firmware/
        └── esp32_laboratorio_philco/
            └── esp32_laboratorio_philco.ino   ← firmware ATUAL (precisa regravar - ver
                                                   seção "Servidor-ponte")
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

> **Histórico** — essa seção é o relato do que aconteceu no teste de 19/11
> (deu bastante trabalho com rede Wi-Fi, veja o resumo no topo do arquivo).
> As instruções de IP/mDNS/túnel aqui estão **superadas** pelo servidor-ponte
> (seção própria abaixo) - mantido só como registro do que já foi tentado.

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
- Desde 23/11 o site fala só com o **servidor-ponte** (seção abaixo), nunca
  mais direto com o ESP - então o link público controla o ar normalmente,
  sem precisar de túnel, mDNS, nem do notebook ligado.

## Servidor-ponte: endereço fixo de verdade (resolve o caos de rede dos dias 18-19/11)

**O problema que isso resolve**: o ESP32 muda de IP toda vez que troca de
rede, mDNS (`.local`) não funciona em vários hotspots de celular, e alguns
hotspots **isolam os aparelhos conectados** (ESP e notebook/celular não se
enxergam mesmo os dois tendo internet) - isso causou um dia inteiro de
instabilidade em 18-19/11 (ver histórico na seção "Plano de amanhã" acima).

**A solução**: nem o site nem o ESP falam mais um com o outro diretamente.
Os dois falam com um servidor pequeno (`ponte-servidor/servidor.js`, Node +
Express) hospedado num endereço **fixo de verdade** na nuvem:

```
Site (navegador, qualquer rede) <--HTTPS--> PONTE <--HTTPS (ESP puxa)--> ESP32 (qualquer rede)
```

O ESP **nunca recebe conexão de fora** - ele só pergunta pra ponte, a cada 3s
(`sincronizarComPonte()` no `.ino`, chamada em `loop()`), "tem comando novo?"
(`GET /dispositivo/<SALA_ID>/comando`) e informa o que aplicou
(`POST /dispositivo/<SALA_ID>/estado`). Isso funciona em **qualquer rede**,
mesmo as que bloqueiam conexão direta entre aparelhos, porque o ESP só faz
conexões de SAÍDA (como abrir uma página no navegador) - nunca abre uma porta
esperando alguém entrar.

- **Repositório da ponte**: https://github.com/ArthurAngelo31/ar-condicionado-bft-ponte
  (repo separado do site, pasta local `ponte-servidor/`).
- **URL fixa (produção)**: `https://ar-condicionado-bft-ponte.onrender.com`
  — nunca muda, não depende de rede nem de qual conta/celular tá sendo usado.
- **Hospedagem**: Render.com, plano **grátis** ($0/mês), conta GitHub do
  workspace "Bonfim's workspace" (conta diferente da `ArthurAngelo31` que
  tem o código - o Render usa o repositório público direto pela URL do
  GitHub, sem precisar dar acesso entre as contas).
- **Pra atualizar a ponte** depois de mudar `servidor.js`: dentro de
  `ponte-servidor/`, `git add -A && git commit -m "..." && git push` - o
  Render reimplanta sozinho a cada push (auto-deploy).
- **Estado fica em memória** (não salva em banco) - zera se o Render reiniciar
  o serviço (deploy novo, ou raramente após dormir). A agenda e o protocolo
  continuam salvos de verdade só no ESP (NVS), a ponte só repassa.
- **Plano grátis "dorme" depois de 15min sem nenhum pedido**, e demora até
  ~50s pra acordar no primeiro pedido depois disso. Como o ESP consulta a
  cada 3s, isso só aconteceria se o ESP ficasse desligado por mais de 15min -
  na prática, enquanto o ESP tá ligado, a ponte nunca dorme.
- **Risco de segurança**: igual ao que já existia (senha do admin pública no
  repo, `/wifi/reset`/`/teste` sem senha no ESP) - agora alcançável de
  qualquer lugar da internet, não só da rede da escola, já que é exatamente
  esse o objetivo (controle de qualquer lugar). Decisão consciente do usuário.
- **mDNS (`bft-lab.local`) e o endereço WebServer local do ESP (porta 80)
  continuam existindo no firmware**, intactos, como caminho alternativo pra
  quem estiver na mesma rede Wi-Fi do ESP (ex: debug direto, `ACHAR-ESP.ps1`)
  - só o SITE não usa mais isso por padrão.

## Múltiplos aparelhos por sala (até 2, marcas diferentes)

**Por que existe**: várias salas da escola têm 2 ar-condicionados de marcas
diferentes. Em vez de precisar de um ESP por aparelho, o mesmo LED IR (montado
de forma a alcançar os dois aparelhos fisicamente) manda o sinal de cada marca
separadamente - cada aparelho só reage ao sinal do protocolo dele, ignora o
do outro.

**Onde mexe**:
- `firmware/.../esp32_laboratorio_philco.ino`: trocou o estado único
  (`ligado`/`temperatura`/`acProtocol`) por um array `aparelhos[2]`, cada um
  com seu `ligado`/`temperatura`/`protocolo`. `qtdAparelhos` (1 ou 2) e os 2
  protocolos ficam salvos na NVS (`salvarConfigAparelhos()`).
- `ponte-servidor/servidor.js`: cada sala guarda `qtd` + `protocolos` (as
  marcas) e `desejado.aparelhos` (array), em vez de um único ligado/temp.
  `desejado.alvo` diz pro ESP se é pra mandar sinal de **todos** os aparelhos
  (automação/agenda/"sala toda") ou só de **um** específico (controle
  individual pelo app).
- `js/config.js`: lista `MARCAS_AR` com as marcas/protocolos suportados
  (nomes exatos do `decode_type_t` da IRremoteESP8266 - o firmware valida com
  `strToDecodeType()`/`IRac::isProtocolSupported()`).
- `js/admin.js` (Admin > Salas e ESPs): cada sala agora tem um seletor "1 ou 2
  aparelhos" e, se 2, um dropdown de marca pra cada um. Isso NÃO fica no
  `localStorage` (como nome/grupo/IP) - é configuração de hardware, salva na
  ponte/ESP via `POST /sala/:id/config` (autenticado), pra valer igual pra
  qualquer navegador/celular que acessar aquela sala.
- `js/sala.js`/`app.js`: ao abrir uma sala com 2 aparelhos, aparece uma tela
  de escolha ("Ar-condicionado 1 · Marca X" / "Ar-condicionado 2 · Marca Y")
  antes do controle; escolhendo um, o dial controla só aquele aparelho
  (`state.aparelho` = 1 ou 2, passado pras chamadas de API). Sala com 1
  aparelho só continua indo direto pro controle, sem a tela de escolha.

**Rotas novas** (ESP direto E através da ponte, mesmo contrato):
- `GET/POST /sala/:id/config` - `{qtd, protocolos: ["MIDEA","GREE"]}` (POST
  exige admin).
- `POST /sala/:id/aparelho/:n/ligar|desligar|temperatura` (`:n` = 1 ou 2) -
  controla só aquele aparelho, sinal IR só da marca dele.
- `/sala/:id/ligar|desligar|temperatura` (sem `/aparelho/`) continuam
  existindo e agora significam **"todos os aparelhos da sala"** - usado pela
  automação/agenda e por salas com 1 aparelho só (compatibilidade total).

**Testando o protocolo de cada aparelho fisicamente** (Monitor Serial ou
`/teste` no celular): os comandos `v`/`p<n>`/`1..7` continuam iguais, mas
agora miram um "aparelho alvo" (padrão: Aparelho 1). Comandos novos no
Serial: **`a1`/`a2`** trocam o alvo antes de testar. Na página `/teste`, os
botões "Mirar Aparelho 1/2" fazem o mesmo. Fluxo pra configurar uma sala com
2 aparelhos: desliga um dos dois ares, descobre o protocolo do outro com
`/teste` (mirando o aparelho certo), repete pro segundo, depois define as
2 marcas descobertas no Admin do site (isso é só confirmação/redundância -
o que o app manda em `/sala/:id/config` sempre prevalece quando chega).

## Firmware: comandos e rotas

Bibliotecas: `WiFiManager` (tzapu), `IRremoteESP8266` (crankyoldgit, classe
`IRac`), `ArduinoJson` v7, `WebServer.h` e `HTTPClient.h`/`WiFiClientSecure.h`
do core ESP32. Placa: ESP32 Dev Module. Sala: `SALA_ID = 6` (Laboratório).
LED IR: `IR_LED_PIN = 4`. Endereço da ponte: `PONTE_URL` (constante no topo
do `.ino`) - `sincronizarComPonte()` consulta a cada `PONTE_INTERVALO_MS`
(3s) dentro de `loop()`. Usa `WiFiClientSecure` com `setInsecure()` (não
valida o certificado HTTPS da ponte - simplifica bastante no ESP32, mesmo
nível de "confiança" que já existia com `/wifi/reset` e `/teste` sem senha).

Rotas HTTP (CORS liberado), SALA TODA - "automação"/compatibilidade com salas
de 1 aparelho:
- `GET /sala/6/estado`, `POST /sala/6/ligar` `{temperatura}`,
  `POST /sala/6/desligar`, `POST /sala/6/temperatura` `{temperatura}` (18–24)

Rotas de 1 aparelho específico (`:n` = 1 ou 2) e de config - ver seção
"Múltiplos aparelhos por sala" acima:
- `POST /sala/6/aparelho/:n/ligar|desligar|temperatura`
- `GET/POST /sala/6/config` `{qtd, protocolos}`

Admin:
- `POST /admin/login` `{senha}` → `{token}` (token em memória: some se o ESP
  reiniciar → sair e entrar de novo no admin)
- `GET/POST /admin/agenda` (header `X-Admin-Token`), salva na NVS
- `POST /admin/senha` `{atual, nova}` (token; 403 se a atual estiver errada;
  4–32 caracteres; salva na NVS)
- `GET /teste` página de teste de protocolo pro celular; `GET /teste/cmd?a=`
  `next|prev|again|off|save|info|ap1|ap2`
- `GET /wifi/reset` apaga o Wi-Fi e reinicia no portal

Monitor Serial (115200):
- `a1`/`a2`: escolhe qual aparelho (1 ou 2) os comandos abaixo afetam
  (padrão: Aparelho 1)
- `1`..`7`: testa COOLIX, MIDEA, GREE, ELECTRA_AC, FUJITSU_AC, HITACHI_AC1,
  TCL112AC (e salva no aparelho mirado); `0`: desliga no protocolo atual
- `v`: varre todos os protocolos de ar (4s cada); Enter salva o atual
- `pN`: testa e salva o protocolo número N (ex: `p34` = MIDEA)
- `t`: LED aceso 5s direto (ver na câmera do celular)
- `w`: apaga o Wi-Fi salvo e abre o portal `BFT-ESP-Setup`

Fica salvo na placa (NVS, sobrevive a desligar): Wi-Fi, quantidade de
aparelhos (`qtd`) e protocolo de cada um (`proto`/`proto2`), senha do admin
(`senha`), agenda.
Senha de fábrica: `ADMIN_SENHA = "admin123"`. Esqueceu a senha: Arduino IDE →
Tools → Erase All Flash Before Sketch Upload → Enabled, e gravar de novo
(apaga também Wi-Fi, protocolos e agenda).

Os comandos `1`..`7` disparam um "ligar" no aparelho mirado; a lista é a do
código, em `menuSerial()`.

## Site: o que mudou nas últimas sessões

- **Desde 23/11, `getIp()`/`getIpSala()` em `config.js` sempre retornam o
  endereço da ponte** (`CFG.IP_DEFAULT`), ignorando qualquer coisa salva no
  `localStorage` ou campo de IP por sala. A ponte diferencia as salas pelo
  `:id` na URL (cada ESP tem seu próprio `SALA_ID` gravado no firmware), não
  por IP - então não existe mais "IP por sala" de verdade, isso virou um
  campo legado na tela (não quebra nada, só não faz mais efeito).
- Login/agenda/senha agora falam só com a ponte (não mais com "todos os ESPs
  distintos das salas" como antes) - token fica em
  `localStorage['bft_admin_token']`.
- A agenda é lida/escrita através da ponte, que repassa pro ESP (ele que
  salva de verdade na NVS).
- `service-worker.js` usa `CACHE = 'bft-v24'`. Ao mudar JS/CSS, subir a
  versão, senão os celulares continuam com a cópia velha.
- **GitHub Pages cacheia cada arquivo JS/CSS por até 10 minutos no CDN**
  (`Cache-Control: max-age=600`), mesmo depois do deploy já estar "built".
  Por isso `index.html` carrega cada `.js`/`.css` com `?v=24` no final —
  isso muda a URL, então o CDN busca a versão nova na hora. **Toda vez que
  mudar JS/CSS**: sobe o número em `?v=` de TODOS os `<script>`/`<link>` no
  `index.html` (mesmo número do `CACHE` acima, pra não esquecer nenhum).
- Campo "IP ou endereço padrão do ESP32" (Admin → Geral) aceita hostname
  também (`enderecoValido()` em `admin.js`) - mantido por compatibilidade,
  mas não é mais usado de verdade já que o endereço é sempre fixo agora.

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

- **O firmware do ESP precisa ser regravado** pra ter a sincronização com a
  ponte (`sincronizarComPonte()`) E o suporte a 2 aparelhos por sala - sem
  isso, o ESP continua funcionando só na rede local, com 1 aparelho só. Lembrete
  de sempre: fechar a aba do sketch no Arduino IDE sem salvar e abrir de novo
  antes de gravar.
  - **"Sketch too big" / "text section exceeds available space"**: o
    HTTPS da ponte (`WiFiClientSecure`) deixou o firmware maior que a
    partição padrão (1.2MB). **Antes de gravar**, no Arduino IDE vai em
    **Tools → Partition Scheme → "Huge APP (3MB No OTA/1MB SPIFFS)"** (não
    precisamos de OTA nem de SPIFFS nesse projeto).
- `/wifi/reset` e `/teste` **não têm senha**: agora alcançável de qualquer
  lugar da internet através da ponte, não só da mesma rede - risco aceito
  conscientemente pelo usuário (ver seção da ponte). Proteger antes de usar
  de verdade na escola com mais salas.
- A **senha de admin da ponte** é independente da senha de admin salva no
  ESP (`adminSenha` no firmware) - são dois sistemas de login separados
  agora. Trocar a senha pelo site (via ponte) não muda a senha de acesso
  local direto ao ESP, e vice-versa. Hoje as duas começam como `admin123`.
- Se você editar as salas no admin, sair sem salvar e voltar, o rascunho
  antigo continua na tela (variável `salasDraft` em `admin.js`).
- Um firmware/ESP controla UMA sala (`SALA_ID = 6`), mas essa sala pode ter
  até 2 aparelhos (ver "Múltiplos aparelhos por sala"). As outras 5 salas do
  site aparecem "sem conexão" até terem ESP.
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
