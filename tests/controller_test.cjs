const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const script = fs.readFileSync(`${__dirname}/../src/index.html`, 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
function client(options = {}) {
    const nodes = {};
    const sockets = [];
    const intervals = new Map();
    const timeouts = [];
    const window = {location:{protocol:'http:',host:'ledworm.test:8080',replace(value){window.destination=value;}}, addEventListener(){}};
    window.confirm = options.confirm || (() => false);
    class WebSocket {
        static OPEN = 1;
        constructor(url) { this.url = url; this.readyState = 1; this.sent = []; sockets.push(this); }
        send(value) { this.sent.push(JSON.parse(value)); }
    }
    vm.runInNewContext(script, {
        window, WebSocket, fetch: options.fetch,
        document:{getElementById(id) {
            return nodes[id] ??= {style:{},disabled:true,textContent:'',value:'63',listeners:{},
                addEventListener(type, callback){this.listeners[type] = callback;}};
        }},
        setInterval(fn) { const id = intervals.size + 1; intervals.set(id,fn); return id; },
        clearInterval(id) { intervals.delete(id); },
        setTimeout(fn) { if (options.immediateTimers) fn(); else timeouts.push(fn); }
    });
    window.onload();
    const socket = sockets[0]; socket.onopen();
    const receive = (state) => socket.onmessage({data:JSON.stringify({type:'player_state', ...state})});
    return {nodes,sockets,socket,intervals,timeouts,receive,window};
}
const a = client(), b = client();
assert.equal(a.socket.url, 'ws://ledworm.test:8080/ws');
assert.equal(a.socket.sent[0].type, 'state');
a.receive({id:1,score:0,length:8,brightness:63,count:2});
b.receive({id:2,score:7,length:8,brightness:200,count:2});
assert.equal(a.nodes['worm-score'].textContent, 0);
assert.equal(b.nodes['worm-score'].textContent, 7);
assert.equal(a.nodes['worm-length'].textContent, 8);
assert.equal(a.nodes['value-slider'].disabled, false);
assert.equal(a.nodes['marquee-send'].disabled, false);
a.nodes['marquee-text'].value = 'HELLO 42!';
a.nodes['marquee-form'].onsubmit({preventDefault(){}});
assert.deepEqual(JSON.parse(JSON.stringify(a.socket.sent.at(-1))),
    {type:'marquee_text',text:'HELLO 42!'});
assert.match(a.nodes['marquee-status'].textContent, /angezeigt/);
a.nodes['marquee-text'].value = '';
a.nodes['marquee-form'].onsubmit({preventDefault(){}});
assert.deepEqual(JSON.parse(JSON.stringify(a.socket.sent.at(-1))),
    {type:'marquee_text',text:''});
a.nodes['marquee-text'].value = 'Grüße';
const beforeInvalidText = a.socket.sent.length;
a.nodes['marquee-form'].onsubmit({preventDefault(){}});
assert.equal(a.socket.sent.length, beforeInvalidText);
a.nodes['value-slider'].listeners.input({target:{value:'0'}});
assert.deepEqual(JSON.parse(JSON.stringify(a.socket.sent.at(-1))), {type:'value',value:0});
assert.equal(a.nodes['brightness-preview'].textContent, 0);
// Confirmed brightness only changes when the server acknowledges it.
assert.equal(a.nodes['worm-brightness'].textContent, '63 (25 %)');
a.receive({id:1,score:1,length:9,brightness:0,count:2});
assert.equal(a.nodes['worm-length'].textContent, 9);
assert.equal(b.nodes['worm-length'].textContent, 8);
assert.equal(a.nodes['worm-score'].textContent, 1);
assert.equal(b.nodes['worm-score'].textContent, 7);
assert.equal(a.nodes['worm-brightness'].textContent, '0 (0 %)');
assert.equal(b.nodes['worm-brightness'].textContent, '200 (78 %)');
assert.equal(b.socket.sent.length, 1);
a.socket.onmessage({data:'invalid json'});
a.intervals.values().next().value();
assert.equal(a.socket.sent.at(-1).type, 'state');
a.socket.readyState = 3; a.socket.onclose();
assert.equal(a.intervals.size, 0);
assert.equal(a.nodes['value-slider'].disabled, true);
assert.equal(a.nodes['marquee-send'].disabled, true);
assert.equal(a.nodes['worm-score'].textContent, '–');
assert.equal(a.nodes['worm-brightness'].textContent, '–');
const sentBefore = a.socket.sent.length;
a.nodes['value-slider'].listeners.input({target:{value:'255'}});
assert.equal(a.socket.sent.length, sentBefore);
a.timeouts[0]();
assert.equal(a.sockets.length, 2);
assert.equal(a.nodes['value-slider'].disabled, true);
console.log('PASS: per-client telemetry, numeric brightness commands, server confirmation, disconnect/reconnect');

const modeClient = client();
const modeState = {id:1,x:0,y:0,length:8,brightness:63,count:1};
modeClient.receive({...modeState, can_reset_wifi:false});
assert.equal(modeClient.nodes['wifi-reset-section'].hidden, false);
assert.equal(modeClient.nodes['wifi-reset'].disabled, false);
assert.equal(modeClient.nodes['wifi-reset'].textContent, 'WLAN-Auswahl');
modeClient.receive({...modeState, can_reset_wifi:true});
assert.equal(modeClient.nodes['wifi-reset-section'].hidden, false);
assert.equal(modeClient.nodes['wifi-reset'].disabled, false);
assert.equal(modeClient.nodes['wifi-reset'].textContent, 'WLAN zurücksetzen');
modeClient.socket.onclose();
assert.equal(modeClient.nodes['wifi-reset-section'].hidden, true);
assert.equal(modeClient.nodes['wifi-reset'].disabled, true);
console.log('PASS: Wi-Fi button selects setup offline, resets in station mode, hides on disconnect');

(async () => {
    const state = {id:1,x:0,y:0,length:8,brightness:63,count:1,can_reset_wifi:false};
    const selectionRequests = [];
    let polls = 0;
    const selection = client({confirm:()=>true, immediateTimers:true, fetch:async (url, options) => {
        selectionRequests.push({url,options});
        if (url === '/setup') return {ok:++polls >= 2};
        assert.equal(url, '/api/wifi/select');
        assert.equal(options.method, 'POST');
        return {ok:true};
    }});
    selection.receive(state);
    await selection.nodes['wifi-reset'].listeners.click();
    assert.equal(selection.window.destination, '/setup');
    assert.equal(selectionRequests.length, 3);
    assert.equal(selection.nodes['wifi-reset'].disabled, true);
    selection.socket.onclose();
    assert.equal(selection.sockets.length, 1);
    for (const fetch of [async()=>({ok:false}), async()=>{throw new Error('offline');}]) {
        const failedSelection = client({confirm:()=>true, immediateTimers:true, fetch});
        failedSelection.receive(state);
        await failedSelection.nodes['wifi-reset'].listeners.click();
        assert.equal(failedSelection.nodes['wifi-reset'].disabled, false);
        assert.match(failedSelection.nodes['wifi-reset-status'].textContent, /nicht bestätigt/);
    }
    console.log('PASS: offline WLAN selection, readiness retry, redirect and errors');
    let requests = 0;
    const cancelled = client({fetch: async () => { requests++; }});
    cancelled.nodes['wifi-reset'].disabled = false;
    await cancelled.nodes['wifi-reset'].listeners.click();
    assert.equal(requests, 0);

    const reset = client({confirm: () => true, fetch: async (url, options) => {
        requests++;
        assert.equal(url, '/api/wifi/reset');
        assert.equal(options.method, 'POST');
        assert.equal(options.headers['X-LedWorm-Reset'], 'confirm');
        return {ok:true};
    }});
    reset.nodes['wifi-reset'].disabled = false;
    await reset.nodes['wifi-reset'].listeners.click();
    assert.equal(requests, 1);
    assert.equal(reset.nodes['wifi-reset'].disabled, true);
    assert.match(reset.nodes['wifi-reset-status'].textContent, /LedWorm-Setup/);
    reset.socket.onclose();
    assert.equal(reset.timeouts.length, 0);

    for (const fetch of [async () => ({ok:false}), async () => { throw new Error('offline'); }]) {
        const failed = client({confirm: () => true, fetch});
        failed.nodes['wifi-reset'].disabled = false;
        await failed.nodes['wifi-reset'].listeners.click();
        assert.equal(failed.nodes['wifi-reset'].disabled, false);
        assert.match(failed.nodes['wifi-reset-status'].textContent, /nicht bestätigt/);
    }
    console.log('PASS: Wi-Fi reset cancellation, POST, success, disconnect and failures');
})().catch(error => { console.error(error); process.exitCode = 1; });
