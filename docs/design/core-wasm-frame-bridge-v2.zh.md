# Core-Wasm Frame Bridge v2: 模块内 PtrLen 构造、能力帧桥接与工作流计算节点打包 -- Design

> Status: **DESIGN GATE**(RFC 0026 P6-7 v2 amendment / KR6.7 收尾)。本文档是
> `docs/design/core-wasm-computed-output-frame.zh.md`(v1,gate commit
> `291ff564`,fix-forward revision `f9de588e`)的**正式修正案**:把 v1 第 11 节
> 明确推迟的四件事--模块内 String/Decimal/Duration 构造、raw-P4-D 计算与
> wire-JSON 能力终态的同 agent 混用、多参数能力封送、工作流投影/构造节点
> 打包--全部收敛为**决策**(无 open question),并给出可独立评审的实现阶梯。
>
> 本文档的 RFC Decision History 条目由本设计门 commit 按仓库惯例补记
> (v1 gate commit `291ff564` 同例:docs-only 设计门同样落 RFC 条目,RFC 0026
> 保持 implementing)。
> 文档应落在 `develop` HEAD `c6ee6025`(`fix(wasm): seal P6-7 frame sum
> high-water, walker bounds, two-container Node regression`)之上;引用的
> v1 实现锚点来自 `b10be97e`(rung A)、`95afafaa`(rung E)、`e5ef5f5c`
> 与 `c6ee6025`(两轮 seal 修复)。
>
> 所有 `file:line` 锚点均在 `c6ee6025` 上逐一核对。符号名与行号并置,重构
> 移行不使锚点静默失效。

## 0. 问题、事实与范围

### 0.1 v1 已交付的事实

* 每个模块恰好一页 64 KiB 线性内存(min 1 page、无 max),常量 SSOT 在
  `include/ahfl/compiler/ir/core_wasm_abi_constants.hpp:21-32`;agent 保留区
  为 input `[1024,4096)`、context `[4096,7168)`、scratch
  `[7168,12288)`(cap 5120)、computed output `[12288,16384)`(cap 4096)、
  collection backing `[16384, ...)`,同文件 `:54-125`;能力工作流的 node-event
  区在 `[1024, +8 + node_count*40)`,`:172-180`。
* `ahfl.core-layout.v1` custom section(P4-D 布局表 + 边界根 + 互不相交 backing
  placement + payload arena span)与带边界根的 `ahfl.wire-schema.v1` 已在
  rung A/E 落地;宿主侧 admit 器见
  `src/runtime/engine/core_wasm_frame_module.hpp:36-69`。
* `runv() -> (status:i32, value_ptr:i32)` 已导出,但**只有 identity 臂**:
  `make_runv_body` 仅接受 `IdentityAction`,其余终态直接跳过并落入
  `unreachable`(`core_wasm_codegen.cpp:8688-8732`);描述符里
  `final_kind` 恒为 `"identity"`、`output_base` 恒等于 input base
  (`core_wasm_codegen.cpp:10036-10043`)。**v1 阶梯的 rung B(computed-final
  emission,v1 第 10 节第 2 档)尚未落地。**
* Node 嵌入式宿主(JS,非 wasmtime 证据)已实现 pack/runv/encode:
  `tests/conformance/node_embedded_host.mjs:256-689`。这是目前**唯一**的
  runv 宿主:`src/` 下没有任何 C++ 代码调用 runv 或做 Value<->P4-D 行走
  (`core_wasm_frame_module.cpp` 只做 section admit)。
* 人口普查当前实际为 **17 agreed / 5 skipped**(任务书所述 16/5 是
  `p6_frame_two_containers` 回归用例落地前的数字):
  `tests/integration/conformance_wasm_node_runner.cpp:74-75`;node-only 茎
  集合为 `{fb3_higher_order}` 一个,`:85-87`。

### 0.2 剩下的 5 个差分 skip 为什么在 v1 下无法执行

5 个 skip 全部是 `eligible: "computation"` + `node_observation_skip:
"blocked_kr66"`(manifest 与编译器双向钉死,
`tests/conformance/wasm_eligibility.cpp:210-281`):

| 用例 / 场景 | 源文件 | v1 下的具体拦截点 |
| --- | --- | --- |
| `if_let_e2e/some`、`/none` | `tests/golden/runtime/if_let_e2e.ahfl`、`tests/conformance/cases/if_let_e2e.case.json` | 终态 `Done` 是含 if-let/match 的**计算终态**,构造带 String 字面量(`"some"`/`"none"`)的 `InspectResponse`;v1 终态只接受 canonical identity(`validate_identity_final`,`core_wasm_codegen.cpp:737-777`,在 `:7531-7545` 对一切无能力终态强制套用);P6 字面量子集只收 Bool/Integer(`:3100-3102`);构造槽拒绝 PtrLen 操作数(`:3300-3305`);workflow 侧返回区只接受无投影的精确 nominal(`:8307-8314`),节点输入区只接受精确未投影帧(`:8001-8011`);打包 agent `allow_computed_goto=false`(`:8196`)。 |
| `enum_variant_e2e/struct_payload_default` | `tests/golden/runtime/enum_variant_e2e.ahfl`、`tests/conformance/cases/enum_variant_e2e.case.json` | 终态含两个 match(其中一个绑定 struct-payload 的 String owner),响应构造含 String 字面量;同样命中计算终态缺失 + PtrLen 构造缺失 + workflow 投影返回缺失。 |
| `e2e_multi_agent/priority_low`、`/priority_high` | `tests/golden/runtime/e2e_multi_agent.ahfl`、`tests/conformance/cases/e2e_multi_agent.case.json` | 除上述全部外,还需要:(a) 非终态 handler 里的四处能力调用(`:87` `ClassifyMessage(input.message)`、`:133` `HandleGeneral(input.user_id, input.message)`、`:139` `HandleTechnical(...)`、`:173` `GenerateSummary(input.category, input.response, input.resolved)`,四处均为 `let result = ...` 有序语句)--今天 handler 模式直接拒绝能力调用(`plan_capability_call` 的 `fn_mode_` 门,`:2786-2790`),含能力的 region 不属于 P6 子集(`:929`);(b) 多参数封送--E2/FB-4 全链路硬钉单参数(`:810`、`:838`、`:2798-2803`、`:4868-4870`),固定 functype `(i32,i32)->(i32,i32,i32)`(`:286`、`:8753`);(c) 构造的节点输入 `SummaryInput{category: classify.category, response: support.response, resolved: support.resolved}` 被 workflow 帧校验拒绝(`:8001-8011`);(d) 打包 agent 禁计算 handler(`:8196`),flow pattern arena 的放行条件不覆盖计算终态(`:7449-7466`)。 |

### 0.3 本修正案建立在其上的既有事实

* **物理布局 SSOT 与逻辑线 SSOT 已分离且均已验证**:P4-D 在
  `CoreLayoutTable`(`include/ahfl/compiler/ir/core_layout.hpp`),逻辑线在
  `CoreWireSchemaTable`(`include/ahfl/compiler/ir/core_wire_schema.hpp:124-169`);
  宿主只能通过不可伪造的 `VerifiedWireSchemaBinding` 消费
  (`include/ahfl/compiler/ir/core_wire_migration.hpp:114-148`)。
* **能力签名本来就是多参数的**:`CoreCapabilityDecl.param_types` 是 vector
  (`include/ahfl/compiler/ir/core_ir.hpp:323-340`),Core verifier 的 arity
  检查已泛化(`src/compiler/ir/core_verify.cpp:2303-2324`),线 schema 投影器
  已按参数逐个投影根(`src/compiler/ir/core_wire_schema.cpp:68-94`,
  `CoreWireCapabilitySchema.params` 为 vector,
  `core_wire_schema.hpp:137-144`),绑定选择器早有 `param_index`
  (`core_wire_migration.hpp:46-51`)。**多参数缺的只是 wasm 边界约定,不是
  IR、不是 schema、不是 evaluator。**
* **evaluator 与宿主传输侧早已按 `vector<Value>` 调用能力**,且已有多参数
  wire 帧 SSOT:`serialize_args_for_wire_json` 规定 0 参 `{}`、**单 Struct 参数
  裸对象、单非 Struct 标量参数 `{"value":...}`**、其余(含多参数)
  `{"args":[...]}`(`src/runtime/engine/wire_value.cpp:14-34`);evaluator 侧构造
  参数向量在 `src/runtime/engine/capability_eval.cpp:140-154`,native 宿主绑定
  同样一次传一个 args 帧
  (`src/runtime/engine/native_host_binding.cpp:37-38`)。
* **String 的物理形态是 PtrLen(8 字节、align 4),只有 String 用
  PtrLen**:`vt_String -> CoreLayout{8,4,CoreLayoutPtrLen{}}`
  (`src/compiler/ir/core_layout.cpp:216-218`);Decimal/Duration/Timestamp 是
  i64 标量字(`:219-227`),Uuid 是 Bytes16(`:228-230`)。不存在第二种
  PtrLen 家族。
* **Decimal/Duration 的边界渲染规则 v1 已定死**:物理字只有 mantissa/ms,
  spell 由宿主在 encode 时用 `scalar_spelling::spell_builtin_decimal`
  (`s<scale>:<mantissa>`,`src/runtime/value/scalar_spelling.cpp:86-92`)
  与裸毫秒 `std::to_string(i64)`(`scalar_spelling.hpp:77-90`,
  `builtins.cpp:946-958` duration_between 直接 `to_string`)生成;
  `value_to_json` 对 Decimal/Duration 逐字输出 `inner.spelling`
  (`src/runtime/value/value_json.cpp:76-79`)。evaluator Value 不存
  mantissa,只存拼写串(`src/runtime/value/value.hpp:47-53`)。
* **模块今天没有 Data section**:段 id 常量表缺 11 号
  (`core_wasm_codegen.cpp:127-143`),1024 以下地址全部未使用;custom sections
  位于 Code 之后、EOF 之前(`:8974-9007`)。
* **工作流是单实例、单页、模块内 Kahn 调度器**:一次 `run2(ptr,len)` 跑完全部
  节点,节点间不重建实例、不复位内存;runner 是 goto 线性 walk,终态要么
  identity 原样返回 `(ptr,len)`,要么直接 call ahfl_cap 并原样转发三元组
  (`make_workflow_runner_body`,`:9186-9258`;调度
  `append_workflow_schedule`,`:9339-9466`;run2
  `:9469-9502`)。节点输出指针保存在每节点两个 local 里
  (`workflow_node_ptr_local`,`:9260-9273`)。
* **IR 早就把构造式节点输入/返回降级好了**:`input_region` 由
  `lower_value_region` 生成,可含 `CoreConstructExpr`
  (`src/compiler/ir/core_lower.cpp:5098-5099`),只是被后端 validator 拒绝;
  工作流返回区同理。v2 不需要改 Core IR。
* **String 长度上界只可能来自有界 String 类型**;裸 `String` 没有
  length_bounds(`src/compiler/semantics/type_resolver.cpp:56-66`,
  `CoreVtString.length_bounds` 为 optional)。5 个目标用例的 String 全部
  无界。
* **durable resume 仍是独立封堵面**:FB-4 已把模块内能力调用钉为
  single-run(非 OK 即 trap,`core_wasm_codegen.cpp:4905-4913`);D2b
  durable-effect authority 未完成,v1 §11 与本修正案均不改动该结论。

### 0.4 非目标(本节决策不覆盖)

* 不做字符串拼接/插值/数字->字符串的**运行时**格式化;v2 的模块内 String
  生产源只有字面量(与指向既有稳定区间的 PtrLen 复制)。
* 不做 wasmtime 实机证据;Node 嵌入式宿主证据与 v1 同级。
* 不做 C++ 生产宿主的 pack/encode/bridge 适配器(v1 的 runv 行走同样只有 JS
  宿主);它是独立阶梯,见第 9 节。
* 不开启 durable replay / memo 对桥接结果的记录;PENDING 在桥接路径上无臂。
* 不放开 closure/fn 作为帧边界值(KR6.8 范畴)、不补 f64 运算 opcode 梯、
  不放开 map/float/decimal/duration/timestamp/uuid 的帧行走子集
  (`node_embedded_host.mjs:532-538` 今天显式拒绝)。
* 不改变任何 E1/E2/E3/FB 模块在不使用新能力时的字节(逐字节义务见第 8 节)。

## 1. 决策总表

| # | 决策 |
| --- | --- |
| **D1** | **模块内 String 构造走"Data section rodata + 立即数 PtrLen",不引入任何宿主构造导入,也不引入运行时 bump writer。** 新增一个保留区 `[256,1024)`(768 字节,8 对齐)承载去重后的 UTF-8 字面量字节,由一个 Data section(section id 11)的 rodata 主动段在实例化时初始化(另有字面量默认值的 context String 字段,每字段追加一个 8 字节 PtrLen 初始化段);一个被构造的 String SSA 值就是两个立即数 `(rodata_base+offset, len)` 的 PtrLen。PtrLen 家族 v2 只发 String。Decimal/Duration 不是 PtrLen,是 i64 字:字面量在编译期经编译器常量 SSOT 解析为 mantissa/ms,宿主 encode 仍按 v1 §3.3 渲染 builtin/裸毫秒拼写。 |
| **D2** | **补完 v1 推迟的 rung B:计算终态(ComputedReturn)。** 终态 region 可以是含 match/if-let、struct/enum 构造、String PtrLen、ctx store 的 P6 子集,以 `CoreReturnStmt` 结尾;planner 物化结果进 output frame,runv 增加 computed 臂返回 `kP6AggregateOutputBase`,描述符 `final_kind` 变为真实判别值。 |
| **D3** | **帧桥接完全在宿主侧做表示转换,模块侧只搬运 P4-D 描述字。** 新增一个追加式 import functype `kTypeCapabilityBridge = (i32) -> (i32,i32)`(bridge control-block 指针入,`(status, result_root_ptr)` 出);宿主用既有 pack/encode 行走对 + `value_to_json`/`decode_json` SSOT + `serialize_args_for_wire_json` 完成 P4-D<->wire JSON 双向转换,经既有能力注册表发出真实调用。能力调用仍是**有序语句**(FB-4 `CoreCapabilityCallStmt`),single-run:非 OK 必 trap,PENDING 无臂。一个能力在一个模块内只有一种模式(opaque-tuple 或 bridge),混用拒编。 |
| **D4** | **多参数封送复用既有参数根表。** bridge control block 携带 `arg_count` 与 N 个 `(ptr,len)` P4-D 跨度,顺序严格等于 `CoreCapabilityDecl.param_types`;schema 侧 `params[]` 根早已存在,宿主逐参做 layout+wire join;wire 帧形遵守 `serialize_args_for_wire_json` 的 0 参 `{}` / 单 Struct 裸对象 / 单非 Struct 标量 `{"value":...}` / 多参 `{"args":[...]}` 既约。 |
| **D5** | **工作流计算节点打包(v1 rung F 细化)在模块内完成 P4-D 投影/构造,不引入第三种帧表示。** 打包 agent 以 `allow_computed_goto=true` 通过与独立 agent 相同的逐 handler 校验;新增 computed runner 形态;每打包实例分配固定 I/C/O node-frame 块;入口帧由宿主 run2 前打包;节点输入与工作流返回的投影/构造由调度器发 P4-D 字拷贝序列(PtrLen 负载指针共享、零拷贝);计算终态的 pattern arena 随 D2 一并放行。 |
| **D6** | **一页容量裁决全部在编译期。** rodata、bridge control-block 页帧(含标量 spill 槽)、node-frame 块、per-call-site 桥接结果放置、entry/result payload arena、既有 frames/backing/scratch/heap 高水位一起进入同一个对 `kCoreWasmFixedLinearMemoryCapacityBytes=65536` 的比较族;放不下即 `wasm.RESOURCE_EXHAUSTED` 拒编,消息可操作;运行时只有既有 checked-alloc 返回 0 -> trap 一种动态失败。无界 String 由一个具名编译期池常量兜底,不是运行时增长。 |
| **D7** | **普查分两档移动 17/5 -> 18/5(V2-C 新增直接 agent 多参桥接 conformance 用例)-> 23/0(V2-E 五个 `blocked_kr66` 转 agreed)**。硬钉按**场景**计数(`conformance_wasm_node_runner.cpp:74-75` 每个 scenario 计 1;当前清单 20 个 manifest 共 22 场景,其中 17 agreed 已含 1 个 node-only stem `fb3_higher_order/double`--它按 manifest blessed 期望比对、仍计入 g_compared,`:487-488`;另 5 场景 skip),所以新增 agreed 用例必须同 commit 移动钉,不能落在两次普查数字之间。5 个 `blocked_kr66` 声明在 V2-E 同档删除,逐场景与 rung 的映射见第 8 节。每一档都保持未用新能力模块的逐字节不变。 |
| **D8** | **不扩大 KR6.8 的生产宿主要求,也不提前退役 evaluator。** 桥接是描述符显式选择的 opt-in 契约,旧宿主拒绝带 bridge/profile-v2 标记的模块即可;evaluator 差分在 v2 期间继续是验收参照;KR6.8 的原子删除门(RFC 0026 P8)仍以全差分绿为前置,本修正案不移动它。 |

## 2. D1 -- 模块内 String/PtrLen 构造

### 2.1 两个候选与决策

* **(A) 宿主辅助构造导入**:新增例如 `ahfl_string_new(ptr,len)->(ptr,len)` 之类的 import,模块把字节"申报"给宿主,宿主分配并回填。
* **(B) 模块内 bump 字节构造 + 边界 `(ptr,len)`,canonical 编码仍在 runv 后的宿主侧(v1 D1 不变)。**

**决策:(B),且 v2 的具体实现是"主动 Data section 初始化的只读 rodata 区 + 立即数 PtrLen",不是运行时写字节。**

理由:

1. **不新增 ahfl_cap 以外的导入。** 任务书的硬约束与最小权限原则都排除
   (A):每多一个导入就多一个宿主必须实现、必须鉴权、必须做确定性论证的
   回调面;v1 D1 已明确"artifact never gains a host callback for
   serialization"(`core-wasm-computed-output-frame.zh.md` §8)。
2. **v2 的字符串生产源在编译期全部已知。** 5 个目标用例里的被构造 String
   无一例外是字面量(`"some"`/`"none"`/`"system"`/`"closed"`/`"high"`),
   其余 String 全部输入可达(经入帧或经 D3 的桥接结果)。AHFL 没有字符串
   拼接表达式;为不存在的生产源引入运行时 bump writer 或宿主调用,违反
   "不为边界需要给每个 artifact 塞能力"的 v1 论证(§2.1 理由 6)。
3. **Data section 是行业标准 rodata 机制**(Clang/GHC 产物同构),比在 code
   段里发一串 `i32.const`+`i32.store` 更优:字节只存一份(可去重)、code 段
   不膨胀、实例化由引擎一次性完成、地址与长度仍是编译期常量。它只新增一个
   标准段(id 11,位于 Code(10) 与 EOF custom sections 之间,段序合法),不
   新增任何运行时机制。
4. **确定性与 resume 安全。** Data 段在每次 fresh instantiation 时由引擎按
   模块字节确定性重放;rodata 只读,模块没有任何 store 指令指向它(构造侧只
   发 PtrLen 立即数)。durable resume 每次重建实例即恢复同一字节,无单运行
   状态。
5. **canonical SSOT 不分裂。** 模块仍不做 JSON escaping、不做 float 格式化;
   宿主 encode 时 String 仍走 `value_to_json -> write_escaped_json_string`
   (`value_json.cpp:74-75`,escaper 在 `src/base/support/json.hpp:10-45`)。
   模块只负责给出 UTF-8 字节跨度。

### 2.2 新区与段格式

在 `core_wasm_abi_constants.hpp` 新增:

```
kP6RodataBase     = 256   // 8 对齐;[0,256) 保留为零页/null-deref 捕获带
kP6RodataCapacity = 768   // [256,1024);1024 起既有 input/node-event 布局不变
```

* 一个模块(agent 或 workflow)只要存在至少一个可达的 String/ctx-String-
  default 字面量,就发恰好一个 Data section:其首个主动段是 **rodata 段**
  (`flags=0`,memory 0,offset 表达式 `i32.const kP6RodataBase; end`),负载
  是字面量池:planner 对所有可达字面量 UTF-8 字节做**去重收纳**(按字节
  hash-cons,Principle 3)并按 UTF-8 字节全序排序拼接(确定性来自字节内容
  本身,不依赖源码出现序或字符串 interner 的分配序),每条 8 对齐 padding
  填零;负载长度即 rodata extent。
* 无字面量的模块(今天的全部模块)不发 Data section,字节不变。
* Data section 的存在性、段序(在 Code 后、custom 前)加入二进制探针;
  F3 只校验 Memory 声明(`core_wasm_resume_host.cpp:91-102`),Data 段不改变
  内存声明,admit 器按 size-skip 跳段的既有读法不受影响。
* 此外,对每个**有字面量默认值的 context String 字段**,Data section 追加
  一个 8 字节主动段,落在该 runner context frame 的字段偏移上,内容为
  `(rodata_ptr:i32-LE, len:i32-LE)`。v2 不改变标量 context 默认值的零初始化
  现状(零初始化恰好是 Bool/Int/enum-tag 默认的正确值);String 默认值是
  唯一需要非零初始化的家族。目标用例中这些字段均先写后读,该初始化是
  完备性而非用例所需。

### 2.3 构造规则与编码规则

* String 字面量在 P6 模型里获得一类新的可读值:**PtrLen 值**(两个 i32 字,
  不是一个 word)。`plan_literal`(`core_wasm_codegen.cpp:3076-3104`)新增
  `CoreLiteralKind::String` 臂(枚举已存在,
  `include/ahfl/compiler/ir/core_ir.hpp:397`):planning 登记字面量到池并返回
  PtrLen kind;emit 时在需要 PtrLen 的槽位上压两个立即数。
* PtrLen 按 v1 D6 的边界/槽位规则流动:struct 字段槽是内联 8 字节;跨 fn
  按地址传(v1 D6 已落地 by-address PtrLen);ctx store 新增 PtrLen 8 字节
  store 臂(今天 `plan_store` 因 `readable_kind==nullopt` 拒掉 String,
  `core_wasm_codegen.cpp:5229-5268`;v2 对**槽位是 PtrLen、值是 PtrLen** 的
  ctx 存储放行--负载指针永远指向 rodata/入帧 arena/桥接结果放置,三者均
  全运行稳定,不存在 v1 拒绝 aggregate ctx store 的 scratch 生命周期问题)。
  仍拒绝 aggregate 槽的 ctx 持久化(无新增生命周期规则)。
* 长度是 **UTF-8 字节长度**(不是码点数);源字符串在前端已是合法 UTF-8,
  池字节逐字取源拼写,模块不做转义/规范化。wire 的 JSON escaping 只发生在
  宿主 encode。
* 每条字面量编译期对照该 PtrLen 槽位线节点的 `length_bounds`
  (`CoreWireSchemaString.length_bounds`,`core_wire_schema.hpp:43-47`):
  存在上界且字面量超长 -> 编译期 `wasm.RESOURCE_EXHAUSTED`(或类型不符的
  `wasm.UNSUPPORTED_CAPABILITY_FRAME`,沿用既有家族),不产生部分产物。
* 宿主 encode 侧的区域成员校验从"只接受 input payload arena"
  (`node_embedded_host.mjs:550-556`)扩展为接受三个授权跨度的并集:
  入帧 payload arena、rodata 区、D3 的 per-call-site 结果 payload 区。
  其余地址、负 len、越界、schema bounds 不符一律 fail-closed,不回显字节。
* Decimal/Duration:**不经过 rodata**。它们是 i64 物理字
  (`core_layout.cpp:219-224`)。v2 让 `CoreLiteralKind::Decimal/Duration`
  在 P6 子集中可构造:planning 用编译器常量 SSOT
  (`src/compiler/semantics/const_sema.cpp` 的 `parse_decimal_const`/
  `parse_duration_milliseconds`,`:55-123`)把源拼写解析为 mantissa/ms,emit
  为 i64 立即数;宿主 encode 仍按 v1 §3.3 用 schema scale/裸毫秒渲染
  builtin 形式。差分语义后果与 v1 入帧规则对称并予以明示:evaluator 对
  **源字面量** Decimal/Duration 保留原拼写(`value.hpp:47-53`,
  `value_json.cpp:76-79`),而 P4-D 通道渲染 builtin/裸毫秒形,因此一个返回
  源字面量 Decimal/Duration 的差分用例不可能字节一致;v2 不为此放宽任何一
  侧,5 个目标用例不含此类输出,该类差分 fixture 继续是非目标(等 KR6.8
  评估器侧拼写策略统一)。算术/builtin 产生的 Decimal/Duration 本就是
  builtin 拼写,无此问题。
* Float 字面量与 f64 帧槽的 ABI 在 v1 已具备,但 f64 opcode 梯不发;v2 不
  改这一边界(5 用例不需要)。

### 2.4 trap / overflow 行为

* rodata 容纳不下(去重后 extent > 768)-> 编译期
  `wasm.RESOURCE_EXHAUSTED`,消息同时给出 extent、容量与"缩短字面量/使用有界
  类型"指引;无运行时溢出路径(地址与长度全为立即数)。
* 宿主侧读到指向 rodata 外的 PtrLen -> runv 观察 fail-closed(与 v1 §3.4
  同家族),不产生观察字节;真正的引擎 trap 分类不变。
* Data 段本身越页(extent+base>65536)在编译期与 §7 D6 同一比较族拦截。

### 2.5 resume / store 含义

rodata 是实例化字节,不是单运行可变状态:runv/桥接计算帧仍按 v1 定性为
single-run;durable memo 不记录它们,D2b 落地时 computed agent 的回放走其
独立门,与本修正案无耦合。

## 3. D2 -- 补完 computed final(v1 rung B)

v1 §4.4 与第 10 节第 2 档已设计、未实现。本修正案按 v1 原文决策落地,仅补
两处 v2 必需的细化:

1. **终态 region 准入**。`build_agent_plan` 对无能力终态不再只走
   `validate_identity_final`(`core_wasm_codegen.cpp:7531-7545`):若 region
   不是 identity 规范形但满足 `is_p6_subset_region`(`:910+`,v2 扩展为含
   D3 bridge 语句与 PtrLen/String 构造,见 §4)且以 `CoreReturnStmt` 结尾,
   则进入新的 `ComputedReturn` planner action;materialization 规则按 v1
   §4.4:标量字按 repr store、aggregate/enum 按布局常尺寸做有界拷贝循环、
   collection 拷 8 字节 inline header、**String 拷 8 字节 PtrLen(负载留指
   rodata/arena,D1)**、padding 存零。
2. **runv computed 臂与描述符**。`make_runv_body` 增加
   `ComputedReturnAction` 臂返回 `(AHFL_CAP_OK,
   kP6AggregateOutputBase)`(`:8688-8732` 今天显式跳过非 identity);
   `build_agent_descriptor` 的 `final_kind` 按 action 如实输出
   `"computed"`,`output_base` 输出 12288(`:10036-10043`)。output frame 的
   `fits_frame_region(..., kP6AggregateOutputCapacity, "output", ...)`
   门(v1 §6.1;`fits_frame_region` 在 `:1238-1255`,input 门在 `:7492-7495`)
   随本档接上。
3. **pattern arena 终态放行**。`:7449-7466` 的 any_of 只在"某 state 是含
   match 的计算 region"时放行非空 pattern arena;v2 把谓词扩展到**计算终态
   region**(if-let 在 Core 里降为 match,其 arena 挂在 flow.storage 上)。
   逐 arm 的 fail-closed 校验不变(`plan_match` 定义于 `:4124`,其 `plan_match_region` 在 `:4201`)。
4. capability 终态与 raw-P4-D 终态在同一 agent 的混用仍拒(v1 §4.4),但
   理由从"永久"收窄为"能力终态走 opaque/run2;能力出现在**非终态有序语
   句**里由 D3 桥接"。
5. **fix-forward 边界(V2-A 落地后的三处收口)**。
   - 宿主按 P4-D **inline 图**打包 INPUT frame(每个字段就位于偏移处,没有
     子地址槽);模块运行时则把 aggregate 字段存为子对象的 i32 地址,物化器
     展开的是这条模块指针树。因此 computed final 从 `input` 投影出的非标量
     边 —— 跨过任一中间 aggregate 边的投影,或落在 struct/payload-bearing
     enum 叶上的读取 —— 在 **inline-input-frame 扩展**(frame-base +
     累加 P4-D 偏移、无 i32.load 解引用)落地之前一律 fail-closed:出生点在
     `plan_path`,constructor aggregate operand 与物化根两处再做一次同不变
     量的边界复查。顶层 scalar / tag-only enum / String PtrLen / collection
     inline header 字段无子边解引用,继续放行;canonical identity 透传
     不变。非终态 goto lane 既有的 inline/指针投影错配仍是独立遗留缺口,
     不在本收口内。
   - enum 物化只拷贝 **ACTIVE variant** 的 payload:运行时读源 discriminant,
     以 i32.eq if 梯选中对应 variant 的 payload struct(单元 variant 不写
     payload 词),越界 tag trap。不再向同一个 payload union 直线覆盖拷贝
     全部 variant —— 当不同 variant 在同一 union 偏移放置不同 field kind
     (子 aggregate 地址槽 vs inline 标量词)时,非活跃 variant 的拷贝会覆盖
     活跃 variant 已展开的词,并把模块 scratch 地址泄漏进宿主可见 frame。
   - match arm / fallback 的"终态返回"判定改用与 `plan()` 相同的结构化谓词
     `p6_region_always_returns`(trailing return,或 trailing if/match 每个
     分支都返回,递归判定);此前只认最后一条语句本身是 `CoreReturnStmt`,
     会把 if-let arm 内 trailing if 双分支返回的合法终态误判为内部错误。

## 4. D3 -- raw-P4-D 计算 <-> wire-JSON 能力终值的帧桥接

### 4.1 边界放在哪里

转换边界**只在宿主侧的 import 回调处**,与 v1 D1 的信任模型一致:模块不
解析、不生成 JSON,不持有 schema 名称表;它只在内存里搭一个 P4-D 控制块并
调用一个新 functype 的导入。宿主在回调中:

1. 用已 admit 的 `VerifiedWireSchemaBinding`(每个能力参数/结果一个,经
   `CoreWireRootSelector{capability,source_symbol,Param,i}` 铸造,
   `core_wire_migration.hpp:46-51`;选择器在 `core_wire_migration.cpp:119-154` 按 i 解析 Param 根,工厂已支持多参数)与
   core-layout 表,逐参数**行走 P4-D 跨度 -> `evaluator::Value`**--这是
   v1 encode 行走的同一 per-shape 分派;
2. 按 wire SSOT 序列化参数帧(`serialize_args_for_wire_json`,
   `wire_value.cpp:14-34`),经既有能力注册表/invoker 发出真实调用
   (`capability_bridge.hpp:163-164`,`CapabilityRegistry::invoke(name, args)`);
3. 对结果 `validate_value`(结果绑定,fail-closed)后,用 v1 的 pack 行走
   把结果 Value **打包进该 call site 的固定 P4-D 结果放置区**,回填
   `(status, result_root_ptr)`;
4. 模块拿到 root 地址后继续 P6 计算(字段投影、ctx store、进入下一状态)。

模块侧与宿主侧的代码分工因此是:**模块发 P4-D 字搬运(store/call/局部绑定),
宿主复用既有 codec/evaluator SSOT,任何一侧都不写第二个 JSON 序列化器。**

### 4.2 导入 ABI 形状(决策)

新增固定 functype,**追加**在两类模块既有固定类型表的末尾(agent 模块:
与 runv 类型同处追加族,按"既有固定类型 -> per-fn -> closure -> 追加固定
类型"的既有规则取号;workflow 模块:现有 5 个固定类型之后取索引 5--今天
workflow 的 Type 段恰好只有这 5 个固定类型,runner 只在 Function 段重复引用
既有 type 4 而不占 Type 槽,故追加后不存在索引位移),编号具名
`kTypeCapabilityBridge`:

```
ahfl_cap.cap_<symbol>  :  (block_ptr:i32) -> (status:i32, result_root_ptr:i32)
```

* 不增设模块名、不增设导入家族:module 名仍是 `ahfl_cap`,field 仍是
  `cap_<symbol>`(`:8807`、`:9637`)。一个 field 一个 functype,因此**一个
  能力在一个模块内只有一种模式**:planner 发现同一能力既被 opaque 转发又被
  bridge 调用 -> `wasm.UNSUPPORTED_CAPABILITY_FRAME` 拒编。不同能力可混用
  两种 functype(import 表按能力逐条选类型)。
* 不沿用 `(i32,i32)->(i32,i32,i32)` 是刻意的显式优于隐式:同一 functype
  跑两种协议是错误的抽象层特判(Principle 1);结果长度可由结果布局根推出,
  不必回传 len。opaque 模块的类型表、导入表字节因此保持不变。
* import-count 规划不变:每个可达能力仍只占一个低序号
  (`build_agent_plan` 的 sorted-unique 表,`:7742-7819`;工作流
  `:8237-8284`)。bridge 模式按 ordinal 记录进描述符与 exec-manifest,宿主
  按 field + 描述符决定回调协议;旧宿主遇到未知 functype 在实例化期即失败,
  天然 fail-closed。

### 4.3 bridge control block(模块搭、宿主读)

一组新保留区(常量进 `core_wasm_abi_constants.hpp`,坐标进 core-layout
section v2 负载)。**每个 bridge 能力模块恰好一个 bridge control-block 页帧**
(agent 模块一个;workflow 模块跨全部打包实例共享一个),planner 按稠密
call-site id(ANF 序,模块内全模块唯一)把每个调用点的块放在固定偏移
`kP6BridgeControlBase + call_site_id * kP6BridgeControlBlockStride`;每个块
只服务一个已知能力,块长按该能力 `param_types.size()`(记为该调用点 arity)
裁成 `8 + 8*arity`,`kP6BridgeControlBlockStride` 取模块内最大块长(编译期
常量,保证固定步长寻址),块基址 8 对齐,不放变长尾巴(见下)。单个块布局:

```
偏移  内容
0     call_site_id : u32     planner 分配的稠密调用点 id(按 ANF 序)
4     arg_count     : u32    必须等于该能力 param_types.size()
8     args[0].ptr   : i32
12    args[0].len   : u32
...   args[i] = (ptr,len) P4-D 跨度描述
```

* 块只承载定长的 `(ptr,len)` 描述数组(每参 8 字节),**不内联标量/聚合负载**:
  所有 arg 值留在其既有稳定地址--标量 SSA 字的帧/局部 spill 槽、PtrLen 的
  8 字节槽、aggregate/enum 的 frame/scratch/ctx 地址、collection 的 8 字节
  inline header 地址--块内 `(ptr,len)` 只命名它们。因此块长度编译期常量,
  不随参数形状变化,也不与 §7 的 payload 池重叠。
* 标量参数(Bool/i32/i64、tag-only enum):模块把 SSA 字 spill 进 block 外的
  **call-site 私有标量 spill 槽区**(同一保留页帧,紧随控制块之后,每参 4/8
  字节、8 对齐),`ptr` 指该槽,`len` = 该字物理宽度;
* PtrLen(String)参数:8 字节 spill 进同一 spill 槽区,`ptr` 指该 8 字节槽,
  `len=8`(宿主读出的 PtrLen 再按其负载区间做二次成员校验);
* aggregate/enum 参数:`ptr` 是其既有地址(frame/scratch/ctx/结果放置),
  `len` = 布局根 size;
* collection:inline header 地址 + 8,元素在其 backing placement;
* control-block 页帧 extent 与标量 spill 槽区一并在 §7 D6 的单页总账中按
  `Σ_call_site (align8(8 + 8*arity) + align8(spill_bytes))` 计高水位
  (`spill_bytes` = 该调用点标量/PtrLen 参数宽度和,保守上界 `8*arity`;无
  标量参数则为 0);所有跨度做 checked u32 区间运算,宿主按"该 call site
  声明允许的区间集合"做成员测试(与 v1 §3.4 同一家族),越界/回绕/
  `arg_count` 不符/参数根 arity 不符/块地址越出保留页帧 -> 回调返回 ERROR
  且模块 trap,无部分调用。

### 4.4 结果放置与单运行语义

* planner 为每个 bridge call site 分配一个**互不相交结果放置**(sum 规则,
  复用 `assign_input_container_placements` 的算法族,
  `core_wasm_codegen.cpp:6843-6877`):`{call_site_id, result_layout_id,
  base, extent, payload_base, payload_extent}`,写进 core-layout section;
  因为调用点在无环 goto 图上(`:7671-7722`)、语句有序,结果区间全运行稳定,
  ctx 可安全持有指向它的 PtrLen(支持 `ctx.response = result.response;`
  跨 goto 存活)。
* 宿主打包结果时,其 String 负载写入该放置的 payload 区;无界 String 同样由
  §7 的池预算兜底,宿主打包前全量预检,失败不写半帧(v1 §5.1 纪律)。
* 状态语义复用 `AHFL_CAP_*` 命名空间:**只有 OK 有结果**;ERROR/PENDING/
  未知状态模块一律 `unreachable`(直接对标 FB-4 的 single-run 处置,
  `:4905-4913`)。v2 不设 PENDING 臂、不设 latch、不写 memo;durable replay
  对桥接调用的确定性回放仍由 D2b durable-effect authority 在其独立门内解
  决,本修正案不解除该封堵。
* 能力调用的**有序性**不变:它仍是 ANF 里的一条有序 `CoreCapabilityCallStmt`
  (FB-4 已确立其语句位,`plan_statement` 在 `:4484-4486` 分派),能力序列
  观察按执行语句序产生,与 evaluator 的 `capability_sequence` 同序。

### 4.5 子集成门(handler 模式开放)

* `plan_capability_call` 今天要求 `fn_mode_`(`:2786-2790`)且单参数
  (`:2798-2803`)。v2:在**计算 region**(非终态、含 goto/trap 终结;终态
  能力调用仍归 E2 opaque 形)中开放 handler 模式的**bridge 臂**:arity 按
  `param_types` 泛化、参数按 §4.3 规则可 spill、结果 SSA 绑定为结果根的 P6
  kind(aggregate Ptr / collection handle / tag enum / 标量;String 结果字段
  以投影消费,不绑定整 PtrLen 之外的新 word 规则)。
* `is_p6_subset_region` 增加"有序 bridge 语句"成员(它不再是纯 region,
  而是 v2 的**桥接计算 region**;`CoreCapabilityCallStmt` 在
  `:929` 的 false 分派替换为"opaque 调用仍 false、bridge 调用 true"的策略
  判断,策略由 AgentPlanPolicy 携带,独立 agent 与打包 agent 同策略)。
* 结果/参数形状必须落在帧行走子集(struct/enum/option/scalar/string/有界
  sequence);map/f64/decimal/duration/timestamp/uuid 形状的桥接参数或结果
  -> `wasm.UNSUPPORTED_CAPABILITY_FRAME` 拒编(宿主行走器今天本就拒绝,
  `node_embedded_host.mjs:532-538`),不产生半产物。

## 5. D4 -- 多参数能力封送

* **布局表已存在**:`CoreWireCapabilitySchema.params` 是按声明序的根 vector
  (`core_wire_schema.hpp:137-144`),投影器逐参数投影
  (`core_wire_schema.cpp:68-94`),选择器按 `param_index` 取根
  (`core_wire_migration.cpp:119-154`)。v2 不新增 schema 节点、不改线格式。
* **顺序**:`control_block.args[i]` 严格对应
  `CoreCapabilityDecl.param_types[i]`(`core_ir.hpp:323-340`),与 Core
  verifier 的 arity/逐参类型检查同序(`core_verify.cpp:2303-2324`)。宿主
  按 i 铸造第 i 个参数绑定;`arg_count` 不等、任一参数 layout/wire 不一致
  均 fail-closed。
* **wire 帧形**:宿主侧序列化直接用 `serialize_args_for_wire_json`
  (`wire_value.cpp:14-34`):单 Struct 参数裸对象(本批用例的
  `ClassifyMessage(message)`),单非 Struct 标量参数 `{"value":...}`(v2
  目标用例不产生该形,桥接行走器仍须按 SSOT 接受),多参数
  `{"args":[...]}`(`HandleGeneral(user_id, message)`、
  `GenerateSummary(category, response, resolved)`),与 native/HTTP 传输完全
  同形,注册表收 `vector<Value>`(`capability_eval.cpp:140-154`)。**模块
  从不构造这个信封。**
* **界**:每个 arg 跨度独立做区域成员检查;标量/PtrLen spill 槽在 §4.3 的
  control-block 页帧内 spill 区、aggregate 在
  授权帧/scratch/结果区、String 负载在三大授权负载区间;长度取自布局,不
  信任模块给的 len(len 只用于交叉校验)。
* canonical size:现有 `max_canonical_json_size` 是单根界
  (`src/runtime/engine/core_wire_canonical_size.hpp:48-49`),v2 需要的物理界不经过它(P4-D
  尺寸走布局表 + payload 池);wire 侧总尺寸只用于宿主侧预检/传输,桥接回
  调在页内直接行走,不经过线帧缓冲,故不新增 per-call 线尺寸 helper。

## 6. D5 -- 工作流计算节点打包(v1 rung F 细化)

### 6.1 记忆模型:单页、单实例、每打包实例固定 node-frame 块

v1 §5.3 曾设想"宿主在节点间行走上游 P4-D 页并重新 pack";现状是调度器内置于
模块(`append_workflow_schedule`,`core_wasm_codegen.cpp:9339-9466`),宿主
无法在一次 run2 中间介入。v2 据实修正 seam:**工作流 lane 的投影/构造在模块
内以 P4-D 字拷贝完成;宿主只在 run2 前打一次入口帧、run2 后编码一次输出帧。**
这不引入第三种表示--全部是 P4-D 字。

能力工作流内存现状:event 区 `[1024,+8+40N)`,heap 从
`align_up(event_end,8)` 起(`compute_event_layout`,`:9029-9060`)。v2 在
heap 之前规划新的 **node-frame block**(agent lane 的 1024/4096/... 保留区
在工作流模块里不存在,坐标不冲突):

```
[event 区结束, W0a)             既有对齐
[W0a, W0b) bridge control-block 页帧 + 标量/PtrLen spill 槽区(§4.3;
          仅含 bridge 调用的工作流;无则零字节,坐标进 layout v2 负载)
[W0b, W1)  node-frame block:
          对每个【打包实例】(sorted-unique,plan.packaged_instances)一份:
            I_k : input    大小 = 该实例 input 根布局 size(fits 门)
            C_k : context  大小 = context 根布局 size
            O_k : output   大小 = output 根布局 size
          各自 8 对齐;I/C/O 均为编译期常量
[W1, W2)  入口 payload arena(入口 String 负载;D6 池规则)
[W2, W3)  per-call-site bridge 结果放置(§4.4,sum 规则)及其 payload
[W3, ...) 既有 bump heap(checked alloc)
...       <= 65536
```

rodata 仍在 `[256,1024)`,与 event 区不相交;Data section 对 agent 与
workflow 模块同形。ctx-String-default 的 8 字节初始化段按各 runner C_k 偏
移发。

**生命周期**:顺序 Kahn 调度,块按打包实例分配。v2 的保守决策:一个 p6 工作
流中两个不同节点复用同一打包实例 -> `wasm.RESOURCE_EXHAUSTED` 拒编(目标用
例全部 1:1;放开需要可证明无观察重叠的复用分析,留待后续)。opaque 节点不
占 node-frame 块,沿用 heap 上的 `(ptr,len)`。

### 6.2 打包 agent 计划与 computed runner

* `build_workflow_plan` 调 `build_agent_plan` 的策略从
  `allow_computed_goto=false`(`:8196`)翻为 **true**,但 unsupported_code
  仍用 `kUnsupportedWorkflowFrame`、slice 名仍为 "E3":即打包 agent 必须通过
  独立 agent 的**同一套**逐 handler region/SSA/安全门(含 D2 计算终态、D3
  bridge 语句、pattern arena),任何独立 agent 拒绝的形状在工作流里同样拒
  绝,没有第二套弱化校验。
* runner 二态(按 agent plan 选择,`make_workflow_runner_body`,
  `:9186-9258`):
  * **linear runner**(既有,字节不变):goto 链 + identity/opaque 能力终态;
  * **computed runner(v2)**:函数体把"私有 current state"置初值,循环调用
    该 agent plan 已编译的 handler 函数(P6-2 起 handler 即独立
    `()->i32`,workflow 模块把它们作为追加定义函数发出),直到
    is_final;终态为 ComputedReturn 时把结果物化进该 runner 的 O_k(handler
    的帧基址从 agent lane 常量重写为该实例的 I_k/C_k/O_k--
    P6ComputationHandlerBuilder 增加一个 frame-base 重定位表,所有既有固定基
    常量经它发射),返回 `(OK, O_k_ptr, output_size)`;bridge 调用在 handler
    函数内直接 call 对应导入(工作流 import 表含 bridge functype 条目),
    回调在节点执行期间嵌套发生,node-event 记录仍只由调度器在节点完成时写
    一条(runner 永不写事件,既有 P0-1 不变)。
* runner 签名保持 `(i32,i32)->(i32,i32,i32)`(工作流函数表形态不变,
  `:9655-9659`):p6 runner 的入参忽略 ptr/len(入口已在 I 区),末态返回
  O_k 与布局 size;opaque runner 行为逐字节不变。

### 6.3 入口打包、节点输入构造、返回构造

* **入口**:`e2e_multi_agent`/`if_let_e2e` 这类含任一 p6 节点的工作流模块
  获得 `ahfl.core-layout.v1`(负载 format_version=2,见 §7.3)与带边界根的
  wire schema(v1 只对 agent 发;workflow 投影今天只在有 imports 时发,
  `:10185-10212`;v2 在有 frame 块时同样投影边界根)。宿主 run2 前把入口
  JSON 用 v1 packer 打进**入口节点对应实例的 I_k + 入口 payload arena**
  (Node 宿主本就可在 run2 前任意写页,`node_embedded_host.mjs:207-211`);
  run2 仍按现状被调用,但其入参 ptr/len 对 p6 入口节点无意义。
* **节点输入**:`validate_workflow_frame_region`(`:7962-8077`)从"只接受
  无 projection、无 members 的精确路径"(`:8001-8011`)扩展为接受
  `lower_value_region` 已生成的规范 ANF 构造 region(`core_lower.cpp:5098-
  5099`):精确转发(`WorkflowFrameSource` 既有两形,`:8013-8072`)、字段投
  影、`CoreConstructExpr` 组装(如 `SummaryInput{...}`)。调度器在 call
  runner **之前**发输入构造序列:标量按宽度 load/store、enum tag 拷贝、
  aggregate 按布局常尺寸拷贝、有界 collection 发按 stride 的有界元素拷贝
  循环(容量编译期已知)、**String PtrLen 8 字节共享拷贝,负载指针原样保
  留**(来源只可能是入口 arena、rodata、上游 bridge 结果放置--全部全运行
  稳定)。祖先/调度序校验保留(`:8054-8063`)。
* **返回**:返回区同样放开投影与构造(`:8289-8332` 今天要求精确声明
  nominal 的裸路径):精确节点输出时 run2 返回该 O_k;构造/投影返回时调度
  器把结果字拷贝进工作流输出区(可复用最后一个读取者实例的 O_k 之后的专
  用 WF-output 槽,坐标编译期固定),run2 返回该槽与布局 size。宿主以
  workflow 输出布局根+wire 输出根行走编码,观察形与 agent lane 完全同构。
* **pattern arena**:节点输入/返回构造区不含 match(`if let` 在 agent flow
  内),工作流 storage 的 pattern/coercion arena 拒编保持(`:8093-8098`);
  计算终态 match 的放行只发生在打包 agent 的 flow storage(D2)。

### 6.4 混合工作流与描述符

一个工作流允许同时含 opaque 节点(能力终态 E2 转发)与 p6/bridge 节点:
import 表逐条选 functype;调度器按节点 runner 形态分派(既有 opaque 路径
字节保留);描述符 `frame_contract` 从恒 `WireJson`(`:10084-10088`)改为:
任一节点 p6 即 `P6Frame`,`workflow_lane.nodes[]` 增 `runner_lane:
"opaque"|"p6"`,并携带 frame section 与每实例 I/C/O 坐标、入口/结果放置表。
旧 JS 宿主按描述符版本拒识未知形态。

## 7. D6 -- 单页容量裁决

### 7.1 总账(编译期常量,全部进同一比较族)

agent(p6/bridge)与 workflow(p6)模块的页高水位:

```
H = max(既有低保留区高水位,
        rodata_base + rodata_extent,                         // 256 + <=768
        event_end(仅能力工作流))
  + (bridge) bridge control-block 页帧:
      call_site_count * align8(kP6BridgeControlBlockStride)  // 定步长块区
      + align8(Σ_call_site spill_bytes)                       // 标量/PtrLen spill 槽区
  + sum(每个 input-reached container 的对齐 backing_size)   // v1 sum 规则
  + entry/input payload arena:
      align8(sum 各 String 槽 schema 上界) + kP6StringPoolBytes(无界池)
  + (workflow) sum_打包实例 (align8(I)+align8(C)+align8(O))
  + (bridge)  sum_call_site (align8(result_size)
                             + result payload 上界和 + 池份额)
  + construct/closure heap 既有高水位
H <= 65536 否则 wasm.RESOURCE_EXHAUSTED
```

* bridge 新区在模块内位置(agent 模块):control-block 页帧与 per-call-site
  结果放置都接在既有 backing/payload-arena 高水位之后、construct heap 之前
  (与 placement 同一 sum 游标,次序:control-block 页帧 -> 结果放置及其
  payload -> construct heap);workflow 模块:control-block 页帧是 node-frame
  block 之前的一档(`[W0a,W0b)`),结果放置在 `[W2,W3)`(§6.1)。二者均编译
  期常量。模块不发 bridge 能力则这两档零字节。
* 新增具名常量(进 `core_wasm_abi_constants.hpp`,带 static_assert):
  `kP6RodataBase=256`、`kP6RodataCapacity=768`、
  `kP6FrameStringPoolBytes`(无界 String 的共享兜底池,初值 **2048**,agent
  入帧与每份 bridge 结果放置各计一份池份额;取值依据:覆盖目标用例全部
  String 负载(最长字面量/结果 <32 字节)并留两个数量级余量,同时把总额
  外预算压在个位数 KiB)。池是编译期保留,不是运行时申请;有界 String 仍
  按 schema 上界精算,不双算池。
* 比较族沿用 v1 的单门族(construct-heap 门在
  `core_wasm_codegen.cpp:7372-7382`;fn construct-heap 预算 SSOT 在
  `:6150`;placement sum 的页内门在 `:6861`;工作流侧
  `compute_event_layout` 的两阶段门在 `:9029-9058`)--v2 只向其加项,不新增
  第二套裁决。每条拒绝消息指名超限区、extent、页容量与缩小手段。
* 编译期可裁决的依据:字面量长度、布局 size/stride/capacity、有界 String
  上界、调用点数、节点数全部编译期已知;无界输入由固定池封顶,宿主 pack
  时实际超长即 fail-closed(与 bounded collection 同哲学:固定页是硬合同)。

### 7.2 目标用例的数量级核对(决策依据,非随机数)

5 用例:rodata 去重后 <128 字节;3 个打包实例的 I/C/O 块各 <128 字节
(PtrLen 字段 8 字节、enum tag 4 字节,无 collection);3 个 bridge 结果放置
各 <64 字节结构 + 4/8/25 字节负载;event 区 1024+8+120=1152。总量远低于
65536,两个数量级余量成立。

### 7.3 section / 描述符的版本处理

* `ahfl.core-layout.v1` 的 section **名不变**(它命名的是 section 家族),
  payload `format_version` 升到 2,以确定性追加记录承载:rodata span、
  bridge control-block 页帧 span(基址/extent)与逐 call-site 块坐标
  (call_site_id/block 偏移/arity)、bridge 结果放置表(含
  call_site_id/result 根/base/extent/payload span)、workflow node-frame 块
  (每打包实例 I/C/O base/size)、入口 payload arena span。解码器接受 v1/v2,
  canonical re-encode 等式校验扩展到新字段;v2 新字段缺失/越界/与内存图
  不一致按既有家族拒入。
* **段发射谓词随 v2 泛化(字节义务不变)**:今天 agent 仅在 `p6_frame`
  (raw-input 投影或 computed final)时发 layout/wire 段、workflow 仅在有
  imports 时发 wire 段(`core_wasm_codegen.cpp:10186-10187`)。v2 把
  "发 frame 段"的条件扩为析取:computed final 或 raw-input 投影(既有)或
  含 String 字面量(rodata)或含 bridge 调用(control-block/结果放置)或
  (workflow)含 p6 node-frame 块。wire-schema 段在 agent/workflow 有
  capability imports **或**有边界根(computed/p6 节点的 input/output
  nominal)时发。不含任何新特性的模块析取全假,两段与 Data 段全部缺席,
  逐字节不变(§8.3 探针钉死)。
* `ahfl.wire-schema.v1` 不改格式:多参数根早已在 `params[]` 中;工作流边
  界根复用既有 frame_roots。
* 描述符新增:`frame_lane.rodata_base/capacity`、
  `frame_lane.bridge_call_sites[]`、工作流 `frame_lane.node_blocks[]` 与
  每节点 `runner_lane`。全部派生自发字节的同一 plan,无第二推导。

### 7.4 运行时失败面

* 唯一新增动态失败:无。所有新区界编译期固定;模块对 rodata 无 store;
  bridge 回调非 OK 即 trap(确定性 single-run 语义);checked-alloc 耗尽
  返回 0 的既有路径保留(`make_checked_alloc_body`,`:8413+`)。

## 8. 信任、普查与逐字节义务

### 8.1 信任面增量

* 新可读区间(rodata、bridge control-block 页帧、bridge 结果放置、workflow
  node 块)全部进入 core-layout v2 负载,admit 时校验:(i) 区间两两不相交且
  在页内;(ii) control block/结果指针只命名其声明区间,且每个 call-site 块
  坐标与声明 arity 一致、块不越出 control-block 页帧;
  (iii) 每个 bridge 调用点的
  capability+source_symbol+arity 与 wire schema capability 记录一致;
  (iv) 布局/wire 一致性复用 `verify_frame_layout_wire_consistency`
  (`include/ahfl/compiler/ir/core_frame_layout.hpp:107-110`)。
* 模块字节仍是不可信证据:宿主对 control block 的每个 ptr/len 做 checked
  u64 + 区间成员测试;PENDING 不 latch、不写事件;能力结果先
  `validate_value` 再打包。
* Data section 只初始化白名单地址(rodata 与 ctx PtrLen 默认词),admit
  校验段数量、目标区间与长度,拒绝指向区间外的段。

### 8.2 普查移动(17/5 -> 18/5 -> 23/0)

钉死点 `conformance_wasm_node_runner.cpp:74-75` **分两档**移动:V2-C 新增
直接 agent 多参桥接 conformance 用例(1 个新 scenario)的同 commit,
`kExpectedAgreed 17 -> 18`、skip 维持 5(新 manifest 带
`engines.evaluator: true`、无 skip 声明,走正常差分 lane);V2-E 同 commit 把
`kExpectedAgreed 18 -> 23`、`kExpectedSkipped 5 -> 0`,删除 3 个 manifest
里的 5 处 `node_observation_skip`(`eligible: "computation"` 保留;
`wasm_eligibility.cpp:212-216` 的 Runnable->None 投影随即使编译器侧 skip
消失,双向门不改形状)。node-only 茎集合仍是 `{fb3_higher_order}` 一个
(其 scenario 始终计入 agreed),本修正案不触碰 KR6.8 茎。普查数字不得在
两次提交之间停留在与清单场景数不符的状态。

| 场景 | 解锁所依赖的 rung |
| --- | --- |
| `if_let_e2e/some`、`/none` | V2-A(计算终态 + match arena)+ V2-B(String PtrLen/rodata)+ V2-D(打包 computed runner、构造节点返回、入口 pack) |
| `enum_variant_e2e/struct_payload_default` | V2-A + V2-B + V2-D(含 struct-payload match 绑定) |
| `e2e_multi_agent/priority_low`、`/priority_high` | V2-A + V2-B(字面量 `"high"`)+ V2-C(bridge + 多参数 + enum 路由)+ V2-D(构造 SummaryInput 节点输入、混合工作流、ctx 跨状态 PtrLen) |

### 8.3 逐字节义务(每档结构验证,不论证)

* 不含 String 字面量、不含计算终态、不含 bridge 调用、不含 p6 节点的模块
  字节不变:既有 golden/binary 探针增加显式断言--Data section 缺席、runv
  缺席/存在性同 v1、import functype 仍只有 tuple、workflow 类型表仍 5 项、
  exports 数仍为 9/11。
* 工作流 opaque 路径(identity/capability workflow)是字节不变的重点:
  runner/run2/schedule 字节由 runner 形态选择保证,只有 computed runner 是
  追加函数;任何 census/golden 计数移动必须与发射改动同 commit。

## 9. 与 KR6.8 / evaluator 退役的关系(D8)

* RFC 0026 P8 的退役门是"差分全绿后**同一 commit** 删除
  `src/runtime/evaluator/`"(`docs/rfcs/0026-ir-tower-and-execution-model.zh.md:201-220`、
  `:285-286`;`docs/plans/q4-2026-roadmap.zh.md:199` 状态未开始)。本修正案
  让 Node-vs-evaluator 差分新增 5 个全绿场景,是**加强**该门的前置,不移动
  它;evaluator 在 v2 期间继续作为差分参照与 canonical SSOT 宿主
  (`value_to_json`/`decode_json` 被桥接宿主复用)。
* **生产宿主要求不扩大**:v1 交付时 runv 的 pack/encode 宿主只有 Node JS
  (`src/` 无 runv 调用方),v2 的 bridge 宿主逻辑同档只落 Node JS。C++ 生产
  引擎(`src/runtime/engine/`)的 Value<->P4-D 行走 + bridge import 绑定是
  独立后续切片;在其落地前,带 p6/bridge 标记的模块在生产宿主侧按"未知
  descriptor/functype"拒绝,能力终态/identity 模块不受影响。KR6.8 的删除门
  必须把该 C++ 适配列为自己的前置(它是让 wasm 成为 `ahflc run` 唯一引擎
  的必要条件),但不属于本设计 gate。
* durable-effect/D2b:桥接调用的结果是外部能力输出,确定性重放只能来自
  memo/durable authority;single-run trap 策略与 FB-4 完全一致,不提前承诺
  resume。

## 10. 有序实现阶梯(每档可独立评审、模块始终可发布)

1. **V2-A 计算终态(agent lane)**:`ComputedReturn` action;终态子集准入
   (含 match/if-let、struct/enum 构造,暂不含 String 槽);物化与 output
   frame fit 门;runv computed 臂;`final_kind` 如实输出;终态 pattern arena
   放行。测试:新增**直接 agent**计算终态 fixture(标量/tag-enum/嵌套
   struct),runv 返回 12288 的二进制探针;identity/capability 字节不变探针。
   普查不动。
2. **V2-B 模块内 PtrLen 构造**:rodata 保留区 + Data section 发射(含 ctx
   PtrLen 默认段);String 字面量 PtrLen kind 与槽位/构造/ctx-store 放行;
   layout section format_version=2(rodata span);input payload arena 容量
   规划从 0 改为"有界上界和 + 池"(v1 遗留
   `payload_arena_capacity=0`,`core_wasm_codegen.cpp:10016`);Node 宿主行走
   器加 rodata 区间成员;Decimal/Duration i64 字面量(无语差分 fixture,
   仅词法/行走单测)。测试:含字符串字面量响应的直接 agent fixture、
   rodata 去重/越界拒编、Data 缺席字节探针。普查不动。
3. **V2-C 能力帧桥接(agent lane)**:`kTypeCapabilityBridge` 追加类型;
   handler 模式 bridge 语句门与 arity 泛化;control block 页帧发射(§4.3,
   含标量 spill 槽区与 D6 容量项);per-call-site 结果放置(sum 规则)与结果
   payload 区;v2 layout 负载(V2-B 已升 format_version=2)追加 bridge span,
   frame 段发射谓词在本档泛化到"含 bridge 调用";Node 宿主桥接回调(P4-D 行走 -> `serialize_args_for_wire_json` -> registry ->
   validate -> pack);多参数 e2e 形状;非 OK trap、错误 arity/越界/坏结果
   fail-closed 负例;functype 逐字节探针。以一个新增**直接 agent** 多参桥接
   conformance 用例验收,普查 **17/5 -> 18/5**(新场景同 commit 移钉;5 个
   blocked_kr66 工作流场景仍 skip)。
4. **V2-D 工作流计算节点打包**:`allow_computed_goto=true` 同门;computed
   runner(handler 函数进 workflow 模块、帧基址重定位);node-frame 块规划与
   capacity 总账;入口 host pack;节点输入/返回投影构造发射;workflow
   layout v2 section + 边界 schema;混合 import 类型;描述符 runner_lane/
   node_blocks;Node 宿主工作流 p6 lane。测试:投影透传、双上游构造输入、
   PtrLen 零拷贝共享、混合 opaque+p6 工作流、opaque 工作流逐字节回归。
5. **V2-E 普查收口**:删除 5 处 `blocked_kr66`,普查 18/5 -> 23/0(同 commit
   移动钉与声明,钉在移动前必与清单实际场景数一致);5 场景 Node-vs-evaluator
   三维差分全绿;文档/注释口径从 v1 §11 的"deferred"改为引用本修正案。

## 11. 显式非目标重申

* 运行时字符串拼接/插值/数字格式化及其 bump writer;
* durable resume 对桥接结果的回放与 memo 格式改动(D2b 独立门);
* wasmtime/真实浏览器引擎证据;
* C++ 生产宿主的 pack/encode/bridge 适配(KR6.8 前置,独立切片);
* closure/fn 作为帧边界值、f64 运算 opcode 梯、map/decimal/duration/
  timestamp/uuid/float 的帧行走子集扩展;
* 两个节点复用同一打包实例的 p6 工作流(v2 直接拒编);
* 任何对 E1/E2/E3/FB 既有模块在不使用新特性时的字节改动。

## 12. 落地状态(实现记录)

* **V2-D emission half 1(RETURN)**:已落地(commit `05ad54d760c566394a22f7babbd974f54c81b6cd`),
  普查 21/5 -> 24/2
  (`enum_variant_e2e`、`if_let_e2e` 两场景)。
* **V2-D emission half 2(CTX + 构造节点输入)**:已落地(commit
  `2cd6dbf65bc1cc7bd268069d696567ef832ecefe`),普查 24/2 ->
  **26/0**(据实纠正 §8.2 设计草稿的 23/0:漏计 V2-C fix-forward 新增的 3 个
  agreed 场景,硬钉按场景计),`blocked_kr66` 跳过集清空(`e2e_multi_agent` 两场景)。实现要点:
  打包 runner 改为在**有界燃料 ladder 上对每个状态分发到重定位 handler**
  (plain goto 内联、computed goto/computed return 调 handler),共享跨 runner
  的稠密 bridge registry(per-runner 可达性压缩到全局 dense id + 全局排序
  import 表 + bridge/tuple 混合 functype),scalar/tag-enum 能力结果 -> ctx
  store 按 `same_word_width` 规则放行,scheduler 零填充 C_k 并物化非入口
  bare-forward 节点帧与构造式 SummaryInput;状态序列证据由新增的**固定状态
  进入 trace ring**(`state_trace_base`,每个分发状态追加 (runner,state)
  8 字节记录)给出,取代静态 walk(后者会漏掉 if 路由未走的分支)。
  computed-goto preamble 喂养**不透明** capability/identity 终态仍按精确
  `UNSUPPORTED_WORKFLOW_FRAME` 拒编(`v2d_computed_goto_preamble_reject` 探针
  继续锁定)。
* **V2-E 普查收口**:随 half 2 同 commit 完成(26/0);KR6.8 evaluator 退役
  门与 C++ 生产宿主桥接适配仍是非目标(见 §9、§11)。
