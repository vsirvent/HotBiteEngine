'use strict';

// A scripted stand-in for Claude Code, for the Claude panel suite (38-agentpanel).
// It speaks the same headless protocol the panel starts `claude -p` with -
// stream-json on stdin and stdout - and edits the level the way the real agent does:
// through the automation channel named in the MCP config the panel wrote, using the
// MCP server's own channel client. So everything on the editor side is the real
// thing: the process launch, the agent's channel root, the event parsing, the
// transcript, the undo grouping, resume, stop and crash handling.
//
// What a message does:
//   run: <cmd> ; <cmd> ...   runs automation commands as an editor_command tool call
//   slow                     starts a tool call and waits until interrupted
//   crash                    exits with code 3 in the middle of the turn
//   anything else            replies "echo: <message>", streamed as deltas
//
// Every start writes its argv and a timestamp to fake-claude-starts.jsonl beside the
// MCP config, so a test can check what the panel launched it with.

const fs = require('fs');
const path = require('path');
const readline = require('readline');

const { Channel } = require(path.resolve(__dirname, '..', '..', '..', 'mcp', 'src', 'channel.js'));

const argv = process.argv.slice(2);
const arg = (name) => {
	const i = argv.indexOf(name);
	return i >= 0 && i + 1 < argv.length ? argv[i + 1] : null;
};

const mcpFile = arg('--mcp-config');
const mcp = JSON.parse(fs.readFileSync(mcpFile, 'utf8'));
const serverArgs = mcp.mcpServers['hotbite-editor'].args;
const channel = new Channel(serverArgs[serverArgs.indexOf('--dir') + 1]);

fs.appendFileSync(path.join(path.dirname(mcpFile), 'fake-claude-starts.jsonl'),
	JSON.stringify({ argv, cwd: process.cwd() }) + '\n');

const sessionId = arg('--resume') || `fake-${process.pid}-${Date.now()}`;
const model = arg('--model') || 'fake-model';
let cost = 0;
let pendingSlow = null;
let toolCounter = 0;

const emit = (obj) => process.stdout.write(JSON.stringify({ session_id: sessionId, ...obj }) + '\n');

emit({
	type: 'system', subtype: 'init', model, cwd: process.cwd(),
	tools: ['Read', 'Glob', 'Grep', 'mcp__hotbite-editor__editor_command'],
	mcp_servers: [{ name: 'hotbite-editor', status: 'connected' }],
});

function result(subtype = 'success', extra = {}) {
	cost += 0.01;
	emit({ type: 'result', subtype, is_error: subtype !== 'success', duration_ms: 5, total_cost_usd: cost, ...extra });
}

function say(text) {
	emit({ type: 'stream_event', event: { type: 'message_start' } });
	for (const piece of text.match(/.{1,6}/g) || []) {
		emit({ type: 'stream_event', event: { type: 'content_block_delta', delta: { type: 'text_delta', text: piece } } });
	}
	emit({ type: 'assistant', message: { role: 'assistant', content: [{ type: 'text', text }] } });
}

function toolUse(name, input) {
	const id = `toolu_fake_${++toolCounter}`;
	emit({ type: 'assistant', message: { role: 'assistant', content: [{ type: 'tool_use', id, name: `mcp__hotbite-editor__${name}`, input }] } });
	return id;
}

function toolResult(id, text, isError) {
	emit({ type: 'user', message: { role: 'user', content: [{ type: 'tool_result', tool_use_id: id, content: [{ type: 'text', text }], is_error: isError }] } });
}

async function handle(text) {
	if (text.startsWith('run:')) {
		const commands = text.slice(4).split(';').map((c) => c.trim()).filter(Boolean);
		const id = toolUse('editor_command', { commands });
		const blocks = await channel.send(commands);
		const failed = blocks.some((b) => b.status !== 'OK');
		toolResult(id, blocks.map((b) => `${b.status} ${b.head}`).join('\n'), failed);
		say(`ran ${commands.length} command(s)${failed ? ' with errors' : ''}`);
		result();
	}
	else if (text === 'slow') {
		toolUse('editor_status', {});
		pendingSlow = true;
	}
	else if (text === 'crash') {
		say('about to crash');
		process.stderr.write('fake-claude: crashing on purpose\n');
		process.exit(3);
	}
	else {
		say(`echo: ${text}`);
		result();
	}
}

const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
let queue = Promise.resolve();
rl.on('line', (line) => {
	if (!line.trim()) return;
	const msg = JSON.parse(line);
	if (msg.type === 'control_request' && msg.request && msg.request.subtype === 'interrupt') {
		emit({ type: 'control_response', response: { subtype: 'success', request_id: msg.request_id } });
		if (pendingSlow) {
			pendingSlow = null;
			result('error_during_execution');
		}
		return;
	}
	if (msg.type !== 'user') return;
	const content = msg.message.content;
	const text = Array.isArray(content) ? content.map((c) => c.text || '').join('') : String(content);
	queue = queue.then(() => handle(text)).catch((e) => {
		process.stderr.write(`fake-claude: ${e.stack || e}\n`);
		result('error_during_execution', { result: String(e.message || e) });
	});
});
rl.on('close', () => process.exit(0));
