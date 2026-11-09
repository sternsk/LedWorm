const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync(`${__dirname}/../src/wifi_setup.html`, 'utf8');
const elements = Object.fromEntries(['status', 'scan', 'setup', 'ssid', 'password', 'networks', 'save', 'offline'].map(id => [id, {value:'', disabled:false, textContent:'', replaceChildren(...children){this.children=children;}}]));
let requests = [];
let response = {ok:true, json:async()=>[]};
let destination;
let fetchError = false;
const initialize = () => vm.runInNewContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], {
  document: {querySelector: selector => elements[selector.slice(1)], createElement: () => ({})},
  TextEncoder,
  window: {location: {replace: value => { destination = value; }}},
  setTimeout: callback => callback(),
  fetch: async (url, options) => {
    requests.push({url, options});
    if (fetchError) throw new Error('offline');
    return typeof response === 'function' ? response(url) : response;
  }
});
initialize();
const submit = () => elements.setup.onsubmit({preventDefault(){}});
(async () => {
  elements.ssid.value = 'ü'.repeat(17); // 34 bytes despite only 17 characters
  await submit(); assert.equal(requests.length, 0);
  elements.ssid.value = ' home ';
  elements.password.value = 'short';
  await submit(); assert.equal(requests.length, 0);
  elements.password.value = '';
  await submit();
  assert.deepEqual(JSON.parse(requests[0].options.body), {ssid:' home ', password:''});
  assert.match(elements.status.textContent, /Gespeichert/);
  assert.equal(elements.offline.disabled, true);
  initialize(); // A new setup page, after the successful save/reboot.
  elements.save.disabled = false;
  elements.password.value = 'validpass';
  response = {ok:false, text:async()=> 'Storage failed'};
  await submit(); assert.equal(elements.save.disabled, false);
  assert.match(elements.status.textContent, /nicht bestätigt/);
  const hostile = '<img src=x onerror=alert(1)>';
  response = {ok:true, json:async()=>[hostile, hostile, '', 'Router']};
  await elements.scan.onclick();
  assert.equal(elements.networks.children.length, 2);
  assert.equal(elements.networks.children[0].value, hostile);
  assert.equal(elements.networks.children[0].innerHTML, undefined);
  response = {ok:false, text:async()=> 'Scan failed'};
  await elements.scan.onclick();
  assert.equal(elements.scan.disabled, false);
  assert.match(elements.status.textContent, /fehlgeschlagen/);

  response = {ok:false};
  await elements.offline.onclick();
  assert.match(elements.status.textContent, /nicht bestätigt/);
  assert.equal(elements.offline.disabled, false);
  assert.equal(elements.save.disabled, false);

  fetchError = true;
  await elements.offline.onclick();
  assert.equal(elements.offline.disabled, false);
  fetchError = false;

  // POST succeeds but the game server never becomes ready.
  response = url => ({ok:url === '/api/offline'});
  await elements.offline.onclick();
  assert.equal(destination, undefined);
  assert.equal(elements.offline.disabled, false);

  let polls = 0;
  response = url => {
    if (url === '/play') {
      polls++;
      if (polls === 1) throw new Error('server switching');
      return {ok:polls >= 3};
    }
    return {ok:true};
  };
  requests = [];
  await elements.offline.onclick();
  assert.equal(requests[0].url, '/api/offline');
  assert.equal(requests[0].options.method, 'POST');
  assert.equal(destination, '/play');
  assert.equal(polls, 3);
  assert.equal(elements.offline.disabled, true);
  assert.equal(elements.save.disabled, true);
  const count = requests.length;
  await elements.offline.onclick();
  await submit();
  assert.equal(requests.length, count);
  console.log('PASS: UTF-8 bounds, open network, storage/scan errors, SSID escaping');
  console.log('PASS: offline start, HTTP/network errors, readiness retry/timeout, duplicate suppression');
})().catch(error => { console.error(error); process.exitCode = 1; });
