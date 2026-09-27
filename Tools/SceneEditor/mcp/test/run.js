'use strict';

// Offline tests for the hotbite-editor MCP server: the real server.js over stdio,
// against a fake editor that answers the automation channel from canned
// responses and records every command it receives. No editor, no GPU.
//
//   node test/run.js        exit code = number of failures

const assert = require('assert');
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const SERVER = path.resolve(__dirname, '..', 'server.js');

// A 2x2 PNG, enough for the screenshot path to resize and re-encode.
const PNG_2x2 = Buffer.from(
	'iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAFklEQVR4nGP8z8DAwMDAxMDAwMDAAAANHQEDasKb6QAAAABJRU5ErkJggg==',
	'base64');

// ---------------------------------------------------------------- fake editor

class FakeEditor {
	constructor(dir) {
		this.dir = dir;
		this.received = [];
		this.camera = { position: [0, 5, -10], world_position: [0, 5, -10], target: [0, 0, 0], rotation_deg: [0, 0, 0], distance: 11.18 };
		this.timer = null;
	}

	start() {
		this.timer = setInterval(() => this.tick(), 15);
	}

	stop() {
		clearInterval(this.timer);
		this.timer = null;
	}

	tick() {
		const file = path.join(this.dir, 'command.txt');
		let body;
		try {
			body = fs.readFileSync(file, 'ascii');
			fs.unlinkSync(file);
		}
		catch (e) {
			return;
		}
		const out = [];
		for (const line of body.split(/\r?\n/).filter((l) => l !== '')) {
			this.received.push(line);
			out.push(`# ${line}`, ...this.answer(line));
		}
		const tmp = path.join(this.dir, 'response.pending');
		fs.writeFileSync(tmp, out.join('\r\n') + '\r\n');
		fs.renameSync(tmp, path.join(this.dir, 'response.txt'));
	}

	answer(line) {
		const tokens = line.match(/"[^"]*"|\S+/g).map((t) => t.replace(/"/g, ''));
		const [cmd, ...args] = tokens;
		switch (cmd) {
			case 'ping': return ['OK pong'];
			case 'state': return ['OK {"level":"test.json","entities":2}'];
			case 'camera': return ['OK ' + JSON.stringify(this.camera)];
			case 'camera_pos': this.camera.position = args.map(Number); return ['OK'];
			case 'camera_target': this.camera.target = args.map(Number); return ['OK'];
			case 'list_selection': return ['OK 0 selected'];
			case 'list_entities': return ['OK 2 entities', 'box1 id=1 pos=(0,0,0)', 'big rock id=2 pos=(1,2,3)'];
			case 'list_groups': return ['OK 0 groups'];
			case 'list_templates': return ['OK 1 templates', 'troll in=file'];
			case 'list_models': return ['OK 0 models'];
			case 'materials': return ['OK 1 materials', 'Stone file=level.mat users=1'];
			case 'lod_info': return ['OK nothing selected'];
			case 'select': return ['OK selected ' + args.join(' ')];
			case 'set_position': return ['OK'];
			case 'place': return [`OK placed ${args[0]}_0 at (0,0,0)`];
			case 'components':
				return args[0] === 'big rock'
					? ['OK 3 components on big rock', 'Base', 'Transform', 'GameThing']
					: [`ERR unknown entity '${args[0]}'`];
			case 'component':
				return args[1] === 'GameThing'
					? ['ERR GameThing is not linked into the editor']
					: [`OK {"component":"${args[1]}"}`];
			case 'screenshot':
				fs.writeFileSync(args[0], PNG_2x2);
				return [`OK saved ${args[0]}`];
			default: return [`ERR unknown command '${cmd}'`];
		}
	}
}

// ---------------------------------------------------------------- MCP client

class Client {
	constructor(dir, extra = []) {
		this.proc = spawn(process.execPath, [SERVER, '--dir', dir, ...extra], { stdio: ['pipe', 'pipe', 'pipe'] });
		this.nextId = 1;
		this.pending = new Map();
		this.stderr = '';
		let buffer = '';
		this.proc.stdout.on('data', (chunk) => {
			buffer += chunk.toString();
			let nl;
			while ((nl = buffer.indexOf('\n')) >= 0) {
				const line = buffer.slice(0, nl);
				buffer = buffer.slice(nl + 1);
				const msg = JSON.parse(line); // anything but JSON on stdout is itself a failure
				const p = this.pending.get(msg.id);
				if (p) {
					this.pending.delete(msg.id);
					p(msg);
				}
			}
		});
		this.proc.stderr.on('data', (c) => { this.stderr += c.toString(); });
	}

	request(method, params) {
		const id = this.nextId++;
		return new Promise((resolve) => {
			this.pending.set(id, resolve);
			this.proc.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
		});
	}

	notify(method, params) {
		this.proc.stdin.write(JSON.stringify({ jsonrpc: '2.0', method, params }) + '\n');
	}

	async call(name, args) {
		const msg = await this.request('tools/call', { name, arguments: args });
		assert.ok(msg.result, `tools/call ${name} returned a JSON-RPC error: ${JSON.stringify(msg.error)}`);
		return msg.result;
	}

	close() {
		this.proc.stdin.end();
	}
}

const textOf = (res) => res.content.filter((c) => c.type === 'text').map((c) => c.text).join('\n');

// ---------------------------------------------------------------- tests

async function main() {
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'hotbite-mcp-test-'));
	const editor = new FakeEditor(dir);
	editor.start();
	const client = new Client(dir);

	const tests = [];
	const test = (name, fn) => tests.push({ name, fn });

	test('initialize echoes a supported protocol version', async () => {
		const msg = await client.request('initialize', {
			protocolVersion: '2025-06-18', capabilities: {}, clientInfo: { name: 'test', version: '0' },
		});
		assert.strictEqual(msg.result.protocolVersion, '2025-06-18');
		assert.ok(msg.result.capabilities.tools);
		assert.ok(/editor_command/.test(msg.result.instructions));
		client.notify('notifications/initialized');
	});

	test('an unknown protocol version is answered with the newest supported one', async () => {
		const msg = await client.request('initialize', { protocolVersion: '1999-01-01', capabilities: {} });
		assert.strictEqual(msg.result.protocolVersion, '2025-06-18');
	});

	test('tools/list offers the read-only tools with schemas', async () => {
		const msg = await client.request('tools/list', {});
		const names = msg.result.tools.map((t) => t.name);
		for (const n of ['editor_launch', 'editor_status', 'scene_summary', 'screenshot', 'editor_query', 'entity_details', 'editor_command']) {
			assert.ok(names.includes(n), `missing ${n}`);
		}
		assert.ok(msg.result.tools.every((t) => t.inputSchema && t.inputSchema.type === 'object'));
	});

	test('unknown methods are a JSON-RPC error, not a crash', async () => {
		const msg = await client.request('resources/list', {});
		assert.strictEqual(msg.error.code, -32601);
	});

	test('scene_summary is one batch with every section', async () => {
		const before = editor.received.length;
		const res = await client.call('scene_summary', {});
		assert.strictEqual(res.isError, false);
		const t = textOf(res);
		for (const s of ['## Editor state', '## Entities', 'big rock id=2', '## Templates', 'troll in=file', '## Materials']) {
			assert.ok(t.includes(s), `summary lacks '${s}'`);
		}
		assert.deepStrictEqual(editor.received.slice(before),
			['state', 'camera', 'list_selection', 'list_entities', 'list_groups', 'list_templates', 'list_models', 'materials']);
	});

	test('arguments with spaces are quoted for the editor tokenizer', async () => {
		const before = editor.received.length;
		const res = await client.call('entity_details', { entity: 'big rock' });
		assert.strictEqual(res.isError, false);
		assert.strictEqual(editor.received[before], 'components "big rock"');
		assert.ok(editor.received.includes('component "big rock" Transform'));
		// An unlinkable game component is reported in place, not a failure.
		assert.ok(textOf(res).includes('## GameThing\npresent, no editable data (GameThing is not linked'));
		assert.ok(textOf(res).includes('## Transform\n{"component":"Transform"}'));
	});

	test('entity_details on an unknown entity is an error', async () => {
		const res = await client.call('entity_details', { entity: 'nope' });
		assert.strictEqual(res.isError, true);
	});

	test('a double quote or line break in an argument is refused before anything is sent', async () => {
		const before = editor.received.length;
		for (const entity of ['a"b', 'box1\nset_position 0 0 0']) {
			const res = await client.call('entity_details', { entity });
			assert.strictEqual(res.isError, true);
		}
		assert.strictEqual(editor.received.length, before);
	});

	test('editor_query runs read commands', async () => {
		const res = await client.call('editor_query', { commands: ['lod_info', 'camera'] });
		assert.strictEqual(res.isError, false);
		assert.ok(textOf(res).includes('nothing selected'));
	});

	test('editor_query refuses commands that write', async () => {
		const before = editor.received.length;
		const refused = [
			['set_position 0 1 0'],
			['render aa 0'],                       // reads with no args, writes with them
			['material_surface Stone on 16'],
			['lod_info', 'delete'],                // one bad entry refuses the batch
			['state\nquit'],
			['camera "x\ny"'],
		];
		for (const commands of refused) {
			const res = await client.call('editor_query', { commands });
			assert.strictEqual(res.isError, true, `${JSON.stringify(commands)} was accepted`);
		}
		assert.strictEqual(editor.received.length, before);
	});

	test('editor_command runs writing commands in one batch', async () => {
		const before = editor.received.length;
		const res = await client.call('editor_command', { commands: ['select box1', 'set_position 0 2 0', 'place tf_box view'] });
		assert.strictEqual(res.isError, false, textOf(res));
		assert.deepStrictEqual(editor.received.slice(before), ['select box1', 'set_position 0 2 0', 'place tf_box view']);
		assert.ok(textOf(res).includes('placed tf_box_0'));
	});

	test('editor_command reports an ERR as a tool error', async () => {
		const res = await client.call('editor_command', { commands: ['no_such_command'] });
		assert.strictEqual(res.isError, true);
	});

	test('editor_command refuses what would close or block the editor', async () => {
		const before = editor.received.length;
		for (const commands of [['quit'], ['debug_crash'], ['menu "File/Exit"'], ['menu "File/Open Level..."'],
			['select box1', 'menu "File/Import Model..."'], ['select a\nquit']]) {
			const res = await client.call('editor_command', { commands });
			assert.strictEqual(res.isError, true, `${JSON.stringify(commands)} was accepted`);
		}
		assert.strictEqual(editor.received.length, before);
	});

	test('--read-only leaves editor_command out entirely', async () => {
		const ro = new Client(dir, ['--read-only']);
		try {
			const init = await ro.request('initialize', { protocolVersion: '2025-06-18', capabilities: {} });
			assert.ok(/read-only/.test(init.result.instructions));
			const list = await ro.request('tools/list', {});
			const names = list.result.tools.map((t) => t.name);
			assert.ok(!names.includes('editor_command'));
			assert.ok(names.includes('scene_summary'));
			const res = await ro.call('editor_command', { commands: ['set_position 0 0 0'] });
			assert.strictEqual(res.isError, true);
		}
		finally {
			ro.close();
		}
	});

	test('--embedded leaves editor_launch out', async () => {
		const em = new Client(dir, ['--embedded']);
		try {
			const init = await em.request('initialize', { protocolVersion: '2025-06-18', capabilities: {} });
			assert.ok(/inside the editor/.test(init.result.instructions));
			const names = (await em.request('tools/list', {})).result.tools.map((t) => t.name);
			assert.ok(!names.includes('editor_launch'));
			assert.ok(names.includes('editor_command'));
			assert.strictEqual((await em.call('editor_launch', {})).isError, true);
		}
		finally {
			em.close();
		}
	});

	test('screenshot returns a JPEG and moves the camera first when asked', async () => {
		const before = editor.received.length;
		const res = await client.call('screenshot', { position: [1, 2, 3], target: [0, 1, 0], settle_frames: 2 });
		assert.strictEqual(res.isError, false, textOf(res));
		const img = res.content.find((c) => c.type === 'image');
		assert.strictEqual(img.mimeType, 'image/jpeg');
		assert.strictEqual(Buffer.from(img.data, 'base64').slice(0, 2).toString('hex'), 'ffd8');
		const sent = editor.received.slice(before);
		assert.deepStrictEqual(sent.slice(0, 2), ['camera_pos 1 2 3', 'camera_target 0 1 0']);
		assert.ok(sent.some((c) => c.startsWith('screenshot ')));
		// The temporary files are cleaned up.
		const shot = sent.find((c) => c.startsWith('screenshot ')).split(' ')[1];
		assert.ok(!fs.existsSync(shot));
	});

	test('concurrent calls do not trample each other', async () => {
		const [a, b, c] = await Promise.all([
			client.call('editor_status', {}),
			client.call('list_assets', { kind: 'templates' }),
			client.call('list_assets', { kind: 'materials' }),
		]);
		assert.ok(textOf(a).includes('test.json'));
		assert.ok(textOf(b).includes('troll'));
		assert.ok(textOf(c).includes('Stone'));
	});

	test('a silent editor is reported with a hint, and the batch is taken back', async () => {
		editor.stop();
		const res = await client.call('editor_status', {});
		assert.strictEqual(res.isError, true);
		assert.ok(/editor_launch/.test(textOf(res)), textOf(res));
		assert.ok(!fs.existsSync(path.join(dir, 'command.txt')), 'stale command.txt left behind');
		editor.start();
		const again = await client.call('editor_status', {});
		assert.strictEqual(again.isError, false);
	});

	let failures = 0;
	for (const t of tests) {
		try {
			await t.fn();
			console.log(`PASS ${t.name}`);
		}
		catch (e) {
			failures++;
			console.log(`FAIL ${t.name}\n     ${e && e.message ? e.message : e}`);
		}
	}
	client.close();
	editor.stop();
	fs.rmSync ? fs.rmSync(dir, { recursive: true, force: true }) : fs.rmdirSync(dir, { recursive: true });
	console.log(`\n${tests.length - failures}/${tests.length} passed`);
	if (failures && client.stderr) console.log(`server stderr:\n${client.stderr}`);
	process.exit(failures);
}

main();
