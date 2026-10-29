// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Reads data at addresses as big-endian values (constants the code loads).
// Usage (headless postScript): ReadF.java <addr>[:<type>[:<count>]] ...
//   type: f (float, default), d (double), u32, s32, u16, u8, str
// @category gd-game-04

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class ReadF extends GhidraScript {
	@Override
	public void run() throws Exception {
		for (String arg : getScriptArgs()) {
			String[] p = arg.split(":");
			Address a = toAddr(Long.parseUnsignedLong(p[0].replaceFirst("^0x", ""), 16));
			String type = p.length > 1 ? p[1] : "f";
			int count = p.length > 2 ? Integer.parseInt(p[2]) : 1;
			for (int i = 0; i < count; i++) {
				String v;
				int size;
				switch (type) {
					case "d": v = Double.toString(Double.longBitsToDouble(getLong(a))); size = 8; break;
					case "u32": v = String.format("0x%08x (%d)", getInt(a), Integer.toUnsignedLong(getInt(a))); size = 4; break;
					case "s32": v = Integer.toString(getInt(a)); size = 4; break;
					case "u16": v = Integer.toString(getShort(a) & 0xffff); size = 2; break;
					case "u8": v = Integer.toString(getByte(a) & 0xff); size = 1; break;
					case "str": {
						StringBuilder sb = new StringBuilder();
						Address b = a;
						int c;
						while ((c = getByte(b) & 0xff) != 0 && sb.length() < 512) { sb.append((char) c); b = b.add(1); }
						v = "\"" + sb + "\""; size = sb.length() + 1; break;
					}
					default: v = Float.toString(Float.intBitsToFloat(getInt(a))) + String.format(" (0x%08x)", getInt(a)); size = 4;
				}
				println(a + "  " + type + "  " + v);
				a = a.add(size);
			}
		}
	}
}
