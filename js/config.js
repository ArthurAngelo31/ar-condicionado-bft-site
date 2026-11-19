/* Configurações globais do app */

// Quando o site é aberto por HTTPS (link público do GitHub Pages), o navegador
// bloqueia pedido pra HTTP local (mixed content) - por isso usamos o túnel
// Cloudflare (HTTPS de verdade) nesse caso. No site local (HTTP) usamos o
// endereço mDNS direto, mais rápido e sem depender de internet.
const USANDO_HTTPS = location.protocol === 'https:';

const CFG = {
  // Local (http://IP-NOTEBOOK:8080): fala direto com o ESP via mDNS.
  // Público (https://...github.io): fala através do túnel Cloudflare.
  // Se o túnel cair/mudar de endereço, atualiza só essa linha.
  // mDNS (bft-lab.local) não funciona em alguns hotspots de celular (bloqueiam
  // multicast mesmo sem isolar o resto). Por isso IP numérico direto aqui -
  // se o ESP reconectar e mudar de IP, atualiza só essa linha.
  IP_DEFAULT: USANDO_HTTPS ? 'discounts-designation-luck-displaying.trycloudflare.com' : '10.99.223.144',
  PROTOCOL: USANDO_HTTPS ? 'https' : 'http',
  USE_MOCK: false,  // false = fala de verdade com os ESPs configurados em cada sala
  // Pelo túnel (internet) cada pedido tem OPTIONS (preflight) + a chamada real,
  // e cada um mediu ~2-8s de ida/volta nos testes - 4s estourava sempre. Local
  // o notebook que roda o site tá com RAM muito baixa (ver HANDOFF), o que
  // deixa até pedidos na rede local mais lentos que o normal - subi também.
  TIMEOUT: USANDO_HTTPS ? 15000 : 10000,
  MOCK_PWD: 'admin123',
  AUTO_REFRESH_MS: 10000
};

const SK = {
  IP: 'bft_esp_ip',
  TOKEN: 'bft_admin_token',
  SALAS: 'bft_salas',
  AGENDA: 'bft_agenda',
  AGENDA_ULTIMO: 'bft_agenda_ultimo',
  THEME: 'bft_theme'
};

const SALAS_DEFAULT = [
  { id: 1, nome: '1A / 6C', grupo: 'PRIMEIRO ANO' },
  { id: 2, nome: '1C / 6D', grupo: 'PRIMEIRO ANO' },
  { id: 3, nome: '2B / 7C', grupo: 'SEGUNDO ANO' },
  { id: 4, nome: '2A / 7D', grupo: 'SEGUNDO ANO' },
  { id: 5, nome: '3B',      grupo: 'TERCEIRO ANO' },
  { id: 6, nome: 'Laboratório', grupo: 'LABORATÓRIO' }
];

const AGENDA_DEFAULT = {
  ativa: false,
  horaLigar: '07:00',
  horaDesligar: '18:00',
  diasAtivos: [true, true, true, true, true],
  tempLigar: 22
};

const state = {
  view: 'home',
  salaId: null,
  sala: null,
  op: false,
  adminTab: 'geral',
  salasCache: null,
  autoRefreshTimer: null
};

const viewHistory = ['home'];

const $ = id => document.getElementById(id);
const sleep = ms => new Promise(r => setTimeout(r, ms));

// No link público (HTTPS) só existe UM endereço possível (o túnel atual),
// então ignoramos qualquer IP salvo antes no aparelho - sempre usa o mais
// recente do código. Isso evita o problema de ficar preso num túnel antigo
// depois que ele cai e um novo é criado. No site local, mantém o que foi
// salvo (pode fazer sentido ter IP customizado por sala na rede local).
const getIp = () => USANDO_HTTPS ? CFG.IP_DEFAULT : (localStorage.getItem(SK.IP) || CFG.IP_DEFAULT);

// Lista de salas configurável: cada sala pode ter seu próprio ESP (campo ip),
// ou deixar em branco para usar o IP padrão (salas que compartilham o mesmo ESP).
const getSalas = () => {
  try {
    const salvas = JSON.parse(localStorage.getItem(SK.SALAS));
    if (Array.isArray(salvas) && salvas.length) return salvas;
  } catch {}
  return SALAS_DEFAULT.map(s => ({ ...s, ip: '' }));
};
const setSalas = arr => localStorage.setItem(SK.SALAS, JSON.stringify(arr));

const getAgenda = () => {
  try { return { ...AGENDA_DEFAULT, ...(JSON.parse(localStorage.getItem(SK.AGENDA)) || {}) }; }
  catch { return { ...AGENDA_DEFAULT }; }
};
const setAgendaStorage = ag => localStorage.setItem(SK.AGENDA, JSON.stringify(ag));

const getGrupoDaSala = id => getSalas().find(s => s.id === id)?.grupo || '';
// No link público, o túnel só aponta pra UM destino (bft-lab.local) - não
// faz sentido usar IP por sala salvo, por isso ignora também aqui.
const getIpSala = id => {
  if (USANDO_HTTPS) return getIp();
  const sala = getSalas().find(s => s.id === id);
  return (sala && sala.ip && sala.ip.trim()) || getIp();
};

const vibrate = ms => { if (navigator.vibrate) navigator.vibrate(ms); };
