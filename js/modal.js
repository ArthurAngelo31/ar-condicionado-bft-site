/* Modal de confirmação genérico */

const Modal = {
  open({ icon = ICO.info, title = 'Confirmar', desc = '', confirmText = 'Confirmar', cancelText = 'Cancelar', danger = false, onConfirm }) {
    const modal = $('modal');
    $('modal-icon').innerHTML = icon;
    $('modal-title').textContent = title;
    $('modal-desc').textContent = desc;
    $('modal-cancel').textContent = cancelText;
    const confirmBtn = $('modal-confirm');
    confirmBtn.textContent = confirmText;
    confirmBtn.classList.toggle('btn--danger', danger);

    modal.classList.add('open');
    modal.setAttribute('aria-hidden', 'false');

    const cleanup = () => {
      modal.classList.remove('open');
      modal.setAttribute('aria-hidden', 'true');
      confirmBtn.removeEventListener('click', onConfirmClick);
    };
    const onConfirmClick = async () => {
      cleanup();
      if (onConfirm) await onConfirm();
    };
    confirmBtn.addEventListener('click', onConfirmClick);

    modal.querySelectorAll('[data-modal-close]').forEach(el => {
      el.addEventListener('click', cleanup, { once: true });
    });
  }
};
