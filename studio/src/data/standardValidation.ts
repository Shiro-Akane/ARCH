import type {StandardParameter} from '../host/configurationContracts.ts';
const decimal=/^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$/;
function number(text:string):number|undefined {
 if(!decimal.test(text))return undefined;
 const value=Number(text);
 // Core from_chars rejects nonzero values underflowing to zero.
 if(!Number.isFinite(value)||(value===0&&/[1-9]/.test(text.split(/[eE]/)[0])))return undefined;
 return value;
}
function expression(text:string):number|undefined {
 const t=text.replace(/\s/g,'');
 if(t==='pi')return Math.PI;if(t==='-pi')return -Math.PI;
 if(t.startsWith('pi*')||t.startsWith('pi/')){const n=number(t.slice(3));if(n===undefined)return undefined;return t[2]==='*'?Math.PI*n:Math.PI/n;}
 if(t.endsWith('*pi')){const n=number(t.slice(0,-3));return n===undefined?undefined:n*Math.PI;}
 return number(t);
}
/** Shared by every standard edit route; custom model parameters retain their own contract. */
export function standardValueError(parameter:StandardParameter,raw:string):string|undefined {
 const text=raw.trim();let value:number|undefined;
 switch(parameter.type){
 case 'string':return undefined;
 case 'bool':return /^(true|false)$/i.test(text)?undefined:'Expected true or false.';
 case 'int':
  if(!/^[+-]?[0-9]+$/.test(text))return 'Expected a complete 32-bit integer.';
  value=Number(text);
  if(!Number.isInteger(value)||value < -2147483648||value > 2147483647)return 'Expected a complete 32-bit integer.';
  break;
 case 'float':value=number(text);break;
 case 'expression':value=expression(text);break;
 }
 if(value===undefined||!Number.isFinite(value))return parameter.type==='expression'?'Expected a finite Core expression.':'Expected a complete finite decimal or scientific-notation number.';
 const c=parameter.constraints;
 if(typeof c.min==='number'&&(value<c.min||(c.minInclusive===false&&value===c.min)))return `Value must be ${c.minInclusive===false?'>':'>='} ${c.min}.`;
 if(typeof c.max==='number'&&(value>c.max||(c.maxInclusive===false&&value===c.max)))return `Value must be ${c.maxInclusive===false?'<':'<='} ${c.max}.`;
 return undefined;
}
