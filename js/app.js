/* Bootstrap do app - navegação, eventos globais, back button */

function go(view, opts = {}) {
  const cur = document.querySelector('.view.active');
  if (cur && cur.dataset.view === view) return;
  if (cur) cur.classList.remove('active', 'back');

  const next = document.querySelector(`.view[data-view="${view}"]`);
  if (!next) return;
  next.classList.add('active');
  if (opts.back) next.classList.add('back');

  state.view = view;
  window.scrollTo(0, 0);

  if (view === 'home') { loadHome(); startAutoRefresh(); }
  else { stopAutoRefresh(); }

  if (view === 'admin') renderAdmin();
}

function navTo(view, data) {
  if (data && data.id) state.salaId = data.id;
  viewHistory.push(view);
  go(view);
  window.history.pushState({ view }, '');
}

function navBack() {
  if (viewHistory.length > 1) viewHistory.pop();
  const prev = viewHistory[viewHistory.length - 1] || 'home';
  go(prev, { back: true });
}

// Intercepta o botão voltar do celular
window.addEventListener('popstate', () => {
  if (viewHistory.length > 1) {
    viewHistory.pop();
    const prev = viewHistory[viewHistory.length - 1] || 'home';
    go(prev, { back: true });
  }
});

// Delegação de eventos
document.addEventListener('click', e => {
  const t = e.target.closest('[data-action]');
  if (!t) return;
  const a = t.dataset.action;

  if (a === 'goto-admin') navTo('admin');
  else if (a === 'back') navBack();
  else if (a === 'open-sala') {
    const id = parseInt(t.dataset.id, 10);
    abrirSala(id);
  }
  else if (a === 'open-aparelho') {
    const id = parseInt(t.dataset.id, 10);
    const aparelho = parseInt(t.dataset.aparelho, 10);
    navTo('sala', { id });
    loadSala(id, aparelho);
  }
  else if (a === 'toggle-theme') toggleTheme();
  else if (a === 'desligar-todos') desligarTodas();
});

// Botões da sala
$('btn-power').addEventListener('click', togglePower);
$('btn-minus').addEventListener('click', () => ajusteTemp(-2));
$('btn-plus').addEventListener('click', () => ajusteTemp(+2));

// Logout
$('admin-logout-btn').addEventListener('click', () => {
  API.logout();
  renderAdmin();
  toast('Sessão encerrada');
});

// Auto-refresh quando volta pra aba
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible' && state.view === 'home') loadHome();
});

// Inicialização
drawTicks();
loadHome();
startAutoRefresh();
window.history.replaceState({ view: 'home' }, '');

// PWA
if ('serviceWorker' in navigator) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('service-worker.js').catch(() => {});
  });
}
