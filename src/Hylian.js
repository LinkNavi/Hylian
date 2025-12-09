// src/Hylian.js
// Hylian — tokenizer -> parser -> renderer (procedural, explicit)

const fs = require('fs');
const path = require('path');

let TEMPLATES_DIR = path.join(__dirname, 'public');
const TEMPLATE_CACHE = new Map();
const FILTERS = Object.create(null);
let AUTO_ESCAPE = true; // global default: escape output unless marked safe

// ----- default filters -----
FILTERS.upper = v => (v == null ? '' : String(v).toUpperCase());
FILTERS.lower = v => (v == null ? '' : String(v).toLowerCase());
FILTERS.json  = v => JSON.stringify(v);
FILTERS.join  = (v, sep = ',') => Array.isArray(v) ? v.join(sep) : v;
FILTERS.escape = v => {
  if (v == null) return '';
  return String(v)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
};
// alias 'safe' and 'raw' to mark value as safe (no escaping)
FILTERS.raw = v => v;
FILTERS.safe = v => v;
FILTERS.default = (v, d = '') => (v == null || v === '') ? d : v;
FILTERS.length = v => (v == null ? 0 : (Array.isArray(v) || typeof v === 'string') ? v.length : 0);
FILTERS.date = (v, fmt) => {
  if (!v) return '';
  try {
    const d = v instanceof Date ? v : new Date(v);
    if (!fmt) return d.toISOString();
    // very tiny formatter: YYYY, MM, DD, hh, mm, ss
    return fmt.replace('YYYY', d.getFullYear())
              .replace('MM', String(d.getMonth()+1).padStart(2,'0'))
              .replace('DD', String(d.getDate()).padStart(2,'0'))
              .replace('hh', String(d.getHours()).padStart(2,'0'))
              .replace('mm', String(d.getMinutes()).padStart(2,'0'))
              .replace('ss', String(d.getSeconds()).padStart(2,'0'));
  } catch (e) { return ''; }
};

// public API
function setTemplatesDir(dir) { TEMPLATES_DIR = dir; }
function registerFilter(name, fn) { FILTERS[name] = fn; }
function clearCache() { TEMPLATE_CACHE.clear(); }
function setAutoEscape(v) { AUTO_ESCAPE = !!v; }

// ----- file reading -----
function readTemplateFile(name) {
  const filePath = path.isAbsolute(name) ? name : path.join(TEMPLATES_DIR, name);
  return fs.readFileSync(filePath, 'utf8');
}

// ---------------- Tokenizer ----------------
// tokens: { type: 'TEXT'|'VAR'|'TAG'|'COMMENT', value, trimLeft?, trimRight? }

function trimLeftOfPreviousText(tokens) {
  if (!tokens.length) return;
  const last = tokens[tokens.length - 1];
  if (last.type !== 'TEXT') return;
  // remove trailing whitespace/newlines
  last.value = last.value.replace(/[ \t\r\n]+$/g, '');
}

function skipWhitespaceAfter(template, idx) {
  let i = idx;
  while (i < template.length && /[ \t\r\n]/.test(template.charAt(i))) i++;
  return i;
}

function tokenize(template) {
  const tokens = [];
  let i = 0;
  const len = template.length;
  while (i < len) {
    const varStart = template.indexOf('{{', i);
    const tagStart = template.indexOf('{%', i);
    const commentStart = template.indexOf('{#', i);

    let next = -1;
    let kind = null;
    if (varStart !== -1) { next = varStart; kind = 'VAR'; }
    if (tagStart !== -1 && (next === -1 || tagStart < next)) { next = tagStart; kind = 'TAG'; }
    if (commentStart !== -1 && (next === -1 || commentStart < next)) { next = commentStart; kind = 'COMMENT'; }

    if (next === -1) {
      tokens.push({ type: 'TEXT', value: template.slice(i) });
      break;
    }

    if (next > i) tokens.push({ type: 'TEXT', value: template.slice(i, next) });

    if (kind === 'VAR') {
      let start = next + 2;
      let trimLeft = false, trimRight = false;
      if (template.charAt(start) === '-') { trimLeft = true; start++; }
      const endTokenPos = template.indexOf('}}', start);
      if (endTokenPos === -1) { tokens.push({ type: 'TEXT', value: template.slice(next) }); break; }
      let end = endTokenPos;
      if (template.charAt(end - 1) === '-') { trimRight = true; /* end--; */ }
      const inside = template.slice(start, end).trim();
      if (trimLeft) trimLeftOfPreviousText(tokens);
      tokens.push({ type: 'VAR', value: inside, trimLeft: trimLeft, trimRight: trimRight });
      i = endTokenPos + 2;
      if (trimRight) i = skipWhitespaceAfter(template, i);
    } else if (kind === 'TAG') {
      let start = next + 2;
      let trimLeft = false, trimRight = false;
      if (template.charAt(start) === '-') { trimLeft = true; start++; }
      const endTokenPos = template.indexOf('%}', start);
      if (endTokenPos === -1) { tokens.push({ type: 'TEXT', value: template.slice(next) }); break; }
      let end = endTokenPos;
      if (template.charAt(end - 1) === '-') { trimRight = true; }
      const inside = template.slice(start, end).trim();
      if (trimLeft) trimLeftOfPreviousText(tokens);
      tokens.push({ type: 'TAG', value: inside, trimLeft: trimLeft, trimRight: trimRight, raw: template.slice(next, endTokenPos+2) });
      i = endTokenPos + 2;
      if (trimRight) i = skipWhitespaceAfter(template, i);
    } else { // COMMENT
      let start = next + 2;
      if (template.charAt(start) === '-') start++;
      const endTokenPos = template.indexOf('#}', start);
      if (endTokenPos === -1) { i = len; }
      else { i = endTokenPos + 2; }
      // comments ignored
    }
  }
  return tokens;
}

// ---------------- Parser ----------------
function makeNode(type, props) {
  const node = Object.assign({ type }, props || {});
  node.children = node.children || [];
  return node;
}

// push child into right place; If nodes use thenChildren/elseChildren
function pushChild(parent, child) {
  if (!parent) return;
  if (parent.type === 'If') {
    if (parent._active === 'else') {
      parent.elseChildren = parent.elseChildren || [];
      parent.elseChildren.push(child);
    } else {
      parent.thenChildren = parent.thenChildren || [];
      parent.thenChildren.push(child);
    }
  } else {
    parent.children = parent.children || [];
    parent.children.push(child);
  }
}

function parse(tokens) {
  const root = makeNode('Root', { children: [], macros: {} });
  const stack = [root];

  function current() { return stack[stack.length - 1]; }

  for (let t of tokens) {
    if (t.type === 'TEXT') {
      pushChild(current(), makeNode('Text', { text: t.value }));
      continue;
    }

    if (t.type === 'VAR') {
      // detect call expression like name(a, b)
      const callMatch = t.value.match(/^([A-Za-z_$][A-Za-z0-9_$]*)\s*\(([\s\S]*)\)$/);
      if (callMatch) {
        const name = callMatch[1];
        const argsRaw = callMatch[2].trim();
        // split args by comma (naive: no nested commas); it's fine for our use-case
        const args = argsRaw === '' ? [] : argsRaw.split(',').map(s => s.trim());
        pushChild(current(), makeNode('Call', { name, args }));
        continue;
      }

      // parse filters: expr | filter1:arg | filter2
      const parts = t.value.split('|').map(s => s.trim());
      const expr = parts.shift();
      const filters = parts.map(part => {
        const segs = part.split(':').map(s => s.trim());
        return { name: segs[0], args: segs.slice(1) };
      });
      pushChild(current(), makeNode('Var', { expr, filters }));
      continue;
    }

    if (t.type === 'TAG') {
      const txt = t.value;
      const parts = txt.split(/\s+/);
      const tag = parts[0];

      if (tag === 'extends') {
        const m = txt.match(/extends\s+['"](.+?)['"]/);
        if (m) stack[0].extends = m[1];
        continue;
      }

      if (tag === 'block') {
        const name = parts[1] || '';
        const node = makeNode('Block', { name, children: [] });
        pushChild(current(), node);
        stack.push(node);
        continue;
      }

      if (tag === 'endblock') {
        stack.pop();
        continue;
      }

      if (tag === 'if') {
        const cond = txt.slice(3).trim();
        const node = { type: 'If', condition: cond, thenChildren: [], elseChildren: null, _active: 'then' };
        pushChild(current(), node);
        stack.push(node);
        continue;
      }

      if (tag === 'else') {
        const node = current();
        if (node && node.type === 'If') node._active = 'else';
        continue;
      }

      if (tag === 'endif') {
        stack.pop();
        continue;
      }

      if (tag === 'for') {
        const m = txt.match(/^for\s+([A-Za-z_$][A-Za-z0-9_$]*)\s+in\s+(.+)$/);
        if (m) {
          const varName = m[1];
          const listExpr = m[2].trim();
          const node = makeNode('For', { varName, listExpr, children: [] });
          pushChild(current(), node);
          stack.push(node);
        }
        continue;
      }

      if (tag === 'endfor') {
        stack.pop();
        continue;
      }

      if (tag === 'include') {
        // include "file" with a=b, c=d
        const m = txt.match(/include\s+['"](.+?)['"](?:\s+with\s+(.+))?/);
        if (m) {
          const file = m[1];
          const withPart = m[2] || '';
          const assigns = [];
          if (withPart.trim()) {
            // split by comma, parse key=expr
            const parts = withPart.split(',').map(s => s.trim()).filter(Boolean);
            for (let p of parts) {
              const mm = p.match(/^([A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*(.+)$/);
              if (mm) assigns.push({ name: mm[1], expr: mm[2].trim() });
            }
          }
          pushChild(current(), makeNode('Include', { file, assigns }));
        }
        continue;
      }

      if (tag === 'set') {
        // set name = expr
        const mm = txt.match(/^set\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*(.+)$/);
        if (mm) {
          pushChild(current(), makeNode('Set', { name: mm[1], expr: mm[2].trim() }));
        }
        continue;
      }

      if (tag === 'macro') {
        // macro name(a, b)
        const mm = txt.match(/^macro\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*\(([\s\S]*)\)$/);
        if (mm) {
          const name = mm[1];
          const argsRaw = mm[2].trim();
          const args = argsRaw === '' ? [] : argsRaw.split(',').map(s => s.trim());
          const node = makeNode('Macro', { name, args, children: [] });
          // macros are collected at root, but we still need its body captured => push, then collect later
          pushChild(current(), node);
          stack.push(node);
        }
        continue;
      }

      if (tag === 'endmacro') {
        // store macro in root and remove from parent's children later
        const node = stack.pop();
        // node is Macro
        continue;
      }

      // unknown tag -> emit raw text fallback
      pushChild(current(), makeNode('Text', { text: `{% ${txt} %}` }));
      continue;
    }
  }

  return root;
}

// ---------------- Path resolution & expressions ----------------

function resolvePath(context, pathStr) {
  if (pathStr == null) return undefined;
  pathStr = pathStr.trim();
  if (pathStr === '') return undefined;
  if (/^["'].*["']$/.test(pathStr)) return pathStr.slice(1, -1);
  if (/^-?\d+(\.\d+)?$/.test(pathStr)) return Number(pathStr);

  const parts = pathStr.split('.');
  let cur = context;
  for (let i = 0; i < parts.length; ++i) {
    if (cur == null) return undefined;
    const p = parts[i];
    if (/^\d+$/.test(p)) cur = cur[parseInt(p, 10)];
    else cur = cur[p];
  }
  return cur;
}

function isSimpleExpr(expr) {
  expr = expr.trim();
  if (expr === '') return false;
  if (/^["'].*["']$/.test(expr)) return true;
  if (/^-?\d+(\.\d+)?$/.test(expr)) return true;
  return /^[A-Za-z_$][A-Za-z0-9_$]*(?:\.[A-Za-z0-9_$]+)*$/.test(expr);
}

// fallback evaluator: uses Function only when necessary (dangerous for untrusted)
function evalExpr(expr, context) {
  const t = expr.trim();
  if (isSimpleExpr(t)) return resolvePath(context, t);
  try {
    const keys = Object.keys(context || {});
    const vals = keys.map(k => context[k]);
    const fn = Function(...keys, `return (${expr});`);
    return fn(...vals);
  } catch (e) {
    return undefined;
  }
}

// ---------------- Renderer ----------------

function collectMacrosAndRemove(node, root) {
  if (!node || !node.children) return;
  // iterate in reverse to safely remove
  for (let i = node.children.length - 1; i >= 0; --i) {
    const c = node.children[i];
    if (c.type === 'Macro') {
      // store macro in root.macros
      root.macros = root.macros || {};
      root.macros[c.name] = c;
      node.children.splice(i, 1); // remove macro node so it doesn't render
      continue;
    }
    // recurse
    collectMacrosAndRemove(c, root);
  }
}

function collectBlocks(node, map) {
  if (!node) return;
  if (node.type === 'Block') {
    map[node.name] = node;
    return;
  }
  const arr = node.children || [];
  for (let i = 0; i < arr.length; ++i) collectBlocks(arr[i], map);
}

function renderNode(node, context, childBlocks, rootAst) {
  if (!node) return '';

  switch (node.type) {
    case 'Root': {
      // if root has extends -> render layout with collected child blocks
      if (node.extends) {
        const blocks = Object.create(null);
        collectBlocks(node, blocks);
        // collect macros in child template too (so child-defined macros available)
        collectMacrosAndRemove(node, node);
        // read layout
        let layout;
        try { layout = readTemplateFile(node.extends); } catch (e) { return ''; }
        const layoutAst = compileStringToAST(layout, node.extends);
        // ensure layout picks up macros from child too: merge macros maps
        layoutAst.macros = Object.assign({}, layoutAst.macros || {}, node.macros || {});
        return renderNode(layoutAst, context, blocks, layoutAst);
      }
      // normal root: first ensure macros collected
      collectMacrosAndRemove(node, node);
      // render children
      return (node.children || []).map(c => renderNode(c, context, childBlocks, node)).join('');
    }

    case 'Text':
      return node.text;

    case 'Var': {
      let value = evalExpr(node.expr, context);
      // apply filters in order
      let safeFlag = false;
      for (let f of node.filters || []) {
        const fn = FILTERS[f.name];
        const fargs = (f.args || []).map(a => {
          if (!a) return undefined;
          return resolvePath(context, a) ?? evalExpr(a, context);
        });
        if (fn) {
          value = fn(value, ...fargs);
        } else {
          // unknown filter: ignore
        }
        if (f.name === 'raw' || f.name === 'safe') safeFlag = true;
      }
      // auto-escape unless safeFlag is set or a raw/safe filter was applied
      if (AUTO_ESCAPE && !safeFlag) {
        value = FILTERS.escape(value);
      }
      return value == null ? '' : String(value);
    }

    case 'Call': {
      // macro call or function in context
      const name = node.name;
      const args = (node.args || []).map(a => resolvePath(context, a) ?? evalExpr(a, context));
      // macros live in rootAst.macros
      const macros = (rootAst && rootAst.macros) ? rootAst.macros : {};
      if (macros && macros[name]) {
        const macro = macros[name];
        // build local context for macro: arguments bound to param names, parent context copied
        const ctx = Object.assign({}, context);
        for (let i = 0; i < (macro.args || []).length; ++i) {
          ctx[macro.args[i]] = args[i];
        }
        // render macro body
        return ((macro.children || []).map(c => renderNode(c, ctx, null, rootAst)).join(''));
      }
      // fallback: call function in context if present
      const fn = resolvePath(context, name);
      if (typeof fn === 'function') {
        try { return String(fn(...args)); } catch (e) { return ''; }
      }
      return '';
    }

    case 'Block': {
      if (childBlocks && childBlocks[node.name]) {
        const override = childBlocks[node.name];
        return (override.children || []).map(c => renderNode(c, context, childBlocks, rootAst)).join('');
      }
      return (node.children || []).map(c => renderNode(c, context, childBlocks, rootAst)).join('');
    }

    case 'If': {
      const cond = Boolean(evalExpr(node.condition, context));
      const chosen = cond ? (node.thenChildren || []) : (node.elseChildren || []);
      return chosen.map(c => renderNode(c, context, childBlocks, rootAst)).join('');
    }

    case 'For': {
      const list = evalExpr(node.listExpr, context);
      if (!Array.isArray(list)) return '';
      const pieces = [];
      for (let i = 0; i < list.length; ++i) {
        const item = list[i];
        const ctx = Object.assign({}, context);
        ctx[node.varName] = item;
        ctx.loop = { index: i, first: i === 0, last: i === list.length - 1, length: list.length };
        pieces.push((node.children || []).map(c => renderNode(c, ctx, childBlocks, rootAst)).join(''));
      }
      return pieces.join('');
    }

    case 'Include': {
      try {
        const tpl = readTemplateFile(node.file);
        const ast = compileStringToAST(tpl, node.file);
        // evaluate assigns to build include context
        const incCtx = Object.assign({}, context);
        for (let a of (node.assigns || [])) {
          incCtx[a.name] = resolvePath(context, a.expr) ?? evalExpr(a.expr, context);
        }
        // merge macros from parent (so includes can call macros)
        ast.macros = Object.assign({}, ast.macros || {}, rootAst && rootAst.macros || {});
        return renderNode(ast, incCtx, null, ast);
      } catch (e) { return ''; }
    }

    case 'Set': {
      const v = evalExpr(node.expr, context);
      // mutate current context (shallow) so subsequent nodes see the change
      context[node.name] = v;
      return '';
    }

    default:
      return '';
  }
}

// ---------------- Compile & helpers ----------------

function compileStringToAST(templateString, debugName) {
  if (debugName && TEMPLATE_CACHE.has(debugName)) return TEMPLATE_CACHE.get(debugName);
  const tokens = tokenize(templateString);
  const ast = parse(tokens);
  // collect macros from AST and remove their nodes so they don't render
  collectMacrosAndRemove(ast, ast);
  if (debugName) TEMPLATE_CACHE.set(debugName, ast);
  return ast;
}

function renderString(templateString, context) {
  const ast = compileStringToAST(templateString, null);
  return renderNode(ast, Object.assign({}, context || {}), null, ast);
}

function renderFile(templateName, context) {
  const fullPath = path.isAbsolute(templateName) ? templateName : path.join(TEMPLATES_DIR, templateName);
  const tpl = readTemplateFile(fullPath);
  const ast = compileStringToAST(tpl, fullPath);
  return renderNode(ast, Object.assign({}, context || {}), null, ast);
}

// public exports
module.exports = {
  setTemplatesDir,
  registerFilter,
  clearCache,
  setAutoEscape,
  renderString,
  renderFile,
  resolvePath,
  _internal: { tokenize, parse, compileStringToAST }
};
