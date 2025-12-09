// src/main.js
const path = require('path');
const express = require('express');
const Hylian = require('./Hylian');

const app = express();
const PORT = 3000;

// Set templates dir
const templatesDir = path.join(__dirname, 'public');
Hylian.setTemplatesDir(templatesDir);

// Optional: register extra filters
Hylian.registerFilter('brackets', v => `[${v}]`);

// Example context
function getContext() {
  return {
    title: 'Hylian Feature Demo',
    user: { name: 'Kirby', isAdmin: true, items: ['Sword', 'Shield', 'Potion'] },
    items: ['apple', 'banana', 'cherry'],
    rawHtml: '<b>bold</b>',
    now: new Date()
  };
}

// Serve static files (CSS, JS, images)
app.use('/static', express.static(path.join(templatesDir, 'static')));

// Routes
app.get('/', (req, res) => {
  const ctx = getContext();
  try {
    const html = Hylian.renderFile('page_with_nav.html', ctx);
    res.send(html);
  } catch (e) {
    res.status(500).send(`<pre>Template error:\n${e.stack}</pre>`);
  }
});

// Another example route
app.get('/dashboard', (req, res) => {
  const ctx = getContext();
  ctx.user.name = 'AdminUser';
  const html = Hylian.renderFile('page_with_nav.html', ctx);
  res.send(html);
});

// Start server
app.listen(PORT, () => {
  console.log(`🚀 Hylian demo server running at http://localhost:${PORT}`);
});
