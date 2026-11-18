# Ar-condicionados BFT — Versão Premium

PWA para controle central dos ar-condicionados da escola BFT.

## Novidades desta versão

- **Design premium** com tipografia Fraunces + Manrope
- **Modo escuro** automático (sistema) e manual (botão no topo)
- **Estatísticas na home** (salas ligadas, total, status da programação)
- **Botão "Desligar tudo"** aparece flutuante quando alguma sala está ligada
- **Modal de confirmação** para ações destrutivas
- **Feedback tátil** (vibração) em ações importantes
- **Auto-refresh** a cada 10s na home
- **Botão voltar do celular** funciona (Android)
- **Acessibilidade** melhorada (foco visível, ARIA, contraste)
- **Temperatura visível** no card da sala ligada (na home)

## Estrutura

```
ar-condicionado-bft/
├── index.html
├── manifest.json
├── service-worker.js
├── css/style.css
├── js/
│   ├── config.js       ← configurações + estado
│   ├── theme.js        ← tema claro/escuro
│   ├── api.js          ← comunicação ESP (mock+real)
│   ├── icons.js        ← ícones + toast
│   ├── modal.js        ← modal de confirmação
│   ├── home.js         ← tela inicial
│   ├── sala.js         ← controle da sala
│   ├── admin.js        ← painel admin
│   ├── scheduler.js    ← agenda (mock)
│   └── app.js          ← bootstrap
├── img/logo.png
└── icons/
```

## Testar

```bash
cd ar-condicionado-bft
python3 -m http.server 8080
```

Abre `http://localhost:8080`. Senha admin (mock): `admin123`.

## Configuração

Em `js/config.js`:

- `IP_DEFAULT`: IP do ESP32
- `PROTOCOL`: `'https'` ou `'http'`
- `USE_MOCK`: `true` durante desenvolvimento, `false` com ESP real
- `AUTO_REFRESH_MS`: intervalo de atualização automática (ms)
