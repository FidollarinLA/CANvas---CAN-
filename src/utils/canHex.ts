export type Endian = "little" | "big";

export interface RecordRow {
  id: string;
  variable: string;
  rawValue: number;
  targetType: string;
  periodMs: number;
}

export function floatToHexBytes(value: number, endian: Endian): string[] {
  const buffer = new ArrayBuffer(4);
  const view = new DataView(buffer);
  view.setFloat32(0, value, endian === "little");
  return [0, 1, 2, 3].map((i) => view.getUint8(i).toString(16).toUpperCase().padStart(2, "0"));
}

export function buildPayloadLines(rows: RecordRow[], endian: Endian, payloadBytes: number): string[] {
  const grouped = new Map<string, RecordRow[]>();

  rows.forEach((row) => {
    if (!grouped.has(row.id)) grouped.set(row.id, []);
    grouped.get(row.id)!.push(row);
  });

  const lines: string[] = [];

  grouped.forEach((groupRows, id) => {
    const period = groupRows[0]?.periodMs ?? 10;
    const byteStream = groupRows.flatMap((r) => floatToHexBytes(r.rawValue, endian));

    for (let i = 0; i < byteStream.length; i += payloadBytes) {
      const chunk = byteStream.slice(i, i + payloadBytes);
      lines.push(`${period}ms | ID: ${id} | Data: ${chunk.join(" ")}`);
    }
  });

  return lines;
}

export function normalizeRow(raw: Record<string, unknown>): RecordRow | null {
  const id = String(raw.id ?? raw.ID ?? raw.canId ?? "").trim();
  const variable = String(raw.variable ?? raw["变量名"] ?? raw.name ?? "").trim();
  const valueRaw = raw.rawValue ?? raw["原始值"] ?? raw.value ?? raw["原始值 (10进制)"];
  const targetType = String(raw.targetType ?? raw["目标类型"] ?? raw.type ?? "Float").trim();
  const periodRaw = raw.periodMs ?? raw["周期"] ?? raw.period ?? 10;

  const rawValue = Number(valueRaw);
  const periodMs = Number(periodRaw);

  if (!id || !variable || Number.isNaN(rawValue)) return null;

  return {
    id: id.startsWith("0x") ? id : `0x${id}`,
    variable,
    rawValue,
    targetType: targetType || "Float",
    periodMs: Number.isNaN(periodMs) ? 10 : periodMs,
  };
}
