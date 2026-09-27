#!/usr/bin/env node
'use strict';

// hotbite-editor: an MCP server that gives an agent the HotBite Scene Editor's
// view of a level - entities, templates, models, materials, textures and the
// rendered viewport - through the editor's automation channel.
//
// MCP over stdio is newline-delimited JSON-RPC 2.0, implemented here directly
// rather than through @modelcontextprotocol/sdk: the SDK needs Node 18 and this
// machine runs 14, and the server only needs initialize / tools/list / tools/call.
// stdout carries protocol messages only; anything diagnostic goes to stderr.
//
//   node server.js [--dir <automation dir>]
//
// The automation dir defaults to HOTBITE_AUTOMATION_DIR, then
// %TEMP%\hotbite-editor. editor_launch starts an editor on that same dir.

const os = require('os');
const path = require('path');
const readline = require('readline');

const { Channel } = require('./src/channel');
const { toolsFor, callTool, makeContext } = require('./src/tools');

const SERVER_INFO = { name: 'hotbite-editor', version: '0.1.0' };
const PROTOCOL_VERSIONS = ['2025-06-18', '2025-03-26', '2024-11-05'];

const LAUNCH_HINT = 'If nothing answers, editor_launch starts the editor. ';
const EMBEDDED_HINT = 'You are running inside the editor, so it is always there; if it does not answer, it is busy ' +
	'(a long import or level load) - wait and retry. ';

const INSTRUCTIONS_COMMON =
	'Call scene_summary first; screenshot shows the rendered viewport. Asset layers: a model is an imported .fbx (meshes, materials, animation clips) and is not ' +
	'placeable; a template is one concept built from those assets and is the only placeable thing; ' +
	'an instance is a template placed in this level. Do not edit level, .mat or .tpl files on disk ' +
	'while the editor has them open - the editor holds the live copy and will overwrite them.';

const INSTRUCTIONS =
	'Tools for inspecting and editing a level open in the HotBite Scene Editor (C++/DirectX 11 engine). ' +
	'editor_command changes the level through the same automation channel the regression tests use; ' +
	'every edit is undoable and nothing reaches disk until a save command. Check the result of an edit ' +
	'with a read tool or a screenshot. ' + INSTRUCTIONS_COMMON;

const INSTRUCTIONS_READ_ONLY =
	'Tools for inspecting a level open in the HotBite Scene Editor (C++/DirectX 11 engine). ' +
	'This server is read-only: it never changes the level. ' + INSTRUCTIONS_COMMON;

function parseArgs(argv) {
	const out = {};
	for (let i = 0; i < argv.length; ++i) {
		if (argv[i] === '--dir' && i + 1 < argv.length) out.dir = argv[++i];
		else if (argv[i] === '--read-only') out.readOnly = true;
		else if (argv[i] === '--embedded') out.embedded = true;
	}
	return out;
}

function log(...args) {
	process.stderr.write(`[hotbite-editor] ${args.join(' ')}\n`);
}

function send(message) {
	process.stdout.write(JSON.stringify(message) + '\n');
}

function reply(id, resultValue) {
	send({ jsonrpc: '2.0', id, result: resultValue });
}

function fail(id, code, message) {
	send({ jsonrpc: '2.0', id, error: { code, message } });
}

async function handle(message, ctx) {
	const { id, method, params } = message;
	const isRequest = id !== undefined && id !== null;

	switch (method) {
		case 'initialize': {
			const asked = params && params.protocolVersion;
			reply(id, {
				protocolVersion: PROTOCOL_VERSIONS.includes(asked) ? asked : PROTOCOL_VERSIONS[0],
				capabilities: { tools: { listChanged: false } },
				serverInfo: SERVER_INFO,
				instructions: (ctx.config.embedded ? EMBEDDED_HINT : LAUNCH_HINT) +
					(ctx.config.readOnly ? INSTRUCTIONS_READ_ONLY : INSTRUCTIONS),
			});
			return;
		}
		case 'ping':
			reply(id, {});
			return;
		case 'tools/list':
			reply(id, {
				tools: toolsFor(ctx.config.readOnly, ctx.config.embedded).map(({ name, description, inputSchema, write }) => ({
					name, description, inputSchema, annotations: { readOnlyHint: !write, destructiveHint: !!write },
				})),
			});
			return;
		case 'tools/call': {
			const name = params && params.name;
			reply(id, await callTool(name, params && params.arguments, ctx));
			return;
		}
		default:
			// Notifications (initialized, cancelled, ...) need no answer.
			if (isRequest) fail(id, -32601, `method not found: ${method}`);
	}
}

function main() {
	const args = parseArgs(process.argv.slice(2));
	const dir = path.resolve(args.dir || process.env.HOTBITE_AUTOMATION_DIR || path.join(os.tmpdir(), 'hotbite-editor'));
	const ctx = makeContext(new Channel(dir), { readOnly: !!args.readOnly, embedded: !!args.embedded });
	log(`automation dir ${dir}${args.readOnly ? ' (read-only)' : ''}${args.embedded ? ' (embedded)' : ''}`);

	const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
	rl.on('line', (line) => {
		if (line.trim() === '') return;
		let message;
		try {
			message = JSON.parse(line);
		}
		catch (e) {
			fail(null, -32700, 'parse error');
			return;
		}
		// Requests run concurrently; the channel itself serializes what reaches the
		// editor.
		handle(message, ctx).catch((e) => {
			log(`unhandled: ${e && e.stack ? e.stack : e}`);
			if (message.id !== undefined) fail(message.id, -32603, String(e && e.message ? e.message : e));
		});
	});
	rl.on('close', () => process.exit(0));
}

main();
