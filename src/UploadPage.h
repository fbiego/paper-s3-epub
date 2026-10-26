#pragma once
#include <Arduino.h>

// Served from firmware, so formatting LittleFS never removes the upload UI.
static const char uploadPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="light">
<title>PaperS3 · Your next chapter</title>
<link rel="icon" type="image/png" href="/papers3-library.png">
<style>
:root{--paper:#f5f3ec;--ink:#243b31;--muted:#65756a;--green:#25674e;--line:#dce1d7;--white:#fffefa}*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;line-height:1.6}main{max-width:1000px;margin:auto;padding:32px 28px 48px}header{display:flex;align-items:center;justify-content:space-between;gap:16px;border-bottom:1px solid var(--line);padding-bottom:24px}.brand{display:flex;align-items:center;gap:12px;font-size:21px;font-weight:750;letter-spacing:-.6px}.brand img{width:40px;height:40px;object-fit:contain;mix-blend-mode:multiply;flex-shrink:0}.brand small{font-size:13px;font-weight:500;letter-spacing:1px;color:var(--muted);margin-left:5px}.badge{display:flex;align-items:center;gap:8px;font-size:12px;background:#e7ede2;border:1px solid #d1dfcd;border-radius:30px;padding:6px 12px;white-space:nowrap}.dot{width:7px;height:7px;background:var(--green);border-radius:50%}.intro{padding:44px 0 28px}.eyebrow{text-transform:uppercase;letter-spacing:2px;font-size:11px;font-weight:750;color:var(--green);margin:0 0 13px}h1{font-family:Georgia,"Times New Roman",serif;font-weight:400;font-size:clamp(36px,5vw,53px);line-height:1.12;letter-spacing:-1.4px;margin:0 0 17px;max-width:650px}.lede{margin:0;color:var(--muted);max-width:580px;font-size:15px}.grid{display:grid;grid-template-columns:minmax(0,1.7fr) minmax(240px,1fr);gap:24px;align-items:start}.card{background:var(--white);border:1px solid var(--line);border-radius:18px;padding:26px}.card h2{font-size:16px;font-weight:700;margin:0 0 6px}.sub{font-size:13px;color:var(--muted);margin:0 0 22px}.drop{position:relative;border:1.5px dashed #a9bba9;border-radius:12px;min-height:214px;padding:25px 18px;display:flex;align-items:center;justify-content:center;flex-direction:column;text-align:center;background:#f8faf5;cursor:pointer;transition:background .15s,border-color .15s}.drop:hover,.drop.drag{background:#edf3e9;border-color:var(--green)}.drop:focus-within{outline:3px solid #8eb29a;outline-offset:4px}.drop svg{width:39px;height:39px;stroke:var(--green);margin-bottom:13px}.drop strong{font-size:15px;overflow-wrap:anywhere}.drop span{display:block;font-size:12px;color:var(--muted);margin-top:5px}.drop .browse{color:var(--green);font-weight:650;text-decoration:underline;text-underline-offset:3px}.file{position:absolute;width:1px;height:1px;opacity:0;overflow:hidden}.actions{display:flex;gap:12px;margin-top:20px;align-items:center;flex-wrap:wrap}button{font:inherit;font-size:13px;font-weight:650;cursor:pointer;border-radius:8px;padding:12px 18px;border:1px solid transparent}.primary{background:var(--green);color:white;flex:1}.primary:hover{background:#1b503b}.primary:disabled{opacity:.45;cursor:not-allowed}.secondary{background:transparent;color:var(--muted);border-color:var(--line)}button:focus-visible{outline:3px solid #8eb29a;outline-offset:3px}.feedback{margin-top:18px;font-size:13px;overflow-wrap:anywhere}.feedback[data-tone="error"]{color:#a1372c}.feedback[data-tone="success"]{color:var(--green)}progress{appearance:none;width:100%;height:7px;border:0;border-radius:8px;margin:14px 0 0;background:#e8ede3;overflow:hidden}progress::-webkit-progress-bar{background:#e8ede3}progress::-webkit-progress-value{background:var(--green);border-radius:8px}progress::-moz-progress-bar{background:var(--green)}[hidden]{display:none!important}.storage-head{display:flex;justify-content:space-between;gap:12px;align-items:center}.storage-head span{font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.8px;color:var(--muted)}.capacity{font-family:Georgia,"Times New Roman",serif;font-size:36px;letter-spacing:-1px;margin:18px 0 0;line-height:1.2}.capacity small{font-family:system-ui;font-size:12px;font-weight:400;letter-spacing:0;color:var(--muted);margin-left:7px}.usage{height:6px;background:#e8ede3;border-radius:10px;margin:17px 0 10px;overflow:hidden}.usage div{background:var(--green);height:100%;width:0;transition:width .2s}.storage-note{color:var(--muted);font-size:11px;margin:0}.refresh{background:transparent;color:var(--green);padding:9px 0;font-size:12px}.steps{padding:24px 2px 0}.steps h2{font-family:Georgia,serif;font-size:24px;font-weight:400;letter-spacing:-.5px;margin:0 0 16px}.step{display:flex;gap:12px;margin:14px 0;font-size:13px;color:var(--muted)}.number{flex:0 0 24px;height:24px;border:1px solid #cbd7c9;border-radius:50%;font-size:11px;color:var(--green);display:grid;place-items:center}.step strong{color:var(--ink);font-weight:650}.note{margin:20px 0 0;font-size:11px;color:var(--muted);border-top:1px solid var(--line);padding-top:16px}footer{margin-top:30px;display:flex;justify-content:space-between;gap:16px;font-size:11px;color:var(--muted);flex-wrap:wrap}a{color:var(--green)}@media(max-width:660px){main{padding:22px 18px 30px}.grid{grid-template-columns:1fr;gap:20px}.intro{padding-top:32px}header{padding-bottom:20px}.card{padding:22px}.badge{font-size:10px;padding:5px 9px}.brand{font-size:19px}.brand small{display:none}.steps{padding:20px 3px 0}.drop{min-height:190px}footer{margin-top:22px}}
.file-manager{margin-top:26px}.file-head{display:flex;justify-content:space-between;align-items:start;gap:16px;flex-wrap:wrap}.folder-path{font-size:12px;color:var(--muted);overflow-wrap:anywhere;margin:6px 0 0}.file-toolbar{display:flex;gap:8px;flex-wrap:wrap;margin:20px 0}.file-toolbar button,.file-pager button{font-size:12px;padding:8px 13px}.file-list{border-top:1px solid var(--line)}.file-row{display:flex;gap:18px;align-items:center;justify-content:space-between;padding:17px 0;border-bottom:1px solid var(--line)}.file-info{min-width:0;flex:1}.file-name{font-size:14px;font-weight:650;overflow-wrap:anywhere}.file-meta{font-size:11px;color:var(--muted);margin-top:3px}.file-actions{display:flex;gap:8px;align-items:center;flex-wrap:wrap;flex-shrink:0}.file-actions a,.file-actions button{font:inherit;font-size:12px;text-decoration:none;border:1px solid var(--line);border-radius:7px;padding:7px 12px;background:transparent;white-space:nowrap}.file-actions .delete-file{color:#a1372c;border-color:#ecd6cf}.file-actions button:disabled,.file-toolbar button:disabled,.file-pager button:disabled{opacity:.4;cursor:not-allowed}.file-pager{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap;margin-top:17px}.file-status{font-size:12px;color:var(--muted);margin:15px 0 0;overflow-wrap:anywhere}.file-status[data-tone="error"]{color:#a1372c}.access{font-size:11px;color:var(--muted);margin:4px 0 0}.empty-files{padding:30px 0;text-align:center;color:var(--muted);font-size:13px}@media(max-width:660px){.file-row{align-items:start;flex-direction:column;gap:10px}.file-actions{width:100%}.file-actions a,.file-actions button{flex:1;text-align:center}.file-manager{margin-top:20px}}
.optimize{display:flex;align-items:flex-start;gap:9px;margin:18px 0 0;color:var(--ink);font-size:13px;cursor:pointer}.optimize input{margin-top:4px;accent-color:var(--green);flex-shrink:0}.optimize small{display:block;color:var(--muted);font-size:12px}
</style>
</head>
<body><main>
<header><div class="brand"><img src="/papers3-library.png" alt="PaperS3 logo" width="40" height="40"><span>Paper<small>S3 LIBRARY</small></span></div><div class="badge"><span class="dot"></span>Reader upload mode</div></header>
<section class="intro"><p class="eyebrow">Make room for a good book</p><h1>Your next chapter<br>starts here.</h1><p class="lede">Send a book to your PaperS3. A quiet screen, a little time, and something worth reading.</p></section>
<div class="grid"><section class="card" aria-labelledby="upload-title"><h2 id="upload-title">Add to your library</h2><p class="sub">Choose a DRM-free EPUB for your reader.</p>
<form id="upload-form" method="post" action="/upload" enctype="multipart/form-data">
<label class="drop" id="drop"><input class="file" id="book" type="file" name="book" accept=".epub" required><svg viewBox="0 0 40 40" fill="none" stroke-width="1.5" aria-hidden="true"><rect x="9" y="4" width="22" height="32" rx="3"/><path d="M14 11h12M14 16h8M20 30V21m-4 4 4-4 4 4"/></svg><strong id="file-name">Drop your book here</strong><span id="file-detail">or <span class="browse">browse files</span> · .epub</span></label>
<label class="optimize"><input id="optimize-images" type="checkbox" checked><span>Optimize JPEG images for PaperS3<small>Convert progressive JPEGs and fit large JPEGs to the screen. Your original EPUB stays unchanged.</small></span></label>
<div class="actions"><button class="primary" id="send" type="submit" disabled>Send to reader &rarr;</button><button class="secondary" id="clear" type="button" hidden>Clear</button></div>
<progress id="progress" max="100" value="0" aria-label="Upload progress" hidden></progress><div class="feedback" id="feedback" role="status" aria-live="polite">Checking your reader's storage...</div>
<noscript><p>Please enable JavaScript for uploads and file management.</p></noscript>
</form><p class="note">Books stay on your reader. Existing filenames are kept safe and will never be overwritten.</p></section>
<aside><section class="card" aria-labelledby="storage-title"><div class="storage-head"><h2 id="storage-title">On your reader</h2><span>Internal storage</span></div><p class="capacity"><span id="free">&mdash;</span><small>available</small></p><div class="usage" role="meter" aria-label="Storage used" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0" id="usage"><div id="used-bar"></div></div><p class="storage-note" id="storage-detail">Loading storage details</p><button type="button" class="refresh" id="refresh">Refresh storage &circlearrowright;</button></section>
<section class="steps"><h2>From here to your hands.</h2><div class="step"><span class="number">1</span><div><strong>Choose a book.</strong><br>A DRM-free EPUB, ready to read.</div></div><div class="step"><span class="number">2</span><div><strong>Send it over.</strong><br>Keep this page open until it's added.</div></div><div class="step"><span class="number">3</span><div><strong>Settle into a story.</strong><br>Tap Stop Wi-Fi / Library on your reader.</div></div></section></aside></div>
<section class="card file-manager" aria-labelledby="files-title"><div class="file-head"><div><h2 id="files-title">Files on your reader</h2><p class="folder-path" id="folder-path">Internal storage · /books</p><p class="access" id="access"></p></div><button type="button" class="refresh" id="files-refresh">Refresh files &circlearrowright;</button></div><div class="file-toolbar"><button type="button" class="secondary" id="root-folder">Root folder</button><button type="button" class="secondary" id="books-folder">Books</button><button type="button" class="secondary" id="parent-folder">Up a folder</button></div><div class="file-list" id="file-list"></div><p class="file-status" id="files-status" role="status" aria-live="polite">Loading your files...</p><div class="file-pager"><button type="button" class="secondary" id="files-previous" disabled>Previous files</button><button type="button" class="secondary" id="files-next" disabled>Next files</button></div></section>
<footer><span>PaperS3 · A small library. A world of stories.</span><span>Wi-Fi turns off when upload mode is idle.</span></footer>
</main>
<script src="/vendor/fflate.js"></script>
<script src="/epub-prepare.js"></script>
<script>
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  // Dropped files are held in memory rather than assigned to input.files.
  // JavaScript validates selection; retain native validation without JS.
  $('book').required = false;
  let selected = null, capacity = null, request = null, managing = false, listing = false, preparing = null;
  let directory = '/books', fileOffset = 0, nextOffset = 0, moreFiles = false;
  function size(bytes) { return bytes >= 1048576 ? (bytes / 1048576).toFixed(1) + ' MiB' : Math.ceil(bytes / 1024) + ' KiB'; }
  function message(text, tone = '') { $('feedback').textContent = text; $('feedback').dataset.tone = tone; }
  function sync() {
    const busy = !!request || managing || !!preparing;
    $('send').disabled = busy || !selected || !capacity;
    $('book').disabled = busy;
    $('optimize-images').disabled = busy;
    $('refresh').disabled = busy;
    $('clear').hidden = !selected && !request && !preparing;
    $('clear').textContent = preparing ? 'Cancel preparation' : request ? 'Cancel upload' : 'Clear';
    $('send').textContent = preparing ? 'Preparing images...' : request ? 'Sending your book...' : 'Send to reader →';
    document.querySelectorAll('#file-list button').forEach(button => { button.disabled = busy || listing || (button.classList.contains('delete-file') && !capacity?.token); });
    for (const id of ['files-refresh', 'root-folder', 'books-folder']) $(id).disabled = busy || listing;
    $('parent-folder').disabled = busy || listing || directory === '/';
    $('files-previous').disabled = busy || listing || fileOffset === 0;
    $('files-next').disabled = busy || listing || !moreFiles;
  }
  function choose(file) {
    if (request || managing || preparing) return;
    if (!file) { selected = null; $('book').value = ''; $('file-name').textContent = 'Drop your book here'; $('file-detail').textContent = 'Choose a .epub file'; sync(); return; }
    if (!/\.epub$/i.test(file.name)) { choose(null); message('Choose an EPUB file to continue.', 'error'); return; }
    if (new TextEncoder().encode(file.name).length > 96) { choose(null); message('Please shorten the filename to 96 bytes or fewer.', 'error'); return; }
    selected = file;
    $('file-name').textContent = file.name;
    $('file-detail').textContent = size(file.size) + ' · EPUB book';
    $('progress').hidden = true;
    message('Your book is ready to send.'); sync();
  }
  async function storage(announce = false) {
    try {
      const response = await fetch('/api/storage', {cache: 'no-store'});
      if (!response.ok) throw new Error('Storage request failed');
      capacity = await response.json();
      $('access').textContent = capacity.authRequired ? 'Sign-in required for file access' : 'No sign-in required on this network';
      const percent = capacity.total ? Math.round(capacity.used * 100 / capacity.total) : 0;
      $('free').textContent = size(capacity.free);
      $('used-bar').style.width = percent + '%'; $('usage').setAttribute('aria-valuenow', percent);
      $('storage-detail').textContent = size(capacity.used) + ' used of ' + size(capacity.total);
      if (announce) message('Storage updated. Choose a book to get started.');
    } catch (error) {
      capacity = null; $('storage-detail').textContent = 'Reader unavailable';
      message('Cannot reach the reader. Check Wi-Fi upload mode and refresh storage.', 'error');
    }
    sync();
  }
  function fileMessage(text, tone = '') { $('files-status').textContent = text; $('files-status').dataset.tone = tone; }
  async function loadFiles(folder = directory, offset = fileOffset, announcement = '') {
    if (listing) return;
    listing = true; sync();
    try {
      const response = await fetch('/api/files?dir=' + encodeURIComponent(folder) + '&offset=' + offset, {cache:'no-store'});
      if (!response.ok) throw new Error(response.status === 401 ? 'Please reload and sign in to view files.' : await response.text());
      const data = await response.json();
      directory = data.directory; fileOffset = data.offset; moreFiles = data.more; nextOffset = data.nextOffset;
      $('folder-path').textContent = 'Internal storage · ' + directory;
      $('file-list').replaceChildren();
      for (const file of data.files) {
        const row = document.createElement('div'); row.className = 'file-row';
        const info = document.createElement('div'); info.className = 'file-info';
        const name = document.createElement('div'); name.className = 'file-name'; name.textContent = file.name;
        const meta = document.createElement('div'); meta.className = 'file-meta'; meta.textContent = file.directory ? 'Folder' : size(file.size);
        info.append(name, meta);
        const actions = document.createElement('div'); actions.className = 'file-actions';
        if (file.directory) {
          const open = document.createElement('button'); open.type = 'button'; open.textContent = 'Open folder'; open.dataset.path = file.path;
          open.addEventListener('click', () => loadFiles(file.path, 0)); actions.append(open);
        } else {
          const download = document.createElement('a'); download.textContent = 'Download'; download.href = '/download?path=' + encodeURIComponent(file.path); download.download = file.name;
          const remove = document.createElement('button'); remove.type = 'button'; remove.className = 'delete-file'; remove.textContent = 'Delete'; remove.dataset.path = file.path;
          remove.addEventListener('click', () => removeFile(file)); actions.append(download, remove);
        }
        row.append(info, actions); $('file-list').append(row);
      }
      if (!data.files.length) { const empty = document.createElement('p'); empty.className = 'empty-files'; empty.textContent = 'No files in this folder.'; $('file-list').append(empty); }
      fileMessage(announcement || (data.files.length ? 'Showing ' + (fileOffset + 1) + '–' + (fileOffset + data.files.length) + (moreFiles ? ' · More files available' : '') : 'Choose Books or another folder to browse.'));
    } catch (error) { fileMessage(error.message || 'Cannot reach the reader. Refresh files to try again.', 'error'); }
    listing = false; sync();
  }
  async function removeFile(file) {
    if (request || managing || preparing || !capacity?.token) return;
    if (!window.confirm('Delete "' + file.name + '" from internal storage? This cannot be undone.')) return;
    managing = true; sync();
    try {
      const response = await fetch('/api/delete', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded', 'X-Reader-Token':capacity.token}, body:new URLSearchParams({path:file.path})});
      if (!response.ok) throw new Error(response.status === 401 ? 'Please reload and sign in to delete files.' : await response.text());
      await loadFiles(directory, 0, 'Deleted "' + file.name + '".');
      await storage();
    } catch (error) { fileMessage(error.message || 'Delete failed. Refresh files before trying again.', 'error'); }
    managing = false; sync();
  }
  $('book').addEventListener('change', () => choose($('book').files[0]));
  $('drop').addEventListener('dragover', event => { event.preventDefault(); if (!request) $('drop').classList.add('drag'); });
  $('drop').addEventListener('dragleave', () => $('drop').classList.remove('drag'));
  $('drop').addEventListener('drop', event => { event.preventDefault(); $('drop').classList.remove('drag'); if (event.dataTransfer.files.length !== 1) { message('Choose one book at a time.', 'error'); return; } choose(event.dataTransfer.files[0]); });
  $('clear').addEventListener('click', () => { if (preparing) { preparing.cancelled = true; message('Canceling image preparation...'); } else if (request) request.abort(); else { choose(null); message('Choose your next book.'); } });
  $('refresh').addEventListener('click', () => storage(true));
  $('files-refresh').addEventListener('click', () => loadFiles());
  $('root-folder').addEventListener('click', () => loadFiles('/', 0));
  $('books-folder').addEventListener('click', () => loadFiles('/books', 0));
  $('parent-folder').addEventListener('click', () => loadFiles(directory.slice(0, directory.lastIndexOf('/')) || '/', 0));
  $('files-previous').addEventListener('click', () => loadFiles(directory, Math.max(0, fileOffset - 32)));
  $('files-next').addEventListener('click', () => loadFiles(directory, nextOffset));
  $('upload-form').addEventListener('submit', async event => {
    event.preventDefault();
    if (request || managing || preparing || !selected || !capacity?.token) return;
    let uploadFile = selected, optimized = 0;
    if ($('optimize-images').checked) {
      preparing = {cancelled:false}; sync(); message('Preparing images in your browser...');
      try {
        if (!window.PaperEpub) throw new Error('Image preparation is unavailable. Reload the page or turn off image optimization.');
        let lastKeepalive = Date.now();
        const result = await PaperEpub.prepare(uploadFile, {cancel:preparing, onProgress:async ({current,total}) => {
          message('Preparing image ' + current + ' of ' + total + ' in your browser...');
          if (Date.now() - lastKeepalive > 15000) { await storage(); lastKeepalive = Date.now(); }
        }});
        if (preparing.cancelled) throw new Error('Upload preparation canceled.');
        uploadFile = result.file; optimized = result.converted;
      } catch (error) { preparing = null; message(error.message, 'error'); sync(); return; }
      preparing = null; sync();
    }
    if (!capacity?.token) { message('Cannot reach the reader. Refresh storage and retry.', 'error'); return; }
    if (uploadFile.size > Math.max(0, capacity.free - capacity.reserve)) { message('This book needs more space than is available on your reader.', 'error'); return; }
    const xhr = new XMLHttpRequest(); request = xhr;
    const data = new FormData(); data.append('book', uploadFile, uploadFile.name);
    xhr.open('POST', '/upload'); xhr.timeout = 180000;
    xhr.setRequestHeader('X-Reader-Token', capacity.token);
    $('progress').value = 0; $('progress').hidden = false; message('Sending your book...'); sync();
    xhr.upload.onprogress = event => { if (event.lengthComputable) { const percent = Math.round(event.loaded * 100 / event.total); $('progress').value = percent; message('Sending your book... ' + percent + '%'); } };
    xhr.upload.onload = () => message('Transfer complete. Checking your EPUB...');
    xhr.onload = () => {
      request = null;
      if (xhr.status === 201) { choose(null); $('progress').value = 100; $('progress').hidden = false; message('Book added. ' + (optimized ? optimized + ' JPEG image(s) optimized. ' : '') + 'Open the library on your reader to start reading.', 'success'); loadFiles('/books', 0); }
      else message(xhr.status === 401 ? 'Your login expired. Reload this page and sign in again.' : (xhr.responseText || 'The book could not be added. Try again.'), 'error');
      sync(); storage();
    };
    function failed(text) { request = null; message(text, 'error'); sync(); storage(); }
    xhr.onerror = () => failed('Connection lost. Check upload mode on your reader, then try again.');
    xhr.ontimeout = () => failed('The upload timed out. Check your reader and try again.');
    xhr.onabort = () => failed('Upload canceled. Check the library on your reader before retrying.');
    xhr.send(data);
  });
  storage(true);
  loadFiles('/books', 0);
})();
</script></body></html>)HTML";
