'use strict';

// Client for the Scene Editor's file-based automation channel
// (Tools/SceneEditor/automation/README.md): write <dir>/command.txt, one command
// per line, and wait for the editor to answer in <dir>/response.txt at the end of
// the frame it ran them in.
//
// The channel holds one batch at a time, so every send goes through one queue.
// Two concurrent MCP calls would otherwise overwrite each other's command.txt and
// both read whichever response landed.

const fs = require('fs');
const path = require('path');

const POLL_MS = 50;

class ChannelError extends Error {}

// One argument, quoted the way EditorAutomation's Tokenize reads it: a double
// quote toggles quoting and is stripped, whitespace splits outside quotes. There
// is no escape, so an argument can never carry a double quote - and a newline
// would end the line and start a second command, which for a read-only tool
// surface is exactly the thing that must not be possible.
function quoteArg(arg) {
	const s = String(arg);
	if (/["\r\n]/.test(s)) {
		throw new ChannelError(`argument may not contain a double quote or a line break: ${JSON.stringify(s)}`);
	}
	return s === '' || /[\s]/.test(s) ? `"${s}"` : s;
}

function formatCommand(name, args = []) {
	return [quoteArg(name), ...args.map(quoteArg)].join(' ');
}

// "# <command>" opens a block; every line up to the next one belongs to it. The
// first of those is the command's own OK/ERR line.
function parseResponse(text) {
	const blocks = [];
	let current = null;
	for (const raw of text.split(/\r?\n/)) {
		if (raw.startsWith('# ')) {
			current = { command: raw.slice(2), status: null, head: '', lines: [] };
			blocks.push(current);
		}
		else if (current !== null) {
			if (current.status === null && (raw.startsWith('OK') || raw.startsWith('ERR'))) {
				current.status = raw.startsWith('OK') ? 'OK' : 'ERR';
				current.head = raw.replace(/^(OK|ERR)\s?/, '');
			}
			else if (raw !== '') {
				current.lines.push(raw);
			}
		}
	}
	return blocks;
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

class Channel {
	constructor(dir) {
		this.dir = dir;
		this.queue = Promise.resolve();
	}

	get commandFile() { return path.join(this.dir, 'command.txt'); }
	get responseFile() { return path.join(this.dir, 'response.txt'); }
	get crashFile() { return path.join(this.dir, 'crash.txt'); }

	// `commands` are full command lines (see formatCommand). Resolves to the parsed
	// blocks, one per command, in order.
	send(commands, { timeoutMs = 30000 } = {}) {
		const run = () => this._send(commands, timeoutMs);
		const result = this.queue.then(run, run);
		this.queue = result.catch(() => {});
		return result;
	}

	async _send(commands, timeoutMs) {
		fs.mkdirSync(this.dir, { recursive: true });
		const started = Date.now();
		const deadline = started + timeoutMs;

		// A command file still sitting there means the editor has not consumed the
		// previous batch - it is not running, or it is stuck inside a long frame.
		while (fs.existsSync(this.commandFile)) {
			if (Date.now() > deadline) {
				throw new ChannelError(this._notAnswering('an earlier command was never picked up', started));
			}
			await sleep(POLL_MS);
		}

		try { fs.unlinkSync(this.responseFile); } catch (e) { /* not there */ }

		// Write-then-rename, as editor-cli.ps1 does, so the editor never reads half a
		// batch. ASCII only: a BOM would make the first command fail to parse.
		const pending = path.join(this.dir, 'command.pending');
		fs.writeFileSync(pending, commands.join('\r\n') + '\r\n', { encoding: 'ascii' });
		fs.renameSync(pending, this.commandFile);

		while (Date.now() <= deadline) {
			await sleep(POLL_MS);
			let text;
			try {
				text = fs.readFileSync(this.responseFile, 'utf8');
			}
			catch (e) {
				continue;
			}
			return parseResponse(text);
		}

		// Nobody answered: take the batch back so it cannot fire later, out of order,
		// into whatever the next call sends.
		try { fs.unlinkSync(this.commandFile); } catch (e) { /* consumed after all */ }
		throw new ChannelError(this._notAnswering(`no response within ${Math.round(timeoutMs / 1000)}s`, started));
	}

	_notAnswering(what, since) {
		let message = `The Scene Editor did not answer (${what}). Is it running with --automation "${this.dir}"? ` +
			'Use editor_launch to start it.';
		try {
			const stat = fs.statSync(this.crashFile);
			if (stat.mtimeMs >= since - 60000) {
				message += '\nThe editor crashed; crash.txt says:\n' + fs.readFileSync(this.crashFile, 'utf8').slice(0, 4000);
			}
		}
		catch (e) { /* no crash report */ }
		return message;
	}
}

module.exports = { Channel, ChannelError, formatCommand, parseResponse, quoteArg };
