'use strict';

// A hand-rolled client for api.meshy.ai, in the same spirit as PolyHaven.cpp's
// HttpGet: no npm dependencies (this machine's Node is 14, well before the global
// `fetch`), and a plain env-var override of the base URL so the offline test suite
// can point this at a local mock server instead of the real, paid API - the same
// role PolyHaven::SetSource plays for polyhaven.com.
//
// This module only talks to Meshy; it knows nothing about credits, approval or the
// editor. See meshy-credits.js for the money/approval half and meshy-tools.js for
// where the two meet.

const fs = require('fs');
const http = require('http');
const https = require('https');
const path = require('path');
const { URL } = require('url');

function baseUrl() {
	return process.env.HOTBITE_MESHY_API_BASE || 'https://api.meshy.ai';
}

function request(method, urlPath, apiKey, body) {
	return new Promise((resolve, reject) => {
		let url;
		try {
			url = new URL(baseUrl() + urlPath);
		}
		catch (e) {
			reject(new Error(`invalid Meshy API base URL: ${baseUrl()}`));
			return;
		}
		const mod = url.protocol === 'http:' ? http : https;
		const payload = body ? JSON.stringify(body) : null;
		const req = mod.request(url, {
			method,
			headers: {
				Authorization: `Bearer ${apiKey}`,
				'Content-Type': 'application/json',
				...(payload ? { 'Content-Length': Buffer.byteLength(payload) } : {}),
			},
		}, (res) => {
			let data = '';
			res.on('data', (c) => { data += c; });
			res.on('end', () => {
				let json;
				try {
					json = data ? JSON.parse(data) : {};
				}
				catch (e) {
					reject(new Error(`Meshy returned a non-JSON response (HTTP ${res.statusCode}): ${data.slice(0, 200)}`));
					return;
				}
				if (res.statusCode >= 400) {
					const msg = (json && (json.message || (json.error && json.error.message) || json.error)) || `HTTP ${res.statusCode}`;
					const err = new Error(`Meshy API error: ${msg}`);
					err.status = res.statusCode;
					err.body = json;
					reject(err);
					return;
				}
				resolve(json);
			});
		});
		req.on('error', (e) => reject(new Error(`could not reach Meshy at ${baseUrl()}: ${e.message}`)));
		if (payload) req.write(payload);
		req.end();
	});
}

// Follows redirects (Meshy's own download links are presigned S3 URLs) and writes
// the body straight to disk.
function download(url, destPath, redirectsLeft = 5) {
	return new Promise((resolve, reject) => {
		const mod = url.startsWith('http://') ? http : https;
		mod.get(url, (res) => {
			if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location && redirectsLeft > 0) {
				res.resume();
				download(res.headers.location, destPath, redirectsLeft - 1).then(resolve, reject);
				return;
			}
			if (res.statusCode >= 400) {
				res.resume();
				reject(new Error(`download failed (HTTP ${res.statusCode}): ${url}`));
				return;
			}
			const file = fs.createWriteStream(destPath);
			res.pipe(file);
			file.on('finish', () => file.close(() => resolve(destPath)));
			file.on('error', reject);
		}).on('error', (e) => reject(new Error(`download failed: ${url}: ${e.message}`)));
	});
}

// ---------------------------------------------------------------- balance

async function getBalance(apiKey) {
	return request('GET', '/openapi/v1/balance', apiKey);
}

// ---------------------------------------------------------------- text-to-3d

async function createTextTo3DPreview(apiKey, params) {
	const res = await request('POST', '/openapi/v2/text-to-3d', apiKey, { mode: 'preview', ...params });
	return res.result;
}

async function createTextTo3DRefine(apiKey, previewTaskId, params) {
	const res = await request('POST', '/openapi/v2/text-to-3d', apiKey,
		{ mode: 'refine', preview_task_id: previewTaskId, ...params });
	return res.result;
}

async function getTextTo3DTask(apiKey, id) {
	return request('GET', `/openapi/v2/text-to-3d/${encodeURIComponent(id)}`, apiKey);
}

// ---------------------------------------------------------------- image-to-3d

async function createImageTo3D(apiKey, params) {
	const res = await request('POST', '/openapi/v1/image-to-3d', apiKey, params);
	return res.result;
}

async function getImageTo3DTask(apiKey, id) {
	return request('GET', `/openapi/v1/image-to-3d/${encodeURIComponent(id)}`, apiKey);
}

// ---------------------------------------------------------------- retexture

async function createRetexture(apiKey, params) {
	const res = await request('POST', '/openapi/v1/retexture', apiKey, params);
	return res.result;
}

async function getRetextureTask(apiKey, id) {
	return request('GET', `/openapi/v1/retexture/${encodeURIComponent(id)}`, apiKey);
}

// ---------------------------------------------------------------- polling

const TASK_GETTERS = {
	text_to_3d: getTextTo3DTask,
	image_to_3d: getImageTo3DTask,
	retexture: getRetextureTask,
};

const DONE_STATUSES = new Set(['SUCCEEDED', 'FAILED', 'CANCELED']);

// Polls a task's GET endpoint until it leaves PENDING/IN_PROGRESS. Meshy generation
// is a background job that can take minutes, so this is expected to block the MCP
// tool call for a while - there is no partial-progress channel back to the agent's
// chat in this server, only the final answer.
// HOTBITE_MESHY_POLL_MS lets the offline test suite poll in milliseconds instead
// of seconds; nothing else should need to override it.
async function pollTask(apiKey, kind, id,
	{ timeoutMs = 300000, intervalMs = Number(process.env.HOTBITE_MESHY_POLL_MS) || 4000, onProgress } = {}) {
	const getter = TASK_GETTERS[kind];
	if (!getter) throw new Error(`unknown Meshy task kind '${kind}'`);
	const deadline = Date.now() + timeoutMs;
	for (;;) {
		const task = await getter(apiKey, id);
		if (onProgress) onProgress(task);
		if (DONE_STATUSES.has(task.status)) return task;
		if (Date.now() > deadline) {
			throw new Error(`Meshy task ${id} did not finish within ${Math.round(timeoutMs / 1000)}s ` +
				`(last status ${task.status}, ${task.progress || 0}%)`);
		}
		await new Promise((resolve) => setTimeout(resolve, intervalMs));
	}
}

// ---------------------------------------------------------------- download

// texture_urls comes back as an array of map-sets (usually one) for text/image-to-3d
// and retexture alike; flatten to the one set, {} when the task has none.
function normalizeTextureUrls(raw) {
	if (!raw) return {};
	if (Array.isArray(raw)) return raw[0] || {};
	return raw;
}

// Meshy's own texture-map vocabulary (base_color plus PBR keys), mapped to the
// suffix MeshyImport.cpp's ClassifySuffix already recognizes. Metallic and
// roughness both land on "specular" - the engine has no metallic/roughness slot,
// and MeshyImport.cpp's own priority (explicit spec > metallic > roughness) is
// mirrored here by only keeping the first of the two that is present.
function slotForKey(key, seen) {
	switch (key) {
		case 'normal': return 'normal';
		case 'ao': case 'occlusion': return 'ao';
		case 'emissive': case 'emission': return 'emissive';
		case 'height': case 'displacement': return 'height';
		case 'opacity': case 'alpha': return 'opacity';
		case 'specular': return 'specular';
		case 'metallic': return seen.has('specular') ? null : 'specular';
		case 'roughness': return (seen.has('specular') || seen.has('metallic')) ? null : 'specular';
		default: return null;
	}
}

function extFromUrl(url, fallback = '.png') {
	try {
		const ext = path.extname(new URL(url).pathname);
		return ext || fallback;
	}
	catch (e) {
		return fallback;
	}
}

// Downloads a finished task's model and texture maps into `destDir`, named the way
// MeshyImport::Import expects to find them next to `<stem>.fbx`: the diffuse map
// bare (`<stem>.png`), everything else `<stem>_<slot>.png` (MeshyImport.h,
// ClassifySuffix). Requesting `target_formats: ["fbx"]` on task creation is what
// makes `model_urls.fbx` exist - the engine's importer only reads .fbx.
async function downloadResult(task, destDir, stem) {
	fs.mkdirSync(destDir, { recursive: true });
	const fbxUrl = task.model_urls && task.model_urls.fbx;
	if (!fbxUrl) {
		throw new Error('the Meshy task has no .fbx in model_urls - request target_formats including "fbx"');
	}
	const fbxPath = path.join(destDir, `${stem}.fbx`);
	await download(fbxUrl, fbxPath);
	const files = [fbxPath];

	const maps = normalizeTextureUrls(task.texture_urls);
	const seen = new Set();
	// base_color first, so metallic/roughness's fallback-to-specular logic above
	// can see whether an explicit specular map already claimed the slot.
	const keys = Object.keys(maps).sort((a, b) => (a === 'base_color' ? -1 : b === 'base_color' ? 1 : 0));
	for (const key of keys) {
		const url = maps[key];
		if (!url) continue;
		const ext = extFromUrl(url);
		if (key === 'base_color' || key === 'diffuse') {
			const p = path.join(destDir, `${stem}${ext}`);
			await download(url, p);
			files.push(p);
			continue;
		}
		const slot = slotForKey(key, seen);
		if (!slot) continue;
		seen.add(slot);
		const p = path.join(destDir, `${stem}_${slot}${ext}`);
		await download(url, p);
		files.push(p);
	}
	return { dir: destDir, fbx: fbxPath, files };
}

// Meshy's model_url/image_url fields also accept a base64 data URI, which is how
// retexture reaches a project's own .fbx (or an image on disk) - it never needs to
// be hosted anywhere public. No size check here: Meshy's own 402/4xx response is
// what tells the caller a file was too large.
function fileToDataUri(filePath, mimeType) {
	const data = fs.readFileSync(filePath);
	return `data:${mimeType};base64,${data.toString('base64')}`;
}

module.exports = {
	baseUrl,
	getBalance,
	createTextTo3DPreview, createTextTo3DRefine, getTextTo3DTask,
	createImageTo3D, getImageTo3DTask,
	createRetexture, getRetextureTask,
	pollTask, downloadResult,
	fileToDataUri,
};
