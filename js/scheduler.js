/* Scheduler mock - simula agenda durante desenvolvimento.
   Na versão final, o ESP32 roda o scheduler dele mesmo, 24/7. */

function tickScheduler() {
  if (!CFG.USE_MOCK) return;

  const ag = getAgenda();
  if (!ag.ativa) return;

  const agora = new Date();
  const dow = agora.getDay();
  const idxDia = dow - 1;
  if (idxDia < 0 || idxDia > 4) return;
  if (!ag.diasAtivos[idxDia]) return;

  const hhmm = `${String(agora.getHours()).padStart(2, '0')}:${String(agora.getMinutes()).padStart(2, '0')}`;
  const ultimoKey = `${agora.toDateString()}|${hhmm}`;
  const ultimo = localStorage.getItem(SK.AGENDA_ULTIMO);
  if (ultimo === ultimoKey) return;

  if (hhmm === ag.horaLigar) {
    localStorage.setItem(SK.AGENDA_ULTIMO, ultimoKey);
    mockState.salas.forEach(s => { s.ligado = true; s.temperatura = ag.tempLigar; });
    state.salasCache = null;
    if (state.view === 'home') loadHome();
    toast('Programação: ar-condicionados ligados');
  } else if (hhmm === ag.horaDesligar) {
    localStorage.setItem(SK.AGENDA_ULTIMO, ultimoKey);
    mockState.salas.forEach(s => { s.ligado = false; });
    state.salasCache = null;
    if (state.view === 'home') loadHome();
    toast('Programação: ar-condicionados desligados');
  }
}

setInterval(tickScheduler, 15000);
