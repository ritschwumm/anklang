// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

const API_LEVEL = 22.03;
let _counter = 1;

/** A ScriptHost object represents a Worker script in the Main thread.
 *
 * A ScriptHost object (defined in `script.js`) runs in the Main Javascript
 * thread. It starts a Worker via `host.js` which creates a WorkerHost object
 * in the Worker thread. The ScriptHost<->WorkerHost objects bridge needed
 * API from the Main thread to the Worker thread.
 * The WorkerHost then loads and calls into a user provided ES Module, the
 * actual user script which communicates via the WorkerHost global variable
 * `host`. See also #WorkerHost.
 */
class ScriptHost {
  constructor (script_name) {
    console.assert (script_name);
    this.script_name = script_name;
    this.script_uuid = undefined;
    this.worker = new Worker ('/host.js', { type: 'module', name: this.script_name });
    this.worker.onerror = this.onerror.bind (this);
    this.worker.onmessage = this.onmessage.bind (this);
    this.promises = undefined;
    this.seen_lifesign = false;
  }
  terminate () {
    this.worker.terminate();
  }
  kill (...args) {
    console.error ('TERMINATE "' + this.script_name + '":', ...args);
    this.terminate();
  }
  onerror (err) {
    let prefix = '';
    if (err.filename && err.lineno)
      prefix = err.filename.replace (/.*\//, '') + ':' + err.lineno + ': ';
    if (!this.seen_lifesign)
      prefix += "failed to load (SyntaxError?): ";
    this.kill (err.message ? prefix + err.message : prefix, err);
    this.terminate();
  }
  onmessage (msg) {
    const d = msg.data, fun = d.fun ? '_' + d.fun : null;
    this.seen_lifesign = true;
    // handle call: WorkerHost -call-> ScriptHost
    if (d.callid > 0 && typeof this[fun] === 'function') {
      const result = this[fun] (...(d.args ? d.args : []));
      this.worker.postMessage ({ resultid: d.callid, result });
      return;
    }
    // handle result: ScriptHost -call-> WorkerHost -result-> ScriptHost
    if (d.resultid > 0) {
      const resolve = this.promises.get (d.resultid);
      if (resolve) {
	this.promises.delete (d.resultid);
	resolve (d.result);
	return;
      }
    }
    // complain
    console.error ('script.js:', this.script_name + ':', 'Unknown message:', msg);
  }
  // Handle incoming messages by resolving _promises entries
  async call (funid, ...args) {
    const callid = _counter++;
    this.worker.postMessage ({ funid, args, callid: callid });
    if (!this.promises)
      this.promises = new Map();
    return new Promise (r => this.promises.set (callid, r));
  }
  // remote call methods start with _
  _api_level () {
    return API_LEVEL;
  }
  _script_name () {
    return this.script_name;
  }
  _set_uuid (uuid) {
    this.script_uuid = uuid;
  }
  _register (category, label, funid, blurb) {
    category = category.replace (/^\/+|\/+$/g, ''); // strip slashes
    const category_list = registry.get (category) || [];
    if (category_list.length == 0)
      registry.set (category, category_list);
    const old = category_list.findIndex (e => e.funid == funid);
    if (old >= 0) // TODO: there should be no dups if scripts properly exit and have UUIDs
      category_list.splice (old, 1);
    category_list.push ({ label, funid, blurb, host: this });
  }
}
const registry = new Map();

// == run_script() ==
export async function run_script_fun (funid) {
  for (const [_key, list] of registry)
    for (const entry of list)
      if (entry.funid == funid) {
	return entry.host.call (entry.funid);
      }
  throw new Error ('script.js: Attempt to call unregistered funid: ' + funid);
}

// == Exports ==
export function registry_keys (category) {
  return registry.get (category) || [];
}

// == load_script() ==
export function load_script (url) {
  const host = new ScriptHost (url);
  script_hosts.push ({ url, host });
  return host;
}
const script_hosts = [];
