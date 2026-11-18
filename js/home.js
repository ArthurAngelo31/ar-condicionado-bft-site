/* Tela inicial - salas + stats + FAB desligar tudo */

async function loadHome() {
  const banner = $('banner-area');
  const area = $('grupos-area');

  if (!state.salasCache) {
    area.innerHTML = renderSkeletons();
  }

  try {
    const data = await API.listar();
    state.salasCache = data.salas;
    renderGrupos(data.salas);
    updateStats(data.salas);
    updateFab(data.salas);

    banner.innerHTML = CFG.USE_MOCK
      ? `<div class="banner">${ICO.info}<div><strong>Modo demonstração</strong>Ainda sem hardware. Todas as ações são simuladas.</div></div>`
      : '';
  } catch (e) {
    // Se falhou, mostra estado neutro e avisa
    const fb = getSalas().map(s => ({
      ...s,
      ligado: false,
      temperatura: 22,
      indefinido: true
    }));
    renderGrupos(fb);
    updateStats(fb);
    updateFab(fb);
    banner.innerHTML = `<div class="banner banner--err">${ICO.info}<div><strong>Sem conexão com o ESP.</strong>${e.message}. Estado das salas não pode ser confirmado.</div></div>`;
  }
}

function renderSkeletons() {
  return getSalas().slice(0, 3).map(() => `
    <div class="sala-card" style="pointer-events:none;opacity:0.5">
      <div class="sala-card__icon" style="background:var(--line-soft)"></div>
      <div class="sala-card__body">
        <div style="height:1.1rem;width:60%;background:var(--line-soft);border-radius:4px"></div>
        <div style="height:0.7rem;width:40%;background:var(--line-soft);border-radius:4px;margin-top:6px"></div>
      </div>
    </div>
  `).join('');
}

function updateStats(salas) {
  const ligados = salas.filter(s => s.ligado && !s.indefinido).length;
  $('stat-ligados').textContent = ligados;
  $('stat-total').textContent = salas.length;

  const ag = getAgenda();
  const agEl = $('stat-agenda');
  if (ag.ativa) {
    agEl.textContent = 'Ativa';
    agEl.classList.add('stat__value--on');
  } else {
    agEl.textContent = 'Inativa';
    agEl.classList.remove('stat__value--on');
  }
}

function updateFab(salas) {
  const algumLigado = salas.some(s => s.ligado && !s.indefinido);
  const fab = $('fab-desligar');
  fab.style.display = algumLigado ? 'flex' : 'none';
}

function renderGrupos(salas) {
  const ordemPadrao = ['PRIMEIRO ANO', 'SEGUNDO ANO', 'TERCEIRO ANO', 'LABORATÓRIO'];
  const grupoBy = id => getGrupoDaSala(id) || 'OUTROS';

  const grupos = {};
  const ordemEncontrada = [];
  salas.forEach(s => {
    const g = grupoBy(s.id);
    if (!grupos[g]) { grupos[g] = []; ordemEncontrada.push(g); }
    grupos[g].push(s);
  });

  // Grupos conhecidos primeiro (ordem fixa), grupos novos/customizados depois,
  // na ordem em que aparecem na lista de salas.
  const ordem = [
    ...ordemPadrao.filter(g => grupos[g]),
    ...ordemEncontrada.filter(g => !ordemPadrao.includes(g))
  ];

  $('grupos-area').innerHTML = ordem.map(g => `
    <section class="ano-group">
      <div class="ano-group__title">${g}</div>
      <ul class="salas-list">
        ${grupos[g].map(s => `
          <li class="sala-card" data-on="${s.ligado && !s.indefinido}" data-id="${s.id}" data-action="open-sala">
            <div class="sala-card__icon">${ICO.ar}</div>
            <div class="sala-card__body">
              <div class="sala-card__name">${s.nome}</div>
              <div class="sala-card__meta">
                <span class="sala-card__dot"></span>
                ${s.indefinido ? 'Sem conexão' : (s.ligado ? 'Em operação' : 'Aguardando')}
              </div>
            </div>
            ${s.ligado && !s.indefinido ? `<div class="sala-card__temp">${s.temperatura}°</div>` : ''}
            <div class="sala-card__arrow">${ICO.arrow}</div>
          </li>`).join('')}
      </ul>
    </section>
  `).join('');
}

function startAutoRefresh() {
  stopAutoRefresh();
  state.autoRefreshTimer = setInterval(() => {
    if (state.view === 'home' && document.visibilityState === 'visible') loadHome();
  }, CFG.AUTO_REFRESH_MS);
}

function stopAutoRefresh() {
  if (state.autoRefreshTimer) {
    clearInterval(state.autoRefreshTimer);
    state.autoRefreshTimer = null;
  }
}

async function desligarTodas() {
  Modal.open({
    icon: ICO.power,
    title: 'Desligar todas as salas?',
    desc: 'Todos os ar-condicionados serão desligados. O app aguarda a confirmação do ESP para cada um.',
    confirmText: 'Desligar tudo',
    onConfirm: async () => {
      try {
        vibrate(15);
        const r = await API.desligarTodas();
        if (r.erros && r.erros.length > 0) {
          toast(`${r.desligadas} de ${r.total} salas confirmadas`, 'err');
        } else {
          toast('Todos os ar-condicionados foram desligados');
        }
        loadHome();
      } catch (e) {
        toast(e.message || 'Erro ao desligar', 'err');
      }
    }
  });
}
