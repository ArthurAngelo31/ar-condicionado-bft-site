/* Tela da sala - estado só muda quando ESP confirma via resposta HTTP */

function dialPos(f) {
  const a = (135 + 270 * f) * Math.PI / 180;
  return { x: 110 + 90 * Math.cos(a), y: 110 + 90 * Math.sin(a) };
}

function drawTicks() {
  $('dial-ticks').innerHTML = [18, 20, 22, 24].map(t => {
    const p = dialPos((t - 18) / 6);
    return `<circle cx="${p.x.toFixed(2)}" cy="${p.y.toFixed(2)}" r="2" fill="currentColor" opacity="0.4"/>`;
  }).join('');
}

function updateDial(t) {
  const f = (t - 18) / 6;
  $('dial-prog').setAttribute('stroke-dasharray', `${(f * 100).toFixed(2)} 100`);
}

function renderSala(animTemp = false) {
  const s = state.sala;
  if (!s) return;

  $('sala-nome').textContent = s.nome;
  $('sala-grupo').textContent = state.aparelho
    ? `${getGrupoDaSala(s.id)} · Ar-condicionado ${state.aparelho}`
    : getGrupoDaSala(s.id);
  document.title = `${s.nome} · Ar-cond. BFT`;

  // Estado visual sempre reflete a última resposta confirmada do ESP
  $('dial').setAttribute('data-on', s.ligado);

  // Se está aguardando comunicação, mostra
  if (state.op) {
    $('dial-state').textContent = 'Aguardando ESP...';
  } else {
    $('dial-state').textContent = s.ligado ? 'Em operação' : 'Desligado';
  }

  if (animTemp) {
    $('dial-temp').classList.add('changing');
    setTimeout(() => {
      $('dial-num').textContent = s.temperatura;
      $('dial-temp').classList.remove('changing');
      updateDial(s.temperatura);
    }, 200);
  } else {
    $('dial-num').textContent = s.temperatura;
    updateDial(s.temperatura);
  }

  $('ctrl-range-val').textContent = `${s.temperatura}°`;

  const p = $('btn-power');
  p.setAttribute('data-on', s.ligado);
  $('power-text').textContent = state.op ? 'Aguarde...' : (s.ligado ? 'Desligar' : 'Ligar');
  p.disabled = state.op;

  const off = !s.ligado || state.op;
  $('btn-minus').disabled = off || s.temperatura <= 18;
  $('btn-plus').disabled  = off || s.temperatura >= 24;
}

// `aparelho` (1 ou 2) controla só aquele aparelho da sala (sinal IR só da marca
// dele); sem `aparelho`, controla a sala toda (todos os aparelhos juntos).
async function loadSala(id, aparelho) {
  state.salaId = id;
  state.aparelho = aparelho || null;
  const def = getSalas().find(s => s.id === id);
  const nome = def?.nome || `Sala ${id}`;
  try {
    // Só entra na tela após ESP responder
    const resp = await API.obter(id, aparelho);
    state.sala = { ...resp, nome };
  } catch (e) {
    // Estado indefinido: mostra "desconhecido" em vez de assumir desligado
    state.sala = { id, nome, ligado: false, temperatura: 22, indefinido: true };
    toast(e.message || 'Sem conexão com o ESP', 'err');
  }
  renderSala();
}

// Abre a sala: se ela tiver 2 aparelhos configurados, mostra a escolha de qual
// controlar primeiro; com 1 só, vai direto pro controle (comportamento de sempre).
async function abrirSala(id) {
  try {
    const cfg = await API.getConfigSala(id);
    if (cfg.qtd === 2) {
      navTo('escolha-aparelho', { id });
      renderEscolhaAparelho(id, cfg);
      return;
    }
  } catch {}
  navTo('sala', { id });
  loadSala(id);
}

function renderEscolhaAparelho(id, cfg) {
  const sala = getSalas().find(s => s.id === id);
  $('escolha-grupo').textContent = getGrupoDaSala(id);
  $('escolha-nome').textContent = sala?.nome || `Sala ${id}`;
  document.title = `${sala?.nome || 'Sala'} · Ar-cond. BFT`;
  const protocolos = cfg.protocolos || ['MIDEA', 'MIDEA'];
  $('escolha-lista').innerHTML = [1, 2].map(n => `
    <li class="sala-card" data-action="open-aparelho" data-id="${id}" data-aparelho="${n}">
      <div class="sala-card__icon">${ICO.ar}</div>
      <div class="sala-card__body">
        <div class="sala-card__name">Ar-condicionado ${n}</div>
        <div class="sala-card__meta"><span class="sala-card__dot"></span>${marcaLabel(protocolos[n - 1])}</div>
      </div>
      <div class="sala-card__arrow">${ICO.arrow}</div>
    </li>
  `).join('');
}

async function togglePower() {
  if (!state.sala || state.op) return;

  // Guarda o estado anterior para rollback em caso de erro
  const estadoAnterior = { ...state.sala };
  const acaoLigar = !state.sala.ligado;

  vibrate(15);
  state.op = true;
  $('btn-power').classList.add('spin');
  renderSala(); // mostra "Aguardando ESP..."

  try {
    const r = acaoLigar
      ? await API.ligar(state.sala.id, state.sala.temperatura, state.aparelho)
      : await API.desligar(state.sala.id, state.aparelho);

    // ✅ ESP confirmou — só agora aceita o novo estado
    state.sala = { ...r, nome: state.sala.nome };
    state.op = false;
    $('btn-power').classList.remove('spin');
    renderSala();
    toast(r.ligado ? 'Ar-condicionado ligado' : 'Ar-condicionado desligado');
    state.salasCache = null;
  } catch (e) {
    // ❌ Rollback: mantém o estado anterior porque o ESP não confirmou
    state.sala = estadoAnterior;
    state.op = false;
    $('btn-power').classList.remove('spin');
    renderSala();
    toast(e.message || 'Falha na comunicação', 'err');
  }
}

async function ajusteTemp(d) {
  if (!state.sala || !state.sala.ligado || state.op) return;
  const nv = state.sala.temperatura + d;
  if (nv < 18 || nv > 24) return;

  const estadoAnterior = { ...state.sala };

  vibrate(8);
  state.op = true;
  renderSala();

  try {
    // Só aceita nova temperatura quando ESP confirma
    const r = await API.setTemp(state.sala.id, nv, state.aparelho);
    state.sala = { ...r, nome: state.sala.nome };
    state.op = false;
    renderSala(true);
    toast(`Temperatura ajustada para ${nv}°`);
    state.salasCache = null;
  } catch (e) {
    // Rollback
    state.sala = estadoAnterior;
    state.op = false;
    renderSala();
    toast(e.message || 'Falha na comunicação', 'err');
  }
}
