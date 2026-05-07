// hello-js: alice.* 标准库全面测试 (QuickJS)

let passed = 0;
let failed = 0;

function ok(name) {
    passed++;
    alice.log.info("  ✅ " + name);
}

function fail(name, reason) {
    failed++;
    alice.log.error("  ❌ " + name + ": " + String(reason));
}

function test(name, fn) {
    try { fn(); } catch (e) { fail(name, e); }
}

function onLoad() {
    alice.log.info("========================================");
    alice.log.info("  alice.* JS 标准库全面测试");
    alice.log.info("========================================");

    // 1. alice.log
    alice.log.info("");
    alice.log.info("🔹 1. alice.log");
    test("log.info", () => alice.log.info("  log.info 测试"));
    test("log.warn", () => alice.log.warn("  log.warn 测试"));
    test("log.debug", () => alice.log.debug("  log.debug 测试"));
    ok("alice.log (info/warn/debug)");

    // 2. alice.fs
    alice.log.info("");
    alice.log.info("🔹 2. alice.fs");
    test("fs.write+read", () => {
        alice.fs.write("_test_js.txt", "Hello from JS!");
        const c = alice.fs.read("_test_js.txt");
        if (c !== "Hello from JS!") throw "read 不匹配: " + c;
        ok("fs.write+read → " + c);
    });
    test("fs.exists true", () => {
        if (alice.fs.exists("_test_js.txt") !== true) throw "应为 true";
        ok("fs.exists(存在) → true");
    });
    test("fs.exists false", () => {
        if (alice.fs.exists("_no_such_99999.txt") !== false) throw "应为 false";
        ok("fs.exists(不存在) → false");
    });

    // 3. alice.kv
    alice.log.info("");
    alice.log.info("🔹 3. alice.kv");
    test("kv.set+get", () => {
        alice.kv.set("js_test", '{"x":42}');
        const v = alice.kv.get("js_test");
        if (!v || v.indexOf("42") < 0) throw "kv 不匹配: " + v;
        ok("kv.set+get → " + v);
    });
    test("kv.get nonexist", () => {
        const v = alice.kv.get("nonexist_js_99999");
        if (v !== null) throw "应为 null, got: " + v;
        ok("kv.get(不存在) → null");
    });

    // 4. alice.event
    alice.log.info("");
    alice.log.info("🔹 4. alice.event");
    test("event.emit+on+off", () => {
        let received = false;
        const handle = alice.event.on("test.js.stdlib", (data_json) => {
            received = true;
        });
        alice.event.emit("test.js.stdlib", '{"test":true}');
        if (!received) throw "事件未收到";
        alice.event.off(handle);
        ok("event.emit+on+off");
    });

    // 5. alice.service
    alice.log.info("");
    alice.log.info("🔹 5. alice.service");
    test("service.register+call", () => {
        alice.service.register("test.js.echo", (method, args_json) => {
            return '{"echo":"' + method + '","args":' + args_json + '}';
        });
        const r = alice.service.call("test.js.echo", "ping", '{"data":"hello"}');
        if (r.indexOf("ping") < 0) throw "调用不匹配: " + r;
        ok("service.register+call → " + r);
    });

    // 6. alice.net
    alice.log.info("");
    alice.log.info("🔹 6. alice.net");
    test("net.fetch GET", () => {
        const resp = alice.net.fetch("https://httpbin.org/get");
        if (!resp) throw "fetch 返回 null";
        const data = JSON.parse(resp);
        if (data.status !== 200) throw "status 不是 200";
        ok("net.fetch(GET) → status=" + data.status);
    });

    // 7. alice.process
    alice.log.info("");
    alice.log.info("🔹 7. alice.process");
    test("process.exec echo", () => {
        const r = JSON.parse(alice.process.exec("echo hello_js"));
        if (r.exit_code !== 0) throw "exit_code 不是 0";
        if (r.stdout.indexOf("hello_js") < 0) throw "stdout 不匹配";
        ok("process.exec(echo) → exit=" + r.exit_code);
    });

    // 8. alice.path
    alice.log.info("");
    alice.log.info("🔹 8. alice.path");
    test("path.join", () => {
        const p = alice.path.join("a/b", "c.txt");
        if (p.indexOf("c.txt") < 0) throw "join 失败: " + p;
        ok("path.join → " + p);
    });
    test("path.basename", () => {
        const b = alice.path.basename("a/b/c.txt");
        if (b !== "c.txt") throw "basename 失败: " + b;
        ok("path.basename → " + b);
    });
    test("path.ext", () => {
        const e = alice.path.ext("a/b/c.txt");
        if (e !== ".txt") throw "ext 失败: " + e;
        ok("path.ext → " + e);
    });

    // 9. alice.regex
    alice.log.info("");
    alice.log.info("🔹 9. alice.regex");
    test("regex.test true", () => {
        if (alice.regex.test("hello world", "wor") !== true) throw "应为 true";
        ok("regex.test → true");
    });
    test("regex.replace", () => {
        const r = alice.regex.replace("hello world", "world", "js");
        if (r !== "hello js") throw "replace 失败: " + r;
        ok("regex.replace → " + r);
    });

    // 10. alice.encoding
    alice.log.info("");
    alice.log.info("🔹 10. alice.encoding");
    test("base64 roundtrip", () => {
        const enc = alice.encoding.base64encode("Hello JS!");
        const dec = alice.encoding.base64decode(enc);
        if (dec !== "Hello JS!") throw "roundtrip 失败: " + dec;
        ok("base64 → " + enc + " → " + dec);
    });
    test("hex", () => {
        const h = alice.encoding.hex("AB");
        if (h !== "4142") throw "hex 失败: " + h;
        ok("hex('AB') → " + h);
    });

    // 11. alice.time
    alice.log.info("");
    alice.log.info("🔹 11. alice.time");
    test("time.now", () => {
        const t = alice.time.now();
        if (t < 1700000000000) throw "时间戳太小: " + t;
        ok("time.now() → " + t);
    });
    test("time.format", () => {
        const s = alice.time.format(alice.time.now(), "%Y-%m-%d %H:%M:%S");
        if (s.length < 10) throw "format 失败: " + s;
        ok("time.format() → " + s);
    });

    // 12. alice.platform
    alice.log.info("");
    alice.log.info("🔹 12. alice.platform");
    test("platform.name", () => {
        const n = alice.platform.name();
        if (n !== "Windows") throw "不是 Windows: " + n;
        ok("platform.name() → " + n);
    });
    test("platform.dataDir", () => {
        const d = alice.platform.dataDir();
        if (!d || d.length === 0) throw "dataDir 为空";
        ok("platform.dataDir() → " + d);
    });

    // 13. alice.timer
    alice.log.info("");
    alice.log.info("🔹 13. alice.timer");
    test("timer.set+remove", () => {
        const id = alice.timer.set("10s", "JS定时器测试", '{"test":true}');
        if (!id || id.length === 0) throw "timer id 为空";
        alice.timer.remove(id);
        ok("timer.set → " + id + " → remove");
    });

    // 14. alice.pipeline
    alice.log.info("");
    alice.log.info("🔹 14. alice.pipeline");
    test("pipeline.register+execute", () => {
        alice.pipeline.register("test-js", "step1", 100, (input_json, shared_json) => {
            const input = JSON.parse(input_json);
            return JSON.stringify({ result: (input.value || 0) + 10 });
        });
        const r = alice.pipeline.execute("test-js", '{"value":5}');
        const data = JSON.parse(r);
        if (data.result !== 15) throw "pipeline 结果不对: " + r;
        ok("pipeline → 5+10=" + data.result);
    });

    // 15. alice.net.addRoute
    alice.log.info("");
    alice.log.info("🔹 15. alice.net.addRoute");
    test("addRoute", () => {
        alice.net.addRoute("GET", "/api/test/js-stdlib", (req_json) => {
            return '{"message":"js route works!","passed":' + passed + '}';
        });
        ok("addRoute GET /api/test/js-stdlib");
    });

    // 16. alice.ws
    alice.log.info("");
    alice.log.info("🔹 16. alice.ws");
    test("ws.handle", () => {
        alice.ws.handle("test.js.stdlib", (req_json) => {
            return '{"echo":"js ws works!"}';
        });
        ok("ws.handle('test.js.stdlib')");
    });

    // 汇总
    alice.log.info("");
    alice.log.info("========================================");
    alice.log.info("  JS 测试结果: " + passed + " 通过, " + failed + " 失败");
    alice.log.info("========================================");
}

function onUnload() {
    alice.log.info("Hello JS 已卸载");
}
