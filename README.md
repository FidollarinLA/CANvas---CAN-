# CANvas

CANvas 是一个面向 CAN 调试场景的 Web 工作台 MVP，支持：

- Excel/CSV 上传与数据预览
- 从 Excel 生成 .list 格式 CAN 报文
- .list 报文规则校验与比赛 Excel 导出
- AI 脚本助手（当前为 Mock API）

## 技术栈

- React + Vite + TypeScript
- Tailwind CSS
- xlsx + papaparse
- prismjs

## 本地启动

1. 安装 Node.js 20+（当前机器未检测到 Node/npm）。
2. 安装依赖：

```bash
npm install
```

3. 启动开发：

```bash
npm run dev
```

4. 构建产物：

```bash
npm run build
```

5. 运行后可以上传 `.list` 文件进行规则校验，并导出 `CANvas-校验结果.xlsx`。
5. 运行后可以上传 Excel，生成 `.list` 报文，并导出 `CANvas-比赛展示.xlsx`。

## 数据格式建议

上传表头建议包含：

- `ID`
- `变量名`
- `原始值 (10进制)`
- `目标类型`
- `周期`

也兼容英文键名：`id`、`variable`、`rawValue`、`targetType`、`periodMs`。
