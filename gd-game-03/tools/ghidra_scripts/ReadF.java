// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Read data words from the program image: little-endian float, signed/unsigned
// 32-bit, and a C string guess, for constants the decompiler shows as raw loads.
// Usage (headless postScript, -readOnly -noanalysis): ReadF.java <addr>[:<count>] ...
// @category gd-game-03

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

public class ReadF extends GhidraScript {

	@Override
	public void run() throws Exception {
		Memory mem = currentProgram.getMemory();
		for (String arg : getScriptArgs()) {
			String[] p = arg.split(":");
			int count = p.length > 1 ? Integer.parseInt(p[1]) : 1;
			Address a = toAddr(Long.parseLong(p[0].startsWith("0x") ? p[0].substring(2) : p[0], 16));
			StringBuilder sb = new StringBuilder();
			for (int i = 0; i < count; i++) {
				Address x = a.add(4L * i);
				int v = mem.getInt(x);
				sb.append(String.format("%s  %08x  f=%-14s i=%-11d", x, v, Float.intBitsToFloat(v), v));
				StringBuilder cs = new StringBuilder();
				for (int k = 0; k < 64; k++) {
					int b = mem.getByte(x.add(k)) & 0xff;
					if (b == 0) {
						break;
					}
					if (b < 0x20 || b > 0x7e) {
						cs.setLength(0);
						break;
					}
					cs.append((char) b);
				}
				if (cs.length() >= 3) {
					sb.append("  \"").append(cs).append('"');
				}
				sb.append('\n');
			}
			println(sb.toString());
		}
	}
}
