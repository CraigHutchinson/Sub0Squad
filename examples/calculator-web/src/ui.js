export function mount(document, evaluate) {
  const expr = document.querySelector('#expr');
  const result = document.querySelector('#result');
  const degrees = document.querySelector('#degrees');
  const buttons = document.querySelectorAll('button[data-key]');

  buttons.forEach((button) => {
    button.addEventListener('click', () => {
      const key = button.dataset.key;
      if (key === 'C') {
        expr.value = '';
        result.textContent = '';
      } else if (key === 'DEL') {
        expr.value = expr.value.slice(0, -1);
      } else if (key === '=') {
        try {
          const value = evaluate(expr.value, { degrees: degrees.checked });
          result.textContent = String(Number(value.toPrecision(12)));
        } catch (e) {
          result.textContent = 'Error: ' + e.message;
        }
      } else {
        expr.value += key;
      }
    });
  });

  expr.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      try {
        const value = evaluate(expr.value, { degrees: degrees.checked });
        result.textContent = String(Number(value.toPrecision(12)));
      } catch (e) {
        result.textContent = 'Error: ' + e.message;
      }
    }
  });
}
