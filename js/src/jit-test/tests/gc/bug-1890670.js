// |jit-test| --enable-symbols-as-weakmap-keys

gczeal(0);
let wm = new WeakMap();
let s = Symbol();
wm.set(s, new WeakMap());
let ss = Symbol();
wm.get(s).set(this, ss);
let wm2 = new WeakMap();
wm2.set(ss, "test");
ss = null;
gc();
