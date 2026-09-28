'use strict';

// The tools the hotbite-editor MCP server offers. All but one only look - at the
// level, its assets or the viewport. `editor_command` is the full automation channel,
// the one the regression suites drive, and is left out when the server runs with
// --read-only. The camera is the one thing a looking tool may move (screenshot's
// `position`/`target`): it is editor view state, not level data.
//
// Everything goes through the automation channel, so what a tool reports is what
// the editor's own panels show.

const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const { ChannelError, formatCommand } = require('./channel');
const { toJpeg } = require('./image');
const { MESHY_TOOLS } = require('./meshy-tools');

// Read-only automation commands `editor_query` accepts, with the most arguments
// each may carry. Several commands read with fewer arguments and *write* with more
// (`render`, `material_surface`, `shader_sources`), so the argument cap is what
// keeps them on the reading side - it is not a usage check.
const READ_COMMANDS = {
	ping: 0, state: 0, camera: 0, render: 0, menus: 0,
	list_entities: 0, list_selection: 0, list_groups: 0,
	list_templates: 0, list_models: 0, list_meshes: 0, list_animations: 0, list_splat_clouds: 0,
	list_textures: 0, texture_folders: 0, materials: 0, multi_materials: 0,
	model_info: 1, template_info: 1, template_parts: 1, template_animations: 1,
	components: 1, component: 2, animations: 1,
	textures: 1, texture_users: 1, shaders: 1, layer: 2, material_surface: 1,
	shaders_loaded: 0, shader_sources: 0, shader_reload_status: 0,
	physics_info: 0, lod_info: 0, rt_info: 0, tess_info: 0, gi_cache_info: 0, splat_info: 0,
	light_gizmo_info: 0, motion_gizmo_info: 0, schema_gizmo_info: 0,
	component_schemas: 0, component_schema: 1,
	polyhaven_status: 0, polyhaven_categories: 0, polyhaven_list: 2,
	rdoc_last: 0, meshy_setup_status: 0,
};

// The editor's own tokenizer (EditorAutomation.cpp), so an argument count here is
// the one the editor will see.
function tokenize(line) {
	const tokens = [];
	let current = '';
	let inQuotes = false;
	let hasToken = false;
	for (const ch of line) {
		if (ch === '"') {
			inQuotes = !inQuotes;
			hasToken = true;
		}
		else if ((ch === ' ' || ch === '\t') && !inQuotes) {
			if (hasToken) {
				tokens.push(current);
				current = '';
				hasToken = false;
			}
		}
		else {
			current += ch;
			hasToken = true;
		}
	}
	if (hasToken) tokens.push(current);
	return tokens;
}

// What `editor_command` still refuses. Ending the editor or crashing it takes the
// agent's own host down with it (the Claude panel lives inside the editor), and a
// menu item ending in "..." opens a modal Windows file dialog on the editor's main
// thread - it would sit there until a person answered it, with the channel stalled.
// Every one of those has a direct command that does the same work without a dialog.
function checkWriteCommand(line) {
	if (/[\r\n]/.test(line)) {
		throw new ChannelError('one command per entry; a line break is not allowed inside a command');
	}
	const tokens = tokenize(line);
	if (tokens.length === 0) throw new ChannelError('empty command');
	const [name, ...args] = tokens;
	if (name === 'quit' || name === 'debug_crash') {
		throw new ChannelError(`'${name}' would close the editor this agent runs in`);
	}
	if (name === 'menu') {
		const item = args.join(' ');
		if (/\.\.\.$/.test(item)) {
			throw new ChannelError(`'${item}' opens a file dialog that would block the editor. Use the direct command instead ` +
				'(open_level, import_model, import_meshy_model, import_template, ...).');
		}
		if (/\/(Exit|Quit)$/i.test(item)) {
			throw new ChannelError(`'${item}' would close the editor this agent runs in`);
		}
	}
	return line;
}

function checkReadCommand(line) {
	if (/[\r\n]/.test(line)) {
		throw new ChannelError('one command per entry; a line break is not allowed inside a command');
	}
	const tokens = tokenize(line);
	if (tokens.length === 0) throw new ChannelError('empty command');
	const [name, ...args] = tokens;
	if (!Object.prototype.hasOwnProperty.call(READ_COMMANDS, name)) {
		throw new ChannelError(`'${name}' is not a read-only command. Allowed: ${Object.keys(READ_COMMANDS).join(', ')}`);
	}
	if (args.length > READ_COMMANDS[name]) {
		throw new ChannelError(`'${name}' takes at most ${READ_COMMANDS[name]} argument(s) when reading; more would change the scene`);
	}
	return line;
}

// ---------------------------------------------------------------- formatting

const MAX_SECTION_LINES = 400;

function blockText(block, { maxLines = MAX_SECTION_LINES } = {}) {
	const head = `${block.status || '??'}${block.head ? ' ' + block.head : ''}`;
	const lines = block.lines.length > maxLines
		? [...block.lines.slice(0, maxLines), `... ${block.lines.length - maxLines} more lines not shown`]
		: block.lines;
	return [head, ...lines].join('\n');
}

function blocksText(blocks, titles) {
	return blocks.map((b, i) => `## ${titles ? titles[i] : b.command}\n${blockText(b)}`).join('\n\n');
}

const text = (t) => ({ type: 'text', text: t });
const result = (content, isError = false) => ({ content: Array.isArray(content) ? content : [content], isError });
const anyError = (blocks) => blocks.some((b) => b.status !== 'OK');

async function runNamed(ctx, sections, opts) {
	const commands = sections.map(([, cmd]) => cmd);
	const blocks = await ctx.channel.send(commands, opts);
	return { blocks, body: blocksText(blocks, sections.map(([title]) => title)) };
}

// ---------------------------------------------------------------- editor process

function defaultEditorExe() {
	// src -> mcp -> SceneEditor -> Tools -> repository root.
	return path.resolve(__dirname, '..', '..', '..', '..', 'Solution', 'x64', 'Release', 'SceneEditor.exe');
}

async function isAnswering(ctx, timeoutMs) {
	try {
		const blocks = await ctx.channel.send(['ping'], { timeoutMs });
		return blocks.length > 0 && blocks[0].status === 'OK';
	}
	catch (e) {
		return false;
	}
}

// ---------------------------------------------------------------- camera

const vec3 = {
	type: 'array', items: { type: 'number' }, minItems: 3, maxItems: 3,
};

const near = (a, b) => Array.isArray(a) && a.every((v, i) => Math.abs(v - b[i]) < 1e-2);

// A commanded pose is applied on the editor camera's own tick, not by the command
// (CLAUDE.md, "A commanded camera pose is not in effect on the next frame"), so the
// readout is polled until it agrees before anything is captured.
async function moveCamera(ctx, position, target) {
	const commands = [];
	if (position) commands.push(formatCommand('camera_pos', position));
	if (target) commands.push(formatCommand('camera_target', target));
	const blocks = await ctx.channel.send(commands);
	if (anyError(blocks)) {
		throw new ChannelError(blocksText(blocks));
	}
	for (let i = 0; i < 60; ++i) {
		const [cam] = await ctx.channel.send(['camera']);
		if (cam.status !== 'OK') throw new ChannelError(blockText(cam));
		const pose = JSON.parse(cam.head);
		if ((!position || near(pose.position, position)) && (!target || near(pose.target, target))) {
			return pose;
		}
	}
	throw new ChannelError('the camera never reached the requested pose');
}

// ---------------------------------------------------------------- tools

const ASSET_KINDS = {
	templates: 'list_templates',
	models: 'list_models',
	materials: 'materials',
	multi_materials: 'multi_materials',
	textures: 'list_textures',
	texture_folders: 'texture_folders',
	meshes: 'list_meshes',
	animations: 'list_animations',
	splat_clouds: 'list_splat_clouds',
	groups: 'list_groups',
};

const TOOLS = [
	{
		name: 'editor_launch',
		standalone: true,
		description:
			'Starts the HotBite Scene Editor with the automation channel this server talks to, optionally ' +
			'opening a level, and waits until it answers. Does nothing if an editor is already answering on ' +
			'the channel. Every other tool needs a running editor.',
		inputSchema: {
			type: 'object',
			properties: {
				level: { type: 'string', description: 'Absolute path of a level .json to open.' },
				project: { type: 'string', description: 'Project root. Derived from the level path when omitted.' },
			},
			additionalProperties: false,
		},
		async handler(args, ctx) {
			if (await isAnswering(ctx, 1500)) {
				const [state] = await ctx.channel.send(['state']);
				return result(text(`An editor is already running on ${ctx.channel.dir}.\n${blockText(state)}`));
			}
			const exe = ctx.config.editorExe;
			if (!fs.existsSync(exe)) {
				return result(text(`SceneEditor.exe not found at ${exe}. Build the Release configuration, or set HOTBITE_EDITOR_EXE.`), true);
			}
			const argv = ['--automation', ctx.channel.dir];
			if (args.level) argv.push('--level', args.level);
			if (args.project) argv.push('--project', args.project);
			// Run from the output folder so relative asset paths resolve the way the
			// other tools expect (automation/README.md).
			const child = spawn(exe, argv, { cwd: path.dirname(exe), detached: true, stdio: 'ignore' });
			child.unref();
			let exited = null;
			child.on('exit', (code) => { exited = code; });
			const deadline = Date.now() + ctx.config.launchTimeoutMs;
			while (Date.now() < deadline) {
				if (exited !== null) {
					return result(text(`The editor exited during startup (code ${exited}). Check ${path.join(ctx.channel.dir, 'crash.txt')}.`), true);
				}
				if (await isAnswering(ctx, 5000)) {
					const [state] = await ctx.channel.send(['state']);
					return result(text(`Editor started (pid ${child.pid}).\n${blockText(state)}`));
				}
			}
			return result(text('The editor started but never answered on the automation channel.'), true);
		},
	},
	{
		name: 'editor_status',
		description: 'Whether the Scene Editor is answering, and its state: project, level, selection, gizmo mode, status message, entity and template counts.',
		inputSchema: { type: 'object', properties: {}, additionalProperties: false },
		async handler(args, ctx) {
			const { blocks, body } = await runNamed(ctx, [['state', 'state']], { timeoutMs: 5000 });
			return result(text(body), anyError(blocks));
		},
	},
	{
		name: 'scene_summary',
		description:
			'An overview of the open level in one call: editor state, camera, selection, every entity ' +
			'(with position and group), groups, templates, imported models and materials. Start here. ' +
			'The project has three asset layers - a model (imported .fbx: meshes, materials, clips; not ' +
			'placeable), a template (one concept, the only placeable thing) and an instance (a template ' +
			'placed in this level).',
		inputSchema: { type: 'object', properties: {}, additionalProperties: false },
		async handler(args, ctx) {
			const { blocks, body } = await runNamed(ctx, [
				['Editor state', 'state'],
				['Camera', 'camera'],
				['Selection', 'list_selection'],
				['Entities', 'list_entities'],
				['Groups', 'list_groups'],
				['Templates', 'list_templates'],
				['Models', 'list_models'],
				['Materials', 'materials'],
			]);
			return result(text(body), anyError(blocks));
		},
	},
	{
		name: 'list_assets',
		description: 'Lists one kind of project asset: templates, models, materials, multi_materials, textures, texture_folders, meshes, animations, splat_clouds or groups.',
		inputSchema: {
			type: 'object',
			properties: { kind: { type: 'string', enum: Object.keys(ASSET_KINDS) } },
			required: ['kind'],
			additionalProperties: false,
		},
		async handler(args, ctx) {
			const cmd = ASSET_KINDS[args.kind];
			if (!cmd) return result(text(`unknown kind '${args.kind}'`), true);
			const { blocks, body } = await runNamed(ctx, [[args.kind, cmd]]);
			return result(text(body), anyError(blocks));
		},
	},
	{
		name: 'entity_details',
		description: "Every component on one scene entity, each as the JSON the Components panel edits (Transform, Mesh, Material, Bounds, Physics, lights, game components...).",
		inputSchema: {
			type: 'object',
			properties: { entity: { type: 'string' } },
			required: ['entity'],
			additionalProperties: false,
		},
		async handler(args, ctx) {
			const [list] = await ctx.channel.send([formatCommand('components', [args.entity])]);
			if (list.status !== 'OK') return result(text(blockText(list)), true);
			// A game component the editor cannot link is listed but has no JSON; its
			// ERR is reported in place rather than failing the whole call.
			// Presence-only components (Lighted) are listed too, and answer ERR for the
			// same reason: there is nothing to serialize. Neither is a missing component.
			const names = list.lines.map((l) => l.trim().split(/\s+/)[0]).filter(Boolean);
			const blocks = await ctx.channel.send(names.map((n) => formatCommand('component', [args.entity, n])));
			const body = blocks.map((b, i) => b.status === 'OK'
				? `## ${names[i]}\n${b.lines.join('\n') || b.head}`
				: `## ${names[i]}\npresent, no editable data (${b.head})`).join('\n\n');
			return result(text(`# ${args.entity}: ${list.head}\n\n${body}`));
		},
	},
	{
		name: 'template_details',
		description: "A template's component blocks, its parts (for a composed template) and its animation library.",
		inputSchema: {
			type: 'object',
			properties: { template: { type: 'string' } },
			required: ['template'],
			additionalProperties: false,
		},
		async handler(args, ctx) {
			const t = args.template;
			const { blocks, body } = await runNamed(ctx, [
				['Components', formatCommand('template_info', [t])],
				['Parts', formatCommand('template_parts', [t])],
				['Animations', formatCommand('template_animations', [t])],
			]);
			return result(text(body), blocks[0].status !== 'OK');
		},
	},
	{
		name: 'model_details',
		description: 'What one imported model (.fbx) contributed: its meshes, materials and animation clips.',
		inputSchema: {
			type: 'object',
			properties: { model: { type: 'string' } },
			required: ['model'],
			additionalProperties: false,
		},
		async handler(args, ctx) {
			const { blocks, body } = await runNamed(ctx, [['Model', formatCommand('model_info', [args.model])]]);
			return result(text(body), anyError(blocks));
		},
	},
	{
		name: 'material_details',
		description: "A material's texture maps, its shader stages and its surface (tessellation/displacement) settings.",
		inputSchema: {
			type: 'object',
			properties: { material: { type: 'string' } },
			required: ['material'],
			additionalProperties: false,
		},
		async handler(args, ctx) {
			const m = args.material;
			const { blocks, body } = await runNamed(ctx, [
				['Textures', formatCommand('textures', [m])],
				['Shaders', formatCommand('shaders', [m])],
				['Surface', formatCommand('material_surface', [m])],
			]);
			return result(text(body), blocks[0].status !== 'OK');
		},
	},
	{
		name: 'texture_users',
		description: 'Which materials and multi-material layers use a texture (a path under Assets/Textures, as list_assets textures prints it).',
		inputSchema: {
			type: 'object',
			properties: { texture: { type: 'string' } },
			required: ['texture'],
			additionalProperties: false,
		},
		async handler(args, ctx) {
			const { blocks, body } = await runNamed(ctx, [['Users', formatCommand('texture_users', [args.texture])]]);
			return result(text(body), anyError(blocks));
		},
	},
	{
		name: 'screenshot',
		description:
			'Captures what the editor shows - the rendered level plus the editor UI around it - and returns ' +
			'it as an image. Pass position and/or target to look from somewhere else first: the camera is ' +
			'moved (it stays there afterwards) and the frame is given time to settle, because lighting ' +
			'accumulates over several frames.',
		inputSchema: {
			type: 'object',
			properties: {
				position: { ...vec3, description: 'Camera position, world units.' },
				target: { ...vec3, description: 'Point the camera looks at.' },
				settle_frames: { type: 'integer', minimum: 0, maximum: 120, description: 'Frames to wait after a camera move (default 20).' },
				max_size: { type: 'integer', minimum: 256, maximum: 2560, description: 'Longer side of the returned image in pixels (default 1568).' },
			},
			additionalProperties: false,
		},
		async handler(args, ctx) {
			let pose = null;
			if (args.position || args.target) {
				pose = await moveCamera(ctx, args.position, args.target);
				const frames = args.settle_frames === undefined ? 20 : args.settle_frames;
				// Every batch is answered at the end of a frame, so one ping is at
				// least one frame.
				for (let i = 0; i < frames; ++i) await ctx.channel.send(['ping']);
			}
			fs.mkdirSync(ctx.config.tmpDir, { recursive: true });
			const stamp = `${process.pid}-${Date.now()}`;
			const png = path.join(ctx.config.tmpDir, `shot-${stamp}.png`);
			const jpg = path.join(ctx.config.tmpDir, `shot-${stamp}.jpg`);
			try {
				const [shot] = await ctx.channel.send([formatCommand('screenshot', [png])]);
				if (shot.status !== 'OK') return result(text(blockText(shot)), true);
				const img = await toJpeg(png, jpg, { maxSize: args.max_size || 1568 });
				const [cam] = pose ? [null] : await ctx.channel.send(['camera']);
				const camText = pose ? JSON.stringify(pose) : (cam && cam.status === 'OK' ? cam.head : 'unknown');
				const sizeText = img.original ? `${img.original.width}x${img.original.height} shown at ${img.size.width}x${img.size.height}` : '';
				return result([
					{ type: 'image', data: img.data, mimeType: 'image/jpeg' },
					text(`Editor window ${sizeText}. Camera: ${camText}`),
				]);
			}
			finally {
				for (const f of [png, jpg]) {
					try { fs.unlinkSync(f); } catch (e) { /* never written */ }
				}
			}
		},
	},
	{
		name: 'editor_query',
		description:
			'Runs read-only automation commands directly, for anything the other tools do not cover ' +
			'(lod_info, physics_info, render, splat_info, layer, animations, ...). One command per entry, ' +
			'arguments with spaces in double quotes. Commands that change the scene are refused. The full ' +
			'reference is Tools/SceneEditor/automation/README.md.',
		inputSchema: {
			type: 'object',
			properties: {
				commands: { type: 'array', items: { type: 'string' }, minItems: 1, maxItems: 32 },
			},
			required: ['commands'],
			additionalProperties: false,
		},
		async handler(args, ctx) {
			const commands = args.commands.map(checkReadCommand);
			const blocks = await ctx.channel.send(commands);
			return result(text(blocksText(blocks)), anyError(blocks));
		},
	},
	{
		name: 'editor_command',
		write: true,
		description:
			'Runs Scene Editor automation commands - the same channel the regression tests drive, with ' +
			'full access: select, set_position/rotation/scale, place, delete, rename, set_component, ' +
			'create_template, template_set, create_material, set_material_texture, import_model, ' +
			'polyhaven_import, undo, menu "File/Save Level", and everything else in ' +
			'Tools/SceneEditor/automation/README.md. Commands run in order in one editor frame. One ' +
			'command per entry; quote arguments containing spaces with double quotes; JSON arguments use ' +
			'single quotes ("{\'type\':\'DYNAMIC\'}"). Every edit is undoable. Nothing is written to disk ' +
			'until a save command. Refused: quit, debug_crash, and menu items ending in "..." (they open ' +
			'a blocking file dialog - use the direct command).',
		inputSchema: {
			type: 'object',
			properties: {
				commands: { type: 'array', items: { type: 'string' }, minItems: 1, maxItems: 64 },
				timeout_s: {
					type: 'integer', minimum: 5, maximum: 600,
					description: 'How long to wait for the editor (default 60). Raise it for large imports.',
				},
			},
			required: ['commands'],
			additionalProperties: false,
		},
		async handler(args, ctx) {
			const commands = args.commands.map(checkWriteCommand);
			const blocks = await ctx.channel.send(commands, { timeoutMs: (args.timeout_s || 60) * 1000 });
			return result(text(blocksText(blocks)), anyError(blocks));
		},
	},
	...MESHY_TOOLS,
];

// The tools this server offers: with `readOnly` nothing that changes the level, and
// `embedded` (the server started by the editor's own Claude panel) nothing that starts
// an editor - that agent is inside the one it would be launching a second copy of.
function toolsFor(readOnly, embedded) {
	return TOOLS.filter((t) => !(readOnly && t.write) && !(embedded && t.standalone));
}

function makeContext(channel, overrides = {}) {
	return {
		channel,
		config: {
			editorExe: process.env.HOTBITE_EDITOR_EXE || defaultEditorExe(),
			tmpDir: path.join(os.tmpdir(), 'hotbite-editor-mcp'),
			launchTimeoutMs: 180000,
			...overrides,
		},
	};
}

async function callTool(name, args, ctx) {
	const tool = toolsFor(ctx.config.readOnly, ctx.config.embedded).find((t) => t.name === name);
	if (!tool) return result(text(`unknown tool '${name}'`), true);
	try {
		return await tool.handler(args || {}, ctx);
	}
	catch (e) {
		if (e instanceof ChannelError) return result(text(e.message), true);
		return result(text(`${name} failed: ${e && e.stack ? e.stack : e}`), true);
	}
}

module.exports = { TOOLS, READ_COMMANDS, toolsFor, callTool, makeContext, checkReadCommand, checkWriteCommand, tokenize };
