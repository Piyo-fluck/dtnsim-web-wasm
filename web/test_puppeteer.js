const http = require('http');
const fs = require('fs');
const path = require('path');
const puppeteer = require('puppeteer');

// Simple static file server for the web/ directory
function startStaticServer(rootDir, port = 8000) {
  const server = http.createServer((req, res) => {
    let urlPath = decodeURIComponent(req.url.split('?')[0]);
    if (urlPath === '/') urlPath = '/index.html';
    const filePath = path.join(rootDir, urlPath);
    if (!filePath.startsWith(path.resolve(rootDir))) {
      res.statusCode = 403; res.end('Forbidden'); return;
    }
    fs.stat(filePath, (err, stat) => {
      if (err || !stat.isFile()) { res.statusCode = 404; res.end('Not Found'); return; }
      const ext = path.extname(filePath).toLowerCase();
      const types = { '.html':'text/html', '.js':'application/javascript', '.wasm':'application/wasm', '.css':'text/css', '.png':'image/png', '.jpg':'image/jpeg' };
      res.setHeader('Content-Type', types[ext] || 'application/octet-stream');
      const stream = fs.createReadStream(filePath);
      stream.pipe(res);
    });
  });
  return new Promise((resolve, reject) => {
    server.listen(port, '127.0.0.1', () => resolve(server));
    server.on('error', reject);
  });
}

(async () => {
  const root = path.resolve(__dirname);
  const server = await startStaticServer(root, 8000);
  console.log('Static server started on http://127.0.0.1:8000');

  const browser = await puppeteer.launch({ args: ['--no-sandbox','--disable-setuid-sandbox'] });
  const page = await browser.newPage();

  const logs = [];
  page.on('console', msg => {
    const text = msg.text(); logs.push(text); console.log('PAGE LOG>', text);
  });
  page.on('pageerror', err => console.error('PAGE ERROR>', err.toString()));

  try {
    const resp = await page.goto('http://127.0.0.1:8000/index.html', { waitUntil: 'networkidle2', timeout: 30000 });
    console.log('PAGE: status', resp && resp.status());
    const html = await page.content();
    console.log('PAGE HTML PREVIEW:\n', html.slice(0,1200));

    // The served file is plain JS text (index.html contains a script but not wrapped in <script> tags).
    // Evaluate the body text as JavaScript so that the demo control is added to the DOM.
    const jsText = await page.evaluate(() => document.body.textContent || '');
    if (jsText && jsText.trim().length > 0) {
      console.log('Injecting JS content from index.html into page to execute demo helpers');
      await page.evaluate(js => { try { eval(js); } catch (e) { console.error('eval error', e); } }, jsText);
    }

    // Wait for Start WASM demo button (may be added by the evaluated JS)
    await page.waitForSelector('button, #start-wasm', { timeout: 10000 });

    // Click the button with text 'Start WASM demo'
    const buttons = await page.$$('button');
    let clicked = false;
    for (const b of buttons) {
      const text = (await (await b.getProperty('textContent')).jsonValue()).trim();
      if (text.includes('Start WASM demo')) { await b.click(); clicked = true; break; }
    }
    if (!clicked) {
      // try element by id
      const idBtn = await page.$('#start-wasm');
      if (idBtn) { await idBtn.click(); clicked = true; }
    }
    if (!clicked) throw new Error('Start button not found');

    // Wait for init log or positions update
    const gotInit = await page.waitForFunction(() => {
      return window.__dtnsim_init_logged === true || document.body.innerText.includes('dtnsim_init rc');
    }, { timeout: 15000 }).catch(() => false);

    const gotPositions = await page.waitForFunction(() => {
      // The page updates a DOM status element and sets a global flag when positions are available
      return (typeof window.__dtnsim_positions_updated !== 'undefined' && window.__dtnsim_positions_updated === true) || document.body.innerText.includes('positions:');
    }, { timeout: 15000 }).catch(() => false);

    await page.screenshot({ path: 'puppeteer_page.png', fullPage: true });
    const canvas = await page.$('canvas'); if (canvas) await canvas.screenshot({ path: 'puppeteer_canvas.png' });

    console.log('RESULTS: init=', !!gotInit, 'positions=', !!gotPositions);
    console.log('Collected logs:', logs.slice(-50));
  } catch (e) {
    console.error('Test error:', e);
    await page.screenshot({ path: 'puppeteer_error.png', fullPage: true }).catch(()=>{});
    throw e;
  } finally {
    await browser.close();
    server.close();
  }
})();