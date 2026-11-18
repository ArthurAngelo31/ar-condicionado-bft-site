/* Configurações globais do app */

const CFG = {
  IP_DEFAULT: 'bft-lab.local', // endereço fixo do ESP (mDNS) - não muda trocando de rede, não precisa mais atualizar o IP aqui
  PROTOCOL: 'http', // o ESP32 serve HTTP simples, não HTTPS
  USE_MOCK: false,  // false = fala de verdade com os ESPs configurados em cada sala
  TIMEOUT: 4000,
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

const getIp = () => localStorage.getItem(SK.IP) || CFG.IP_DEFAULT;

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
const getIpSala = id => {
  const sala = getSalas().find(s => s.id === id);
  return (sala && sala.ip && sala.ip.trim()) || getIp();
};

const vibrate = ms => { if (navigator.vibrate) navigator.vibrate(ms); };
