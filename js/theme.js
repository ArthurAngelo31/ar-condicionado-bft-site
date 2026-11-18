/* Gerenciamento de tema (light/dark/auto) */

function getStoredTheme() {
  return localStorage.getItem(SK.THEME);
}

function applyTheme(theme) {
  if (theme === 'light' || theme === 'dark') {
    document.documentElement.setAttribute('data-theme', theme);
  } else {
    document.documentElement.removeAttribute('data-theme');
  }
  // Atualiza theme-color do meta
  const dark = theme === 'dark' || (theme !== 'light' && matchMedia('(prefers-color-scheme: dark)').matches);
  const meta = document.querySelector('meta[name="theme-color"]:not([media])');
  if (meta) meta.setAttribute('content', dark ? '#1a0708' : '#7c1113');
}

function toggleTheme() {
  const cur = getStoredTheme() || (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
  const next = cur === 'dark' ? 'light' : 'dark';
  localStorage.setItem(SK.THEME, next);
  applyTheme(next);
  vibrate(10);
}

// Aplica no boot
(function () {
  const stored = getStoredTheme();
  if (stored) applyTheme(stored);
})();
