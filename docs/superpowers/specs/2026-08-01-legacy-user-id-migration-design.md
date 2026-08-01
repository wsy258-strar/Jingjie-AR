# 旧版用户 ID 无符号化迁移设计

## 背景

当前初始化脚本将 `users.id`、`sessions.user_id` 及新增作品互动表的 `user_id`
定义为 `BIGINT UNSIGNED`。已部署的早期数据库可能将前两列定义为有符号
`BIGINT`，导致创建 `artwork_likes` 和 `artwork_comments` 时，外键列与
`users.id` 类型不兼容。

`CREATE TABLE IF NOT EXISTS` 只能初始化新库，不能升级既有列类型；因此升级
逻辑必须独立于 `sql/jingjie_ar_schema.sql`。

## 范围与目标

新增一个显式、版本化、可重复执行的迁移器，将旧库的：

- `users.id BIGINT`
- `sessions.user_id BIGINT`

升级为 `BIGINT UNSIGNED`，并恢复 `sessions.user_id -> users.id` 的外键。
迁移器只处理这一兼容问题；不会删除业务数据、不会在应用启动期间执行、不会
修改全景配置或作品数据。

## 方案选择

采用 Bash 迁移器而非 MySQL 存储过程：它仅依赖部署账号已有的 `SELECT`、
`ALTER` 和 `REFERENCES` 权限，不要求 `CREATE ROUTINE`；同时能在执行 DDL
前完成备份文件、字段类型、负值和外键拓扑的明确校验。

迁移器位于：

```text
sql/migrations/20260801_align_user_id_unsigned.sh
```

它从已经导出的 `.env` 中读取 `MYSQL_HOST`、`MYSQL_PORT`、`MYSQL_USER`、
`MYSQL_PASSWORD`、`MYSQL_DATABASE`。调用者必须通过 `MIGRATION_BACKUP_FILE`
传入一个存在且非空的备份文件；缺少该文件时，迁移器拒绝执行任何 DDL。

## 执行模型

1. 读取 `users.id` 和 `sessions.user_id` 的 `COLUMN_TYPE`。
2. 仅接受 `bigint` 或 `bigint unsigned`；其他类型立即失败并输出实际类型。
3. 检查两列不存在负数，防止有符号数据转换为无符号时改变语义。
4. 查询所有引用 `users.id` 的外键。仅允许已知的 `sessions.user_id` 外键；
   发现其他引用时拒绝执行，要求人工评审其依赖关系。
5. 查询 `sessions.user_id` 上实际外键名，而不假设名称固定为
   `sessions_ibfk_1`。
6. 如存在该外键，先删除；再将尚未无符号化的父、子列分别改为
   `BIGINT UNSIGNED NOT NULL`，其中 `users.id` 保留 `AUTO_INCREMENT`。
7. 确保外键以规范名称 `sessions_ibfk_1` 重建，并带有 `ON DELETE CASCADE`。
8. 再次读取字段类型和外键定义，只有全部符合目标结构才返回成功。

MySQL DDL 会隐式提交，因此无法依赖事务回滚。执行前备份是强制门槛；若迁移
失败，运维人员应停止应用并按备份恢复流程处理，而不是删除表或执行不受控的
反向 DDL。

## 幂等性与中断恢复

若两列已经是 `BIGINT UNSIGNED` 且规范外键存在，迁移器不执行 DDL 并成功退出。

如果之前在删除外键或修改其中一列后中断，迁移器会重新检查状态：对仍为有符号
的列补做转换，对缺失外键补做创建。因此“父列已转换 / 子列未转换”或“两个列
均已转换 / 外键未恢复”都可安全恢复。未知类型、负值或未知的额外外键不会被
猜测处理，而是明确失败。

## 部署顺序

对于已有数据库：

1. 备份数据库并确认备份文件非空。
2. 导出应用环境变量，传入 `MIGRATION_BACKUP_FILE`，执行版本化迁移器。
3. 执行 `sql/jingjie_ar_schema.sql` 创建缺失的展馆统计和作品互动表。
4. 构建、启动 `ar_server`，执行回环 API 验收。

对于全新数据库，只执行 `sql/jingjie_ar_schema.sql`；版本化迁移器可省略。

README 和云服务器部署指南都将明确区分这两条路径。

## 验证策略

- 静态测试：脚本必须具备备份门槛、类型白名单、负数检查、外键拓扑检查、
  迁移后验证和幂等分支；部署文档必须给出正确顺序。
- 真实 MySQL 集成测试：在隔离库建立有符号旧 schema、写入用户和会话，执行
  迁移两次，验证数据保留、类型无符号化、外键级联和后续 schema 初始化。
- 中断恢复等价场景：分别构造“只转换父列”和“外键已删除”的状态，确认脚本
  恢复为规范结构。
- 拒绝路径：负值、未知类型、额外外键、缺失或空备份文件必须失败且不执行 DDL。

未配置 `AR_TEST_MYSQL_DATABASE` 时，真实 MySQL 测试应显式跳过；静态测试仍必须
在本地和 CI 中运行。
