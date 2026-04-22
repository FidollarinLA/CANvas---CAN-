import * as XLSX from "xlsx";
import { floatToHexBytes } from "./canHex";
import { parseSendList, SendListDocument } from "./listValidation";

export interface ExcelInputRow {
  id?: string;
  variable?: string;
  rawValue?: number;
  targetType?: string;
  periodMs?: number;
  cycles?: number;
  interval?: number;
  times?: number;
  len?: number;
  increaseId?: number;
  increaseData?: number;
  obj?: string;
}

export interface GeneratedListLine {
  lineNumber: number;
  interval: number;
  text: string;
}

export interface GeneratedListResult {
  xml: string;
  document: SendListDocument;
  lines: GeneratedListLine[];
  cycles: number;
  mode: "direct" | "engineering";
}

function parseNumber(value: unknown, fallback = 0): number {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? numeric : fallback;
}

function toHexByte(value: number): string {
  return (value & 0xff).toString(16).toUpperCase().padStart(2, "0");
}

function hashString(text: string): number {
  let hash = 0;
  for (let index = 0; index < text.length; index += 1) {
    hash = (hash * 31 + text.charCodeAt(index)) >>> 0;
  }
  return hash;
}

function buildSyntheticObj(row: ExcelInputRow, groupIndex: number, frameIndex: number): string {
  const idSeed = row.id?.trim() || row.variable?.trim() || `ROW-${groupIndex}-${frameIndex}`;
  const typeSeed = row.targetType?.trim() || "Float";
  const idHash = hashString(idSeed);
  const typeHash = hashString(typeSeed);
  const valueBytes = floatToHexBytes(parseNumber(row.rawValue, 0), "little");
  const period = Math.max(0, Math.min(65535, parseNumber(row.periodMs, 100)));
  const counter = groupIndex * 100;

  const bytes = [
    toHexByte(frameIndex),
    toHexByte(groupIndex + 1),
    toHexByte(idHash & 0xff),
    toHexByte((idHash >> 8) & 0xff),
    toHexByte(period & 0xff),
    toHexByte((period >> 8) & 0xff),
    toHexByte(typeHash & 0xff),
    toHexByte((typeHash >> 8) & 0xff),
    ...valueBytes,
    ...valueBytes,
    "00",
    "00",
    "00",
    "00",
    "00",
    toHexByte(counter & 0xff),
    toHexByte((counter >> 8) & 0xff),
    toHexByte((counter >> 16) & 0xff),
  ];

  return bytes.slice(0, 24).join("");
}

export function normalizeExcelRow(raw: Record<string, unknown>): ExcelInputRow | null {
  const hasDirectListColumns =
    raw.obj !== undefined ||
    raw.iInterval !== undefined ||
    raw.iTimes !== undefined ||
    raw.len !== undefined ||
    raw.bIncreaseID !== undefined ||
    raw.bIncreaseData !== undefined;

  const id = String(raw.id ?? raw.ID ?? raw.canId ?? raw["ID"] ?? "").trim();
  const variable = String(raw.variable ?? raw["变量名"] ?? raw.name ?? raw["变量"] ?? "").trim();
  const rawValueCandidate = raw.rawValue ?? raw["原始值"] ?? raw.value ?? raw["原始值 (10进制)"];
  const targetType = String(raw.targetType ?? raw["目标类型"] ?? raw.type ?? "Float").trim();
  const periodCandidate = raw.periodMs ?? raw["周期"] ?? raw.period ?? 100;

  if (hasDirectListColumns) {
    const obj = String(raw.obj ?? raw["obj"] ?? "").trim().replace(/\s+/g, "").toUpperCase();
    if (!obj) return null;

    return {
      cycles: parseNumber(raw.cycles ?? raw.m_dwCycles ?? raw["m_dwCycles"], 1),
      interval: parseNumber(raw.iInterval ?? raw["iInterval"], 0),
      times: parseNumber(raw.iTimes ?? raw["iTimes"], 1),
      len: parseNumber(raw.len ?? raw["len"], 1),
      increaseId: parseNumber(raw.bIncreaseID ?? raw["bIncreaseID"], 0),
      increaseData: parseNumber(raw.bIncreaseData ?? raw["bIncreaseData"], 0),
      obj,
      id,
      variable,
      rawValue: Number(rawValueCandidate),
      targetType,
      periodMs: parseNumber(periodCandidate, 100),
    };
  }

  const rawValue = Number(rawValueCandidate);
  if (!id && !variable && Number.isNaN(rawValue)) return null;

  return {
    id,
    variable,
    rawValue,
    targetType,
    periodMs: parseNumber(periodCandidate, 100),
  };
}

function chunkRows(rows: ExcelInputRow[], chunkSize: number): ExcelInputRow[][] {
  const chunks: ExcelInputRow[][] = [];
  for (let index = 0; index < rows.length; index += chunkSize) {
    chunks.push(rows.slice(index, index + chunkSize));
  }
  return chunks;
}

export function generateSendList(rows: ExcelInputRow[], cycles = 1): GeneratedListResult {
  const directRows = rows.filter((row) => row.obj);
  const mode: "direct" | "engineering" = directRows.length > 0 ? "direct" : "engineering";

  const sourceRows = mode === "direct" ? directRows : rows;
  const lines: GeneratedListLine[] = [];
  const xmlLines: string[] = [`<SendList m_dwCycles="${cycles}">`];

  if (mode === "direct") {
    sourceRows.forEach((row, index) => {
      if (!row.obj) return;
      const interval = index % 4 === 3 ? parseNumber(row.periodMs, 100) : parseNumber(row.interval, 0);
      const text = `    <tagSendUint iInterval="${interval}" iTimes="${parseNumber(row.times, 1)}" len="${parseNumber(row.len, 1)}" bIncreaseID="${parseNumber(row.increaseId, 0)}" bIncreaseData="${parseNumber(row.increaseData, 0)}" obj="${row.obj}" />`;
      lines.push({ lineNumber: index + 1, interval, text });
      xmlLines.push(text);
    });
  } else {
    const orderedRows = [...sourceRows].sort((left, right) => {
      const leftKey = `${left.id ?? ""}-${left.variable ?? ""}-${left.rawValue ?? 0}`;
      const rightKey = `${right.id ?? ""}-${right.variable ?? ""}-${right.rawValue ?? 0}`;
      return leftKey.localeCompare(rightKey);
    });

    const groups = chunkRows(orderedRows, 4);
    groups.forEach((group, groupIndex) => {
      group.forEach((row, frameIndex) => {
        const interval = frameIndex === 3 ? parseNumber(row.periodMs, 100) : 0;
        const obj = buildSyntheticObj(row, groupIndex, frameIndex);
        const text = `    <tagSendUint iInterval="${interval}" iTimes="1" len="1" bIncreaseID="0" bIncreaseData="0" obj="${obj}" />`;
        const lineNumber = groupIndex * 4 + frameIndex + 1;
        lines.push({ lineNumber, interval, text });
        xmlLines.push(text);
      });
    });
  }

  xmlLines.push("</SendList>");
  const xml = xmlLines.join("\n");
  const document = parseSendList(xml);

  return {
    xml,
    document,
    lines,
    cycles,
    mode,
  };
}

export function buildPresentationWorkbook(rows: ExcelInputRow[], result: GeneratedListResult) {
  const workbook = XLSX.utils.book_new();

  const inputSheet = XLSX.utils.json_to_sheet(
    rows.map((row, index) => ({
      序号: index + 1,
      ID: row.id ?? "",
      变量名: row.variable ?? "",
      原始值: row.rawValue ?? "",
      目标类型: row.targetType ?? "",
      周期: row.periodMs ?? "",
      iInterval: row.interval ?? "",
      iTimes: row.times ?? "",
      len: row.len ?? "",
      bIncreaseID: row.increaseId ?? "",
      bIncreaseData: row.increaseData ?? "",
      obj: row.obj ?? "",
    }))
  );

  const outputSheet = XLSX.utils.json_to_sheet(
    result.lines.map((line) => ({
      行号: line.lineNumber,
      iInterval: line.interval,
      内容: line.text,
    }))
  );

  const summarySheet = XLSX.utils.json_to_sheet([
    { 项目: "生成模式", 值: result.mode === "direct" ? "直接映射" : "工程数据映射" },
    { 项目: "m_dwCycles", 值: result.cycles },
    { 项目: "输入行数", 值: rows.length },
    { 项目: "生成行数", 值: result.lines.length },
    { 项目: "校验通过行数", 值: result.document.entries.filter((entry) => entry.issues.length === 0).length },
    { 项目: "校验问题数", 值: result.document.issues.length },
  ]);

  XLSX.utils.book_append_sheet(workbook, summarySheet, "Summary");
  XLSX.utils.book_append_sheet(workbook, inputSheet, "Input");
  XLSX.utils.book_append_sheet(workbook, outputSheet, "GeneratedList");

  return workbook;
}
