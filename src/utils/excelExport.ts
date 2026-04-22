import * as XLSX from "xlsx";
import { SendListDocument } from "./listValidation";

export function buildValidationWorkbook(document: SendListDocument) {
  const summaryRows = [
    { 项目: "m_dwCycles", 值: document.cycles ?? "未解析" },
    { 项目: "总记录数", 值: document.entries.length },
    { 项目: "通过记录数", 值: document.entries.filter((entry) => entry.issues.length === 0).length },
    { 项目: "未通过记录数", 值: document.entries.filter((entry) => entry.issues.length > 0).length },
    { 项目: "总问题数", 值: document.issues.length },
  ];

  const detailRows = document.entries.map((entry, index) => {
    const counterHex = entry.obj.slice(-6);
    const counterValue = Number.parseInt(counterHex, 16);

    return {
      序号: index + 1,
      行号: entry.lineNumber,
      iInterval: entry.interval,
      iTimes: entry.times,
      len: entry.len,
      bIncreaseID: entry.increaseId,
      bIncreaseData: entry.increaseData,
      obj: entry.obj,
      obj长度: entry.obj.length,
      首字节: entry.obj.slice(0, 2),
      末三字节: counterHex,
      计数值: Number.isFinite(counterValue) ? counterValue : "",
      校验状态: entry.issues.length === 0 ? "通过" : "未通过",
      问题: entry.issues.join("；"),
      原始行: entry.rawLine,
    };
  });

  const issueRows = [
    ...document.issues.map((issue) => ({ 类型: "文档级", 内容: issue })),
    ...document.entries.flatMap((entry) =>
      entry.issues.map((issue) => ({ 类型: `第 ${entry.lineNumber} 行`, 内容: issue }))
    ),
  ];

  const workbook = XLSX.utils.book_new();
  XLSX.utils.book_append_sheet(workbook, XLSX.utils.json_to_sheet(summaryRows), "Summary");
  XLSX.utils.book_append_sheet(workbook, XLSX.utils.json_to_sheet(detailRows), "SendList");
  XLSX.utils.book_append_sheet(workbook, XLSX.utils.json_to_sheet(issueRows.length ? issueRows : [{ 类型: "说明", 内容: "校验通过，无问题" }]), "Issues");

  return workbook;
}
