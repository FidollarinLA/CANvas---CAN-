export interface SendListEntry {
  lineNumber: number;
  interval: number;
  times: number;
  len: number;
  increaseId: number;
  increaseData: number;
  obj: string;
  rawLine: string;
  issues: string[];
}

export interface SendListDocument {
  cycles: number | null;
  entries: SendListEntry[];
  issues: string[];
}

export interface ValidationSummary {
  totalEntries: number;
  validEntries: number;
  invalidEntries: number;
  groupCount: number;
  groupPatternMatched: number;
  cycles: number | null;
  issues: string[];
}

const ATTR_REGEX = /(\w+)="([^"]*)"/g;

function parseAttributes(line: string): Record<string, string> {
  const attributes: Record<string, string> = {};
  for (const match of line.matchAll(ATTR_REGEX)) {
    const [, key, value] = match;
    attributes[key] = value;
  }
  return attributes;
}

function parseNumber(value: string | undefined): number | null {
  if (value === undefined || value === "") return null;
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

function hexBytes(value: string): string[] {
  return value.match(/.{1,2}/g)?.map((part) => part.toUpperCase()) ?? [];
}

function validateObject(obj: string, issues: string[]) {
  if (!/^[0-9A-F]+$/.test(obj)) {
    issues.push("obj 必须是大写十六进制字符串");
  }

  if (obj.length % 2 !== 0) {
    issues.push("obj 长度必须为偶数");
  }

  if (obj.length !== 48) {
    issues.push(`obj 长度应为 48 个十六进制字符，当前为 ${obj.length}`);
  }
}

function validateGroup(entries: SendListEntry[], issues: string[]) {
  if (entries.length !== 4) {
    issues.push(`每组应包含 4 条 tagSendUint 记录，当前为 ${entries.length}`);
    return;
  }

  const expectedIntervals = [0, 0, 0, 100];
  entries.forEach((entry, index) => {
    if (entry.interval !== expectedIntervals[index]) {
      entry.issues.push(`iInterval 应为 ${expectedIntervals[index]}`);
    }
    if (entry.times !== 1) entry.issues.push("iTimes 应为 1");
    if (entry.len !== 1) entry.issues.push("len 应为 1");
    if (entry.increaseId !== 0) entry.issues.push("bIncreaseID 应为 0");
    if (entry.increaseData !== 0) entry.issues.push("bIncreaseData 应为 0");

    const bytes = hexBytes(entry.obj);
    const firstByte = bytes[0];
    const expectedFirstByte = String(index).padStart(2, "0").toUpperCase();
    if (firstByte !== expectedFirstByte) {
      entry.issues.push(`obj 首字节应为 ${expectedFirstByte}`);
    }
  });

  const cycleValue = parseInt(entries[0].obj.slice(-6), 16);
  if (!Number.isFinite(cycleValue)) {
    issues.push("无法解析分组计数值");
    return;
  }

  const sameCounter = entries.every((entry) => parseInt(entry.obj.slice(-6), 16) === cycleValue);
  if (!sameCounter) {
    issues.push("同组 4 条记录的 obj 末 3 字节应一致");
  }
}

export function parseSendList(content: string): SendListDocument {
  const lines = content.split(/\r?\n/);
  const issues: string[] = [];
  const entries: SendListEntry[] = [];

  const rootLine = lines.find((line) => /<SendList\b/i.test(line)) ?? "";
  const rootMatch = rootLine.match(/m_dwCycles="(\d+)"/i);
  const cycles = rootMatch ? Number(rootMatch[1]) : null;

  if (!rootLine) {
    issues.push("未找到 <SendList> 根节点");
  } else if (cycles === null || Number.isNaN(cycles)) {
    issues.push("无法解析 m_dwCycles");
  }

  lines.forEach((line, index) => {
    if (!/<tagSendUint\b/i.test(line)) return;

    const attrs = parseAttributes(line);
    const entryIssues: string[] = [];
    const interval = parseNumber(attrs.iInterval);
    const times = parseNumber(attrs.iTimes);
    const len = parseNumber(attrs.len);
    const increaseId = parseNumber(attrs.bIncreaseID);
    const increaseData = parseNumber(attrs.bIncreaseData);
    const obj = (attrs.obj ?? "").toUpperCase();

    if (interval === null) entryIssues.push("缺少或非法的 iInterval");
    if (times === null) entryIssues.push("缺少或非法的 iTimes");
    if (len === null) entryIssues.push("缺少或非法的 len");
    if (increaseId === null) entryIssues.push("缺少或非法的 bIncreaseID");
    if (increaseData === null) entryIssues.push("缺少或非法的 bIncreaseData");
    if (!obj) entryIssues.push("缺少 obj");

    if (obj) validateObject(obj, entryIssues);

    entries.push({
      lineNumber: index + 1,
      interval: interval ?? 0,
      times: times ?? 0,
      len: len ?? 0,
      increaseId: increaseId ?? 0,
      increaseData: increaseData ?? 0,
      obj,
      rawLine: line,
      issues: entryIssues,
    });
  });

  if (entries.length === 0) {
    issues.push("未找到任何 tagSendUint 记录");
  }

  for (let index = 0; index < entries.length; index += 4) {
    const group = entries.slice(index, index + 4);
    if (group.length < 4) {
      issues.push(`最后一组记录数量不足 4 条：${group.length}`);
      break;
    }
    validateGroup(group, issues);
  }

  return { cycles, entries, issues };
}

export function summarizeSendList(document: SendListDocument): ValidationSummary {
  const validEntries = document.entries.filter((entry) => entry.issues.length === 0).length;
  const invalidEntries = document.entries.length - validEntries;
  const groupCount = Math.floor(document.entries.length / 4);
  const groupPatternMatched = document.entries.reduce((count, entry) => count + (entry.issues.length === 0 ? 1 : 0), 0) / 4;

  return {
    totalEntries: document.entries.length,
    validEntries,
    invalidEntries,
    groupCount,
    groupPatternMatched,
    cycles: document.cycles,
    issues: document.issues,
  };
}
