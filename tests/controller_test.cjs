const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const script = fs.readFileSync(`${__dirname}/../src/index.html`, 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
function client() {
    const nodes = {};
    const sockets = [];
    const intervals = new Map();
    const timeouts = [];
    const window = {location:{protocol:'http:',host:'ledworm.test:8080'}, addEventListener(){}};
    class WebSocket {
        static OPEN = 1;
        constructor(url) { this.url = url; this.readyState = 1; this.sent = []; sockets.push(this); }
        send(value) { this.sent.push(JSON.parse(value)); }
    }
    vm.runInNewContext(script, {
        window, WebSocket,
        document:{getElementById(id) {
            return nodes[id] ??= {style:{},disabled:true,textContent:'',value:'63',listeners:{},
                addEventListener(type, callback){this.listeners[type] = callback;}};
        }},
        setInterval(fn) { const id = intervals.size + 1; intervals.set(id,fn); return id; },
        clearInterval(id) { intervals.delete(id); },
        setTimeout(fn) { timeouts.push(fn); }
    });
    window.onload();
    const socket = sockets[0]; socket.onopen();
    const receive = (state) => socket.onmessage({data:JSON.stringify({type:'player_state', ...state})});
    return {nodes,sockets,socket,intervals,timeouts,receive};
}
const a = client(), b = client();
assert.equal(a.socket.url, 'ws://ledworm.test:8080/ws');
assert.equal(a.socket.sent[0].type, 'state');
a.receive({id:1,x:2.25,y:4.5,length:8,brightness:63,count:2});
b.receive({id:2,x:6,y:12,length:8,brightness:200,count:2});
assert.equal(a.nodes['worm-position'].textContent, '2.25 / 4.50');
assert.equal(a.nodes['worm-length'].textContent, 8);
assert.equal(a.nodes['value-slider'].disabled, false);
a.nodes['value-slider'].listeners.input({target:{value:'0'}});
assert.deepEqual(JSON.parse(JSON.stringify(a.socket.sent.at(-1))), {type:'value',value:0});
assert.equal(a.nodes['brightness-preview'].textContent, 0);
// Confirmed brightness only changes when the server acknowledges it.
assert.equal(a.nodes['worm-brightness'].textContent, '63 (25 %)');
a.receive({id:1,x:3,y:4.5,length:8,brightness:0,count:2});
assert.equal(a.nodes['worm-brightness'].textContent, '0 (0 %)');
assert.equal(b.nodes['worm-brightness'].textContent, '200 (78 %)');
assert.equal(b.socket.sent.length, 1);
a.socket.onmessage({data:'invalid json'});
a.intervals.values().next().value();
assert.equal(a.socket.sent.at(-1).type, 'state');
a.socket.readyState = 3; a.socket.onclose();
assert.equal(a.intervals.size, 0);
assert.equal(a.nodes['value-slider'].disabled, true);
assert.equal(a.nodes['worm-position'].textContent, '–');
assert.equal(a.nodes['worm-brightness'].textContent, '–');
const sentBefore = a.socket.sent.length;
a.nodes['value-slider'].listeners.input({target:{value:'255'}});
assert.equal(a.socket.sent.length, sentBefore);
a.timeouts[0]();
assert.equal(a.sockets.length, 2);
assert.equal(a.nodes['value-slider'].disabled, true);
console.log('PASS: per-client telemetry, numeric brightness commands, server confirmation, disconnect/reconnect');
