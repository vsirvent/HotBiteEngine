'use strict';

// MCP tools that let the agent generate models and textures through Meshy AI,
// gated behind a two-call "estimate, then confirm" pattern (see meshy-credits.js
// for the token/ledger mechanics and CLAUDE.md for the design rationale). Merged
// into tools.js's TOOLS array.
//
// A generated asset reaches the level through the automation commands
// MeshyImport already exposes - import_meshy_model for a brand-new model
// (text-to-3d/image-to-3d), import_texture + set_material_texture for retexturing
// something already in the scene - so none of this touches the editor's C++ at
// all; it only downloads files into the shape those commands already expect
// (meshy-client.js's downloadResult, matching MeshyImport.h's ClassifySuffix).

const fs = require('fs');
const path = require('path');

const { formatCommand } = require('./channel');
const meshyClient = require('./meshy-client');
const credits = require('./meshy-credits');

const text = (t) => ({ type: 'text', text: t });
const result = (content, isError = false) => ({ content: Array.isArray(content) ? content : [content], isError });

function slugify(s) {
	return String(s).toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '').slice(0, 40) || 'meshy_asset';
}

// Meshy's own examples always show the mime type matching the actual file
// (image/jpeg for a .jpg, image/png for a .png) - unlike a model data URI, which
// Meshy expects labeled application/octet-stream regardless of which of its five
// supported formats it actually is (see meshy_retexture below). Defaults to png,
// the common case and Meshy's own default output format.
function imageMimeType(filePath) {
	const ext = path.extname(filePath).toLowerCase();
	return (ext === '.jpg' || ext === '.jpeg') ? 'image/jpeg' : 'image/png';
}

function downloadDir(token) {
	const d = path.join(credits.dir(), 'downloads', token);
	fs.mkdirSync(d, { recursive: true });
	return d;
}

// Spliced into a generation's final reply and its ledger line. The mismatch branch
// is how a stale entry in meshy-credits.js's pricing table is meant to surface -
// see that file's header comment.
function chargeNote(estimated, consumed) {
	if (consumed == null) return `quoted ${estimated} credits (Meshy did not report consumed_credits)`;
	if (consumed === estimated) return `${consumed} credits (matched the quote)`;
	return `${consumed} credits (quoted ${estimated} - meshy-credits.js's pricing table is out of date, update it)`;
}

// ---------------------------------------------------------------- the approval gate

// Shared by every credit-consuming tool. With no confirm_token: quotes the cost and
// stops, having sent nothing to Meshy's paid endpoints. With a valid, matching
// token: consumes it (single-use) and returns for the caller to do the real work.
// Throws (as a user-facing message) for a missing key, an expired/mismatched
// token, or a configured spending cap being exceeded.
async function gate(op, args) {
	const key = credits.apiKey();
	if (!key) {
		throw new Error('no Meshy API key configured - run Tools/SceneEditor/mcp/setup-meshy-key.ps1 once ' +
			`(it writes only ${credits.configPath()}; the key must never be pasted into this chat)`);
	}
	if (!args.confirm_token) {
		const estimated = credits.estimate(op, args);
		credits.checkCaps(estimated); // fail before ever handing out a token for a request that could never be approved anyway
		let balanceNote = '';
		try {
			const bal = await meshyClient.getBalance(key);
			balanceNote = ` Current Meshy balance: ${JSON.stringify(bal)}.`;
		}
		catch (e) {
			balanceNote = ` (could not check the current balance: ${e.message})`;
		}
		const token = credits.createPendingApproval(op, args, estimated);
		return {
			pending: true,
			message: `This would submit a Meshy ${op} task for an estimated ${estimated} credits - Meshy's ` +
				`published price for these exact parameters.${balanceNote} Nothing has been sent to Meshy yet. ` +
				'State this cost to the user and stop; do not call this tool again in this same turn. Only ' +
				'after the user explicitly approves in a later message, call this same tool again with the ' +
				`identical arguments plus confirm_token="${token}" (valid for 15 minutes) to actually run it.`,
		};
	}
	const entry = credits.consumePendingApproval(op, args.confirm_token, args);
	credits.checkCaps(entry.estimated_credits);
	return { pending: false, estimated: entry.estimated_credits, key };
}

// ---------------------------------------------------------------- editor plumbing

async function importAsTemplate(ctx, dir, name, lodArg) {
	const cmdArgs = lodArg ? [dir, name, lodArg] : [dir, name];
	const [block] = await ctx.channel.send([formatCommand('import_meshy_model', cmdArgs)], { timeoutMs: 120000 });
	if (block.status !== 'OK') throw new Error(`import_meshy_model failed: ${block.head}`);
	return block.head; // the editor's own status_message, for the reply text
}

async function resolveModelFbxPath(ctx, model) {
	const [block] = await ctx.channel.send([formatCommand('model_info', [model])]);
	if (block.status !== 'OK') throw new Error(`unknown model '${model}': ${block.head}`);
	const m = /^model \S+ from (.+)$/.exec(block.head);
	if (!m) throw new Error(`could not read the model's file path from: ${block.head}`);
	return m[1];
}

// The suffix meshyClient.downloadResult wrote (MeshyImport's own vocabulary) to the
// slot set_material_texture takes (EditorAutomation.cpp's own, smaller vocabulary -
// no separate metallic/roughness slot, hence both collapsing onto "specular" is
// already done by downloadResult before this ever sees a file).
const SLOT_FOR_SUFFIX = {
	normal: 'normal', ao: 'ao', emissive: 'emission', height: 'height',
	opacity: 'opacity', specular: 'specular',
};

// Applies the maps downloadResult wrote onto an existing material: import_texture
// registers each file under Assets/Textures (namespaced under the material's own
// name, like PolyHaven's per-asset subfolder), set_material_texture points the
// slot at it. Returns the slots actually applied.
async function applyMapsToMaterial(ctx, files, stem, material) {
	const applied = [];
	for (const file of files) {
		const base = path.basename(file, path.extname(file));
		const isDiffuse = base === stem;
		const suffix = !isDiffuse && base.startsWith(`${stem}_`) ? base.slice(stem.length + 1) : null;
		const slot = isDiffuse ? 'diffuse' : SLOT_FOR_SUFFIX[suffix];
		if (!slot) continue;
		const [imp] = await ctx.channel.send([formatCommand('import_texture', [file, material])]);
		if (imp.status !== 'OK') throw new Error(`import_texture failed for ${file}: ${imp.head}`);
		// import_texture's OK line is the destination's *absolute* path
		// (MaterialOps::ImportTexture's out_path - not project-relative), but
		// set_material_texture takes the name the way MaterialOps::ListTextures
		// reports it: relative to Assets/Textures, native separators. We already
		// know both halves of that (the subfolder we asked for, `material`, and
		// the file's own name), so build it directly instead of trying to
		// reparse the absolute path back into it.
		const relativeName = `${material}\\${path.basename(file)}`;
		const [set] = await ctx.channel.send([formatCommand('set_material_texture', [material, slot, relativeName])]);
		if (set.status !== 'OK') throw new Error(`set_material_texture ${slot} failed: ${set.head}`);
		applied.push(slot);
	}
	return applied;
}

// ---------------------------------------------------------------- tools

const CONFIRM_TOKEN_PROP = {
	confirm_token: {
		type: 'string',
		description: 'From a prior call with identical other arguments. Supplying it actually runs the ' +
			'generation and spends credits; omitting it only returns a cost estimate.',
	},
};

const MESHY_TOOLS = [
	{
		name: 'meshy_status',
		description: 'Whether a Meshy API key is configured (never reveals it), the account credit balance, ' +
			'any configured spending caps, and the most recent local ledger entries.',
		inputSchema: { type: 'object', properties: {}, additionalProperties: false },
		async handler() {
			const key = credits.apiKey();
			if (!key) {
				return result(text('No Meshy API key configured. Run Tools/SceneEditor/mcp/setup-meshy-key.ps1 ' +
					`once (it writes only ${credits.configPath()}; the key must never be pasted into this chat).`));
			}
			let balanceText;
			try {
				balanceText = JSON.stringify(await meshyClient.getBalance(key));
			}
			catch (e) {
				return result(text(`Meshy key is configured but the balance check failed: ${e.message}`), true);
			}
			const { maxPerTask, maxPerDay } = credits.caps();
			const ledger = credits.readLedger(5);
			const ledgerText = ledger.length
				? ledger.map((l) => `${l.time} ${l.op} ${l.consumed_credits}cr`).join('\n')
				: '(none yet)';
			return result(text(
				`Meshy key configured. Balance: ${balanceText}.\n` +
				`Caps: max_credits_per_task=${maxPerTask || 'none'}, max_credits_per_day=${maxPerDay || 'none'} ` +
				`(set in ${credits.configPath()}).\n` +
				`Recent ledger entries:\n${ledgerText}`));
		},
	},
	{
		name: 'meshy_ledger',
		description: 'The local record of every Meshy generation actually run and charged: timestamp, ' +
			'operation, and the real credits Meshy consumed.',
		inputSchema: {
			type: 'object',
			properties: { limit: { type: 'integer', minimum: 1, maximum: 200, description: 'Defaults to 20.' } },
			additionalProperties: false,
		},
		async handler(args) {
			const ledger = credits.readLedger(args.limit || 20);
			if (ledger.length === 0) return result(text('No Meshy generations recorded yet.'));
			return result(text(ledger.map((l) =>
				`${l.time} ${l.op} ${l.consumed_credits}cr${l.name ? ` (${l.name})` : ''}${l.note ? ` - ${l.note}` : ''}`
			).join('\n')));
		},
	},
	{
		name: 'meshy_text_to_3d',
		write: true,
		description: 'Generates a brand-new 3D model from a text prompt via Meshy AI (your Pro account - ' +
			'spends real credits) and imports it as a placeable template. Costs credits: call once without ' +
			'confirm_token to see the estimate (nothing is sent to Meshy yet); only after the user explicitly ' +
			'approves in a later message, call again with the same arguments plus confirm_token to actually ' +
			'generate it. Use editor_command\'s `place` afterward to put the resulting template in the scene.',
		inputSchema: {
			type: 'object',
			properties: {
				prompt: { type: 'string', maxLength: 800, description: 'What to generate, e.g. "a weathered wooden treasure chest with iron bands".' },
				name: { type: 'string', description: 'Registry name for the model/material/template. Defaults to a slug of the prompt.' },
				ai_model: { type: 'string', enum: ['meshy-6-lite', 'meshy-6', 'meshy-7', 'meshy-7.1', 'latest'], description: 'Defaults to "latest". The -lite model is cheaper.' },
				art_style: { type: 'string', description: 'e.g. "realistic", "cartoon", "low-poly".' },
				texture_resolution: { type: 'string', enum: ['2k', '4k', '8k'], description: 'Defaults to 2k.' },
				...CONFIRM_TOKEN_PROP,
			},
			required: ['prompt'],
			additionalProperties: false,
		},
		async handler(args, ctx) {
			const g = await gate('text_to_3d', args);
			if (g.pending) return result(text(g.message));

			const name = args.name || slugify(args.prompt);
			const previewParams = { prompt: args.prompt, ai_model: args.ai_model, art_style: args.art_style };
			const previewId = await meshyClient.createTextTo3DPreview(g.key, previewParams);
			const preview = await meshyClient.pollTask(g.key, 'text_to_3d', previewId, { timeoutMs: 300000 });
			if (preview.status !== 'SUCCEEDED') {
				return result(text(`Meshy's preview stage did not succeed (${preview.status}): ` +
					`${(preview.task_error && preview.task_error.message) || ''}`), true);
			}
			const refineParams = { texture_resolution: args.texture_resolution, target_formats: ['fbx'] };
			const refineId = await meshyClient.createTextTo3DRefine(g.key, previewId, refineParams);
			const refined = await meshyClient.pollTask(g.key, 'text_to_3d', refineId, { timeoutMs: 300000 });
			if (refined.status !== 'SUCCEEDED') {
				return result(text(`Meshy's refine stage did not succeed (${refined.status}): ` +
					`${(refined.task_error && refined.task_error.message) || ''}`), true);
			}

			// Logged *before* download/import, not after: Meshy has already run and
			// charged for this at this point regardless of what happens next, and a
			// ledger write that waited until the end used to mean a local-only
			// failure (a bad path, a naming collision) silently dropped the record of
			// a real spend - see CLAUDE.md's note on this.
			const consumed = (preview.consumed_credits || 0) + (refined.consumed_credits || 0);
			credits.appendLedger({ op: 'text_to_3d', name, consumed_credits: consumed, note: chargeNote(g.estimated, consumed) });
			const dir = downloadDir(refineId);
			try {
				const download = await meshyClient.downloadResult(refined, dir, name);
				const status = await importAsTemplate(ctx, download.dir, name);
				return result(text(`Generated and imported '${name}' as a template. ${status}\n` +
					`Charged ${chargeNote(g.estimated, consumed)}.`));
			}
			catch (e) {
				return result(text(`Meshy generated this and charged ${chargeNote(g.estimated, consumed)} (already in ` +
					`meshy_ledger), but importing the result locally failed: ${e.message}\nThe downloaded files are ` +
					`still at ${dir} - retry import_meshy_model on that folder rather than generating again.`), true);
			}
		},
	},
	{
		name: 'meshy_image_to_3d',
		write: true,
		description: 'Generates a brand-new 3D model from a reference image via Meshy AI (your Pro account - ' +
			'spends real credits) and imports it as a placeable template. Same two-call estimate/confirm pattern ' +
			'as meshy_text_to_3d.',
		inputSchema: {
			type: 'object',
			properties: {
				image_url: { type: 'string', description: 'A publicly reachable image URL. Use this or image_path, not both.' },
				image_path: { type: 'string', description: 'A local file path (e.g. a project asset) to read and upload. Use this or image_url, not both.' },
				name: { type: 'string', description: 'Registry name for the model/material/template. Defaults to the image file stem, or "meshy_asset".' },
				ai_model: { type: 'string', enum: ['meshy-6-lite', 'meshy-6', 'meshy-7', 'meshy-7.1', 'latest'] },
				should_texture: { type: 'boolean', description: 'Defaults to true. false generates mesh only, cheaper.' },
				texture_resolution: { type: 'string', enum: ['2k', '4k', '8k'], description: 'Defaults to 2k. Ignored when should_texture is false.' },
				...CONFIRM_TOKEN_PROP,
			},
			additionalProperties: false,
		},
		async handler(args, ctx) {
			if (!args.image_url && !args.image_path) {
				return result(text('image_to_3d needs image_url or image_path.'), true);
			}
			const g = await gate('image_to_3d', args);
			if (g.pending) return result(text(g.message));

			const name = args.name || slugify(args.image_path
				? path.basename(args.image_path, path.extname(args.image_path)) : 'meshy_asset');
			const params = {
				image_url: args.image_url || meshyClient.fileToDataUri(args.image_path, imageMimeType(args.image_path)),
				ai_model: args.ai_model,
				should_texture: args.should_texture,
				texture_resolution: args.texture_resolution,
				target_formats: ['fbx'],
			};
			const taskId = await meshyClient.createImageTo3D(g.key, params);
			const task = await meshyClient.pollTask(g.key, 'image_to_3d', taskId, { timeoutMs: 300000 });
			if (task.status !== 'SUCCEEDED') {
				return result(text(`Meshy did not succeed (${task.status}): ${(task.task_error && task.task_error.message) || ''}`), true);
			}

			// Logged before download/import - see the same note in meshy_text_to_3d.
			const consumed = task.consumed_credits || 0;
			credits.appendLedger({ op: 'image_to_3d', name, consumed_credits: consumed, note: chargeNote(g.estimated, consumed) });
			const dir = downloadDir(taskId);
			try {
				const download = await meshyClient.downloadResult(task, dir, name);
				const status = await importAsTemplate(ctx, download.dir, name);
				return result(text(`Generated and imported '${name}' as a template. ${status}\n` +
					`Charged ${chargeNote(g.estimated, consumed)}.`));
			}
			catch (e) {
				return result(text(`Meshy generated this and charged ${chargeNote(g.estimated, consumed)} (already in ` +
					`meshy_ledger), but importing the result locally failed: ${e.message}\nThe downloaded files are ` +
					`still at ${dir} - retry import_meshy_model on that folder rather than generating again.`), true);
			}
		},
	},
	{
		name: 'meshy_retexture',
		write: true,
		description: 'Generates new PBR texture maps for a model already in this project via Meshy AI (your ' +
			'Pro account - spends real credits) from a text style prompt, and applies them to a material ' +
			'already in the level. Same two-call estimate/confirm pattern as meshy_text_to_3d. Look the model ' +
			'name up with list_assets (kind=models) first.',
		inputSchema: {
			type: 'object',
			properties: {
				model: { type: 'string', description: 'An imported model (.fbx), as listed by list_assets kind=models - its file is uploaded as the mesh to retexture.' },
				material: { type: 'string', description: 'The material (list_assets kind=materials) to apply the generated maps to.' },
				style_prompt: { type: 'string', maxLength: 800, description: 'The desired look, e.g. "rusted, weathered steel with peeling paint".' },
				ai_model: { type: 'string', enum: ['meshy-6-lite', 'meshy-6', 'meshy-7', 'latest'] },
				texture_resolution: { type: 'string', enum: ['2k', '4k', '8k'], description: 'Defaults to 2k.' },
				...CONFIRM_TOKEN_PROP,
			},
			required: ['model', 'material', 'style_prompt'],
			additionalProperties: false,
		},
		async handler(args, ctx) {
			const g = await gate('retexture', args);
			if (g.pending) return result(text(g.message));

			const fbxPath = await resolveModelFbxPath(ctx, args.model);
			const params = {
				// Meshy expects this labeled application/octet-stream for every one of
				// its five supported model formats via data URI - not a format-specific
				// mime type (docs.meshy.ai/en/api/retexture; getting this wrong reads
				// back as "invalid model file content", not a mime/format error).
				model_url: meshyClient.fileToDataUri(fbxPath, 'application/octet-stream'),
				text_style_prompt: args.style_prompt,
				ai_model: args.ai_model,
				texture_resolution: args.texture_resolution,
				target_formats: ['fbx'],
			};
			const taskId = await meshyClient.createRetexture(g.key, params);
			const task = await meshyClient.pollTask(g.key, 'retexture', taskId, { timeoutMs: 300000 });
			if (task.status !== 'SUCCEEDED') {
				return result(text(`Meshy did not succeed (${task.status}): ${(task.task_error && task.task_error.message) || ''}`), true);
			}

			// Logged before download/apply - see the same note in meshy_text_to_3d.
			const consumed = task.consumed_credits || 0;
			credits.appendLedger({
				op: 'retexture', name: args.material, consumed_credits: consumed,
				note: chargeNote(g.estimated, consumed),
			});
			const dir = downloadDir(taskId);
			const stem = path.basename(fbxPath, path.extname(fbxPath));
			try {
				const download = await meshyClient.downloadResult(task, dir, stem);
				// Only the texture maps go onto the material - the retextured .fbx
				// itself (whose basename is also `stem`, same as the diffuse map's) is not one.
				const maps = download.files.filter((f) => f !== download.fbx);
				const applied = await applyMapsToMaterial(ctx, maps, stem, args.material);
				return result(text(`Applied ${applied.join(', ') || '(no recognized maps)'} to material ` +
					`'${args.material}'. Charged ${chargeNote(g.estimated, consumed)}.`));
			}
			catch (e) {
				return result(text(`Meshy generated this and charged ${chargeNote(g.estimated, consumed)} (already ` +
					`in meshy_ledger), but applying the maps locally failed: ${e.message}\nThe downloaded files are ` +
					`still at ${dir} - retry import_texture/set_material_texture on them by hand rather than ` +
					`generating again.`), true);
			}
		},
	},
];

module.exports = { MESHY_TOOLS };
