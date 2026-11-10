const PI = Math.PI;
const E = Math.E;
const TAU = PI * 2;

export function constant(name) {
  if (name === 'pi') return PI;
  if (name === 'e') return E;
  if (name === 'tau') return TAU;
  return undefined;
}

export function factorial(n) {
  if (n < 0 || !Number.isInteger(n)) {
    throw new Error('Factorial requires a non-negative integer');
  }
  if (n === 0 || n === 1) return 1;
  let result = 1;
  for (let i = 2; i <= n; i++) {
    result *= i;
  }
  return result;
}

const ONE_ARG_FUNCTIONS = [
  'sin', 'cos', 'tan',
  'asin', 'acos', 'atan',
  'sinh', 'cosh', 'tanh',
  'ln', 'log', 'log2',
  'sqrt', 'cbrt', 'abs',
  'exp', 'floor', 'ceil', 'round', 'fact'
];

const TWO_ARG_FUNCTIONS = [
  'pow', 'min', 'max'
];

function toRadians(degrees) {
  return degrees * (PI / 180);
}

function toDegrees(radians) {
  return radians * (180 / PI);
}

export function callFunction(name, args, degrees = false) {
  const argCount = args.length;

  if (ONE_ARG_FUNCTIONS.includes(name)) {
    if (argCount !== 1) {
      throw new Error(`Function ${name} requires exactly 1 argument`);
    }
    const val = args[0];
    switch (name) {
      case 'sin':
        return Math.sin(degrees ? toRadians(val) : val);
      case 'cos':
        return Math.cos(degrees ? toRadians(val) : val);
      case 'tan':
        return Math.tan(degrees ? toRadians(val) : val);
      case 'asin': {
        const res = Math.asin(val);
        return degrees ? toDegrees(res) : res;
      }
      case 'acos': {
        const res = Math.acos(val);
        return degrees ? toDegrees(res) : res;
      }
      case 'atan': {
        const res = Math.atan(val);
        return degrees ? toDegrees(res) : res;
      }
      case 'sinh':
        return Math.sinh(val);
      case 'cosh':
        return Math.cosh(val);
      case 'tanh':
        return Math.tanh(val);
      case 'ln':
        if (val <= 0) throw new Error('ln requires a positive number');
        return Math.log(val);
      case 'log':
        if (val <= 0) throw new Error('log requires a positive number');
        return Math.log10(val);
      case 'log2':
        if (val <= 0) throw new Error('log2 requires a positive number');
        return Math.log2(val);
      case 'sqrt':
        if (val < 0) throw new Error('sqrt requires a non-negative number');
        return Math.sqrt(val);
      case 'cbrt':
        return Math.cbrt(val);
      case 'abs':
        return Math.abs(val);
      case 'exp':
        return Math.exp(val);
      case 'floor':
        return Math.floor(val);
      case 'ceil':
        return Math.ceil(val);
      case 'round':
        return Math.round(val);
      case 'fact':
        return factorial(val);
      default:
        throw new Error(`Unknown function: ${name}`);
    }
  } else if (TWO_ARG_FUNCTIONS.includes(name)) {
    if (argCount !== 2) {
      throw new Error(`Function ${name} requires exactly 2 arguments`);
    }
    const [a, b] = args;
    switch (name) {
      case 'pow':
        return Math.pow(a, b);
      case 'min':
        return Math.min(a, b);
      case 'max':
        return Math.max(a, b);
      default:
        throw new Error(`Unknown function: ${name}`);
    }
  } else {
    throw new Error(`Unknown function: ${name}`);
  }
}

