# 退款金额规则：按需数据合同证明

本例使用合同声明退款金额规则，并演示如何证明规则或查找反例。`amount`、`total` 是 `Int(0,1000000)`，单位为最小货币单位（例如分）；在 `amount <= total` 前提下，请求证明 `total - amount >= 0`。

从仓库根目录执行：

```bash
AHFLC="$PWD/build/dev/src/tooling/cli/ahflc"
# 检查类型与控制规则，不请求数据合同证明。
"$AHFLC" check --manifest examples/refund-rules/ahfl.toml
# 请求数据合同证明。
"$AHFLC" check --manifest examples/refund-rules/ahfl.toml --verify-data-contracts
```

添加 `--verify-data-contracts` 后，预期得到 `data_contract_status: proved`，
表示规则在声明的前提下成立；`data_contract_scope: contract_implication`
表示证明范围为合同蕴含。控制规则与数据规则分别报告。
`runtime_contract_checks: not_implemented` 提醒你运行时不会自动执行这些合同检查。

## 制造一个公式反例

为了保留正例，复制到临时目录并将 `>= 0` 改为 `> 0`：

```bash
RULE_DEMO_DIR=$(mktemp -d)
cp -R examples/refund-rules/. "$RULE_DEMO_DIR/"
python3 - "$RULE_DEMO_DIR/src/main.ahfl" <<'PYCODE'
from pathlib import Path
import sys
path = Path(sys.argv[1])
source = path.read_text()
path.write_text(source.replace(
    "ensures: input.total - input.amount >= 0;",
    "ensures: input.total - input.amount > 0;",
))
PYCODE
"$AHFLC" check --manifest "$RULE_DEMO_DIR/ahfl.toml" --verify-data-contracts
```

此命令应非零退出，报告 refuted 与变量赋值。`amount = total` 满足前提，却使差值为零，反驳严格大于零。solver 可以选择任意满足该关系的具体值，不能假定它一定返回 0。

## 证明边界

数据合同证明检查 **requires ⇒ goal**，即目标是否由前提推出。
本例证明的是输入金额规则，不涵盖 flow 中的赋值、返回值或实现与公式的等价关系。
如果将 `ensures` 改为 `output.remaining == input.total - input.amount`，
不能仅凭 flow 的计算过程让这条规则通过证明。

调用者必须校验输入满足 `requires`；运行时不会自动检查合同谓词或 flow 中的
`assert`、`requires`。本例通过输入上下界保证减法结果落在 ±1000000 内。
一般整数公式证明不等于机器整数的溢出安全证明。

验证无法支持目标或必要前提、前提矛盾、结果未知（`unknown`）、超时、缺少求解器，
或发现反例时，命令都会失败。声明 `invariant` 不能让该规则自动获得证明。
不要同时传入 `--verify-data-contracts` 和 `--verify=off`，这两个选项互相冲突。

[退款审核示例](../refund/audit.ahfl) 使用宿主提供的订单服务谓词。
你仍可以检查它的控制规则，但仅有谓词声明时无法证明对应的数据合同；
显式请求数据合同证明会失败。相关用法见[保障教程](../../docs/tutorials/assurance.zh.md)，
详细规则见[保障规范](../../docs/spec/assurance.zh.md)。
