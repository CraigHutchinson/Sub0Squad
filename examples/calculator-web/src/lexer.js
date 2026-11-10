export function tokenize(text) {
  const tokens = [];
  let i = 0;

  while (i < text.length) {
    const char = text[i];

    // Skip whitespace
    if (/\s/.test(char)) {
      i++;
      continue;
    }

    const pos = i;

    // Check for numbers: integer, decimal, exponent, or starting with dot
    if (/\d/.test(char) || (char === '.' && i + 1 < text.length && /\d/.test(text[i + 1]))) {
      let start = i;
      // Consume digits
      while (i < text.length && /\d/.test(text[i])) {
        i++;
      }
      // Consume decimal part
      if (i < text.length && text[i] === '.') {
        i++;
        while (i < text.length && /\d/.test(text[i])) {
          i++;
        }
      }
      // Consume exponent part
      if (i < text.length && (text[i] === 'e' || text[i] === 'E')) {
        i++;
        if (i < text.length && (text[i] === '+' || text[i] === '-')) {
          i++;
        }
        while (i < text.length && /\d/.test(text[i])) {
          i++;
        }
      }
      const numStr = text.substring(start, i);
      tokens.push({ type: 'num', value: Number(numStr), pos });
      continue;
    }

    // Check for identifiers: starts with letter, followed by letters, digits, or underscore
    if (/[a-zA-Z]/.test(char)) {
      let start = i;
      while (i < text.length && /[a-zA-Z0-9_]/.test(text[i])) {
        i++;
      }
      const id = text.substring(start, i).toLowerCase();
      tokens.push({ type: 'id', value: id, pos });
      continue;
    }

    // Check for operators, parentheses, comma
    if (char === '(') {
      tokens.push({ type: 'lp', value: '(', pos });
      i++;
      continue;
    }
    if (char === ')') {
      tokens.push({ type: 'rp', value: ')', pos });
      i++;
      continue;
    }
    if (char === ',') {
      tokens.push({ type: 'comma', value: ',', pos });
      i++;
      continue;
    }
    if (char === '+' || char === '-' || char === '*' || char === '/' || char === '%' || char === '^' || char === '!') {
      tokens.push({ type: 'op', value: char, pos });
      i++;
      continue;
    }

    // Unexpected character
    throw new Error(`Unexpected character '${char}' at position ${pos}`);
  }

  return tokens;
}
