/* Camada de API - só confirma estado quando ESP responde corretamente */

const mockState = { salas: [] };

// Sincroniza a lista mock com as salas configuradas (adicionadas/removidas/renomeadas),
// preservando o estado (ligado/temperatura) das que já existiam.
function syncMockSalas() {
  const defs = getSalas();
  const existentes = new Map(mockState.salas.map(s => [s.id, s]));
  mockState.salas = defs.map(d => {
    const prev = existentes.get(d.id);
    return { id: d.id, nome: d.nome, ligado: prev ? prev.ligado : false, temperatura: prev ? prev.temperatura : 22 };
  });
}

async function fetchTO(url, opts = {}) {
  const c = new AbortController();
  const t = setTimeout(() => c.abort(), CFG.TIMEOUT);
  try {
    const r = await fetch(url, { ...opts, signal: c.signal });
    clearTimeout(t);
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return await r.json();
  } catch (e) {
    clearTimeout(t);
    if (e.name === 'AbortError') throw new Error('Sem resposta do ESP32');
    if (e.message.includes('Failed to fetch') || e.message.includes('NetworkError')) {
      throw new Error('ESP32 não encontrado na rede');
    }
    throw new Error(e.message || 'Erro de comunicação');
  }
}

// Com `id`, resolve o IP específico daquela sala (ou o padrão, se não configurado).
const espUrl = (path, id) => `${CFG.PROTOCOL}://${id != null ? getIpSala(id) : getIp()}${path}`;

// Cada ESP roda a própria agenda e emite o próprio token, então login/agenda
// falam com TODOS os ESPs distintos das salas. Tokens ficam num mapa { ip: token }.
const LOGIN_EXPIROU = 'login expirou nesse ESP (ele reiniciou?), saia e entre de novo';
const espsDistintos =() => [...new Set(getSalas().map(s => getIpSala(s.id)))];
const getTokens = () => {
  try { const t = JSON.parse(localStorage.getItem(SK.TOKEN)); return t && typeof t === 'object' ? t : {}; }
  catch { return {}; } // valor antigo (token único em texto) = não logado
};

// Roda `fn(ip)` em todos os ESPs em paralelo; devolve quem respondeu e quem falhou.
async function emTodosEsps(fn) {
  const ips = espsDistintos();
  const res = await Promise.allSettled(ips.map(fn));
  const ok = {}, erros = [];
  res.forEach((r, i) => r.status === 'fulfilled' ? ok[ips[i]] = r.value : erros.push(`${ips[i]}: ${r.reason.message}`));
  return { ok, erros, total: ips.length };
}

function validarResposta(resp, campos = ['id', 'ligado', 'temperatura']) {
  if (!resp || typeof resp !== 'object') throw new Error('Resposta inválida do ESP');
  for (const campo of campos) {
    if (!(campo in resp)) throw new Error(`Resposta incompleta do ESP (falta: ${campo})`);
  }
  return resp;
}

const API = {
  // Cada sala pode estar em um ESP diferente: consulta cada uma individualmente,
  // então uma falha isolada não derruba a tela inteira (sala fica "indefinido").
  async listar() {
    syncMockSalas();
    if (CFG.USE_MOCK) { await sleep(180); return JSON.parse(JSON.stringify(mockState)); }

    const salas = getSalas();
    const resultados = await Promise.all(salas.map(async s => {
      try {
        const resp = await fetchTO(espUrl(`/sala/${s.id}/estado`, s.id));
        validarResposta(resp);
        return { id: s.id, nome: s.nome, ligado: !!resp.ligado, temperatura: resp.temperatura };
      } catch {
        return { id: s.id, nome: s.nome, ligado: false, temperatura: 22, indefinido: true };
      }
    }));
    return { salas: resultados };
  },

  async obter(id) {
    syncMockSalas();
    if (CFG.USE_MOCK) {
      await sleep(140);
      const s = mockState.salas.find(x => x.id === id);
      if (!s) throw new Error('Sala não encontrada');
      return { ...s };
    }
    const resp = await fetchTO(espUrl(`/sala/${id}/estado`, id));
    return validarResposta(resp);
  },

  async ligar(id, temperatura = 22) {
    if (CFG.USE_MOCK) {
      await sleep(240);
      const s = mockState.salas.find(x => x.id === id);
      if (!s) throw new Error('Sala não encontrada');
      s.ligado = true; s.temperatura = temperatura;
      return { ...s };
    }
    const resp = await fetchTO(espUrl(`/sala/${id}/ligar`, id), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ temperatura })
    });
    validarResposta(resp);
    if (resp.ligado !== true) throw new Error('ESP não confirmou o comando');
    if (resp.id !== id) throw new Error('ESP retornou sala incorreta');
    return resp;
  },

  async desligar(id) {
    if (CFG.USE_MOCK) {
      await sleep(240);
      const s = mockState.salas.find(x => x.id === id);
      if (!s) throw new Error('Sala não encontrada');
      s.ligado = false;
      return { ...s };
    }
    const resp = await fetchTO(espUrl(`/sala/${id}/desligar`, id), { method: 'POST' });
    validarResposta(resp);
    if (resp.ligado !== false) throw new Error('ESP não confirmou o comando');
    if (resp.id !== id) throw new Error('ESP retornou sala incorreta');
    return resp;
  },

  async setTemp(id, t) {
    if (![18, 20, 22, 24].includes(t)) throw new Error('Temperatura inválida');
    if (CFG.USE_MOCK) {
      await sleep(200);
      const s = mockState.salas.find(x => x.id === id);
      if (!s) throw new Error('Sala não encontrada');
      if (!s.ligado) throw new Error('Sala desligada');
      s.temperatura = t;
      return { ...s };
    }
    const resp = await fetchTO(espUrl(`/sala/${id}/temperatura`, id), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ temperatura: t })
    });
    validarResposta(resp);
    if (resp.temperatura !== t) throw new Error('ESP não confirmou a nova temperatura');
    return resp;
  },

  async desligarTodas() {
    if (CFG.USE_MOCK) {
      await sleep(400);
      mockState.salas.forEach(s => s.ligado = false);
      return { ok: true, desligadas: mockState.salas.length };
    }
    const salas = getSalas();
    let confirmadas = 0;
    const erros = [];
    for (const s of salas) {
      try { await API.desligar(s.id); confirmadas++; }
      catch (e) { erros.push(`${s.nome}: ${e.message}`); }
    }
    if (confirmadas === 0) throw new Error('Nenhuma sala respondeu. ' + (erros[0] || ''));
    return { ok: true, desligadas: confirmadas, total: salas.length, erros };
  },

  async login(senha) {
    if (CFG.USE_MOCK) {
      await sleep(360);
      if (senha === CFG.MOCK_PWD) {
        localStorage.setItem(SK.TOKEN, JSON.stringify({ mock: 'mock-' + Date.now() }));
        return { erros: [] };
      }
      throw new Error('Senha incorreta');
    }
    const { ok, erros } = await emTodosEsps(async ip => {
      const r = await fetchTO(`${CFG.PROTOCOL}://${ip}/admin/login`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ senha })
      });
      if (!r.token) throw new Error('ESP não retornou token');
      return r.token;
    });
    if (!Object.keys(ok).length) {
      throw new Error(erros.some(e => e.includes('401')) ? 'Senha incorreta' : 'Nenhum ESP respondeu. ' + (erros[0] || ''));
    }
    localStorage.setItem(SK.TOKEN, JSON.stringify(ok));
    // Puxa a agenda real de um ESP pra tela não mostrar um valor velho do navegador.
    try { setAgendaStorage(await API.getAgenda()); } catch {}
    return { erros };
  },

  async getAgenda() {
    if (CFG.USE_MOCK) { await sleep(120); return getAgenda(); }
    const tokens = getTokens();
    for (const ip of Object.keys(tokens)) {
      try {
        return await fetchTO(`${CFG.PROTOCOL}://${ip}/admin/agenda`, { headers: { 'X-Admin-Token': tokens[ip] } });
      } catch {}
    }
    throw new Error('Nenhum ESP devolveu a agenda');
  },

  // Manda a mesma agenda pra todos os ESPs; cada um liga/desliga as próprias salas.
  async setAgenda(ag) {
    if (CFG.USE_MOCK) { await sleep(260); setAgendaStorage(ag); return { erros: [] }; }
    const tokens = getTokens();
    const { ok, erros, total } = await emTodosEsps(async ip => {
      if (!tokens[ip]) throw new Error('sem login nesse ESP (entre de novo)');
      const resp = await fetchTO(`${CFG.PROTOCOL}://${ip}/admin/agenda`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-Admin-Token': tokens[ip] },
        body: JSON.stringify(ag)
      }).catch(e => { throw new Error(e.message.includes('401') ? LOGIN_EXPIROU : e.message); });
      if (!resp.ok) throw new Error('ESP não confirmou salvamento da agenda');
    });
    const salvos = Object.keys(ok).length;
    if (!salvos) throw new Error('Nenhum ESP salvou a agenda. ' + (erros[0] || ''));
    setAgendaStorage(ag);
    return { erros, salvos, total };
  },

  // Troca a senha em todos os ESPs. Se só alguns trocarem, as senhas ficam
  // diferentes entre ESPs: o erro lista quais falharam pra tentar de novo.
  async trocarSenha(atual, nova) {
    if (CFG.USE_MOCK) {
      await sleep(260);
      if (atual !== CFG.MOCK_PWD) throw new Error('Senha atual incorreta');
      CFG.MOCK_PWD = nova;
      return { erros: [] };
    }
    const tokens = getTokens();
    const { ok, erros, total } = await emTodosEsps(async ip => {
      if (!tokens[ip]) throw new Error('sem login nesse ESP (entre de novo)');
      try {
        return await fetchTO(`${CFG.PROTOCOL}://${ip}/admin/senha`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json', 'X-Admin-Token': tokens[ip] },
          body: JSON.stringify({ atual, nova })
        });
      } catch (e) {
        throw new Error(e.message.includes('403') ? 'senha atual incorreta' : e.message.includes('401') ? LOGIN_EXPIROU : e.message);
      }
    });
    const trocados = Object.keys(ok).length;
    if (!trocados) throw new Error(erros.some(e => e.includes('senha atual incorreta')) ? 'Senha atual incorreta' : 'Nenhum ESP trocou a senha. ' + (erros[0] || ''));
    return { erros, trocados, total };
  },

  async ping() {
    if (CFG.USE_MOCK) return true;
    try {
      const r = await fetchTO(espUrl('/estado'));
      return !!(r && r.salas);
    } catch { return false; }
  },

  logout() { localStorage.removeItem(SK.TOKEN); },
  isLogged() { return Object.keys(getTokens()).length > 0; }
};
