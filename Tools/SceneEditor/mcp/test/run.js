'use strict';

// Offline tests for the hotbite-editor MCP server: the real server.js over stdio,
// against a fake editor that answers the automation channel from canned
// responses and records every command it receives. No editor, no GPU.
//
//   node test/run.js        exit code = number of failures

const assert = require('assert');
const { spawn } = require('child_process');
const fs = require('fs');
const http = require('http');
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
		this.modelFiles = {}; // name -> fake .fbx path, answered by model_info (meshy_retexture's lookup)
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
			case 'model_info': {
				const fbx = this.modelFiles && this.modelFiles[args[0]];
				return fbx ? [`OK model ${args[0]} from ${fbx}`] : [`ERR unknown model: ${args[0]}`];
			}
			case 'import_meshy_model':
				return [`OK Imported Meshy model '${args[1] || args[0]}' as a template (2 texture map(s), 0 LOD level(s))`];
			case 'import_texture':
				return [`OK ${args[1] || ''}/${path.basename(args[0])}`];
			case 'set_material_texture':
				return this.failSetMaterialTexture ? ['ERR simulated local failure'] : ['OK'];
			case 'meshy_setup_status':
				return ['OK open=false busy=false has_result=false'];
			default: return [`ERR unknown command '${cmd}'`];
		}
	}
}

// ---------------------------------------------------------------- MCP client

class Client {
	constructor(dir, extra = [], env = {}) {
		this.proc = spawn(process.execPath, [SERVER, '--dir', dir, ...extra],
			{ stdio: ['pipe', 'pipe', 'pipe'], env: { ...process.env, ...env } });
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

// ---------------------------------------------------------------- fake Meshy API

// Stands in for api.meshy.ai for the meshy_* tools: balance, task creation and
// polling, and serving the "downloaded" model/texture files - all on localhost, so
// the offline suite never makes a real network call or spends a real credit.
class FakeMeshy {
	constructor() {
		this.tasks = new Map();
		this.received = []; // {method, url}
		this.balance = { balance: 1000 };
		this.nextId = 1;
		this.server = http.createServer((req, res) => this.handle(req, res));
	}

	listen() {
		return new Promise((resolve) => {
			this.server.listen(0, '127.0.0.1', () => resolve(this.baseUrl()));
		});
	}

	baseUrl() {
		return `http://127.0.0.1:${this.server.address().port}`;
	}

	close() {
		return new Promise((resolve) => this.server.close(resolve));
	}

	handle(req, res) {
		let body = '';
		req.on('data', (c) => { body += c; });
		req.on('end', () => {
			this.received.push({ method: req.method, url: req.url });
			const send = (code, obj) => {
				res.writeHead(code, { 'Content-Type': 'application/json' });
				res.end(JSON.stringify(obj));
			};
			if (req.method === 'GET' && req.url === '/openapi/v1/balance') return send(200, this.balance);
			if (req.method === 'GET' && req.url.startsWith('/asset/')) {
				res.writeHead(200, { 'Content-Type': 'application/octet-stream' });
				res.end(Buffer.from('fake binary data for ' + req.url));
				return;
			}
			const create = /^\/openapi\/v[12]\/(text-to-3d|image-to-3d|retexture)$/.exec(req.url);
			if (req.method === 'POST' && create) {
				const payload = JSON.parse(body || '{}');
				const id = `${create[1]}-${this.nextId++}`;
				this.tasks.set(id, { kind: create[1], payload, polls: 0, ...(this.nextTask || {}) });
				this.nextTask = null;
				return send(200, { result: id });
			}
			const poll = /^\/openapi\/v[12]\/(?:text-to-3d|image-to-3d|retexture)\/([^/]+)$/.exec(req.url);
			if (req.method === 'GET' && poll) {
				const t = this.tasks.get(poll[1]);
				if (!t) return send(404, { message: 'unknown task' });
				t.polls++;
				if (t.polls < (t.doneAfterPolls || 1)) return send(200, { id: poll[1], status: 'IN_PROGRESS', progress: 50 });
				if (t.fail) return send(200, { id: poll[1], status: 'FAILED', task_error: { message: t.fail } });
				return send(200, {
					id: poll[1], status: 'SUCCEEDED', progress: 100,
					consumed_credits: t.consumedCredits != null ? t.consumedCredits : 20,
					model_urls: { fbx: `${this.baseUrl()}/asset/model.fbx` },
					texture_urls: [{ base_color: `${this.baseUrl()}/asset/diffuse.png`, normal: `${this.baseUrl()}/asset/normal.png` }],
				});
			}
			send(404, { message: `unhandled ${req.method} ${req.url}` });
		});
	}
}

const textOf = (res) => res.content.filter((c) => c.type === 'text').map((c) => c.text).join('\n');

// ---------------------------------------------------------------- tests

async function main() {
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'hotbite-mcp-test-'));
	const editor = new FakeEditor(dir);
	editor.start();
	const client = new Client(dir);

	// meshy_* tools: their own fake Meshy server, a throwaway config/ledger dir, and
	// a dedicated MCP client whose environment points at both, so nothing here ever
	// makes a real network call or touches real credits.
	const meshyDir = fs.mkdtempSync(path.join(os.tmpdir(), 'hotbite-meshy-test-'));
	fs.writeFileSync(path.join(meshyDir, 'config.json'), JSON.stringify({ api_key: 'fake-key-123' }));
	const fakeMeshy = new FakeMeshy();
	const meshyBase = await fakeMeshy.listen();
	const fakeFbx = path.join(meshyDir, 'crate.fbx');
	fs.writeFileSync(fakeFbx, 'not a real fbx, just needs to exist and be readable');
	editor.modelFiles.crate = fakeFbx;
	const meshyEnv = { HOTBITE_MESHY_DIR: meshyDir, HOTBITE_MESHY_API_BASE: meshyBase, HOTBITE_MESHY_POLL_MS: '5' };
	const meshyClient = new Client(dir, [], meshyEnv);

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
			['select box1', 'menu "File/Import Model..."'], ['select a\nquit'],
			// The Meshy API key setup popup: an agent must never be able to open it -
			// there is no automation command anywhere that accepts a key, and this is
			// the other half of that (see MeshySetup.h).
			['menu "View/Claude: Meshy API Key..."']]) {
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

	test('meshy_status reports the key is configured, and never prints it', async () => {
		const res = await meshyClient.call('meshy_status', {});
		assert.strictEqual(res.isError, false, textOf(res));
		const t = textOf(res);
		assert.ok(t.includes('Meshy key configured'), t);
		assert.ok(!t.includes('fake-key-123'), 'the raw key leaked into the tool result');
	});

	test('meshy_text_to_3d with no confirm_token only estimates - no paid endpoint is called', async () => {
		const before = fakeMeshy.received.length;
		const res = await meshyClient.call('meshy_text_to_3d', { prompt: 'a small wooden crate' });
		assert.strictEqual(res.isError, false, textOf(res));
		const t = textOf(res);
		assert.ok(/estimated \d+ credits/.test(t), t);
		assert.ok(/confirm_token="[0-9a-f]+"/.test(t), t);
		const hit = fakeMeshy.received.slice(before);
		assert.ok(hit.length > 0 && hit.every((r) => r.method === 'GET' && r.url === '/openapi/v1/balance'),
			`only the free balance check should run before approval: ${JSON.stringify(hit)}`);
	});

	test('confirming with an unknown token, or with changed arguments, is refused', async () => {
		let res = await meshyClient.call('meshy_text_to_3d', { prompt: 'a small wooden crate', confirm_token: 'not-a-real-token' });
		assert.strictEqual(res.isError, true);
		assert.ok(/no pending approval/.test(textOf(res)), textOf(res));

		const est = await meshyClient.call('meshy_text_to_3d', { prompt: 'a small wooden crate' });
		const token = /confirm_token="([0-9a-f]+)"/.exec(textOf(est))[1];
		res = await meshyClient.call('meshy_text_to_3d', { prompt: 'a completely different prompt', confirm_token: token });
		assert.strictEqual(res.isError, true);
		assert.ok(/arguments changed/.test(textOf(res)), textOf(res));
	});

	test('meshy_image_to_3d runs once confirmed, logs the real consumed_credits (flagging a mismatch), and imports a template', async () => {
		const est = await meshyClient.call('meshy_image_to_3d', { image_url: 'http://example.invalid/ref.png', name: 'crate' });
		assert.strictEqual(est.isError, false, textOf(est));
		assert.ok(/estimated 30 credits/.test(textOf(est)), textOf(est));
		const token = /confirm_token="([0-9a-f]+)"/.exec(textOf(est))[1];

		fakeMeshy.nextTask = { consumedCredits: 35 }; // deliberately not 30, to exercise the mismatch note
		const before = editor.received.length;
		const res = await meshyClient.call('meshy_image_to_3d',
			{ image_url: 'http://example.invalid/ref.png', name: 'crate', confirm_token: token });
		assert.strictEqual(res.isError, false, textOf(res));
		const t = textOf(res);
		assert.ok(/Charged 35 credits \(quoted 30/.test(t), t);
		assert.ok(editor.received.slice(before).some((c) => c.startsWith('import_meshy_model ')), JSON.stringify(editor.received.slice(before)));

		const ledger = await meshyClient.call('meshy_ledger', {});
		assert.ok(/image_to_3d 35cr/.test(textOf(ledger)), textOf(ledger));
	});

	test('meshy_retexture uploads the model as a data URI and applies the maps to the material, not the model itself', async () => {
		const est = await meshyClient.call('meshy_retexture', { model: 'crate', material: 'CrateMat', style_prompt: 'rusted metal' });
		assert.strictEqual(est.isError, false, textOf(est));
		const token = /confirm_token="([0-9a-f]+)"/.exec(textOf(est))[1];

		fakeMeshy.nextTask = { consumedCredits: 10 }; // matches the quote (no ai_model/8k given)
		const before = editor.received.length;
		const res = await meshyClient.call('meshy_retexture',
			{ model: 'crate', material: 'CrateMat', style_prompt: 'rusted metal', confirm_token: token });
		assert.strictEqual(res.isError, false, textOf(res));
		const t = textOf(res);
		assert.ok(/Applied diffuse, normal to material 'CrateMat'/.test(t), t);
		assert.ok(/Charged 10 credits \(matched the quote\)/.test(t), t);
		const sent = editor.received.slice(before);
		assert.ok(sent.some((c) => c.startsWith('import_texture ')), JSON.stringify(sent));
		assert.ok(sent.some((c) => c.startsWith('set_material_texture CrateMat diffuse')), JSON.stringify(sent));
		assert.ok(!sent.some((c) => c.startsWith('import_meshy_model')), 'retexture must not create a new model/template');

		const sentTask = [...fakeMeshy.tasks.values()].find((tk) => tk.kind === 'retexture');
		assert.ok(sentTask.payload.model_url.startsWith('data:application/octet-stream;base64,'), sentTask.payload.model_url.slice(0, 60));
	});

	test('a real charge is still recorded in the ledger even when applying the result locally fails', async () => {
		// Regression for the exact failure hit generating the terrain retexture for
		// real: Meshy had already run and charged for the task, but the ledger write
		// used to happen only after a successful local apply, so a purely local
		// failure (there: a wrong relative texture name) silently dropped the record
		// of a real, already-spent charge.
		const est = await meshyClient.call('meshy_retexture', { model: 'crate', material: 'CrateMat', style_prompt: 'rusted metal' });
		const token = /confirm_token="([0-9a-f]+)"/.exec(textOf(est))[1];

		editor.failSetMaterialTexture = true;
		fakeMeshy.nextTask = { consumedCredits: 12 };
		const res = await meshyClient.call('meshy_retexture',
			{ model: 'crate', material: 'CrateMat', style_prompt: 'rusted metal', confirm_token: token });
		editor.failSetMaterialTexture = false;

		assert.strictEqual(res.isError, true);
		const t = textOf(res);
		assert.ok(/charged 12 credits/.test(t), t);
		assert.ok(/downloaded files are still at/.test(t), t);

		const ledger = await meshyClient.call('meshy_ledger', {});
		assert.ok(/retexture 12cr/.test(textOf(ledger)), textOf(ledger));
	});

	test('a configured max_credits_per_task refuses the estimate outright', async () => {
		const configPath = path.join(meshyDir, 'config.json');
		fs.writeFileSync(configPath, JSON.stringify({ api_key: 'fake-key-123', max_credits_per_task: 1 }));
		try {
			const res = await meshyClient.call('meshy_text_to_3d', { prompt: 'something expensive' });
			assert.strictEqual(res.isError, true);
			assert.ok(/exceeds the configured max_credits_per_task/.test(textOf(res)), textOf(res));
		}
		finally {
			fs.writeFileSync(configPath, JSON.stringify({ api_key: 'fake-key-123' }));
		}
	});

	test('no configured Meshy key is a clear error, not a crash', async () => {
		const noKeyDir = fs.mkdtempSync(path.join(os.tmpdir(), 'hotbite-meshy-nokey-'));
		const noKeyClient = new Client(dir, [], { HOTBITE_MESHY_DIR: noKeyDir, HOTBITE_MESHY_API_BASE: meshyBase });
		try {
			const res = await noKeyClient.call('meshy_text_to_3d', { prompt: 'x' });
			assert.strictEqual(res.isError, true);
			assert.ok(/no Meshy API key configured/.test(textOf(res)), textOf(res));
		}
		finally {
			noKeyClient.close();
			fs.rmSync(noKeyDir, { recursive: true, force: true });
		}
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
	meshyClient.close();
	await fakeMeshy.close();
	editor.stop();
	fs.rmSync ? fs.rmSync(dir, { recursive: true, force: true }) : fs.rmdirSync(dir, { recursive: true });
	fs.rmSync(meshyDir, { recursive: true, force: true });
	console.log(`\n${tests.length - failures}/${tests.length} passed`);
	if (failures && client.stderr) console.log(`server stderr:\n${client.stderr}`);
	process.exit(failures);
}

main();
