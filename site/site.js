(function () {
  var root = document.documentElement;
  var saved;
  try { saved = localStorage.getItem('ziran-theme'); } catch (_) {}
  if (saved === 'dark' || saved === 'light') root.dataset.theme = saved;
  var toggle = document.querySelector('.theme-toggle');
  function updateThemeLabel() {
    if (toggle) toggle.setAttribute('aria-label', root.dataset.theme === 'dark' ? 'Switch to light theme' : 'Switch to dark theme');
  }
  updateThemeLabel();
  if (toggle) toggle.addEventListener('click', function () {
    var next = root.dataset.theme === 'dark' ? 'light' : 'dark';
    root.dataset.theme = next;
    try { localStorage.setItem('ziran-theme', next); } catch (_) {}
    updateThemeLabel();
  });
  var menu = document.querySelector('.menu-button');
  var nav = document.getElementById('primary-nav');
  function closeMenu() {
    if (!menu || !nav) return;
    menu.setAttribute('aria-expanded', 'false');
    nav.classList.remove('is-open');
  }
  if (menu && nav) menu.addEventListener('click', function () {
    var open = menu.getAttribute('aria-expanded') !== 'true';
    menu.setAttribute('aria-expanded', String(open));
    nav.classList.toggle('is-open', open);
  });
  if (nav) nav.addEventListener('click', function (event) {
    if (event.target.closest('a')) closeMenu();
  });
  document.addEventListener('keydown', function (event) {
    if (event.key === 'Escape') closeMenu();
  });
  document.querySelectorAll('.article-body pre').forEach(function (block) {
    var button = document.createElement('button');
    button.type = 'button';
    button.className = 'copy-button';
    button.textContent = 'Copy';
    button.setAttribute('aria-label', 'Copy code');
    button.addEventListener('click', function () {
      if (!navigator.clipboard) return;
      navigator.clipboard.writeText(block.querySelector('code').textContent).then(function () {
        button.textContent = 'Copied';
        setTimeout(function () { button.textContent = 'Copy'; }, 1800);
      });
    });
    block.appendChild(button);
  });
})();
