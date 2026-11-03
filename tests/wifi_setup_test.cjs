const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync(`${__dirname}/../src/wifi_setup.html`, 'utf8');
const elements = Object.fromEntries(['status', 'scan', 'setup', 'ssid', 'password', 'networks', 'save'].map(id => [id, {value:'', disabled:false, textContent:'', replaceChildren(...children){this.children=children;}}]));
let requests = [];
let response = {ok:true, json:async()=>[]};
vm.runInNewContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], {
  document: {querySelector: selector => elements[selector.slice(1)], createElement: () => ({})},
  TextEncoder,
  fetch: async (url, options) => { requests.push({url, options}); return response; }
});
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
  console.log('PASS: UTF-8 bounds, open network, storage/scan errors, SSID escaping');
})().catch(error => { console.error(error); process.exitCode = 1; });
