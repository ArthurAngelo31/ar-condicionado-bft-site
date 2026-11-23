/* Painel administrativo com abas Geral e Programação */

// Cópia de trabalho da lista de salas, editada no painel antes de salvar.
let salasDraft = null;

// Quantidade de aparelhos + marca de cada um, por sala (vem da ponte, não do
// localStorage - é configuração de hardware, igual pra todo mundo que acessa
// a mesma sala). Carregado sob demanda em renderSalasEditor().
let configAparelhosCache = {}; // { [salaId]: {qtd, protocolos} }
let configAparelhosCarregando = new Set();

// Aceita IP numérico (192.168.1.100) ou hostname mDNS (bft-lab.local).
const enderecoValido = v => /^(\d{1,3}\.){3}\d{1,3}$/.test(v) || /^[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)+$/.test(v);

function renderAdmin() {
  const c = $('admin-content');
  const lo = $('admin-logout-btn');

  if (!API.isLogged()) {
    salasDraft = null;
    lo.style.display = 'none';
    c.innerHTML = `
      <div class="admin-login">
        <img src="img/logo.png" alt="" class="admin-login__logo">
        <h2 class="admin-login__title">Acesso restrito</h2>
        <p class="admin-login__sub">Use a senha de administrador para acessar as configurações do sistema.</p>
        <form class="admin-login__form" id="login-form" autocomplete="off">
          <div class="field">
            <label class="field__label" for="login-pwd">Senha</label>
            <input type="password" id="login-pwd" class="field__input" placeholder="••••••••" autocomplete="current-password" required>
          </div>
          <button type="submit" class="btn-primary" id="login-btn" style="width:100%">Entrar</button>
        </form>
      </div>`;

    $('login-form').addEventListener('submit', async e => {
      e.preventDefault();
      const pwd = $('login-pwd').value;
      const b = $('login-btn');
      b.disabled = true;
      b.textContent = 'Verificando...';
      try {
        const { erros } = await API.login(pwd);
        if (erros.length) toast(`Entrou, mas ${erros.length} ESP(s) não responderam: ${erros.join('; ')}`, 'err');
        else toast('Bem-vindo');
        vibrate(15);
        renderAdmin();
      } catch (err) {
        b.disabled = false;
        b.textContent = 'Entrar';
        toast(err.message || 'Erro', 'err');
        $('login-pwd').value = '';
      }
    });
    return;
  }

  lo.style.display = 'flex';
  const ip = getIp();
  const ag = getAgenda();

  c.innerHTML = `
    <div class="admin-tabs" role="tablist">
      <button class="admin-tab" data-tab="geral" data-active="${state.adminTab === 'geral'}">${ICO.gear}<span>Geral</span></button>
      <button class="admin-tab" data-tab="agenda" data-active="${state.adminTab === 'agenda'}">${ICO.clock}<span>Programação</span></button>
    </div>

    <div class="admin-panel ${state.adminTab === 'geral' ? 'active' : ''}" data-panel="geral">
      <section class="admin-section">
        <h3 class="admin-section__title">Conexão</h3>
        <div class="field">
          <label class="field__label" for="cfg-ip">Endereço do servidor-ponte</label>
          <input type="text" id="cfg-ip" class="field__input" value="${ip}" autocomplete="off" disabled>
        </div>
        <p class="admin-hint">Endereço fixo (servidor-ponte na nuvem) - não muda com a rede, não dá pra editar aqui.</p>
      </section>
      <section class="admin-section">
        <h3 class="admin-section__title">Salas e ESPs</h3>
        <p class="admin-hint">Cada sala pode ter seu próprio ESP32. Salas que compartilham o mesmo ESP podem usar o mesmo IP (ou deixar em branco para usar o IP padrão).</p>
        <div id="salas-editor"></div>
        <button type="button" class="btn btn--ghost btn--add-sala" id="add-sala">${ICO.plus}<span>Adicionar sala</span></button>
      </section>
      <section class="admin-section">
        <h3 class="admin-section__title">Alterar senha</h3>
        <div class="field"><label class="field__label" for="pwd-atual">Senha atual</label><input type="password" id="pwd-atual" class="field__input" placeholder="••••••••" autocomplete="off"></div>
        <div class="field"><label class="field__label" for="pwd-nova">Nova senha</label><input type="password" id="pwd-nova" class="field__input" placeholder="Mínimo 4 caracteres" autocomplete="off"></div>
        <div class="field"><label class="field__label" for="pwd-conf">Confirmar nova senha</label><input type="password" id="pwd-conf" class="field__input" placeholder="Repita a nova senha" autocomplete="off"></div>
      </section>
      <button class="btn-primary" id="cfg-save" style="width:100%">Salvar alterações</button>
    </div>

    <div class="admin-panel ${state.adminTab === 'agenda' ? 'active' : ''}" data-panel="agenda">
      <div class="toggle-card" data-on="${ag.ativa}" id="agenda-toggle-card">
        <div class="toggle-card__info">
          <div class="toggle-card__title">Programação automática</div>
          <div class="toggle-card__desc" id="agenda-desc">
            ${ag.ativa
              ? 'Os ar-condicionados serão ligados e desligados automaticamente nos horários definidos.'
              : 'Ativada apenas por comandos manuais.'}
          </div>
        </div>
        <div class="toggle" data-on="${ag.ativa}" id="agenda-toggle" role="switch" aria-checked="${ag.ativa}" tabindex="0"></div>
      </div>

      <div class="agenda-section ${ag.ativa ? '' : 'disabled'}" id="agenda-section">
        <section class="admin-section">
          <h3 class="admin-section__title">Horários</h3>
          <div class="time-row">
            <div class="field">
              <label class="field__label" for="ag-ligar">Ligar às</label>
              <input type="time" id="ag-ligar" class="field__input field__input--time" value="${ag.horaLigar}">
            </div>
            <div class="field">
              <label class="field__label" for="ag-desligar">Desligar às</label>
              <input type="time" id="ag-desligar" class="field__input field__input--time" value="${ag.horaDesligar}">
            </div>
          </div>
        </section>

        <section class="admin-section">
          <h3 class="admin-section__title">Dias ativos</h3>
          <div class="dias-semana">
            ${['SEG', 'TER', 'QUA', 'QUI', 'SEX'].map((d, i) => `
              <button type="button" class="dia-chip" data-dia="${i}" data-on="${ag.diasAtivos[i]}">
                <span>${d.charAt(0)}</span>
                <span class="dia-chip__sub">${d.slice(1)}</span>
              </button>
            `).join('')}
          </div>
        </section>

        <div class="agenda-resumo" id="agenda-resumo">
          ${ICO.info}
          <div id="agenda-resumo-text"></div>
        </div>
      </div>

      <button class="btn-primary" id="agenda-save" style="width:100%;margin-top:1.4rem">Salvar programação</button>
    </div>
  `;

  c.querySelectorAll('.admin-tab').forEach(t => {
    t.addEventListener('click', () => {
      state.adminTab = t.dataset.tab;
      vibrate(5);
      c.querySelectorAll('.admin-tab').forEach(x => x.setAttribute('data-active', x.dataset.tab === state.adminTab));
      c.querySelectorAll('.admin-panel').forEach(p => p.classList.toggle('active', p.dataset.panel === state.adminTab));
    });
  });

  $('cfg-save').addEventListener('click', saveGeral);

  renderSalasEditor();
  $('add-sala').addEventListener('click', () => {
    capturarSalasDraft();
    const proximoId = Math.max(0, ...salasDraft.map(s => s.id)) + 1;
    salasDraft.push({ id: proximoId, nome: '', grupo: '', ip: '' });
    vibrate(5);
    renderSalasEditor();
    const novoInput = document.querySelector(`[data-sala-id="${proximoId}"] [data-field="nome"]`);
    if (novoInput) novoInput.focus();
  });

  const toggleEl = $('agenda-toggle');
  const toggleCard = $('agenda-toggle-card');
  const sectionEl = $('agenda-section');
  const descEl = $('agenda-desc');

  const updateToggle = (on) => {
    toggleEl.setAttribute('data-on', on);
    toggleEl.setAttribute('aria-checked', on);
    toggleCard.setAttribute('data-on', on);
    sectionEl.classList.toggle('disabled', !on);
    descEl.textContent = on
      ? 'Os ar-condicionados serão ligados e desligados automaticamente nos horários definidos.'
      : 'Ativada apenas por comandos manuais.';
  };

  const toggleClick = () => {
    const novo = toggleEl.getAttribute('data-on') !== 'true';
    updateToggle(novo);
    vibrate(10);
    atualizarResumo();
  };
  toggleEl.addEventListener('click', toggleClick);
  toggleEl.addEventListener('keydown', e => {
    if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); toggleClick(); }
  });

  c.querySelectorAll('.dia-chip').forEach(chip => {
    chip.addEventListener('click', () => {
      const novo = chip.getAttribute('data-on') !== 'true';
      chip.setAttribute('data-on', novo);
      vibrate(5);
      atualizarResumo();
    });
  });

  const atualizarResumo = () => {
    const ligar = $('ag-ligar').value;
    const desligar = $('ag-desligar').value;
    const dias = Array.from(c.querySelectorAll('.dia-chip')).map(x => x.getAttribute('data-on') === 'true');
    const ativa = toggleEl.getAttribute('data-on') === 'true';

    const labels = ['segunda', 'terça', 'quarta', 'quinta', 'sexta'];
    const ativos = labels.filter((_, i) => dias[i]);

    let txt;
    if (!ativa) {
      txt = '<strong>Programação desativada.</strong>';
    } else if (ativos.length === 0) {
      txt = '<strong>Nenhum dia selecionado.</strong> A programação não será executada.';
    } else if (ativos.length === 5) {
      txt = `Ligar às <strong>${ligar}</strong> e desligar às <strong>${desligar}</strong>, de segunda a sexta.`;
    } else {
      const lista = ativos.length === 1 ? ativos[0] : ativos.slice(0, -1).join(', ') + ' e ' + ativos.slice(-1);
      txt = `Ligar às <strong>${ligar}</strong> e desligar às <strong>${desligar}</strong>, ${lista}.`;
    }
    $('agenda-resumo-text').innerHTML = txt;
  };

  $('ag-ligar').addEventListener('change', atualizarResumo);
  $('ag-desligar').addEventListener('change', atualizarResumo);
  atualizarResumo();

  $('agenda-save').addEventListener('click', saveAgenda);
}

// Busca (uma vez por sala) a quantidade de aparelhos + marca de cada um na
// ponte, e redesenha o editor quando chegar - pra não travar a tela esperando.
function carregarConfigAparelhos(id) {
  if (configAparelhosCache[id] || configAparelhosCarregando.has(id)) return;
  configAparelhosCarregando.add(id);
  API.getConfigSala(id)
    .then(cfg => { configAparelhosCache[id] = cfg; })
    .catch(() => { configAparelhosCache[id] = { qtd: 1, protocolos: ['MIDEA', 'MIDEA'] }; })
    .finally(() => { configAparelhosCarregando.delete(id); renderSalasEditor(); });
}

function opcoesMarca(selecionada) {
  return MARCAS_AR.map(m => `<option value="${m.v}" ${m.v === selecionada ? 'selected' : ''}>${m.l}</option>`).join('');
}

// Renderiza a lista editável de salas a partir de `salasDraft` (inicializa a partir
// do storage na primeira vez). Chamada de novo a cada adição/remoção.
function renderSalasEditor() {
  if (!salasDraft) salasDraft = getSalas().map(s => ({ ...s }));
  const host = $('salas-editor');
  if (!host) return;

  const gruposExistentes = [...new Set(salasDraft.map(s => s.grupo).filter(Boolean))];

  host.innerHTML = salasDraft.map((s, i) => {
    const cfg = configAparelhosCache[s.id];
    if (!cfg) carregarConfigAparelhos(s.id);
    const qtd = s._qtd ?? cfg?.qtd ?? 1;
    const protocolos = s._protocolos ?? cfg?.protocolos ?? ['MIDEA', 'MIDEA'];
    return `
    <div class="sala-edit" data-sala-id="${s.id}">
      <div class="sala-edit__head">
        <div class="admin-room__num">${i + 1}</div>
        <button type="button" class="sala-edit__remove" data-remove-sala="${s.id}" aria-label="Remover sala" ${salasDraft.length <= 1 ? 'disabled' : ''}>${ICO.trash}</button>
      </div>
      <div class="field">
        <label class="field__label">Nome da sala</label>
        <input type="text" class="field__input" data-field="nome" value="${(s.nome || '').replace(/"/g, '&quot;')}" placeholder="Ex: 1A / 6C" autocomplete="off">
      </div>
      <div class="sala-edit__row2">
        <div class="field">
          <label class="field__label">Grupo</label>
          <input type="text" class="field__input" data-field="grupo" list="grupos-sugeridos" value="${(s.grupo || '').replace(/"/g, '&quot;')}" placeholder="Ex: PRIMEIRO ANO" autocomplete="off">
        </div>
        <div class="field">
          <label class="field__label">IP do ESP</label>
          <input type="text" class="field__input" data-field="ip" value="${(s.ip || '').replace(/"/g, '&quot;')}" placeholder="Padrão (${getIp()})" autocomplete="off">
        </div>
      </div>
      ${!cfg ? `<p class="admin-hint">Carregando configuração de ar-condicionados...</p>` : `
      <div class="field">
        <label class="field__label">Ar-condicionados nessa sala</label>
        <select class="field__input" data-field="qtd">
          <option value="1" ${qtd === 1 ? 'selected' : ''}>1 aparelho</option>
          <option value="2" ${qtd === 2 ? 'selected' : ''}>2 aparelhos (marcas diferentes)</option>
        </select>
      </div>
      <div class="sala-edit__row2">
        <div class="field">
          <label class="field__label">Marca do Ar-condicionado 1</label>
          <select class="field__input" data-field="marca1">${opcoesMarca(protocolos[0])}</select>
        </div>
        ${qtd === 2 ? `
        <div class="field">
          <label class="field__label">Marca do Ar-condicionado 2</label>
          <select class="field__input" data-field="marca2">${opcoesMarca(protocolos[1])}</select>
        </div>` : ''}
      </div>
      <p class="admin-hint">${qtd === 2
        ? 'O LED precisa alcançar os 2 aparelhos. Ao ligar/desligar a sala toda (ou pela agenda), o sinal de cada marca é enviado em sequência.'
        : 'Pra uma sala com 2 ar-condicionados de marcas diferentes, mude pra "2 aparelhos".'}</p>
      `}
    </div>`;
  }).join('') + `<datalist id="grupos-sugeridos">${gruposExistentes.map(g => `<option value="${g}">`).join('')}</datalist>`;

  host.querySelectorAll('[data-field="qtd"]').forEach(sel => {
    sel.addEventListener('change', () => {
      capturarSalasDraft();
      vibrate(5);
      renderSalasEditor();
    });
  });

  host.querySelectorAll('[data-remove-sala]').forEach(btn => {
    btn.addEventListener('click', () => {
      if (salasDraft.length <= 1) { toast('É necessário manter ao menos uma sala', 'err'); return; }
      capturarSalasDraft();
      const id = parseInt(btn.dataset.removeSala, 10);
      const sala = salasDraft.find(s => s.id === id);
      Modal.open({
        icon: ICO.trash,
        title: 'Remover sala?',
        desc: `"${sala?.nome || 'Esta sala'}" será removida da lista ao salvar. Essa ação não pode ser desfeita.`,
        confirmText: 'Remover',
        danger: true,
        onConfirm: () => {
          salasDraft = salasDraft.filter(s => s.id !== id);
          vibrate(10);
          renderSalasEditor();
        }
      });
    });
  });
}

// Lê os valores atuais dos campos na tela para `salasDraft`, preservando o que
// o usuário já digitou antes de adicionar/remover uma linha ou salvar.
function capturarSalasDraft() {
  const host = $('salas-editor');
  if (!host) return;
  salasDraft = Array.from(host.querySelectorAll('.sala-edit')).map(row => {
    const qtdSel = row.querySelector('[data-field="qtd"]');
    const marca1Sel = row.querySelector('[data-field="marca1"]');
    const marca2Sel = row.querySelector('[data-field="marca2"]');
    const base = {
      id: parseInt(row.dataset.salaId, 10),
      nome: row.querySelector('[data-field="nome"]').value,
      grupo: row.querySelector('[data-field="grupo"]').value,
      ip: row.querySelector('[data-field="ip"]').value
    };
    if (qtdSel) {
      const qtd = parseInt(qtdSel.value, 10);
      base._qtd = qtd;
      base._protocolos = [marca1Sel ? marca1Sel.value : 'MIDEA', marca2Sel ? marca2Sel.value : 'MIDEA'];
    }
    return base;
  });
}

async function saveGeral() {
  const b = $('cfg-save');
  b.disabled = true;
  b.textContent = 'Salvando...';
  const restore = () => { b.disabled = false; b.textContent = 'Salvar alterações'; };

  const ip = $('cfg-ip').value.trim();
  if (!enderecoValido(ip)) {
    toast('IP ou endereço padrão inválido', 'err');
    restore();
    return;
  }

  capturarSalasDraft();
  if (salasDraft.length === 0) {
    toast('É necessário ao menos uma sala', 'err');
    restore();
    return;
  }
  for (const s of salasDraft) {
    const ipSala = (s.ip || '').trim();
    if (ipSala && !enderecoValido(ipSala)) {
      toast(`IP inválido na sala "${s.nome || 'sem nome'}"`, 'err');
      restore();
      return;
    }
  }

  localStorage.setItem(SK.IP, ip);

  const salasFinal = salasDraft.map((s, i) => ({
    id: s.id,
    nome: s.nome.trim() || `Sala ${i + 1}`,
    grupo: s.grupo.trim() || 'OUTROS',
    ip: s.ip.trim()
  }));

  // Config de aparelhos (qtd + marca) é por sala, na ponte - salva só as que mudaram.
  const configErros = [];
  for (const s of salasDraft) {
    if (s._qtd == null) continue;
    const atual = configAparelhosCache[s.id];
    const mudou = !atual || atual.qtd !== s._qtd ||
      atual.protocolos[0] !== s._protocolos[0] || (s._qtd === 2 && atual.protocolos[1] !== s._protocolos[1]);
    if (!mudou) continue;
    try {
      await API.setConfigSala(s.id, { qtd: s._qtd, protocolos: s._protocolos });
      configAparelhosCache[s.id] = { qtd: s._qtd, protocolos: s._protocolos };
    } catch (e) {
      configErros.push(`${s.nome || 'sala ' + s.id}: ${e.message}`);
    }
  }

  setSalas(salasFinal);
  salasDraft = null;
  state.salasCache = null;

  const a = $('pwd-atual').value;
  const n = $('pwd-nova').value;
  const cf = $('pwd-conf').value;
  if (a || n || cf) {
    if (!a) { toast('Digite a senha atual', 'err'); restore(); return; }
    if (n.length < 4 || n.length > 32) { toast('Nova senha deve ter de 4 a 32 caracteres', 'err'); restore(); return; }
    if (n !== cf) { toast('Senhas não conferem', 'err'); restore(); return; }
    try {
      const { erros, trocados, total } = await API.trocarSenha(a, n);
      ['pwd-atual', 'pwd-nova', 'pwd-conf'].forEach(id => $(id).value = '');
      if (erros.length) {
        toast(`Senha trocada em ${trocados} de ${total} ESPs. Falhou: ${erros.join('; ')}`, 'err');
        restore();
        return;
      }
    } catch (e) {
      toast(e.message || 'Erro ao trocar senha', 'err');
      restore();
      return;
    }
  }

  await sleep(280);
  restore();
  renderSalasEditor();
  vibrate(10);
  if (configErros.length) toast(`Salvo, mas a config. de ar-condicionados falhou em: ${configErros.join('; ')}`, 'err');
  else toast('Configurações salvas');
}

async function saveAgenda() {
  const b = $('agenda-save');
  b.disabled = true;
  b.textContent = 'Salvando...';
  const restore = () => { b.disabled = false; b.textContent = 'Salvar programação'; };

  const ativa = $('agenda-toggle').getAttribute('data-on') === 'true';
  const horaLigar = $('ag-ligar').value;
  const horaDesligar = $('ag-desligar').value;
  const diasAtivos = Array.from(document.querySelectorAll('.dia-chip'))
    .map(x => x.getAttribute('data-on') === 'true');

  if (!/^\d{2}:\d{2}$/.test(horaLigar) || !/^\d{2}:\d{2}$/.test(horaDesligar)) {
    toast('Horários inválidos', 'err'); restore(); return;
  }
  if (horaLigar === horaDesligar) {
    toast('Horários devem ser diferentes', 'err'); restore(); return;
  }

  const ag = { ativa, horaLigar, horaDesligar, diasAtivos, tempLigar: 22 };

  try {
    const { erros, salvos, total } = await API.setAgenda(ag);
    restore();
    vibrate(10);
    if (erros.length) toast(`Salva em ${salvos} de ${total} ESPs. Falhou: ${erros.join('; ')}`, 'err');
    else toast('Programação salva');
  } catch (e) {
    restore();
    toast(e.message || 'Erro ao salvar', 'err');
  }
}
