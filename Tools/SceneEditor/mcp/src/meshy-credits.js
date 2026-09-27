'use strict';

// Everything about money and approval for Meshy generation, independent of the
// HTTP client (meshy-client.js) and the editor (meshy-tools.js). Three files live
// under one directory, %TEMP%\HotBiteMeshy by default (HOTBITE_MESHY_DIR overrides
// it, the same override pattern HOTBITE_AUTOMATION_DIR/HOTBITE_EDITOR_EXE use, and
// what the test suite points at a throwaway folder) - deliberately the OS temp
// folder, never the project or the engine repo, so a stray git add can never pick
// up an API key. See CLAUDE.md's note on secrets for the general rule this follows.
//
//   config.json  - {api_key, max_credits_per_task?, max_credits_per_day?}, written
//                   by setup-meshy-key.ps1, never by a tool (a key must never pass
//                   through the agent's own conversation - see mcp/README.md).
//   pending.json - outstanding "I quoted this, waiting on approval" tokens.
//   ledger.json  - append-only log of every task actually run and charged.

const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');

const PENDING_TTL_MS = 15 * 60 * 1000;

function dir() {
	return process.env.HOTBITE_MESHY_DIR || path.join(os.tmpdir(), 'HotBiteMeshy');
}
function configPath() { return path.join(dir(), 'config.json'); }
function pendingPath() { return path.join(dir(), 'pending.json'); }
function ledgerPath() { return path.join(dir(), 'ledger.json'); }

function readJson(p, fallback) {
	try {
		return JSON.parse(fs.readFileSync(p, 'utf8'));
	}
	catch (e) {
		return fallback;
	}
}

function writeJsonAtomic(p, value) {
	fs.mkdirSync(path.dirname(p), { recursive: true });
	const tmp = p + '.tmp';
	fs.writeFileSync(tmp, JSON.stringify(value, null, 2));
	fs.renameSync(tmp, p);
}

// ---------------------------------------------------------------- config

function loadConfig() {
	return readJson(configPath(), {});
}

// Never logged, never put in a tool's text result - see meshy-tools.js's
// meshy_status, which reports *whether* a key is configured but never the key.
function apiKey() {
	return loadConfig().api_key || '';
}

function caps() {
	const cfg = loadConfig();
	return {
		maxPerTask: cfg.max_credits_per_task || null,
		maxPerDay: cfg.max_credits_per_day || null,
	};
}

// ---------------------------------------------------------------- pricing (estimate only)

// Meshy's own published per-call pricing (docs.meshy.ai/en/api/pricing), current as
// of 2026-09. This is not a guess: for every one of these operations the price is
// fully determined by the request parameters, with no content-dependent variation -
// Meshy has no free "quote" endpoint (confirmed against their help center: cost is
// only ever known for certain as `consumed_credits` on a task that has already run
// and been charged), so a parameter-exact table lookup is the only way to show a
// number before spending anything. Whatever this returns is ONLY ever used for the
// pre-approval estimate text; meshy-tools.js always logs and reports the real
// `consumed_credits` Meshy's response carries afterward, and flags a mismatch
// against this table rather than trusting it silently.
const LITE_MODELS = new Set(['meshy-5', 'meshy-6-lite', 'meshy-t2']);
const isLite = (aiModel) => LITE_MODELS.has(aiModel);

const PRICING = {
	text_to_3d_preview(p) {
		return isLite(p.ai_model) ? 5 : 20;
	},
	text_to_3d_refine(p) {
		return p.texture_resolution === '8k' ? 15 : 10;
	},
	image_to_3d(p) {
		const lite = isLite(p.ai_model);
		if (p.should_texture === false) return lite ? 5 : 20;
		if (p.texture_resolution === '8k') return lite ? 20 : 35;
		return lite ? 15 : 30; // 2k or 4k
	},
	retexture(p) {
		return p.texture_resolution === '8k' ? 15 : 10;
	},
};
// text-to-3d is one user-facing action but two chargeable Meshy calls (a cheap
// mesh-only preview, then a pricier textured refine using its task id) - quoted
// and approved together since meshy-tools.js runs both under a single token.
PRICING.text_to_3d = (p) => PRICING.text_to_3d_preview(p) + PRICING.text_to_3d_refine(p);

function estimate(op, params) {
	const fn = PRICING[op];
	if (!fn) throw new Error(`no pricing entry for Meshy operation '${op}'`);
	return fn(params || {});
}

// ---------------------------------------------------------------- caps (hard backstop)

// Optional, unset by default - the confirm-token round trip is the gate the user
// chose; these are an extra, user-set ceiling that holds even if a token gets
// confirmed without a real human in the loop having looked at it.
function checkCaps(estimatedCredits) {
	const { maxPerTask, maxPerDay } = caps();
	if (maxPerTask && estimatedCredits > maxPerTask) {
		throw new Error(`refused: ${estimatedCredits} credits exceeds the configured max_credits_per_task ` +
			`(${maxPerTask}) in ${configPath()}`);
	}
	if (maxPerDay) {
		const since = Date.now() - 24 * 3600 * 1000;
		const spentToday = readJson(ledgerPath(), [])
			.filter((e) => new Date(e.time).getTime() >= since)
			.reduce((sum, e) => sum + (e.consumed_credits || 0), 0);
		if (spentToday + estimatedCredits > maxPerDay) {
			throw new Error(`refused: this would bring today's Meshy spend to ${spentToday + estimatedCredits} ` +
				`credits, over the configured max_credits_per_day (${maxPerDay}) in ${configPath()}`);
		}
	}
}

// ---------------------------------------------------------------- pending approvals

// confirm_token itself is excluded from the hash: the first (estimate) call has
// none and the second (confirm) call carries the one being validated, so hashing
// it would make every confirm call fail to match its own estimate.
function hashArgs(args) {
	const { confirm_token, ...rest } = args || {};
	return crypto.createHash('sha256').update(JSON.stringify(rest)).digest('hex');
}

function loadPending() {
	return readJson(pendingPath(), {});
}

function gcPending(map) {
	const now = Date.now();
	let changed = false;
	for (const [token, entry] of Object.entries(map)) {
		if (entry.expires_at <= now) {
			delete map[token];
			changed = true;
		}
	}
	return changed;
}

function createPendingApproval(op, args, estimatedCredits) {
	const map = loadPending();
	gcPending(map);
	const token = crypto.randomBytes(16).toString('hex');
	const now = Date.now();
	map[token] = {
		op,
		args_hash: hashArgs(args),
		estimated_credits: estimatedCredits,
		created_at: now,
		expires_at: now + PENDING_TTL_MS,
	};
	writeJsonAtomic(pendingPath(), map);
	return token;
}

// Validates and single-use-consumes a token: it must exist, not be expired, match
// this exact operation, and match the args it was issued for (so a token approved
// for one request can't be replayed against a request that was quietly edited
// afterward). Throws with a message meant to be shown to the agent/user as-is.
function consumePendingApproval(op, token, args) {
	const map = loadPending();
	const changed = gcPending(map);
	const entry = map[token];
	if (!entry) {
		if (changed) writeJsonAtomic(pendingPath(), map);
		throw new Error(`no pending approval for that token (unknown, already used, or expired after ` +
			`15 minutes) - call this tool again without confirm_token to get a fresh estimate`);
	}
	if (entry.op !== op) {
		throw new Error(`that token was issued for '${entry.op}', not '${op}'`);
	}
	if (entry.args_hash !== hashArgs(args)) {
		throw new Error('the request arguments changed since this token was issued - call again without ' +
			'confirm_token to re-estimate and get a token that matches');
	}
	delete map[token];
	writeJsonAtomic(pendingPath(), map);
	return entry;
}

// ---------------------------------------------------------------- ledger

function appendLedger(entry) {
	const list = readJson(ledgerPath(), []);
	list.push({ time: new Date().toISOString(), ...entry });
	writeJsonAtomic(ledgerPath(), list);
	return list[list.length - 1];
}

function readLedger(limit = 20) {
	return readJson(ledgerPath(), []).slice(-limit);
}

module.exports = {
	dir, configPath, pendingPath, ledgerPath,
	apiKey, caps,
	estimate, checkCaps,
	createPendingApproval, consumePendingApproval,
	appendLedger, readLedger,
};
