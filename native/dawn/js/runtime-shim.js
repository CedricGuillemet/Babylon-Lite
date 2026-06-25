// runtime-shim.js
//
// Loaded BEFORE the Babylon-Lite scene bundle. Defines all browser globals the
// bundle expects (document, navigator.gpu, GPU* classes, fetch helpers, etc.)
// on top of the small native surface exposed by the C++ host:
//
//   __wgpu(opName, self, ...args)         WebGPU dispatch
//   __createGPUObject(typeName, handle)   wrap a native handle (called from C++)
//   document / window / navigator / fetch / requestAnimationFrame / etc.
//     (installed by dom_shim.cpp before this script runs)
//
// This file uses ONLY ES5-compatible features so it can be loaded as a classic
// script via JsRun rather than a module.

(function (g) {
  'use strict';

  // -------------------------------------------------------------------------
  // GPU* class registry & __createGPUObject helper
  // -------------------------------------------------------------------------
  var classes = {};
  function defineClass(name) {
    var Ctor = function (handle) {
      this.__h = handle | 0;
      this.__type = name;
      this.label = '';
    };
    Ctor.prototype.__type = name;
    classes[name] = Ctor;
    g[name] = Ctor;
    return Ctor;
  }

  g.__createGPUObject = function (name, handle) {
    var C = classes[name];
    if (!C) throw new Error('Unknown GPU class: ' + name);
    return new C(handle);
  };

  function defineMethod(cls, name, op) {
    cls.prototype[name] = function () {
      var a = arguments;
      switch (a.length) {
        case 0: return g.__wgpu(op, this);
        case 1: return g.__wgpu(op, this, a[0]);
        case 2: return g.__wgpu(op, this, a[0], a[1]);
        case 3: return g.__wgpu(op, this, a[0], a[1], a[2]);
        case 4: return g.__wgpu(op, this, a[0], a[1], a[2], a[3]);
        case 5: return g.__wgpu(op, this, a[0], a[1], a[2], a[3], a[4]);
        case 6: return g.__wgpu(op, this, a[0], a[1], a[2], a[3], a[4], a[5]);
        default:
          // Fallback: pack into an array
          var arr = []; for (var i = 0; i < a.length; ++i) arr.push(a[i]);
          return g.__wgpu(op, this, arr);
      }
    };
  }

  function defineGetter(cls, name, op) {
    Object.defineProperty(cls.prototype, name, {
      get: function () { return g.__wgpu(op, this); },
      configurable: true,
    });
  }

  // -------------------------------------------------------------------------
  // GPU* classes (each handle is a number — the C++ side knows the type)
  // -------------------------------------------------------------------------
  defineClass('GPUAdapter');
  defineClass('GPUDevice');
  defineClass('GPUQueue');
  defineClass('GPUBuffer');
  defineClass('GPUTexture');
  defineClass('GPUTextureView');
  defineClass('GPUSampler');
  defineClass('GPUShaderModule');
  defineClass('GPUBindGroupLayout');
  defineClass('GPUBindGroup');
  defineClass('GPUPipelineLayout');
  defineClass('GPURenderPipeline');
  defineClass('GPUComputePipeline');
  defineClass('GPUCommandEncoder');
  defineClass('GPUCommandBuffer');
  defineClass('GPURenderPassEncoder');
  defineClass('GPUComputePassEncoder');
  defineClass('GPURenderBundleEncoder');
  defineClass('GPURenderBundle');
  defineClass('GPUCanvasContext');
  defineClass('GPUQuerySet');

  defineMethod(classes.GPUAdapter, 'requestDevice', 'adapter.requestDevice');
  defineGetter(classes.GPUAdapter, 'features', 'adapter.features');
  defineGetter(classes.GPUAdapter, 'limits', 'adapter.limits');
  defineGetter(classes.GPUAdapter, 'info', 'adapter.info');
  classes.GPUAdapter.prototype.requestAdapterInfo = function () {
    return Promise.resolve(g.__wgpu('adapter.info', this));
  };

  defineMethod(classes.GPUDevice, 'createBuffer', 'device.createBuffer');
  defineMethod(classes.GPUDevice, 'createTexture', 'device.createTexture');
  defineMethod(classes.GPUDevice, 'createSampler', 'device.createSampler');
  defineMethod(classes.GPUDevice, 'createShaderModule', 'device.createShaderModule');
  defineMethod(classes.GPUDevice, 'createBindGroupLayout', 'device.createBindGroupLayout');
  defineMethod(classes.GPUDevice, 'createBindGroup', 'device.createBindGroup');
  defineMethod(classes.GPUDevice, 'createPipelineLayout', 'device.createPipelineLayout');
  defineMethod(classes.GPUDevice, 'createRenderPipeline', 'device.createRenderPipeline');
  defineMethod(classes.GPUDevice, 'createComputePipeline', 'device.createComputePipeline');
  classes.GPUDevice.prototype.createRenderPipelineAsync = function (d) {
    return Promise.resolve(this.createRenderPipeline(d));
  };
  classes.GPUDevice.prototype.createComputePipelineAsync = function (d) {
    return Promise.resolve(this.createComputePipeline(d));
  };
  defineMethod(classes.GPUDevice, 'createCommandEncoder', 'device.createCommandEncoder');
  defineMethod(classes.GPUDevice, 'createRenderBundleEncoder', 'device.createRenderBundleEncoder');
  defineMethod(classes.GPUDevice, 'createQuerySet', 'device.createQuerySet');
  defineMethod(classes.GPUDevice, 'destroy', 'device.destroy');
  classes.GPUDevice.prototype.pushErrorScope = function () {};
  classes.GPUDevice.prototype.popErrorScope = function () { return Promise.resolve(null); };
  defineGetter(classes.GPUDevice, 'features', 'device.features');
  defineGetter(classes.GPUDevice, 'limits', 'device.limits');
  Object.defineProperty(classes.GPUDevice.prototype, 'lost', {
    get: function () { return new Promise(function () {}); }, // never lost
    configurable: true,
  });
  classes.GPUDevice.prototype.addEventListener = function () {};
  classes.GPUDevice.prototype.removeEventListener = function () {};

  defineMethod(classes.GPUQueue, 'submit', 'queue.submit');
  defineMethod(classes.GPUQueue, 'writeBuffer', 'queue.writeBuffer');
  defineMethod(classes.GPUQueue, 'writeTexture', 'queue.writeTexture');
  defineMethod(classes.GPUQueue, 'onSubmittedWorkDone', 'queue.onSubmittedWorkDone');
  classes.GPUQueue.prototype.copyExternalImageToTexture = function (source, dest, copySize) {
    // Decode source image into bytes then call writeTexture.
    var bitmap = source.source || source;
    var pixels = bitmap.__pixels;
    if (!pixels) throw new Error('copyExternalImageToTexture: source has no decoded pixels');
    var w = bitmap.width, h = bitmap.height;
    this.writeTexture(
      dest,
      pixels,
      { offset: 0, bytesPerRow: w * 4, rowsPerImage: h },
      { width: w, height: h, depthOrArrayLayers: 1 }
    );
  };

  defineMethod(classes.GPUBuffer, 'mapAsync', 'buffer.mapAsync');
  defineMethod(classes.GPUBuffer, 'getMappedRange', 'buffer.getMappedRange');
  defineMethod(classes.GPUBuffer, 'unmap', 'buffer.unmap');
  defineMethod(classes.GPUBuffer, 'destroy', 'buffer.destroy');

  defineMethod(classes.GPUTexture, 'createView', 'texture.createView');
  defineMethod(classes.GPUTexture, 'destroy', 'texture.destroy');

  classes.GPUShaderModule.prototype.getCompilationInfo = function () {
    return Promise.resolve({ messages: [] });
  };

  defineMethod(classes.GPURenderPipeline, 'getBindGroupLayout', 'renderPipeline.getBindGroupLayout');
  defineMethod(classes.GPUComputePipeline, 'getBindGroupLayout', 'computePipeline.getBindGroupLayout');

  defineMethod(classes.GPUCommandEncoder, 'beginRenderPass', 'commandEncoder.beginRenderPass');
  defineMethod(classes.GPUCommandEncoder, 'beginComputePass', 'commandEncoder.beginComputePass');
  defineMethod(classes.GPUCommandEncoder, 'copyBufferToBuffer', 'commandEncoder.copyBufferToBuffer');
  defineMethod(classes.GPUCommandEncoder, 'copyBufferToTexture', 'commandEncoder.copyBufferToTexture');
  defineMethod(classes.GPUCommandEncoder, 'copyTextureToBuffer', 'commandEncoder.copyTextureToBuffer');
  defineMethod(classes.GPUCommandEncoder, 'copyTextureToTexture', 'commandEncoder.copyTextureToTexture');
  defineMethod(classes.GPUCommandEncoder, 'clearBuffer', 'commandEncoder.clearBuffer');
  defineMethod(classes.GPUCommandEncoder, 'finish', 'commandEncoder.finish');
  defineMethod(classes.GPUCommandEncoder, 'pushDebugGroup', 'commandEncoder.pushDebugGroup');
  defineMethod(classes.GPUCommandEncoder, 'popDebugGroup', 'commandEncoder.popDebugGroup');
  defineMethod(classes.GPUCommandEncoder, 'insertDebugMarker', 'commandEncoder.insertDebugMarker');

  defineMethod(classes.GPURenderPassEncoder, 'setPipeline', 'renderPass.setPipeline');
  defineMethod(classes.GPURenderPassEncoder, 'setBindGroup', 'renderPass.setBindGroup');
  defineMethod(classes.GPURenderPassEncoder, 'setVertexBuffer', 'renderPass.setVertexBuffer');
  defineMethod(classes.GPURenderPassEncoder, 'setIndexBuffer', 'renderPass.setIndexBuffer');
  defineMethod(classes.GPURenderPassEncoder, 'draw', 'renderPass.draw');
  defineMethod(classes.GPURenderPassEncoder, 'drawIndexed', 'renderPass.drawIndexed');
  defineMethod(classes.GPURenderPassEncoder, 'drawIndirect', 'renderPass.drawIndirect');
  defineMethod(classes.GPURenderPassEncoder, 'drawIndexedIndirect', 'renderPass.drawIndexedIndirect');
  defineMethod(classes.GPURenderPassEncoder, 'setViewport', 'renderPass.setViewport');
  defineMethod(classes.GPURenderPassEncoder, 'setScissorRect', 'renderPass.setScissorRect');
  defineMethod(classes.GPURenderPassEncoder, 'setBlendConstant', 'renderPass.setBlendConstant');
  defineMethod(classes.GPURenderPassEncoder, 'setStencilReference', 'renderPass.setStencilReference');
  defineMethod(classes.GPURenderPassEncoder, 'executeBundles', 'renderPass.executeBundles');
  defineMethod(classes.GPURenderPassEncoder, 'end', 'renderPass.end');
  defineMethod(classes.GPURenderPassEncoder, 'pushDebugGroup', 'renderPass.pushDebugGroup');
  defineMethod(classes.GPURenderPassEncoder, 'popDebugGroup', 'renderPass.popDebugGroup');
  defineMethod(classes.GPURenderPassEncoder, 'insertDebugMarker', 'renderPass.insertDebugMarker');

  defineMethod(classes.GPUComputePassEncoder, 'setPipeline', 'computePass.setPipeline');
  defineMethod(classes.GPUComputePassEncoder, 'setBindGroup', 'computePass.setBindGroup');
  defineMethod(classes.GPUComputePassEncoder, 'dispatchWorkgroups', 'computePass.dispatchWorkgroups');
  defineMethod(classes.GPUComputePassEncoder, 'dispatchWorkgroupsIndirect', 'computePass.dispatchWorkgroupsIndirect');
  defineMethod(classes.GPUComputePassEncoder, 'end', 'computePass.end');

  defineMethod(classes.GPURenderBundleEncoder, 'setPipeline', 'renderBundle.setPipeline');
  defineMethod(classes.GPURenderBundleEncoder, 'setBindGroup', 'renderBundle.setBindGroup');
  defineMethod(classes.GPURenderBundleEncoder, 'setVertexBuffer', 'renderBundle.setVertexBuffer');
  defineMethod(classes.GPURenderBundleEncoder, 'setIndexBuffer', 'renderBundle.setIndexBuffer');
  defineMethod(classes.GPURenderBundleEncoder, 'draw', 'renderBundle.draw');
  defineMethod(classes.GPURenderBundleEncoder, 'drawIndexed', 'renderBundle.drawIndexed');
  defineMethod(classes.GPURenderBundleEncoder, 'drawIndirect', 'renderBundle.drawIndirect');
  defineMethod(classes.GPURenderBundleEncoder, 'drawIndexedIndirect', 'renderBundle.drawIndexedIndirect');
  defineMethod(classes.GPURenderBundleEncoder, 'pushDebugGroup', 'renderBundle.pushDebugGroup');
  defineMethod(classes.GPURenderBundleEncoder, 'popDebugGroup', 'renderBundle.popDebugGroup');
  defineMethod(classes.GPURenderBundleEncoder, 'insertDebugMarker', 'renderBundle.insertDebugMarker');
  defineMethod(classes.GPURenderBundleEncoder, 'finish', 'renderBundle.finish');

  defineMethod(classes.GPUCanvasContext, 'configure', 'canvasContext.configure');
  defineMethod(classes.GPUCanvasContext, 'unconfigure', 'canvasContext.unconfigure');
  defineMethod(classes.GPUCanvasContext, 'getCurrentTexture', 'canvasContext.getCurrentTexture');

  // The single canvas context the host exposes. The native side allocates a
  // handle of 1 for it (canvas-bound surface).
  g.__webgpuContext = new classes.GPUCanvasContext(1);
  g.__webgpuContext.canvas = g.__canvas;
  g.__canvas.__context = g.__webgpuContext;

  // -------------------------------------------------------------------------
  // WebGPU constants
  // -------------------------------------------------------------------------
  g.GPUBufferUsage = Object.freeze({
    MAP_READ: 0x0001, MAP_WRITE: 0x0002, COPY_SRC: 0x0004, COPY_DST: 0x0008,
    INDEX: 0x0010, VERTEX: 0x0020, UNIFORM: 0x0040, STORAGE: 0x0080,
    INDIRECT: 0x0100, QUERY_RESOLVE: 0x0200,
  });
  g.GPUTextureUsage = Object.freeze({
    COPY_SRC: 0x01, COPY_DST: 0x02, TEXTURE_BINDING: 0x04,
    STORAGE_BINDING: 0x08, RENDER_ATTACHMENT: 0x10,
  });
  g.GPUShaderStage = Object.freeze({
    VERTEX: 0x1, FRAGMENT: 0x2, COMPUTE: 0x4,
  });
  g.GPUColorWrite = Object.freeze({
    RED: 0x1, GREEN: 0x2, BLUE: 0x4, ALPHA: 0x8, ALL: 0xF,
  });
  g.GPUMapMode = Object.freeze({ READ: 0x1, WRITE: 0x2 });

  // -------------------------------------------------------------------------
  // navigator.gpu
  // -------------------------------------------------------------------------
  function GPU() {}
  GPU.prototype.requestAdapter = function (opts) {
    return g.__wgpu('gpu.requestAdapter', undefined, opts || {});
  };
  GPU.prototype.getPreferredCanvasFormat = function () {
    return g.__wgpu('gpu.getPreferredCanvasFormat', undefined);
  };
  // Sets used by feature-detect code.
  GPU.prototype.wgslLanguageFeatures = (function () {
    var s = new Set();
    return s;
  })();
  g.navigator.gpu = new GPU();

  // -------------------------------------------------------------------------
  // URL — minimal parser sufficient for Babylon-Lite's resource paths.
  // -------------------------------------------------------------------------
  if (typeof g.URL === 'undefined') {
    g.URL = function URL(input, base) {
      var s = String(input);
      if (base && !/^[a-z][a-z0-9+.-]*:/i.test(s)) {
        // Resolve relative to base.
        var b = String(base);
        var bSlash = b.lastIndexOf('/');
        s = (bSlash >= 0 ? b.substr(0, bSlash + 1) : b + '/') + s;
      }
      this.href = s;
      // very rough parse
      var m = /^([a-z]+:\/\/[^/]+)?(\/?[^?#]*)(\?[^#]*)?(#.*)?$/i.exec(s) || [];
      this.origin = m[1] || '';
      this.pathname = m[2] || '';
      this.search = m[3] || '';
      this.hash = m[4] || '';
      this.protocol = (this.origin.split(':')[0] || '') + ':';
      this.host = (this.origin.split('://')[1] || '');
      this.hostname = this.host.split(':')[0];
      this.port = this.host.split(':')[1] || '';
    };
    g.URL.prototype.toString = function () { return this.href; };
    g.URL.createObjectURL = function () { return ''; };
    g.URL.revokeObjectURL = function () {};
  }

  // Blob — minimal stub.
  if (typeof g.Blob === 'undefined') {
    g.Blob = function Blob(parts, opts) {
      this._parts = parts || [];
      this.type = (opts && opts.type) || '';
      this.size = 0;
      var i;
      for (i = 0; i < this._parts.length; ++i) {
        var p = this._parts[i];
        if (p instanceof ArrayBuffer) this.size += p.byteLength;
        else if (ArrayBuffer.isView(p)) this.size += p.byteLength;
        else if (typeof p === 'string') this.size += p.length;
      }
    };
    g.Blob.prototype.arrayBuffer = function () {
      var total = 0, i;
      for (i = 0; i < this._parts.length; ++i) {
        var p = this._parts[i];
        if (p instanceof ArrayBuffer) total += p.byteLength;
        else if (ArrayBuffer.isView(p)) total += p.byteLength;
      }
      var out = new Uint8Array(total);
      var off = 0;
      for (i = 0; i < this._parts.length; ++i) {
        var p = this._parts[i];
        if (p instanceof ArrayBuffer) { out.set(new Uint8Array(p), off); off += p.byteLength; }
        else if (ArrayBuffer.isView(p)) { out.set(new Uint8Array(p.buffer, p.byteOffset, p.byteLength), off); off += p.byteLength; }
      }
      return Promise.resolve(out.buffer);
    };
    g.Blob.prototype.text = function () {
      return this.arrayBuffer().then(function (ab) { return new g.TextDecoder().decode(new Uint8Array(ab)); });
    };
    g.Blob.prototype.slice = function (start, end, type) {
      var self = this;
      // Build a new Blob with concatenated bytes of the slice.
      // Lazy: do an arrayBuffer round-trip.
      // Note: returns a Blob synchronously holding a Promise; Babylon-Lite uses
      // the returned Blob's arrayBuffer()/etc which we override.
      var b = new g.Blob([], { type: type || self.type });
      b._slice = { src: self, start: start || 0, end: end };
      b.arrayBuffer = function () {
        return self.arrayBuffer().then(function (ab) {
          var e = (typeof end === 'number') ? end : ab.byteLength;
          return ab.slice(b._slice.start, e);
        });
      };
      return b;
    };
    // Blob.stream() — return a minimal ReadableStream-like that exposes
    // getReader().read() yielding the whole buffer in one chunk.
    g.Blob.prototype.stream = function () {
      var self = this;
      var done = false;
      return {
        getReader: function () {
          return {
            read: function () {
              if (done) return Promise.resolve({ value: undefined, done: true });
              done = true;
              return self.arrayBuffer().then(function (ab) {
                return { value: new Uint8Array(ab), done: false };
              });
            },
            releaseLock: function () {},
            cancel: function () { done = true; return Promise.resolve(); },
          };
        },
      };
    };
  }

  // ImageBitmap minimal stub (createImageBitmap is provided by host).

  // matchMedia stub (Babylon may probe).
  if (typeof g.matchMedia === 'undefined') {
    g.matchMedia = function () { return { matches: false, addEventListener: function () {}, removeEventListener: function () {} }; };
  }

  // DOMException stub.
  if (typeof g.DOMException === 'undefined') {
    g.DOMException = function DOMException(msg, name) { this.message = msg || ''; this.name = name || 'Error'; };
  }

  // Crypto stub (some bundles call crypto.getRandomValues for IDs).
  if (typeof g.crypto === 'undefined') {
    g.crypto = {
      getRandomValues: function (a) {
        for (var i = 0; i < a.length; ++i) a[i] = (Math.random() * 0x100000000) | 0;
        return a;
      },
      randomUUID: function () {
        function h(n) { var s = ''; for (var i = 0; i < n; ++i) s += ((Math.random() * 16) | 0).toString(16); return s; }
        return h(8) + '-' + h(4) + '-4' + h(3) + '-' + h(4) + '-' + h(12);
      },
    };
  }

  // -------------------------------------------------------------------------
  // atob / btoa (base64) — many scenes encode data URLs / inline assets.
  // -------------------------------------------------------------------------
  if (typeof g.atob === 'undefined') {
    var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    var B64REV = (function () { var m = {}; for (var i = 0; i < B64.length; ++i) m[B64.charAt(i)] = i; return m; })();
    g.atob = function (s) {
      s = String(s).replace(/[^A-Za-z0-9+/]/g, '');
      var out = '';
      for (var i = 0; i < s.length; i += 4) {
        var c1 = B64REV[s.charAt(i)] | 0;
        var c2 = B64REV[s.charAt(i + 1)] | 0;
        var c3 = B64REV[s.charAt(i + 2)] | 0;
        var c4 = B64REV[s.charAt(i + 3)] | 0;
        out += String.fromCharCode((c1 << 2) | (c2 >> 4));
        if (s.charAt(i + 2) && s.charAt(i + 2) !== '=') out += String.fromCharCode(((c2 & 0xF) << 4) | (c3 >> 2));
        if (s.charAt(i + 3) && s.charAt(i + 3) !== '=') out += String.fromCharCode(((c3 & 0x3) << 6) | c4);
      }
      return out;
    };
    g.btoa = function (s) {
      s = String(s);
      var out = '', i;
      for (i = 0; i < s.length; i += 3) {
        var b1 = s.charCodeAt(i), b2 = s.charCodeAt(i + 1), b3 = s.charCodeAt(i + 2);
        var c1 = b1 >> 2;
        var c2 = ((b1 & 0x3) << 4) | ((isNaN(b2) ? 0 : b2) >> 4);
        var c3 = isNaN(b2) ? 64 : (((b2 & 0xF) << 2) | ((isNaN(b3) ? 0 : b3) >> 6));
        var c4 = isNaN(b3) ? 64 : (b3 & 0x3F);
        out += B64.charAt(c1) + B64.charAt(c2) + (c3 === 64 ? '=' : B64.charAt(c3)) + (c4 === 64 ? '=' : B64.charAt(c4));
      }
      return out;
    };
  }

  // -------------------------------------------------------------------------
  // URLSearchParams (used by Vite-style bundle helpers and lots of scenes).
  // -------------------------------------------------------------------------
  if (typeof g.URLSearchParams === 'undefined') {
    g.URLSearchParams = function URLSearchParams(init) {
      this._pairs = [];
      if (typeof init === 'string') {
        var s = init.charAt(0) === '?' ? init.slice(1) : init;
        if (s) {
          var parts = s.split('&');
          for (var i = 0; i < parts.length; ++i) {
            var eq = parts[i].indexOf('=');
            var k = eq < 0 ? parts[i] : parts[i].slice(0, eq);
            var v = eq < 0 ? '' : parts[i].slice(eq + 1);
            this._pairs.push([decodeURIComponent(k.replace(/\+/g, ' ')), decodeURIComponent(v.replace(/\+/g, ' '))]);
          }
        }
      } else if (init && typeof init === 'object') {
        if (init instanceof g.URLSearchParams) {
          for (var j = 0; j < init._pairs.length; ++j) this._pairs.push(init._pairs[j].slice());
        } else if (Array.isArray(init)) {
          for (var k = 0; k < init.length; ++k) this._pairs.push([String(init[k][0]), String(init[k][1])]);
        } else {
          for (var key in init) if (Object.prototype.hasOwnProperty.call(init, key)) {
            this._pairs.push([key, String(init[key])]);
          }
        }
      }
    };
    var P = g.URLSearchParams.prototype;
    P.append = function (k, v) { this._pairs.push([String(k), String(v)]); };
    P.delete = function (k) { this._pairs = this._pairs.filter(function (p) { return p[0] !== k; }); };
    P.get = function (k) { for (var i = 0; i < this._pairs.length; ++i) if (this._pairs[i][0] === k) return this._pairs[i][1]; return null; };
    P.getAll = function (k) { return this._pairs.filter(function (p) { return p[0] === k; }).map(function (p) { return p[1]; }); };
    P.has = function (k) { return this.get(k) !== null; };
    P.set = function (k, v) { this.delete(k); this.append(k, v); };
    P.toString = function () {
      return this._pairs.map(function (p) {
        return encodeURIComponent(p[0]) + '=' + encodeURIComponent(p[1]);
      }).join('&');
    };
    P.forEach = function (cb, thisArg) {
      for (var i = 0; i < this._pairs.length; ++i) cb.call(thisArg, this._pairs[i][1], this._pairs[i][0], this);
    };
    P.entries = function () { var i = 0, p = this._pairs; return { next: function () { return i < p.length ? { value: p[i++].slice(), done: false } : { value: undefined, done: true }; } }; };
    P.keys = function () { var i = 0, p = this._pairs; return { next: function () { return i < p.length ? { value: p[i++][0], done: false } : { value: undefined, done: true }; } }; };
    P.values = function () { var i = 0, p = this._pairs; return { next: function () { return i < p.length ? { value: p[i++][1], done: false } : { value: undefined, done: true }; } }; };
  }

  // -------------------------------------------------------------------------
  // Response constructor — used by some scenes to build fake Responses.
  // -------------------------------------------------------------------------
  if (typeof g.Response === 'undefined') {
    function Response(body, init) {
      this.ok = (init && init.status) ? (init.status >= 200 && init.status < 300) : true;
      this.status = (init && init.status) || 200;
      this.statusText = (init && init.statusText) || 'OK';
      this.headers = (init && init.headers) || {};
      this._body = body;
    }
    Response.prototype.arrayBuffer = function () {
      var b = this._body;
      if (b instanceof ArrayBuffer) return Promise.resolve(b);
      if (ArrayBuffer.isView(b)) return Promise.resolve(b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength));
      if (b && typeof b.arrayBuffer === 'function') return b.arrayBuffer();
      if (typeof b === 'string') { var enc = new g.TextEncoder().encode(b); return Promise.resolve(enc.buffer); }
      return Promise.resolve(new ArrayBuffer(0));
    };
    Response.prototype.text = function () {
      var b = this._body;
      if (typeof b === 'string') return Promise.resolve(b);
      return this.arrayBuffer().then(function (ab) { return new g.TextDecoder().decode(new Uint8Array(ab)); });
    };
    Response.prototype.json = function () { return this.text().then(JSON.parse); };
    Response.prototype.blob = function () {
      var b = this._body;
      if (b instanceof g.Blob) return Promise.resolve(b);
      return this.arrayBuffer().then(function (ab) { return new g.Blob([ab]); });
    };
    g.Response = Response;
  }
  if (typeof g.Request === 'undefined') {
    g.Request = function Request(input, init) {
      this.url = (input && input.url) || String(input);
      this.method = (init && init.method) || 'GET';
      this.headers = (init && init.headers) || {};
    };
  }
  if (typeof g.Headers === 'undefined') {
    g.Headers = function Headers(init) {
      this._map = {};
      if (init) for (var k in init) if (Object.prototype.hasOwnProperty.call(init, k)) this._map[k.toLowerCase()] = String(init[k]);
    };
    g.Headers.prototype.get = function (k) { return this._map[String(k).toLowerCase()] || null; };
    g.Headers.prototype.set = function (k, v) { this._map[String(k).toLowerCase()] = String(v); };
    g.Headers.prototype.has = function (k) { return Object.prototype.hasOwnProperty.call(this._map, String(k).toLowerCase()); };
    g.Headers.prototype.append = function (k, v) {
      var key = String(k).toLowerCase();
      this._map[key] = this._map[key] ? this._map[key] + ', ' + String(v) : String(v);
    };
    g.Headers.prototype.delete = function (k) { delete this._map[String(k).toLowerCase()]; };
    g.Headers.prototype.forEach = function (cb, thisArg) {
      for (var k in this._map) if (Object.prototype.hasOwnProperty.call(this._map, k)) cb.call(thisArg, this._map[k], k, this);
    };
  }

  // TextDecoder/TextEncoder minimal (UTF-8 only).
  if (typeof g.TextDecoder === 'undefined') {
    g.TextDecoder = function (label) { this.label = label || 'utf-8'; };
    g.TextDecoder.prototype.decode = function (input) {
      if (!input) return '';
      var bytes = input instanceof Uint8Array ? input : new Uint8Array(input.buffer || input);
      // Use a simple UTF-8 decoder.
      var out = '', i = 0;
      while (i < bytes.length) {
        var c = bytes[i++];
        if (c < 0x80) { out += String.fromCharCode(c); }
        else if (c < 0xC0) { out += '\uFFFD'; }
        else if (c < 0xE0) { out += String.fromCharCode(((c & 0x1F) << 6) | (bytes[i++] & 0x3F)); }
        else if (c < 0xF0) { out += String.fromCharCode(((c & 0x0F) << 12) | ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F)); }
        else {
          var cp = ((c & 0x07) << 18) | ((bytes[i++] & 0x3F) << 12) | ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F);
          cp -= 0x10000;
          out += String.fromCharCode(0xD800 | (cp >> 10), 0xDC00 | (cp & 0x3FF));
        }
      }
      return out;
    };
  }
  if (typeof g.TextEncoder === 'undefined') {
    g.TextEncoder = function () {};
    g.TextEncoder.prototype.encode = function (s) {
      s = String(s);
      var bytes = [], i = 0;
      while (i < s.length) {
        var c = s.charCodeAt(i++);
        if (c < 0x80) bytes.push(c);
        else if (c < 0x800) { bytes.push(0xC0 | (c >> 6), 0x80 | (c & 0x3F)); }
        else if ((c & 0xFC00) === 0xD800 && i < s.length) {
          var l = s.charCodeAt(i++);
          var cp = 0x10000 + (((c & 0x3FF) << 10) | (l & 0x3FF));
          bytes.push(0xF0 | (cp >> 18), 0x80 | ((cp >> 12) & 0x3F), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F));
        } else { bytes.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 0x3F), 0x80 | (c & 0x3F)); }
      }
      return new Uint8Array(bytes);
    };
  }

  // Image stub for createImageBitmap pathways that pass <img>-like sources.
  if (typeof g.Image === 'undefined') {
    g.Image = function () { this.width = 0; this.height = 0; this.complete = false; };
    g.Image.prototype.addEventListener = function () {};
  }

  // ResizeObserver stub.
  if (typeof g.ResizeObserver === 'undefined') {
    g.ResizeObserver = function () { this.observe = function () {}; this.unobserve = function () {}; this.disconnect = function () {}; };
  }
  // IntersectionObserver / MutationObserver stubs.
  if (typeof g.IntersectionObserver === 'undefined') {
    g.IntersectionObserver = function () { this.observe = function () {}; this.disconnect = function () {}; };
  }
  if (typeof g.MutationObserver === 'undefined') {
    g.MutationObserver = function () { this.observe = function () {}; this.disconnect = function () {}; };
  }

  // queueMicrotask
  if (typeof g.queueMicrotask === 'undefined') {
    g.queueMicrotask = function (fn) { Promise.resolve().then(fn); };
  }

  // -------------------------------------------------------------------------
  // DecompressionStream / CompressionStream — stub that yields the input
  // bytes unchanged. Real gzip/deflate decoding isn't implemented; callers
  // that depend on actual decompression will fail downstream with format
  // errors, which is preferable to a ReferenceError that kills the bundle
  // before it can report a useful message.
  // -------------------------------------------------------------------------
  if (typeof g.DecompressionStream === 'undefined') {
    function PassthroughStream(name) { this.name = name; }
    PassthroughStream.prototype.writable = null;
    PassthroughStream.prototype.readable = null;
    g.DecompressionStream = function (format) {
      var chunks = [];
      var resolveReader, readerPromise = new Promise(function (r) { resolveReader = r; });
      var closed = false;
      this.writable = {
        getWriter: function () {
          return {
            write: function (chunk) {
              if (chunk) chunks.push(chunk instanceof Uint8Array ? chunk : new Uint8Array(chunk.buffer || chunk));
              return Promise.resolve();
            },
            close: function () { closed = true; resolveReader(); return Promise.resolve(); },
            abort: function () { return Promise.resolve(); },
            releaseLock: function () {},
          };
        },
      };
      this.readable = {
        getReader: function () {
          var emitted = false;
          return {
            read: function () {
              return readerPromise.then(function () {
                if (emitted) return { value: undefined, done: true };
                emitted = true;
                var total = 0;
                for (var i = 0; i < chunks.length; ++i) total += chunks[i].byteLength;
                var out = new Uint8Array(total);
                var off = 0;
                for (var j = 0; j < chunks.length; ++j) {
                  out.set(chunks[j], off);
                  off += chunks[j].byteLength;
                }
                return { value: out, done: false };
              });
            },
            releaseLock: function () {},
            cancel: function () { return Promise.resolve(); },
          };
        },
      };
    };
    g.CompressionStream = g.DecompressionStream;
  }

  // -------------------------------------------------------------------------
  // ReadableStream-like helpers — pipeThrough/pipeTo on Blob.stream() and
  // DecompressionStream.readable so the gaussian-splatting bundles can chain
  // Blob.stream().pipeThrough(new DecompressionStream('gzip')).
  // -------------------------------------------------------------------------
  function rsPipeThrough(transform) {
    var src = this.getReader();
    var w = transform.writable.getWriter();
    function pump() {
      return src.read().then(function (r) {
        if (r.done) return w.close();
        return w.write(r.value).then(pump);
      });
    }
    pump().catch(function (e) { console.error('pipeThrough error:', e); });
    return transform.readable;
  }
  function rsPipeTo(writable) {
    var src = this.getReader();
    var w = writable.getWriter();
    function pump() {
      return src.read().then(function (r) {
        if (r.done) return w.close();
        return w.write(r.value).then(pump);
      });
    }
    return pump();
  }
  function wrapAsReadable(obj) {
    if (!obj || typeof obj.getReader !== 'function') return obj;
    obj.pipeThrough = rsPipeThrough;
    obj.pipeTo = rsPipeTo;
    return obj;
  }
  var __origBlobStream = g.Blob && g.Blob.prototype && g.Blob.prototype.stream;
  if (__origBlobStream) {
    g.Blob.prototype.stream = function () { return wrapAsReadable(__origBlobStream.call(this)); };
  }
  var __origDStrmCtor = g.DecompressionStream;
  if (__origDStrmCtor) {
    g.DecompressionStream = function (fmt) {
      var s = new __origDStrmCtor(fmt);
      wrapAsReadable(s.readable);
      return s;
    };
    g.CompressionStream = g.DecompressionStream;
  }

  // -------------------------------------------------------------------------
  // BigInt64Array / BigUint64Array — ChakraCore 1.11 lacks BigInt entirely.
  // Stub backed by Float64Array so reads/writes succeed (values are imprecise
  // beyond 2^53 but the bundles only index with sizes that fit a double).
  // -------------------------------------------------------------------------
  if (typeof g.BigInt === 'undefined') {
    g.BigInt = function (v) { return Number(v); };
  }
  if (typeof g.BigInt64Array === 'undefined') {
    g.BigInt64Array = Float64Array;
    g.BigUint64Array = Float64Array;
  }
  // -------------------------------------------------------------------------
  // ES2019+ Array polyfills (ChakraCore 1.11 doesn't have flat/flatMap).
  // -------------------------------------------------------------------------
  if (!Array.prototype.flat) {
    Array.prototype.flat = function (depth) {
      depth = depth === undefined ? 1 : depth | 0;
      var out = [];
      (function rec(arr, d) {
        for (var i = 0; i < arr.length; ++i) {
          var v = arr[i];
          if (Array.isArray(v) && d > 0) rec(v, d - 1);
          else out.push(v);
        }
      })(this, depth);
      return out;
    };
  }
  if (!Array.prototype.flatMap) {
    Array.prototype.flatMap = function (cb, thisArg) {
      var out = [];
      for (var i = 0; i < this.length; ++i) {
        var v = cb.call(thisArg, this[i], i, this);
        if (Array.isArray(v)) { for (var j = 0; j < v.length; ++j) out.push(v[j]); }
        else out.push(v);
      }
      return out;
    };
  }
  // String.prototype.replaceAll (ES2021)
  if (!String.prototype.replaceAll) {
    String.prototype.replaceAll = function (search, replace) {
      if (search && typeof search === 'object' && search.constructor === RegExp) {
        if (!search.global) throw new TypeError('replaceAll: regex must be global');
        return this.replace(search, replace);
      }
      var s = String(search);
      return this.split(s).join(String(replace));
    };
  }
  // Object.fromEntries (ES2019)
  if (!Object.fromEntries) {
    Object.fromEntries = function (entries) {
      var o = {};
      if (entries && typeof entries.forEach === 'function') {
        entries.forEach(function (e) { o[e[0]] = e[1]; });
      } else {
        for (var i = 0; i < entries.length; ++i) o[entries[i][0]] = entries[i][1];
      }
      return o;
    };
  }
  // String.prototype.matchAll
  if (!String.prototype.matchAll) {
    String.prototype.matchAll = function (re) {
      var src = String(this);
      var flags = re.flags + (re.flags.indexOf('g') >= 0 ? '' : 'g');
      var r = new RegExp(re.source, flags);
      var out = [];
      var m;
      while ((m = r.exec(src)) !== null) out.push(m);
      return out;
    };
  }
  // Promise.allSettled (ES2020)
  if (!Promise.allSettled) {
    Promise.allSettled = function (promises) {
      return Promise.all(Array.from(promises).map(function (p) {
        return Promise.resolve(p).then(function (v) { return { status: 'fulfilled', value: v }; },
                                        function (r) { return { status: 'rejected', reason: r }; });
      }));
    };
  }
  // Promise.any (ES2021) — minimal
  if (!Promise.any) {
    Promise.any = function (promises) {
      var arr = Array.from(promises);
      return new Promise(function (resolve, reject) {
        var rejected = 0, errors = new Array(arr.length);
        arr.forEach(function (p, i) {
          Promise.resolve(p).then(resolve, function (e) {
            errors[i] = e; if (++rejected === arr.length) reject(new AggregateError(errors));
          });
        });
      });
    };
  }
  if (typeof g.AggregateError === 'undefined') {
    g.AggregateError = function (errors, message) { this.errors = errors; this.message = message || ''; };
  }
  // globalThis already aliased above. Number.isFinite/isInteger should exist (ES2015).

  // structuredClone (minimal: handles arrays / plain objects / typed arrays)
  if (typeof g.structuredClone === 'undefined') {
    g.structuredClone = function structuredClone(v) {
      if (v === null || typeof v !== 'object') return v;
      if (v instanceof ArrayBuffer) return v.slice(0);
      if (ArrayBuffer.isView(v)) return new v.constructor(structuredClone(v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength)));
      if (Array.isArray(v)) return v.map(structuredClone);
      var o = {};
      for (var k in v) if (Object.prototype.hasOwnProperty.call(v, k)) o[k] = structuredClone(v[k]);
      return o;
    };
  }

  // Reusable wrapper for the host to ask JS to present the swap chain.
  g.__present = function () { g.__wgpu('present', undefined); };

  // -------------------------------------------------------------------------
  // Worker — backed by the native __createWorker / __workerPost / __workerTerminate
  // primitives. Each worker runs in its own thread + Chakra runtime.
  //
  // The native bindings are installed AFTER this shim, so we define Worker
  // lazily: first access reads `__createWorker` from globalThis.
  // -------------------------------------------------------------------------
  if (typeof g.Worker === 'undefined') {
    function Worker(url, opts) {
      if (typeof g.__createWorker !== 'function') {
        throw new Error('Worker: native binding not installed');
      }
      var spec = url;
      if (url && typeof url === 'object' && url.href) spec = url.href;
      this.__id = g.__createWorker(String(spec));
      this.__onmessage = null;
      this.__onerror = null;
      g.__workerTable[this.__id] = this;
    }
    Object.defineProperty(Worker.prototype, 'onmessage', {
      get: function () { return this.__onmessage; },
      set: function (v) { this.__onmessage = v; },
      configurable: true,
    });
    Object.defineProperty(Worker.prototype, 'onerror', {
      get: function () { return this.__onerror; },
      set: function (v) { this.__onerror = v; },
      configurable: true,
    });
    Worker.prototype.addEventListener = function (type, fn) {
      if (type === 'message') this.__onmessage = fn;
      else if (type === 'error') this.__onerror = fn;
    };
    Worker.prototype.removeEventListener = function (type) {
      if (type === 'message') this.__onmessage = null;
      else if (type === 'error') this.__onerror = null;
    };
    Worker.prototype.postMessage = function (data /*, transferList */) {
      g.__workerPost(this.__id, data);
    };
    Worker.prototype.terminate = function () {
      g.__workerTerminate(this.__id);
      delete g.__workerTable[this.__id];
    };
    g.Worker = Worker;
  }

  // -------------------------------------------------------------------------
  // WebAssembly stub — surface as "not supported" via a recognizable Error so
  // the host's run loop can detect and report skipped scenes. We don't actually
  // run any WASM; any access on `WebAssembly.*` produces a controlled failure.
  // -------------------------------------------------------------------------
  if (typeof g.WebAssembly === 'undefined') {
    var WASM_ERR = 'WebAssembly is not supported in this host (DawnTest/ChakraCore)';
    function markWasm() { g.__wasmTriggered = 'true'; }
    function wasmThrow() { markWasm(); var e = new Error(WASM_ERR); e.__wasmUnsupported = true; throw e; }
    g.WebAssembly = {
      instantiate: function () { markWasm(); return Promise.reject(new Error(WASM_ERR)); },
      instantiateStreaming: function () { markWasm(); return Promise.reject(new Error(WASM_ERR)); },
      compile: function () { markWasm(); return Promise.reject(new Error(WASM_ERR)); },
      compileStreaming: function () { markWasm(); return Promise.reject(new Error(WASM_ERR)); },
      validate: function () { markWasm(); return false; },
      Module: function () { wasmThrow(); },
      Instance: function () { wasmThrow(); },
      Memory: function () { wasmThrow(); },
      Table: function () { wasmThrow(); },
      Global: function () { wasmThrow(); },
      LinkError: function () {},
      CompileError: function () {},
      RuntimeError: function () {},
    };
    g.__wasmUnsupportedMarker = WASM_ERR;
  }

  // Hook for the main loop to surface unhandled rejections.
  g.addEventListener && g.addEventListener('unhandledrejection', function (e) {
    var msg = e.reason && (e.reason.stack || e.reason.message || e.reason);
    console.error('unhandledrejection:', msg);
    // Don't mark as fatal — many scenes have benign rejection paths.
  });
  g.addEventListener && g.addEventListener('error', function (e) {
    console.error('uncaught:', e.message || e);
    g.__fatalError = 'true';
  });
})(globalThis);
