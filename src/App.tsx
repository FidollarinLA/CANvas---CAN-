import { ChangeEvent, DragEvent, useMemo, useState } from "react";
import Papa from "papaparse";
import Prism from "prismjs";
import "prismjs/components/prism-clike";
import "prismjs/themes/prism.css";
import * as XLSX from "xlsx";
import { mockGenerateScript } from "./services/mockAi";
import { Endian, normalizeRow, RecordRow } from "./utils/canHex";
import { buildPresentationWorkbook, generateSendList, normalizeExcelRow, ExcelInputRow, GeneratedListResult } from "./utils/listGenerator";
import { parseSendList, summarizeSendList, SendListDocument } from "./utils/listValidation";

function App() {
  const [rows, setRows] = useState<RecordRow[]>([]);
  const [excelRows, setExcelRows] = useState<ExcelInputRow[]>([]);
  const [generatedResult, setGeneratedResult] = useState<GeneratedListResult | null>(null);
  const [sendListDocument, setSendListDocument] = useState<SendListDocument | null>(null);
  const [dragOver, setDragOver] = useState(false);
  const [endian, setEndian] = useState<Endian>("little");
  const [payloadBytes, setPayloadBytes] = useState(8);
  const [hexOutput, setHexOutput] = useState("");
  const [prompt, setPrompt] = useState("");
  const [generatedCode, setGeneratedCode] = useState("// 在这里显示 AI 生成的脚本");
  const [loadingCode, setLoadingCode] = useState(false);

  const previewRows = useMemo(() => rows.slice(0, 10), [rows]);
  const activeListDocument = generatedResult?.document ?? sendListDocument;
  const listPreviewRows = useMemo(() => activeListDocument?.entries.slice(0, 12) ?? [], [activeListDocument]);
  const validationSummary = useMemo(() => (activeListDocument ? summarizeSendList(activeListDocument) : null), [activeListDocument]);
  const highlightedCode = useMemo(
    () => Prism.highlight(generatedCode, Prism.languages.clike, "clike"),
    [generatedCode]
  );

  const handleFile = async (file: File) => {
    const ext = file.name.split(".").pop()?.toLowerCase();

    if (ext === "csv") {
      Papa.parse<Record<string, unknown>>(file, {
        header: true,
        complete: (result) => {
          const parsed = result.data.map(normalizeRow).filter((v): v is RecordRow => !!v);
          setRows(parsed);
          const normalizedExcelRows = result.data.map(normalizeExcelRow).filter((v): v is ExcelInputRow => !!v);
          setExcelRows(normalizedExcelRows);
        },
      });
      return;
    }

    if (ext === "xlsx" || ext === "xls") {
      const buffer = await file.arrayBuffer();
      const workbook = XLSX.read(buffer, { type: "array" });
      const firstSheet = workbook.Sheets[workbook.SheetNames[0]];
      const jsonRows = XLSX.utils.sheet_to_json<Record<string, unknown>>(firstSheet, { raw: true });
      const parsed = jsonRows.map(normalizeRow).filter((v): v is RecordRow => !!v);
      setRows(parsed);
      const normalizedExcelRows = jsonRows.map(normalizeExcelRow).filter((v): v is ExcelInputRow => !!v);
      setExcelRows(normalizedExcelRows);
      return;
    }

    alert("仅支持 .csv / .xlsx / .xls 文件");
  };

  const handleListFile = async (file: File) => {
    const content = await file.text();
    const document = parseSendList(content);
    setSendListDocument(document);
    setGeneratedResult({
      xml: content,
      document,
      lines: document.entries.map((entry, index) => ({ lineNumber: index + 1, interval: entry.interval, text: entry.rawLine })),
      cycles: document.cycles ?? 1,
      mode: "direct",
    });
  };

  const onInputFile = (e: ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (file) void handleFile(file);
  };

  const onInputListFile = (e: ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (file) void handleListFile(file);
  };

  const onDrop = (e: DragEvent<HTMLLabelElement>) => {
    e.preventDefault();
    setDragOver(false);
    const file = e.dataTransfer.files?.[0];
    if (file) void handleFile(file);
  };

  const convertNow = () => {
    if (excelRows.length === 0) {
      alert("请先上传 Excel 文件。");
      return;
    }

    const result = generateSendList(excelRows, 1);
    setGeneratedResult(result);
    setSendListDocument(result.document);
    setHexOutput(result.xml);
  };

  const exportValidationExcel = () => {
    if (!generatedResult) {
      alert("请先生成 .list 文件。");
      return;
    }

    const workbook = buildPresentationWorkbook(excelRows, generatedResult);
    XLSX.writeFile(workbook, "CANvas-比赛展示.xlsx");
  };

  const downloadListFile = () => {
    if (!generatedResult) {
      alert("请先生成 .list 文件。");
      return;
    }

    const blob = new Blob([generatedResult.xml], { type: "text/plain;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "CANvas-生成结果.list";
    a.click();
    URL.revokeObjectURL(url);
  };

  const askAi = async () => {
    setLoadingCode(true);
    try {
      const code = await mockGenerateScript(prompt);
      setGeneratedCode(code);
    } finally {
      setLoadingCode(false);
    }
  };

  const copyCode = async () => {
    await navigator.clipboard.writeText(generatedCode);
    alert("脚本已复制到剪贴板");
  };

  const exportScript = () => {
    const blob = new Blob([generatedCode], { type: "text/plain;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "canvas-script.can";
    a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <div className="min-h-screen px-4 py-8 text-canvas-ink sm:px-6 lg:px-10">
      <div className="mx-auto max-w-7xl space-y-6">
        <header className="animate-riseIn rounded-2xl border border-canvas-line bg-canvas-panel p-6 shadow-soft grid-paper">
          <p className="text-sm font-semibold tracking-[0.2em] text-canvas-accent">CANVAS WORKBENCH</p>
          <h1 className="mt-2 text-3xl font-bold">Excel 导入生成 .list 的 CAN 报文工作台</h1>
          <p className="mt-2 max-w-3xl text-sm text-canvas-mute">
            一键导入 Excel，自动生成符合 SendList 结构的 .list 报文，并提供规则校验与比赛用 Excel 导出。
          </p>
        </header>

        <section className="grid gap-6 lg:grid-cols-2">
          <div className="animate-riseIn rounded-2xl border border-canvas-line bg-canvas-panel p-5 shadow-soft">
            <h2 className="text-lg font-semibold">1. Excel 导入与预览</h2>
            <label
              onDragOver={(e) => {
                e.preventDefault();
                setDragOver(true);
              }}
              onDragLeave={() => setDragOver(false)}
              onDrop={onDrop}
              className={`mt-4 flex h-36 cursor-pointer items-center justify-center rounded-xl border-2 border-dashed px-4 text-center transition ${
                dragOver ? "border-canvas-accent bg-blue-50" : "border-canvas-line bg-slate-50"
              }`}
            >
              <input type="file" accept=".csv,.xlsx,.xls" className="hidden" onChange={onInputFile} />
              <span className="text-sm text-canvas-mute">拖拽或点击上传 Excel / CSV 文件</span>
            </label>

            <div className="mt-4 overflow-x-auto rounded-xl border border-canvas-line">
              <table className="min-w-full text-sm">
                <thead className="bg-slate-100 text-left">
                  <tr>
                    <th className="px-3 py-2">ID</th>
                    <th className="px-3 py-2">变量名</th>
                    <th className="px-3 py-2">原始值 (10进制)</th>
                    <th className="px-3 py-2">目标类型</th>
                  </tr>
                </thead>
                <tbody>
                  {previewRows.map((row, idx) => (
                    <tr key={`${row.id}-${row.variable}-${idx}`} className={idx % 2 ? "bg-slate-50" : "bg-white"}>
                      <td className="px-3 py-2 font-mono">{row.id}</td>
                      <td className="px-3 py-2">{row.variable}</td>
                      <td className="px-3 py-2">{row.rawValue}</td>
                      <td className="px-3 py-2">{row.targetType}</td>
                    </tr>
                  ))}
                  {previewRows.length === 0 && (
                    <tr>
                      <td className="px-3 py-6 text-center text-canvas-mute" colSpan={4}>
                        上传后展示前 10 行预览
                      </td>
                    </tr>
                  )}
                </tbody>
              </table>
            </div>
          </div>

          <div className="animate-riseIn rounded-2xl border border-canvas-line bg-canvas-panel p-5 shadow-soft">
            <h2 className="text-lg font-semibold">2. 一键生成 .list</h2>
            <div className="mt-4 grid grid-cols-2 gap-3">
              <label className="text-sm">
                <span className="mb-1 block text-canvas-mute">字节序</span>
                <select
                  className="w-full rounded-lg border border-canvas-line bg-white px-3 py-2"
                  value={endian}
                  onChange={(e) => setEndian(e.target.value as Endian)}
                >
                  <option value="little">Little Endian</option>
                  <option value="big">Big Endian</option>
                </select>
              </label>

              <label className="text-sm">
                <span className="mb-1 block text-canvas-mute">每帧字节数</span>
                <select
                  className="w-full rounded-lg border border-canvas-line bg-white px-3 py-2"
                  value={payloadBytes}
                  onChange={(e) => setPayloadBytes(Number(e.target.value))}
                >
                  <option value={8}>8 (CAN)</option>
                  <option value={16}>16 (CAN FD)</option>
                  <option value={32}>32 (CAN FD)</option>
                  <option value={64}>64 (CAN FD)</option>
                </select>
              </label>
            </div>

            <button
              onClick={convertNow}
              className="mt-4 rounded-lg bg-canvas-accent px-4 py-2 text-sm font-semibold text-white transition hover:brightness-110"
            >
              生成 .list 报文
            </button>
            <div className="mt-3 flex gap-3">
              <button className="rounded-lg border border-canvas-line px-3 py-2 text-sm" onClick={downloadListFile}>
                下载 .list 文件
              </button>
              <button className="rounded-lg border border-canvas-line px-3 py-2 text-sm" onClick={exportValidationExcel}>
                导出比赛 Excel
              </button>
            </div>

            <textarea
              className="mt-4 h-72 w-full rounded-xl border border-canvas-line bg-slate-950 p-3 font-mono text-xs text-slate-100"
              readOnly
              value={hexOutput || "<SendList m_dwCycles=\"1\">..."}
            />
          </div>
        </section>

        <section className="grid gap-6 lg:grid-cols-[1.1fr_0.9fr]">
          <div className="animate-riseIn rounded-2xl border border-canvas-line bg-canvas-panel p-5 shadow-soft">
            <h2 className="text-lg font-semibold">3. 生成结果规则校验</h2>
            <p className="mt-2 text-sm text-canvas-mute">
              生成后会按比赛样例格式进行校验：4 条一组、固定属性、48 位 obj、计数器与首字节模式。
            </p>

            <label className="mt-4 flex h-28 cursor-pointer items-center justify-center rounded-xl border-2 border-dashed border-canvas-line bg-slate-50 px-4 text-center transition hover:border-canvas-accent">
              <input type="file" accept=".list,.txt,.xml" className="hidden" onChange={onInputListFile} />
              <span className="text-sm text-canvas-mute">点击上传已有 .list 文件复核，或直接生成后对比</span>
            </label>

            <div className="mt-4 grid gap-3 sm:grid-cols-4">
              <div className="rounded-xl border border-canvas-line bg-slate-50 p-3">
                <p className="text-xs text-canvas-mute">记录总数</p>
                <p className="mt-1 text-2xl font-semibold">{validationSummary?.totalEntries ?? 0}</p>
              </div>
              <div className="rounded-xl border border-canvas-line bg-slate-50 p-3">
                <p className="text-xs text-canvas-mute">通过</p>
                <p className="mt-1 text-2xl font-semibold text-emerald-600">{validationSummary?.validEntries ?? 0}</p>
              </div>
              <div className="rounded-xl border border-canvas-line bg-slate-50 p-3">
                <p className="text-xs text-canvas-mute">未通过</p>
                <p className="mt-1 text-2xl font-semibold text-rose-600">{validationSummary?.invalidEntries ?? 0}</p>
              </div>
              <div className="rounded-xl border border-canvas-line bg-slate-50 p-3">
                <p className="text-xs text-canvas-mute">分组</p>
                <p className="mt-1 text-2xl font-semibold">{validationSummary?.groupCount ?? 0}</p>
              </div>
            </div>

            <div className="mt-4 rounded-xl border border-canvas-line p-4">
              <p className="text-sm font-semibold">规则状态</p>
              <p className="mt-2 text-sm text-canvas-mute">
                {generatedResult
                  ? generatedResult.document.issues.length === 0 && generatedResult.document.entries.every((entry) => entry.issues.length === 0)
                    ? "校验通过，当前 .list 文件结构满足比赛展示样例。"
                    : "存在规则不匹配项，请查看右侧明细。"
                  : "等待上传 .list 文件。"}
              </p>
            </div>

            <div className="mt-4 overflow-hidden rounded-xl border border-canvas-line">
              <table className="min-w-full text-sm">
                <thead className="bg-slate-100 text-left">
                  <tr>
                    <th className="px-3 py-2">行号</th>
                    <th className="px-3 py-2">iInterval</th>
                    <th className="px-3 py-2">首字节</th>
                    <th className="px-3 py-2">末三字节</th>
                    <th className="px-3 py-2">状态</th>
                  </tr>
                </thead>
                <tbody>
                  {listPreviewRows.map((entry, idx) => (
                    <tr key={`${entry.lineNumber}-${idx}`} className={idx % 2 ? "bg-slate-50" : "bg-white"}>
                      <td className="px-3 py-2">{entry.lineNumber}</td>
                      <td className="px-3 py-2">{entry.interval}</td>
                      <td className="px-3 py-2 font-mono">{entry.obj.slice(0, 2)}</td>
                      <td className="px-3 py-2 font-mono">{entry.obj.slice(-6)}</td>
                      <td className="px-3 py-2">{entry.issues.length === 0 ? "通过" : "未通过"}</td>
                    </tr>
                  ))}
                  {listPreviewRows.length === 0 && (
                    <tr>
                      <td className="px-3 py-6 text-center text-canvas-mute" colSpan={5}>
                        上传后显示前 12 条规则校验预览
                      </td>
                    </tr>
                  )}
                </tbody>
              </table>
            </div>
          </div>

          <div className="animate-riseIn rounded-2xl border border-canvas-line bg-canvas-panel p-5 shadow-soft">
            <h2 className="text-lg font-semibold">4. 比赛 Excel 导出</h2>
            <p className="mt-2 text-sm text-canvas-mute">
              一键导出包含 Summary / Input / GeneratedList 三个工作表的 Excel，用于比赛演示和答辩展示。
            </p>

            <div className="mt-4 rounded-xl border border-canvas-line bg-slate-50 p-4 text-sm text-canvas-mute">
              <p className="font-semibold text-canvas-ink">导出内容</p>
              <ul className="mt-2 space-y-1">
                <li>• Summary：生成模式、m_dwCycles、输入行数、生成行数</li>
                <li>• Input：Excel 原始输入与解析结果</li>
                <li>• GeneratedList：生成的 .list 文本片段</li>
              </ul>
            </div>

            <div className="mt-4 rounded-xl border border-canvas-line bg-slate-950 p-4 font-mono text-xs text-slate-100">
              {generatedResult ? (
                <>
                  <p>m_dwCycles = {generatedResult.document.cycles ?? "未解析"}</p>
                  <p>记录数 = {generatedResult.document.entries.length}</p>
                  <p>状态 = {generatedResult.document.issues.length === 0 ? "结构通过" : "存在问题"}</p>
                </>
              ) : (
                <p>请先生成 .list 后再导出比赛 Excel。</p>
              )}
            </div>
          </div>
        </section>

        <section className="animate-riseIn rounded-2xl border border-canvas-line bg-canvas-panel p-5 shadow-soft">
          <h2 className="text-lg font-semibold">5. AI Script 助手</h2>
          <div className="mt-4 grid gap-4 lg:grid-cols-[1fr_auto]">
            <input
              className="rounded-lg border border-canvas-line px-3 py-2"
              value={prompt}
              onChange={(e) => setPrompt(e.target.value)}
              placeholder="例如：把所有电机指令周期改为 20ms，并在第 5 帧加入唤醒信号"
            />
            <button
              onClick={askAi}
              disabled={loadingCode}
              className="rounded-lg bg-canvas-ink px-4 py-2 text-sm font-semibold text-white disabled:opacity-60"
            >
              {loadingCode ? "生成中..." : "发送"}
            </button>
          </div>

          <div className="mt-4 flex gap-3">
            <button className="rounded-lg border border-canvas-line px-3 py-2 text-sm" onClick={() => void copyCode()}>
              一键复制
            </button>
            <button className="rounded-lg border border-canvas-line px-3 py-2 text-sm" onClick={exportScript}>
              导出为脚本文件
            </button>
          </div>

          <pre className="mt-4 max-h-80 overflow-auto rounded-xl border border-canvas-line bg-white p-4 text-sm font-mono">
            <code dangerouslySetInnerHTML={{ __html: highlightedCode }} />
          </pre>
        </section>
      </div>
    </div>
  );
}

export default App;
